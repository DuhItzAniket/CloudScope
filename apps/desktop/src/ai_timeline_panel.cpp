#include "ai_timeline_panel.hpp"

#include "ai_overlay.hpp"

#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

namespace cloudscope::desktop {

namespace {

constexpr double kCbhTop = 12000.0;  // the contract's highest bin edge
constexpr int kStripHeight = 10;
constexpr int kMargin = 6;
constexpr int kLabelWidth = 44;

// One colour per genus of the contract, in its order (clear and contrail last).
const std::array<QColor, 12>& genus_colours()
{
    static const std::array<QColor, 12> colours{
        QColor(200, 230, 255), QColor(180, 210, 255), QColor(160, 190, 240),  // Ci, Cc, Cs: pale blues
        QColor(150, 230, 150), QColor(120, 200, 120), QColor(90, 150, 90),    // Ac, As, Ns: greens
        QColor(255, 220, 120), QColor(230, 190, 100), QColor(255, 180, 60),   // Sc, St, Cu: ambers
        QColor(220, 90, 90),                                                  // Cb: red
        QColor(60, 60, 120),                                                  // clear: deep blue
        QColor(200, 200, 200)};                                               // contrail: grey
    return colours;
}

}  // namespace

TimelinePoint timeline_point(const ai::Prediction& prediction)
{
    TimelinePoint p;
    p.utc = prediction.frame.captured.utc;
    p.cover_fraction = prediction.cover_fraction;
    if (prediction.cbh) {
        p.cbh_median_m = prediction.cbh->median_m;
        p.cbh_low_80_m = prediction.cbh->low_80_m;
        p.cbh_high_80_m = prediction.cbh->high_80_m;
    }
    p.genus_top = prediction.genus_top;
    p.uncertain = prediction.uncertain;
    return p;
}

TimelinePoint timeline_point(const AiResultEntry& entry)
{
    return {.utc = entry.utc,
            .cover_fraction = entry.cover_fraction,
            .cbh_median_m = entry.cbh_median_m,
            .cbh_low_80_m = entry.cbh_low_80_m,
            .cbh_high_80_m = entry.cbh_high_80_m,
            .genus_top = entry.genus_top,
            .uncertain = entry.uncertain};
}

// ------------------------------------------------------------------------------------------------- model

void AiTimelineModel::add(const TimelinePoint& point)
{
    if (!points_.empty() && point.utc < points_.back().utc) {
        const auto at = std::ranges::upper_bound(points_, point.utc, {}, &TimelinePoint::utc);
        points_.insert(at, point);
    } else {
        points_.push_back(point);
    }
    trim();
}

void AiTimelineModel::set_points(std::vector<TimelinePoint> points)
{
    std::ranges::stable_sort(points, {}, &TimelinePoint::utc);
    points_.assign(points.begin(), points.end());
    trim();
}

void AiTimelineModel::clear()
{
    points_.clear();
}

void AiTimelineModel::set_window(std::chrono::seconds window)
{
    window_ = window;
    trim();
}

void AiTimelineModel::trim()
{
    if (points_.empty()) {
        return;
    }
    const UtcTime newest = points_.back().utc;
    while (!points_.empty() && (newest - points_.front().utc > window_ || points_.size() > kMaxPoints)) {
        points_.pop_front();
    }
}

// ------------------------------------------------------------------------------------------------ widget

AiTimelineWidget::AiTimelineWidget(QWidget* parent) : QWidget(parent)
{
    setAccessibleName(tr("STRATIA timelines"));
    setMinimumHeight(120);
}

void AiTimelineWidget::set_model(const AiTimelineModel* model)
{
    model_ = model;
    update();
}

void AiTimelineWidget::set_colours(QColor text, QColor warning, QColor accent)
{
    text_ = text;
    warning_ = warning;
    accent_ = accent;
    update();
}

QSize AiTimelineWidget::sizeHint() const
{
    return {480, 180};
}

void AiTimelineWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paint(painter, rect());
}

void AiTimelineWidget::paint(QPainter& painter, const QRectF& area) const
{
    painter.fillRect(area, QColor(0, 0, 0, 40));
    if (model_ == nullptr || model_->points().empty()) {
        painter.setPen(text_);
        painter.drawText(area, Qt::AlignCenter, tr("No results yet"));
        return;
    }
    const std::deque<TimelinePoint>& points = model_->points();
    const UtcTime newest = points.back().utc;
    const auto window = std::chrono::duration_cast<std::chrono::milliseconds>(model_->window());
    const double span = std::max<double>(static_cast<double>(window.count()), 1000.0);
    const QRectF plot(area.left() + kLabelWidth, area.top() + kMargin, area.width() - kLabelWidth - kMargin,
                      area.height() - 2 * kMargin);
    const double strip_top = plot.bottom() - kStripHeight;
    const double chart_height = strip_top - plot.top() - kMargin;
    const double cover_bottom = plot.top() + chart_height * 0.45;
    const double cbh_top = cover_bottom + kMargin;
    const double cbh_bottom = strip_top - kMargin;
    const auto x_of = [&](UtcTime t) {
        const double age =
            static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(newest - t).count());
        return plot.right() - age / span * plot.width();
    };
    painter.setPen(text_);
    painter.drawText(QRectF(area.left(), plot.top(), kLabelWidth - 4, cover_bottom - plot.top()),
                     Qt::AlignTop | Qt::AlignRight, tr("cover"));
    painter.drawText(QRectF(area.left(), cbh_top, kLabelWidth - 4, cbh_bottom - cbh_top), Qt::AlignTop | Qt::AlignRight,
                     tr("base"));
    painter.drawText(QRectF(area.left(), strip_top, kLabelWidth - 4, kStripHeight), Qt::AlignVCenter | Qt::AlignRight,
                     tr("genus"));
    painter.setPen(QPen(QColor(text_.red(), text_.green(), text_.blue(), 60), 1.0));
    painter.drawLine(QPointF(plot.left(), cover_bottom), QPointF(plot.right(), cover_bottom));
    painter.drawLine(QPointF(plot.left(), cbh_bottom), QPointF(plot.right(), cbh_bottom));

    // cover line
    QPainterPath cover;
    bool first = true;
    for (const TimelinePoint& p : points) {
        const QPointF at(x_of(p.utc), cover_bottom - p.cover_fraction * (cover_bottom - plot.top()));
        if (first) {
            cover.moveTo(at);
            first = false;
        } else {
            cover.lineTo(at);
        }
    }
    painter.setPen(QPen(accent_, 1.5));
    painter.drawPath(cover);

    // cloud base: band and median
    const auto y_cbh = [&](double metres) {
        return cbh_bottom - std::clamp(metres, 0.0, kCbhTop) / kCbhTop * (cbh_bottom - cbh_top);
    };
    QPainterPath band;
    QPainterPath median;
    bool band_open = false;
    bool median_open = false;
    std::vector<QPointF> lows;
    for (const TimelinePoint& p : points) {
        if (!p.cbh_median_m) {
            if (band_open) {
                for (const QPointF& low : std::views::reverse(lows)) {
                    band.lineTo(low);
                }
                band.closeSubpath();
                lows.clear();
                band_open = false;
            }
            median_open = false;
            continue;
        }
        const double x = x_of(p.utc);
        const QPointF high(x, y_cbh(p.cbh_high_80_m.value_or(*p.cbh_median_m)));
        const QPointF low(x, y_cbh(p.cbh_low_80_m.value_or(*p.cbh_median_m)));
        if (!band_open) {
            band.moveTo(high);
            band_open = true;
        } else {
            band.lineTo(high);
        }
        lows.push_back(low);
        const QPointF mid(x, y_cbh(*p.cbh_median_m));
        if (!median_open) {
            median.moveTo(mid);
            median_open = true;
        } else {
            median.lineTo(mid);
        }
    }
    if (band_open) {
        for (const QPointF& low : std::views::reverse(lows)) {
            band.lineTo(low);
        }
        band.closeSubpath();
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(accent_.red(), accent_.green(), accent_.blue(), 60));
    painter.drawPath(band);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(accent_, 1.5));
    painter.drawPath(median);

    // genus strip: one coloured cell from each point to the next; uncertain points in the warning colour
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double x0 = x_of(points[i].utc);
        const double x1 = i + 1 < points.size() ? x_of(points[i + 1].utc) : plot.right();
        const QColor colour = points[i].uncertain
                                  ? warning_
                                  : genus_colours()[static_cast<std::size_t>(std::clamp(points[i].genus_top, 0, 11))];
        painter.fillRect(QRectF(x0, strip_top, std::max(x1 - x0, 1.0), kStripHeight), colour);
    }
}

// ------------------------------------------------------------------------------------------------- panel

AiTimelinePanel::AiTimelinePanel(QWidget* parent)
    : QWidget(parent), chart_(new AiTimelineWidget(this)), window_(new QComboBox(this)), session_(new QComboBox(this))
{
    setAccessibleName(tr("STRATIA timeline panel"));
    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout();
    window_->setAccessibleName(tr("Timeline window"));
    window_->addItem(tr("10 minutes"), 600);
    window_->addItem(tr("1 hour"), 3600);
    window_->addItem(tr("6 hours"), 6 * 3600);
    window_->addItem(tr("24 hours"), 24 * 3600);
    window_->setCurrentIndex(1);
    session_->setAccessibleName(tr("Session to replay"));
    session_->addItem(tr("(live results)"), QString());
    replay_ = new QPushButton(tr("Replay session"), this);
    replay_->setToolTip(tr("Load the logged results of the chosen session from the catalogue"));
    live_ = new QPushButton(tr("Live"), this);
    live_->setToolTip(tr("Back to the results of the running model"));
    controls->addWidget(new QLabel(tr("Window:"), this));
    controls->addWidget(window_);
    controls->addWidget(session_, 1);
    controls->addWidget(replay_);
    controls->addWidget(live_);
    layout->addLayout(controls);
    chart_->set_model(&model_);
    layout->addWidget(chart_, 1);
    summary_ = new QLabel(tr("No results yet"), this);
    summary_->setAccessibleName(tr("Timeline summary"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    connect(window_, &QComboBox::currentIndexChanged, this, [this] {
        model_.set_window(std::chrono::seconds(window_->currentData().toInt()));
        chart_->update();
        update_summary();
    });
    connect(replay_, &QPushButton::clicked, this,
            [this] { Q_EMIT replayRequested(session_->currentData().toString()); });
    connect(live_, &QPushButton::clicked, this, [this] {
        replaying_ = false;
        clear();
        Q_EMIT liveRequested();
    });
}

void AiTimelinePanel::add_prediction(const ai::Prediction& prediction)
{
    if (replaying_) {
        return;  // a replay stays as loaded until the operator goes back to live
    }
    model_.add(timeline_point(prediction));
    chart_->update();
    update_summary();
}

Expected<std::size_t> AiTimelinePanel::replay(const Catalogue& catalogue, const AiResultQuery& query)
{
    auto rows = catalogue.ai_results(query);
    if (!rows) {
        return fail(rows.error());
    }
    std::vector<TimelinePoint> points;
    points.reserve(rows->size());
    for (const AiResultEntry& row : *rows) {
        points.push_back(timeline_point(row));
    }
    const std::size_t count = points.size();
    model_.set_points(std::move(points));
    replaying_ = true;
    chart_->update();
    update_summary();
    return count;
}

void AiTimelinePanel::set_sessions(const std::vector<SessionInfo>& sessions)
{
    const QString current = session_->currentData().toString();
    session_->clear();
    session_->addItem(tr("(live results)"), QString());
    for (const SessionInfo& info : sessions) {
        session_->addItem(QString::fromStdString(info.id), QString::fromStdString(info.id));
    }
    const int index = session_->findData(current);
    session_->setCurrentIndex(index >= 0 ? index : 0);
}

void AiTimelinePanel::set_colours(QColor text, QColor warning, QColor accent)
{
    chart_->set_colours(text, warning, accent);
}

void AiTimelinePanel::clear()
{
    model_.clear();
    chart_->update();
    update_summary();
}

void AiTimelinePanel::update_summary()
{
    const auto& points = model_.points();
    if (points.empty()) {
        summary_->setText(tr("No results yet"));
        return;
    }
    double cover = 0.0;
    std::size_t uncertain = 0;
    std::size_t with_base = 0;
    double base = 0.0;
    for (const TimelinePoint& p : points) {
        cover += p.cover_fraction;
        uncertain += p.uncertain ? 1 : 0;
        if (p.cbh_median_m) {
            ++with_base;
            base += *p.cbh_median_m;
        }
    }
    const QString base_text = with_base > 0 ? height_text(base / static_cast<double>(with_base)) : tr("–");
    summary_->setText(tr("%1 results%2: mean cover %3 %, mean cloud base %4, %5 uncertain")
                          .arg(points.size())
                          .arg(replaying_ ? tr(" (replayed)") : QString())
                          .arg(cover / static_cast<double>(points.size()) * 100.0, 0, 'f', 0)
                          .arg(base_text)
                          .arg(uncertain));
}

}  // namespace cloudscope::desktop
