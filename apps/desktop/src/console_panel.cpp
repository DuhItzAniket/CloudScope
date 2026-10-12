#include "console_panel.hpp"

#include <QtCore/QSortFilterProxyModel>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableView>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

namespace cloudscope::desktop {

namespace {

QString level_text(LogLevel level)
{
    const std::string_view text = to_string(level);
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}  // namespace

// Filters by minimum level and by text in the component or the message.
class LogFilterProxy final : public QSortFilterProxyModel {
public:
    explicit LogFilterProxy(LogModel& source, QObject* parent) : QSortFilterProxyModel(parent), source_(source)
    {
        setSourceModel(&source);
    }
    void set_minimum_level(LogLevel level)
    {
        minimum_ = level;
        invalidateRowsFilter();
    }
    void set_search(const QString& text)
    {
        search_ = text;
        invalidateRowsFilter();
    }

protected:
    [[nodiscard]] bool filterAcceptsRow(int source_row, const QModelIndex& /*parent*/) const override
    {
        const LogRecord& record = source_.record(source_row);
        if (static_cast<int>(record.level) < static_cast<int>(minimum_)) {
            return false;
        }
        if (search_.isEmpty()) {
            return true;
        }
        return QString::fromStdString(record.message).contains(search_, Qt::CaseInsensitive) ||
               QString::fromStdString(record.component).contains(search_, Qt::CaseInsensitive);
    }

private:
    LogModel& source_;
    LogLevel minimum_ = LogLevel::Trace;
    QString search_;
};

LogModel::LogModel(std::size_t capacity, QObject* parent)
    : QAbstractTableModel(parent), capacity_(std::max<std::size_t>(capacity, 10)), ui_(this)
{
}

LogModel::~LogModel()
{
    detach();
}

void LogModel::attach()
{
    if (listener_ >= 0) {
        return;
    }
    for (const LogRecord& record : recent_log_records()) {
        append(record);
    }
    listener_ = add_log_listener([this](const LogRecord& record) {
        {
            const std::lock_guard lock(pending_mutex_);
            pending_.push_back(record);
        }
        if (!flush_posted_.exchange(true)) {
            ui_.post([this] { flush(); });
        }
    });
}

void LogModel::detach()
{
    if (listener_ >= 0) {
        remove_log_listener(listener_);
        listener_ = -1;
    }
}

void LogModel::flush()
{
    std::vector<LogRecord> batch;
    {
        const std::lock_guard lock(pending_mutex_);
        batch.swap(pending_);
    }
    flush_posted_.store(false);
    for (const LogRecord& record : batch) {
        append(record);
    }
}

void LogModel::append(const LogRecord& record)
{
    const int row = static_cast<int>(records_.size());
    beginInsertRows(QModelIndex(), row, row);
    records_.push_back(record);
    endInsertRows();
    if (records_.size() > capacity_) {
        const int excess = static_cast<int>(records_.size() - capacity_);
        beginRemoveRows(QModelIndex(), 0, excess - 1);
        records_.erase(records_.begin(), records_.begin() + excess);
        endRemoveRows();
    }
}

void LogModel::clear()
{
    beginResetModel();
    records_.clear();
    endResetModel();
}

int LogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(records_.size());
}

int LogModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= records_.size()) {
        return {};
    }
    const LogRecord& record = records_[static_cast<std::size_t>(index.row())];
    if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
        switch (index.column()) {
        case TimeColumn:
            return QString::fromStdString(format_iso8601(record.time));
        case LevelColumn:
            return level_text(record.level);
        case ComponentColumn:
            return QString::fromStdString(record.component);
        case MessageColumn:
            return QString::fromStdString(record.message);
        default:
            return {};
        }
    }
    if (role == Qt::ForegroundRole) {
        if (record.level >= LogLevel::Error) {
            return QColor(255, 90, 90);
        }
        if (record.level == LogLevel::Warn) {
            return QColor(255, 180, 60);
        }
    }
    return {};
}

QVariant LogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case TimeColumn:
        return tr("Time (UTC)");
    case LevelColumn:
        return tr("Level");
    case ComponentColumn:
        return tr("Component");
    case MessageColumn:
        return tr("Message");
    default:
        return {};
    }
}

ConsolePanel::ConsolePanel(QWidget* parent)
    : QWidget(parent),
      model_(new LogModel(5000, this)),
      proxy_(new LogFilterProxy(*model_, this)),
      table_(new QTableView(this)),
      level_box_(new QComboBox(this)),
      search_(new QLineEdit(this)),
      count_label_(new QLabel(this))
{
    setAccessibleName(QStringLiteral("Log console"));
    auto* layout = new QVBoxLayout(this);
    auto* row = new QHBoxLayout();
    level_box_->setAccessibleName(QStringLiteral("Minimum log level"));
    level_box_->setToolTip(tr("Hide messages below this level"));
    for (const LogLevel level :
         {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error, LogLevel::Critical}) {
        level_box_->addItem(level_text(level), static_cast<int>(level));
    }
    level_box_->setCurrentIndex(level_box_->findData(static_cast<int>(LogLevel::Info)));
    search_->setPlaceholderText(tr("Search messages and components"));
    search_->setAccessibleName(QStringLiteral("Log search"));
    search_->setClearButtonEnabled(true);
    follow_ = new QCheckBox(tr("Follow"), this);
    follow_->setChecked(true);
    follow_->setToolTip(tr("Scroll to new messages as they arrive"));
    clear_ = new QPushButton(tr("Clear"), this);
    clear_->setToolTip(tr("Remove the messages shown (the log files keep them)"));
    count_label_->setAccessibleName(QStringLiteral("Log count"));
    row->addWidget(new QLabel(tr("Level"), this));
    row->addWidget(level_box_);
    row->addWidget(search_, 1);
    row->addWidget(follow_);
    row->addWidget(clear_);
    row->addWidget(count_label_);
    layout->addLayout(row);

    table_->setModel(proxy_);
    table_->setAccessibleName(QStringLiteral("Log table"));
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(18);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setWordWrap(false);
    layout->addWidget(table_, 1);

    connect(level_box_, &QComboBox::currentIndexChanged, this, [this](int) { apply_filters(); });
    connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { apply_filters(); });
    connect(clear_, &QPushButton::clicked, model_, &LogModel::clear);
    connect(proxy_, &QAbstractItemModel::rowsInserted, this, [this] {
        count_label_->setText(tr("%1 shown").arg(proxy_->rowCount()));
        if (follow_->isChecked()) {
            table_->scrollToBottom();
        }
    });
    connect(proxy_, &QAbstractItemModel::modelReset, this,
            [this] { count_label_->setText(tr("%1 shown").arg(proxy_->rowCount())); });
    apply_filters();
    model_->attach();
}

QAbstractItemModel* ConsolePanel::proxy() const
{
    return proxy_;
}

void ConsolePanel::set_minimum_level(LogLevel level)
{
    level_box_->setCurrentIndex(level_box_->findData(static_cast<int>(level)));
}

void ConsolePanel::set_search(const QString& text)
{
    search_->setText(text);
}

void ConsolePanel::apply_filters()
{
    proxy_->set_minimum_level(static_cast<LogLevel>(level_box_->currentData().toInt()));
    proxy_->set_search(search_->text());
    count_label_->setText(tr("%1 shown").arg(proxy_->rowCount()));
}

NotificationCenter::NotificationCenter(QObject* parent) : QObject(parent) {}

void NotificationCenter::notify(LogLevel level, const QString& text)
{
    const Notification notification{.time = QDateTime::currentDateTimeUtc(), .level = level, .text = text};
    notifications_.push_back(notification);
    if (notifications_.size() > 500) {
        notifications_.erase(notifications_.begin());
    }
    ++unread_;
    Q_EMIT notified(notification);
    Q_EMIT unreadChanged(unread_);
}

void NotificationCenter::mark_read()
{
    if (unread_ != 0) {
        unread_ = 0;
        Q_EMIT unreadChanged(0);
    }
}

}  // namespace cloudscope::desktop
