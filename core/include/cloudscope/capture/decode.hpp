// Decoding camera frames into images (P023, FR-CAM-03).
//
//   MJPEG      libjpeg-turbo (TurboJPEG API); the decoder handle is kept per thread
//   YUYV       YUV 4:2:2 to BGR, OpenCV
//   Gray8/16   as they are (16-bit stays 16-bit in decode_native, becomes 8-bit in decode_bgr8 / decode_gray8)
//   Bgr8/Rgb8  copied, red and blue swapped for RGB
//
// Returned images own their memory (the frame can be released); 8-bit BGR is the working format of the rest
// of the pipeline, 16-bit grey is kept for recording.
#pragma once

#include "cloudscope/capture/frame.hpp"
#include "cloudscope/common/error.hpp"

#include <opencv2/core.hpp>

#include <cstddef>
#include <span>

namespace cloudscope {

struct JpegInfo {
    int width = 0;
    int height = 0;
    int subsampling = 0;  // TurboJPEG TJSAMP_* value
};

// Parse if the bytes are not a JPEG header.
[[nodiscard]] Expected<JpegInfo> jpeg_info(std::span<const std::byte> jpeg);

// The frame as the closest OpenCV image of its own kind: CV_8UC3 BGR (from MJPEG, YUYV, BGR8, RGB8), CV_8UC1
// (Gray8) or CV_16UC1 (Gray16). Parse for a corrupt JPEG, InvalidArgument for a frame whose size does not match
// its mode.
[[nodiscard]] Expected<cv::Mat> decode_native(const Frame& frame);

// 8-bit BGR, three channels, whatever the frame's format (16-bit grey is scaled down by 256).
[[nodiscard]] Expected<cv::Mat> decode_bgr8(const Frame& frame);

// 8-bit luma, one channel (BT.601 weights for colour frames).
[[nodiscard]] Expected<cv::Mat> decode_gray8(const Frame& frame);

}  // namespace cloudscope
