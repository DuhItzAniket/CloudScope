#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>

// Mask components -> visuals (stub: Ph27 implements).
struct CloudObject {
    int x = 0, y = 0, w = 0, h = 0;
    std::vector<std::pair<int, int>> polygon;
    int area = 0;
};

enum class OverlayMode { Rect = 0, Polygon = 1, Symmetry = 2 };

class CloudVision {
public:
    static std::vector<CloudObject> extract(const cv::Mat& cloudMask);
    static bool sane(const std::vector<CloudObject>& objs, int fw, int fh);
    static void render(cv::Mat& bgr, const std::vector<CloudObject>& objs,
                       const std::string& label, OverlayMode mode);
};
