#include "cloudscope/capture/calibration_frames.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

namespace cloudscope {

namespace {

double depth_scale(int depth)
{
    return depth == CV_16U ? 65535.0 : 255.0;
}

cv::Mat luma32(const cv::Mat& picture)
{
    cv::Mat single;
    if (picture.channels() == 3) {
        cv::Mat as32;
        picture.convertTo(as32, CV_32FC3);
        cv::cvtColor(as32, single, cv::COLOR_BGR2GRAY);
    } else {
        picture.convertTo(single, CV_32F);
    }
    return single;
}

}  // namespace

Expected<MasterFrame> average_frames(const std::vector<cv::Mat>& frames)
{
    if (frames.empty()) {
        return fail(ErrorCode::InvalidArgument, "no frames to average");
    }
    const cv::Mat& first = frames.front();
    if (first.depth() != CV_8U && first.depth() != CV_16U) {
        return fail(ErrorCode::InvalidArgument, "frames must be 8- or 16-bit");
    }
    const int channels = first.channels();
    const int type32 = CV_MAKETYPE(CV_32F, channels);
    cv::Mat sum = cv::Mat::zeros(first.size(), type32);
    cv::Mat sum_squares = cv::Mat::zeros(first.size(), type32);
    for (const cv::Mat& frame : frames) {
        if (frame.size() != first.size() || frame.type() != first.type()) {
            return fail(ErrorCode::InvalidArgument, "frames differ in size or type");
        }
        cv::Mat as32;
        frame.convertTo(as32, type32);
        sum += as32;
        cv::Mat squared;
        cv::multiply(as32, as32, squared);
        sum_squares += squared;
    }
    MasterFrame master;
    master.frames = static_cast<int>(frames.size());
    master.depth = first.depth();
    master.mean = sum / static_cast<double>(frames.size());
    cv::Mat mean_squared;
    cv::multiply(master.mean, master.mean, mean_squared);
    cv::Mat variance = sum_squares / static_cast<double>(frames.size()) - mean_squared;
    cv::max(variance, 0.0, variance);
    cv::sqrt(variance, master.stddev);
    return master;
}

Expected<cv::Mat> gain_map(const MasterFrame& flat, const MasterFrame* dark, double floor, double ceiling)
{
    if (flat.mean.empty()) {
        return fail(ErrorCode::InvalidArgument, "the flat master is empty");
    }
    cv::Mat signal = flat.mean.clone();
    if (dark != nullptr && !dark->mean.empty()) {
        if (dark->mean.size() != flat.mean.size() || dark->mean.type() != flat.mean.type()) {
            return fail(ErrorCode::InvalidArgument, "the dark master does not match the flat master");
        }
        signal -= dark->mean;
    }
    cv::max(signal, 1e-3, signal);
    const cv::Scalar level = cv::mean(signal);
    cv::Mat gain = cv::Mat(signal.size(), signal.type());
    if (signal.channels() == 1) {
        gain = level[0] / signal;
    } else {
        std::vector<cv::Mat> channels;
        cv::split(signal, channels);
        for (std::size_t c = 0; c < channels.size(); ++c) {
            channels[c] = level[static_cast<int>(c)] / channels[c];
        }
        cv::merge(channels, gain);
    }
    cv::min(gain, ceiling, gain);
    cv::max(gain, floor, gain);
    return gain;
}

Expected<cv::Mat> apply_calibration(const cv::Mat& picture, const cv::Mat& dark_mean, const cv::Mat& gain)
{
    if (picture.empty() || (picture.depth() != CV_8U && picture.depth() != CV_16U)) {
        return fail(ErrorCode::InvalidArgument, "the picture must be 8- or 16-bit");
    }
    const int type32 = CV_MAKETYPE(CV_32F, picture.channels());
    cv::Mat work;
    picture.convertTo(work, type32);
    if (!dark_mean.empty()) {
        if (dark_mean.size() != picture.size() || dark_mean.channels() != picture.channels()) {
            return fail(ErrorCode::InvalidArgument, "the dark frame does not match the picture");
        }
        work -= dark_mean;
    }
    if (!gain.empty()) {
        if (gain.size() != picture.size() || gain.channels() != picture.channels()) {
            return fail(ErrorCode::InvalidArgument, "the gain map does not match the picture");
        }
        cv::multiply(work, gain, work);
    }
    cv::Mat out;
    work.convertTo(out, picture.type());  // saturates
    return out;
}

Expected<VignettingModel> fit_vignetting(const cv::Mat& flat)
{
    if (flat.empty() || flat.rows < 8 || flat.cols < 8) {
        return fail(ErrorCode::InvalidArgument, "the flat is too small to fit");
    }
    const cv::Mat luma = luma32(flat);
    VignettingModel model;
    model.centre_x = (flat.cols - 1) / 2.0;
    model.centre_y = (flat.rows - 1) / 2.0;
    const double half_diagonal = 0.5 * std::hypot(flat.cols - 1.0, flat.rows - 1.0);
    // Radial bins of mean brightness; the innermost bin is the reference (g = 1 at the centre).
    constexpr int kBins = 40;
    std::vector<double> sums(kBins, 0.0);
    std::vector<double> counts(kBins, 0.0);
    for (int y = 0; y < luma.rows; ++y) {
        const float* row = luma.ptr<float>(y);
        for (int x = 0; x < luma.cols; ++x) {
            const double r = std::hypot(x - model.centre_x, y - model.centre_y) / half_diagonal;
            const int bin = std::min(static_cast<int>(r * kBins), kBins - 1);
            sums[static_cast<std::size_t>(bin)] += static_cast<double>(row[x]);
            counts[static_cast<std::size_t>(bin)] += 1.0;
        }
    }
    double centre = 0.0;
    double centre_count = 0.0;
    for (int bin = 0; bin < 3; ++bin) {
        centre += sums[static_cast<std::size_t>(bin)];
        centre_count += counts[static_cast<std::size_t>(bin)];
    }
    if (centre_count <= 0.0 || centre <= 0.0) {
        return fail(ErrorCode::InvalidArgument, "the flat has no light at the centre");
    }
    centre /= centre_count;
    // Least squares for (a, b): g - 1 = a r^2 + b r^4 over bins with pixels.
    double s22 = 0.0;
    double s24 = 0.0;
    double s44 = 0.0;
    double t2 = 0.0;
    double t4 = 0.0;
    for (int bin = 0; bin < kBins; ++bin) {
        const std::size_t i = static_cast<std::size_t>(bin);
        if (counts[i] <= 0.0) {
            continue;
        }
        const double r = (bin + 0.5) / kBins;
        const double g = sums[i] / counts[i] / centre - 1.0;
        const double r2 = r * r;
        const double r4 = r2 * r2;
        const double w = counts[i];
        s22 += w * r2 * r2;
        s24 += w * r2 * r4;
        s44 += w * r4 * r4;
        t2 += w * r2 * g;
        t4 += w * r4 * g;
    }
    const double determinant = s22 * s44 - s24 * s24;
    if (std::abs(determinant) < 1e-12) {
        return fail(ErrorCode::InvalidArgument, "the radial profile is degenerate");
    }
    model.a = (t2 * s44 - t4 * s24) / determinant;
    model.b = (s22 * t4 - s24 * t2) / determinant;
    double residual = 0.0;
    double total = 0.0;
    for (int bin = 0; bin < kBins; ++bin) {
        const std::size_t i = static_cast<std::size_t>(bin);
        if (counts[i] <= 0.0) {
            continue;
        }
        const double r = (bin + 0.5) / kBins;
        const double predicted = 1.0 + model.a * r * r + model.b * r * r * r * r;
        const double measured = sums[i] / counts[i] / centre;
        residual += counts[i] * (predicted - measured) * (predicted - measured);
        total += counts[i];
    }
    model.rms_residual = std::sqrt(residual / std::max(total, 1.0));
    return model;
}

cv::Mat vignetting_gain(const VignettingModel& model, const cv::Size& size)
{
    cv::Mat gain(size, CV_32FC1);
    const double half_diagonal = 0.5 * std::hypot(size.width - 1.0, size.height - 1.0);
    for (int y = 0; y < size.height; ++y) {
        float* row = gain.ptr<float>(y);
        for (int x = 0; x < size.width; ++x) {
            const double r = std::hypot(x - model.centre_x, y - model.centre_y) / half_diagonal;
            const double g = 1.0 + model.a * r * r + model.b * r * r * r * r;
            row[x] = static_cast<float>(1.0 / std::clamp(g, 0.05, 20.0));
        }
    }
    const cv::Scalar level = cv::mean(gain);
    return gain / level[0];
}

double uniformity_spread(const cv::Mat& picture)
{
    cv::Mat luma = luma32(picture);
    cv::blur(luma, luma, cv::Size(15, 15));
    std::vector<float> values(luma.begin<float>(), luma.end<float>());
    if (values.empty()) {
        return 0.0;
    }
    const auto percentile = [&](double fraction) {
        const std::size_t index =
            std::min(values.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(values.size())));
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
        return static_cast<double>(values[index]);
    };
    const double median = percentile(0.5);
    const double low = percentile(0.05);
    const double high = percentile(0.95);
    return median > 0.0 ? (high - low) / median : 0.0;
}

Expected<void> write_image(const std::filesystem::path& file, const cv::Mat& image)
{
    std::vector<std::uint8_t> encoded;
    const std::string extension = file.extension().string().empty() ? ".tiff" : file.extension().string();
    try {
        if (!cv::imencode(extension, image, encoded)) {
            return fail(ErrorCode::Unsupported, fmt::format("cannot encode an image as {}", extension));
        }
    } catch (const cv::Exception& e) {
        return fail(ErrorCode::Unsupported, fmt::format("cannot encode an image as {}: {}", extension, e.what()));
    }
    std::ofstream out(file, std::ios::binary);
    if (!out.write(
            reinterpret_cast<const char*>(encoded.data()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            static_cast<std::streamsize>(encoded.size()))) {
        return fail(ErrorCode::Io, fmt::format("could not write {}", file.string()));
    }
    return {};
}

Expected<cv::Mat> read_image(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::NotFound, fmt::format("could not read {}", file.string()));
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const cv::Mat image = cv::imdecode(bytes, cv::IMREAD_UNCHANGED);
    if (image.empty()) {
        return fail(ErrorCode::Parse, fmt::format("{} is not an image this build can decode", file.string()));
    }
    return image;
}

Expected<void> save_master(const MasterFrame& master, const std::filesystem::path& file)
{
    if (master.mean.empty()) {
        return fail(ErrorCode::InvalidArgument, "the master is empty");
    }
    cv::Mat scaled;
    master.mean.convertTo(scaled, CV_MAKETYPE(CV_16U, master.mean.channels()), 65535.0 / depth_scale(master.depth));
    const std::string path = file.string();
    if (auto written = write_image(file, scaled); !written) {
        return written;
    }
    const nlohmann::json sidecar{{"schema", "cloudscope.master_frame/1"},
                                 {"frames", master.frames},
                                 {"depth_bits", master.depth == CV_16U ? 16 : 8},
                                 {"channels", master.mean.channels()},
                                 {"noise_mean", cv::mean(master.stddev)[0]},
                                 {"scale", "stored as 16-bit; value = stored * depth_max / 65535"}};
    std::ofstream out(std::filesystem::path(file.native() + std::filesystem::path(".json").native()), std::ios::binary);
    if (!out) {
        return fail(ErrorCode::Io, fmt::format("could not write {}.json", path));
    }
    out << sidecar.dump(2) << "\n";
    return {};
}

Expected<MasterFrame> load_master(const std::filesystem::path& file)
{
    const auto read = read_image(file);
    if (!read) {
        return fail(read.error());
    }
    const cv::Mat stored = *read;
    std::ifstream in(std::filesystem::path(file.native() + std::filesystem::path(".json").native()), std::ios::binary);
    if (!in) {
        return fail(ErrorCode::NotFound, fmt::format("could not read {}.json", file.string()));
    }
    nlohmann::json sidecar;
    try {
        in >> sidecar;
    } catch (const nlohmann::json::exception& e) {
        return fail(ErrorCode::Parse, fmt::format("{}.json: {}", file.string(), e.what()));
    }
    MasterFrame master;
    master.frames = sidecar.value("frames", 0);
    master.depth = sidecar.value("depth_bits", 8) == 16 ? CV_16U : CV_8U;
    stored.convertTo(master.mean, CV_MAKETYPE(CV_32F, stored.channels()), depth_scale(master.depth) / 65535.0);
    master.stddev = cv::Mat::zeros(master.mean.size(), master.mean.type());
    return master;
}

}  // namespace cloudscope
