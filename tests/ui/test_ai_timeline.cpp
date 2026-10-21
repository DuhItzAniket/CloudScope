// P069: timelines of the STRATIA results: the model keeps a window of points, the chart paints them, the panel
// replays a session from the catalogue, and the main window logs every result into the catalogue.

#include "ai_controller.hpp"
#include "ai_timeline_panel.hpp"
#include "desktop_fixture.hpp"
#include "main_window.hpp"

#include <cloudscope/ai/result_log.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/session/catalogue.hpp>

#include <DockWidget.h>

#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>

using namespace cloudscope;
using namespace cloudscope::desktop;
using cloudscope::desktop::test::DesktopFixture;

namespace {

MainWindowOptions test_options()
{
    return MainWindowOptions{
        .renderer = Renderer::Raster, .theme = std::nullopt, .restore_layout = false, .gamepad = false};
}

ai::Prediction sample(UtcTime utc, double cover, std::optional<double> base, int genus, bool uncertain = false)
{
    ai::Prediction p;
    p.frame.captured.utc = utc;
    p.frame.sequence = static_cast<std::uint64_t>(to_unix_ms(utc) / 1000);
    p.model_name = "stratia";
    p.model_version = "test";
    p.provider = "CPU";
    p.genus_probs.fill(0.05F);
    p.genus_probs[static_cast<std::size_t>(genus)] = 0.8F;
    p.genus_top = genus;
    p.cover_fraction = cover;
    p.oktas = static_cast<int>(cover * 8.0);
    if (base) {
        p.cbh = ai::CbhEstimate{.median_m = *base,
                                .low_80_m = *base * 0.8,
                                .high_80_m = *base * 1.3,
                                .low_95_m = *base * 0.6,
                                .high_95_m = *base * 1.6};
        p.p_cloud_overhead = 0.9;
    }
    p.uncertain = uncertain;
    p.ood_score = uncertain ? 3.0F : 0.4F;
    return p;
}

}  // namespace

TEST_CASE("the timeline model keeps points in time order inside its window", "[desktop][ai][timeline]")
{
    AiTimelineModel model;
    model.set_window(std::chrono::seconds(600));
    const UtcTime t0 = from_unix_ms(1'760'000'000'000);
    model.add(timeline_point(sample(t0, 0.2, 1000.0, 8)));
    model.add(timeline_point(sample(t0 + std::chrono::seconds(60), 0.4, std::nullopt, 6)));
    model.add(
        timeline_point(sample(t0 + std::chrono::seconds(30), 0.3, 1200.0, 0)));  // out of order: inserted in place
    REQUIRE(model.size() == 3);
    CHECK(model.points()[1].utc == t0 + std::chrono::seconds(30));
    CHECK_FALSE(model.points()[2].cbh_median_m.has_value());
    model.add(timeline_point(sample(t0 + std::chrono::seconds(500), 0.9, 500.0, 9)));  // 500 s later: still inside
    CHECK(model.size() == 4);
    model.add(timeline_point(sample(t0 + std::chrono::seconds(700), 0.9, 500.0, 9)));  // the first three fall out
    CHECK(model.size() == 2);
    CHECK(model.points().front().utc == t0 + std::chrono::seconds(500));
    model.set_window(std::chrono::seconds(10));
    CHECK(model.size() == 1);
    model.clear();
    CHECK(model.size() == 0);
}

TEST_CASE("the chart paints cover, base and genus for a model with points", "[desktop][ai][timeline]")
{
    AiTimelineModel model;
    const UtcTime t0 = from_unix_ms(1'760'000'000'000);
    for (int i = 0; i < 20; ++i) {
        model.add(timeline_point(
            sample(t0 + std::chrono::seconds(10 * i), 0.1 + 0.04 * i, 800.0 + 50.0 * i, i % 12, i == 7)));
    }
    AiTimelineWidget widget;
    widget.set_model(&model);
    widget.set_colours(QColor(230, 230, 230), QColor(255, 140, 40), QColor(90, 200, 255));
    QImage image(480, 180, QImage::Format_RGB32);
    image.fill(QColor(10, 10, 10));
    {
        QPainter painter(&image);
        widget.paint(painter, QRectF(0, 0, 480, 180));
    }
    int bright = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            bright += qGray(image.pixel(x, y)) > 60 ? 1 : 0;
        }
    }
    CHECK(bright > 500);  // lines, band and the genus strip were drawn
    AiTimelineModel empty;
    widget.set_model(&empty);
    QImage blank(480, 180, QImage::Format_RGB32);
    blank.fill(QColor(10, 10, 10));
    {
        QPainter painter(&blank);
        widget.paint(painter, QRectF(0, 0, 480, 180));
    }
    int bright_blank = 0;
    for (int y = 0; y < blank.height(); ++y) {
        for (int x = 0; x < blank.width(); ++x) {
            bright_blank += qGray(blank.pixel(x, y)) > 60 ? 1 : 0;
        }
    }
    CHECK(bright_blank < bright);
}

TEST_CASE("the panel takes live results and replays a session from the catalogue", "[desktop][ai][timeline]")
{
    const cloudscope::test::TempWorkspace workspace;
    auto catalogue = Catalogue::open(workspace.path("catalogue.sqlite"));
    REQUIRE(catalogue.has_value());
    const UtcTime t0 = from_unix_ms(1'760'000'000'000);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(ai::log_prediction(**catalogue, sample(t0 + std::chrono::seconds(30 * i), 0.5, 1500.0, 3), "night-1")
                    .has_value());
    }
    REQUIRE(ai::log_prediction(**catalogue, sample(t0 + std::chrono::seconds(300), 0.1, std::nullopt, 10), "night-2")
                .has_value());

    AiTimelinePanel panel;
    panel.show();
    CHECK(panel.summary_label()->text().contains(QStringLiteral("No results")));
    panel.add_prediction(sample(t0, 0.25, 900.0, 8));
    panel.add_prediction(sample(t0 + std::chrono::seconds(30), 0.75, 1100.0, 8));
    CHECK(panel.model().size() == 2);
    CHECK(panel.summary_label()->text().contains(QStringLiteral("2 results")));
    CHECK(panel.summary_label()->text().contains(QStringLiteral("50 %")));

    SessionInfo a;
    a.id = "night-1";
    SessionInfo b;
    b.id = "night-2";
    panel.set_sessions({a, b});
    CHECK(panel.session_box()->count() == 3);

    const auto replayed = panel.replay(**catalogue, AiResultQuery{.session_id = std::string("night-1"),
                                                                  .from = std::nullopt,
                                                                  .to = std::nullopt,
                                                                  .limit = 10000,
                                                                  .newest_first = false});
    REQUIRE(replayed.has_value());
    CHECK(*replayed == 5);
    CHECK(panel.model().size() == 5);
    CHECK(panel.summary_label()->text().contains(QStringLiteral("replayed")));
    CHECK(panel.model().points().front().utc == t0);
    CHECK(panel.model().points().back().cbh_median_m.value_or(0.0) == 1500.0);
    panel.add_prediction(sample(t0 + std::chrono::seconds(600), 0.9, 500.0, 9));  // ignored while replaying
    CHECK(panel.model().size() == 5);

    panel.live_button()->click();
    CHECK(panel.model().size() == 0);
    panel.add_prediction(sample(t0 + std::chrono::seconds(600), 0.9, 500.0, 9));
    CHECK(panel.model().size() == 1);
}

TEST_CASE("the main window logs every result into the catalogue and shows it on the timeline",
          "[desktop][ai][timeline]")
{
    DesktopFixture fixture;
    MainWindow window(*fixture.context, test_options());
    window.show();
    REQUIRE(window.docks().count(QStringLiteral("ai_timeline")) == 1);
    const UtcTime t0 = fixture.context->clock().now_utc();
    auto first = std::make_shared<const ai::Prediction>(sample(t0, 0.5, 1000.0, 8));
    auto second = std::make_shared<const ai::Prediction>(sample(t0 + std::chrono::seconds(1), 0.6, 1200.0, 8, true));
    Q_EMIT window.ai().predictionReady(first);
    Q_EMIT window.ai().predictionReady(second);
    CHECK(window.ai_timeline().model().size() == 2);
    CHECK(window.ai_timeline().summary_label()->text().contains(QStringLiteral("1 uncertain")));

    auto catalogue = Catalogue::open(fixture.context->settings().session_root / "catalogue.sqlite");
    REQUIRE(catalogue.has_value());
    const auto count = (*catalogue)->ai_result_count();
    REQUIRE(count.has_value());
    CHECK(*count == 2);
    const auto rows = (*catalogue)
                          ->ai_results(AiResultQuery{.session_id = std::nullopt,
                                                     .from = std::nullopt,
                                                     .to = std::nullopt,
                                                     .limit = 10000,
                                                     .newest_first = true});
    REQUIRE(rows.has_value());
    REQUIRE(rows->size() == 2);
    CHECK(rows->front().uncertain);
    CHECK(rows->front().session_id.empty());  // no recording session was running
}
