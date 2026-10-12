#include "histogram_panel.hpp"

#include <QtGui/QPainter>
#include <QtGui/QPen>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace cloudscope::desktop {

namespace {

constexpr int kLevels = 256;
constexpr int kSliderSteps = 1000;

double bar_height(std::uint32_t count, std::uint32_t peak, bool logarithmic)
{
    if (peak == 0 || count == 0) {
        return 0.0;
    }
    if (logarithmic) {
        return std::log1p(static_cast<double>(count)) / std::log1p(static_cast<double>(peak));
    }
    return static_cast<double>(count) / static_cast<double>(peak);
}

}  // namespace

HistogramWidget::HistogramWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(80);
    setAccessibleName(QStringLiteral("Histogram"));
    setToolTip(tr("Luma histogram with the blue, green and red channels; the bars at the ends show clipped pixels"));
}

QSize HistogramWidget::sizeHint() const
{
    return {256, 120};
}

void HistogramWidget::set_histograms(const Histogram& luma, const ChannelHistograms& channels)
{
    luma_ = luma;
    channels_ = channels;
    clipping_ = clipping_fractions(luma);
    update();
}

void HistogramWidget::set_logarithmic(bool logarithmic)
{
    logarithmic_ = logarithmic;
    update();
}

void HistogramWidget::set_colours(QColor foreground, QColor warning)
{
    foreground_ = foreground;
    warning_ = warning;
    update();
}

void HistogramWidget::paint(QPainter& painter, const QRectF& area) const
{
    painter.save();
    painter.fillRect(area, QColor(0, 0, 0));
    const double margin = 4.0;
    const QRectF plot = area.adjusted(margin, margin, -margin, -margin - 12.0);
    if (plot.width() <= 0.0 || plot.height() <= 0.0) {
        painter.restore();
        return;
    }
    const std::uint32_t luma_peak = *std::ranges::max_element(luma_);
    std::uint32_t channel_peak = 0;
    for (const Histogram& channel : channels_) {
        channel_peak = std::max(channel_peak, *std::ranges::max_element(channel));
    }
    const double bin_width = plot.width() / kLevels;
    // Channels as translucent filled curves, then the luma as a line on top.
    const std::array<QColor, 3> channel_colours = {QColor(80, 120, 255, 90), QColor(80, 220, 80, 90),
                                                   QColor(255, 80, 80, 90)};
    for (std::size_t c = 0; c < 3; ++c) {
        QPolygonF polygon;
        polygon.append(QPointF(plot.left(), plot.bottom()));
        for (int level = 0; level < kLevels; ++level) {
            const double h = bar_height(channels_[c][static_cast<std::size_t>(level)], channel_peak, logarithmic_);
            polygon.append(QPointF(plot.left() + (level + 0.5) * bin_width, plot.bottom() - h * plot.height()));
        }
        polygon.append(QPointF(plot.right(), plot.bottom()));
        painter.setPen(Qt::NoPen);
        painter.setBrush(channel_colours[c]);
        painter.drawPolygon(polygon);
    }
    QPolygonF luma_line;
    for (int level = 0; level < kLevels; ++level) {
        const double h = bar_height(luma_[static_cast<std::size_t>(level)], luma_peak, logarithmic_);
        luma_line.append(QPointF(plot.left() + (level + 0.5) * bin_width, plot.bottom() - h * plot.height()));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(foreground_, 1.0));
    painter.drawPolyline(luma_line);
    // Clipping indicators: a bar at each end whose height is the clipped share (full height = 10 % or more).
    const double indicator_scale = 10.0;
    const double black_h = std::min(clipping_.black * indicator_scale, 1.0) * plot.height();
    const double white_h = std::min(clipping_.white * indicator_scale, 1.0) * plot.height();
    painter.setPen(Qt::NoPen);
    painter.setBrush(warning_);
    if (black_h > 0.0) {
        painter.drawRect(QRectF(plot.left(), plot.bottom() - black_h, 3.0, black_h));
    }
    if (white_h > 0.0) {
        painter.drawRect(QRectF(plot.right() - 3.0, plot.bottom() - white_h, 3.0, white_h));
    }
    painter.setPen(foreground_);
    painter.drawText(QRectF(plot.left(), plot.bottom(), plot.width(), 12.0), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("0  (%1 %)").arg(clipping_.black * 100.0, 0, 'f', 2));
    painter.drawText(QRectF(plot.left(), plot.bottom(), plot.width(), 12.0), Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("(%1 %)  255").arg(clipping_.white * 100.0, 0, 'f', 2));
    painter.drawText(QRectF(plot.left(), plot.bottom(), plot.width(), 12.0), Qt::AlignHCenter | Qt::AlignVCenter,
                     logarithmic_ ? QStringLiteral("log") : QStringLiteral("linear"));
    painter.restore();
}

void HistogramWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    paint(painter, QRectF(rect()));
}

ParadeWidget::ParadeWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(64);
    setAccessibleName(QStringLiteral("RGB parade"));
    setToolTip(tr("RGB parade: for every column of the picture, the distribution of each channel's values"));
}

QSize ParadeWidget::sizeHint() const
{
    return {256, 96};
}

void ParadeWidget::set_parade(const std::array<cv::Mat, 3>& parade)
{
    for (std::size_t c = 0; c < 3; ++c) {
        const cv::Mat& channel = parade[c];
        if (channel.empty() || channel.type() != CV_32FC1) {
            images_[c] = QImage();
            continue;
        }
        QImage image(channel.cols, channel.rows, QImage::Format_Grayscale8);
        float peak = 0.0F;
        for (int y = 0; y < channel.rows; ++y) {
            const auto* row = channel.ptr<float>(y);
            for (int x = 0; x < channel.cols; ++x) {
                peak = std::max(peak, row[x]);
            }
        }
        const float scale = peak > 0.0F ? 255.0F / peak : 0.0F;
        for (int y = 0; y < channel.rows; ++y) {
            // Row 0 of the parade is the darkest bin; the image's top row shows the brightest.
            const auto* row = channel.ptr<float>(channel.rows - 1 - y);
            uchar* out = image.scanLine(y);
            for (int x = 0; x < channel.cols; ++x) {
                out[x] = static_cast<uchar>(std::clamp(std::lround(row[x] * scale), 0L, 255L));
            }
        }
        images_[c] = image;
    }
    update();
}

void ParadeWidget::paint(QPainter& painter, const QRectF& area) const
{
    painter.save();
    painter.fillRect(area, QColor(0, 0, 0));
    const double gap = 2.0;
    const double width = (area.width() - 2.0 * gap) / 3.0;
    const std::array<QColor, 3> tints = {QColor(90, 130, 255), QColor(90, 220, 90), QColor(255, 90, 90)};
    // Draw as red, green, blue from left to right, the order scopes use.
    const std::array<std::size_t, 3> order = {2, 1, 0};
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const std::size_t c = order[slot];
        const QRectF target(area.left() + static_cast<double>(slot) * (width + gap), area.top(), width, area.height());
        if (images_[c].isNull()) {
            continue;
        }
        painter.drawImage(target, images_[c]);
        painter.setCompositionMode(QPainter::CompositionMode_Multiply);
        painter.fillRect(target, tints[c]);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    painter.restore();
}

void ParadeWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    paint(painter, QRectF(rect()));
}

HistogramPanel::HistogramPanel(QWidget* parent)
    : QWidget(parent),
      histogram_(new HistogramWidget(this)),
      parade_(new ParadeWidget(this)),
      clipping_label_(new QLabel(this))
{
    setAccessibleName(QStringLiteral("Histogram and stretch panel"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(histogram_, 2);
    parade_->setVisible(false);
    layout->addWidget(parade_, 1);

    auto* scope_row = new QHBoxLayout();
    log_scale_ = new QCheckBox(tr("Logarithmic"), this);
    log_scale_->setToolTip(tr("Logarithmic vertical scale of the histogram"));
    show_parade_ = new QCheckBox(tr("RGB parade"), this);
    show_parade_->setToolTip(tr("Show the RGB parade below the histogram"));
    clipping_label_->setAccessibleName(QStringLiteral("Clipping"));
    scope_row->addWidget(log_scale_);
    scope_row->addWidget(show_parade_);
    scope_row->addStretch(1);
    scope_row->addWidget(clipping_label_);
    layout->addLayout(scope_row);

    auto* stretch_box = new QGroupBox(tr("Display stretch (preview only; recorded files are never changed)"), this);
    auto* form = new QFormLayout(stretch_box);
    enabled_ = new QCheckBox(tr("Enabled"), stretch_box);
    enabled_->setToolTip(tr("Stretch the preview's levels; the camera's frames and recordings stay as they are"));
    automatic_ = new QCheckBox(tr("Automatic levels"), stretch_box);
    automatic_->setToolTip(tr("Black and white points from the 0.1 % and 99.9 % percentiles of the luma"));
    black_ = new QSlider(Qt::Horizontal, stretch_box);
    black_->setRange(0, kSliderSteps);
    black_->setAccessibleName(QStringLiteral("Black point"));
    black_->setToolTip(tr("Input level shown as black (fraction of the range)"));
    white_ = new QSlider(Qt::Horizontal, stretch_box);
    white_->setRange(0, kSliderSteps);
    white_->setValue(kSliderSteps);
    white_->setAccessibleName(QStringLiteral("White point"));
    white_->setToolTip(tr("Input level shown as white (fraction of the range)"));
    gamma_ = new QDoubleSpinBox(stretch_box);
    gamma_->setRange(0.1, 10.0);
    gamma_->setSingleStep(0.1);
    gamma_->setValue(1.0);
    gamma_->setAccessibleName(QStringLiteral("Gamma"));
    gamma_->setToolTip(tr("Gamma of the stretch: above 1 brightens the mid-tones"));
    highlight_ = new QCheckBox(tr("Highlight over-exposed pixels"), stretch_box);
    highlight_->setToolTip(tr("Paint pixels at or above the clipping level in magenta (preview only)"));
    applied_label_ = new QLabel(stretch_box);
    applied_label_->setAccessibleName(QStringLiteral("Stretch in effect"));
    form->addRow(enabled_);
    form->addRow(automatic_);
    form->addRow(tr("Black"), black_);
    form->addRow(tr("White"), white_);
    form->addRow(tr("Gamma"), gamma_);
    form->addRow(highlight_);
    form->addRow(applied_label_);
    layout->addWidget(stretch_box);

    connect(log_scale_, &QCheckBox::toggled, histogram_, &HistogramWidget::set_logarithmic);
    connect(show_parade_, &QCheckBox::toggled, this, [this](bool on) {
        parade_->setVisible(on);
        Q_EMIT paradeWantedChanged(on);
    });
    for (QCheckBox* box : {enabled_, automatic_, highlight_}) {
        connect(box, &QCheckBox::toggled, this, [this] { read_controls(); });
    }
    connect(black_, &QSlider::valueChanged, this, [this] { read_controls(); });
    connect(white_, &QSlider::valueChanged, this, [this] { read_controls(); });
    connect(gamma_, &QDoubleSpinBox::valueChanged, this, [this] { read_controls(); });
    write_controls();
}

bool HistogramPanel::parade_wanted() const
{
    return show_parade_->isChecked();
}

void HistogramPanel::set_colours(QColor foreground, QColor warning)
{
    histogram_->set_colours(foreground, warning);
}

void HistogramPanel::show_frame(const std::shared_ptr<const PreviewFrame>& frame)
{
    if (!frame) {
        return;
    }
    if (frame->statistics) {
        histogram_->set_histograms(frame->statistics->histogram, frame->channels);
        const ClippingFractions clipping = histogram_->clipping();
        clipping_label_->setText(tr("Clipped: %1 % black, %2 % white")
                                     .arg(clipping.black * 100.0, 0, 'f', 2)
                                     .arg(clipping.white * 100.0, 0, 'f', 2));
    }
    if (parade_->isVisible() && !frame->parade[0].empty()) {
        parade_->set_parade(frame->parade);
    }
    if (stretch_.enabled) {
        const Lut& lut = frame->lut;
        int black = 0;
        while (black < kLevels - 1 && lut[static_cast<std::size_t>(black)] == 0) {
            ++black;
        }
        int white = kLevels - 1;
        while (white > 0 && lut[static_cast<std::size_t>(white)] == kLevels - 1) {
            --white;
        }
        applied_label_->setText(tr("In effect: black %1, white %2, gamma %3%4")
                                    .arg(black)
                                    .arg(white)
                                    .arg(stretch_.gamma, 0, 'f', 2)
                                    .arg(stretch_.highlight_overexposed
                                             ? tr("; %1 over-exposed pixels").arg(frame->overexposed_pixels)
                                             : QString()));
    } else {
        applied_label_->setText(stretch_.highlight_overexposed
                                    ? tr("In effect: no stretch; %1 over-exposed pixels").arg(frame->overexposed_pixels)
                                    : tr("In effect: no stretch"));
    }
}

void HistogramPanel::set_stretch(const StretchSettings& settings)
{
    stretch_ = settings;
    write_controls();
}

void HistogramPanel::read_controls()
{
    if (updating_) {
        return;
    }
    stretch_.enabled = enabled_->isChecked();
    stretch_.automatic = automatic_->isChecked();
    stretch_.black = static_cast<double>(black_->value()) / kSliderSteps;
    stretch_.white = static_cast<double>(white_->value()) / kSliderSteps;
    if (stretch_.white <= stretch_.black) {
        stretch_.white = std::min(stretch_.black + 1.0 / kSliderSteps, 1.0);
    }
    stretch_.gamma = gamma_->value();
    stretch_.highlight_overexposed = highlight_->isChecked();
    const bool manual = stretch_.enabled && !stretch_.automatic;
    black_->setEnabled(manual);
    white_->setEnabled(manual);
    gamma_->setEnabled(stretch_.enabled);
    automatic_->setEnabled(stretch_.enabled);
    Q_EMIT stretchChanged(stretch_);
}

void HistogramPanel::write_controls()
{
    updating_ = true;
    enabled_->setChecked(stretch_.enabled);
    automatic_->setChecked(stretch_.automatic);
    black_->setValue(static_cast<int>(std::lround(stretch_.black * kSliderSteps)));
    white_->setValue(static_cast<int>(std::lround(stretch_.white * kSliderSteps)));
    gamma_->setValue(stretch_.gamma);
    highlight_->setChecked(stretch_.highlight_overexposed);
    const bool manual = stretch_.enabled && !stretch_.automatic;
    black_->setEnabled(manual);
    white_->setEnabled(manual);
    gamma_->setEnabled(stretch_.enabled);
    automatic_->setEnabled(stretch_.enabled);
    updating_ = false;
}

}  // namespace cloudscope::desktop
