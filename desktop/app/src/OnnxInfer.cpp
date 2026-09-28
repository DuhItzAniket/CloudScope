#include "OnnxInfer.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <opencv2/imgproc.hpp>
#include <windows.h>

namespace {

const float kMean[3] = {0.485f, 0.456f, 0.406f};
const float kStd[3] = {0.229f, 0.224f, 0.225f};

// BGR uchar -> resized RGB float NCHW==1 with ImageNet norm (== Python).
void preprocessNchw(const cv::Mat& bgr, int size, std::vector<float>& out)
{
    cv::Mat resized, rgb, f;
    cv::resize(bgr, resized, cv::Size(size, size), 0, 0, cv::INTER_LINEAR);
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(f, CV_32FC3, 1.0 / 255.0);
    out.resize(3 * size * size);
    const int HW = size * size;
    for (int y = 0; y < size; ++y) {
        const cv::Vec3f* row = f.ptr<cv::Vec3f>(y);
        for (int x = 0; x < size; ++x) {
            for (int c = 0; c < 3; ++c)
                out[c * HW + y * size + x] = (row[x][c] - kMean[c]) / kStd[c];
        }
    }
}

std::wstring toWide(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

Ort::Session makeSession(Ort::Env& env, const std::string& path, bool& usedCuda)
{
    const std::wstring wpath = toWide(path);
    Ort::SessionOptions opt;
    opt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    usedCuda = false;
    try {
        OrtCUDAProviderOptions co;
        opt.AppendExecutionProvider_CUDA(co);
        Ort::Session s(env, wpath.c_str(), opt);
        usedCuda = true;
        return s;
    } catch (const Ort::Exception&) {
        Ort::SessionOptions cpuOpt;
        cpuOpt.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        return Ort::Session(env, wpath.c_str(), cpuOpt);
    }
}

std::vector<int64_t> shapeOf(const Ort::Session& s, bool input, size_t idx = 0)
{
    Ort::AllocatorWithDefaultOptions alloc;
    if (input)
        return s.GetInputTypeInfo(idx).GetTensorTypeAndShapeInfo().GetShape();
    return s.GetOutputTypeInfo(idx).GetTensorTypeAndShapeInfo().GetShape();
}

}  // namespace

OnnxInfer::OnnxInfer() : env_(ORT_LOGGING_LEVEL_WARNING, "cloudscope") {}

const std::vector<std::string>& OnnxInfer::classNames()
{
    static const std::vector<std::string> k = {"Ac", "As", "Cb", "Cc", "Ci",
                                               "Cs", "Ct", "Cu", "Ns", "Sc", "St"};
    return k;
}

bool OnnxInfer::load(const std::string& classifierPath, const std::string& segPath)
{
    bool cudaUsed = false;
    backend_ = "unloaded";
    if (!classifierPath.empty()) {
        try {
            bool cu = false;
            clsSession_ = std::make_unique<Ort::Session>(
                makeSession(env_, classifierPath, cu));
            cudaUsed = cudaUsed || cu;
        } catch (const Ort::Exception&) {
            clsSession_.reset();
        }
    }
    if (!segPath.empty()) {
        try {
            bool cu = false;
            segSession_ = std::make_unique<Ort::Session>(
                makeSession(env_, segPath, cu));
            cudaUsed = cudaUsed || cu;
        } catch (const Ort::Exception&) {
            segSession_.reset();
        }
    }
    if (clsSession_ || segSession_)
        backend_ = cudaUsed ? "cuda" : "cpu";
    return clsSession_ != nullptr || segSession_ != nullptr;
}

void OnnxInfer::preprocess(const cv::Mat& bgr, int size, std::vector<float>& out,
                           int& w, int& h)
{
    w = bgr.cols;
    h = bgr.rows;
    preprocessNchw(bgr, size, out);
}

OnnxInfer::ClassResult OnnxInfer::classify(const cv::Mat& bgr)
{
    ClassResult r;
    if (!clsSession_ || bgr.empty())
        return r;
    std::vector<float> input;
    int w = 0, h = 0;
    preprocess(bgr, 224, input, w, h);
    Ort::AllocatorWithDefaultOptions alloc;
    // AllocatedStringPtr keeps the name heap strings alive for Run().
    Ort::AllocatedStringPtr inNameHeld =
        clsSession_->GetInputNameAllocated(0, alloc);
    Ort::AllocatedStringPtr outNameHeld =
        clsSession_->GetOutputNameAllocated(0, alloc);
    std::array<int64_t, 4> dims{1, 3, 224, 224};
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
        OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
    Ort::Value inT = Ort::Value::CreateTensor<float>(
        mem, input.data(), input.size(), dims.data(), dims.size());
    const char* inNames[1] = {inNameHeld.get()};
    const char* outNames[1] = {outNameHeld.get()};
    auto outs = clsSession_->Run(Ort::RunOptions{}, inNames, &inT, 1, outNames, 1);
    const float* logits = outs[0].GetTensorData<float>();
    std::vector<float> prob(11);
    float mx = *std::max_element(logits, logits + 11);
    float sum = 0.0f;
    for (int i = 0; i < 11; ++i) {
        prob[i] = std::exp(logits[i] - mx);
        sum += prob[i];
    }
    std::vector<int> idx(11);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(),
              [&](int a, int b) { return prob[a] > prob[b]; });
    const auto& names = classNames();
    r.id = idx[0];
    r.label = names[idx[0]];
    r.confidence = prob[idx[0]] / sum;
    for (int k = 0; k < 3; ++k)
        r.top3.emplace_back(names[idx[k]], prob[idx[k]] / sum);
    return r;
}

cv::Mat OnnxInfer::segment(const cv::Mat& bgr)
{
    if (!segSession_ || bgr.empty())
        return cv::Mat();
    std::vector<float> input;
    int w = 0, h = 0;
    preprocess(bgr, 512, input, w, h);
    Ort::AllocatorWithDefaultOptions alloc;
    Ort::AllocatedStringPtr inNameHeld =
        segSession_->GetInputNameAllocated(0, alloc);
    Ort::AllocatedStringPtr outNameHeld =
        segSession_->GetOutputNameAllocated(0, alloc);
    // Model was exported with dynamic HW; feed 512x512, map back to frame.
    std::array<int64_t, 4> dims{1, 3, 512, 512};
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(
        OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
    Ort::Value inT = Ort::Value::CreateTensor<float>(
        mem, input.data(), input.size(), dims.data(), dims.size());
    const char* inNames[1] = {inNameHeld.get()};
    const char* outNames[1] = {outNameHeld.get()};
    auto outs = segSession_->Run(Ort::RunOptions{}, inNames, &inT, 1, outNames, 1);
    auto info = outs[0].GetTensorTypeAndShapeInfo();
    std::vector<int64_t> sh = info.GetShape();  // [1,3,H,W]
    const int oh = static_cast<int>(sh[2]);
    const int ow = static_cast<int>(sh[3]);
    const float* lg = outs[0].GetTensorData<float>();
    cv::Mat mask(oh, ow, CV_8UC1);
    for (int y = 0; y < oh; ++y) {
        uint8_t* row = mask.ptr<uint8_t>(y);
        for (int x = 0; x < ow; ++x) {
            int best = 0;
            float bv = lg[x + y * ow];
            for (int c = 1; c < 3; ++c) {
                float v = lg[c * oh * ow + x + y * ow];
                if (v > bv) {
                    bv = v;
                    best = c;
                }
            }
            row[x] = static_cast<uint8_t>(best);
        }
    }
    cv::Mat full;
    cv::resize(mask, full, cv::Size(w, h), 0, 0, cv::INTER_NEAREST);
    return full;
}
