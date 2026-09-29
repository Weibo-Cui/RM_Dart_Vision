//
// opencv traditional vision detector
// created by cwb on 2026.1.20
//

#include "Traditional.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

TraditionalDetector::TraditionalDetector(const std::string &config_path) {
    auto yaml = tools::load(config_path);    
    h_min_ = yaml["hsv"]["h_min"].as<int>();
    h_max_ = yaml["hsv"]["h_max"].as<int>();
    s_min_ = yaml["hsv"]["s_min"].as<int>();
    s_max_ = yaml["hsv"]["s_max"].as<int>() ;
    v_min_ = yaml["hsv"]["v_min"].as<int>();
    v_max_ = yaml["hsv"]["v_max"].as<int>() ;  
    morph_kernel_size_ = yaml["morphology"]["kernel_size"].as<int>();
    morph_iterations_ = yaml["morphology"]["iterations"].as<int>();
    dilate_iterations_ = yaml["morphology"]["dilate_iterations"].as<int>() ;
    erode_iterations_ = yaml["morphology"]["erode_iterations"].as<int>();
    min_area_ = yaml["contour_filter"]["min_area"].as<double>();
    max_area_ = yaml["contour_filter"]["max_area"].as<double>();
    max_aspect_ratio_ = yaml["contour_filter"]["max_aspect_ratio"].as<double>();
    min_aspect_ratio_ = yaml["contour_filter"]["min_aspect_ratio"].as<double>();
    use_roi_ = yaml["use_roi"].as<bool>();
    roi_x_ = yaml["roi"]["x"].as<int>();
    roi_y_ = yaml["roi"]["y"].as<int>();
    roi_width_ = yaml["roi"]["width"].as<int>() ;
    roi_height_ = yaml["roi"]["height"].as<int>() ; 
    gaussian_kernel_size_ = yaml["gaussian"]["kernel_size"].as<int>();
    gaussian_sigma_ = yaml["gaussian"]["sigma"].as<double>();
    use_trackbar_ = yaml["use_trackbar"].as<bool>();
    // Debug
    if (use_trackbar_){
        track_h_min_ = h_min_;
        track_h_max_ = h_max_;
        track_s_min_ = s_min_;
        track_s_max_ = s_max_;
        track_v_min_ = v_min_;
        track_v_max_ = v_max_;
        track_min_area_ = static_cast<int>(min_area_);
        track_max_area_ = static_cast<int>(max_area_);
        track_min_aspect_ratio_ = static_cast<int>(min_aspect_ratio_ * 100);
        track_max_aspect_ratio_ = static_cast<int>(max_aspect_ratio_ * 100);
        track_morph_kernel_size_ = morph_kernel_size_;
        track_dilate_iterations_ = dilate_iterations_;
        track_erode_iterations_ = erode_iterations_;
    }
    morph_kernel_ = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(morph_kernel_size_, morph_kernel_size_));
}

std::vector<LightDetect::Light> TraditionalDetector::detect(cv::Mat &src, const cv::Size2d &dst_size, const int &my_color, const bool &startup)
{
    if (src.empty()) return {};

    cv::Mat resized;
    cv::Size target_size(static_cast<int>(dst_size.width), static_cast<int>(dst_size.height));
    if (src.size() != target_size) {
        cv::resize(src, resized, target_size);
    } else {
        resized = src;
    }
    cv::Size original_size = resized.size();   // = dst_size，mapCoordinates scale=1.0
    cv::Rect roi_rect;
    preprocess(resized, processed_img, roi_rect);
    colorSegmentation(processed_img, bir_img);
    morphologyProcess(bir_img);
    std::vector<Light> lights = findAndFilterContours(bir_img, roi_rect, dst_size, original_size);
    return lights;
}

void TraditionalDetector::preprocess(const cv::Mat &src, cv::Mat &dst,cv::Rect &roi_rect) {
    if (use_roi_) {
        auto x = std::max(0, std::min(roi_x_, src.cols - 1));
        auto y = std::max(0, std::min(roi_y_, src.rows - 1));
        auto w = std::min(roi_width_, src.cols - x);
        auto h = std::min(roi_height_, src.rows - y);
        roi_rect = cv::Rect(x, y, w, h);
        cv::Mat roi_frame = src(roi_rect);
        debug_roiimg = roi_frame.clone();   // 供可视化显示ROI裁剪图
        cv::GaussianBlur(roi_frame, dst, cv::Size(gaussian_kernel_size_, gaussian_kernel_size_), gaussian_sigma_);
    } else {
        roi_rect = cv::Rect(0, 0, src.cols, src.rows);
        debug_roiimg = src;
        cv::GaussianBlur(src, dst, cv::Size(gaussian_kernel_size_, gaussian_kernel_size_), gaussian_sigma_);
    }
}

void TraditionalDetector::colorSegmentation(const cv::Mat &src,cv::Mat &dst) {
    cv::cvtColor(src, hsv_img, cv::COLOR_BGR2HSV);
    cv::Scalar lower(h_min_, s_min_, v_min_);
    cv::Scalar upper(h_max_, s_max_, v_max_);
    cv::inRange(hsv_img, lower, upper, dst);  
}

void TraditionalDetector::morphologyProcess(cv::Mat &binary) {    
    if (erode_iterations_ > 0) {
        cv::erode(binary, binary, morph_kernel_, cv::Point(-1, -1), erode_iterations_);
    }    
    if (dilate_iterations_ > 0) {
        cv::dilate(binary, binary, morph_kernel_, cv::Point(-1, -1), dilate_iterations_);
    }  
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, morph_kernel_, cv::Point(-1, -1), morph_iterations_);    
    cv::morphologyEx(binary, binary, cv::MORPH_OPEN, morph_kernel_, cv::Point(-1, -1), 1);
    if(use_trackbar_)this->debug_birimg = binary;
}

std::vector<LightDetect::Light> TraditionalDetector::findAndFilterContours(const cv::Mat &binary, const cv::Rect &roi_offset,const cv::Size2d &dst_size,const cv::Size &original_size) 
{
    std::vector<Light> result;
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binary, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (const auto &contour : contours) {
        double area = cv::contourArea(contour);
        if (area < min_area_ || area > max_area_) continue;
        if (contour.size() < 5) continue;
        
        cv::RotatedRect rRect = cv::fitEllipse(contour);
        float width = rRect.size.width;
        float height = rRect.size.height;    
        float max_axis = std::max(width, height);
        float min_axis = std::min(width, height);
        if (min_axis == 0) continue; 
        
        float aspect_ratio = max_axis / min_axis;       
        if (aspect_ratio > max_aspect_ratio_ || aspect_ratio < min_aspect_ratio_) continue;

        cv::Point2f center;
        float radius;
        cv::minEnclosingCircle(contour, center, radius);

        Light light;
        light.center_point = center;
        light.box = cv::Rect2d(rRect.boundingRect());
        light.min_axis = min_axis;
        light.max_axis = max_axis;
        mapCoordinates(light, roi_offset, dst_size, original_size);
        
        result.push_back(light);
    }
    std::sort(result.begin(), result.end(), [](const Light &a, const Light &b) {
        return a.score > b.score;
    }); 
    return result;
}

void TraditionalDetector::mapCoordinates(Light &light, const cv::Rect &roi_offset,const cv::Size2d &dst_size,const cv::Size &original_size)
{
    (void)roi_offset;

    // 原图坐标 → 目标尺寸坐标（入口缩放后 original_size == dst_size，scale=1.0）
    double scale_x = dst_size.width / original_size.width;
    double scale_y = dst_size.height / original_size.height;

    light.center_point.x *= scale_x;
    light.center_point.y *= scale_y;
    light.box.x *= scale_x;
    light.box.y *= scale_y;
    light.box.width *= scale_x;
    light.box.height *= scale_y;
}

void TraditionalDetector::onTrackbarCallback(int value, void* userdata){
    TraditionalDetector* trad_detector = static_cast<TraditionalDetector*>(userdata);
    trad_detector->updateParameters();
}

void TraditionalDetector::updateParameters(){
    h_min_ = track_h_min_;
    h_max_ = track_h_max_;
    s_min_ = track_s_min_;
    s_max_ = track_s_max_;
    v_min_ = track_v_min_;
    v_max_ = track_v_max_;   
    min_area_ = static_cast<double>(track_min_area_);
    max_area_ = static_cast<double>(track_max_area_);
    min_aspect_ratio_ = static_cast<double>(track_min_aspect_ratio_) / 100.0;
    max_aspect_ratio_ = static_cast<double>(track_max_aspect_ratio_) / 100.0;
    morph_kernel_size_ = (track_morph_kernel_size_ % 2 == 0) ? 
                         track_morph_kernel_size_ + 1 : track_morph_kernel_size_;
    dilate_iterations_ = track_dilate_iterations_;
    erode_iterations_ = track_erode_iterations_;
    morph_kernel_ = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(morph_kernel_size_, morph_kernel_size_));
}

void TraditionalDetector::createTrackbars(tools::WebViewer* web_viewer) {
    if (!web_viewer) return;
    web_viewer->namedWindow("Debug Settings");
    // HSV 
    web_viewer->createTrackbar("H Min", "Debug Settings", &track_h_min_, 180, onTrackbarCallback, this);
    web_viewer->createTrackbar("H Max", "Debug Settings", &track_h_max_, 180, onTrackbarCallback, this);
    web_viewer->createTrackbar("S Min", "Debug Settings", &track_s_min_, 255, onTrackbarCallback, this);
    web_viewer->createTrackbar("S Max", "Debug Settings", &track_s_max_, 255, onTrackbarCallback, this);
    web_viewer->createTrackbar("V Min", "Debug Settings", &track_v_min_, 255, onTrackbarCallback, this);
    web_viewer->createTrackbar("V Max", "Debug Settings", &track_v_max_, 255, onTrackbarCallback, this);
    //形态学
    web_viewer->createTrackbar("Kernel Size", "Debug Settings", &track_morph_kernel_size_, 15, onTrackbarCallback, this);
    web_viewer->createTrackbar("Dilate", "Debug Settings", &track_dilate_iterations_, 10, onTrackbarCallback, this);
    web_viewer->createTrackbar("Erode", "Debug Settings", &track_erode_iterations_, 10, onTrackbarCallback, this);
    // 轮廓筛选
    web_viewer->createTrackbar("Min Area", "Debug Settings", &track_min_area_, 3000, onTrackbarCallback, this);
    web_viewer->createTrackbar("Max Area", "Debug Settings", &track_max_area_, 20000, onTrackbarCallback, this);
    web_viewer->createTrackbar("Min Aspect Ratio", "Debug Settings", &track_min_aspect_ratio_, 200, onTrackbarCallback, this);
    web_viewer->createTrackbar("Max Aspect Ratio", "Debug Settings", &track_max_aspect_ratio_, 200, onTrackbarCallback, this);
}
