#ifndef VISION_PIPELINE_HPP
#define VISION_PIPELINE_HPP

#include "tasks/light_detect/Detect.hpp"
#include "tasks/light_detect/Traditional.hpp"
#include "tasks/light_detect/Tracker.hpp"
#include "io/source.hpp"
#include "io/gimbal/gimbal.hpp"
#include "tools/recorder.hpp"
#include "tools/yaml.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class VisionPipeline {
public:
    struct FrameResult {
        cv::Mat frame;                                       // 相机原图（供录制）
        cv::Mat display_frame;                               // 待显示帧（tradition检测器：与检测框同空间的图；其他：原图）
        std::vector<LightDetect::Light> lights;              // 检测结果
        GreenLightTracker::TrackedTarget target;             // 跟踪结果
        std::chrono::steady_clock::time_point timestamp;
        float pixel_offset = 0.0f;                           // 计算好的下发偏移
    };

    explicit VisionPipeline(const std::string& config_path);
    VisionPipeline(const VisionPipeline&) = delete;
    VisionPipeline& operator=(const VisionPipeline&) = delete;
    ~VisionPipeline() = default;

    explicit operator bool() const { return detector_ != nullptr; }

    // 单步推进：读一帧→检测→跟踪→下发，结果写入out
    bool step(FrameResult& out);

    void record(const cv::Mat& img, const std::chrono::steady_clock::time_point& ts);

    bool debug() const { return debug_; }

    bool useTrackbar() const { return use_trackbar_; }

    const std::string& detectorType() const { return detector_type_; }

    bool recordEnabled() const { return record_; }

    bool useRoi() const { return use_roi_; }

    const TraditionalDetector* tradDetector() const {
        return dynamic_cast<const TraditionalDetector*>(detector_.get());
    }
    TraditionalDetector* tradDetector() {
        return dynamic_cast<TraditionalDetector*>(detector_.get());
    }

private:
    // 跟踪+gimbal下发+offset日志
    GreenLightTracker::TrackedTarget trackAndDispatch(
        const std::vector<LightDetect::Light>& lights,
        const std::chrono::steady_clock::time_point& ts,
        float& out_offset);

    std::unique_ptr<io::ImageSource> image_source_;
    std::unique_ptr<LightDetect>     detector_;
    std::unique_ptr<io::Gimbal>      gimbal_;
    std::unique_ptr<tools::Recorder> recorder_;
    GreenLightTracker                tracker_;

    double pixel_compensation_ = 0.0;
    bool   debug_ = false;
    bool   use_trackbar_ = false;
    bool   record_ = false;
    bool   use_roi_ = false;
    bool   is_startup_ = true;
    std::string detector_type_;

    static constexpr double kHalfWidth_ = 640.0 / 2.0;   
};

#endif  // VISION_PIPELINE_HPP
