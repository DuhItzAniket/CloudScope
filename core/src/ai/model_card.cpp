#include "cloudscope/ai/model_card.hpp"

#include <QtCore/QFile>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace cloudscope::ai {

namespace {

template <std::size_t N>
std::string joined(const std::array<std::string_view, N>& items)
{
    std::string out;
    for (std::string_view item : items) {
        out += out.empty() ? "" : ", ";
        out += item;
    }
    return out;
}

template <std::size_t N>
bool same_order(const std::vector<std::string>& got, const std::array<std::string_view, N>& want)
{
    return got.size() == N && std::equal(got.begin(), got.end(), want.begin());
}

std::vector<std::string> strings(const nlohmann::json& array)
{
    std::vector<std::string> out;
    for (const nlohmann::json& item : array) {
        out.push_back(item.get<std::string>());
    }
    return out;
}

std::string shape_text(const std::vector<std::int64_t>& shape)
{
    std::string out = "[";
    for (std::size_t i = 0; i < shape.size(); ++i) {
        out += (i != 0U ? ", " : "") + std::to_string(shape[i]);
    }
    return out + "]";
}

Expected<void> check_tensor(const hal::TensorInfo& declared, const hal::TensorInfo& wanted, std::string_view what)
{
    if (declared.shape.size() != wanted.shape.size()) {
        return fail(ErrorCode::Unsupported,
                    fmt::format("{} '{}' has shape {}, the contract wants {}", what, wanted.name,
                                shape_text(declared.shape), shape_text(wanted.shape)));
    }
    for (std::size_t d = 0; d < wanted.shape.size(); ++d) {
        const std::int64_t have = declared.shape[d];
        const std::int64_t want = wanted.shape[d];
        if (want == -1) {
            continue;  // the batch axis: open or fixed, both run one frame at a time
        }
        if (have != want && have != -1) {
            return fail(ErrorCode::Unsupported,
                        fmt::format("{} '{}' has shape {}, the contract wants {}", what, wanted.name,
                                    shape_text(declared.shape), shape_text(wanted.shape)));
        }
    }
    return {};
}

}  // namespace

std::vector<hal::TensorInfo> ModelCard::expected_inputs() const
{
    return ai::expected_inputs(input.height, input.width, input.patch_size);
}

std::vector<hal::TensorInfo> ModelCard::expected_outputs() const
{
    return ai::expected_outputs(input.height, input.width, cbh_bins());
}

const JsonSchema& model_card_schema()
{
    static const JsonSchema schema = [] {
        QFile file(QStringLiteral(":/cloudscope/model_card.schema.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("the model card schema is missing from the resources");
        }
        const QByteArray bytes = file.readAll();
        auto compiled = JsonSchema::compile(nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size()));
        if (!compiled) {
            throw std::runtime_error("the model card schema does not compile: " + compiled.error().to_string());
        }
        return *compiled;
    }();
    return schema;
}

Expected<ModelCard> model_card_from_json(const nlohmann::json& document)
{
    const std::vector<SchemaIssue> issues = model_card_schema().validate(document);
    if (!issues.empty()) {
        std::string text;
        for (const SchemaIssue& issue : issues) {
            text += (text.empty() ? "" : "; ") + issue.to_string();
        }
        return fail(ErrorCode::Validation, "model card: " + text);
    }
    ModelCard card;
    card.document = document;
    try {
        const std::string version = document.at("contract_version").get<std::string>();
        const auto dot = version.find('.');
        card.contract = {.major = std::stoi(version.substr(0, dot)), .minor = std::stoi(version.substr(dot + 1))};
        card.model_name = document.at("model_name").get<std::string>();
        card.model_version = document.at("model_version").get<std::string>();
        card.created_utc = document.at("created_utc").get<std::string>();
        card.git_sha = document.at("git_sha").get<std::string>();
        const nlohmann::json& in = document.at("input");
        card.input.height = in.at("height").get<int>();
        card.input.width = in.at("width").get<int>();
        card.input.patch_size = in.at("patch_size").get<int>();
        for (std::size_t i = 0; i < 3; ++i) {
            card.input.mean[i] = in.at("mean").at(i).get<double>();
            card.input.std[i] = in.at("std").at(i).get<double>();
        }
        card.input.resize = in.at("resize").get<std::string>();
        card.genus_classes = strings(document.at("genus_classes"));
        card.etage_classes = strings(document.at("etage_classes"));
        card.sky_parse_classes = strings(document.at("sky_parse_classes"));
        card.layer_classes = strings(document.at("layer_classes"));
        for (const nlohmann::json& edge : document.at("cbh_bin_edges_m")) {
            card.cbh_bin_edges_m.push_back(edge.get<double>());
        }
        const nlohmann::json& calibration = document.at("calibration");
        for (const auto& [head, value] : calibration.at("temperatures").items()) {
            card.calibration.temperatures[head] = value.get<double>();
        }
        card.calibration.folded_into_graph = calibration.at("folded_into_graph").get<bool>();
        if (calibration.contains("cbh_conformal_radius_cdf")) {
            for (const auto& [level, value] : calibration.at("cbh_conformal_radius_cdf").items()) {
                card.calibration.cbh_conformal_radius_cdf[level] = value.get<double>();
            }
        }
        card.ood_threshold = document.at("ood_threshold").get<double>();
        for (const nlohmann::json& item : document.at("training_data")) {
            card.training_data.push_back({.dataset = item.at("dataset").get<std::string>(),
                                          .manifest_sha256 = item.at("manifest_sha256").get<std::string>()});
        }
        card.licences = strings(document.at("licences"));
        card.evaluation_report = document.value("evaluation_report", std::string());
        card.backbone = document.value("backbone", std::string());
    } catch (const nlohmann::json::exception& error) {
        return fail(ErrorCode::Internal,
                    std::string("model card: the schema and this reader disagree: ") + error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::Validation, std::string("model card: ") + error.what());
    }
    if (auto contract = check_contract(card); !contract) {
        return fail(contract.error());
    }
    return card;
}

Expected<ModelCard> load_model_card(const std::filesystem::path& file)
{
    std::ifstream in(file);
    if (!in) {
        return fail(ErrorCode::NotFound, fmt::format("could not read {}", file.string()));
    }
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception& error) {
        return fail(ErrorCode::Parse, fmt::format("{}: {}", file.string(), error.what()));
    }
    auto card = model_card_from_json(document);
    if (!card) {
        return fail(card.error().with_context(file.string()));
    }
    return card;
}

Expected<void> check_contract(const ModelCard& card)
{
    if (card.contract.major != kContractMajor) {
        return fail(ErrorCode::Unsupported,
                    fmt::format("model '{}' {} uses stratia-contract {}.{}; this build supports {}.x", card.model_name,
                                card.model_version, card.contract.major, card.contract.minor, kContractMajor));
    }
    if (!same_order(card.genus_classes, kGenusClasses)) {
        return fail(ErrorCode::Unsupported, "genus_classes differ from the contract's order: " + joined(kGenusClasses));
    }
    if (!same_order(card.etage_classes, kEtageClasses)) {
        return fail(ErrorCode::Unsupported, "etage_classes differ from the contract's order: " + joined(kEtageClasses));
    }
    if (!same_order(card.sky_parse_classes, kSkyParseClasses)) {
        return fail(ErrorCode::Unsupported,
                    "sky_parse_classes differ from the contract's order: " + joined(kSkyParseClasses));
    }
    if (!same_order(card.layer_classes, kLayerClasses)) {
        return fail(ErrorCode::Unsupported, "layer_classes differ from the contract's order: " + joined(kLayerClasses));
    }
    const ModelInput& in = card.input;
    if (in.patch_size <= 0 || in.height % in.patch_size != 0 || in.width % in.patch_size != 0 ||
        in.height % kDenseDivisor != 0 || in.width % kDenseDivisor != 0) {
        return fail(ErrorCode::Unsupported,
                    fmt::format("input {} x {} is not a multiple of the patch size {} and of {}", in.width, in.height,
                                in.patch_size, kDenseDivisor));
    }
    for (double s : in.std) {
        if (!(s > 0.0)) {
            return fail(ErrorCode::Unsupported, "input.std must be positive");
        }
    }
    const std::vector<double>& edges = card.cbh_bin_edges_m;
    if (edges.size() < 3 || !std::is_sorted(edges.begin(), edges.end()) ||
        std::adjacent_find(edges.begin(), edges.end()) != edges.end() || edges.front() < 0.0) {
        return fail(ErrorCode::Unsupported,
                    "cbh_bin_edges_m must be at least three strictly ascending non-negative edges");
    }
    if (!std::isfinite(card.ood_threshold)) {
        return fail(ErrorCode::Unsupported, "ood_threshold must be a finite number");
    }
    return {};
}

Expected<void> check_model_io(const ModelCard& card, const hal::InferenceCapabilities& capabilities)
{
    const auto find = [](const std::vector<hal::TensorInfo>& tensors, const std::string& name) {
        return std::find_if(tensors.begin(), tensors.end(), [&](const hal::TensorInfo& t) { return t.name == name; });
    };
    for (const hal::TensorInfo& wanted : card.expected_inputs()) {
        const auto it = find(capabilities.inputs, wanted.name);
        if (it == capabilities.inputs.end()) {
            return fail(ErrorCode::Unsupported, fmt::format("the model has no input '{}'", wanted.name));
        }
        if (auto ok = check_tensor(*it, wanted, "input"); !ok) {
            return ok;
        }
    }
    if (capabilities.inputs.size() != kInputNames.size()) {
        return fail(ErrorCode::Unsupported, fmt::format("the model declares {} inputs, the contract has {}",
                                                        capabilities.inputs.size(), kInputNames.size()));
    }
    for (const hal::TensorInfo& wanted : card.expected_outputs()) {
        const auto it = find(capabilities.outputs, wanted.name);
        if (it == capabilities.outputs.end()) {
            return fail(ErrorCode::Unsupported, fmt::format("the model has no output '{}'", wanted.name));
        }
        if (auto ok = check_tensor(*it, wanted, "output"); !ok) {
            return ok;
        }
    }
    return {};
}

Expected<ModelPackage> open_model_package(const std::filesystem::path& folder_or_card)
{
    std::error_code error;
    ModelPackage package;
    if (std::filesystem::is_regular_file(folder_or_card, error)) {
        package.card_file = folder_or_card;
        package.folder = folder_or_card.parent_path();
    } else if (std::filesystem::is_directory(folder_or_card, error)) {
        package.folder = folder_or_card;
        package.card_file = folder_or_card / "model_card.json";
    } else {
        return fail(ErrorCode::NotFound, fmt::format("no model package at {}", folder_or_card.string()));
    }
    if (!std::filesystem::is_regular_file(package.card_file, error)) {
        return fail(ErrorCode::NotFound, fmt::format("{} has no model_card.json", package.folder.string()));
    }
    std::vector<std::filesystem::path> models;
    for (const auto& entry : std::filesystem::directory_iterator(package.folder, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".onnx") {
            models.push_back(entry.path());
        }
    }
    if (models.empty()) {
        return fail(ErrorCode::NotFound,
                    fmt::format("{} has no .onnx file next to the model card", package.folder.string()));
    }
    if (models.size() > 1) {
        return fail(ErrorCode::Validation, fmt::format("{} holds {} .onnx files; a package has exactly one",
                                                       package.folder.string(), models.size()));
    }
    package.model_file = models.front();
    auto card = load_model_card(package.card_file);
    if (!card) {
        return fail(card.error());
    }
    package.card = std::move(*card);
    return package;
}

}  // namespace cloudscope::ai
