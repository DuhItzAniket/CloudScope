// Timelines of the STRATIA results (P069, FR-AI-07): cloud cover, cloud-base height with its 80 % band, and
// the top genus as a colour strip, over a chosen window; live from the controller, or replayed from the
// catalogue for a session.

#pragma once

#include <cloudscope/ai/inference_service.hpp>
#include <cloudscope/common/clock.hpp>
#include <cloudscope/session/catalogue.hpp>

#include <QtGui/QColor>
#include <QtWidgets/QWidget>

#include <chrono>
#include <deque>
#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;

namespace cloudscope::desktop {

struct TimelinePoint {
    UtcTime utc{};
    double cover_fraction = 0.0;
    std::optional<double> cbh_median_m;
    std::optional<double> cbh_low_80_m;
    std::optional<double> cbh_high_80_m;
    int genus_top = 0;
    bool uncertain = false;
    friend bool operator==(const TimelinePoint&, const TimelinePoint&) = default;
};

[[nodiscard]] TimelinePoint timeline_point(const ai::Prediction& prediction);
[[nodiscard]] TimelinePoint timeline_point(const AiResultEntry& entry);

// The points of one timeline, oldest first, trimmed to a window behind the newest point.
class AiTimelineModel {
public:
    void add(const TimelinePoint& point);
    void set_points(std::vector<TimelinePoint> points);  // sorted by time, trimmed
    void clear();
    void set_window(std::chrono::seconds window);
    [[nodiscard]] std::chrono::seconds window() const { return window_; }
    [[nodiscard]] const std::deque<TimelinePoint>& points() const { return points_; }
    [[nodiscard]] std::size_t size() const { return points_.size(); }

private:
    void trim();
    std::deque<TimelinePoint> points_;
    std::chrono::seconds window_{3600};
    static constexpr std::size_t kMaxPoints = 20000;
};

class AiTimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit AiTimelineWidget(QWidget* parent = nullptr);
    void set_model(const AiTimelineModel* model);  // not owned
    void set_colours(QColor text, QColor warning, QColor accent);
    [[nodiscard]] QSize sizeHint() const override;
    void paint(QPainter& painter, const QRectF& area) const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const AiTimelineModel* model_ = nullptr;
    QColor text_{230, 230, 230};
    QColor warning_{255, 140, 40};
    QColor accent_{90, 200, 255};
};

class AiTimelinePanel : public QWidget {
    Q_OBJECT
public:
    explicit AiTimelinePanel(QWidget* parent = nullptr);

    void add_prediction(const ai::Prediction& prediction);
    // Replaces the live points with the catalogue's rows for the query; the number of points loaded.
    [[nodiscard]] Expected<std::size_t> replay(const Catalogue& catalogue, const AiResultQuery& query);
    void set_sessions(const std::vector<SessionInfo>& sessions);  // offered for replay
    void set_colours(QColor text, QColor warning, QColor accent);
    void clear();

    [[nodiscard]] const AiTimelineModel& model() const { return model_; }
    [[nodiscard]] AiTimelineWidget* chart() const { return chart_; }
    [[nodiscard]] QComboBox* window_box() const { return window_; }
    [[nodiscard]] QComboBox* session_box() const { return session_; }
    [[nodiscard]] QPushButton* replay_button() const { return replay_; }
    [[nodiscard]] QPushButton* live_button() const { return live_; }
    [[nodiscard]] QLabel* summary_label() const { return summary_; }

Q_SIGNALS:
    void replayRequested(const QString& session_id);
    void liveRequested();

private:
    void update_summary();

    AiTimelineModel model_;
    AiTimelineWidget* chart_ = nullptr;
    QComboBox* window_ = nullptr;
    QComboBox* session_ = nullptr;
    QPushButton* replay_ = nullptr;
    QPushButton* live_ = nullptr;
    QLabel* summary_ = nullptr;
    bool replaying_ = false;
};

}  // namespace cloudscope::desktop
