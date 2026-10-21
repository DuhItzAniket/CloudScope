// P070, P071: the models dialog installs, activates and rolls back through the store, tells the window which
// model to load, and runs the benchmark off the UI thread.

#include "ai_models_dialog.hpp"
#include "desktop_fixture.hpp"
#include "main_window.hpp"

#include <cloudscope/ai/onnx_engine.hpp>
#include <cloudscope/hal/registry.hpp>

#include <QtTest/QSignalSpy>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace cloudscope;
using namespace cloudscope::desktop;
using cloudscope::desktop::test::DesktopFixture;
using cloudscope::test::data_dir;

namespace {

MainWindowOptions test_options()
{
    return MainWindowOptions{
        .renderer = Renderer::Raster, .theme = std::nullopt, .restore_layout = false, .gamepad = false};
}

std::filesystem::path package_copy(const std::filesystem::path& folder, const std::string& version)
{
    std::filesystem::create_directories(folder);
    std::filesystem::copy_file(data_dir() / "stratia" / "stratia_dummy.onnx", folder / "stratia_dummy.onnx",
                               std::filesystem::copy_options::overwrite_existing);
    std::ifstream in(data_dir() / "stratia" / "model_card.json");
    nlohmann::json card = nlohmann::json::parse(in);
    card["model_version"] = version;
    std::ofstream out(folder / "model_card.json");
    out << card.dump(2);
    return folder;
}

}  // namespace

TEST_CASE("the models dialog installs, activates and rolls back, naming the folder to load", "[desktop][ai][models]")
{
    const cloudscope::test::TempWorkspace workspace;
    hal::DeviceRegistry registry;
    AiModelsDialog dialog(workspace.path("store"), registry);
    dialog.show();
    CHECK(dialog.table()->rowCount() == 0);
    CHECK_FALSE(dialog.activate_button()->isEnabled());
    CHECK_FALSE(dialog.rollback_button()->isEnabled());

    QSignalSpy active_spy(&dialog, &AiModelsDialog::activeModelChanged);
    QSignalSpy notice_spy(&dialog, &AiModelsDialog::notice);
    const auto first = dialog.install_from(package_copy(workspace.path("p1"), "1.0.0"), true);
    REQUIRE(first.has_value());
    CHECK(dialog.table()->rowCount() == 1);
    REQUIRE(active_spy.count() == 1);
    CHECK(active_spy.takeFirst().at(0).toString().contains(QStringLiteral("stratia-dummy-1.0.0")));
    CHECK(notice_spy.count() == 1);

    REQUIRE(dialog.install_from(package_copy(workspace.path("p2"), "1.1.0"), false).has_value());
    CHECK(dialog.table()->rowCount() == 2);
    CHECK(active_spy.count() == 0);
    dialog.select("stratia-dummy-1.1.0");
    CHECK(dialog.selected_id() == std::string("stratia-dummy-1.1.0"));
    dialog.activate_button()->click();
    REQUIRE(active_spy.count() == 1);
    CHECK(active_spy.takeFirst().at(0).toString().contains(QStringLiteral("stratia-dummy-1.1.0")));
    CHECK((*dialog.store().active())->id == "stratia-dummy-1.1.0");
    CHECK(dialog.rollback_button()->isEnabled());

    dialog.rollback_button()->click();
    REQUIRE(active_spy.count() == 1);
    CHECK(active_spy.takeFirst().at(0).toString().contains(QStringLiteral("stratia-dummy-1.0.0")));
    CHECK((*dialog.store().active())->id == "stratia-dummy-1.0.0");

    dialog.select("stratia-dummy-1.0.0");
    dialog.remove_button()->click();  // the active one: refused with a notice
    CHECK(dialog.table()->rowCount() == 2);
    dialog.select("stratia-dummy-1.1.0");
    REQUIRE(dialog.remove_selected().has_value());
    CHECK(dialog.table()->rowCount() == 1);
}

TEST_CASE("the models dialog benchmarks the selected model on a worker thread", "[desktop][ai][models]")
{
    if (!ai::onnx_runtime_available()) {
        SKIP("this build has no ONNX Runtime");
    }
    const cloudscope::test::TempWorkspace workspace;
    hal::DeviceRegistry registry;
    REQUIRE(registry.add_driver(ai::OnnxDriver::make()).has_value());
    AiModelsDialog dialog(workspace.path("store"), registry);
    REQUIRE(dialog.install_from(package_copy(workspace.path("p1"), "1.0.0"), true).has_value());
    QSignalSpy finished(&dialog, &AiModelsDialog::benchmarkFinished);
    ai::BenchmarkOptions options;
    options.runs = 2;
    options.warmup = 0;
    options.provider = "cpu";
    options.width = 320;
    options.height = 240;
    dialog.run_benchmark(options);
    CHECK(dialog.benchmark_running());
    CHECK_FALSE(dialog.benchmark_button()->isEnabled());
    REQUIRE(finished.wait(60000));
    CHECK_FALSE(dialog.benchmark_running());
    const QString markdown = finished.takeFirst().at(0).toString();
    CHECK(markdown.contains(QStringLiteral("| Engine |")));
    CHECK(markdown.contains(QStringLiteral("onnx:inference:cpu")));
    CHECK(dialog.benchmark_output()->toPlainText() == markdown);
}

TEST_CASE("activating a model in the window loads it and remembers it in the user configuration",
          "[desktop][ai][models]")
{
    if (!ai::onnx_runtime_available()) {
        SKIP("this build has no ONNX Runtime");
    }
    DesktopFixture fixture;
    MainWindow window(*fixture.context, test_options());
    window.show();
    CHECK(window.shortcuts().action(QStringLiteral("ai.models")) != nullptr);
    AiModelsDialog& dialog = window.models_dialog();
    REQUIRE(dialog.install_from(package_copy(fixture.workspace.path("p1"), "1.0.0"), true).has_value());
    CHECK(window.ai().status().loaded);
    CHECK(window.ai().status().model_version == "1.0.0");
    const nlohmann::json user_file = cloudscope::desktop::test::read_config_file(fixture.paths.user_config);
    const std::string remembered = user_file.value("ai", nlohmann::json::object()).value("model_dir", std::string());
    CHECK(remembered.find("stratia-dummy-1.0.0") != std::string::npos);
}
