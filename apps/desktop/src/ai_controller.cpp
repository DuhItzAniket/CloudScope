#include "ai_controller.hpp"

#include "app_context.hpp"

#include <cloudscope/common/log.hpp>

#include <cmath>

namespace cloudscope::desktop {

namespace {

constexpr double kSunMoveDeg = 0.05;  // the ray map is recomputed when the Sun moved by more than this

}  // namespace

AiController::AiController(AppContext& context, QObject* parent) : QObject(parent), context_(context), ui_(this)
{
    const auto settings = ai::ai_settings(context_.config().effective);
    if (settings) {
        settings_ = *settings;
    } else {
        logger("desktop").warn("AI settings not read: {}", settings.error().to_string());
    }
    service_.set_callback([this](std::shared_ptr<const ai::Prediction> prediction) {
        ui_.post([this, prediction = std::move(prediction)] { Q_EMIT predictionReady(prediction); });
    });
}

AiController::~AiController()
{
    service_.set_callback({});
}

Expected<void> AiController::load_configured()
{
    if (settings_.model_dir.empty()) {
        return {};
    }
    return load(settings_.model_dir);
}

Expected<void> AiController::load(const std::filesystem::path& package)
{
    auto loaded = service_.load(package, context_.registry(), settings_);
    Q_EMIT statusChanged();
    return loaded;
}

void AiController::unload()
{
    service_.unload();
    Q_EMIT statusChanged();
}

void AiController::set_projection(std::shared_ptr<const SkyProjection> projection)
{
    projection_ = std::move(projection);
    geometry_dirty_ = true;
}

void AiController::set_sun(std::optional<SunPosition> sun)
{
    sun_ = sun;
    if (!sun_ || !geometry_sun_) {
        geometry_dirty_ = true;
        return;
    }
    if (std::abs(sun_->azimuth_deg - geometry_sun_->azimuth_deg) > kSunMoveDeg ||
        std::abs(sun_->apparent_elevation_deg - geometry_sun_->apparent_elevation_deg) > kSunMoveDeg) {
        geometry_dirty_ = true;
    }
}

void AiController::refresh_geometry()
{
    const auto card = service_.card();
    ai::RayMapSpec spec;
    if (card) {
        spec.height = card->input.height;
        spec.width = card->input.width;
        spec.patch = card->input.patch_size;
    }
    const CameraModel* model = projection_ ? &projection_->model() : nullptr;
    const Quaternion* pose = projection_ ? &projection_->camera_to_enu() : nullptr;
    const SunPosition* sun = sun_ ? &*sun_ : nullptr;
    geometry_ = ai::geometry_inputs(model, pose, sun, spec);
    geometry_sun_ = sun_;
    geometry_dirty_ = false;
}

bool AiController::on_frame(const std::shared_ptr<const PreviewFrame>& frame)
{
    if (!running_ || !frame || !frame->picture || frame->picture->empty() || !service_.status().loaded) {
        return false;
    }
    if (geometry_dirty_) {
        refresh_geometry();
    }
    return service_.submit(frame->picture, frame->info, geometry_);
}

}  // namespace cloudscope::desktop
