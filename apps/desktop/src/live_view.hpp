// The live view (P034, FR-DSP-01): the camera picture with zoom, pan, fit and 1:1, the frame-rate and drop
// indicators, and the overlays of P037 drawn over it. Two canvases paint the same scene: an OpenGL one
// (QOpenGLWidget: the GPU scales the picture, which keeps 16-megapixel frames smooth) and a software one
// (QWidget with QPainter) for computers without usable OpenGL and for the offscreen tests.
//
// Lives on the UI thread. Pictures arrive as PreviewFrame objects from the preview pipeline.
#pragma once

#include "ai_overlay.hpp"
#include "overlay.hpp"
#include "preview_pipeline.hpp"
#include "viewport.hpp"

#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtWidgets/QWidget>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace cloudscope::desktop {

enum class Renderer : std::uint8_t { Auto, OpenGL, Raster };
[[nodiscard]] Renderer renderer_from_string(std::string_view text);  // "auto", "opengl", "raster"; Auto otherwise
// Whether an OpenGL context can be created on this computer (tried once).
[[nodiscard]] bool opengl_available();

struct LiveViewIndicators {
    double camera_fps = 0.0;    // frames the camera delivered per second
    double display_fps = 0.0;   // frames shown per second
    std::uint64_t lost = 0;     // lost before they reached CloudScope (gaps in the sequence numbers)
    std::uint64_t dropped = 0;  // read but not shown (pool misses plus preview skips)
    double decode_ms = 0.0;
    bool simulated = false;
};

// Everything the canvases paint; shared with the tests through LiveView::scene().
struct LiveViewScene {
    std::shared_ptr<const PreviewFrame> frame;
    Viewport viewport;
    OverlaySettings overlays;
    std::shared_ptr<const SkyProjection> projection;
    std::optional<SunPosition> sun;
    LiveViewIndicators indicators;
    bool show_indicators = true;
    QString message;  // shown centred when there is no picture ("No camera open")
    QColor overlay_colour{255, 200, 0};
    QColor sun_colour{255, 80, 0};
    QColor text_colour{230, 230, 230};
    QColor background{0, 0, 0};
    std::optional<QRect> roi;                          // region of interest in image pixels (P038)
    std::shared_ptr<const ai::Prediction> prediction;  // the latest STRATIA result for this frame (P068)
    QImage ai_image;                                   // its mask picture, or null
    AiOverlaySettings ai_overlay;
    QColor warning_colour{255, 140, 40};
};

// Paints a scene into a painter of the given widget size: the picture through the viewport, then the overlays,
// the region of interest and the indicators. Used by both canvases and by the screenshot tests.
void paint_live_view(QPainter& painter, const LiveViewScene& scene);

class LiveView : public QWidget {
    Q_OBJECT

public:
    explicit LiveView(Renderer renderer = Renderer::Auto, QWidget* parent = nullptr);

    [[nodiscard]] Renderer renderer_in_use() const { return renderer_; }
    [[nodiscard]] const LiveViewScene& scene() const { return scene_; }
    [[nodiscard]] const Viewport& viewport() const { return scene_.viewport; }

    void set_frame(std::shared_ptr<const PreviewFrame> frame);
    void set_message(const QString& message);
    void set_overlay_settings(const OverlaySettings& settings);
    void set_projection(std::shared_ptr<const SkyProjection> projection);
    void set_sun(std::optional<SunPosition> sun);
    void set_indicators(const LiveViewIndicators& indicators);
    void set_show_indicators(bool show);
    void set_colours(QColor overlay, QColor sun, QColor text);
    void set_roi(std::optional<QRect> roi);
    void set_prediction(std::shared_ptr<const ai::Prediction> prediction);  // null clears the AI overlay
    void set_ai_overlay_settings(const AiOverlaySettings& settings);
    void set_warning_colour(QColor colour);
    [[nodiscard]] std::optional<QRect> roi() const { return scene_.roi; }

    // The image pixel under the cursor, if the cursor is over the picture.
    [[nodiscard]] std::optional<QPoint> cursor_image_point() const { return cursor_; }

    void fit();
    void one_to_one();
    void zoom_in();
    void zoom_out();

Q_SIGNALS:
    void cursorMoved(QPoint image_point, bool over_image);
    void roiChanged(std::optional<QRect> roi);
    void zoomChanged(double scale);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void repaint_canvas();
    void layout_viewport();
    void update_cursor(QPointF widget_point);

    Renderer renderer_;
    QWidget* canvas_ = nullptr;  // the OpenGL or raster canvas, filling this widget
    LiveViewScene scene_;
    std::optional<QPoint> cursor_;
    std::optional<QPointF> drag_last_;
    std::optional<QPoint> roi_anchor_;  // image pixel where a Shift-drag started
};

}  // namespace cloudscope::desktop
