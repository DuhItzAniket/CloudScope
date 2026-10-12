// The log console and notifications (P042, FR-DSP-09): every log record of the process (the in-memory sink of
// log.hpp feeds it) in a table with level and text filters, and a notification centre for the messages the
// operator must see (device failures, captures finished), shown in the status bar and kept in a list.
//
// Lives on the UI thread. Log records arrive on the thread that logged them and are handed over in batches.
#pragma once

#include <cloudscope/common/executor.hpp>
#include <cloudscope/common/log.hpp>

#include <QtCore/QAbstractTableModel>
#include <QtCore/QDateTime>
#include <QtCore/QString>
#include <QtWidgets/QWidget>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;

namespace cloudscope::desktop {

class LogFilterProxy;  // the level and text filter over the log model (console_panel.cpp)

class LogModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column : std::uint8_t { TimeColumn = 0, LevelColumn, ComponentColumn, MessageColumn, ColumnCount };

    explicit LogModel(std::size_t capacity = 5000, QObject* parent = nullptr);
    ~LogModel() override;
    LogModel(const LogModel&) = delete;
    LogModel& operator=(const LogModel&) = delete;
    LogModel(LogModel&&) = delete;
    LogModel& operator=(LogModel&&) = delete;

    // Starts listening to the process log (and takes the records already in the memory sink).
    void attach();
    void detach();
    void append(const LogRecord& record);  // UI thread
    void clear();
    [[nodiscard]] const LogRecord& record(int row) const { return records_.at(static_cast<std::size_t>(row)); }

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    void flush();  // UI thread: moves the pending records into the model

    std::size_t capacity_;
    std::deque<LogRecord> records_;
    std::mutex pending_mutex_;
    std::vector<LogRecord> pending_;
    std::atomic<bool> flush_posted_{false};
    int listener_ = -1;
    QtExecutor ui_;
};

class ConsolePanel : public QWidget {
    Q_OBJECT

public:
    explicit ConsolePanel(QWidget* parent = nullptr);
    [[nodiscard]] LogModel* model() const { return model_; }
    [[nodiscard]] QAbstractItemModel* proxy() const;  // the filtered view of the model
    [[nodiscard]] QTableView* table() const { return table_; }
    void set_minimum_level(LogLevel level);
    void set_search(const QString& text);

private:
    void apply_filters();

    LogModel* model_ = nullptr;
    LogFilterProxy* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QComboBox* level_box_ = nullptr;
    QLineEdit* search_ = nullptr;
    QCheckBox* follow_ = nullptr;
    QPushButton* clear_ = nullptr;
    QLabel* count_label_ = nullptr;
};

struct Notification {
    QDateTime time;
    LogLevel level = LogLevel::Info;
    QString text;
};

class NotificationCenter : public QObject {
    Q_OBJECT

public:
    explicit NotificationCenter(QObject* parent = nullptr);
    void notify(LogLevel level, const QString& text);
    [[nodiscard]] const std::vector<Notification>& notifications() const { return notifications_; }
    [[nodiscard]] int unread() const { return unread_; }
    void mark_read();

Q_SIGNALS:
    void notified(const Notification& notification);
    void unreadChanged(int unread);

private:
    std::vector<Notification> notifications_;
    int unread_ = 0;
};

}  // namespace cloudscope::desktop
