#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/calibration/intrinsics.hpp>
#include <cloudscope/common/clock.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <random>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

const BoardSpec kBoard{.columns = 9, .rows = 6, .square_mm = 25.0};

CameraModel true_fisheye()
{
    CameraModel model;
    model.model = LensModel::Fisheye;
    model.width = 640;
    model.height = 480;
    model.fx = 300.0;
    model.fy = 302.0;
    model.cx = 325.0;
    model.cy = 236.0;
    model.distortion = {-0.05, 0.01, -0.002, 0.0005};
    model.board = kBoard;
    return model;
}

CameraModel true_pinhole()
{
    CameraModel model;
    model.model = LensModel::Pinhole;
    model.width = 640;
    model.height = 480;
    model.fx = 520.0;
    model.fy = 518.0;
    model.cx = 318.0;
    model.cy = 242.0;
    model.distortion = {-0.20, 0.05, 0.001, -0.0005, 0.0};
    model.board = kBoard;
    return model;
}

// Poses that spread the board over the field: shifted, tilted, near and far.
std::vector<std::pair<cv::Vec3d, cv::Vec3d>> poses()
{
    std::vector<std::pair<cv::Vec3d, cv::Vec3d>> out;
    const double board_width = (kBoard.columns - 1) * kBoard.square_mm;
    const double board_height = (kBoard.rows - 1) * kBoard.square_mm;
    for (int i = 0; i < 14; ++i) {
        const double angle_x = 0.35 * std::sin(i * 1.3);
        const double angle_y = 0.35 * std::cos(i * 0.9);
        const double angle_z = 0.2 * std::sin(i * 0.5);
        const double z = 420.0 + 60.0 * std::sin(i * 2.1);
        const double x = -board_width / 2.0 + 140.0 * std::cos(i * 1.1);
        const double y = -board_height / 2.0 + 100.0 * std::sin(i * 1.7);
        out.emplace_back(cv::Vec3d(angle_x, angle_y, angle_z), cv::Vec3d(x, y, z));
    }
    return out;
}

// Views of the board seen through a model, with a little noise on the corners.
std::vector<DetectedBoard> synthetic_views(const CameraModel& model, double noise_px)
{
    std::mt19937 random(7);
    std::normal_distribution<double> noise(0.0, std::max(noise_px, 1e-6));  // sigma 0 is not allowed
    const std::vector<cv::Point3f> object = board_points(kBoard);
    std::vector<DetectedBoard> views;
    for (const auto& [rotation, translation] : poses()) {
        std::vector<cv::Point2f> image_points;
        if (model.model == LensModel::Fisheye) {
            cv::fisheye::projectPoints(object, image_points, cv::Mat(rotation), cv::Mat(translation), model.camera_matrix(), model.distortion_vector());
        } else {
            cv::projectPoints(object, cv::Mat(rotation), cv::Mat(translation), model.camera_matrix(), model.distortion_vector(), image_points);
        }
        bool inside = true;
        for (cv::Point2f& point : image_points) {
            point.x += static_cast<float>(noise(random));
            point.y += static_cast<float>(noise(random));
            inside = inside && point.x > 2.0F && point.y > 2.0F && point.x < 638.0F && point.y < 478.0F;
        }
        if (inside) {
            views.push_back(DetectedBoard{.corners = image_points, .image_size = cv::Size(model.width, model.height)});
        }
    }
    return views;
}

// A flat checkerboard picture warped by a homography, and where its inner corners land.
std::pair<cv::Mat, std::vector<cv::Point2f>> rendered_board(const cv::Mat& homography)
{
    const int square = 40;
    const int margin = 60;
    const int width = (kBoard.columns + 1) * square + 2 * margin;
    const int height = (kBoard.rows + 1) * square + 2 * margin;
    cv::Mat flat(height, width, CV_8UC1, cv::Scalar(230));
    for (int row = 0; row <= kBoard.rows; ++row) {
        for (int column = 0; column <= kBoard.columns; ++column) {
            if ((row + column) % 2 == 0) {
                cv::rectangle(flat, cv::Rect(margin + column * square, margin + row * square, square, square), cv::Scalar(20), cv::FILLED);
            }
        }
    }
    std::vector<cv::Point2f> flat_corners;
    for (int row = 1; row <= kBoard.rows; ++row) {
        for (int column = 1; column <= kBoard.columns; ++column) {
            flat_corners.emplace_back(static_cast<float>(margin + column * square), static_cast<float>(margin + row * square));
        }
    }
    cv::Mat warped;
    cv::warpPerspective(flat, warped, homography, cv::Size(640, 480), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(180));
    cv::GaussianBlur(warped, warped, cv::Size(3, 3), 0.7);
    std::vector<cv::Point2f> corners;
    cv::perspectiveTransform(flat_corners, corners, homography);
    return {warped, corners};
}

}  // namespace

TEST_CASE("a fisheye model is recovered from synthetic views with sub-pixel reprojection", "[calibration][intrinsics]")
{
    const CameraModel truth = true_fisheye();
    const std::vector<DetectedBoard> views = synthetic_views(truth, 0.05);
    REQUIRE(views.size() >= 8);
    const auto fitted = fit_intrinsics(views, kBoard, LensModel::Fisheye);
    REQUIRE(outcome(fitted) == "ok");
    INFO("rms " << fitted->rms_px << " fx " << fitted->fx << " fy " << fitted->fy << " cx " << fitted->cx << " cy " << fitted->cy);
    CHECK(fitted->rms_px < 0.5);
    CHECK_THAT(fitted->fx, WithinRel(truth.fx, 0.01));
    CHECK_THAT(fitted->fy, WithinRel(truth.fy, 0.01));
    CHECK_THAT(fitted->cx, WithinAbs(truth.cx, 2.0));
    CHECK_THAT(fitted->cy, WithinAbs(truth.cy, 2.0));
    REQUIRE(fitted->distortion.size() == 4);
    CHECK_THAT(fitted->distortion[0], WithinAbs(truth.distortion[0], 0.02));
    CHECK(fitted->views == static_cast<int>(views.size()));
    CHECK(fitted->width == 640);
    for (const DetectedBoard& view : views) {
        const auto error = reprojection_error(*fitted, view);
        REQUIRE(outcome(error) == "ok");
        CHECK(*error < 0.5);
    }
    // A pixel maps to a ray and back: everywhere on the true model, and within the part of the image the views
    // covered on the fitted one (the fitted polynomial is not trusted beyond the corners it has seen).
    for (const cv::Point2d pixel : {cv::Point2d(320, 240), cv::Point2d(40, 60), cv::Point2d(600, 430), cv::Point2d(10, 470)}) {
        const cv::Point3d ray = pixel_to_ray(truth, pixel);
        CHECK_THAT(cv::norm(ray), WithinAbs(1.0, 1e-9));
        CHECK(ray.z > 0.0);
        const cv::Point2d back = ray_to_pixel(truth, ray);
        CHECK_THAT(back.x, WithinAbs(pixel.x, 0.01));
        CHECK_THAT(back.y, WithinAbs(pixel.y, 0.01));
    }
    for (const cv::Point2d pixel : {cv::Point2d(320, 240), cv::Point2d(200, 150), cv::Point2d(450, 330)}) {
        const cv::Point2d back = ray_to_pixel(*fitted, pixel_to_ray(*fitted, pixel));
        CHECK_THAT(back.x, WithinAbs(pixel.x, 0.01));
        CHECK_THAT(back.y, WithinAbs(pixel.y, 0.01));
        // The fitted and the true model agree on the direction of a pixel they both know.
        CHECK_THAT(cv::norm(pixel_to_ray(*fitted, pixel) - pixel_to_ray(truth, pixel)), WithinAbs(0.0, 0.002));
    }
    CHECK(pixel_to_ray(*fitted, cv::Point2d(fitted->cx, fitted->cy)).z > 0.9999);
}

TEST_CASE("a pinhole model with distortion is recovered as well", "[calibration][intrinsics]")
{
    const CameraModel truth = true_pinhole();
    const std::vector<DetectedBoard> views = synthetic_views(truth, 0.05);
    REQUIRE(views.size() >= 8);
    const auto fitted = fit_intrinsics(views, kBoard, LensModel::Pinhole);
    REQUIRE(outcome(fitted) == "ok");
    INFO("rms " << fitted->rms_px << " fx " << fitted->fx << " cx " << fitted->cx);
    CHECK(fitted->rms_px < 0.5);
    CHECK_THAT(fitted->fx, WithinRel(truth.fx, 0.01));
    CHECK_THAT(fitted->cx, WithinAbs(truth.cx, 2.0));
    REQUIRE(fitted->distortion.size() == 5);
    CHECK_THAT(fitted->distortion[0], WithinAbs(truth.distortion[0], 0.02));
    CHECK(fit_intrinsics({views[0], views[1]}, kBoard, LensModel::Pinhole).error().code == ErrorCode::InvalidArgument);
    std::vector<DetectedBoard> mixed = views;
    mixed.back().image_size = cv::Size(100, 100);
    CHECK_FALSE(fit_intrinsics(mixed, kBoard, LensModel::Pinhole));
}

TEST_CASE("the checkerboard detector finds the inner corners of a rendered board", "[calibration][intrinsics]")
{
    const cv::Mat homography = (cv::Mat_<double>(3, 3) << 0.95, 0.08, 70.0, -0.05, 0.9, 40.0, 0.0001, -0.00005, 1.0);
    const auto [image, expected] = rendered_board(homography);
    const auto detected = detect_checkerboard(image, kBoard);
    REQUIRE(outcome(detected) == "ok");
    REQUIRE(detected->corners.size() == expected.size());
    CHECK(detected->image_size == cv::Size(640, 480));
    // Order may start from either end: match every expected corner to its nearest detected one.
    double worst = 0.0;
    for (const cv::Point2f& wanted : expected) {
        double best = 1e9;
        for (const cv::Point2f& found : detected->corners) {
            best = std::min(best, cv::norm(found - wanted));
        }
        worst = std::max(worst, best);
    }
    INFO("worst corner error " << worst << " px");
    CHECK(worst < 1.0);  // rendering (perspective warp + blur) moves edges by a fraction of a pixel itself
    // Colour input is accepted; an empty picture and a plain picture are not boards.
    cv::Mat colour;
    cv::cvtColor(image, colour, cv::COLOR_GRAY2BGR);
    CHECK(detect_checkerboard(colour, kBoard));
    CHECK(detect_checkerboard(cv::Mat(480, 640, CV_8UC1, cv::Scalar(128)), kBoard).error().code == ErrorCode::NotFound);
    CHECK(detect_checkerboard(cv::Mat(), kBoard).error().code == ErrorCode::InvalidArgument);
    CHECK(detect_checkerboard(image, BoardSpec{.columns = 2, .rows = 2}).error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("the capture assistant keeps views that cover new ground", "[calibration][intrinsics]")
{
    CaptureAssistant assistant(cv::Size(640, 480), 4);
    CHECK(assistant.coverage() == 0.0);
    const CameraModel truth = true_fisheye();
    const std::vector<DetectedBoard> views = synthetic_views(truth, 0.0);
    REQUIRE(views.size() >= 4);
    std::string reason;
    CHECK(assistant.accept(views[0], &reason));
    CHECK(assistant.views() == 1);
    CHECK(assistant.coverage() > 0.0);
    // The same view again adds nothing.
    CHECK_FALSE(assistant.accept(views[0], &reason));
    CHECK(reason.find("move the board") != std::string::npos);
    CHECK(assistant.views() == 1);
    // Another size is refused.
    DetectedBoard other = views[1];
    other.image_size = cv::Size(320, 240);
    CHECK_FALSE(assistant.accept(other, &reason));
    int accepted = 1;
    for (std::size_t i = 1; i < views.size(); ++i) {
        if (assistant.accept(views[i])) {
            ++accepted;
        }
    }
    CHECK(assistant.views() == accepted);
    CHECK(accepted >= 3);
    CHECK(assistant.coverage() >= 0.4);  // the synthetic poses stay away from the corners
    CHECK(assistant.accepted().size() == static_cast<std::size_t>(accepted));
    CHECK(assistant.covered_cells().size() == 16);
}

TEST_CASE("camera models round-trip through the STRATIA file format", "[calibration][intrinsics]")
{
    const TempWorkspace workspace;
    CameraModel model = true_fisheye();
    model.rms_px = 0.21;
    model.views = 12;
    model.camera_id = "uvc:0c45:636d:1";
    model.camera_name = "Arducam B0268";
    model.calibration_id = "intrinsics-20261009T101530Z";
    model.calibrated = parse_iso8601("2026-10-09T10:15:30.123Z").value();

    const nlohmann::json document = to_json(model);
    CHECK(camera_model_schema().validate(document).empty());
    CHECK(document["schema"] == "cloudscope.camera_model/1");
    CHECK(document["model"] == "opencv_fisheye");
    CHECK(document["distortion"].size() == 4);
    CHECK(document["calibrated_utc"] == "2026-10-09T10:15:30.123+00:00");

    const std::filesystem::path file = workspace.path("calibration/b0268.camera.json");
    REQUIRE(outcome(save_camera_model(model, file)) == "ok");
    const auto loaded = load_camera_model(file);
    REQUIRE(outcome(loaded) == "ok");
    CHECK(loaded->model == LensModel::Fisheye);
    CHECK(loaded->fx == model.fx);
    CHECK(loaded->cy == model.cy);
    CHECK(loaded->distortion == model.distortion);
    CHECK(loaded->rms_px == 0.21);
    CHECK(loaded->views == 12);
    CHECK(loaded->board.columns == 9);
    CHECK(loaded->board.square_mm == 25.0);
    CHECK(loaded->camera_id == "uvc:0c45:636d:1");
    CHECK(loaded->calibration_id == "intrinsics-20261009T101530Z");
    CHECK(loaded->calibrated == model.calibrated);
    CHECK_FALSE(std::filesystem::exists(workspace.path("calibration/b0268.camera.json.part")));

    nlohmann::json wrong = document;
    wrong["distortion"] = {0.1, 0.2, 0.3, 0.4, 0.5};  // five coefficients on a fisheye
    CHECK(camera_model_from_json(wrong).error().code == ErrorCode::Validation);
    wrong = document;
    wrong["model"] = "ocamcalib";
    CHECK(camera_model_from_json(wrong).error().code == ErrorCode::Validation);
    wrong = document;
    wrong.erase("intrinsics");
    CHECK(camera_model_from_json(wrong).error().code == ErrorCode::Validation);
    CHECK(load_camera_model(workspace.path("missing.json")).error().code == ErrorCode::NotFound);
    CHECK(lens_model_from_string("fisheye").value() == LensModel::Fisheye);
    CHECK(lens_model_from_string("opencv_pinhole").value() == LensModel::Pinhole);
    CHECK_FALSE(lens_model_from_string("omnidir"));
    CHECK(to_string(LensModel::Pinhole) == "opencv_pinhole");
}
