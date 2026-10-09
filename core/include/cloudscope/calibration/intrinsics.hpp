// Intrinsic camera calibration (P031): checkerboard detection, a capture assistant that says when a new view adds
// coverage, the OpenCV pinhole and fisheye fits, the pixel <-> ray mapping, and the camera-model file STRATIA reads
// (schema "cloudscope.camera_model/1", core/resources/camera_model.schema.json).
//
// The file is the contract with STRATIA's camera models (STRATIA P043) and with CloudScope's own metadata provider
// (P067, the ray map): a model name, the image size the parameters refer to, fx fy cx cy, the distortion vector of
// that model, and how good the fit was. Nothing else is needed to turn a pixel into a direction.
#pragma once

#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/error.hpp"
#include "cloudscope/common/json_schema.hpp"

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cloudscope {

enum class LensModel : std::uint8_t {
    Pinhole,  // OpenCV: k1 k2 p1 p2 k3 (cv::calibrateCamera)
    Fisheye,  // OpenCV equidistant fisheye: k1 k2 k3 k4 (cv::fisheye)
};
[[nodiscard]] std::string_view to_string(LensModel model);  // "opencv_pinhole", "opencv_fisheye"
[[nodiscard]] Expected<LensModel> lens_model_from_string(std::string_view text);

struct BoardSpec {
    int columns = 9;         // inner corners across
    int rows = 6;            // inner corners down
    double square_mm = 25.0;  // side of a square
};

struct DetectedBoard {
    std::vector<cv::Point2f> corners;  // columns * rows, row by row
    cv::Size image_size;
};

struct CameraModel {
    LensModel model = LensModel::Fisheye;
    int width = 0;
    int height = 0;
    double fx = 0.0;
    double fy = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    std::vector<double> distortion;  // 4 (fisheye) or 5 (pinhole)
    double rms_px = 0.0;             // root-mean-square reprojection error of the fit
    int views = 0;
    std::string camera_id;
    std::string camera_name;
    std::string calibration_id;  // "intrinsics-20261009T101530Z"
    UtcTime calibrated{};
    BoardSpec board;

    [[nodiscard]] cv::Mat camera_matrix() const;      // 3x3 CV_64F
    [[nodiscard]] cv::Mat distortion_vector() const;  // 1xN CV_64F
};

// Finds the inner corners of a checkerboard in an 8-bit grey or BGR image. NotFound when it is not there.
[[nodiscard]] Expected<DetectedBoard> detect_checkerboard(const cv::Mat& image, const BoardSpec& board);

// Which views are worth keeping: a view is accepted when its corners cover a part of the image no earlier view
// covered (a grid of `grid` x `grid` cells), so that the fit sees the whole field, edges included.
class CaptureAssistant {
public:
    explicit CaptureAssistant(cv::Size image_size, int grid = 4);
    // true: the view adds coverage and is kept. false: redundant (the reason says why).
    [[nodiscard]] bool accept(const DetectedBoard& board, std::string* reason = nullptr);
    [[nodiscard]] double coverage() const;  // fraction of cells with at least one corner, 0..1
    [[nodiscard]] int views() const { return static_cast<int>(views_.size()); }
    [[nodiscard]] const std::vector<DetectedBoard>& accepted() const { return views_; }
    [[nodiscard]] std::vector<bool> covered_cells() const { return covered_; }

private:
    cv::Size image_size_;
    int grid_;
    std::vector<bool> covered_;
    std::vector<DetectedBoard> views_;
};

// Fits the model to the accepted views. InvalidArgument with fewer than three views or inconsistent sizes.
[[nodiscard]] Expected<CameraModel> fit_intrinsics(const std::vector<DetectedBoard>& views, const BoardSpec& board,
                                                   LensModel model);

// Root-mean-square reprojection error of one view against a model (the board's pose is solved first).
[[nodiscard]] Expected<double> reprojection_error(const CameraModel& model, const DetectedBoard& view);

// A unit direction in the camera frame (x right, y down, z forward) for a pixel, and back.
[[nodiscard]] cv::Point3d pixel_to_ray(const CameraModel& model, cv::Point2d pixel);
[[nodiscard]] cv::Point2d ray_to_pixel(const CameraModel& model, cv::Point3d ray);

[[nodiscard]] nlohmann::json to_json(const CameraModel& model);
[[nodiscard]] Expected<CameraModel> camera_model_from_json(const nlohmann::json& document);
[[nodiscard]] Expected<void> save_camera_model(const CameraModel& model, const std::filesystem::path& file);
[[nodiscard]] Expected<CameraModel> load_camera_model(const std::filesystem::path& file);
[[nodiscard]] const JsonSchema& camera_model_schema();

// The object points of a board (z = 0 plane, millimetres), in the order detect_checkerboard() returns corners.
[[nodiscard]] std::vector<cv::Point3f> board_points(const BoardSpec& board);

}  // namespace cloudscope
