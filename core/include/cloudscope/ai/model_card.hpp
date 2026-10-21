// STRATIA model cards and model packages (P065, FR-AI-01).
//
// A model package is a folder with `model_card.json` and one ONNX file. The card is validated against the
// bundled schema (core/resources/model_card.schema.json, a copy of STRATIA's), then against the contract
// constants of contract.hpp: a wrong major version, a different class order or an input that is not a multiple
// of the patch size make the model unusable and are reported as Unsupported, never worked around.

#pragma once

#include <cloudscope/ai/contract.hpp>
#include <cloudscope/common/error.hpp>
#include <cloudscope/common/json_schema.hpp>
#include <cloudscope/hal/inference.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace cloudscope::ai {

struct ContractVersion {
    int major = 0;
    int minor = 0;
    friend bool operator==(const ContractVersion&, const ContractVersion&) = default;
};

struct ModelInput {
    int height = 0;
    int width = 0;
    int patch_size = 0;
    std::array<double, 3> mean{};  // of the [0, 1] RGB image, applied after the resize
    std::array<double, 3> std{};
    std::string resize;  // the host's resize rule, in words (the card is the authority)
};

struct ModelCalibration {
    std::map<std::string, double> temperatures;              // per head; informative when folded into the graph
    bool folded_into_graph = false;                          // true: the graph outputs calibrated values, apply nothing
    std::map<std::string, double> cbh_conformal_radius_cdf;  // "80", "95" -> half-width in CDF units
};

struct TrainingDataset {
    std::string dataset;
    std::string manifest_sha256;
};

struct ModelCard {
    ContractVersion contract;
    std::string model_name;
    std::string model_version;
    std::string created_utc;
    std::string git_sha;
    ModelInput input;
    std::vector<std::string> genus_classes;
    std::vector<std::string> etage_classes;
    std::vector<std::string> sky_parse_classes;
    std::vector<std::string> layer_classes;
    std::vector<double> cbh_bin_edges_m;  // k + 1 ascending edges; the last is the top of the highest bin
    ModelCalibration calibration;
    double ood_threshold = 0.0;  // above it, results are shown as uncertain (FR-AI-06)
    std::vector<TrainingDataset> training_data;
    std::vector<std::string> licences;
    std::string evaluation_report;
    std::string backbone;     // informative
    nlohmann::json document;  // the whole card as read

    [[nodiscard]] int cbh_bins() const { return static_cast<int>(cbh_bin_edges_m.size()) - 1; }
    [[nodiscard]] std::vector<hal::TensorInfo> expected_inputs() const;
    [[nodiscard]] std::vector<hal::TensorInfo> expected_outputs() const;
};

[[nodiscard]] const JsonSchema& model_card_schema();

// Reads a card: schema problems are Validation, contract disagreements Unsupported.
[[nodiscard]] Expected<ModelCard> model_card_from_json(const nlohmann::json& document);
[[nodiscard]] Expected<ModelCard> load_model_card(const std::filesystem::path& file);

// The contract checks alone (class orders, version, sizes, bin edges); [] from model_card_from_json already.
[[nodiscard]] Expected<void> check_contract(const ModelCard& card);

// The loaded model declares the tensors the card promises (names, order-free; shapes with -1 for the batch).
[[nodiscard]] Expected<void> check_model_io(const ModelCard& card, const hal::InferenceCapabilities& capabilities);

struct ModelPackage {
    std::filesystem::path folder;
    std::filesystem::path card_file;
    std::filesystem::path model_file;  // the one *.onnx next to the card
    ModelCard card;
};

// Opens a package from its folder or from the card's path. NotFound without a card or an ONNX file, Validation
// with more than one ONNX file, and the card's own errors.
[[nodiscard]] Expected<ModelPackage> open_model_package(const std::filesystem::path& folder_or_card);

}  // namespace cloudscope::ai
