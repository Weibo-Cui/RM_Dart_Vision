#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core/mat.hpp>

namespace tools
{

typedef void (*TrackbarCallback)(int pos, void* userdata);

/**
 * @brief 基于内嵌 HTTP 服务器的 Web 图像查看器
 *
 * 通过 MJPEG 流在浏览器中显示图像，并通过 HTTP 接收键盘事件，
 * 可直接替代 cv::imshow / cv::waitKey / cv::namedWindow。
 *
 * 用法示例：
 * @code
 *   qd::WebViewer viewer(8080);
 *   viewer.namedWindow("窗口名");
 *   while (true) {
 *       cv::Mat img = ...;
 *       viewer.imshow("窗口名", img);
 *       int key = viewer.waitKey(30);
 *       if (key == 27) break;
 *   }
 *   viewer.destroyAllWindows();
 * @endcode
 */
class WebViewer {
public:
    /**
     * @param port         HTTP 监听端口
     * @param jpeg_quality JPEG 编码质量 (1-100)
     */
    explicit WebViewer(int port = 8080, int jpeg_quality = 80);
    ~WebViewer();

    WebViewer(const WebViewer&) = delete;
    WebViewer& operator=(const WebViewer&) = delete;

    void namedWindow(const std::string& winname);
    void imshow(const std::string& winname, const cv::Mat& img);
    void createTrackbar(const std::string& trackbarname, const std::string& winname,
                        int* value, int count,
                        TrackbarCallback onChange = nullptr,
                        void* userdata = nullptr);

    /**
     * @brief 等待键盘输入
     * @param delay_ms 超时毫秒数，0 表示永久阻塞直到按键
     * @return 按键 ASCII 码，超时返回 -1
     */
    int waitKey(int delay_ms = 0);

    void destroyWindow(const std::string& winname);
    void destroyAllWindows();

    int port() const { return port_; }

private:
    void server_loop();
    void handle_client(int client_fd);

    void send_html_page(int fd);
    void send_mjpeg_stream(int fd, const std::string& window);
    void send_window_list(int fd);
    void send_trackbar_list(int fd);
    void send_response(int fd, int code, const std::string& content_type,
                       const std::string& body);

    int port_;
    int jpeg_quality_;
    int server_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread server_thread_;

    struct FrameData {
        cv::Mat frame;
        uint64_t seq = 0;
    };
    std::map<std::string, FrameData> windows_;
    std::mutex win_mtx_;
    std::condition_variable win_cv_;

    struct TrackbarData {
        std::string name;
        std::string winname;
        int* value_ptr;
        int max_val;
        TrackbarCallback onChange;
        void* userdata;
    };
    std::vector<TrackbarData> trackbars_;
    std::mutex tb_mtx_;

    std::queue<int> keys_;
    std::mutex key_mtx_;
    std::condition_variable key_cv_;

    std::set<int> active_fds_;
    std::mutex fds_mtx_;
    std::atomic<int> active_handlers_{0};
};

} // namespace tools
