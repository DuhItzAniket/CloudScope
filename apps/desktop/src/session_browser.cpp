#include "session_browser.hpp"

#include "image_tools.hpp"

#include <cloudscope/capture/calibration_frames.hpp>
#include <cloudscope/capture/recording.hpp>
#include <cloudscope/common/config.hpp>
#include <cloudscope/common/log.hpp>

#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtCore/QThreadPool>
#include <QtCore/QTimeZone>
#include <QtGui/QPixmap>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDateTimeEdit>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableView>
#include <QtWidgets/QVBoxLayout>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace cloudscope::desktop {

namespace {

constexpr int kThumbnailSide = 96;

spdlog::logger& log()
{
    static spdlog::logger& instance = logger("desktop.sessions");
    return instance;
}

QString path_text(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

QString optional_number(const std::optional<double>& value, int decimals, const QString& unit = QString())
{
    if (!value.has_value()) {
        return QStringLiteral("—");
    }
    return QString::number(*value, 'f', decimals) + unit;
}

QString bytes_text(std::uint64_t bytes)
{
    if (bytes >= 1024ULL * 1024ULL) {
        return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MiB");
    }
    return QString::number(static_cast<double>(bytes) / 1024.0, 'f', 0) + QStringLiteral(" KiB");
}

// The sidecar of a picture, if it exists: "<name>.<ext>.json" (recording.hpp) or "<name>.json".
std::optional<std::filesystem::path> sidecar_of(const std::filesystem::path& picture)
{
    std::error_code error;
    std::filesystem::path appended =
        picture.string().empty() ? picture : std::filesystem::path(picture).concat(".json");
    if (std::filesystem::exists(appended, error)) {
        return appended;
    }
    std::filesystem::path replaced = std::filesystem::path(picture).replace_extension(".json");
    if (replaced != picture && std::filesystem::exists(replaced, error)) {
        return replaced;
    }
    return std::nullopt;
}

}  // namespace

QImage load_thumbnail(const std::filesystem::path& picture, int max_side)
{
    cv::Mat image;
    if (picture.extension() == ".fits" || picture.extension() == ".fit") {
        auto fits = read_fits_image(picture);
        if (!fits) {
            return {};
        }
        image = std::move(*fits);
    } else {
        std::ifstream in(picture, std::ios::binary);
        if (!in) {
            return {};
        }
        const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.empty()) {
            return {};
        }
        // Decoding at reduced size keeps 16-megapixel JPEGs cheap.
        image = cv::imdecode(bytes, cv::IMREAD_REDUCED_COLOR_4);
        if (image.empty()) {
            image = cv::imdecode(bytes, cv::IMREAD_COLOR);
        }
    }
    if (image.empty()) {
        return {};
    }
    if (image.depth() == CV_16U) {
        image.convertTo(image, CV_8U, 1.0 / 256.0);
    }
    if (image.channels() == 1) {
        cv::cvtColor(image, image, cv::COLOR_GRAY2BGR);
    }
    const int side = std::max(image.cols, image.rows);
    if (side > max_side) {
        const double scale = static_cast<double>(max_side) / side;
        cv::resize(image, image, cv::Size(), scale, scale, cv::INTER_AREA);
    }
    return to_qimage(image);
}

CatalogueModel::CatalogueModel(QObject* parent) : QAbstractTableModel(parent), self_(this) {}

Expected<void> CatalogueModel::open(const std::filesystem::path& file)
{
    std::error_code error;
    beginResetModel();
    rows_.clear();
    pages_ = 0;
    cursor_.reset();
    more_ = false;
    catalogue_.reset();
    thumbnails_.clear();
    in_flight_.clear();
    endResetModel();
    if (!std::filesystem::exists(file, error)) {
        return {};
    }
    auto opened = Catalogue::open(file);
    if (!opened) {
        return fail(opened.error());
    }
    catalogue_ = std::move(*opened);
    reload();
    return {};
}

void CatalogueModel::set_filter(const BrowserFilter& filter)
{
    filter_ = filter;
    reload();
}

void CatalogueModel::set_time_display(const std::string& display)
{
    time_display_ = display;
    if (!rows_.empty()) {
        Q_EMIT dataChanged(index(0, TimeColumn), index(static_cast<int>(rows_.size()) - 1, TimeColumn));
    }
}

std::vector<SessionInfo> CatalogueModel::sessions() const
{
    if (!catalogue_) {
        return {};
    }
    auto result = catalogue_->sessions();
    return result ? *result : std::vector<SessionInfo>{};
}

void CatalogueModel::reload()
{
    beginResetModel();
    rows_.clear();
    pages_ = 0;
    cursor_.reset();
    more_ = catalogue_ != nullptr;
    endResetModel();
    if (more_) {
        fetchMore(QModelIndex());
    }
}

int CatalogueModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int CatalogueModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

bool CatalogueModel::canFetchMore(const QModelIndex& parent) const
{
    return !parent.isValid() && more_;
}

void CatalogueModel::fetchMore(const QModelIndex& parent)
{
    if (parent.isValid() || !more_ || !catalogue_) {
        return;
    }
    FrameQuery query{.session_id = filter_.session_id,
                     .from = filter_.from,
                     .to = filter_.to,
                     .limit = kPageSize,
                     .newest_first = true};
    if (cursor_.has_value()) {
        query.to = *cursor_;  // exclusive: the page after the oldest row fetched so far
    }
    auto page = catalogue_->frames(query);
    if (!page) {
        log().warn("catalogue query failed: {}", page.error().to_string());
        more_ = false;
        return;
    }
    more_ = page->size() == kPageSize;
    if (!page->empty()) {
        cursor_ = page->back().utc;
    }
    std::vector<FrameEntry> accepted;
    accepted.reserve(page->size());
    for (FrameEntry& entry : *page) {
        if (!filter_.format.empty() && entry.format != filter_.format) {
            continue;
        }
        if (!filter_.include_simulated && entry.simulated) {
            continue;
        }
        accepted.push_back(std::move(entry));
    }
    ++pages_;
    if (accepted.empty()) {
        // A page of filtered-out rows: the view would see nothing new and stop asking, so keep going.
        if (more_) {
            fetchMore(parent);
        }
        return;
    }
    const int first = static_cast<int>(rows_.size());
    beginInsertRows(QModelIndex(), first, first + static_cast<int>(accepted.size()) - 1);
    rows_.insert(rows_.end(), accepted.begin(), accepted.end());
    endInsertRows();
}

QString CatalogueModel::time_text(UtcTime time) const
{
    const QDateTime utc = QDateTime::fromMSecsSinceEpoch(to_unix_ms(time), QTimeZone::UTC);
    const QString utc_text = utc.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString local_text = utc.toLocalTime().toString(QStringLiteral("HH:mm:ss"));
    if (time_display_ == "utc") {
        return utc_text + QStringLiteral(" UTC");
    }
    if (time_display_ == "local") {
        return utc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    return utc_text + QStringLiteral(" UTC (") + local_text + QStringLiteral(" local)");
}

QVariant CatalogueModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= rows_.size()) {
        return {};
    }
    const FrameEntry& entry = rows_[static_cast<std::size_t>(index.row())];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case TimeColumn:
            return time_text(entry.utc);
        case SessionColumn:
            return QString::fromStdString(entry.session_id);
        case FileColumn:
            return path_text(entry.path.filename());
        case FormatColumn:
            return QString::fromStdString(entry.format);
        case SizeColumn:
            return QStringLiteral("%1×%2, %3").arg(entry.width).arg(entry.height).arg(bytes_text(entry.bytes));
        case ExposureColumn:
            return optional_number(entry.exposure_ms, 2, QStringLiteral(" ms"));
        case GainColumn:
            return optional_number(entry.gain, 1);
        case MeanColumn:
            return optional_number(entry.mean, 1);
        case ClippedColumn:
            return entry.clipped_fraction.has_value()
                       ? QString::number(*entry.clipped_fraction * 100.0, 'f', 2) + QStringLiteral(" %")
                       : QStringLiteral("—");
        case SunColumn:
            return entry.sun_elevation_deg.has_value() ? QStringLiteral("%1° el, %2° az")
                                                             .arg(*entry.sun_elevation_deg, 0, 'f', 1)
                                                             .arg(entry.sun_azimuth_deg.value_or(0.0), 0, 'f', 1)
                                                       : QStringLiteral("—");
        case SimulatedColumn:
            return entry.simulated ? tr("simulated") : tr("measured");
        default:
            return {};
        }
    }
    if (role == Qt::ToolTipRole) {
        return path_text(entry.path);
    }
    if (role == Qt::DecorationRole && index.column() == FileColumn && thumbnails_enabled_) {
        const QImage image = thumbnail(index.row());
        if (!image.isNull()) {
            return QPixmap::fromImage(image);
        }
        request_thumbnail(index.row());
    }
    return {};
}

QVariant CatalogueModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    switch (section) {
    case TimeColumn:
        return tr("Time");
    case SessionColumn:
        return tr("Session");
    case FileColumn:
        return tr("Picture");
    case FormatColumn:
        return tr("Format");
    case SizeColumn:
        return tr("Size");
    case ExposureColumn:
        return tr("Exposure");
    case GainColumn:
        return tr("Gain");
    case MeanColumn:
        return tr("Mean");
    case ClippedColumn:
        return tr("Clipped");
    case SunColumn:
        return tr("Sun");
    case SimulatedColumn:
        return tr("Data");
    default:
        return {};
    }
}

void CatalogueModel::set_thumbnails_enabled(bool enabled)
{
    thumbnails_enabled_ = enabled;
}

QImage CatalogueModel::thumbnail(int row) const
{
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) {
        return {};
    }
    const QString key = path_text(rows_[static_cast<std::size_t>(row)].path);
    const QImage* cached = thumbnails_.object(key);
    return cached != nullptr ? *cached : QImage();
}

void CatalogueModel::request_thumbnail(int row) const
{
    const std::filesystem::path path = rows_[static_cast<std::size_t>(row)].path;
    const QString key = path_text(path);
    if (in_flight_.contains(key)) {
        return;
    }
    in_flight_.insert(key);
    const QPointer<CatalogueModel> self = self_;
    QThreadPool::globalInstance()->start([self, path, key, row] {
        QImage image = load_thumbnail(path, kThumbnailSide);
        if (image.isNull()) {
            image = QImage(1, 1, QImage::Format_RGB32);  // remembered as "not loadable": no second attempt
            image.fill(Qt::black);
        }
        if (self.isNull()) {
            return;
        }
        CatalogueModel* model = self.data();
        QMetaObject::invokeMethod(
            model,
            [model, key, image, row] {
                model->thumbnails_.insert(key, new QImage(image));
                model->in_flight_.remove(key);
                if (row < model->rowCount()) {
                    Q_EMIT model->thumbnailLoaded(row);
                    Q_EMIT model->dataChanged(model->index(row, FileColumn), model->index(row, FileColumn),
                                              {Qt::DecorationRole});
                }
            },
            Qt::QueuedConnection);
    });
}

Expected<ExportResult> export_frames(const std::vector<FrameEntry>& frames, const std::filesystem::path& folder)
{
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error) {
        return fail(ErrorCode::Io, "cannot create " + folder.string() + ": " + error.message());
    }
    ExportResult result;
    nlohmann::json files = nlohmann::json::array();
    for (const FrameEntry& frame : frames) {
        const std::filesystem::path target = folder / frame.path.filename();
        std::filesystem::copy_file(frame.path, target, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            log().warn("export: {} not copied: {}", frame.path.string(), error.message());
            ++result.failed;
            error.clear();
            continue;
        }
        ++result.files;
        nlohmann::json item{
            {"file", target.filename().string()}, {"sha256", frame.sha256},         {"bytes", frame.bytes},
            {"utc", format_iso8601(frame.utc)},   {"session_id", frame.session_id}, {"simulated", frame.simulated}};
        if (const auto sidecar = sidecar_of(frame.path)) {
            const std::filesystem::path sidecar_target = folder / sidecar->filename();
            std::filesystem::copy_file(*sidecar, sidecar_target, std::filesystem::copy_options::overwrite_existing,
                                       error);
            if (!error) {
                ++result.sidecars;
                item["sidecar"] = sidecar_target.filename().string();
            }
            error.clear();
        }
        files.push_back(item);
    }
    const nlohmann::json manifest{{"schema", "cloudscope.export/1"}, {"files", files}};
    result.manifest = folder / "export.json";
    if (auto written = write_file_atomically(result.manifest, manifest.dump(2) + "\n"); !written) {
        return fail(written.error());
    }
    return result;
}

SessionBrowser::SessionBrowser(std::filesystem::path session_root, const std::string& time_display, QWidget* parent)
    : QWidget(parent),
      session_root_(std::move(session_root)),
      model_(new CatalogueModel(this)),
      table_(new QTableView(this)),
      session_box_(new QComboBox(this)),
      format_box_(new QComboBox(this)),
      count_label_(new QLabel(this))
{
    setAccessibleName(QStringLiteral("Session browser"));
    auto* layout = new QVBoxLayout(this);
    auto* filters = new QGridLayout();
    session_box_->setAccessibleName(QStringLiteral("Session filter"));
    session_box_->setToolTip(tr("Show one session or all"));
    use_from_ = new QCheckBox(tr("From"), this);
    use_from_->setToolTip(tr("Only pictures taken at or after this time"));
    from_ = new QDateTimeEdit(QDateTime::currentDateTimeUtc().addDays(-1), this);
    from_->setAccessibleName(QStringLiteral("From time"));
    from_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    from_->setTimeZone(QTimeZone::UTC);
    use_to_ = new QCheckBox(tr("To"), this);
    use_to_->setToolTip(tr("Only pictures taken before this time"));
    to_ = new QDateTimeEdit(QDateTime::currentDateTimeUtc(), this);
    to_->setAccessibleName(QStringLiteral("To time"));
    to_->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    to_->setTimeZone(QTimeZone::UTC);
    format_box_->setAccessibleName(QStringLiteral("Format filter"));
    format_box_->addItem(tr("Any format"), QString());
    for (const char* format : {"png", "jpeg", "tiff16", "fits"}) {
        format_box_->addItem(QString::fromLatin1(format), QString::fromLatin1(format));
    }
    simulated_ = new QCheckBox(tr("Include simulated"), this);
    simulated_->setChecked(true);
    simulated_->setToolTip(tr("Pictures from simulated cameras are labelled; untick to hide them"));
    refresh_ = new QPushButton(tr("Refresh"), this);
    refresh_->setToolTip(tr("Read the catalogue again"));
    filters->addWidget(new QLabel(tr("Session"), this), 0, 0);
    filters->addWidget(session_box_, 0, 1, 1, 3);
    filters->addWidget(use_from_, 1, 0);
    filters->addWidget(from_, 1, 1);
    filters->addWidget(use_to_, 1, 2);
    filters->addWidget(to_, 1, 3);
    filters->addWidget(format_box_, 2, 0, 1, 2);
    filters->addWidget(simulated_, 2, 2);
    filters->addWidget(refresh_, 2, 3);
    layout->addLayout(filters);

    model_->set_time_display(time_display);
    table_->setModel(model_);
    table_->setAccessibleName(QStringLiteral("Frames table"));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setIconSize(QSize(kThumbnailSide, kThumbnailSide));
    table_->verticalHeader()->setDefaultSectionSize(kThumbnailSide + 4);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setAlternatingRowColors(true);
    layout->addWidget(table_, 1);

    auto* actions = new QHBoxLayout();
    compare_ = new QPushButton(tr("Compare two"), this);
    compare_->setToolTip(tr("Show two selected pictures side by side"));
    export_ = new QPushButton(tr("Export…"), this);
    export_->setToolTip(tr("Copy the selected pictures with their sidecars and a checksum manifest to a folder"));
    count_label_->setAccessibleName(QStringLiteral("Row count"));
    actions->addWidget(compare_);
    actions->addWidget(export_);
    actions->addStretch(1);
    actions->addWidget(count_label_);
    layout->addLayout(actions);

    connect(refresh_, &QPushButton::clicked, this, [this] {
        if (auto reopened = reopen(); !reopened) {
            Q_EMIT notice(tr("Catalogue not opened: %1").arg(QString::fromStdString(reopened.error().message)));
        }
    });
    connect(session_box_, &QComboBox::activated, this, [this](int) { apply_filter(); });
    connect(format_box_, &QComboBox::activated, this, [this](int) { apply_filter(); });
    connect(simulated_, &QCheckBox::toggled, this, [this](bool) { apply_filter(); });
    connect(use_from_, &QCheckBox::toggled, this, [this](bool) { apply_filter(); });
    connect(use_to_, &QCheckBox::toggled, this, [this](bool) { apply_filter(); });
    connect(from_, &QDateTimeEdit::editingFinished, this, [this] { apply_filter(); });
    connect(to_, &QDateTimeEdit::editingFinished, this, [this] { apply_filter(); });
    connect(compare_, &QPushButton::clicked, this, &SessionBrowser::compare_selected);
    connect(export_, &QPushButton::clicked, this, &SessionBrowser::export_selected);
    connect(table_, &QTableView::activated, this, [this](const QModelIndex& index) {
        if (index.isValid()) {
            Q_EMIT frameActivated(path_text(model_->entry(index.row()).path));
        }
    });
    const auto update_count = [this] {
        count_label_->setText(model_->canFetchMore(QModelIndex())
                                  ? tr("%1 pictures loaded, more on scroll").arg(model_->rowCount())
                                  : tr("%1 pictures").arg(model_->rowCount()));
    };
    connect(model_, &QAbstractItemModel::rowsInserted, this, update_count);
    connect(model_, &QAbstractItemModel::modelReset, this, update_count);
    if (auto opened = reopen(); !opened) {
        log().warn("catalogue not opened: {}", opened.error().to_string());
    }
}

Expected<void> SessionBrowser::reopen()
{
    auto opened = model_->open(session_root_ / "catalogue.sqlite");
    const QString selected = session_box_->currentData().toString();
    session_box_->clear();
    session_box_->addItem(tr("All sessions"), QString());
    for (const SessionInfo& session : model_->sessions()) {
        session_box_->addItem(QString::fromStdString(session.id), QString::fromStdString(session.id));
    }
    const int index = session_box_->findData(selected);
    session_box_->setCurrentIndex(std::max(index, 0));
    if (!opened) {
        return fail(opened.error());
    }
    apply_filter();
    return {};
}

void SessionBrowser::set_session_root(const std::filesystem::path& root)
{
    session_root_ = root;
    if (auto reopened = reopen(); !reopened) {
        Q_EMIT notice(tr("Catalogue not opened: %1").arg(QString::fromStdString(reopened.error().message)));
    }
}

void SessionBrowser::apply_filter()
{
    BrowserFilter filter;
    const QString session = session_box_->currentData().toString();
    if (!session.isEmpty()) {
        filter.session_id = session.toStdString();
    }
    if (use_from_->isChecked()) {
        filter.from = from_unix_ms(from_->dateTime().toMSecsSinceEpoch());
    }
    if (use_to_->isChecked()) {
        filter.to = from_unix_ms(to_->dateTime().toMSecsSinceEpoch());
    }
    filter.format = format_box_->currentData().toString().toStdString();
    filter.include_simulated = simulated_->isChecked();
    model_->set_filter(filter);
}

std::vector<FrameEntry> SessionBrowser::selected_frames() const
{
    std::vector<FrameEntry> frames;
    for (const QModelIndex& index : table_->selectionModel()->selectedRows()) {
        frames.push_back(model_->entry(index.row()));
    }
    std::ranges::sort(frames, [](const FrameEntry& a, const FrameEntry& b) { return a.utc < b.utc; });
    return frames;
}

void SessionBrowser::compare_selected()
{
    const std::vector<FrameEntry> frames = selected_frames();
    if (frames.size() != 2) {
        Q_EMIT notice(tr("Select exactly two pictures to compare"));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Compare"));
    auto* grid = new QGridLayout(&dialog);
    for (int i = 0; i < 2; ++i) {
        const FrameEntry& frame = frames[static_cast<std::size_t>(i)];
        auto* picture = new QLabel(&dialog);
        picture->setAccessibleName(tr("Picture %1").arg(i + 1));
        const QImage image = load_thumbnail(frame.path, 640);
        if (image.isNull()) {
            picture->setText(tr("Cannot read %1").arg(path_text(frame.path)));
        } else {
            picture->setPixmap(QPixmap::fromImage(image));
        }
        auto* text =
            new QLabel(tr("%1\n%2 UTC\nexposure %3, gain %4, mean %5")
                           .arg(path_text(frame.path.filename()), QString::fromStdString(format_iso8601(frame.utc)),
                                optional_number(frame.exposure_ms, 2, QStringLiteral(" ms")),
                                optional_number(frame.gain, 1), optional_number(frame.mean, 1)),
                       &dialog);
        grid->addWidget(picture, 0, i);
        grid->addWidget(text, 1, i);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    grid->addWidget(buttons, 2, 0, 1, 2);
    dialog.exec();
}

void SessionBrowser::export_selected()
{
    const std::vector<FrameEntry> frames = selected_frames();
    if (frames.empty()) {
        Q_EMIT notice(tr("Select the pictures to export first"));
        return;
    }
    const QString chosen = QFileDialog::getExistingDirectory(this, tr("Export folder"));
    if (chosen.isEmpty()) {
        return;
    }
    auto result = export_frames(frames, std::filesystem::path(chosen.toStdU16String()));
    if (!result) {
        Q_EMIT notice(tr("Export failed: %1").arg(QString::fromStdString(result.error().message)));
        return;
    }
    Q_EMIT notice(tr("Exported %1 pictures and %2 sidecars to %3 (%4 failed); manifest export.json")
                      .arg(result->files)
                      .arg(result->sidecars)
                      .arg(chosen)
                      .arg(result->failed));
}

}  // namespace cloudscope::desktop
