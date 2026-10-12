#include "viewport.hpp"

#include <algorithm>
#include <cmath>

namespace cloudscope::desktop {

void Viewport::set_widget_size(QSizeF size)
{
    widget_ = QSizeF(std::max(size.width(), 1.0), std::max(size.height(), 1.0));
    relayout();
}

void Viewport::set_image_size(QSize size)
{
    const bool changed = size != image_;
    image_ = QSize(std::max(size.width(), 0), std::max(size.height(), 0));
    if (changed && mode_ == ZoomMode::Free) {
        // A new image size under a free zoom: keep the scale, re-centre.
        offset_ = QPointF((widget_.width() - scale_ * image_.width()) / 2.0,
                          (widget_.height() - scale_ * image_.height()) / 2.0);
    }
    relayout();
}

double Viewport::fit_scale() const
{
    if (image_.isEmpty()) {
        return 1.0;
    }
    return std::clamp(std::min(widget_.width() / image_.width(), widget_.height() / image_.height()), kMinScale,
                      kMaxScale);
}

void Viewport::fit()
{
    mode_ = ZoomMode::Fit;
    relayout();
}

void Viewport::one_to_one()
{
    mode_ = ZoomMode::OneToOne;
    relayout();
}

void Viewport::set_scale(double scale, QPointF anchor_widget)
{
    const double clamped = std::clamp(scale, kMinScale, kMaxScale);
    const QPointF anchor_image = widget_to_image(anchor_widget);
    mode_ = ZoomMode::Free;
    scale_ = clamped;
    // The anchor's image point must map back to the same widget point: offset = anchor - image * scale.
    offset_ = anchor_widget - anchor_image * scale_;
    clamp_offset();
}

void Viewport::zoom_by(double factor, QPointF anchor_widget)
{
    if (factor > 0.0 && std::isfinite(factor)) {
        set_scale(scale_ * factor, anchor_widget);
    }
}

void Viewport::pan_by(QPointF delta_widget)
{
    if (mode_ == ZoomMode::Fit) {
        return;  // nothing to pan: the whole image is visible
    }
    mode_ = ZoomMode::Free;
    offset_ += delta_widget;
    clamp_offset();
}

QPointF Viewport::image_to_widget(QPointF image_point) const
{
    return offset_ + image_point * scale_;
}

QPointF Viewport::widget_to_image(QPointF widget_point) const
{
    return (widget_point - offset_) / scale_;
}

QRectF Viewport::image_rect_in_widget() const
{
    return {offset_, QSizeF(image_.width() * scale_, image_.height() * scale_)};
}

QTransform Viewport::transform() const
{
    QTransform t;
    t.translate(offset_.x(), offset_.y());
    t.scale(scale_, scale_);
    return t;
}

bool Viewport::contains_image_point(QPointF image_point) const
{
    return image_point.x() >= 0.0 && image_point.y() >= 0.0 && image_point.x() < image_.width() &&
           image_point.y() < image_.height();
}

void Viewport::relayout()
{
    switch (mode_) {
    case ZoomMode::Fit:
        scale_ = fit_scale();
        break;
    case ZoomMode::OneToOne:
        scale_ = 1.0;
        break;
    case ZoomMode::Free:
        break;
    }
    if (mode_ != ZoomMode::Free) {
        offset_ = QPointF((widget_.width() - scale_ * image_.width()) / 2.0,
                          (widget_.height() - scale_ * image_.height()) / 2.0);
    }
    clamp_offset();
}

void Viewport::clamp_offset()
{
    const double scaled_w = scale_ * image_.width();
    const double scaled_h = scale_ * image_.height();
    // Smaller than the widget: centred. Larger: no gap between the image's edge and the widget's edge.
    if (scaled_w <= widget_.width()) {
        offset_.setX((widget_.width() - scaled_w) / 2.0);
    } else {
        offset_.setX(std::clamp(offset_.x(), widget_.width() - scaled_w, 0.0));
    }
    if (scaled_h <= widget_.height()) {
        offset_.setY((widget_.height() - scaled_h) / 2.0);
    } else {
        offset_.setY(std::clamp(offset_.y(), widget_.height() - scaled_h, 0.0));
    }
}

}  // namespace cloudscope::desktop
