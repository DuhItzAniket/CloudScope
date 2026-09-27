#include "CloudVision.h"
// Ph27: real components + overlays land here.
std::vector<CloudObject> CloudVision::extract(const cv::Mat&) { return {}; }
bool CloudVision::sane(const std::vector<CloudObject>&, int, int) { return false; }
void CloudVision::render(cv::Mat&, const std::vector<CloudObject>&,
                         const std::string&, OverlayMode) {}
