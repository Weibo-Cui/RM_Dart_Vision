#include "tasks/light_detect/VisionPipeline.hpp"
#include "tasks/light_detect/Visualizer.hpp"
#include "tools/yaml.hpp"
#include <chrono>
#include <string>

int main() {
    std::string config_path = "configs/test.yaml";
    auto yaml = tools::load(config_path);
    bool debug_ = yaml["debug"].as<bool>();
    bool use_trackbar = yaml["use_trackbar"].as<bool>();
    auto detector_type = yaml["detector"].as<std::string>();
    bool record = yaml["record"].as<bool>();

    VisionPipeline pipeline(config_path);
    if (!pipeline) return -1;   // detector 加载失败

    Visualizer viz(8080, config_path);
    viz.setupWindows(debug_, use_trackbar, detector_type, pipeline.tradDetector());

    auto last = std::chrono::steady_clock::now();
    while (true) {
        VisionPipeline::FrameResult r;
        if (!pipeline.step(r)) break;

        if (record) pipeline.record(r.frame, r.timestamp);

        if (debug_) {
            auto now = std::chrono::steady_clock::now();
            double latency_ms = std::chrono::duration<double, std::milli>(now - last).count();
            last = now;

            viz.show(r.display_frame, r.lights, r.target, r.pixel_offset, latency_ms);

            if (detector_type == "tradition" && use_trackbar) {
                if (const auto* td = pipeline.tradDetector()) {
                    viz.showBinary(td->debug_birimg);
                }
            }

            int key = viz.waitKey(1);
            if (key == 'q' || key == 27) {   // q或ESC退出
                break;
            }
        } else {
            last = std::chrono::steady_clock::now();
        }
    }
    viz.destroy();
    return 0;
}
