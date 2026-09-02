#include "io/video_player.hpp"
#include "tools/logger.hpp" 
#include <iostream>

namespace io {

VideoPlayer::VideoPlayer(const std::string &config_path) {
    // 加载 yaml 配置
    auto yaml = tools::load(config_path);    
    video_path_ = yaml["video"]["path"].as<std::string>();
    keep_looping_ = yaml["video"]["keep_looping"].as<bool>();
    play_speed_ = yaml["video"]["play_speed"].as<double>();

    // 打开视频文件
    cap_.open(video_path_);
    if (!cap_.isOpened()) {
        tools::logger()->error("Failed to open video file: " + video_path_);
    } else {
        tools::logger()->info("Successfully opened video: " + video_path_);
        fps_ = cap_.get(cv::CAP_PROP_FPS);
        if (fps_ <= 0.0) fps_ = 30.0; 
        last_read_time_ = std::chrono::steady_clock::now();
    }
}

VideoPlayer::~VideoPlayer() {if (cap_.isOpened()) {cap_.release();}}

bool VideoPlayer::read(cv::Mat &img, std::chrono::steady_clock::time_point &timestamp) {
    if (!cap_.isOpened()) return false;

    now_read_time_ = std::chrono::steady_clock::now();
    double expected_delay_ms = 1000.0 / (fps_ * play_speed_);
    double elapsed_ms = std::chrono::duration<double, std::milli>(now_read_time_ - last_read_time_).count();
    
    if (elapsed_ms < expected_delay_ms) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(expected_delay_ms - elapsed_ms))
        );
    }

    cap_ >> img; // 读取一帧
    // 如果读到的帧为空，说明视频播放到了末尾
    if (img.empty()) {
        if (keep_looping_) {
            cap_.release();
            cap_.open(video_path_);   
            cap_ >> img;         
        } else {
            return false;
        }
    }
    timestamp = std::chrono::steady_clock::now();
    last_read_time_ = timestamp;
    return true;
}

bool VideoPlayer::isOpened() const {return cap_.isOpened();}

} // namespace io