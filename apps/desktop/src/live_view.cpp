#include "live_view.hpp"

#include <QtCore/QEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QOffscreenSurface>
#include <QtGui/QOpenGLContext>
#include <QtGui/QPainter>
#include <QtGui/QPen>
#include <QtGui/QResizeEvent>
#include <QtGui/QWheelEvent>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace cloudscope::desktop {

namespace {

constexpr double kWheelBase = 1.0015;  // zoom factor per wheel degree: one notch (15 degrees) is about 1.2x
constexpr double kStepFactor = 1.25;
constexpr std::int64_t kSmoothScalingPixels = 2'500'000;  // larger pictures are scaled without filtering

class RasterCanvas final : public QWidget {
public:
    explicit RasterCanvas(const LiveView& owner) : QWidget(nullptr), owner_(owner)
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setAccessibleName(QStringLiteral("Live view canvas"));
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        paint_live_view(painter, owner_.scene());
    }

private:
    const LiveView& owner_;
};

class GlCanvas final : public QOpenGLWidget {
public:
    explicit GlCanvas(const LiveView& owner) : QOpenGLWidget(nullptr), owner_(owner)
    {
        setMouseTracking(true);
        setAccessibleName(QStringLiteral("Live view canvas"));
    }

protected:
    void paintGL() override
    {
        QPainter painter(this);
        paint_live_view(painter, owner_.scene());
    }

private:
    const LiveView& owner_;
};

QString indicator_text(const LiveViewIndicators& indicators, const PreviewFrame* frame)
{
    QString text = QStringLiteral("Camera %1 fps  Display %2 fps  Lost %3  Dropped %4  Decode %5 ms")
                       .arg(indicators.camera_fps, 0, 'f', 1)
                       .arg(indicators.display_fps, 0, 'f', 1)
                       .arg(indicators.lost)
                       .arg(indicators.dropped)
                       .arg(indicators.decode_ms, 0, 'f', 1);
    if (frame != nullptr) {
        text += QStringLiteral("  %1×%2").arg(frame->info.width).arg(frame->info.height);
    }
    if (indicators.simulated) {
        text += QStringLiteral("  SIMULATED");
    }
    return text;
}

}  // namespace

Renderer renderer_from_string(std::string_view text)
{
    if (text == "opengl") {
        return Renderer::OpenGL;
    }
    if (text == "raster") {
        return Renderer::Raster;
    }
    return Renderer::Auto;
}

bool opengl_available()
{
    static const bool available = [] {
        if (QGuiApplication::instance() == nullptr) {
            return false;
        }
        QOpenGLContext context;
        if (!context.create()) {
            return false;
        }
        QOffscreenSurface surface;
        surface.setFormat(context.format());
        surface.create();
        if (!surface.isValid() || !context.makeCurrent(&surface)) {
            return false;
        }
        context.doneCurrent();
        return true;
    }();
    return available;
}

void paint_live_view(QPainter& painter, const LiveViewScene& scene)
{
    const QSizeF size = scene.viewport.widget_size();
    const QRectF whole(QPointF(0.0, 0.0), size);
    painter.fillRect(whole, scene.background);
    const PreviewFrame* frame = scene.frame.get();
    if (frame != nullptr && !frame->image.isNull()) {
        painter.save();
        const double scale = scene.viewport.scale();
        const std::int64_t pixels = static_cast<std::int64_t>(frame->image.width()) * frame->image.height();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, scale < 1.0 && pixels <= kSmoothScalingPixels);
        painter.setTransform(scene.viewport.transform());
        painter.drawImage(QPointF(0.0, 0.0), frame->image);
        painter.restore();
    } else {
        painter.setPen(scene.text_colour);
        painter.drawText(whole, Qt::AlignCenter, scene.message);
    }
    if (frame != nullptr) {
        const OverlayScene overlay_scene{.projection = scene.projection.get(),
                                         .sun = scene.sun.has_value() ? &*scene.sun : nullptr,
                                         .colour = scene.overlay_colour,
                                         .sun_colour = scene.sun_colour};
        paint_overlays(painter, scene.viewport, scene.overlays, overlay_scene);
    }
    if (scene.roi.has_value() && frame != nullptr) {
        painter.save();
        QPen pen(scene.overlay_colour, 1.0, Qt::DashLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QRectF roi(scene.viewport.image_to_widget(scene.roi->topLeft()),
                         scene.viewport.image_to_widget(scene.roi->bottomRight() + QPoint(1, 1)));
        painter.drawRect(roi);
        painter.restore();
    }
    if (scene.show_indicators && frame != nullptr) {
        painter.save();
        const QString text = indicator_text(scene.indicators, frame);
        const QRectF box = painter.fontMetrics().boundingRect(text).adjusted(-6.0, -4.0, 6.0, 4.0);
        const QRectF at(QPointF(8.0, 8.0), box.size());
        painter.fillRect(at, QColor(0, 0, 0, 160));
        painter.setPen(scene.text_colour);
        painter.drawText(at, Qt::AlignCenter, text);
        painter.restore();
    }
}

LiveView::LiveView(Renderer renderer, QWidget* parent) : QWidget(parent), renderer_(renderer)
{
    if (renderer_ == Renderer::Auto) {
        renderer_ = opengl_available() ? Renderer::OpenGL : Renderer::Raster;
    }
    if (renderer_ == Renderer::OpenGL) {
        canvas_ = new GlCanvas(*this);
    } else {
        canvas_ = new RasterCanvas(*this);
    }
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(canvas_);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Live view"));
    scene_.message = tr("No camera open");
    scene_.viewport.set_widget_size(QSizeF(size()));
}

void LiveView::repaint_canvas()
{
    canvas_->update();
}

void LiveView::layout_viewport()
{
    scene_.viewport.set_widget_size(QSizeF(size()));
}

void LiveView::set_frame(std::shared_ptr<const PreviewFrame> frame)
{
    const QSize image_size = frame ? frame->image.size() : QSize();
    if (image_size != scene_.viewport.image_size()) {
        scene_.viewport.set_image_size(image_size);
        Q_EMIT zoomChanged(scene_.viewport.scale());
    }
    scene_.frame = std::move(frame);
    repaint_canvas();
}

void LiveView::set_message(const QString& message)
{
    scene_.message = message;
    repaint_canvas();
}

void LiveView::set_overlay_settings(const OverlaySettings& settings)
{
    scene_.overlays = settings;
    repaint_canvas();
}

void LiveView::set_projection(std::shared_ptr<const SkyProjection> projection)
{
    scene_.projection = std::move(projection);
    repaint_canvas();
}

void LiveView::set_sun(std::optional<SunPosition> sun)
{
    scene_.sun = sun;
    repaint_canvas();
}

void LiveView::set_indicators(const LiveViewIndicators& indicators)
{
    scene_.indicators = indicators;
    repaint_canvas();
}

void LiveView::set_show_indicators(bool show)
{
    scene_.show_indicators = show;
    repaint_canvas();
}

void LiveView::set_colours(QColor overlay, QColor sun, QColor text)
{
    scene_.overlay_colour = overlay;
    scene_.sun_colour = sun;
    scene_.text_colour = text;
    repaint_canvas();
}

void LiveView::set_roi(std::optional<QRect> roi)
{
    scene_.roi = roi;
    repaint_canvas();
    Q_EMIT roiChanged(roi);
}

void LiveView::fit()
{
    scene_.viewport.fit();
    Q_EMIT zoomChanged(scene_.viewport.scale());
    repaint_canvas();
}

void LiveView::one_to_one()
{
    scene_.viewport.one_to_one();
    Q_EMIT zoomChanged(scene_.viewport.scale());
    repaint_canvas();
}

void LiveView::zoom_in()
{
    const QPointF centre(width() / 2.0, height() / 2.0);
    scene_.viewport.zoom_by(kStepFactor, centre);
    Q_EMIT zoomChanged(scene_.viewport.scale());
    repaint_canvas();
}

void LiveView::zoom_out()
{
    const QPointF centre(width() / 2.0, height() / 2.0);
    scene_.viewport.zoom_by(1.0 / kStepFactor, centre);
    Q_EMIT zoomChanged(scene_.viewport.scale());
    repaint_canvas();
}

void LiveView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layout_viewport();
    Q_EMIT zoomChanged(scene_.viewport.scale());
}

void LiveView::wheelEvent(QWheelEvent* event)
{
    const int degrees = event->angleDelta().y() / 8;
    if (degrees == 0) {
        event->ignore();
        return;
    }
    scene_.viewport.zoom_by(std::pow(kWheelBase, static_cast<double>(degrees)), event->position());
    Q_EMIT zoomChanged(scene_.viewport.scale());
    update_cursor(event->position());
    repaint_canvas();
    event->accept();
}

void LiveView::update_cursor(QPointF widget_point)
{
    const QPointF image_point = scene_.viewport.widget_to_image(widget_point);
    const bool over = scene_.frame != nullptr && scene_.viewport.contains_image_point(image_point);
    const QPoint pixel(static_cast<int>(std::floor(image_point.x())), static_cast<int>(std::floor(image_point.y())));
    cursor_ = over ? std::optional<QPoint>(pixel) : std::nullopt;
    Q_EMIT cursorMoved(pixel, over);
}

void LiveView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    setFocus();
    const QPointF image_point = scene_.viewport.widget_to_image(event->position());
    if ((event->modifiers() & Qt::ShiftModifier) != 0 && scene_.frame != nullptr) {
        const QSize image = scene_.viewport.image_size();
        roi_anchor_ =
            QPoint(std::clamp(static_cast<int>(std::floor(image_point.x())), 0, std::max(image.width() - 1, 0)),
                   std::clamp(static_cast<int>(std::floor(image_point.y())), 0, std::max(image.height() - 1, 0)));
    } else {
        drag_last_ = event->position();
    }
    event->accept();
}

void LiveView::mouseMoveEvent(QMouseEvent* event)
{
    update_cursor(event->position());
    if (drag_last_.has_value()) {
        scene_.viewport.pan_by(event->position() - *drag_last_);
        drag_last_ = event->position();
        repaint_canvas();
    } else if (roi_anchor_.has_value()) {
        const QPointF image_point = scene_.viewport.widget_to_image(event->position());
        const QSize image = scene_.viewport.image_size();
        const QPoint current(
            std::clamp(static_cast<int>(std::floor(image_point.x())), 0, std::max(image.width() - 1, 0)),
            std::clamp(static_cast<int>(std::floor(image_point.y())), 0, std::max(image.height() - 1, 0)));
        set_roi(QRect(*roi_anchor_, current).normalized());
    }
    event->accept();
}

void LiveView::mouseReleaseEvent(QMouseEvent* event)
{
    drag_last_.reset();
    roi_anchor_.reset();
    event->accept();
}

void LiveView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (scene_.viewport.mode() == ZoomMode::Fit) {
        scene_.viewport.set_scale(1.0, event->position());
    } else {
        scene_.viewport.fit();
    }
    Q_EMIT zoomChanged(scene_.viewport.scale());
    repaint_canvas();
    event->accept();
}

void LiveView::leaveEvent(QEvent* event)
{
    cursor_.reset();
    Q_EMIT cursorMoved(QPoint(), false);
    QWidget::leaveEvent(event);
}

}  // namespace cloudscope::desktop
