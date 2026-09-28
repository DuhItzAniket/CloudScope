#pragma once
#include <opencv2/videoio.hpp>
#include <string>
#include <vector>

// Uniform source opener: integer string -> local camera index (MSMF on
// Windows); rtsp/http URL -> network stream; anything else -> video file.
class CameraSource {
public:
    struct DeviceInfo {
        int index = -1;
        std::string name;
        bool opened = false;
        int width = 0;
        int height = 0;
    };

    CameraSource() = default;
    ~CameraSource() { close(); }

    // Returns true when frames can be read.
    bool open(const std::string& spec);
    void close();
    bool isOpened() const { return cap_.isOpened(); }
    bool read(cv::Mat& frame);
    bool setProp(int propId, double value);
    double getProp(int propId) const;

    int width() const { return static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH)); }
    int height() const { return static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT)); }

    // Probe local indices [0, maxIndex). Slow (~1s per absent camera).
    static std::vector<DeviceInfo> enumerateLocal(int maxIndex = 5);

private:
    static bool isIntegerSpec(const std::string& s, int& out);
    cv::VideoCapture cap_;
};
