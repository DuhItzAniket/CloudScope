// The STRATIA panel (P068, FR-AI-05): what the model says about the latest frame, the engine in use, and the
// overlay switches. Every number carries its uncertainty: genus and étage with their probabilities, oktas with
// the expected cover, the cloud base with its 80 % interval, and the reliability score against its threshold.

#pragma once

#include "ai_overlay.hpp"

#include <cloudscope/ai/inference_service.hpp>

#include <QtWidgets/QWidget>

#include <array>
#include <memory>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;

namespace cloudscope::desktop {

class AiPanel : public QWidget {
    Q_OBJECT
public:
    explicit AiPanel(QWidget* parent = nullptr);

    void set_status(const ai::ServiceStatus& status, const ai::ServiceStats& stats);
    void show_prediction(const std::shared_ptr<const ai::Prediction>& prediction);
    void clear_prediction();
    void set_colours(QColor text, QColor warning);

    [[nodiscard]] AiOverlaySettings overlay_settings() const;
    void set_overlay_settings(const AiOverlaySettings& settings);  // updates the controls without emitting
    [[nodiscard]] bool running() const;
    void set_running(bool running);

    // For the tests
    [[nodiscard]] QLabel* model_label() const { return model_; }
    [[nodiscard]] QLabel* provider_label() const { return provider_; }
    [[nodiscard]] QLabel* genus_label() const { return genus_; }
    [[nodiscard]] QLabel* cover_label() const { return cover_; }
    [[nodiscard]] QLabel* cbh_label() const { return cbh_; }
    [[nodiscard]] QLabel* reliability_label() const { return reliability_; }
    [[nodiscard]] QCheckBox* run_checkbox() const { return run_; }
    [[nodiscard]] QCheckBox* masks_checkbox() const { return masks_; }
    [[nodiscard]] QPushButton* load_button() const { return load_; }
    [[nodiscard]] QPushButton* benchmark_button() const { return benchmark_; }

Q_SIGNALS:
    void loadRequested(const QString& folder);
    void unloadRequested();
    void runningChanged(bool running);
    void overlaySettingsChanged(const AiOverlaySettings& settings);
    void benchmarkRequested();

private:
    void emit_overlay_settings();
    void choose_model();

    QLabel* model_ = nullptr;
    QLabel* provider_ = nullptr;
    QLabel* timing_ = nullptr;
    QLabel* message_ = nullptr;
    QPushButton* load_ = nullptr;
    QPushButton* unload_ = nullptr;
    QPushButton* benchmark_ = nullptr;
    QCheckBox* run_ = nullptr;
    QCheckBox* masks_ = nullptr;
    QCheckBox* layers_ = nullptr;
    QCheckBox* badge_ = nullptr;
    QCheckBox* show_uncertain_ = nullptr;
    QSlider* opacity_ = nullptr;
    QLabel* genus_ = nullptr;
    std::array<QProgressBar*, 3> genus_bars_{};
    std::array<QLabel*, 3> genus_names_{};
    std::array<QProgressBar*, 3> etage_bars_{};
    QLabel* cover_ = nullptr;
    QProgressBar* cover_bar_ = nullptr;
    QLabel* cbh_ = nullptr;
    QLabel* reliability_ = nullptr;
    QLabel* frame_ = nullptr;
    QColor text_{230, 230, 230};
    QColor warning_{255, 140, 40};
};

}  // namespace cloudscope::desktop
