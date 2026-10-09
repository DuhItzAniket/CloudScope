#include "cloudscope/calibration/intrinsics.hpp"

#include "cloudscope/common/build_info.hpp"

#include <QtCore/QFile>
#include <fmt/format.h>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace cloudscope {

std::string_view to_string(LensModel model)
{
    switch (model) {
    case LensModel::Pinhole:
        return "opencv_pinhole";
    case LensModel::Fisheye:
        return "opencv_fisheye";
    }
    return "unknown";
}

Expected<LensModel> lens_model_from_string(std::string_view text)
{
    if (text == "opencv_pinhole" || text == "pinhole") {
        return LensModel::Pinhole;
    }
    if (text == "opencv_fisheye" || text == "fisheye") {
        return LensModel::Fisheye;
    }
    return fail(ErrorCode::InvalidArgument, fmt::format("unknown lens model '{}'", text));
}

cv::Mat CameraModel::camera_matrix() const
{
    cv::Mat k = cv::Mat::eye(3, 3, CV_64F);
    k.at<double>(0, 0) = fx;
    k.at<double>(1, 1) = fy;
    k.at<double>(0, 2) = cx;
    k.at<double>(1, 2) = cy;
    return k;
}

cv::Mat CameraModel::distortion_vector() const
{
    cv::Mat d(1, static_cast<int>(distortion.size()), CV_64F);
    for (std::size_t i = 0; i < distortion.size(); ++i) {
        d.at<double>(0, static_cast<int>(i)) = distortion[i];
    }
    return d;
}

std::vector<cv::Point3f> board_points(const BoardSpec& board)
{
    std::vector<cv::Point3f> points;
    points.reserve(static_cast<std::size_t>(board.columns) * static_cast<std::size_t>(board.rows));
    for (int row = 0; row < board.rows; ++row) {
        for (int column = 0; column < board.columns; ++column) {
            points.emplace_back(static_cast<float>(column * board.square_mm), static_cast<float>(row * board.square_mm),
                                0.0F);
        }
    }
    return points;
}

Expected<DetectedBoard> detect_checkerboard(const cv::Mat& image, const BoardSpec& board)
{
    if (image.empty() || image.depth() != CV_8U) {
        return fail(ErrorCode::InvalidArgument, "checkerboard detection needs an 8-bit image");
    }
    if (board.columns < 3 || board.rows < 3) {
        return fail(ErrorCode::InvalidArgument, "a board needs at least 3x3 inner corners");
    }
    cv::Mat grey;
    if (image.channels() == 3) {
        cv::cvtColor(image, grey, cv::COLOR_BGR2GRAY);
    } else if (image.channels() == 4) {
        cv::cvtColor(image, grey, cv::COLOR_BGRA2GRAY);
    } else {
        grey = image;
    }
    const cv::Size pattern(board.columns, board.rows);
    std::vector<cv::Point2f> corners;
    bool found = false;
    try {
        found = cv::findChessboardCornersSB(
            grey, pattern, corners, cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_EXHAUSTIVE | cv::CALIB_CB_ACCURACY);
    } catch (const cv::Exception&) {
        found = false;
    }
    if (!found) {
        // The classic detector as a fallback, refined to sub-pixel corners.
        found = cv::findChessboardCorners(grey, pattern, corners,
                                          cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
        if (found) {
            cv::cornerSubPix(grey, corners, cv::Size(11, 11), cv::Size(-1, -1),
                             cv::TermCriteria(cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 40, 0.001));
        }
    }
    if (!found || corners.size() != static_cast<std::size_t>(board.columns) * static_cast<std::size_t>(board.rows)) {
        return fail(ErrorCode::NotFound, fmt::format("no {}x{} checkerboard in the image", board.columns, board.rows));
    }
    return DetectedBoard{.corners = std::move(corners), .image_size = grey.size()};
}

CaptureAssistant::CaptureAssistant(cv::Size image_size, int grid)
    : image_size_(image_size),
      grid_(std::max(grid, 1)),
      covered_(static_cast<std::size_t>(grid_) * static_cast<std::size_t>(grid_), false)
{
}

bool CaptureAssistant::accept(const DetectedBoard& board, std::string* reason)
{
    if (board.image_size != image_size_) {
        if (reason != nullptr) {
            *reason = "the view has another image size";
        }
        return false;
    }
    std::vector<bool> cells(covered_.size(), false);
    for (const cv::Point2f& corner : board.corners) {
        const int column =
            std::clamp(static_cast<int>(corner.x / static_cast<float>(image_size_.width) * static_cast<float>(grid_)),
                       0, grid_ - 1);
        const int row =
            std::clamp(static_cast<int>(corner.y / static_cast<float>(image_size_.height) * static_cast<float>(grid_)),
                       0, grid_ - 1);
        cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(grid_) + static_cast<std::size_t>(column)] =
            true;
    }
    int added = 0;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (cells[i] && !covered_[i]) {
            ++added;
        }
    }
    if (added == 0) {
        if (reason != nullptr) {
            *reason = "no new part of the image is covered: move the board";
        }
        return false;
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        covered_[i] = covered_[i] || cells[i];
    }
    views_.push_back(board);
    if (reason != nullptr) {
        *reason = fmt::format("{} new cell(s) covered", added);
    }
    return true;
}

double CaptureAssistant::coverage() const
{
    const auto count = static_cast<double>(std::ranges::count(covered_, true));
    return count / static_cast<double>(covered_.size());
}

Expected<CameraModel> fit_intrinsics(const std::vector<DetectedBoard>& views, const BoardSpec& board, LensModel model)
{
    if (views.size() < 3) {
        return fail(ErrorCode::InvalidArgument, fmt::format("a fit needs at least 3 views, got {}", views.size()));
    }
    const cv::Size size = views.front().image_size;
    const std::vector<cv::Point3f> object = board_points(board);
    std::vector<std::vector<cv::Point3f>> object_points;
    std::vector<std::vector<cv::Point2f>> image_points;
    for (const DetectedBoard& view : views) {
        if (view.image_size != size) {
            return fail(ErrorCode::InvalidArgument, "the views have different image sizes");
        }
        if (view.corners.size() != object.size()) {
            return fail(ErrorCode::InvalidArgument, "a view has the wrong number of corners for the board");
        }
        object_points.push_back(object);
        image_points.push_back(view.corners);
    }
    CameraModel out;
    out.model = model;
    out.width = size.width;
    out.height = size.height;
    out.views = static_cast<int>(views.size());
    out.board = board;
    // OpenCV estimates the starting point from the views themselves (homographies); a guessed focal length far
    // from the truth sent the fisheye fit into a wrong minimum.
    cv::Mat k = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat d;
    std::vector<cv::Mat> rvecs;
    std::vector<cv::Mat> tvecs;
    try {
        if (model == LensModel::Fisheye) {
            d = cv::Mat::zeros(4, 1, CV_64F);
            const int flags = cv::fisheye::CALIB_RECOMPUTE_EXTRINSIC | cv::fisheye::CALIB_FIX_SKEW;
            out.rms_px =
                cv::fisheye::calibrate(object_points, image_points, size, k, d, rvecs, tvecs, flags,
                                       cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 200, 1e-8));
        } else {
            d = cv::Mat::zeros(5, 1, CV_64F);
            out.rms_px = cv::calibrateCamera(object_points, image_points, size, k, d, rvecs, tvecs, 0);
        }
    } catch (const cv::Exception& e) {
        return fail(ErrorCode::Internal, fmt::format("the calibration did not converge: {}", e.what()));
    }
    out.fx = k.at<double>(0, 0);
    out.fy = k.at<double>(1, 1);
    out.cx = k.at<double>(0, 2);
    out.cy = k.at<double>(1, 2);
    out.distortion.assign(d.begin<double>(), d.end<double>());
    if (!std::isfinite(out.rms_px) || out.fx <= 0.0 || out.fy <= 0.0) {
        return fail(ErrorCode::Internal, "the calibration produced an unusable model");
    }
    return out;
}

Expected<double> reprojection_error(const CameraModel& model, const DetectedBoard& view)
{
    const std::vector<cv::Point3f> object = board_points(model.board);
    if (view.corners.size() != object.size()) {
        return fail(ErrorCode::InvalidArgument, "the view has the wrong number of corners for the model's board");
    }
    const cv::Mat k = model.camera_matrix();
    const cv::Mat d = model.distortion_vector();
    std::vector<cv::Point2f> projected;
    try {
        cv::Mat rvec;
        cv::Mat tvec;
        if (model.model == LensModel::Fisheye) {
            // solvePnP works on undistorted normalised points; project them back through the fisheye model.
            std::vector<cv::Point2f> undistorted;
            cv::fisheye::undistortPoints(view.corners, undistorted, k, d);
            cv::solvePnP(object, undistorted, cv::Mat::eye(3, 3, CV_64F), cv::noArray(), rvec, tvec, false,
                         cv::SOLVEPNP_IPPE);
            cv::fisheye::projectPoints(object, projected, rvec, tvec, k, d);
        } else {
            cv::solvePnP(object, view.corners, k, d, rvec, tvec, false, cv::SOLVEPNP_IPPE);
            cv::projectPoints(object, rvec, tvec, k, d, projected);
        }
    } catch (const cv::Exception& e) {
        return fail(ErrorCode::Internal, fmt::format("reprojection failed: {}", e.what()));
    }
    double sum = 0.0;
    for (std::size_t i = 0; i < object.size(); ++i) {
        const cv::Point2f delta = projected[i] - view.corners[i];
        sum += static_cast<double>(delta.x) * static_cast<double>(delta.x) +
               static_cast<double>(delta.y) * static_cast<double>(delta.y);
    }
    return std::sqrt(sum / static_cast<double>(object.size()));
}

cv::Point3d pixel_to_ray(const CameraModel& model, cv::Point2d pixel)
{
    const std::vector<cv::Point2d> in{pixel};
    std::vector<cv::Point2d> normalised;
    if (model.model == LensModel::Fisheye) {
        cv::fisheye::undistortPoints(in, normalised, model.camera_matrix(), model.distortion_vector());
    } else {
        cv::undistortPoints(in, normalised, model.camera_matrix(), model.distortion_vector());
    }
    const cv::Point3d ray(normalised.front().x, normalised.front().y, 1.0);
    return ray / cv::norm(ray);
}

cv::Point2d ray_to_pixel(const CameraModel& model, cv::Point3d ray)
{
    const std::vector<cv::Point3d> in{ray};
    std::vector<cv::Point2d> out;
    const cv::Mat zero = cv::Mat::zeros(3, 1, CV_64F);
    if (model.model == LensModel::Fisheye) {
        cv::fisheye::projectPoints(in, out, zero, zero, model.camera_matrix(), model.distortion_vector());
    } else {
        cv::projectPoints(in, zero, zero, model.camera_matrix(), model.distortion_vector(), out);
    }
    return out.front();
}

nlohmann::json to_json(const CameraModel& model)
{
    const BuildInfo& build = build_info();
    nlohmann::json out{
        {"schema", "cloudscope.camera_model/1"},
        {"model", std::string(to_string(model.model))},
        {"image", {{"width", model.width}, {"height", model.height}}},
        {"intrinsics", {{"fx", model.fx}, {"fy", model.fy}, {"cx", model.cx}, {"cy", model.cy}}},
        {"distortion", model.distortion},
        {"fit",
         {{"rms_px", model.rms_px},
          {"views", model.views},
          {"board",
           {{"columns", model.board.columns}, {"rows", model.board.rows}, {"square_mm", model.board.square_mm}}}}},
        {"camera", {{"id", model.camera_id}, {"name", model.camera_name}}},
        {"calibration_id", model.calibration_id},
        {"calibrated_utc", format_iso8601(model.calibrated)},
        {"software", {{"name", "CloudScope"}, {"version", build.version}, {"git_revision", build.git_revision}}},
    };
    return out;
}

const JsonSchema& camera_model_schema()
{
    static const JsonSchema schema = [] {
        QFile file(QStringLiteral(":/cloudscope/camera_model.schema.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("the camera model schema is missing from the resources");
        }
        const QByteArray bytes = file.readAll();
        auto compiled = JsonSchema::compile(nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size()));
        if (!compiled) {
            throw std::runtime_error("the camera model schema does not compile: " + compiled.error().to_string());
        }
        return *compiled;
    }();
    return schema;
}

Expected<CameraModel> camera_model_from_json(const nlohmann::json& document)
{
    const auto issues = camera_model_schema().validate(document);
    if (!issues.empty()) {
        return fail(ErrorCode::Validation, fmt::format("camera model: {}", issues.front().to_string()));
    }
    CameraModel model;
    const auto lens = lens_model_from_string(document["model"].get<std::string>());
    if (!lens) {
        return fail(lens.error());
    }
    model.model = *lens;
    model.width = document["image"]["width"].get<int>();
    model.height = document["image"]["height"].get<int>();
    model.fx = document["intrinsics"]["fx"].get<double>();
    model.fy = document["intrinsics"]["fy"].get<double>();
    model.cx = document["intrinsics"]["cx"].get<double>();
    model.cy = document["intrinsics"]["cy"].get<double>();
    model.distortion = document["distortion"].get<std::vector<double>>();
    const std::size_t expected = model.model == LensModel::Fisheye ? 4 : 5;
    if (model.distortion.size() != expected) {
        return fail(ErrorCode::Validation, fmt::format("camera model: {} needs {} distortion coefficients, got {}",
                                                       to_string(model.model), expected, model.distortion.size()));
    }
    model.rms_px = document["fit"]["rms_px"].get<double>();
    model.views = document["fit"]["views"].get<int>();
    const nlohmann::json& board = document["fit"]["board"];
    model.board = BoardSpec{.columns = board["columns"].get<int>(),
                            .rows = board["rows"].get<int>(),
                            .square_mm = board["square_mm"].get<double>()};
    model.camera_id = document["camera"]["id"].get<std::string>();
    model.camera_name = document["camera"]["name"].get<std::string>();
    model.calibration_id = document["calibration_id"].get<std::string>();
    const auto calibrated = parse_iso8601(document["calibrated_utc"].get<std::string>());
    if (!calibrated) {
        return fail(ErrorCode::Parse, "camera model: calibrated_utc is not a time");
    }
    model.calibrated = *calibrated;
    return model;
}

Expected<void> save_camera_model(const CameraModel& model, const std::filesystem::path& file)
{
    std::error_code ignored;
    std::filesystem::create_directories(file.parent_path(), ignored);
    const std::filesystem::path temporary(file.native() + std::filesystem::path(".part").native());
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail(ErrorCode::Io, fmt::format("could not write {}", temporary.string()));
        }
        out << to_json(model).dump(2) << "\n";
        if (!out) {
            return fail(ErrorCode::Io, fmt::format("could not write {}", temporary.string()));
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::remove(file, error);
        std::filesystem::rename(temporary, file, error);
        if (error) {
            return fail(ErrorCode::Io, fmt::format("could not move {} into place", temporary.string()));
        }
    }
    return {};
}

Expected<CameraModel> load_camera_model(const std::filesystem::path& file)
{
    std::ifstream in(file);
    if (!in) {
        return fail(ErrorCode::NotFound, fmt::format("could not read {}", file.string()));
    }
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception& e) {
        return fail(ErrorCode::Parse, fmt::format("{}: {}", file.string(), e.what()));
    }
    return camera_model_from_json(document);
}

}  // namespace cloudscope
