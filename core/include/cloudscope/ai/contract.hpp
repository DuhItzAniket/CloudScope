// The stratia-contract v1 constants (STRATIA docs/contract.md; mirrored by STRATIA's stratia/contract.py).
//
// The names, shapes and class orders here are fixed by the contract's major version: a model card that
// disagrees with them is refused (FR-AI-01). Anything a model may vary (input size, bin edges, temperatures,
// threshold) comes from its card, never from here.

#pragma once

#include <cloudscope/hal/inference.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cloudscope::ai {

inline constexpr int kContractMajor = 1;
inline constexpr int kContractMinor = 0;
inline constexpr int kPatchSize = 16;          // the ray map has one cell per patch of the input
inline constexpr int kDefaultInputSize = 512;  // the v1 default; the card says what the model takes
inline constexpr int kRayMapChannels = 4;      // unit direction in the Sun-aligned frame + valid flag
inline constexpr int kMetaSize = 3;            // cos(Sun zenith), sin(Sun zenith), valid
inline constexpr int kOktasClasses = 9;        // 0..8 oktas
inline constexpr int kDenseDivisor = 4;        // the dense outputs are at H/4 x W/4

inline constexpr std::array<std::string_view, 12> kGenusClasses{"Ci", "Cc", "Cs", "Ac", "As",    "Ns",
                                                                "Sc", "St", "Cu", "Cb", "clear", "contrail"};
inline constexpr std::array<std::string_view, 3> kEtageClasses{"low", "mid", "high"};
inline constexpr std::array<std::string_view, 4> kSkyParseClasses{"invalid", "sky", "cloud", "sun_glare"};
inline constexpr std::array<std::string_view, 3> kLayerClasses{"low", "mid", "high"};

inline constexpr std::array<std::string_view, 3> kInputNames{"image", "ray_map", "meta"};
inline constexpr std::array<std::string_view, 7> kOutputNames{
    "genus_logits", "etage_logits", "oktas_probs", "sky_parse_logits", "layer_logits", "cbh_probs", "ood_score"};

// Index of the "no cloud overhead" class in cbh_probs for a model with `k_cbh` height bins.
[[nodiscard]] constexpr int no_cloud_index(int k_cbh)
{
    return k_cbh;
}

// The tensors a conforming model declares, with -1 for the batch axis. `height` and `width` are the model's input
// size (a multiple of the patch size); `k_cbh` is the number of height bins (bin edges - 1).
[[nodiscard]] std::vector<hal::TensorInfo> expected_inputs(int height, int width, int patch = kPatchSize);
[[nodiscard]] std::vector<hal::TensorInfo> expected_outputs(int height, int width, int k_cbh);

}  // namespace cloudscope::ai
