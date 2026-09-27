#include "CameraSource.h"

#include <cctype>

bool CameraSource::isIntegerSpec(const std::string& s, int& out)
{
    if (s.empty())
        return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    }
    out = std::stoi(s);
    return true;
}

bool CameraSource::open(const std::string& spec)
{
    close();
    int idx = 0;
    if (isIntegerSpec(spec, idx)) {
#ifdef _WIN32
        cap_.open(idx, cv::CAP_MSMF);
#else
        cap_.open(idx);
#endif
    } else {
        cap_.open(spec);
    }
    return cap_.isOpened();
}

void CameraSource::close()
{
    if (cap_.isOpened())
        cap_.release();
}

bool CameraSource::read(cv::Mat& frame)
{
    if (!cap_.isOpened())
        return false;
    return cap_.read(frame) && !frame.empty();
}

std::vector<CameraSource::DeviceInfo> CameraSource::enumerateLocal(int maxIndex)
{
    std::vector<DeviceInfo> out;
    for (int i = 0; i < maxIndex; ++i) {
        DeviceInfo info;
        info.index = i;
        info.name = "Camera " + std::to_string(i);
#ifdef _WIN32
        cv::VideoCapture c(i, cv::CAP_MSMF);
#else
        cv::VideoCapture c(i);
#endif
        info.opened = c.isOpened();
        if (info.opened) {
            info.width = static_cast<int>(c.get(cv::CAP_PROP_FRAME_WIDTH));
            info.height = static_cast<int>(c.get(cv::CAP_PROP_FRAME_HEIGHT));
        }
        out.push_back(info);
    }
    return out;
}
