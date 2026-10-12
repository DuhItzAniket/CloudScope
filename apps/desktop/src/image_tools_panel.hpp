// The image tools panel (P038, FR-DSP-04, FR-DSP-05): focus aid with a history of the sharpness measure, loupe
// around the cursor, pixel inspector, and statistics of the region of interest selected on the live view
// (Shift-drag). Lives on the UI thread; it works on the decoded picture of the newest preview frame.
#pragma once

#include "image_tools.hpp"
#include "preview_pipeline.hpp"

#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtWidgets/QWidget>

#include <deque>
#include <memory>
#include <optional>

class QCheckBox;
class QLabel;
class QSpinBox;

namespace cloudscope::desktop {

// A small line plot of recent values with the maximum marked (the focus history).
class SparklineWidget : public QWidget {
    Q_OBJECT

public:
    explicit SparklineWidget(QWidget* parent = nullptr);
    void push(double value);
    void clear();
    [[nodiscard]] const std::deque<double>& values() const { return values_; }
    [[nodiscard]] double best() const { return best_; }
    [[nodiscard]] QSize sizeHint() const override;
    void set_colours(QColor line, QColor best);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::deque<double> values_;
    double best_ = 0.0;
    std::size_t capacity_ = 300;
    QColor line_{230, 230, 230};
    QColor best_colour_{255, 200, 0};
};

class ImageToolsPanel : public QWidget {
    Q_OBJECT

public:
    explicit ImageToolsPanel(QWidget* parent = nullptr);

    void show_frame(const std::shared_ptr<const PreviewFrame>& frame);
    void set_cursor(std::optional<QPoint> image_point);
    void set_roi(std::optional<QRect> roi);
    void set_colours(QColor line, QColor best);

    [[nodiscard]] QLabel* pixel_label() const { return pixel_label_; }
    [[nodiscard]] QLabel* roi_label() const { return roi_label_; }
    [[nodiscard]] QLabel* focus_label() const { return focus_label_; }
    [[nodiscard]] SparklineWidget* focus_history() const { return focus_history_; }
    [[nodiscard]] QLabel* loupe_label() const { return loupe_label_; }
    [[nodiscard]] std::optional<double> last_focus() const { return last_focus_; }

private:
    void refresh();

    std::shared_ptr<const PreviewFrame> frame_;
    std::optional<QPoint> cursor_;
    std::optional<QRect> roi_;
    std::optional<double> last_focus_;
    QCheckBox* focus_enabled_ = nullptr;
    QLabel* focus_label_ = nullptr;
    SparklineWidget* focus_history_ = nullptr;
    QSpinBox* loupe_zoom_ = nullptr;
    QLabel* loupe_label_ = nullptr;
    QLabel* pixel_label_ = nullptr;
    QLabel* roi_label_ = nullptr;
};

}  // namespace cloudscope::desktop
