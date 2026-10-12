// Histogram, scopes and display stretch (P036, FR-DSP-02, FR-DSP-03, FR-DSP-11): the luma histogram with linear
// and logarithmic scales and clipping indicators, the RGB parade, and the controls of the preview-only stretch
// and the over-exposure highlight.
//
// Lives on the UI thread. The widgets paint what the preview pipeline computed; nothing here touches frames.
#pragma once

#include "display_stretch.hpp"
#include "preview_pipeline.hpp"

#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtWidgets/QWidget>

#include <array>
#include <memory>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;

namespace cloudscope::desktop {

class HistogramWidget : public QWidget {
    Q_OBJECT

public:
    explicit HistogramWidget(QWidget* parent = nullptr);

    void set_histograms(const Histogram& luma, const ChannelHistograms& channels);
    void set_logarithmic(bool logarithmic);
    [[nodiscard]] bool logarithmic() const { return logarithmic_; }
    void set_colours(QColor foreground, QColor warning);
    [[nodiscard]] ClippingFractions clipping() const { return clipping_; }
    [[nodiscard]] QSize sizeHint() const override;

    // Paints the histogram into any painter (the panel's paintEvent and the screenshot tests).
    void paint(QPainter& painter, const QRectF& area) const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Histogram luma_{};
    ChannelHistograms channels_{};
    ClippingFractions clipping_;
    bool logarithmic_ = false;
    QColor foreground_{230, 230, 230};
    QColor warning_{255, 140, 40};
};

class ParadeWidget : public QWidget {
    Q_OBJECT

public:
    explicit ParadeWidget(QWidget* parent = nullptr);
    void set_parade(const std::array<cv::Mat, 3>& parade);
    [[nodiscard]] QSize sizeHint() const override;
    void paint(QPainter& painter, const QRectF& area) const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::array<QImage, 3> images_;  // one per channel, intensity = share of pixels
};

class HistogramPanel : public QWidget {
    Q_OBJECT

public:
    explicit HistogramPanel(QWidget* parent = nullptr);

    void show_frame(const std::shared_ptr<const PreviewFrame>& frame);
    [[nodiscard]] StretchSettings stretch() const { return stretch_; }
    void set_stretch(const StretchSettings& settings);  // updates the controls without emitting
    [[nodiscard]] bool parade_wanted() const;
    void set_colours(QColor foreground, QColor warning);
    [[nodiscard]] HistogramWidget* histogram_widget() const { return histogram_; }

Q_SIGNALS:
    void stretchChanged(const StretchSettings& settings);
    void paradeWantedChanged(bool wanted);

private:
    void read_controls();
    void write_controls();

    HistogramWidget* histogram_ = nullptr;
    ParadeWidget* parade_ = nullptr;
    QCheckBox* log_scale_ = nullptr;
    QCheckBox* show_parade_ = nullptr;
    QCheckBox* enabled_ = nullptr;
    QCheckBox* automatic_ = nullptr;
    QSlider* black_ = nullptr;
    QSlider* white_ = nullptr;
    QDoubleSpinBox* gamma_ = nullptr;
    QCheckBox* highlight_ = nullptr;
    QLabel* clipping_label_ = nullptr;
    QLabel* applied_label_ = nullptr;
    StretchSettings stretch_;
    bool updating_ = false;
};

}  // namespace cloudscope::desktop
