#ifndef VISUALIZER_HPP
#define VISUALIZER_HPP

#include "tasks/light_detect/Detect.hpp"
#include "tasks/light_detect/Traditional.hpp"
#include "tasks/light_detect/Tracker.hpp"
#include "tools/web_viewer.hpp"
#include "tools/yaml.hpp"
#include <string>
#include <vector>

class Visualizer {
public:
    Visualizer(int port, const std::string& config_path)
        : viewer_(port)
    {
        auto yaml = tools::load(config_path);
        debug_ = yaml["debug"].as<bool>();
    }

    void setupWindows(bool debug, bool use_trackbar, const std::string& detector_type,
                      TraditionalDetector* trad_detector)
    {
        if (!debug) return;
        viewer_.namedWindow("Camera Image");
        if (detector_type == "tradition" && use_trackbar && trad_detector) {
            viewer_.namedWindow("Binary Image");
            trad_detector->createTrackbars(&viewer_);
        }
    }

    void show(const cv::Mat& frame, const std::vector<LightDetect::Light>& lights,
              const GreenLightTracker::TrackedTarget& tgt, float pixel_offset,
              double latency_ms)
    {
        constexpr int kCanvasW = 640, kCanvasH = 480;
        cv::Mat display_img = cv::Mat::zeros(kCanvasH, kCanvasW, CV_8UC3);  // 黑色底

        double scale = std::min((double)kCanvasW / frame.cols, (double)kCanvasH / frame.rows);
        int dst_w = static_cast<int>(frame.cols * scale);
        int dst_h = static_cast<int>(frame.rows * scale);
        int off_x = (kCanvasW - dst_w) / 2;
        int off_y = (kCanvasH - dst_h) / 2;
        cv::Mat scaled;
        cv::resize(frame, scaled, cv::Size(dst_w, dst_h));
        scaled.copyTo(display_img(cv::Rect(off_x, off_y, dst_w, dst_h)));

        auto map_x = [&](double x) { return x * scale + off_x; };
        auto map_y = [&](double y) { return y * scale + off_y; };

        // 检测框(绿) + 中心(蓝) + 尺寸文本
        for (const auto& light : lights) {
            cv::Rect2d box(map_x(light.box.x), map_y(light.box.y),
                           light.box.width * scale, light.box.height * scale);
            cv::rectangle(display_img, box, cv::Scalar(0, 255, 0), 2);
            cv::Point center(map_x(light.center_point.x), map_y(light.center_point.y));
            cv::circle(display_img, center, 5, cv::Scalar(255, 0, 0), -1);
            std::string axis_str = cv::format("L*W: %.2f, %.2f", light.max_axis, light.min_axis);
            cv::putText(display_img, axis_str, center, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 255), 1);
        }

        // 跟踪框(黄) + 跟踪中心(红)
        cv::Rect2d tgt_box(map_x(tgt.box.x), map_y(tgt.box.y),
                           tgt.box.width * scale, tgt.box.height * scale);
        cv::rectangle(display_img, tgt_box, cv::Scalar(0, 255, 255), 3);
        cv::Point tgt_center(map_x(tgt.center_point.x), map_y(tgt.center_point.y));
        cv::circle(display_img, tgt_center, 7, cv::Scalar(0, 0, 255), -1);

        // offset / state 文本
        std::string offset_str = cv::format("Pixel Offset: %.2f", pixel_offset);
        cv::putText(display_img, offset_str, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
        std::string state_str = cv::format("%s", GreenLightTracker::stateStr(tgt.state));
        cv::putText(display_img, state_str, cv::Point(10, 55), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 255), 2);

        // Latency(ms)
        std::string lat_str = cv::format("Latency: %.1f ms", latency_ms);
        int baseLine = 0;
        cv::Size ts = cv::getTextSize(lat_str, cv::FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseLine);
        cv::putText(display_img, lat_str,
                    cv::Point(display_img.cols - ts.width - 10, ts.height + 10),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 255), 2);

        // 中线（画布中心）
        cv::line(display_img, cv::Point(kCanvasW / 2, 0), cv::Point(kCanvasW / 2, kCanvasH), cv::Scalar(0, 0, 255), 2);
        viewer_.imshow("Camera Image", display_img);
    }

    void showBinary(const cv::Mat& bir_img)
    {
        if (bir_img.empty()) return;
        constexpr int kCanvasW = 640, kCanvasH = 480;
        cv::Mat display_img = cv::Mat::zeros(kCanvasH, kCanvasW, CV_8UC3);

        cv::Mat bir3;
        if (bir_img.channels() == 1) {
            cv::cvtColor(bir_img, bir3, cv::COLOR_GRAY2BGR);
        } else {
            bir3 = bir_img;
        }
        double scale = std::min((double)kCanvasW / bir3.cols, (double)kCanvasH / bir3.rows);
        int dst_w = static_cast<int>(bir3.cols * scale);
        int dst_h = static_cast<int>(bir3.rows * scale);
        int off_x = (kCanvasW - dst_w) / 2;
        int off_y = (kCanvasH - dst_h) / 2;
        cv::Mat scaled;
        cv::resize(bir3, scaled, cv::Size(dst_w, dst_h));
        scaled.copyTo(display_img(cv::Rect(off_x, off_y, dst_w, dst_h)));
        viewer_.imshow("Binary Image", display_img);
    }

    int waitKey(int delay_ms = 5) { return viewer_.waitKey(delay_ms); }
    void destroy() { viewer_.destroyAllWindows(); }
    tools::WebViewer& viewer() { return viewer_; }
    bool debug() const { return debug_; }

private:
    tools::WebViewer viewer_;
    bool debug_ = false;
};

#endif  // VISUALIZER_HPP
