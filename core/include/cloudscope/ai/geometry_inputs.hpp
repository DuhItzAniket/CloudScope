// The model's geometry inputs (P067, FR-AI-04): the contract's ray map and Sun metadata from the camera's
// intrinsic calibration, its orientation and the Sun's position, computed as STRATIA's reference
// implementation does (stratia/geometry/raymap.py; shared vectors in tests/data/stratia/).
//
// Ray map [4, H/p, W/p]: at the centre of every patch of the resized model input, channels 0-2 are the unit
// viewing direction in the Sun-aligned frame (z up, x toward the Sun's azimuth, y = z x x) and channel 3 is 1
// where the direction is known: the patch centre maps inside the source image, at least half of its footprint
// is inside the camera mask (when one is given), and the direction is above the minimum elevation. Everything
// else, and the whole tensor when the calibration, the pose or the Sun is unknown, is 0; the model treats zeros
// as "unknown" (it was trained with geometry dropout), so a missing input degrades, never breaks, a result.

#pragma once

#include <cloudscope/ai/inference_service.hpp>
#include <cloudscope/calibration/intrinsics.hpp>
#include <cloudscope/capture/solar.hpp>
#include <cloudscope/geometry/rotation.hpp>

#include <opencv2/core.hpp>

#include <optional>

namespace cloudscope::ai {

struct RayMapSpec {
    int height = kDefaultInputSize;  // the model input (from the card)
    int width = kDefaultInputSize;
    int patch = kPatchSize;
    double min_elevation_deg = 0.0;  // directions below it are invalid (near-horizon obstruction)
    int mask_subgrid = 4;            // samples per patch side when a camera mask is given
};

// The full-frame resize of the contract: output pixel (u, v) -> source pixel, pixel-centre convention.
struct FullFrameAffine {
    double sx = 1.0;
    double sy = 1.0;
    double tx = 0.0;
    double ty = 0.0;
    [[nodiscard]] cv::Point2d operator()(cv::Point2d out) const { return {sx * out.x + tx, sy * out.y + ty}; }
};
[[nodiscard]] FullFrameAffine full_frame_affine(int source_width, int source_height, int out_width, int out_height);

// Rotation from East-North-Up to the Sun-aligned frame (rows: x toward the Sun's azimuth, y, z up).
[[nodiscard]] Vector3 sun_aligned(const Vector3& enu, double sun_azimuth_deg);

// `[cos(Sun zenith), sin(Sun zenith), 1]`, or zeros when the Sun is unknown. The apparent (refracted) elevation
// is used, as STRATIA's generator does.
[[nodiscard]] std::array<float, 3> meta_vector(const SunPosition* sun);

// The geometry inputs of one frame. Any null argument gives the all-zero, invalid tensor. `camera_to_enu` is the
// camera orientation of the overlays (the roll applied first, then the pointing: SkyProjection in the desktop
// application, camera_orientation() in core). `mask` (8-bit, non-zero = sky) is in source-image pixels.
[[nodiscard]] GeometryInputs geometry_inputs(const CameraModel* model, const Quaternion* camera_to_enu,
                                             const SunPosition* sun, const RayMapSpec& spec = {},
                                             const cv::Mat* mask = nullptr);

// The ray map alone (4 x g_h x g_w floats), for the tests and the benchmark.
[[nodiscard]] std::vector<float> ray_map(const CameraModel& model, const Quaternion& camera_to_enu,
                                         const SunPosition& sun, const RayMapSpec& spec = {},
                                         const cv::Mat* mask = nullptr);

}  // namespace cloudscope::ai
