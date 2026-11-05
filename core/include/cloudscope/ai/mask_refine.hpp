// Image-guided refinement of the dense masks and a geometric Sun-glare rule (follow-up to P068).
//
// The model's sky parse and layer maps come at H/4 x W/4 (128 x 128 for a 512 px input) and look blocky when
// stretched over a 16 MP frame. `refine_classes` upsamples each class probability to the model's input size and
// runs a guided filter (He, Sun and Tang, 2010) with the grey picture as the guide, so that the mask edges follow
// the picture's edges inside the filter window; the argmax of the refined probabilities is the class map.
//
// The learned glare class does not fire on every camera (STRATIA P093: never on the B0268), and the saturated
// sky around the Sun is then parsed as cloud. `find_sun` locates the Sun in the picture as the brightest blurred
// plateau on the ring of the Sun's zenith angle (the ray map gives each pixel's zenith angle without the camera's
// rotation about the zenith, which a declared pointing does not know), and `apply_glare_rule` turns the cloud
// pixels within `glare_radius_deg` of it that are saturated into `sun_glare`. Both steps are presentation-side
// post-processing in the application: the model's outputs and STRATIA's reported numbers are unchanged.

#pragma once

#include <cloudscope/ai/inference_service.hpp>

#include <opencv2/core.hpp>

#include <optional>
#include <vector>

namespace cloudscope::ai {

struct MaskRefineSettings {
    int guide_radius_px = 16;                // guided-filter window radius at the model's input size
    int guide_passes = 2;                    // passes of the filter: two pull an edge up to ~10 px onto the picture's
    float guide_eps = 1.0e-3F;               // regularisation, on [0, 1] intensities
    int sun_blur_px = 31;                    // box blur that finds the Sun's plateau
    float glare_luminance = 0.90F;           // of full scale: "saturated"
    float glare_radius_deg = 25.0F;          // circumsolar radius of the rule
    float sun_zenith_tolerance_deg = 20.0F;  // how far from the Sun's zenith angle the plateau may sit
};

// He et al.'s guided filter: `guide` and `src` are CV_32FC1 of the same size.
[[nodiscard]] cv::Mat guided_filter(const cv::Mat& guide, const cv::Mat& src, int radius, float eps);

// Per-class probability maps (CV_32FC1, one common size) -> CV_8UC1 class map at the guide's size, after bilinear
// upsampling and guided filtering of every class with the guide (the grey picture, CV_32FC1 in [0, 1]).
[[nodiscard]] cv::Mat refine_classes(const std::vector<cv::Mat>& probabilities, const cv::Mat& guide,
                                     const MaskRefineSettings& settings);

// The unit ray direction per pixel (CV_32FC3) and its validity (CV_8UC1) at `size`, interpolated from the
// geometry's ray map of `grid_h` x `grid_w` patches; `ok` is false when the geometry is unknown.
struct RayField {
    cv::Mat directions;
    cv::Mat valid;
    bool ok = false;
};
[[nodiscard]] RayField ray_field(const GeometryInputs& geometry, int grid_h, int grid_w, cv::Size size);

// Luminance in [0, 1] (CV_32FC1): the brightest channel of an 8-bit three-channel picture.
[[nodiscard]] cv::Mat luminance_of(const cv::Mat& rgb);

// The Sun's pixel: the brightest `sun_blur_px` plateau among the valid pixels whose zenith angle is within the
// tolerance of the Sun's (from the geometry's meta), when that plateau is saturated.
[[nodiscard]] std::optional<cv::Point> find_sun(const cv::Mat& luminance, const RayField& rays,
                                                const GeometryInputs& geometry, const MaskRefineSettings& settings);

// Cloud pixels within `glare_radius_deg` of `sun` that are saturated become `sun_glare`; returns how many.
int apply_glare_rule(cv::Mat& sky_parse, const cv::Mat& luminance, const RayField& rays, cv::Point sun,
                     const MaskRefineSettings& settings);

}  // namespace cloudscope::ai
