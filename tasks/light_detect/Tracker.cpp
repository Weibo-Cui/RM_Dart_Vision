#include "tasks/light_detect/Tracker.hpp"

#include <algorithm>
#include <limits>

#include "tools/logger.hpp"
#include "tools/math_tools.hpp"
#include "tools/yaml.hpp"

GreenLightTracker::GreenLightTracker(const std::string & config_path)
{
    auto yaml = tools::load(config_path);
    const auto & t = yaml["tracker"];
    if (!t) {
        tools::logger()->warn("GreenLightTracker: 'tracker' config missing, using defaults");
        return;
    }
    tracking_thres_ = t["tracking_thres"].as<int>(tracking_thres_);
    lost_time_thres_ = t["lost_time_thres"].as<double>(lost_time_thres_);
    max_pixel_dis_ = t["max_pixel_dis"].as<double>(max_pixel_dis_);
    tools::logger()->info(
        "GreenLightTracker: tracking_thres={}, lost_time_thres={}, max_pixel_dis={}",
        tracking_thres_, lost_time_thres_, max_pixel_dis_);
}

const char * GreenLightTracker::stateStr(State s)
{
    switch (s) {
        case State::LOST:      return "LOST";
        case State::DETECTING: return "DETECTING";
        case State::TRACKING:  return "TRACKING";
        case State::TEMP_LOST: return "TEMP_LOST";
    }
    return "UNKNOWN";
}

GreenLightTracker::TrackedTarget GreenLightTracker::track(
    const std::vector<LightDetect::Light> & lights,
    const std::chrono::steady_clock::time_point & timestamp)
{
    // 乱序/重放保护:时间不前进直接沿用上次状态
    if (timestamp <= last_time_) {
        TrackedTarget tgt;
        tgt.box = last_matched_.box;
        tgt.center_point = last_matched_.center_point;
        tgt.score = last_matched_.score;
        tgt.min_axis = last_matched_.min_axis;
        tgt.max_axis = last_matched_.max_axis;
        // 只有TRACKING才向控制器下发,丢失期(TEMP_LOST/LOST/DETECTING)都不输出
        tgt.is_tracking = (state_ == State::TRACKING);
        tgt.state = state_;
        return tgt;
    }

    double dt = tools::delta_time(timestamp, last_time_);
    last_time_ = timestamp;

    bool found;
    if (state_ == State::LOST) {
        found = initTarget(lights);
    } else {
        found = updateTarget(lights);
    }
    // 即将从 TRACKING 掉到 TEMP_LOST 时,用当前 dt 折算一次丢失容忍帧数并固定,
    // 整个 TEMP_LOST 期间不再重算,避免帧率抖动导致阈值塌缩。
    if (!found && state_ == State::TRACKING) {
        lost_thres_ = std::max(1, static_cast<int>(std::abs(lost_time_thres_ / dt)));
    }
    updateFsm(found);

    TrackedTarget tgt;
    tgt.box = last_matched_.box;
    tgt.center_point = last_matched_.center_point;
    tgt.score = last_matched_.score;
    tgt.min_axis = last_matched_.min_axis;
    tgt.max_axis = last_matched_.max_axis;
    // 只有TRACKING才向控制器下发,丢失期(TEMP_LOST/LOST/DETECTING)都不输出
    tgt.is_tracking = (state_ == State::TRACKING);
    tgt.state = state_;
    return tgt;
}

bool GreenLightTracker::initTarget(const std::vector<LightDetect::Light> & lights)
{
    if (lights.empty()) return false;

    // 选 score 最大框作为新目标(score:Traditional为圆度,RKNN为检测置信度)
    int best_id = -1;
    double max_score = -std::numeric_limits<double>::max();
    for (size_t i = 0; i < lights.size(); ++i) {
        if (lights[i].score > max_score) {
            max_score = lights[i].score;
            best_id = static_cast<int>(i);
        }
    }
    if (best_id < 0) return false;

    last_matched_ = lights[best_id];
    has_last_ = true;
    return true;
}

bool GreenLightTracker::updateTarget(const std::vector<LightDetect::Light> & lights)
{
    int best_id = matchNearest(lights);
    if (best_id < 0) return false;   // 本帧未找到匹配,沿用last_matched_
    last_matched_ = lights[best_id];
    has_last_ = true;
    return true;
}

int GreenLightTracker::matchNearest(const std::vector<LightDetect::Light> & lights) const
{
    if (!has_last_ || lights.empty()) return -1;

    int best_id = -1;
    double min_error = std::numeric_limits<double>::max();
    const cv::Point2f & ref = last_matched_.center_point;

    for (size_t i = 0; i < lights.size(); ++i) {
        double dist = cv::norm(lights[i].center_point - ref);
        if (dist >= min_error) continue;
        if (dist > max_pixel_dis_) continue;
        min_error = dist;
        best_id = static_cast<int>(i);
    }
    return best_id;
}

void GreenLightTracker::updateFsm(bool found)
{
    switch (state_) {
        case State::LOST:
            if (found) {
                state_ = State::DETECTING;
                detect_count_ = 1;
            }
            break;
        case State::DETECTING:
            if (found) {
                ++detect_count_;
                if (detect_count_ > tracking_thres_) {
                    detect_count_ = 0;
                    state_ = State::TRACKING;
                }
            } else {
                detect_count_ = 0;
                state_ = State::LOST;
            }
            break;
        case State::TRACKING:
            if (!found) {
                state_ = State::TEMP_LOST;
                lost_count_ = 1;
            }
            break;
        case State::TEMP_LOST:
            if (!found) {
                ++lost_count_;
                if (lost_count_ > lost_thres_) {
                    lost_count_ = 0;
                    state_ = State::LOST;
                }
            } else {
                state_ = State::TRACKING;
                lost_count_ = 0;
            }
            break;
    }
}
