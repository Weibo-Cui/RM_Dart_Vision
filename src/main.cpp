#include "tasks/light_detect/Detect.hpp"
#include "tasks/light_detect/Traditional.hpp"
#include "tasks/tracker/Tracker.hpp"
#include "io/source.hpp"
#include "io/gimbal/gimbal.hpp"
#include "tools/yaml.hpp"
#include "tools/recorder.hpp"
#include "tools/web_viewer.hpp"
#include <cmath>
#include <chrono>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string config_path = "configs/test.yaml";
    auto yaml = tools::load(config_path);
    auto debug_= yaml["debug"].as<bool>();
    auto use_trackbar = yaml["use_trackbar"].as<bool>();
    auto detector_type = yaml["detector"].as<std::string>(); 
    auto record = yaml["record"].as<bool>();
    auto record_fps = yaml["record_fps"].as<double>();
    auto pixel_compensation = yaml["pixel_compensation"].as<double>();
    auto use_serial = yaml["use_serial"].as<bool>();

    std::unique_ptr<io::Gimbal> gimbal;
    if (use_serial) {gimbal = std::make_unique<io::Gimbal>(config_path);}
    tools::WebViewer viewer(8080);

    std::unique_ptr<io::ImageSource> image_source = io::get_image_source(config_path);
    std::unique_ptr<tools::Recorder> recorder;
    std::unique_ptr<LightDetect> detector = LightDetect::load_detector(config_path);
    GreenLightTracker tracker(config_path);
 
    if (!detector) return -1;
    if (record){
        tools::logger()->info("open record mode");
        recorder = std::make_unique<tools::Recorder>(record_fps);
    }
    if (debug_){
        viewer.namedWindow("Camera Image");
        if (detector_type == "tradition" && use_trackbar){
            auto* trad_detector = dynamic_cast<TraditionalDetector*>(detector.get());
            viewer.namedWindow("Binary Image");
            trad_detector->createTrackbars(&viewer);
        }
    }

    cv::Mat img;
    bool is_startup = true;

    // FPS计算
    constexpr int FPS_WINDOW = 30;
    double fps_ema = 0.0;
    auto fps_last = std::chrono::steady_clock::now();

    while (true) {
        std::chrono::steady_clock::time_point timestamp;
        image_source->read(img, timestamp);      
        if (img.empty()) {
            std::cout << "Empty frame!" << std::endl;
            continue;
        }
        if (record){
            recorder->record(img, Eigen::Quaterniond::Identity(), timestamp);
        }
        auto lights = detector->detect(img, cv::Size2d(640, 480), 0, is_startup);
        is_startup = false; 

        if(debug_){
            cv::Mat display_img;
            cv::resize(img, display_img, cv::Size2d(640, 480));

            for (const auto &light : lights) {
                cv::rectangle(display_img, light.box, cv::Scalar(0, 255, 0), 2);
                cv::circle(display_img, light.center_point, 5, cv::Scalar(255, 0, 0), -1);
                std::string axis_str = cv::format("L*W: %.2f, %.2f", light.max_axis, light.min_axis);
                cv::Point text_pos(light.center_point.x, light.center_point.y);
                cv::putText(display_img, axis_str, text_pos, cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 255), 1);
            }

            auto tgt = tracker.track(lights, timestamp);

            // 跟踪框(黄)与跟踪中心(红)
            cv::rectangle(display_img, tgt.box, cv::Scalar(0, 255, 255), 3);
            cv::circle(display_img, tgt.center_point, 7, cv::Scalar(0, 0, 255), -1);

            // 仅TRACKING/TEMP_LOST下发
            float pixel_offset = 0.0f;
            if (tgt.is_tracking) {
                pixel_offset = tgt.center_point.x - (640 / 2.0f) + pixel_compensation;
                io::VisionToGimbal send_data;  //serial
                send_data.yaw_offset = pixel_offset; //serial
                if (gimbal) gimbal->send(send_data); //serial
                tools::logger()->debug("Pixel Offset: {:.2f} (state={})",
                                      pixel_offset, GreenLightTracker::stateStr(tgt.state));
            } else {
                tools::logger()->debug("Tracking not active, state={}",
                                      GreenLightTracker::stateStr(tgt.state));
            }

            std::string offset_str = cv::format("Pixel Offset: %.2f", pixel_offset);
            cv::putText(display_img, offset_str, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
            std::string state_str = cv::format("%s", GreenLightTracker::stateStr(tgt.state));
            cv::putText(display_img, state_str, cv::Point(10, 55), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 255), 2);
            // FPS
            auto fps_now = std::chrono::steady_clock::now();
            double dt_ms = std::chrono::duration<double, std::milli>(fps_now - fps_last).count();
            fps_last = fps_now;
            double inst_fps = dt_ms > 0 ? 1000.0 / dt_ms : 0;
            fps_ema = fps_ema > 0 ? (fps_ema * 0.9 + inst_fps * 0.1) : inst_fps;
            std::string fps_str = cv::format("FPS: %.1f", fps_ema);
            int baseLine = 0;
            cv::Size ts = cv::getTextSize(fps_str, cv::FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseLine);
            cv::putText(display_img, fps_str,
                        cv::Point(display_img.cols - ts.width - 10, ts.height + 10),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);

            cv::line(display_img, cv::Point(320, 0), cv::Point(320, 480), cv::Scalar(0, 0, 255), 2);
            viewer.imshow("Camera Image", display_img);
            if (detector_type == "tradition" && use_trackbar) {
                auto* trad_detector = dynamic_cast<TraditionalDetector*>(detector.get());
                cv::Mat bir_img = trad_detector->debug_birimg;
                if (!bir_img.empty()) {
                    cv::resize(bir_img, bir_img, cv::Size(640, 480)); 
                    viewer.imshow("Binary Image", bir_img);
                }
            }
            int key = viewer.waitKey(5); 
            if (key == 'q' || key == 27) { // q或ESC退出
                break;
            }
        } else {
            // 仅TRACKING/TEMP_LOST下发
            auto tgt = tracker.track(lights, timestamp);
            if (tgt.is_tracking) {
                float pixel_offset = tgt.center_point.x - (640 / 2.0f) + pixel_compensation;
                io::VisionToGimbal send_data; //serial
                send_data.yaw_offset = pixel_offset; //serial
                if (gimbal) gimbal->send(send_data); //serial
                tools::logger()->debug("Pixel Offset: {:.2f} (state={})",
                                      pixel_offset, static_cast<int>(tgt.state));
            }
            else {
                tools::logger()->debug("Tracking not active, state={}",
                                      static_cast<int>(tgt.state));
            }
        }
    }
    viewer.destroyAllWindows();
    return 0;
}