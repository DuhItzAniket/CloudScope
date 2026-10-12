// P038: focus measure, pixel inspector, region statistics and the loupe on known images.

#include "image_tools.hpp"

#include <cloudscope/sim/synthetic_sky.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <opencv2/imgproc.hpp>

using Catch::Matchers::WithinAbs;
using namespace cloudscope;
using namespace cloudscope::desktop;

TEST_CASE("a uniform region has zero spread and zero focus; a sharp edge scores higher than a blurred one",
          "[desktop][image_tools]")
{
    cv::Mat image = cv::Mat::zeros(100, 100, CV_8UC3);
    image.setTo(cv::Scalar(10, 20, 30));
    const auto flat = roi_statistics(image, cv::Rect(10, 10, 50, 50));
    REQUIRE(flat.has_value());
    CHECK(flat->pixels == 2500);
    CHECK_THAT(flat->mean[0], WithinAbs(10.0, 1e-9));
    CHECK_THAT(flat->mean[1], WithinAbs(20.0, 1e-9));
    CHECK_THAT(flat->mean[2], WithinAbs(30.0, 1e-9));
    // OpenCV's spread is sqrt(E[x^2] - E[x]^2): rounding leaves a few millionths on some CPUs (arm64), not zero.
    CHECK_THAT(flat->std_dev[0], WithinAbs(0.0, 1e-5));
    CHECK(flat->minimum[2] == 30);
    CHECK(flat->maximum[2] == 30);
    CHECK_THAT(flat->focus, WithinAbs(0.0, 1e-5));
    CHECK_THAT(flat->luma_mean, WithinAbs(0.299 * 30.0 + 0.587 * 20.0 + 0.114 * 10.0, 1e-9));

    cv::Mat edge = cv::Mat::zeros(100, 100, CV_8UC1);
    edge(cv::Rect(50, 0, 50, 100)).setTo(255);
    cv::Mat blurred;
    cv::GaussianBlur(edge, blurred, cv::Size(15, 15), 4.0);
    CHECK(focus_measure(edge) > focus_measure(blurred));
    CHECK(focus_measure(blurred) > 0.0);
    CHECK(focus_measure(edge, cv::Rect(0, 0, 20, 100)) == 0.0);  // a flat part of the image
}

TEST_CASE("the pixel inspector reads the channels and the luma, and says when it is off the image",
          "[desktop][image_tools]")
{
    cv::Mat image = cv::Mat::zeros(10, 10, CV_8UC3);
    image.at<cv::Vec3b>(3, 7) = cv::Vec3b(100, 150, 200);  // B, G, R
    const PixelValue value = pixel_value(image, 7, 3);
    REQUIRE(value.valid);
    CHECK(value.blue == 100);
    CHECK(value.green == 150);
    CHECK(value.red == 200);
    CHECK(value.luma == static_cast<int>(std::lround(0.299 * 200 + 0.587 * 150 + 0.114 * 100)));
    CHECK_FALSE(pixel_value(image, 10, 3).valid);
    CHECK_FALSE(pixel_value(image, -1, 3).valid);
    const cv::Mat grey = cv::Mat::ones(4, 4, CV_8UC1) * 77;
    const PixelValue g = pixel_value(grey, 1, 1);
    CHECK(g.valid);
    CHECK(g.luma == 77);
    CHECK(g.red == 77);
}

TEST_CASE("the region is clipped to the image and a region fully outside gives nothing", "[desktop][image_tools]")
{
    const cv::Mat image = cv::Mat::zeros(40, 60, CV_8UC3);
    CHECK(clamp_roi(cv::Rect(-10, -10, 30, 30), image.size()) == cv::Rect(0, 0, 20, 20));
    CHECK(clamp_roi(cv::Rect(50, 30, 30, 30), image.size()) == cv::Rect(50, 30, 10, 10));
    CHECK(clamp_roi(cv::Rect(100, 100, 5, 5), image.size()).empty());
    CHECK_FALSE(roi_statistics(image, cv::Rect(100, 100, 5, 5)).has_value());
    const auto part = roi_statistics(image, cv::Rect(-10, -10, 30, 30));
    REQUIRE(part.has_value());
    CHECK(part->pixels == 400);
}

TEST_CASE("the loupe magnifies without interpolation and pads outside the image with black", "[desktop][image_tools]")
{
    cv::Mat image = cv::Mat::zeros(20, 20, CV_8UC3);
    image.at<cv::Vec3b>(10, 10) = cv::Vec3b(0, 0, 255);             // one red pixel at the centre
    const QImage loupe = loupe_image(image, QPoint(10, 10), 2, 4);  // 5 x 5 source pixels, 4x
    REQUIRE(loupe.width() == 20);
    REQUIRE(loupe.height() == 20);
    // The centre source pixel covers a 4 x 4 block of pure red; its neighbours stay black (no blending).
    for (int y = 8; y < 12; ++y) {
        for (int x = 8; x < 12; ++x) {
            CHECK(loupe.pixelColor(x, y) == QColor(255, 0, 0));
        }
    }
    CHECK(loupe.pixelColor(7, 10) == QColor(0, 0, 0));
    CHECK(loupe.pixelColor(12, 10) == QColor(0, 0, 0));
    // At the corner, the part outside the image is black and the rest is the image.
    const QImage corner = loupe_image(image, QPoint(0, 0), 2, 1);
    REQUIRE(corner.width() == 5);
    CHECK(corner.pixelColor(0, 0) == QColor(0, 0, 0));
    CHECK(loupe_image(cv::Mat(), QPoint(0, 0), 2, 2).isNull());
}

TEST_CASE("a synthetic sky's statistics inside the Sun's disc are saturated", "[desktop][image_tools]")
{
    sim::SyntheticSkySpec spec;
    spec.width = 320;
    spec.height = 240;
    spec.sun_x = 0.5;
    spec.sun_y = 0.5;
    spec.sun_radius_px = 20.0;
    const auto sky = sim::make_synthetic_sky(spec);
    REQUIRE(sky.has_value());
    const auto centre = roi_statistics(sky->image, cv::Rect(155, 115, 10, 10));
    REQUIRE(centre.has_value());
    CHECK(centre->minimum[0] == 255);
    CHECK(centre->minimum[1] == 255);
    CHECK(centre->minimum[2] == 255);
    const QImage q = to_qimage(sky->image);
    CHECK(q.width() == 320);
    CHECK(q.format() == QImage::Format_BGR888);
}
