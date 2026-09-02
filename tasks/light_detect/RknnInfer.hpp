#ifndef RKNN_INFER_HPP_
#define RKNN_INFER_HPP_

#include "tasks/light_detect/Detect.hpp"
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <yaml-cpp/yaml.h>
#include "tools/yaml.hpp"

#ifdef USE_RKNN
#include "rknn_api.h"
#endif

class RknnInfer : public LightDetect {
public:
    struct Resize {
        cv::Mat resized_image;
        int dw;
        int dh;
    };

    explicit RknnInfer(const std::string &config_path);
    ~RknnInfer() override;

    static void fitRec(std::vector<RknnInfer::Light> &bboxes, cv::Size2d ori_size, cv::Size2d now_size);
    std::vector<Light> detect(cv::Mat &src, const cv::Size2d &dst_size, const int &my_color, const bool &startup) override;

private:
    static RknnInfer::Resize letterBox(cv::Mat &src, const cv::Size2d &dst_size);

    YAML::Node yaml_;
    float score_threshold = 0.f;
    float nms_threshold = 0.f;
    std::string rknn_path_;

#ifdef USE_RKNN
    rknn_context ctx_ = 0;
    rknn_input_output_num io_num_{0, 0};
    bool initialized_ = false;
    int input_h_ = 640;  // 模型实际输入H/W, 构造时查询, detect时letterBox用此而非调用方dst_size
    int input_w_ = 640;
#endif
};

#endif // RKNN_INFER_HPP_
