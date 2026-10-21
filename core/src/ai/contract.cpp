#include "cloudscope/ai/contract.hpp"

#include <string>

namespace cloudscope::ai {

std::vector<hal::TensorInfo> expected_inputs(int height, int width, int patch)
{
    return {
        {.name = std::string(kInputNames[0]), .shape = {-1, 3, height, width}},
        {.name = std::string(kInputNames[1]), .shape = {-1, kRayMapChannels, height / patch, width / patch}},
        {.name = std::string(kInputNames[2]), .shape = {-1, kMetaSize}},
    };
}

std::vector<hal::TensorInfo> expected_outputs(int height, int width, int k_cbh)
{
    const std::int64_t dense_h = height / kDenseDivisor;
    const std::int64_t dense_w = width / kDenseDivisor;
    return {
        {.name = std::string(kOutputNames[0]), .shape = {-1, static_cast<std::int64_t>(kGenusClasses.size())}},
        {.name = std::string(kOutputNames[1]), .shape = {-1, static_cast<std::int64_t>(kEtageClasses.size())}},
        {.name = std::string(kOutputNames[2]), .shape = {-1, kOktasClasses}},
        {.name = std::string(kOutputNames[3]),
         .shape = {-1, static_cast<std::int64_t>(kSkyParseClasses.size()), dense_h, dense_w}},
        {.name = std::string(kOutputNames[4]),
         .shape = {-1, static_cast<std::int64_t>(kLayerClasses.size()), dense_h, dense_w}},
        {.name = std::string(kOutputNames[5]), .shape = {-1, k_cbh + 1}},
        {.name = std::string(kOutputNames[6]), .shape = {-1, 1}},
    };
}

}  // namespace cloudscope::ai
