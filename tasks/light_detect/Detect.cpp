//
// detector parent class
// created by cwb on 2026.1.20
//

#include "Detect.hpp"
#include "Traditional.hpp"
#include "OpenvinoInfer.hpp"
#include "RknnInfer.hpp"
#include "tools/logger.hpp"

std::unique_ptr<LightDetect> LightDetect::load_detector(const std::string &config_path) {
    auto yaml = tools::load(config_path);
    auto detector = yaml["detector"].as<std::string>();

    if (detector == "tradition") {
        tools::logger()->info("Using Traditional detector.");
        return std::make_unique<TraditionalDetector>(config_path);
    }
    else if (detector == "yolo") {
        tools::logger()->info("Using Yolo detector.");
        return std::make_unique<OpenvinoInfer>(config_path);
    }
    else if (detector == "rknn") {
        tools::logger()->info("Using RKNN detector.");
        return std::make_unique<RknnInfer>(config_path);
    }
    else {
        tools::logger()->error("Detector no found!");
        return nullptr;
    }
}
