#include "VisionPipeline.hpp"
#include "tools/logger.hpp"
#include <iostream>
#include <string>

VisionPipeline::VisionPipeline(const std::string& config_path)
    : tracker_(config_path)
{
    auto yaml = tools::load(config_path);
    auto use_serial = yaml["use_serial"].as<bool>();
    auto record_fps = yaml["record_fps"].as<double>();
    pixel_compensation_ = yaml["pixel_compensation"].as<double>();
    debug_ = yaml["debug"].as<bool>();
    use_trackbar_ = yaml["use_trackbar"].as<bool>();
    detector_type_ = yaml["detector"].as<std::string>();
    record_ = yaml["record"].as<bool>();
    use_roi_ = yaml["use_roi"].as<bool>();

    if (use_serial) {
        gimbal_ = std::make_unique<io::Gimbal>(config_path);
    }

    image_source_ = io::get_image_source(config_path);
    detector_ = LightDetect::load_detector(config_path);

    if (record_) {
        tools::logger()->info("open record mode");
        recorder_ = std::make_unique<tools::Recorder>(record_fps);
    }
}

bool VisionPipeline::step(FrameResult& out)
{
    if (!detector_) return false;

    image_source_->read(out.frame, out.timestamp);
    if (out.frame.empty()) {
        std::cout << "Empty frame!" << std::endl;
        return true;   // 空帧不退出，继续下一轮
    }

    out.lights = detector_->detect(out.frame, cv::Size2d(640, 480), 0, is_startup_);
    is_startup_ = false;

    //   不开ROI时为640x480缩放图，开ROI时为ROI裁剪图
    //   不是tradition检测器用原图
    out.display_frame = out.frame;
    if (detector_type_ == "tradition") {
        if (auto* td = tradDetector(); td && !td->debug_roiimg.empty()) {
            out.display_frame = td->debug_roiimg;
        }
    }

    out.target = trackAndDispatch(out.lights, out.timestamp, out.pixel_offset);
    return true;
}

void VisionPipeline::record(const cv::Mat& img, const std::chrono::steady_clock::time_point& ts)
{
    if (recorder_) {
        recorder_->record(img, Eigen::Quaterniond::Identity(), ts);
    }
}

GreenLightTracker::TrackedTarget VisionPipeline::trackAndDispatch(
    const std::vector<LightDetect::Light>& lights,
    const std::chrono::steady_clock::time_point& ts,
    float& out_offset)
{
    auto tgt = tracker_.track(lights, ts);
    out_offset = 0.0f;

    if (tgt.is_tracking) {
        out_offset = tgt.center_point.x - kHalfWidth_ + pixel_compensation_;
        if (gimbal_) {
            io::VisionToGimbal send_data;
            send_data.yaw_offset = out_offset;
            gimbal_->send(send_data);
        }
        tools::logger()->debug("Pixel Offset: {:.2f} (state={})",
                               out_offset,
                               debug_ ? GreenLightTracker::stateStr(tgt.state)
                                      : std::to_string(static_cast<int>(tgt.state)));
    } else {
        tools::logger()->debug("Tracking not active, state={}",
                               debug_ ? GreenLightTracker::stateStr(tgt.state)
                                      : std::to_string(static_cast<int>(tgt.state)));
    }
    return tgt;
}
