#include "ai_models_dialog.hpp"

#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>

#include <fstream>

namespace cloudscope::desktop {

namespace {

QString text(const std::string& s)
{
    return QString::fromStdString(s);
}

QString folder_text(const std::filesystem::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

}  // namespace

AiModelsDialog::AiModelsDialog(std::filesystem::path store_root, const hal::DeviceRegistry& registry, QWidget* parent)
    : QDialog(parent),
      store_(std::move(store_root)),
      registry_(registry),
      ui_(this),
      worker_(std::make_unique<ThreadPool>("ai-benchmark", 1))
{
    setWindowTitle(tr("STRATIA models"));
    setAccessibleName(tr("STRATIA models dialog"));
    resize(820, 560);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Installed models in %1").arg(folder_text(store_.root())), this));
    table_ = new QTableWidget(0, 5, this);
    table_->setAccessibleName(tr("Installed models"));
    table_->setHorizontalHeaderLabels({tr("Model"), tr("Version"), tr("Active"), tr("Installed"), tr("Checksum")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table_, 1);
    auto* buttons = new QHBoxLayout();
    install_ = new QPushButton(tr("Install…"), this);
    install_->setToolTip(
        tr("Copy a model package (model_card.json and one .onnx) into the store, verifying its checksum"));
    activate_ = new QPushButton(tr("Activate"), this);
    activate_->setToolTip(tr("Load the selected model now and at every start"));
    rollback_ = new QPushButton(tr("Roll back"), this);
    rollback_->setToolTip(tr("Go back to the model that was active before the current one"));
    remove_ = new QPushButton(tr("Remove"), this);
    benchmark_ = new QPushButton(tr("Benchmark"), this);
    benchmark_->setToolTip(tr("Time the selected model on every available execution provider"));
    save_ = new QPushButton(tr("Save report…"), this);
    save_->setEnabled(false);
    for (QPushButton* b : {install_, activate_, rollback_, remove_, benchmark_, save_}) {
        buttons->addWidget(b);
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);
    message_ = new QLabel(this);
    message_->setWordWrap(true);
    message_->setAccessibleName(tr("Models message"));
    layout->addWidget(message_);
    output_ = new QPlainTextEdit(this);
    output_->setReadOnly(true);
    output_->setAccessibleName(tr("Benchmark result"));
    output_->setPlaceholderText(tr("The benchmark table appears here"));
    QFont mono = output_->font();
    mono.setFamily(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    output_->setFont(mono);
    layout->addWidget(output_, 1);

    connect(install_, &QPushButton::clicked, this, &AiModelsDialog::choose_and_install);
    connect(activate_, &QPushButton::clicked, this, [this] {
        if (auto done = activate_selected(); !done) {
            Q_EMIT notice(tr("Not activated: %1").arg(text(done.error().message)));
        }
    });
    connect(rollback_, &QPushButton::clicked, this, [this] {
        if (auto done = rollback(); !done) {
            Q_EMIT notice(tr("Not rolled back: %1").arg(text(done.error().message)));
        }
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (auto done = remove_selected(); !done) {
            Q_EMIT notice(tr("Not removed: %1").arg(text(done.error().message)));
        }
    });
    connect(benchmark_, &QPushButton::clicked, this, [this] { run_benchmark(); });
    connect(save_, &QPushButton::clicked, this, &AiModelsDialog::save_report);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &AiModelsDialog::update_buttons);
    refresh();
}

AiModelsDialog::~AiModelsDialog()
{
    worker_.reset();  // a running benchmark finishes before the widgets go
}

void AiModelsDialog::refresh()
{
    const std::optional<std::string> selected = selected_id();
    table_->setRowCount(0);
    auto models = store_.list();
    if (!models) {
        message_->setText(tr("Could not read the store: %1").arg(text(models.error().message)));
        update_buttons();
        return;
    }
    for (const ai::InstalledModel& model : *models) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* name = new QTableWidgetItem(text(model.name));
        name->setData(Qt::UserRole, text(model.id));
        table_->setItem(row, 0, name);
        table_->setItem(row, 1, new QTableWidgetItem(text(model.version)));
        table_->setItem(row, 2, new QTableWidgetItem(model.active ? tr("yes") : QString()));
        table_->setItem(row, 3, new QTableWidgetItem(text(model.installed_utc)));
        table_->setItem(row, 4,
                        new QTableWidgetItem(text(model.onnx_sha256.substr(0, 12)) +
                                             (model.checksum_declared ? tr(" (declared)") : tr(" (computed)"))));
    }
    const auto previous = store_.previous();
    message_->setText(previous && *previous ? tr("Previous model: %1 (Roll back returns to it)").arg(text(**previous))
                                            : tr("No previous model to roll back to"));
    if (selected) {
        select(*selected);
    } else if (table_->rowCount() > 0) {
        table_->selectRow(0);
    }
    update_buttons();
}

std::optional<std::string> AiModelsDialog::selected_id() const
{
    const QList<QTableWidgetItem*> items = table_->selectedItems();
    if (items.isEmpty()) {
        return std::nullopt;
    }
    return table_->item(items.front()->row(), 0)->data(Qt::UserRole).toString().toStdString();
}

void AiModelsDialog::select(const std::string& id)
{
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (table_->item(row, 0)->data(Qt::UserRole).toString().toStdString() == id) {
            table_->selectRow(row);
            return;
        }
    }
}

void AiModelsDialog::update_buttons()
{
    const bool selected = selected_id().has_value();
    activate_->setEnabled(selected);
    remove_->setEnabled(selected);
    benchmark_->setEnabled(selected && !benchmark_running_);
    const auto previous = store_.previous();
    rollback_->setEnabled(previous && previous->has_value());
}

Expected<ai::InstalledModel> AiModelsDialog::install_from(const std::filesystem::path& package, bool activate)
{
    auto installed = store_.install(package, activate);
    if (!installed) {
        return installed;
    }
    refresh();
    select(installed->id);
    Q_EMIT notice(tr("Model %1 %2 installed").arg(text(installed->name), text(installed->version)));
    if (activate) {
        Q_EMIT activeModelChanged(folder_text(installed->folder));
    }
    return installed;
}

Expected<void> AiModelsDialog::activate_selected()
{
    const std::optional<std::string> id = selected_id();
    if (!id) {
        return fail(ErrorCode::InvalidArgument, "no model selected");
    }
    if (auto done = store_.activate(*id); !done) {
        return done;
    }
    refresh();
    const auto model = store_.find(*id);
    if (model && *model) {
        Q_EMIT activeModelChanged(folder_text((*model)->folder));
    }
    return {};
}

Expected<void> AiModelsDialog::rollback()
{
    if (auto done = store_.rollback(); !done) {
        return done;
    }
    refresh();
    const auto active = store_.active();
    if (active && *active) {
        Q_EMIT activeModelChanged(folder_text((*active)->folder));
    }
    return {};
}

Expected<void> AiModelsDialog::remove_selected()
{
    const std::optional<std::string> id = selected_id();
    if (!id) {
        return fail(ErrorCode::InvalidArgument, "no model selected");
    }
    if (auto done = store_.remove(*id); !done) {
        return done;
    }
    refresh();
    return {};
}

std::optional<ai::InstalledModel> AiModelsDialog::benchmark_target() const
{
    if (const auto id = selected_id()) {
        const auto found = store_.find(*id);
        if (found && *found) {
            return **found;
        }
    }
    const auto active = store_.active();
    return active ? *active : std::nullopt;
}

void AiModelsDialog::run_benchmark(const ai::BenchmarkOptions& options)
{
    if (benchmark_running_) {
        return;
    }
    const std::optional<ai::InstalledModel> target = benchmark_target();
    if (!target) {
        Q_EMIT notice(tr("No model to benchmark: install and select one first"));
        return;
    }
    auto package = ai::open_model_package(target->folder);
    if (!package) {
        Q_EMIT notice(tr("Benchmark not started: %1").arg(text(package.error().message)));
        return;
    }
    benchmark_running_ = true;
    update_buttons();
    output_->setPlainText(tr("Benchmarking %1 %2 on every provider, %3 runs each…")
                              .arg(text(target->name), text(target->version))
                              .arg(options.runs));
    worker_->post([this, package = std::move(*package), options] {
        auto report = ai::benchmark_model(package, registry_, options);
        const QString markdown = report ? QString::fromStdString(ai::benchmark_markdown(*report))
                                        : tr("Benchmark failed: %1").arg(text(report.error().message));
        ui_.post([this, markdown] {
            benchmark_running_ = false;
            last_markdown_ = markdown;
            output_->setPlainText(markdown);
            save_->setEnabled(true);
            update_buttons();
            Q_EMIT benchmarkFinished(markdown);
        });
    });
}

void AiModelsDialog::choose_and_install()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose a model package folder"));
    if (folder.isEmpty()) {
        return;
    }
    if (auto installed = install_from(std::filesystem::path(folder.toStdU16String()), true); !installed) {
        Q_EMIT notice(tr("Not installed: %1").arg(text(installed.error().message)));
    }
}

void AiModelsDialog::save_report()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Save the benchmark report"),
                                                      QStringLiteral("ai_benchmark.md"), tr("Markdown (*.md)"));
    if (file.isEmpty()) {
        return;
    }
    std::ofstream out(std::filesystem::path(file.toStdU16String()));
    out << last_markdown_.toStdString();
    Q_EMIT notice(out ? tr("Benchmark report saved to %1").arg(file) : tr("Could not write %1").arg(file));
}

}  // namespace cloudscope::desktop
