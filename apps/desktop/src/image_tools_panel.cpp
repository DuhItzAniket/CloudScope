#include "image_tools_panel.hpp"

#include <QtGui/QPainter>
#include <QtGui/QPen>
#include <QtGui/QPixmap>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

namespace cloudscope::desktop {

namespace {

constexpr int kLoupeRadius = 8;  // 17 x 17 source pixels

}  // namespace

SparklineWidget::SparklineWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(40);
    setAccessibleName(QStringLiteral("Focus history"));
    setToolTip(tr("Sharpness over the last frames; the line marks the best value seen"));
}

QSize SparklineWidget::sizeHint() const
{
    return {200, 60};
}

void SparklineWidget::push(double value)
{
    values_.push_back(value);
    while (values_.size() > capacity_) {
        values_.pop_front();
    }
    best_ = std::max(best_, value);
    update();
}

void SparklineWidget::clear()
{
    values_.clear();
    best_ = 0.0;
    update();
}

void SparklineWidget::set_colours(QColor line, QColor best)
{
    line_ = line;
    best_colour_ = best;
    update();
}

void SparklineWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0));
    if (values_.size() < 2 || best_ <= 0.0) {
        return;
    }
    const QRectF plot = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);
    QPolygonF line;
    const double dx = plot.width() / static_cast<double>(capacity_ - 1);
    const double start = plot.right() - dx * static_cast<double>(values_.size() - 1);
    for (std::size_t i = 0; i < values_.size(); ++i) {
        const double y = plot.bottom() - std::clamp(values_[i] / best_, 0.0, 1.0) * plot.height();
        line.append(QPointF(start + dx * static_cast<double>(i), y));
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(best_colour_, 1.0, Qt::DashLine));
    painter.drawLine(QPointF(plot.left(), plot.top()), QPointF(plot.right(), plot.top()));
    painter.setPen(QPen(line_, 1.5));
    painter.drawPolyline(line);
}

ImageToolsPanel::ImageToolsPanel(QWidget* parent) : QWidget(parent)
{
    setAccessibleName(QStringLiteral("Image tools panel"));
    auto* layout = new QVBoxLayout(this);

    auto* focus_box = new QGroupBox(tr("Focus aid (variance of the Laplacian)"), this);
    auto* focus_form = new QFormLayout(focus_box);
    focus_enabled_ = new QCheckBox(tr("Measure every frame"), focus_box);
    focus_enabled_->setChecked(true);
    focus_enabled_->setToolTip(tr("Sharpness of the region of interest, or of the whole picture; higher is sharper"));
    focus_label_ = new QLabel(QStringLiteral("—"), focus_box);
    focus_label_->setAccessibleName(QStringLiteral("Focus value"));
    focus_history_ = new SparklineWidget(focus_box);
    focus_form->addRow(focus_enabled_);
    focus_form->addRow(tr("Sharpness"), focus_label_);
    focus_form->addRow(focus_history_);
    layout->addWidget(focus_box);

    auto* loupe_box = new QGroupBox(tr("Loupe"), this);
    auto* loupe_form = new QFormLayout(loupe_box);
    loupe_zoom_ = new QSpinBox(loupe_box);
    loupe_zoom_->setRange(2, 16);
    loupe_zoom_->setValue(8);
    loupe_zoom_->setSuffix(QStringLiteral("×"));
    loupe_zoom_->setAccessibleName(QStringLiteral("Loupe magnification"));
    loupe_zoom_->setToolTip(tr("Magnification of the 17 × 17 pixels around the cursor (no interpolation)"));
    loupe_label_ = new QLabel(loupe_box);
    loupe_label_->setAccessibleName(QStringLiteral("Loupe image"));
    loupe_label_->setMinimumSize((2 * kLoupeRadius + 1) * 8, (2 * kLoupeRadius + 1) * 8);
    loupe_label_->setAlignment(Qt::AlignCenter);
    loupe_form->addRow(tr("Magnification"), loupe_zoom_);
    loupe_form->addRow(loupe_label_);
    layout->addWidget(loupe_box);

    auto* inspect_box = new QGroupBox(tr("Pixel inspector and region of interest"), this);
    auto* inspect_form = new QFormLayout(inspect_box);
    pixel_label_ = new QLabel(tr("Move the cursor over the picture"), inspect_box);
    pixel_label_->setAccessibleName(QStringLiteral("Pixel value"));
    roi_label_ = new QLabel(tr("Shift-drag on the picture to select a region"), inspect_box);
    roi_label_->setAccessibleName(QStringLiteral("Region statistics"));
    roi_label_->setWordWrap(true);
    inspect_form->addRow(tr("Pixel"), pixel_label_);
    inspect_form->addRow(tr("Region"), roi_label_);
    layout->addWidget(inspect_box);
    layout->addStretch(1);

    connect(loupe_zoom_, &QSpinBox::valueChanged, this, [this](int) { refresh(); });
    connect(focus_enabled_, &QCheckBox::toggled, this, [this](bool on) {
        if (!on) {
            focus_history_->clear();
            focus_label_->setText(QStringLiteral("—"));
        }
    });
}

void ImageToolsPanel::set_colours(QColor line, QColor best)
{
    focus_history_->set_colours(line, best);
}

void ImageToolsPanel::show_frame(const std::shared_ptr<const PreviewFrame>& frame)
{
    frame_ = frame;
    if (frame_ && frame_->picture && focus_enabled_->isChecked()) {
        const cv::Rect region =
            roi_.has_value() ? cv::Rect(roi_->x(), roi_->y(), roi_->width(), roi_->height()) : cv::Rect();
        const double focus = focus_measure(*frame_->picture, region);
        last_focus_ = focus;
        focus_label_->setText(QStringLiteral("%1  (best %2)")
                                  .arg(focus, 0, 'f', 1)
                                  .arg(std::max(focus_history_->best(), focus), 0, 'f', 1));
        focus_history_->push(focus);
    }
    refresh();
}

void ImageToolsPanel::set_cursor(std::optional<QPoint> image_point)
{
    cursor_ = image_point;
    refresh();
}

void ImageToolsPanel::set_roi(std::optional<QRect> roi)
{
    roi_ = roi;
    focus_history_->clear();
    refresh();
}

void ImageToolsPanel::refresh()
{
    if (!frame_ || !frame_->picture) {
        return;
    }
    const cv::Mat& picture = *frame_->picture;
    if (cursor_.has_value()) {
        const PixelValue value = pixel_value(picture, cursor_->x(), cursor_->y());
        if (value.valid) {
            pixel_label_->setText(QStringLiteral("x %1, y %2:  R %3  G %4  B %5  luma %6")
                                      .arg(value.x)
                                      .arg(value.y)
                                      .arg(value.red)
                                      .arg(value.green)
                                      .arg(value.blue)
                                      .arg(value.luma));
            const QImage loupe = loupe_image(picture, *cursor_, kLoupeRadius, loupe_zoom_->value());
            loupe_label_->setPixmap(QPixmap::fromImage(loupe));
        }
    }
    if (roi_.has_value()) {
        const auto stats = roi_statistics(picture, cv::Rect(roi_->x(), roi_->y(), roi_->width(), roi_->height()));
        if (stats.has_value()) {
            roi_label_->setText(tr("%1 × %2 at (%3, %4): %5 pixels\nmean R %6 G %7 B %8 (luma %9)\n"
                                   "std R %10 G %11 B %12\nmin/max R %13/%14 G %15/%16 B %17/%18\nsharpness %19")
                                    .arg(roi_->width())
                                    .arg(roi_->height())
                                    .arg(roi_->x())
                                    .arg(roi_->y())
                                    .arg(stats->pixels)
                                    .arg(stats->mean[2], 0, 'f', 1)
                                    .arg(stats->mean[1], 0, 'f', 1)
                                    .arg(stats->mean[0], 0, 'f', 1)
                                    .arg(stats->luma_mean, 0, 'f', 1)
                                    .arg(stats->std_dev[2], 0, 'f', 1)
                                    .arg(stats->std_dev[1], 0, 'f', 1)
                                    .arg(stats->std_dev[0], 0, 'f', 1)
                                    .arg(stats->minimum[2])
                                    .arg(stats->maximum[2])
                                    .arg(stats->minimum[1])
                                    .arg(stats->maximum[1])
                                    .arg(stats->minimum[0])
                                    .arg(stats->maximum[0])
                                    .arg(stats->focus, 0, 'f', 1));
        }
    } else {
        roi_label_->setText(tr("Shift-drag on the picture to select a region"));
    }
}

}  // namespace cloudscope::desktop
