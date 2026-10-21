#include "ai_panel.hpp"

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace cloudscope::desktop {

namespace {

QProgressBar* make_bar(const QString& accessible, QWidget* parent)
{
    auto* bar = new QProgressBar(parent);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(true);
    bar->setFormat(QStringLiteral("%p %"));
    bar->setAccessibleName(accessible);
    bar->setMaximumHeight(16);
    return bar;
}

QString percent(float probability)
{
    return QString::number(static_cast<int>(std::lround(probability * 100.0F)));
}

}  // namespace

AiPanel::AiPanel(QWidget* parent) : QWidget(parent)
{
    setAccessibleName(tr("STRATIA panel"));
    auto* layout = new QVBoxLayout(this);

    auto* model_box = new QGroupBox(tr("Model"), this);
    auto* model_form = new QFormLayout(model_box);
    model_ = new QLabel(tr("none loaded"), model_box);
    model_->setAccessibleName(tr("Loaded model"));
    provider_ = new QLabel(tr("–"), model_box);
    provider_->setAccessibleName(tr("Execution provider"));
    timing_ = new QLabel(tr("–"), model_box);
    timing_->setAccessibleName(tr("Inference timing"));
    message_ = new QLabel(QString(), model_box);
    message_->setWordWrap(true);
    message_->setAccessibleName(tr("Model message"));
    model_form->addRow(tr("Model:"), model_);
    model_form->addRow(tr("Runs on:"), provider_);
    model_form->addRow(tr("Timing:"), timing_);
    model_form->addRow(message_);
    auto* buttons = new QHBoxLayout();
    load_ = new QPushButton(tr("Load model…"), model_box);
    load_->setToolTip(tr("Choose a model package: a folder with model_card.json and one .onnx file"));
    unload_ = new QPushButton(tr("Unload"), model_box);
    unload_->setEnabled(false);
    benchmark_ = new QPushButton(tr("Benchmark…"), model_box);
    benchmark_->setToolTip(tr("Time the loaded model on every available execution provider (P070)"));
    benchmark_->setEnabled(false);
    buttons->addWidget(load_);
    buttons->addWidget(unload_);
    buttons->addWidget(benchmark_);
    model_form->addRow(buttons);
    run_ = new QCheckBox(tr("Run on live frames"), model_box);
    run_->setChecked(true);
    run_->setToolTip(tr("Send the newest preview frame to the model at the configured rate (FR-AI-03)"));
    model_form->addRow(run_);
    layout->addWidget(model_box);

    auto* overlay_box = new QGroupBox(tr("Overlay"), this);
    auto* overlay_form = new QFormLayout(overlay_box);
    masks_ = new QCheckBox(tr("Sky, cloud and glare masks"), overlay_box);
    masks_->setChecked(true);
    layers_ = new QCheckBox(tr("Cloud layers (low, mid, high)"), overlay_box);
    badge_ = new QCheckBox(tr("Headline badge on the live view"), overlay_box);
    badge_->setChecked(true);
    show_uncertain_ = new QCheckBox(tr("Show masks of uncertain results too"), overlay_box);
    show_uncertain_->setToolTip(tr("By default an uncertain result shows only the badge (FR-AI-06)"));
    opacity_ = new QSlider(Qt::Horizontal, overlay_box);
    opacity_->setRange(5, 100);
    opacity_->setValue(40);
    opacity_->setAccessibleName(tr("Mask opacity"));
    overlay_form->addRow(masks_);
    overlay_form->addRow(layers_);
    overlay_form->addRow(badge_);
    overlay_form->addRow(show_uncertain_);
    overlay_form->addRow(tr("Opacity:"), opacity_);
    layout->addWidget(overlay_box);

    auto* result_box = new QGroupBox(tr("Latest result"), this);
    auto* result_form = new QFormLayout(result_box);
    frame_ = new QLabel(tr("no result yet"), result_box);
    frame_->setAccessibleName(tr("Result frame"));
    result_form->addRow(tr("Frame:"), frame_);
    reliability_ = new QLabel(tr("–"), result_box);
    reliability_->setAccessibleName(tr("Reliability"));
    result_form->addRow(tr("Reliability:"), reliability_);
    genus_ = new QLabel(tr("–"), result_box);
    genus_->setAccessibleName(tr("Cloud genus"));
    result_form->addRow(tr("Genus:"), genus_);
    for (std::size_t i = 0; i < genus_bars_.size(); ++i) {
        genus_names_[i] = new QLabel(QStringLiteral("–"), result_box);
        genus_bars_[i] = make_bar(tr("Genus candidate %1").arg(i + 1), result_box);
        result_form->addRow(genus_names_[i], genus_bars_[i]);
    }
    const std::array<QString, 3> etage_labels{tr("Low:"), tr("Mid:"), tr("High:")};
    for (std::size_t i = 0; i < etage_bars_.size(); ++i) {
        etage_bars_[i] = make_bar(tr("%1 cloud present").arg(etage_labels[i]), result_box);
        result_form->addRow(etage_labels[i], etage_bars_[i]);
    }
    cover_ = new QLabel(tr("–"), result_box);
    cover_->setAccessibleName(tr("Cloud cover"));
    cover_bar_ = make_bar(tr("Cloud cover"), result_box);
    result_form->addRow(tr("Cover:"), cover_);
    result_form->addRow(QString(), cover_bar_);
    cbh_ = new QLabel(tr("–"), result_box);
    cbh_->setAccessibleName(tr("Cloud-base height"));
    cbh_->setWordWrap(true);
    result_form->addRow(tr("Cloud base:"), cbh_);
    layout->addWidget(result_box);
    layout->addStretch(1);

    connect(load_, &QPushButton::clicked, this, &AiPanel::choose_model);
    connect(unload_, &QPushButton::clicked, this, &AiPanel::unloadRequested);
    connect(benchmark_, &QPushButton::clicked, this, &AiPanel::benchmarkRequested);
    connect(run_, &QCheckBox::toggled, this, &AiPanel::runningChanged);
    for (QCheckBox* box : {masks_, layers_, badge_, show_uncertain_}) {
        connect(box, &QCheckBox::toggled, this, &AiPanel::emit_overlay_settings);
    }
    connect(opacity_, &QSlider::valueChanged, this, &AiPanel::emit_overlay_settings);
}

void AiPanel::set_status(const ai::ServiceStatus& status, const ai::ServiceStats& stats)
{
    if (status.loaded) {
        model_->setText(QStringLiteral("%1 %2").arg(QString::fromStdString(status.model_name),
                                                    QString::fromStdString(status.model_version)));
        provider_->setText(tr("%1 (%2), at most %3 per second")
                               .arg(QString::fromStdString(status.provider), QString::fromStdString(status.engine_id))
                               .arg(status.rate_hz));
        timing_->setText(tr("%1 ms last, %2 ms mean; %3 run, %4 dropped, %5 failed")
                             .arg(stats.last_run_ms, 0, 'f', 0)
                             .arg(stats.mean_run_ms, 0, 'f', 0)
                             .arg(stats.run)
                             .arg(stats.dropped)
                             .arg(stats.failed));
    } else {
        model_->setText(tr("none loaded"));
        provider_->setText(tr("–"));
        timing_->setText(tr("–"));
    }
    message_->setText(QString::fromStdString(status.message));
    message_->setVisible(!status.message.empty());
    unload_->setEnabled(status.loaded);
    benchmark_->setEnabled(status.loaded);
}

void AiPanel::show_prediction(const std::shared_ptr<const ai::Prediction>& prediction)
{
    if (!prediction) {
        clear_prediction();
        return;
    }
    const ai::Prediction& p = *prediction;
    frame_->setText(tr("#%1, %2 × %3, %4 ms (%5 ms in the model)")
                        .arg(p.frame.sequence)
                        .arg(p.frame.width)
                        .arg(p.frame.height)
                        .arg(p.total_ms, 0, 'f', 0)
                        .arg(p.run_ms, 0, 'f', 0));
    const QString reliability = p.uncertain
                                    ? (p.night ? tr("UNCERTAIN: night frame")
                                               : tr("UNCERTAIN: out of domain (score %1)").arg(p.ood_score, 0, 'f', 2))
                                    : tr("reliable (score %1)").arg(p.ood_score, 0, 'f', 2);
    reliability_->setText(reliability);
    reliability_->setStyleSheet(p.uncertain ? QStringLiteral("color: %1; font-weight: bold").arg(warning_.name())
                                            : QStringLiteral("color: %1").arg(text_.name()));
    std::array<int, ai::kGenusClasses.size()> order{};
    std::iota(order.begin(), order.end(), 0);
    std::ranges::sort(order, [&](int a, int b) {
        return p.genus_probs[static_cast<std::size_t>(a)] > p.genus_probs[static_cast<std::size_t>(b)];
    });
    genus_->setText(
        tr("%1 (%2 %)").arg(genus_name(p.genus_top), percent(p.genus_probs[static_cast<std::size_t>(p.genus_top)])));
    for (std::size_t i = 0; i < genus_bars_.size(); ++i) {
        const int index = order[i];
        genus_names_[i]->setText(genus_name(index));
        genus_bars_[i]->setValue(
            static_cast<int>(std::lround(p.genus_probs[static_cast<std::size_t>(index)] * 100.0F)));
    }
    for (std::size_t i = 0; i < etage_bars_.size(); ++i) {
        etage_bars_[i]->setValue(static_cast<int>(std::lround(p.etage_probs[i] * 100.0F)));
    }
    cover_->setText(tr("%1 okta (expected %2 %)").arg(p.oktas).arg(p.cover_fraction * 100.0, 0, 'f', 0));
    cover_bar_->setValue(static_cast<int>(std::lround(p.cover_fraction * 100.0)));
    if (p.cbh.has_value()) {
        cbh_->setText(tr("%1 (80 %: %2 – %3; 95 %: %4 – %5); p(cloud overhead) %6 %")
                          .arg(height_text(p.cbh->median_m), height_text(p.cbh->low_80_m),
                               height_text(p.cbh->high_80_m), height_text(p.cbh->low_95_m),
                               height_text(p.cbh->high_95_m))
                          .arg(p.p_cloud_overhead * 100.0, 0, 'f', 0));
    } else {
        cbh_->setText(tr("no cloud overhead (p %1 %)").arg(p.p_cloud_overhead * 100.0, 0, 'f', 0));
    }
}

void AiPanel::clear_prediction()
{
    frame_->setText(tr("no result yet"));
    reliability_->setText(tr("–"));
    reliability_->setStyleSheet(QString());
    genus_->setText(tr("–"));
    for (std::size_t i = 0; i < genus_bars_.size(); ++i) {
        genus_names_[i]->setText(QStringLiteral("–"));
        genus_bars_[i]->setValue(0);
    }
    for (QProgressBar* bar : etage_bars_) {
        bar->setValue(0);
    }
    cover_->setText(tr("–"));
    cover_bar_->setValue(0);
    cbh_->setText(tr("–"));
}

void AiPanel::set_colours(QColor text, QColor warning)
{
    text_ = text;
    warning_ = warning;
}

AiOverlaySettings AiPanel::overlay_settings() const
{
    return {.masks = masks_->isChecked(),
            .layers = layers_->isChecked(),
            .badge = badge_->isChecked(),
            .opacity_percent = opacity_->value(),
            .show_when_uncertain = show_uncertain_->isChecked()};
}

void AiPanel::set_overlay_settings(const AiOverlaySettings& settings)
{
    for (QObject* o : {static_cast<QObject*>(masks_), static_cast<QObject*>(layers_), static_cast<QObject*>(badge_),
                       static_cast<QObject*>(show_uncertain_), static_cast<QObject*>(opacity_)}) {
        o->blockSignals(true);
    }
    masks_->setChecked(settings.masks);
    layers_->setChecked(settings.layers);
    badge_->setChecked(settings.badge);
    show_uncertain_->setChecked(settings.show_when_uncertain);
    opacity_->setValue(settings.opacity_percent);
    for (QObject* o : {static_cast<QObject*>(masks_), static_cast<QObject*>(layers_), static_cast<QObject*>(badge_),
                       static_cast<QObject*>(show_uncertain_), static_cast<QObject*>(opacity_)}) {
        o->blockSignals(false);
    }
}

bool AiPanel::running() const
{
    return run_->isChecked();
}

void AiPanel::set_running(bool running)
{
    run_->setChecked(running);
}

void AiPanel::emit_overlay_settings()
{
    Q_EMIT overlaySettingsChanged(overlay_settings());
}

void AiPanel::choose_model()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose a model package folder"));
    if (!folder.isEmpty()) {
        Q_EMIT loadRequested(folder);
    }
}

}  // namespace cloudscope::desktop
