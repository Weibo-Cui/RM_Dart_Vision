#ifndef TRACKER__TRACKER_HPP
#define TRACKER__TRACKER_HPP

#include <chrono>
#include <vector>

#include "tasks/light_detect/Detect.hpp"

class GreenLightTracker
{
public:
    enum class State
    {
        LOST,       
        DETECTING,  // 连续命中tracking_thres帧才进TRACKING
        TRACKING,   // 稳定跟踪
        TEMP_LOST,  // 短暂丢失,沿用last_matched保持,超时回LOST
    };

    struct TrackedTarget
    {
        cv::Rect2d box;
        cv::Point2f center_point;
        double score = 0.;
        float min_axis = 0.0f;
        float max_axis = 0.0f;
        bool is_tracking = false;   // TRACKING/TEMP_LOST 为 true(可下发),其余 false
        State state = State::LOST;
    };

    explicit GreenLightTracker(const std::string & config_path);
    ~GreenLightTracker() = default;

    // 用一帧检测结果更新跟踪器,返回当前目标(单帧原样数据)。
    TrackedTarget track(const std::vector<LightDetect::Light> & lights,
                        const std::chrono::steady_clock::time_point & timestamp);

    // FSM 状态的可读名称(LOST/DETECTING/TRACKING/TEMP_LOST),用于可视化
    static const char * stateStr(State s);

    std::chrono::steady_clock::time_point getLastTime() const { return last_time_; }

private:
    bool initTarget(const std::vector<LightDetect::Light> & lights);
    bool updateTarget(const std::vector<LightDetect::Light> & lights);
    void updateFsm(bool found);

    // 与上次锁定目标的最近邻匹配(像素距离门控),返回命中索引,无命中返回 -1。
    int matchNearest(const std::vector<LightDetect::Light> & lights) const;

    State state_ = State::LOST;
    int detect_count_ = 0;
    int lost_count_ = 0;                    // TEMP_LOST 连续丢检帧数

    LightDetect::Light last_matched_;      // 最近一次成功匹配的检测框,丢失期沿用
    bool has_last_ = false;                // last_matched_ 是否有效

    std::chrono::steady_clock::time_point last_time_;   // 上一帧时间戳,用于算 dt 与乱序保护
    int lost_thres_ = 30;               // TEMP_LOST 容忍帧数,进入 TEMP_LOST 时按 dt 折算一次固定

    // 参数
    int tracking_thres_ = 5;
    double lost_time_thres_ = 1.0;          // 秒
    double max_pixel_dis_ = 120.0;          // 像素距离门控
};

#endif  // TRACKER__TRACKER_HPP
