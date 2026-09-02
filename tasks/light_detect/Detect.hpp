//
// detector parent class
// created by cwb on 2026.1.20
//

#ifndef LIGHT_DETECT_HPP
#define LIGHT_DETECT_HPP

#include <opencv2/opencv.hpp>
#include <memory>
#include <vector>
#include <string>
#include "tools/yaml.hpp"

class LightDetect {
public:
    struct Light {
        double score = 0.;       // 置信度
        cv::Rect2d box;          // 边界框
        cv::Point2f center_point; // 中心点
        float min_axis = 0.0f;   // 最小轴
        float max_axis = 0.0f;   // 最大轴
    };
    virtual ~LightDetect() = default;
    virtual std::vector<Light> detect(cv::Mat &src, const cv::Size2d &dst_size, const int &my_color, const bool &startup) = 0;
    static std::unique_ptr<LightDetect> load_detector(const std::string &config_path);
protected:
    std::string config_path_;
};

#endif // LIGHT_DETECT_HPP
