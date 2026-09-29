//
// opencv traditional vision detector
// created by cwb on 2026.1.20
//

#ifndef TRADITIONAL_DETECTOR_HPP
#define TRADITIONAL_DETECTOR_HPP

#include "Detect.hpp"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "tools/yaml.hpp"
#include "tools/web_viewer.hpp"

class TraditionalDetector : public LightDetect {
public:
    explicit TraditionalDetector(const std::string &config_path);
    ~TraditionalDetector() override = default;
    std::vector<Light> detect(cv::Mat &src, const cv::Size2d &dst_size, const int &my_color, const bool &startup) override;
    // Debug
    int track_h_min_,track_s_min_,track_v_min_;
    int track_h_max_,track_s_max_,track_v_max_;       
    int track_min_area_,track_max_area_;
    int track_min_aspect_ratio_,track_max_aspect_ratio_;
    int track_morph_kernel_size_,track_dilate_iterations_,track_erode_iterations_;
    cv::Mat debug_birimg;
    cv::Mat debug_roiimg;
    static void onTrackbarCallback(int value, void* userdata);
    void updateParameters();
    void createTrackbars(tools::WebViewer* viewer);

private:
    int h_min_, s_min_,v_min_;           
    int h_max_, s_max_,v_max_;                
    int morph_kernel_size_, morph_iterations_;    
    int dilate_iterations_,erode_iterations_; 
    double min_area_,max_area_;           
    double min_aspect_ratio_, max_aspect_ratio_;
    bool use_roi_,use_trackbar_;      
    int roi_x_,roi_y_,roi_width_,roi_height_;        
    int gaussian_kernel_size_; 
    double gaussian_sigma_;            

    cv::Mat hsv_img;
    cv::Mat bir_img;
    cv::Mat processed_img;
    cv::Mat morph_kernel_;

    void preprocess(const cv::Mat &src,cv::Mat &dst, cv::Rect &roi_rect);
    void colorSegmentation(const cv::Mat &src,cv::Mat &dst);
    void morphologyProcess(cv::Mat &binary);
    std::vector<Light> findAndFilterContours(const cv::Mat &binary, const cv::Rect &roi_offset,const cv::Size2d &dst_size,const cv::Size &original_size);
    void mapCoordinates(Light &light, const cv::Rect &roi_offset,const cv::Size2d &dst_size,const cv::Size &original_size);
};

#endif // TRADITIONAL_DETECTOR_HPP
