// P070: the benchmark times the model on every engine and reports a table. P071: the model store installs
// packages with checksums, activates one, rolls back to the previous one and refuses corrupt files.

#include "hal_contract.hpp"
#include "test_support.hpp"

#include <cloudscope/ai/benchmark.hpp>
#include <cloudscope/ai/model_store.hpp>
#include <cloudscope/ai/onnx_engine.hpp>
#include <cloudscope/hal/registry.hpp>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace cloudscope;
using namespace cloudscope::ai;
using cloudscope::test::data_dir;
using cloudscope::test::outcome;
using cloudscope::test::TempWorkspace;

namespace {

std::filesystem::path copy_package(const TempWorkspace& workspace, const std::string& name,
                                   const std::string& version = {})
{
    const std::filesystem::path folder = workspace.path(name);
    std::filesystem::create_directories(folder);
    std::filesystem::copy_file(data_dir() / "stratia" / "stratia_dummy.onnx", folder / "stratia_dummy.onnx");
    std::ifstream in(data_dir() / "stratia" / "model_card.json");
    nlohmann::json card = nlohmann::json::parse(in);
    if (!version.empty()) {
        card["model_version"] = version;
    }
    std::ofstream out(folder / "model_card.json");
    out << card.dump(2);
    return folder;
}

}  // namespace

TEST_CASE("the benchmark reports every engine with its timings and a Markdown table", "[ai][benchmark]")
{
    if (!onnx_runtime_available()) {
        SKIP("this build has no ONNX Runtime");
    }
    const auto package = open_model_package(data_dir() / "stratia");
    REQUIRE(outcome(package) == "ok");
    hal::DeviceRegistry registry;
    REQUIRE(outcome(registry.add_driver(OnnxDriver::make())) == "ok");
    BenchmarkOptions options;
    options.runs = 3;
    options.warmup = 1;
    options.provider = "all";
    options.width = 640;
    options.height = 480;
    const auto report = benchmark_model(*package, registry, options);
    REQUIRE(outcome(report) == "ok");
    CHECK(report->model_name == "stratia-dummy");
    CHECK(report->width == 640);
    REQUIRE_FALSE(report->rows.empty());
    const auto cpu = std::find_if(report->rows.begin(), report->rows.end(),
                                  [](const BenchmarkRow& r) { return r.device_id == "onnx:inference:cpu"; });
    REQUIRE(cpu != report->rows.end());
    CHECK(cpu->ok);
    CHECK(cpu->runs == 3);
    CHECK(cpu->mean_ms > 0.0);
    CHECK(cpu->p50_ms <= cpu->p95_ms);
    CHECK(cpu->min_ms <= cpu->p50_ms);
    CHECK(cpu->load_ms > 0.0);
    const std::string markdown = benchmark_markdown(*report);
    CHECK(markdown.find("| Engine |") != std::string::npos);
    CHECK(markdown.find("onnx:inference:cpu") != std::string::npos);
    const nlohmann::json json = benchmark_json(*report);
    CHECK(json.at("rows").size() == report->rows.size());

    BenchmarkOptions one;
    one.runs = 1;
    one.warmup = 0;
    one.provider = "cpu";
    one.width = 320;
    one.height = 240;
    const auto only_cpu = benchmark_model(*package, registry, one);
    REQUIRE(outcome(only_cpu) == "ok");
    REQUIRE(only_cpu->rows.size() == 1);
    CHECK(only_cpu->rows.front().runs == 1);
    BenchmarkOptions none;
    none.provider = "abacus";
    CHECK(outcome(benchmark_model(*package, registry, none)) == "Unavailable");
}

TEST_CASE("the model store installs, activates, rolls back and removes models with checksums", "[ai][model_store]")
{
    const TempWorkspace workspace;
    ModelStore store(workspace.path("models"));
    CHECK(store.list()->empty());
    CHECK_FALSE(store.active()->has_value());
    CHECK(outcome(store.rollback()) == "Unavailable");

    const std::filesystem::path v1 = copy_package(workspace, "pkg1", "1.0.0");
    const auto first = store.install(v1, true);
    REQUIRE(outcome(first) == "ok");
    CHECK(first->id == "stratia-dummy-1.0.0");
    CHECK(first->active);
    CHECK_FALSE(first->checksum_declared);  // nothing declared a checksum: the computed one is recorded
    CHECK(first->onnx_sha256.size() == 64);
    REQUIRE(store.active()->has_value());
    CHECK((*store.active())->id == "stratia-dummy-1.0.0");
    CHECK(outcome(store.verify("stratia-dummy-1.0.0")) == "ok");

    // a package that declares its checksum (STRATIA's export report) is verified against it
    const std::filesystem::path v2 = copy_package(workspace, "pkg2", "1.1.0");
    {
        std::ofstream report(v2 / "export_report.json");
        report << nlohmann::json{{"onnx_sha256", file_sha256(v2 / "stratia_dummy.onnx")}}.dump();
    }
    const auto second = store.install(v2, true);
    REQUIRE(outcome(second) == "ok");
    CHECK(second->checksum_declared);
    CHECK((*store.active())->id == "stratia-dummy-1.1.0");
    CHECK(*store.previous() == "stratia-dummy-1.0.0");
    CHECK(store.list()->size() == 2);

    // a tampered package is refused
    const std::filesystem::path bad = copy_package(workspace, "pkg3", "1.2.0");
    {
        std::ofstream report(bad / "export_report.json");
        report << nlohmann::json{{"onnx_sha256", std::string(64, 'a')}}.dump();
    }
    const auto refused = store.install(bad);
    CHECK(outcome(refused) == "Validation");
    CHECK(refused.error().message.find("checksum") != std::string::npos);
    CHECK(store.list()->size() == 2);

    // rollback restores the previous active model, and rollback again goes forward
    REQUIRE(outcome(store.rollback()) == "ok");
    CHECK((*store.active())->id == "stratia-dummy-1.0.0");
    CHECK(*store.previous() == "stratia-dummy-1.1.0");
    REQUIRE(outcome(store.rollback()) == "ok");
    CHECK((*store.active())->id == "stratia-dummy-1.1.0");

    // corruption after installation is caught by verify() and blocks activation
    {
        std::ofstream corrupt((*store.find("stratia-dummy-1.0.0"))->model_file, std::ios::binary | std::ios::app);
        corrupt << "garbage";
    }
    CHECK(outcome(store.verify("stratia-dummy-1.0.0")) == "Validation");
    CHECK(outcome(store.activate("stratia-dummy-1.0.0")) == "Validation");
    CHECK(outcome(store.rollback()) == "Validation");
    CHECK((*store.active())->id == "stratia-dummy-1.1.0");

    // the active model cannot be removed; another one can
    CHECK(outcome(store.remove("stratia-dummy-1.1.0")) == "InvalidArgument");
    REQUIRE(outcome(store.remove("stratia-dummy-1.0.0")) == "ok");
    CHECK(store.list()->size() == 1);
    CHECK_FALSE(store.previous()->has_value());
    CHECK(outcome(store.remove("stratia-dummy-1.0.0")) == "NotFound");
    CHECK(outcome(store.activate("nope")) == "NotFound");
    CHECK(outcome(store.install(workspace.path("nowhere"))) == "NotFound");
}
