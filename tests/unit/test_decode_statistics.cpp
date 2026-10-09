#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/capture/decode.hpp>
#include <cloudscope/capture/frame.hpp>
#include <cloudscope/capture/statistics.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstring>
#include <vector>

using namespace cloudscope;
using namespace cloudscope::test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

cv::Mat test_picture(int width = 96, int height = 64)
{
    cv::Mat image(height, width, CV_8UC3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.at<cv::Vec3b>(y, x) =
                cv::Vec3b(static_cast<std::uint8_t>(x * 255 / width), static_cast<std::uint8_t>(y * 255 / height), 128);
        }
    }
    return image;
}

std::unique_ptr<Frame> frame_of(const std::vector<std::byte>& bytes, PixelFormat format, int width, int height,
                                std::size_t stride)
{
    auto frame = std::make_unique<Frame>(std::max<std::size_t>(bytes.size(), 16));
    std::memcpy(frame->buffer().data(), bytes.data(), bytes.size());
    frame->set_size(bytes.size());
    frame->info() = {.sequence = 1,
                     .captured = {},
                     .width = width,
                     .height = height,
                     .format = format,
                     .stride = stride,
                     .simulated = true};
    return frame;
}

std::vector<std::byte> bytes_of(const cv::Mat& image)
{
    std::vector<std::byte> out(image.total() * image.elemSize());
    std::memcpy(out.data(), image.data, out.size());
    return out;
}

double psnr(const cv::Mat& a, const cv::Mat& b)
{
    cv::Mat difference;
    cv::absdiff(a, b, difference);
    difference.convertTo(difference, CV_64F);
    difference = difference.mul(difference);
    const double mse = cv::mean(difference)[0] + cv::mean(difference)[1] + cv::mean(difference)[2];
    return mse <= 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 * 3.0 / mse);
}

}  // namespace

TEST_CASE("MJPEG frames decode to the picture that was encoded", "[capture][decode]")
{
    const cv::Mat picture = test_picture();
    std::vector<std::uint8_t> encoded;
    REQUIRE(cv::imencode(".jpg", picture, encoded, {cv::IMWRITE_JPEG_QUALITY, 95}));
    std::vector<std::byte> bytes(encoded.size());
    std::memcpy(bytes.data(), encoded.data(), bytes.size());
    const auto frame = frame_of(bytes, PixelFormat::Mjpeg, picture.cols, picture.rows, 0);

    const auto info = jpeg_info(frame->data());
    REQUIRE(outcome(info) == "ok");
    CHECK(info->width == picture.cols);
    CHECK(info->height == picture.rows);

    const auto decoded = decode_bgr8(*frame);
    REQUIRE(outcome(decoded) == "ok");
    CHECK(decoded->type() == CV_8UC3);
    CHECK(decoded->cols == picture.cols);
    CHECK(psnr(*decoded, picture) > 35.0);

    const auto gray = decode_gray8(*frame);
    REQUIRE(outcome(gray) == "ok");
    CHECK(gray->type() == CV_8UC1);

    bytes.resize(bytes.size() / 2);  // a truncated JPEG is a parse error, not a crash
    const auto broken = frame_of(bytes, PixelFormat::Mjpeg, picture.cols, picture.rows, 0);
    CHECK(outcome(decode_bgr8(*broken)) == "Parse");
    CHECK(outcome(jpeg_info(std::vector<std::byte>(8, std::byte{0}))) == "Parse");
}

TEST_CASE("uncompressed formats decode with the right channels and depths", "[capture][decode]")
{
    const cv::Mat picture = test_picture(32, 16);
    const auto bgr = frame_of(bytes_of(picture), PixelFormat::Bgr8, 32, 16, 96);
    const auto decoded_bgr = decode_native(*bgr);
    REQUIRE(outcome(decoded_bgr) == "ok");
    CHECK(cv::norm(*decoded_bgr, picture, cv::NORM_INF) == 0.0);

    cv::Mat rgb;
    cv::cvtColor(picture, rgb, cv::COLOR_BGR2RGB);
    const auto decoded_rgb = decode_native(*frame_of(bytes_of(rgb), PixelFormat::Rgb8, 32, 16, 96));
    REQUIRE(outcome(decoded_rgb) == "ok");
    CHECK(cv::norm(*decoded_rgb, picture, cv::NORM_INF) == 0.0);

    cv::Mat gray;
    cv::cvtColor(picture, gray, cv::COLOR_BGR2GRAY);
    const auto decoded_gray = decode_native(*frame_of(bytes_of(gray), PixelFormat::Gray8, 32, 16, 32));
    REQUIRE(outcome(decoded_gray) == "ok");
    CHECK(decoded_gray->type() == CV_8UC1);
    const auto as_bgr = decode_bgr8(*frame_of(bytes_of(gray), PixelFormat::Gray8, 32, 16, 32));
    REQUIRE(outcome(as_bgr) == "ok");
    CHECK(as_bgr->type() == CV_8UC3);

    cv::Mat gray16;
    gray.convertTo(gray16, CV_16U, 256.0);
    const auto decoded16 = decode_native(*frame_of(bytes_of(gray16), PixelFormat::Gray16, 32, 16, 64));
    REQUIRE(outcome(decoded16) == "ok");
    CHECK(decoded16->type() == CV_16UC1);
    const auto down = decode_gray8(*frame_of(bytes_of(gray16), PixelFormat::Gray16, 32, 16, 64));
    REQUIRE(outcome(down) == "ok");
    CHECK(cv::countNonZero(*down != gray) == 0);

    // YUYV with neutral chroma is a grey picture; the luma is on the limited (16..235) scale of BT.601, so the
    // decoded grey level is 1.164 * (Y - 16), clipped.
    cv::Mat yuyv(16, 32, CV_8UC2);
    cv::Mat expected(16, 32, CV_8UC1);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 32; ++x) {
            const std::uint8_t luma = gray.at<std::uint8_t>(y, x);
            yuyv.at<cv::Vec2b>(y, x) = cv::Vec2b(luma, 128);
            expected.at<std::uint8_t>(y, x) = cv::saturate_cast<std::uint8_t>(1.164 * (luma - 16));
        }
    }
    const auto decoded_yuyv = decode_bgr8(*frame_of(bytes_of(yuyv), PixelFormat::Yuyv, 32, 16, 64));
    REQUIRE(outcome(decoded_yuyv) == "ok");
    cv::Mat back;
    cv::cvtColor(*decoded_yuyv, back, cv::COLOR_BGR2GRAY);
    CHECK(cv::norm(back, expected, cv::NORM_INF) <= 3.0);

    const auto short_frame = frame_of(std::vector<std::byte>(10, std::byte{0}), PixelFormat::Bgr8, 32, 16, 96);
    CHECK(outcome(decode_native(*short_frame)) == "InvalidArgument");
}

TEST_CASE("statistics describe brightness, clipping, noise and sharpness", "[capture][statistics]")
{
    cv::Mat flat(64, 64, CV_8UC1, cv::Scalar(100));
    const FrameStatistics plain = compute_statistics(flat);
    CHECK_THAT(plain.mean, WithinAbs(100.0, 1e-9));
    CHECK(plain.median == 100.0);
    CHECK_THAT(plain.std_dev, WithinAbs(0.0, 1e-9));
    CHECK(plain.noise_sigma == 0.0);
    CHECK(plain.sharpness == 0.0);
    CHECK(plain.clipped_fraction == 0.0);
    CHECK(plain.dark_fraction == 0.0);
    CHECK(plain.histogram[100] == 64 * 64);
    CHECK_FALSE(plain.sun.found);
    CHECK(plain.percentile(0.99) == 100.0);

    cv::Mat noisy = flat.clone();
    cv::Mat noise(64, 64, CV_8SC1);
    cv::randn(noise, 0.0, 6.0);
    noisy += noise;
    const FrameStatistics with_noise = compute_statistics(noisy);
    CHECK_THAT(with_noise.noise_sigma, WithinRel(6.0, 0.35));
    CHECK(with_noise.sharpness > plain.sharpness);

    cv::Mat gradient(64, 256, CV_8UC1);
    for (int x = 0; x < 256; ++x) {
        gradient.col(x).setTo(x);
    }
    const FrameStatistics ramp = compute_statistics(gradient);
    CHECK_THAT(ramp.mean, WithinAbs(127.5, 1e-9));
    CHECK_THAT(ramp.clipped_fraction, WithinAbs(6.0 / 256.0, 1e-9));  // levels 250..255
    CHECK_THAT(ramp.dark_fraction, WithinAbs(6.0 / 256.0, 1e-9));     // levels 0..5
    CHECK(ramp.percentile(0.5) == 127.0);

    cv::Mat colour(8, 8, CV_8UC3, cv::Scalar(0, 0, 255));  // pure red: luma 76 by BT.601
    CHECK_THAT(compute_statistics(colour).mean, WithinAbs(76.0, 1.0));
}

TEST_CASE("the Sun is the largest round clipped blob", "[capture][statistics]")
{
    cv::Mat sky(200, 300, CV_8UC1, cv::Scalar(120));
    cv::circle(sky, cv::Point(210, 70), 15, cv::Scalar(255), cv::FILLED);
    cv::rectangle(sky, cv::Point(10, 150), cv::Point(60, 153), cv::Scalar(255), cv::FILLED);  // a thin clipped edge
    const FrameStatistics stats = compute_statistics(sky);
    REQUIRE(stats.sun.found);
    CHECK_THAT(stats.sun.x, WithinAbs(210.0, 1.0));
    CHECK_THAT(stats.sun.y, WithinAbs(70.0, 1.0));
    CHECK_THAT(stats.sun.radius_px, WithinAbs(15.0, 1.0));
    CHECK(stats.sun.fill > 0.7);
    CHECK(cv::countNonZero(saturation_map(sky)) == static_cast<int>(stats.sun.area_px) + 51 * 4);

    cv::Mat ragged(200, 300, CV_8UC1, cv::Scalar(120));
    cv::line(ragged, cv::Point(0, 0), cv::Point(299, 199), cv::Scalar(255), 2);  // clipped but not a disc
    CHECK_FALSE(compute_statistics(ragged).sun.found);
    cv::Mat tiny(200, 300, CV_8UC1, cv::Scalar(120));
    cv::circle(tiny, cv::Point(50, 50), 1, cv::Scalar(255), cv::FILLED);  // too small to be the Sun
    CHECK_FALSE(compute_statistics(tiny).sun.found);
}
