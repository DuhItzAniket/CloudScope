// The session browser (P040, FR-DAT-02): the frame catalogue of P030 as a table with thumbnails, filters by
// session, time and format, a side-by-side comparison of two pictures, and an export of a selection with sidecars
// and checksums. Rows are fetched in pages as the view scrolls, so a catalogue of 100,000 frames opens at once.
//
// Lives on the UI thread; the catalogue connection is opened and used here only (Qt SQL is thread-bound).
// Thumbnails are decoded on Qt's global thread pool.
#pragma once

#include <cloudscope/common/error.hpp>
#include <cloudscope/session/catalogue.hpp>

#include <QtCore/QAbstractTableModel>
#include <QtCore/QCache>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

class QComboBox;
class QDateTimeEdit;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;

namespace cloudscope::desktop {

struct BrowserFilter {
    std::optional<std::string> session_id = std::nullopt;
    std::optional<UtcTime> from = std::nullopt;
    std::optional<UtcTime> to = std::nullopt;
    std::string format;  // empty: any
    bool include_simulated = true;
};

class CatalogueModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column : std::uint8_t {
        TimeColumn = 0,
        SessionColumn,
        FileColumn,
        FormatColumn,
        SizeColumn,
        ExposureColumn,
        GainColumn,
        MeanColumn,
        ClippedColumn,
        SunColumn,
        SimulatedColumn,
        ColumnCount
    };
    static constexpr std::size_t kPageSize = 500;

    explicit CatalogueModel(QObject* parent = nullptr);

    // Opens the catalogue file; a missing file gives an empty model, not an error. Resets the rows.
    [[nodiscard]] Expected<void> open(const std::filesystem::path& file);
    void set_filter(const BrowserFilter& filter);  // resets and reloads the first page
    [[nodiscard]] const BrowserFilter& filter() const { return filter_; }
    void set_time_display(const std::string& display);  // "utc", "local", "both"
    [[nodiscard]] const FrameEntry& entry(int row) const { return rows_.at(static_cast<std::size_t>(row)); }
    [[nodiscard]] std::vector<SessionInfo> sessions() const;
    [[nodiscard]] std::size_t pages_loaded() const { return pages_; }
    [[nodiscard]] Catalogue* catalogue() const { return catalogue_.get(); }

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    // Thumbnails: requested lazily for the rows a view shows; decoded in the background.
    void set_thumbnails_enabled(bool enabled);
    [[nodiscard]] QImage thumbnail(int row) const;  // null while not loaded

Q_SIGNALS:
    void thumbnailLoaded(int row);

private:
    void reload();
    void request_thumbnail(int row) const;
    [[nodiscard]] QString time_text(UtcTime time) const;

    std::unique_ptr<Catalogue> catalogue_;
    BrowserFilter filter_;
    std::string time_display_ = "both";
    std::vector<FrameEntry> rows_;
    std::optional<UtcTime> cursor_;  // time of the oldest row fetched so far (the next page lies before it)
    bool more_ = false;
    std::size_t pages_ = 0;
    bool thumbnails_enabled_ = true;
    mutable QCache<QString, QImage> thumbnails_{400};
    mutable QSet<QString> in_flight_;
    QPointer<CatalogueModel> self_;  // handed to the thumbnail tasks: null once the model is gone
};

// Reads and downsizes a picture for a thumbnail (any thread; OpenCV codecs).
[[nodiscard]] QImage load_thumbnail(const std::filesystem::path& picture, int max_side);

struct ExportResult {
    int files = 0;     // pictures copied
    int sidecars = 0;  // sidecars copied
    int failed = 0;
    std::filesystem::path manifest;  // export.json with the checksums
};
// Copies the pictures (and their sidecars when present) into `folder` and writes a manifest with the catalogue's
// SHA-256 of each picture. Files that already exist are replaced.
[[nodiscard]] Expected<ExportResult> export_frames(const std::vector<FrameEntry>& frames,
                                                   const std::filesystem::path& folder);

class SessionBrowser : public QWidget {
    Q_OBJECT

public:
    SessionBrowser(std::filesystem::path session_root, const std::string& time_display, QWidget* parent = nullptr);

    [[nodiscard]] CatalogueModel* model() const { return model_; }
    [[nodiscard]] QTableView* table() const { return table_; }
    [[nodiscard]] Expected<void> reopen();  // (re)opens <session_root>/catalogue.sqlite
    [[nodiscard]] std::vector<FrameEntry> selected_frames() const;
    void set_session_root(const std::filesystem::path& root);

    void apply_filter();
    void compare_selected();
    void export_selected();

Q_SIGNALS:
    void notice(const QString& text);
    void frameActivated(const QString& path);

private:
    std::filesystem::path session_root_;
    CatalogueModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    QComboBox* session_box_ = nullptr;
    QDateTimeEdit* from_ = nullptr;
    QDateTimeEdit* to_ = nullptr;
    QCheckBox* use_from_ = nullptr;
    QCheckBox* use_to_ = nullptr;
    QComboBox* format_box_ = nullptr;
    QCheckBox* simulated_ = nullptr;
    QPushButton* refresh_ = nullptr;
    QPushButton* compare_ = nullptr;
    QPushButton* export_ = nullptr;
    QLabel* count_label_ = nullptr;
};

}  // namespace cloudscope::desktop
