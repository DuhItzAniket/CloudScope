// P065: model cards are validated against the schema and the contract; packages are found next to their card.

#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/ai/contract.hpp>
#include <cloudscope/ai/model_card.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace cloudscope;
using namespace cloudscope::ai;
using cloudscope::test::data_path;
using cloudscope::test::outcome;
using cloudscope::test::TempWorkspace;

namespace {

nlohmann::json dummy_card()
{
    std::ifstream in(data_path("stratia/model_card.json"));
    REQUIRE(in);
    return nlohmann::json::parse(in);
}

void write_json(const std::filesystem::path& file, const nlohmann::json& document)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file);
    out << document.dump(2);
}

}  // namespace

TEST_CASE("the contract constants describe the v1 tensors", "[ai][contract]")
{
    const auto inputs = expected_inputs(512, 512);
    REQUIRE(inputs.size() == 3);
    CHECK(inputs[0].name == "image");
    CHECK(inputs[0].shape == std::vector<std::int64_t>{-1, 3, 512, 512});
    CHECK(inputs[1].name == "ray_map");
    CHECK(inputs[1].shape == std::vector<std::int64_t>{-1, 4, 32, 32});
    CHECK(inputs[2].shape == std::vector<std::int64_t>{-1, 3});
    const auto outputs = expected_outputs(512, 512, 24);
    REQUIRE(outputs.size() == 7);
    CHECK(outputs[3].name == "sky_parse_logits");
    CHECK(outputs[3].shape == std::vector<std::int64_t>{-1, 4, 128, 128});
    CHECK(outputs[5].shape == std::vector<std::int64_t>{-1, 25});
    CHECK(outputs[6].shape == std::vector<std::int64_t>{-1, 1});
    CHECK(no_cloud_index(24) == 24);
}

TEST_CASE("STRATIA's dummy model card loads, validates and keeps its numbers", "[ai][model_card]")
{
    const auto card = load_model_card(data_path("stratia/model_card.json"));
    REQUIRE(outcome(card) == "ok");
    CHECK(card->contract == ContractVersion{.major = 1, .minor = 0});
    CHECK(card->model_name == "stratia-dummy");
    CHECK(card->input.height == 512);
    CHECK(card->input.width == 512);
    CHECK(card->input.patch_size == kPatchSize);
    CHECK(card->genus_classes.size() == kGenusClasses.size());
    CHECK(card->genus_classes.front() == "Ci");
    CHECK(card->genus_classes.back() == "contrail");
    CHECK(card->cbh_bins() == 24);
    CHECK_THAT(card->cbh_bin_edges_m.back(), Catch::Matchers::WithinAbs(12000.0, 1e-9));
    CHECK(card->calibration.folded_into_graph);
    CHECK(card->calibration.temperatures.at("genus") > 1.0);
    CHECK(card->licences.size() >= 1);
    CHECK(card->training_data.size() == 1);
    CHECK(card->expected_outputs()[5].shape == std::vector<std::int64_t>{-1, 25});
}

TEST_CASE("cards that break the schema or the contract are refused with the reason", "[ai][model_card]")
{
    const nlohmann::json good = dummy_card();
    REQUIRE(outcome(model_card_from_json(good)) == "ok");

    SECTION("a missing required field is a validation error")
    {
        nlohmann::json bad = good;
        bad.erase("ood_threshold");
        const auto card = model_card_from_json(bad);
        CHECK(outcome(card) == "Validation");
        CHECK(card.error().message.find("ood_threshold") != std::string::npos);
    }
    SECTION("another major version is unsupported")
    {
        nlohmann::json bad = good;
        bad["contract_version"] = "2.0";
        const auto card = model_card_from_json(bad);
        CHECK(outcome(card) != "ok");  // the schema pins 1.x; a schema of a newer host would say Unsupported
    }
    SECTION("a reordered class list is unsupported")
    {
        nlohmann::json bad = good;
        std::swap(bad["etage_classes"][0], bad["etage_classes"][2]);
        // the schema fixes the class lists too; check_contract guards the reader's own constants
        ModelCard card = *model_card_from_json(good);
        std::swap(card.etage_classes[0], card.etage_classes[2]);
        const auto checked = check_contract(card);
        CHECK(outcome(checked) == "Unsupported");
        CHECK(checked.error().message.find("etage_classes") != std::string::npos);
        CHECK(outcome(model_card_from_json(bad)) != "ok");
    }
    SECTION("an input that is not a multiple of the patch size is unsupported")
    {
        nlohmann::json bad = good;
        bad["input"]["height"] = 500;
        const auto card = model_card_from_json(bad);
        CHECK(outcome(card) == "Unsupported");
    }
    SECTION("bin edges must ascend")
    {
        nlohmann::json bad = good;
        bad["cbh_bin_edges_m"] = nlohmann::json::array({0.0, 2000.0, 1000.0});
        CHECK(outcome(model_card_from_json(bad)) == "Unsupported");
    }
    SECTION("a newer minor version of the same major is accepted")
    {
        nlohmann::json ok = good;
        ok["contract_version"] = "1.3";
        const auto card = model_card_from_json(ok);
        REQUIRE(outcome(card) == "ok");
        CHECK(card->contract.minor == 3);
    }
}

TEST_CASE("a model's declared tensors are checked against the card", "[ai][model_card]")
{
    const ModelCard card = *model_card_from_json(dummy_card());
    hal::InferenceCapabilities capabilities{
        .provider = "CPU", .inputs = card.expected_inputs(), .outputs = card.expected_outputs()};
    CHECK(outcome(check_model_io(card, capabilities)) == "ok");

    hal::InferenceCapabilities fixed_batch = capabilities;
    for (hal::TensorInfo& t : fixed_batch.inputs) {
        t.shape[0] = 1;
    }
    CHECK(outcome(check_model_io(card, fixed_batch)) == "ok");

    hal::InferenceCapabilities renamed = capabilities;
    renamed.inputs[1].name = "rays";
    const auto missing = check_model_io(card, renamed);
    CHECK(outcome(missing) == "Unsupported");
    CHECK(missing.error().message.find("ray_map") != std::string::npos);

    hal::InferenceCapabilities wrong_shape = capabilities;
    wrong_shape.outputs[5].shape = {-1, 10};
    const auto shape = check_model_io(card, wrong_shape);
    CHECK(outcome(shape) == "Unsupported");
    CHECK(shape.error().message.find("cbh_probs") != std::string::npos);

    hal::InferenceCapabilities extra = capabilities;
    extra.inputs.push_back({.name = "extra", .shape = {-1, 1}});
    CHECK(outcome(check_model_io(card, extra)) == "Unsupported");
}

TEST_CASE("a model package is the card plus exactly one ONNX file", "[ai][model_card]")
{
    const TempWorkspace workspace;
    const std::filesystem::path folder = workspace.path("model");
    write_json(folder / "model_card.json", dummy_card());
    CHECK(outcome(open_model_package(folder)) == "NotFound");  // no ONNX file yet

    std::filesystem::copy_file(data_path("stratia/stratia_dummy.onnx"), folder / "stratia_dummy.onnx");
    const auto package = open_model_package(folder);
    REQUIRE(outcome(package) == "ok");
    CHECK(package->model_file.filename() == "stratia_dummy.onnx");
    CHECK(package->card.model_name == "stratia-dummy");
    CHECK(outcome(open_model_package(folder / "model_card.json")) == "ok");  // the card's path works too

    std::filesystem::copy_file(data_path("stratia/stratia_dummy.onnx"), folder / "second.onnx");
    CHECK(outcome(open_model_package(folder)) == "Validation");

    CHECK(outcome(open_model_package(workspace.path("nowhere"))) == "NotFound");
    const std::filesystem::path no_card = workspace.path("no_card");
    std::filesystem::create_directories(no_card);
    std::filesystem::copy_file(data_path("stratia/stratia_dummy.onnx"), no_card / "model.onnx");
    CHECK(outcome(open_model_package(no_card)) == "NotFound");
}
