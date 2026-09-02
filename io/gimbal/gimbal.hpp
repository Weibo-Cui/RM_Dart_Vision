#ifndef IO__GIMBAL_HPP
#define IO__GIMBAL_HPP

#include <Eigen/Geometry>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>

#include "serial/serial.h"
#include "tools/thread_safe_queue.hpp"

namespace io
{
struct __attribute__((packed)) GimbalToVision
{
  uint8_t head[2] = {'E', 'C'};
  uint8_t state;
  uint16_t crc16;
};

static_assert(sizeof(GimbalToVision) == 5);
struct __attribute__((packed)) VisionToGimbal
{
  uint8_t head[2] = {'V', 'C'};
  float yaw_offset;
  uint16_t crc16;
};
static_assert(sizeof(VisionToGimbal) == 8);

enum class GimbalMode
{
  ERROR,       // 对应电控00（错误）
  RUNNING,     // 对应电控01（开始）
  IDLE         // 视觉本地空闲状态（用于初始化）
};

class Gimbal
{
public:
  Gimbal(const std::string & config_path);

  ~Gimbal();

  // 不可拷贝（含 std::thread / std::mutex / std::atomic 成员）
  Gimbal(const Gimbal &) = delete;
  Gimbal & operator=(const Gimbal &) = delete;

  GimbalMode mode() const;
  std::string str(GimbalMode mode) const;

  void send(io::VisionToGimbal VisionToGimbal);

private:
  serial::Serial serial_;
  int baud_rate_ = 0;        // 保存配置，重连 open 后需重新下发
  std::string com_port_;     // 配置的端口名（软链接）

  std::thread thread_;
  std::atomic<bool> quit_ = false;
  // 重连进行中置 true：send() 静默跳过，避免每帧刷一条 PortNotOpenedException
  std::atomic<bool> reconnecting_ = false;
  // 重连彻底失败（10 次仍打不开）置 true：send() 不再发，read_thread 继续兜底重试
  std::atomic<bool> serial_dead_ = false;
  mutable std::mutex mutex_;
  // 保护 serial_ 的 open/close/write 之间的互斥，避免重连关串口时主线程正在写
  mutable std::mutex serial_mutex_;

  GimbalToVision rx_data_;
  VisionToGimbal tx_data_;

  GimbalMode mode_ = GimbalMode::IDLE;

  int reconnect_max_retry_count_ = 10;
  std::chrono::milliseconds reconnect_timeout_{5000};
  std::chrono::milliseconds slow_packet_threshold_{200};
  std::chrono::microseconds idle_read_sleep_{1000};

  // 重连期间累计丢弃的待发帧数与起始时间，用于周期性告警"已断连 N 帧/N ms"
  std::atomic<uint64_t> dropped_frames_{0};
  std::chrono::steady_clock::time_point reconnect_start_{};

  bool read(uint8_t * buffer, size_t size);
  void read_thread();
  void reconnect();

  // 统一下发波特率/超时配置：open 后必须调用，serial 库 close→open 会丢失这些设置
  void apply_serial_config();

  // 诊断并打印当前串口设备节点的真实状态（软链接指向、是否可访问），
  // 用于区分 "STM32 复位导致设备重新枚举" 与 "线缆/转接模块掉线"。
  void diagnose_port(const std::string & port) const;

  // 重连时若配置端口（软链接）失效，扫描 /dev/ttyUSB* / /dev/ttyACM* 找一个能打开的设备回退使用。
  // 成功打开后会调用 apply_serial_config() 重新下发波特率与超时。返回成功打开所用的端口名，失败返回空串。
  std::string try_open_with_fallback(const std::string & configured_port);

  // 当前已打开的端口名（重连成功后可能从软链接回退到真实 tty 节点）。
  std::string active_port_;

  // 字节级帧对齐缓冲：CH340 重连后串口常有半帧残留，在本地缓冲里滑动找帧头 'E''C'，
  // 对齐后再凑齐一帧。避免逐字节阻塞 read 拖垮主线程 send。
  std::vector<uint8_t> rx_buf_;
};

}  // namespace io

#endif  // IO__GIMBAL_HPP
