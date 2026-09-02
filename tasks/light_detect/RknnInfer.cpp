#include "RknnInfer.hpp"
#include "tools/logger.hpp"
#include <fstream>
#include <cstring>

#define BLUE 0
#define RED  1
#define NONE_ 2

#ifndef USE_RKNN

RknnInfer::RknnInfer(const std::string &config_path) {
    (void) config_path;
    tools::logger()->error("RknnInfer not built with USE_RKNN=ON. "
                           "Rebuild with -DUSE_RKNN=ON on an arm64/RK3588 target.");
    throw std::runtime_error("RknnInfer requires USE_RKNN=ON build.");
}

RknnInfer::~RknnInfer() = default;

std::vector<RknnInfer::Light> RknnInfer::detect(cv::Mat &src, const cv::Size2d &dst_size,
                                                 const int &my_color, const bool &startup) {
    (void) src; (void) dst_size; (void) my_color; (void) startup;
    return {};
}

RknnInfer::Resize RknnInfer::letterBox(cv::Mat &src, const cv::Size2d &dst_size) {
    (void) src; (void) dst_size;
    return {};
}

void RknnInfer::fitRec(std::vector<RknnInfer::Light> &bboxes, cv::Size2d ori_size, cv::Size2d now_size) {
    (void) bboxes; (void) ori_size; (void) now_size;
}

#else // USE_RKNN 

RknnInfer::RknnInfer(const std::string &config_path) {
    yaml_ = tools::load(config_path);
    rknn_path_ = tools::read<std::string>(yaml_, "rknn_path");
    score_threshold = tools::read<float>(yaml_, "score_threshold");
    nms_threshold = tools::read<float>(yaml_, "nms_threshold");

    // 读取 .rknn 模型文件
    std::ifstream ifs(rknn_path_, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        tools::logger()->error("RknnInfer: cannot open rknn model: {}", rknn_path_);
        throw std::runtime_error("RknnInfer: open rknn model failed");
    }
    size_t model_size = static_cast<size_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    std::vector<char> model_data(model_size);
    if (!ifs.read(model_data.data(), model_size)) {
        tools::logger()->error("RknnInfer: read rknn model failed: {}", rknn_path_);
        throw std::runtime_error("RknnInfer: read rknn model failed");
    }

    // 初始化 RKNN 运行时上下文
    int ret = rknn_init(&ctx_, model_data.data(), model_size, 0, nullptr);
    if (ret < 0) {
        tools::logger()->error("RknnInfer: rknn_init failed, ret={}", ret);
        throw std::runtime_error("RknnInfer: rknn_init failed");
    }
    initialized_ = true;

    // 查询输入/输出个数，用于 sanity check
    ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
    if (ret < 0) {
        tools::logger()->warn("RknnInfer: rknn_query IN_OUT_NUM failed, ret={}", ret);
    } else {
        tools::logger()->info("RknnInfer: input_num={}, output_num={}",
                              io_num_.n_input, io_num_.n_output);
    }

    // 查询第0个输入张量的属性(尺寸H/W), detect时letterBox用此,
    // 不依赖调用方传的dst_size(可能是为旧模型传的640x480等, 与本模型640x640不匹配)
    if (io_num_.n_input > 0) {
        rknn_tensor_attr in_attr;
        std::memset(&in_attr, 0, sizeof(in_attr));
        in_attr.index = 0;
        ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &in_attr, sizeof(in_attr));
        if (ret < 0) {
            tools::logger()->warn("RknnInfer: rknn_query INPUT_ATTR failed, ret={}", ret);
        } else {
            // dims 按 fmt 解释: NCHW=[1,C,H,W] 取 dims[2,3]; NHWC=[1,H,W,C] 取 dims[1,2]
            if (in_attr.n_dims >= 4) {
                if (in_attr.fmt == RKNN_TENSOR_NCHW) {
                    input_h_ = static_cast<int>(in_attr.dims[2]);
                    input_w_ = static_cast<int>(in_attr.dims[3]);
                } else {  // NHWC 或其它, 按 NHWC 取
                    input_h_ = static_cast<int>(in_attr.dims[1]);
                    input_w_ = static_cast<int>(in_attr.dims[2]);
                }
            }
            tools::logger()->info("RknnInfer: model input {}x{} (fmt={})",
                                  input_w_, input_h_, static_cast<int>(in_attr.fmt));
        }
    }
    tools::logger()->info("RknnInfer: loaded {}", rknn_path_);
}

RknnInfer::~RknnInfer() {
    if (initialized_) {
        rknn_destroy(ctx_);
        initialized_ = false;
    }
}

std::vector<RknnInfer::Light> RknnInfer::detect(cv::Mat &src, const cv::Size2d &dst_size,
                                                const int &my_color, const bool &startup) {
    (void) my_color;
    // RKNN 第一版为同步推理，没有 OpenVINO 的 CURR/NEXT 双 buffer 流水线；
    // startup 阶段也无预热需求，直接推理。
    (void) startup;

    // letterBox 到模型实际输入尺寸(从rknn查询的input_h_/input_w_), 不用调用方传的dst_size。
    // 调用方(main.cpp)传的可能是为旧模型定的640x480等, 与本模型640x640不匹配会导致
    // "param input size < model input size" 报错。
    (void) dst_size;
    cv::Size2d model_size(input_w_, input_h_);
    RknnInfer::Resize resize = RknnInfer::letterBox(src, model_size);
    // letterBox 给图加灰边保持长宽比, 框坐标是相对640x640含灰边图的。
    // main.cpp 显示用640x480图(调用方传dst_size=640x480, 等于letterBox的内容区),
    // 故还原: 减去letterBox的padding(dy/dx), 让框回到内容区(640x480)坐标。
    // x方向dx=0所以不偏(已验证x一致), y方向要减dy(之前偏低就是没减dy)。

    // BGR -> RGB: 模型训练时用RGB(与OpenvinoInfer的BGR->RGB一致),
    // RKNN的mean_values/std_values只做数值归一化不做通道转换, 必须手动转。
    // 否则模型看到通道反了的图, conf会很低(~0)。
    cv::Mat input_rgb;
    cv::cvtColor(resize.resized_image, input_rgb, cv::COLOR_BGR2RGB);

    // 填充输入：letterBox 输出是 NHWC 的 uint8 BGR，与 OpenvinoInfer 预处理后图像一致。
    // 归一化（mean/std、BGR->RGB、/255）在 RKNN 模型内通过 rknn-toolkit2 的
    // config(mean_values, std_values) 完成，这里直接喂 uint8 像素。
    rknn_input input[1];
    std::memset(input, 0, sizeof(input));
    input[0].index = 0;
    input[0].type = RKNN_TENSOR_UINT8;   // 数据类型: uint8
    input[0].size = static_cast<uint32_t>(input_rgb.total() * input_rgb.elemSize());
    input[0].fmt = RKNN_TENSOR_NHWC;     // 数据格式: NHWC (RGB)
    input[0].buf = input_rgb.data;       // 已转 RGB

    int ret = rknn_inputs_set(ctx_, 1, input);
    if (ret < 0) {
        tools::logger()->error("RknnInfer: rknn_inputs_set failed, ret={}", ret);
        return {};
    }

    ret = rknn_run(ctx_, nullptr);
    if (ret < 0) {
        tools::logger()->error("RknnInfer: rknn_run failed, ret={}", ret);
        return {};
    }

    // 取第 0 个输出。新绿灯模型是 ultralytics 单类 detect 输出 1x5xN
    // （cx,cy,w,h,conf），单类时 ultralytics 把 obj 和 class 合并成 5 维,
    // conf 既是 obj 置信度也是 green 类置信度, 不再单独有 obj+class 两个值。
    // FP RKNN（未量化）输出为 float32；want_float=1 强制 float 避免量化分支差异。
    rknn_output output;
    std::memset(&output, 0, sizeof(output));
    output.index = 0;
    output.want_float = 1;
    ret = rknn_outputs_get(ctx_, 1, &output, nullptr);
    if (ret < 0) {
        tools::logger()->error("RknnInfer: rknn_outputs_get failed, ret={}", ret);
        return {};
    }

    // 后处理: ultralytics 单类输出 shape = [1, 5, num_boxes] (通道优先 CHW-like),
    // 5 = cx,cy,w,h,conf。内存排列: outputData[c * num_boxes + box],
    // 即 [cx0..cxN, cy0..cyN, w0..wN, h0..hN, conf0..confN]。
    // (之前误按 [N,5] 行优先解析, 把相邻框的cx/cy/w/h混读, 框乱套、NMS失效)
    auto *outputData = static_cast<float *>(output.buf);
    constexpr size_t n_channels = 5;  // cx,cy,w,h,conf
    size_t n_total = (output.is_prealloc == 0)
                         ? (output.size / sizeof(float))
                         : (output.size);
    size_t num_boxes = n_total / n_channels;

    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;
    std::vector<RknnInfer::Light> Lights;
    std::vector<int> nms_result;

    // 通道优先解析: 第c通道起始 = outputData + c * num_boxes
    const float *cx_ptr = outputData + 0 * num_boxes;
    const float *cy_ptr = outputData + 1 * num_boxes;
    const float *w_ptr  = outputData + 2 * num_boxes;
    const float *h_ptr  = outputData + 3 * num_boxes;
    const float *cf_ptr = outputData + 4 * num_boxes;

    for (size_t i = 0; i < num_boxes; ++i) {
        float conf = cf_ptr[i];
        if (conf < score_threshold) {
            continue;
        }
        // 框坐标在640x640含灰边图里, 减去letterBox padding还原到内容区(640x480显示图)坐标
        float bx = cx_ptr[i] - resize.dw;
        float by = cy_ptr[i] - resize.dh;
        float bw = w_ptr[i], bh = h_ptr[i];

        RknnInfer::Light Light;
        Light.score = conf;
        Light.center_point = cv::Point2f(bx, by);
        Light.box = cv::Rect2d(bx - bw / 2., by - bh / 2., bw, bh);
        Lights.emplace_back(Light);
        confidences.emplace_back(conf);
        boxes.emplace_back(cv::Rect(bx - bw / 2., by - bh / 2., bw, bh));
    }

    cv::dnn::NMSBoxes(boxes, confidences, score_threshold, nms_threshold, nms_result);
    std::vector<RknnInfer::Light> result;
    for (const int &idx : nms_result) {
        result.emplace_back(RknnInfer::Light{
            Lights[idx].score, Lights[idx].box, Lights[idx].center_point});
    }

    // RKNN 输出需手动释放（除非 is_prealloc）
    if (output.is_prealloc == 0) {
        rknn_outputs_release(ctx_, 1, &output);
    }

    // letterBox 后处理坐标回到原图基准，使与 Traditional 后端坐标系一致，
    // 保证跟踪器在多后端切换时坐标基准统一。
    fitRec(result, src.size(), dst_size);
    return result;
}

// letterBox / fitRec 与 OpenvinoInfer 等价（复制一份，不动 OpenvinoInfer）。
RknnInfer::Resize RknnInfer::letterBox(cv::Mat &src, const cv::Size2d &dst_size) {
    int w = src.cols;
    int h = src.rows;

    double r_w = (double) dst_size.width / w;
    double r_h = (double) dst_size.height / h;
    double r = std::min(r_w, r_h);

    int new_w = (int) (w * r);
    int new_h = (int) (h * r);

    cv::Mat resized_image;
    cv::resize(src, resized_image, cv::Size(new_w, new_h), cv::INTER_CUBIC);

    cv::Mat canvas(dst_size, CV_8UC3, cv::Scalar(128, 128, 128));

    int dx = (int) ((dst_size.width - new_w) / 2.);
    int dy = (int) ((dst_size.height - new_h) / 2.);

    resized_image.copyTo(canvas(cv::Rect(dx, dy, new_w, new_h)));
    return {canvas, dx, dy};
}

void RknnInfer::fitRec(std::vector<RknnInfer::Light> &bboxes, cv::Size2d ori_size, cv::Size2d now_size) {
    double scale = std::max((double) ori_size.width / now_size.width,
                            (double) ori_size.height / now_size.height);

    for (auto &bbox : bboxes) {
        bbox.box.x = (bbox.box.x - now_size.width / 2) * scale + ori_size.width / 2;
        bbox.box.y = (bbox.box.y - now_size.height / 2) * scale + ori_size.height / 2;
        bbox.box.width *= scale;
        bbox.box.height *= scale;
        bbox.center_point.x = (bbox.center_point.x - now_size.width / 2) * scale + ori_size.width / 2;
        bbox.center_point.y = (bbox.center_point.y - now_size.height / 2) * scale + ori_size.height / 2;
    }
}

#endif // USE_RKNN
