#ifndef OPENVINO_INFER_HPP_
#define OPENVINO_INFER_HPP_
#include "tasks/light_detect/Detect.hpp"
#include <openvino/openvino.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <yaml-cpp/yaml.h>
#include "tools/yaml.hpp"

class OpenvinoInfer : public LightDetect {
public:
    struct Resize {
        cv::Mat resized_image;
        int dw;
        int dh;
    };

    explicit OpenvinoInfer(const std::string &config_path);
    ~OpenvinoInfer() override = default;

    static void fitRec(std::vector<OpenvinoInfer::Light> &bboxes, cv::Size2d ori_size, cv::Size2d now_size);
    std::vector<Light> detect(cv::Mat &src, const cv::Size2d &dst_size, const int &my_color, const bool &startup)override;

private:
    static OpenvinoInfer::Resize letterBox(cv::Mat &src, const cv::Size2d &dst_size);

    ov::Core core_;
    ov::CompiledModel compiled_model_;
    ov::CompiledModel compiled_model_next_;
    std::shared_ptr<ov::Model> model_;
    std::map<bool, ov::InferRequest> infer_requests_;
    YAML::Node yaml_;
    float score_threshold;
    float nms_threshold;
};



#endif