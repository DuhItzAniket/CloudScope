// The desktop's STRATIA controller (P066–P068): owns the inference service, feeds it the newest preview frame
// with the frame's geometry (the sky projection of the overlays and the Sun, P067), and hands every result to the
// UI thread. The service runs on its own thread; nothing here blocks the UI (FR-AI-03).

#pragma once

#include "overlay.hpp"
#include "preview_pipeline.hpp"

#include <cloudscope/ai/geometry_inputs.hpp>
#include <cloudscope/ai/inference_service.hpp>
#include <cloudscope/ai/settings.hpp>
#include <cloudscope/common/executor.hpp>

#include <QtCore/QObject>

#include <filesystem>
#include <memory>
#include <optional>

namespace cloudscope::desktop {

class AppContext;

class AiController : public QObject {
    Q_OBJECT
public:
    explicit AiController(AppContext& context, QObject* parent = nullptr);
    ~AiController() override;
    AiController(const AiController&) = delete;
    AiController& operator=(const AiController&) = delete;
    AiController(AiController&&) = delete;
    AiController& operator=(AiController&&) = delete;

    // Loads the package named in the configuration ([ai] model_dir); nothing to do when it is empty.
    [[nodiscard]] Expected<void> load_configured();
    [[nodiscard]] Expected<void> load(const std::filesystem::path& package);
    void unload();

    [[nodiscard]] ai::InferenceService& service() { return service_; }
    [[nodiscard]] ai::ServiceStatus status() const { return service_.status(); }
    [[nodiscard]] ai::ServiceStats stats() const { return service_.stats(); }
    [[nodiscard]] const ai::AiSettings& settings() const { return settings_; }
    [[nodiscard]] bool running() const { return running_; }
    void set_running(bool running) { running_ = running; }

    void set_projection(std::shared_ptr<const SkyProjection> projection);
    void set_sun(std::optional<SunPosition> sun);
    [[nodiscard]] const ai::GeometryInputs& geometry() const { return geometry_; }

    // Hands the frame to the service when running and a model is loaded. Returns true when it was submitted.
    bool on_frame(const std::shared_ptr<const PreviewFrame>& frame);

Q_SIGNALS:
    void predictionReady(std::shared_ptr<const ai::Prediction> prediction);
    void statusChanged();

private:
    void refresh_geometry();

    AppContext& context_;
    ai::AiSettings settings_;
    ai::InferenceService service_;
    QtExecutor ui_;
    std::shared_ptr<const SkyProjection> projection_;
    std::optional<SunPosition> sun_;
    ai::GeometryInputs geometry_;
    std::optional<SunPosition> geometry_sun_;  // the Sun the ray map was computed for
    bool geometry_dirty_ = true;
    bool running_ = true;
};

}  // namespace cloudscope::desktop
