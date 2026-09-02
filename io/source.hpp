#pragma once

#include <chrono>
#include <string>

#include "io/camera.hpp"
#include "io/video_player.hpp"
#include "tools/yaml.hpp"

namespace io {

class ImageSource {
public:
    virtual ~ImageSource() = default;
    virtual void read(cv::Mat &img, std::chrono::steady_clock::time_point &timestamp) = 0;
};

class CameraSource : public ImageSource {
public:
    explicit CameraSource(const std::string& config_path) {
        camera_ = std::make_unique<io::Camera>(config_path);
    }

    void read(cv::Mat &img, std::chrono::steady_clock::time_point &timestamp) override {
        camera_->read(img, timestamp);
    }

private:
    std::unique_ptr<io::Camera> camera_;
};

class VideoSource : public ImageSource {
public:
    explicit VideoSource(const std::string& config_path) {
        player_ = std::make_unique<io::VideoPlayer>(config_path);
    }

    void read(cv::Mat &img, std::chrono::steady_clock::time_point &timestamp) override {
        player_->read(img, timestamp);
    }

private:
    std::unique_ptr<io::VideoPlayer> player_;
};

inline std::unique_ptr<ImageSource> get_image_source(const std::string& config_path) {
    auto yaml = tools::load(config_path);
    auto source = yaml["source"].as<std::string>();
    if (source == "video") {
        tools::logger()->info("Using VideoPlayer as image source");
        return std::make_unique<VideoSource>(config_path);
    } else if (source == "camera") {
        tools::logger()->info("Using Camera as image source");
        return std::make_unique<CameraSource>(config_path);
    } else {
        tools::logger()->error("Unknown image source type: {}", source);
        return nullptr;
    }
}

} // namespace io
