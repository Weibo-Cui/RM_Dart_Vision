#include "gimbal.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>

#include "tools/crc.hpp"
#include "tools/logger.hpp"
#include "tools/math_tools.hpp"
#include "tools/yaml.hpp"

namespace io
{
Gimbal::Gimbal(const std::string & config_path)
{
  auto yaml = tools::load(config_path);
  com_port_ = tools::read<std::string>(yaml, "com_port");
  baud_rate_ = tools::read<int>(yaml, "baud_rate");

  try {
    serial_.setPort(com_port_);
    serial_.open();
    apply_serial_config();
    active_port_ = com_port_;
  } catch (const std::exception & e) {
    tools::logger()->error("[Gimbal] Failed to open serial: {}", e.what());
    exit(1);
  }


  thread_ = std::thread(&Gimbal::read_thread, this);
  tools::logger()->info("[Gimbal] Serial port opened, read thread started.");
}

Gimbal::~Gimbal()
{
  quit_ = true;
  if (thread_.joinable()) thread_.join();
  serial_.close();
}

GimbalMode Gimbal::mode() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return mode_;
}

std::string Gimbal::str(GimbalMode mode) const
{
  switch (mode) {
    case GimbalMode::IDLE:
      return "IDLE";
    case GimbalMode::ERROR:
      return "ERROR";
    case GimbalMode::RUNNING:
      return "RUNNING";
    default:
      return "INVALID";
  }
}

void Gimbal::apply_serial_config()
{
  // serial 库 close→open 会丢失波特率与超时设置，每次 open 后必须重新下发。
  serial_.setBaudrate(baud_rate_);
  serial::Timeout timeout = serial::Timeout::simpleTimeout(20);  //可能需要调整
  serial_.setTimeout(timeout);
}

void Gimbal::send(io::VisionToGimbal vision_to_gimbal)
{
  tx_data_.yaw_offset = vision_to_gimbal.yaw_offset;
  tx_data_.crc16 = tools::get_crc16(
    reinterpret_cast<uint8_t *>(&tx_data_),
    sizeof(tx_data_) - sizeof(tx_data_.crc16)
  );

  // 串口彻底断了（10 次重连失败）：不发送，等看门狗重启进程
  if (serial_dead_.load(std::memory_order_relaxed)) {
    return;
  }
  // 重连进行中：丢帧，但累计计数并周期性告警，避免"静默哑火"不可见
  if (reconnecting_.load(std::memory_order_relaxed)) {
    uint64_t dropped = dropped_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
    // 每 300 帧（约 10 秒@30fps）告警一次，提示已断连多久
    if (dropped == 1 || dropped % 300 == 0) {
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - reconnect_start_).count();
      tools::logger()->warn("[Gimbal] Serial reconnecting, dropped {} frames ({} ms so far).",
                            dropped, ms);
    }
    return;
  }

  std::lock_guard<std::mutex> lock(serial_mutex_);
  if (!serial_.isOpen()) {
    return;
  }
  try {
    serial_.write(reinterpret_cast<uint8_t *>(&tx_data_), sizeof(tx_data_));
  } catch (const std::exception & e) {
    tools::logger()->warn("[Gimbal] Failed to write serial: {}", e.what());
  }
}

bool Gimbal::read(uint8_t * buffer, size_t size)
{
  try {
    return serial_.read(buffer, size) == size;
  } catch (const std::exception & e) {
    return false;
  }
}

void Gimbal::read_thread()
{
  tools::logger()->info("[Gimbal] read_thread started.");
  int error_count = 0;
  // 连续无数据的轮次；用于在空闲时退避，减少 CPU 占用
  int idle_streak = 0;

  while (!quit_) {
    if (error_count > 5000) {
      error_count = 0;
      tools::logger()->warn("[Gimbal] Too many errors, attempting to reconnect...");
      diagnose_port(active_port_);
      reconnect();
      continue;
    }

    // 重连期间不读，避免在 close 的串口上 read 抛异常刷屏
    if (reconnecting_.load(std::memory_order_relaxed) ||
        serial_dead_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::microseconds(idle_read_sleep_));
      continue;
    }

    // 用 available() 查可读字节，无数据则短暂退避，绝不阻塞在 read 上等数据——
    // 否则会持锁卡住主线程 send()，把帧率拖到 0.1FPS。
    size_t avail = 0;
    try {
      avail = serial_.available();
    } catch (const std::exception &) {
      error_count++;
      std::this_thread::sleep_for(std::chrono::microseconds(idle_read_sleep_));
      continue;
    }
    if (avail == 0) {
      idle_streak++;
      // 最多读到 5 字节就成帧，少量 sleep 即可；指数式退避上限 1ms
      std::this_thread::sleep_for(
        std::chrono::microseconds(std::min(100 + idle_streak * 50, 1000)));
      continue;
    }
    idle_streak = 0;

    // 批量读：有多少读多少，追加到本地缓冲。注意：此处不持 serial_mutex_，
    // 因为 serial::Serial 内部线程安全，read 与 write 可并发；
    // 重连的 close/open 由 reconnecting_ 标志让本线程跳过、send() 跳过，无需长持锁。
    std::vector<uint8_t> chunk(avail);
    size_t got = 0;
    try {
      got = serial_.read(chunk.data(), avail);
    } catch (const std::exception &) {
      error_count++;
      continue;
    }
    rx_buf_.insert(rx_buf_.end(), chunk.begin(), chunk.begin() + got);

    // 在本地缓冲里滑动找帧头 'E','C'，对齐后凑齐一帧。
    // 这样 CH340 重连后缓冲区里的半帧残留能被滑过对齐到下一帧，不会因错位反复重连。
    constexpr size_t FRAME_SIZE = sizeof(GimbalToVision);  // 5
    while (rx_buf_.size() >= FRAME_SIZE) {
      // 找帧头
      size_t head_pos = 0;
      bool found = false;
      for (; head_pos + 1 < rx_buf_.size(); ++head_pos) {
        if (rx_buf_[head_pos] == 'E' && rx_buf_[head_pos + 1] == 'C') {
          found = true;
          break;
        }
      }
      if (!found) {
        // 保留最后一个字节（可能是 'E' 的前半），其余丢弃
        uint8_t last = rx_buf_.back();
        rx_buf_.clear();
        rx_buf_.push_back(last);
        error_count++;
        break;
      }
      if (head_pos > 0) {
        // 丢弃帧头前的杂字节
        rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + head_pos);
      }
      if (rx_buf_.size() < FRAME_SIZE) {
        // 帧头对齐了但还不够一帧，等下一批数据
        break;
      }

      // 取一帧
      std::memcpy(&rx_data_, rx_buf_.data(), FRAME_SIZE);
      rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + FRAME_SIZE);

      if (!tools::check_crc16(reinterpret_cast<uint8_t *>(&rx_data_), sizeof(rx_data_))) {
        tools::logger()->debug("[Gimbal] CRC16 check failed.");
        error_count++;
        continue;
      }

      error_count = 0;
      std::lock_guard<std::mutex> lock(mutex_);
      switch (rx_data_.state) {
        case 0x00:
          mode_ = GimbalMode::ERROR;
          break;
        case 0x01:
          mode_ = GimbalMode::RUNNING;
          break;
        default:
          mode_ = GimbalMode::IDLE;
          tools::logger()->warn("[Gimbal] Invalid state from gimbal: {}", rx_data_.state);
          break;
      }
      tools::logger()->debug("[Gimbal] Received state: {}, mode: {}",
                             rx_data_.state, str(mode_));
    }
  }

  tools::logger()->info("[Gimbal] read_thread stopped.");
}

void Gimbal::diagnose_port(const std::string & port) const
{
  namespace fs = std::filesystem;

  tools::logger()->warn("[Gimbal] Diagnosing port: configured='{}', active='{}'",
                        port.empty() ? "(none)" : port,
                        active_port_.empty() ? "(none)" : active_port_);

  // 配置端口（往往是软链接，如 /dev/rm_usb0）是否存在 / 是否可访问
  struct stat st{};
  if (stat(port.c_str(), &st) != 0) {
    tools::logger()->warn("[Gimbal] stat('{}') failed: {} (errno={}). "
                          "Device node GONE — likely USB cable/adapter dropped, or STM32 reset.",
                          port, strerror(errno), errno);
    return;
  }
  tools::logger()->warn("[Gimbal] stat('{}'): exists, mode=0{:o}.",
                        port, st.st_mode & 0777);

  // 软链接解析：/dev/rm_usb0 → 真实设备（如 /dev/ttyUSB0）。
  // CH340/STM32 复位会让 USB 设备重新枚举成新节点，旧软链接可能变成失效链接。
  std::error_code ec;
  if (fs::is_symlink(port, ec)) {
    auto target = fs::read_symlink(port, ec);
    if (ec) {
      tools::logger()->warn("[Gimbal] read_symlink('{}') failed: {}.", port, ec.message());
    } else {
      // 相对路径目标（如 'ttyUSB0'）解析到 /dev 下，便于直接判读
      std::string target_str = target.string();
      if (!target_str.empty() && target_str[0] != '/') {
        std::string parent = port.substr(0, port.find_last_of('/') + 1);
        target_str = parent + target_str;
      }
      bool target_exists = fs::exists(target_str, ec);
      if (target_exists) {
        tools::logger()->warn("[Gimbal] symlink '{}' -> '{}'.", port, target_str);
      } else {
        tools::logger()->warn("[Gimbal] symlink '{}' -> '{}' (BROKEN: target does not exist). "
                              "USB device (CH340) dropped and has not re-enumerated yet.",
                              port, target_str);
      }
    }
  }

  // access 可访问性
  if (access(port.c_str(), R_OK | W_OK) != 0) {
    tools::logger()->warn("[Gimbal] access('{}', R_OK|W_OK) failed: {} (errno={}).",
                          port, strerror(errno), errno);
  } else {
    tools::logger()->warn("[Gimbal] access('{}', R_OK|W_OK): OK.", port);
  }

  // 列出当前所有 ttyACM* / ttyUSB* 节点，便于核对设备是否重新枚举成新节点
  for (const auto & prefix : {"/dev/ttyACM", "/dev/ttyUSB"}) {
    std::string found;
    DIR * dir = opendir("/dev");
    if (dir) {
      struct dirent * ent = nullptr;
      while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.rfind(prefix + 1, 0) == 0) {  // prefix without leading slash of name
          found += name + " ";
        }
      }
      closedir(dir);
    }
    if (!found.empty()) {
      tools::logger()->warn("[Gimbal] /dev/{}* present: {}", prefix, found);
    }
  }
}

std::string Gimbal::try_open_with_fallback(const std::string & configured_port)
{
  // 优先尝试配置端口；失败则在 /dev/ttyUSB* 和 /dev/ttyACM* 中找一个能打开的设备回退。
  std::vector<std::string> candidates = {configured_port};

  DIR * dir = opendir("/dev");
  if (dir) {
    std::vector<std::string> tty;
    struct dirent * ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
      std::string name = ent->d_name;
      if (name.rfind("ttyUSB", 0) == 0 || name.rfind("ttyACM", 0) == 0) {
        tty.emplace_back(std::string("/dev/") + name);
      }
    }
    closedir(dir);
    std::sort(tty.begin(), tty.end());
    for (auto & p : tty) {
      candidates.push_back(p);
    }
  }

  for (const auto & port : candidates) {
    if (port.empty()) continue;
    // 配置端口可能是失效软链接，先做存在性/可访问性过滤，避免无谓的 open 抛异常
    if (access(port.c_str(), R_OK | W_OK) != 0) continue;
    try {
      serial_.setPort(port);
      serial_.open();
      apply_serial_config();  // open 后必须重新下发波特率/超时，否则用默认值
      tools::logger()->info("[Gimbal] Opened serial port: '{}'.", port);
      return port;
    } catch (const std::exception & e) {
      tools::logger()->warn("[Gimbal] open('{}') failed: {}.", port, e.what());
    }
  }
  return std::string();
}

void Gimbal::reconnect()
{
  // 重连全程让 send() 静默跳过，避免每帧刷一条 PortNotOpenedException
  reconnecting_.store(true, std::memory_order_relaxed);
  serial_dead_.store(false, std::memory_order_relaxed);
  dropped_frames_.store(0, std::memory_order_relaxed);
  reconnect_start_ = std::chrono::steady_clock::now();
  struct reconnecting_guard {
    std::atomic<bool> & reconnecting;
    ~reconnecting_guard() { reconnecting.store(false, std::memory_order_relaxed); }
  } guard{reconnecting_};

  int max_retry_count = reconnect_max_retry_count_;
  for (int i = 0; i < max_retry_count && !quit_; ++i) {
    tools::logger()->warn("[Gimbal] Reconnecting serial, attempt {}/{}...", i + 1, max_retry_count);
    {
      std::lock_guard<std::mutex> lock(serial_mutex_);
      try {
        serial_.close();
      } catch (...) {
      }
    }
    // 退避：CH340 复位后 USB 重新枚举需要时间，逐次拉长等待
    std::this_thread::sleep_for(std::chrono::milliseconds(500 + i * 500));

    // 用回退扫描打开（配置软链接失效时回退到真实 ttyUSB 节点）
    std::lock_guard<std::mutex> lock(serial_mutex_);
    try {
      active_port_ = try_open_with_fallback(active_port_);
    } catch (const std::exception & e) {
      tools::logger()->warn("[Gimbal] try_open_with_fallback exception: {}.", e.what());
      active_port_.clear();
    }

    if (!active_port_.empty()) {
      tools::logger()->info("[Gimbal] Reconnected serial successfully on '{}'.", active_port_);
      return;  // guard 析构复位 reconnecting_
    }
  }

  // 10 次重连全部失败：串口彻底断了。置 serial_dead_ 让 send() 停发，
  // 并退出进程，交由 systemd / 看门狗重新拉起整个程序。
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - reconnect_start_).count();
  tools::logger()->error(
    "[Gimbal] Reconnect FAILED after {} attempts ({} ms). Serial is dead. Exiting process "
    "for supervisor restart.", max_retry_count, ms);
  serial_dead_.store(true, std::memory_order_relaxed);
  std::exit(1);
}

}  // namespace io
