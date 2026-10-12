// Geometry of the live view (P034, FR-DSP-01): where the image sits in the widget, zoom and pan, and the mapping
// between widget and image coordinates. Plain values without widgets, so that the renderer and the tests share it.
//
// Three modes: Fit (the whole image visible, centred), OneToOne (one image pixel per device-independent pixel,
// centred, pannable when larger than the widget) and Free (any scale after the user zoomed or panned). In every
// mode an image smaller than the widget is centred, and a larger one never shows empty space beyond its edges.
#pragma once

#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtCore/QSizeF>
#include <QtGui/QTransform>

#include <cstdint>

namespace cloudscope::desktop {

enum class ZoomMode : std::uint8_t { Fit, OneToOne, Free };

class Viewport {
public:
    static constexpr double kMinScale = 1.0 / 64.0;
    static constexpr double kMaxScale = 64.0;

    void set_widget_size(QSizeF size);
    void set_image_size(QSize size);
    [[nodiscard]] QSizeF widget_size() const { return widget_; }
    [[nodiscard]] QSize image_size() const { return image_; }

    [[nodiscard]] ZoomMode mode() const { return mode_; }
    [[nodiscard]] double scale() const { return scale_; }

    void fit();
    void one_to_one();
    // Zooms so that the image point under `anchor_widget` stays under it. The scale is clamped to [kMinScale, kMaxScale].
    void set_scale(double scale, QPointF anchor_widget);
    void zoom_by(double factor, QPointF anchor_widget);
    // Moves the image by a widget-space offset (dragging), within the limits described above.
    void pan_by(QPointF delta_widget);

    [[nodiscard]] QPointF image_to_widget(QPointF image_point) const;
    [[nodiscard]] QPointF widget_to_image(QPointF widget_point) const;
    [[nodiscard]] QRectF image_rect_in_widget() const;  // where the whole image is drawn (may exceed the widget)
    [[nodiscard]] QTransform transform() const;         // image coordinates -> widget coordinates
    // True if the image point lies on the image.
    [[nodiscard]] bool contains_image_point(QPointF image_point) const;

private:
    void relayout();
    void clamp_offset();
    [[nodiscard]] double fit_scale() const;

    QSizeF widget_{1.0, 1.0};
    QSize image_;
    double scale_ = 1.0;
    QPointF offset_;  // widget position of the image's top-left corner
    ZoomMode mode_ = ZoomMode::Fit;
};

}  // namespace cloudscope::desktop
