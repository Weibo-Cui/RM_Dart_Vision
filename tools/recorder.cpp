#include "recorder.hpp"

#include <fmt/chrono.h>
#include <fmt/core.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "math_tools.hpp"
#include "tools/logger.hpp"

namespace tools
{
Recorder::Recorder(double fps) : init_(false), fps_(fps), queue_(30), stop_thread_(false)
{
  start_time_ = std::chrono::steady_clock::now();
  last_time_ = start_time_;

  auto folder_path = "records";
  auto file_name = fmt::format("{:%Y-%m-%d_%H-%M-%S}", std::chrono::system_clock::now());
  //text_path_ = fmt::format("{}/{}.txt", folder_path, file_name);
  //video_path_ = fmt::format("{}/{}.avi", folder_path, file_name);
  video_path_ = fmt::format("{}/{}.mkv", folder_path, file_name);

  std::filesystem::create_directory(folder_path);
}

Recorder::~Recorder()
{
  stop_thread_ = true;
  // 退出时给队列中额外推入一个空帧，避免pop一直等待
  queue_.push({cv::Mat::zeros(0, 0, 0), {0, 0, 0, 0}, std::chrono::steady_clock::now()});
  if (saving_thread_.joinable()) saving_thread_.join();  // 等待视频保存线程结束

  if (!init_) return;
  //text_writer_.close();
  // pclose会阻塞至ffmpeg子进程结束，写出的.mkv能完整可播。
  if (ffmpeg_pipe_) {
    pclose(ffmpeg_pipe_);
    ffmpeg_pipe_ = nullptr;
  }
}

void Recorder::save_to_file()
{
  while (!stop_thread_) {
    FrameData frame;
    queue_.pop(frame);  // 从队列中取出帧数据
    if (frame.img.empty()) {
      tools::logger()->debug("Recorder received empty img. Skip this frame.");
      continue;
    }
    // 写入视频文件：把连续的 BGR 像素喂给 ffmpeg 子进程编码
    if (ffmpeg_pipe_ && frame.img.isContinuous()) {
      fwrite(frame.img.data, 1, frame.img.total() * frame.img.elemSize(), ffmpeg_pipe_);
      fflush(ffmpeg_pipe_);  // 减小非正常退出时的丢帧量
    } else if (ffmpeg_pipe_) {
      // 非连续矩阵逐行写
      for (int r = 0; r < frame.img.rows; ++r) {
        fwrite(frame.img.ptr(r), 1, frame.img.cols * frame.img.elemSize(), ffmpeg_pipe_);
      }
      fflush(ffmpeg_pipe_);
    }

    // 写入文本文件（输出顺序为wxyz）
    /*Eigen::Vector4d xyzw = frame.q.coeffs();
    auto since_begin = tools::delta_time(frame.timestamp, start_time_);
    text_writer_ << fmt::format(
      "{} {} {} {} {}\n", since_begin, xyzw[3], xyzw[0], xyzw[1], xyzw[2]);
    */
  }
}

void Recorder::record(
  const cv::Mat & img, const Eigen::Quaterniond & q,
  const std::chrono::steady_clock::time_point & timestamp)
{
  if (img.empty()) return;
  if (!init_) init(img);

  auto since_last = tools::delta_time(timestamp, last_time_);
  if (since_last < 1.0 / fps_) return;

  last_time_ = timestamp;
  queue_.push({img, q, timestamp});
}

bool Recorder::ffmpeg_has_encoder(const std::string & encoder)
{
  std::string cmd = "ffmpeg -hide_banner -encoders 2>/dev/null | grep -qw " + encoder;
  return std::system(cmd.c_str()) == 0;
}

bool Recorder::open_ffmpeg(const std::string & codec)
{
  // rawvideo 喂入（相机帧为BGR8），用codec编码为H.264/JPEG写入.mkv
  // -preset ultrafast -tune zerolatency：低延迟、低CPU，适合实时录制
  // -flush_packets 1：督促muxer尽快落盘（mkv上效果有限，主要靠子进程的流式封装）
  // -an：无音频；2>/dev/null 丢弃ffmpeg的stderr日志
  std::string cmd = fmt::format(
    "ffmpeg -y -f rawvideo -pix_fmt bgr24 -s {}x{} -r {} -i - "
    "-c:v {} -preset ultrafast -tune zerolatency -pix_fmt yuv420p "
    "-flush_packets 1 -an {} 2>/dev/null",
    frame_size_.width, frame_size_.height, fps_, codec, video_path_);
  ffmpeg_pipe_ = popen(cmd.c_str(), "w");
  return ffmpeg_pipe_ != nullptr;
}

void Recorder::init(const cv::Mat & img)
{
  //text_writer_.open(text_path_);
  //auto fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');  // MJPG编码画面有损耗，其图像与相机采集图像相比整体较暗，占用空间小
  //auto fourcc = cv::VideoWriter::fourcc('F', 'F', 'V', '1'); // FFV1编码画面基本上无损耗，但是占用空间大，要及时删除无用的录频
  frame_size_ = img.size();
  // H.264（libx264）压缩编码：体积小、写盘压力低
  std::string codec = "libx264";
  if (!ffmpeg_has_encoder(codec)) {
    // 环境无libx264时回退mjpeg
    tools::logger()->warn("libx264 not available, fallback to mjpeg");
    codec = "mjpeg";
  }
  if (!open_ffmpeg(codec)) {
    tools::logger()->error("Cannot open ffmpeg recorder at {}", video_path_);
    return;  // init_保持false，record后续调用会再次尝试
  }
  saving_thread_ = std::thread(&Recorder::save_to_file, this);  // 启动保存线程
  init_ = true;
}

}  // namespace tools
