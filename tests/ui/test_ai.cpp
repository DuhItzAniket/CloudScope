// P068: the STRATIA panel, the AI overlay and the main window's AI wiring, driven with the dummy model.

#include "ai_overlay.hpp"
#include "ai_panel.hpp"
#include "desktop_fixture.hpp"
#include "live_view.hpp"
#include "main_window.hpp"

#include <cloudscope/ai/inference_service.hpp>
#include <cloudscope/capture/frame.hpp>
#include <cloudscope/sim/synthetic_sky.hpp>

#include <DockWidget.h>

#include <QtCore/QDir>
#include <QtGui/QAction>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtTest/QSignalSpy>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLabel>
#include <catch2/catch_test_macros.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <memory>

using namespace cloudscope;
using namespace cloudscope::desktop;
using cloudscope::desktop::test::DesktopFixture;
using cloudscope::test::data_path;

namespace {

MainWindowOptions test_options()
{
    return MainWindowOptions{
        .renderer = Renderer::Raster, .theme = std::nullopt, .restore_layout = false, .gamepad = false};
}

std::shared_ptr<ai::Prediction> sample_prediction(bool uncertain)
{
    auto p = std::make_shared<ai::Prediction>();
    p->frame.sequence = 42;
    p->frame.width = 640;
    p->frame.height = 480;
    p->model_name = "stratia";
    p->model_version = "test";
    p->provider = "CPU";
    p->genus_probs.fill(0.05F);
    p->genus_probs[8] = 0.82F;  // Cu
    p->genus_top = 8;
    p->etage_probs = {0.9F, 0.2F, 0.1F};
    p->oktas_probs.fill(0.0F);
    p->oktas_probs[3] = 1.0F;
    p->oktas = 3;
    p->cover_fraction = 3.0 / 8.0;
    p->cbh_probs.assign(25, 0.0F);
    p->cbh_probs[5] = 0.8F;
    p->cbh_probs[24] = 0.2F;
    p->p_cloud_overhead = 0.8;
    p->cbh = ai::CbhEstimate{
        .median_m = 1200.0, .low_80_m = 900.0, .high_80_m = 1600.0, .low_95_m = 700.0, .high_95_m = 2000.0};
    p->sky_parse = cv::Mat(128, 128, CV_8UC1, cv::Scalar(1));
    p->sky_parse(cv::Rect(0, 64, 128, 64)).setTo(2);  // bottom half cloud
    p->sky_parse(cv::Rect(0, 0, 16, 16)).setTo(3);    // a glare patch
    p->layer = cv::Mat(128, 128, CV_8UC1, cv::Scalar(255));
    p->layer(cv::Rect(0, 64, 128, 64)).setTo(0);  // low
    p->sky_fractions = {0.0F, 0.5F, 0.5F, 0.0F};
    p->ood_score = uncertain ? 3.0F : 0.5F;
    p->uncertain = uncertain;
    p->run_ms = 85.0;
    p->total_ms = 97.0;
    return p;
}

}  // namespace

TEST_CASE("the overlay picture tints cloud and glare, and hides masks of uncertain results", "[desktop][ai][overlay]")
{
    const auto p = sample_prediction(false);
    const QImage image = ai_overlay_image(*p, AiOverlaySettings{});
    REQUIRE_FALSE(image.isNull());
    CHECK(image.size() == QSize(128, 128));
    CHECK(qAlpha(image.pixel(64, 10)) == 0);                    // sky: clear
    CHECK(qAlpha(image.pixel(64, 100)) > 0);                    // cloud: tinted
    CHECK(qRed(image.pixel(5, 5)) > qBlue(image.pixel(5, 5)));  // glare: orange
    const QImage layers = ai_overlay_image(*p, AiOverlaySettings{.masks = false, .layers = true});
    CHECK(qAlpha(layers.pixel(64, 100)) > 0);
    CHECK(qAlpha(layers.pixel(5, 5)) == 0);  // glare only with the masks
    CHECK(ai_overlay_image(*p, AiOverlaySettings{.masks = false, .layers = false}).isNull());

    const auto uncertain = sample_prediction(true);
    CHECK(ai_overlay_image(*uncertain, AiOverlaySettings{}).isNull());
    CHECK_FALSE(ai_overlay_image(*uncertain, AiOverlaySettings{.show_when_uncertain = true}).isNull());

    CHECK(ai_badge_text(*p).contains(QStringLiteral("Cu")));
    CHECK(ai_badge_text(*p).contains(QStringLiteral("3 okta")));
    CHECK(ai_badge_text(*p).contains(QStringLiteral("1.2 km")));
    CHECK(ai_badge_text(*uncertain).contains(QStringLiteral("uncertain")));
    CHECK(height_text(850.0) == QStringLiteral("850 m"));
}

TEST_CASE("the live view paints the overlay over the picture and the badge in the corner", "[desktop][ai][overlay]")
{
    auto frame = std::make_shared<PreviewFrame>();
    frame->image = QImage(256, 256, QImage::Format_RGB32);
    frame->image.fill(QColor(0, 0, 0));
    frame->info.width = 256;
    frame->info.height = 256;
    LiveViewScene scene;
    scene.frame = frame;
    scene.viewport.set_widget_size(QSizeF(256.0, 256.0));
    scene.viewport.set_image_size(QSize(256, 256));
    scene.show_indicators = false;
    scene.overlays = {};
    scene.overlays.reticle = false;
    scene.prediction = sample_prediction(false);
    scene.ai_overlay = AiOverlaySettings{.badge = false, .opacity_percent = 100};
    scene.ai_image = ai_overlay_image(*scene.prediction, scene.ai_overlay);
    QImage rendered(256, 256, QImage::Format_RGB32);
    {
        QPainter painter(&rendered);
        paint_live_view(painter, scene);
    }
    CHECK(qGray(rendered.pixel(128, 40)) < 20);    // sky half: the black picture shows
    CHECK(qGray(rendered.pixel(128, 200)) > 200);  // cloud half: white tint at full opacity

    scene.ai_overlay.badge = true;
    scene.ai_overlay.masks = false;
    scene.ai_image = QImage();
    {
        QPainter painter(&rendered);
        paint_live_view(painter, scene);
    }
    bool badge_drawn = false;
    for (int x = 10; x < 120 && !badge_drawn; ++x) {
        badge_drawn = qGray(rendered.pixel(x, 18)) > 100;
    }
    CHECK(badge_drawn);
}

TEST_CASE("the STRATIA panel shows a result with its uncertainty and reports the overlay switches",
          "[desktop][ai][panel]")
{
    AiPanel panel;
    panel.show();
    CHECK(panel.model_label()->text().contains(QStringLiteral("none")));
    ai::ServiceStatus status{.loaded = true,
                             .model_name = "stratia",
                             .model_version = "1.0",
                             .provider = "DirectML",
                             .engine_id = "onnx:inference:directml",
                             .message = {},
                             .rate_hz = 2.0};
    panel.set_status(
        status, ai::ServiceStats{
                    .submitted = 10, .run = 4, .dropped = 6, .failed = 0, .mean_run_ms = 80.0, .last_run_ms = 85.0});
    CHECK(panel.model_label()->text() == QStringLiteral("stratia 1.0"));
    CHECK(panel.provider_label()->text().contains(QStringLiteral("DirectML")));

    panel.show_prediction(sample_prediction(false));
    CHECK(panel.genus_label()->text().contains(QStringLiteral("Cu")));
    CHECK(panel.genus_label()->text().contains(QStringLiteral("82")));
    CHECK(panel.cover_label()->text().contains(QStringLiteral("3 okta")));
    CHECK(panel.cbh_label()->text().contains(QStringLiteral("1.2 km")));
    CHECK(panel.reliability_label()->text().contains(QStringLiteral("reliable")));
    panel.show_prediction(sample_prediction(true));
    CHECK(panel.reliability_label()->text().contains(QStringLiteral("UNCERTAIN")));
    panel.clear_prediction();
    CHECK(panel.genus_label()->text() == QStringLiteral("–"));

    QSignalSpy overlay_spy(&panel, &AiPanel::overlaySettingsChanged);
    panel.masks_checkbox()->setChecked(false);
    REQUIRE(overlay_spy.count() == 1);
    CHECK_FALSE(panel.overlay_settings().masks);
    panel.set_overlay_settings(AiOverlaySettings{.masks = true, .layers = true, .opacity_percent = 70});
    CHECK(overlay_spy.count() == 1);  // set_overlay_settings does not emit
    CHECK(panel.overlay_settings().layers);
    CHECK(panel.overlay_settings().opacity_percent == 70);

    QSignalSpy run_spy(&panel, &AiPanel::runningChanged);
    panel.run_checkbox()->setChecked(false);
    CHECK(run_spy.count() == 1);
    CHECK_FALSE(panel.running());
}

TEST_CASE("the main window runs the dummy model on preview frames and shows the result everywhere",
          "[desktop][ai][main_window]")
{
    if (!ai::onnx_runtime_available()) {
        SKIP("this build has no ONNX Runtime");
    }
    DesktopFixture fixture;
    MainWindow window(*fixture.context, test_options());
    window.show();
    REQUIRE(window.docks().count(QStringLiteral("ai")) == 1);
    CHECK(window.status_bar().ai_label()->text().contains(QStringLiteral("not available")));
    CHECK(window.shortcuts().action(QStringLiteral("ai.run"))->isChecked());

    REQUIRE(window.ai().load(cloudscope::test::data_dir() / "stratia"));
    CHECK(window.ai().status().loaded);
    CHECK(window.status_bar().ai_label()->text().contains(QStringLiteral("stratia-dummy")));

    sim::SyntheticSkySpec spec;
    spec.width = 320;
    spec.height = 240;
    const auto sky = sim::make_synthetic_sky(spec);
    REQUIRE(sky.has_value());
    auto frame = std::make_shared<PreviewFrame>();
    frame->picture = std::make_shared<const cv::Mat>(sky->image.clone());
    frame->image = QImage(320, 240, QImage::Format_RGB32);
    frame->info.width = 320;
    frame->info.height = 240;
    frame->info.sequence = 7;

    QSignalSpy spy(&window.ai(), &AiController::predictionReady);
    REQUIRE(window.ai().on_frame(frame));
    REQUIRE(spy.wait(30000));
    const auto prediction = spy.takeFirst().at(0).value<std::shared_ptr<const ai::Prediction>>();
    REQUIRE(prediction != nullptr);
    CHECK(prediction->frame.sequence == 7);
    CHECK_FALSE(prediction->geometry_valid);  // no calibration file in the fixture: zeros, as the contract allows
    CHECK(window.ai_panel().genus_label()->text() != QStringLiteral("–"));
    CHECK(window.status_bar().ai_label()->text().contains(QStringLiteral("ms")));
    CHECK(window.live_view().scene().prediction != nullptr);

    // pausing clears the live view's result and leaves the model loaded
    window.ai_panel().run_checkbox()->setChecked(false);
    CHECK_FALSE(window.ai().running());
    CHECK(window.live_view().scene().prediction == nullptr);
    CHECK(window.status_bar().ai_label()->text().contains(QStringLiteral("paused")));
    CHECK_FALSE(window.ai().on_frame(frame));
}
