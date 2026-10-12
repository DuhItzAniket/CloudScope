// P036: the display stretch, scopes and highlights, against offline computation.

#include "display_stretch.hpp"

#include <cloudscope/capture/statistics.hpp>
#include <cloudscope/sim/synthetic_sky.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <opencv2/imgproc.hpp>

#include <numeric>

using Catch::Matchers::WithinAbs;
using namespace cloudscope;
using namespace cloudscope::desktop;

namespace {

cv::Mat gradient_image(int width, int height)
{
    cv::Mat image(height, width, CV_8UC3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto level = static_cast<std::uint8_t>(x * 255 / (width - 1));
            image.at<cv::Vec3b>(y, x) = cv::Vec3b(level, static_cast<std::uint8_t>(255 - level), 128);
        }
    }
    return image;
}

}  // namespace

TEST_CASE("the identity table leaves every level alone and is detected as such", "[desktop][stretch]")
{
    const Lut identity = make_lut(0.0, 1.0, 1.0);
    for (int level = 0; level < 256; ++level) {
        CHECK(identity[static_cast<std::size_t>(level)] == level);
    }
    CHECK(is_identity(identity));
    CHECK_FALSE(is_identity(make_lut(0.1, 0.9, 1.0)));
    StretchSettings disabled;
    disabled.enabled = false;
    disabled.black = 0.5;
    CHECK(is_identity(stretch_lut(disabled, Histogram{})));
}

TEST_CASE("levels and gamma map black, white and the midpoint as computed offline", "[desktop][stretch]")
{
    const Lut lut = make_lut(0.25, 0.75, 2.0);
    CHECK(lut[0] == 0);
    CHECK(lut[63] == 0);     // below the black point
    CHECK(lut[192] == 255);  // above the white point
    CHECK(lut[255] == 255);
    // Midpoint: ((0.5 - 0.25) / 0.5) ^ (1/2) = 0.7071 -> 180.3
    const int mid = lut[static_cast<std::size_t>(std::lround(0.5 * 255))];
    CHECK(std::abs(mid - static_cast<int>(std::lround(std::sqrt(0.5) * 255.0))) <= 1);
}

TEST_CASE("automatic levels follow the histogram's percentiles", "[desktop][stretch]")
{
    Histogram histogram{};
    for (int level = 40; level <= 200; ++level) {
        histogram[static_cast<std::size_t>(level)] = 100;  // flat between 40 and 200
    }
    const Levels levels = auto_levels(histogram, 0.0, 1.0);
    CHECK_THAT(levels.black * 255.0, WithinAbs(40.0, 1e-9));
    CHECK_THAT(levels.white * 255.0, WithinAbs(200.0, 1e-9));
    const Levels inner = auto_levels(histogram, 0.1, 0.9);
    CHECK(inner.black * 255.0 > 40.0);
    CHECK(inner.white * 255.0 < 200.0);
    CHECK(inner.white > inner.black);
    const Levels flat = auto_levels(Histogram{}, 0.001, 0.999);
    CHECK(flat.black == 0.0);
    CHECK(flat.white == 1.0);
    StretchSettings automatic;
    automatic.enabled = true;
    automatic.automatic = true;
    automatic.auto_low = 0.0;
    automatic.auto_high = 1.0;
    const Lut lut = stretch_lut(automatic, histogram);
    CHECK(lut[40] == 0);
    CHECK(lut[200] == 255);
}

TEST_CASE("applying the stretch returns a new image and leaves the original unchanged", "[desktop][stretch]")
{
    const cv::Mat original = gradient_image(256, 4);
    const cv::Mat copy = original.clone();
    const cv::Mat stretched = apply_lut(original, make_lut(0.5, 1.0, 1.0));
    CHECK(stretched.data != original.data);
    CHECK(cv::norm(original, copy, cv::NORM_INF) == 0.0);
    // The left half (blue channel below 128) is now black in the blue channel.
    CHECK(stretched.at<cv::Vec3b>(0, 10)[0] == 0);
    CHECK(stretched.at<cv::Vec3b>(0, 255)[0] == 255);
}

TEST_CASE("channel histograms count every pixel of every channel", "[desktop][stretch]")
{
    const cv::Mat image = gradient_image(256, 3);
    const ChannelHistograms channels = channel_histograms(image);
    for (const Histogram& channel : channels) {
        CHECK(std::accumulate(channel.begin(), channel.end(), std::uint64_t{0}) == 256ULL * 3ULL);
    }
    CHECK(channels[2][128] == 256 * 3);  // red is 128 everywhere
    CHECK(channels[0][0] == 3);          // blue: one column at 0
    const ChannelHistograms grey = channel_histograms(luma_of(image));
    CHECK(grey[0] == grey[1]);
    CHECK(grey[1] == grey[2]);
}

TEST_CASE("the RGB parade has one normalised distribution per strip", "[desktop][stretch]")
{
    const cv::Mat image = gradient_image(512, 64);
    const std::array<cv::Mat, 3> parade = rgb_parade(image, 128, 32);
    for (const cv::Mat& channel : parade) {
        REQUIRE(channel.rows == 32);
        REQUIRE(channel.cols == 128);
        for (int x = 0; x < channel.cols; ++x) {
            double column = 0.0;
            for (int y = 0; y < channel.rows; ++y) {
                column += static_cast<double>(channel.at<float>(y, x));
            }
            CHECK_THAT(column, WithinAbs(1.0, 1e-3));
        }
    }
    // Red is 128 everywhere: all of its weight sits in the bin of 128.
    CHECK_THAT(static_cast<double>(parade[2].at<float>(128 * 32 / 256, 10)), WithinAbs(1.0, 1e-3));
}

TEST_CASE("the over-exposure highlight paints exactly the saturated pixels of a synthetic sky", "[desktop][stretch]")
{
    sim::SyntheticSkySpec spec;
    spec.width = 320;
    spec.height = 240;
    spec.sun_radius_px = 15.0;
    const auto sky = sim::make_synthetic_sky(spec);
    REQUIRE(sky.has_value());
    cv::Mat preview = sky->image.clone();
    const int painted = highlight_overexposed(preview, luma_of(sky->image), 255, cv::Vec3b(255, 0, 255));
    CHECK(painted == sky->saturated_pixels);
    CHECK(painted > 0);
    // Not applied to the source: it still holds exactly the saturated pixels it had.
    cv::Mat white;
    cv::inRange(sky->image, cv::Scalar(255, 255, 255), cv::Scalar(255, 255, 255), white);
    CHECK(cv::countNonZero(white) == sky->saturated_pixels);
    cv::Mat magenta;
    cv::inRange(preview, cv::Scalar(255, 0, 255), cv::Scalar(255, 0, 255), magenta);
    CHECK(cv::countNonZero(magenta) == painted);
}

TEST_CASE("the night rendering is red only and the clipping fractions come from the histogram", "[desktop][stretch]")
{
    const cv::Mat image = gradient_image(64, 4);
    const cv::Mat red = night_vision(image);
    REQUIRE(red.type() == CV_8UC3);
    for (int x = 0; x < red.cols; ++x) {
        const auto& px = red.at<cv::Vec3b>(0, x);
        CHECK(px[0] == 0);
        CHECK(px[1] == 0);
    }
    Histogram histogram{};
    histogram[0] = 10;
    histogram[255] = 30;
    histogram[100] = 60;
    const ClippingFractions clipping = clipping_fractions(histogram);
    CHECK_THAT(clipping.black, WithinAbs(0.1, 1e-12));
    CHECK_THAT(clipping.white, WithinAbs(0.3, 1e-12));
}
