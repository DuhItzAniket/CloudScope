#include "CloudVision.h"

#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace {
// Minimum component area as a fraction of frame (kills speckle).
constexpr double kMinFrac = 0.002;
// A single component covering this much of the frame = degenerate mask
// (all-cloud failure mode); overlays are suppressed, label-only + badge.
constexpr double kMaxFrac = 0.80;
}  // namespace

std::vector<CloudObject> CloudVision::extract(const cv::Mat& cloudMask)
{
    std::vector<CloudObject> out;
    if (cloudMask.empty() || cloudMask.type() != CV_8UC1)
        return out;
    cv::Mat bin;
    cv::threshold(cloudMask, bin, 0, 255, cv::THRESH_BINARY);
    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8);
    const double frameArea = static_cast<double>(cloudMask.rows * cloudMask.cols);
    for (int i = 1; i < n; ++i) {
        const int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < kMinFrac * frameArea)
            continue;
        CloudObject o;
        o.x = stats.at<int>(i, cv::CC_STAT_LEFT);
        o.y = stats.at<int>(i, cv::CC_STAT_TOP);
        o.w = stats.at<int>(i, cv::CC_STAT_WIDTH);
        o.h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        o.area = area;
        cv::Mat comp = (labels == i);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(comp, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (contours.empty())
            continue;
        const auto& best = *std::max_element(
            contours.begin(), contours.end(),
            [](const std::vector<cv::Point>& a, const std::vector<cv::Point>& b) {
                return cv::contourArea(a) < cv::contourArea(b);
            });
        std::vector<cv::Point> approx;
        cv::approxPolyDP(best, approx, 0.005 * cv::arcLength(best, true), true);
        for (const auto& p : approx)
            o.polygon.emplace_back(p.x, p.y);
        out.push_back(std::move(o));
    }
    return out;
}

bool CloudVision::sane(const std::vector<CloudObject>& objs, int fw, int fh)
{
    if (objs.empty())
        return false;
    const double frameArea = static_cast<double>(fw * fh);
    int biggest = 0;
    for (const auto& o : objs)
        biggest = std::max(biggest, o.area);
    return biggest < kMaxFrac * frameArea;
}

void CloudVision::render(cv::Mat& bgr, const std::vector<CloudObject>& objs,
                         const std::string& label, OverlayMode mode)
{
    const cv::Scalar col(0, 159, 230);  // cloud orange (BGR)
    for (const auto& o : objs) {
        std::vector<cv::Point> pts;
        pts.reserve(o.polygon.size());
        for (const auto& p : o.polygon)
            pts.emplace_back(p.first, p.second);
        if (mode == OverlayMode::Rect) {
            cv::rectangle(bgr, cv::Rect(o.x, o.y, o.w, o.h), col, 3);
        } else if (mode == OverlayMode::Polygon) {
            if (!pts.empty())
                cv::polylines(bgr, pts, true, col, 3);
        } else {  // Symmetry: translucent highlight + bright outline.
            if (!pts.empty()) {
                cv::Mat layer = bgr.clone();
                cv::fillPoly(layer, std::vector<std::vector<cv::Point>>{pts},
                             cv::Scalar(60, 120, 220));
                constexpr double kAlpha = 0.35;
                cv::addWeighted(layer, kAlpha, bgr, 1.0 - kAlpha, 0.0, bgr);
                cv::polylines(bgr, pts, true, cv::Scalar(255, 255, 255), 2);
            }
        }
        cv::putText(bgr, label, cv::Point(o.x, std::max(0, o.y - 8)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.9, cv::Scalar(255, 255, 255), 2);
    }
}
