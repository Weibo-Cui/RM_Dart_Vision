#ifndef IO_VIDEO_PLAYER_HPP
#define IO_VIDEO_PLAYER_HPP

#include <opencv2/opencv.hpp>
#include <string>
#include <chrono>
#include <thread>
#include "tools/yaml.hpp"

namespace io {

class VideoPlayer {
public:
    explicit VideoPlayer(const std::string &config_path);
    ~VideoPlayer();
    bool read(cv::Mat &img, std::chrono::steady_clock::time_point &timestamp);
    bool isOpened() const;

private:
    cv::VideoCapture cap_;
    std::string video_path_;
    bool keep_looping_;
    double fps_;
    double play_speed_;
    std::chrono::steady_clock::time_point now_read_time_;
    std::chrono::steady_clock::time_point last_read_time_;
    
};

} // namespace io

#endif // IO_VIDEO_PLAYER_HPP