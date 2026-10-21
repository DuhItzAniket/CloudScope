// Installed models and the inference benchmark (P070, P071): install a package into the model store with its
// checksum, activate one (the application loads it and remembers it in the configuration), roll back to the
// previous one, remove old ones; and time the active model on every execution provider.

#pragma once

#include <cloudscope/ai/benchmark.hpp>
#include <cloudscope/ai/model_store.hpp>
#include <cloudscope/common/executor.hpp>
#include <cloudscope/hal/registry.hpp>

#include <QtWidgets/QDialog>

#include <filesystem>
#include <memory>
#include <optional>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace cloudscope::desktop {

class AiModelsDialog : public QDialog {
    Q_OBJECT
public:
    AiModelsDialog(std::filesystem::path store_root, const hal::DeviceRegistry& registry, QWidget* parent = nullptr);
    ~AiModelsDialog() override;
    AiModelsDialog(const AiModelsDialog&) = delete;
    AiModelsDialog& operator=(const AiModelsDialog&) = delete;
    AiModelsDialog(AiModelsDialog&&) = delete;
    AiModelsDialog& operator=(AiModelsDialog&&) = delete;

    void refresh();
    [[nodiscard]] ai::ModelStore& store() { return store_; }
    [[nodiscard]] std::optional<std::string> selected_id() const;
    void select(const std::string& id);

    // The actions behind the buttons, callable without dialogs (tests, scripts).
    [[nodiscard]] Expected<ai::InstalledModel> install_from(const std::filesystem::path& package, bool activate);
    [[nodiscard]] Expected<void> activate_selected();
    [[nodiscard]] Expected<void> rollback();
    [[nodiscard]] Expected<void> remove_selected();
    // Benchmarks the selected model (or the active one) on a worker thread; benchmarkFinished carries the table.
    void run_benchmark(const ai::BenchmarkOptions& options = {});
    [[nodiscard]] bool benchmark_running() const { return benchmark_running_; }

    [[nodiscard]] QTableWidget* table() const { return table_; }
    [[nodiscard]] QPushButton* install_button() const { return install_; }
    [[nodiscard]] QPushButton* activate_button() const { return activate_; }
    [[nodiscard]] QPushButton* rollback_button() const { return rollback_; }
    [[nodiscard]] QPushButton* remove_button() const { return remove_; }
    [[nodiscard]] QPushButton* benchmark_button() const { return benchmark_; }
    [[nodiscard]] QPlainTextEdit* benchmark_output() const { return output_; }

Q_SIGNALS:
    void notice(const QString& text);
    void activeModelChanged(const QString& folder);  // the folder of the model to load now
    void benchmarkFinished(const QString& markdown);

private:
    void update_buttons();
    void choose_and_install();
    void save_report();
    [[nodiscard]] std::optional<ai::InstalledModel> benchmark_target() const;

    ai::ModelStore store_;
    const hal::DeviceRegistry& registry_;
    QtExecutor ui_;
    QTableWidget* table_ = nullptr;
    QPushButton* install_ = nullptr;
    QPushButton* activate_ = nullptr;
    QPushButton* rollback_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* benchmark_ = nullptr;
    QPushButton* save_ = nullptr;
    QLabel* message_ = nullptr;
    QPlainTextEdit* output_ = nullptr;
    QString last_markdown_;
    bool benchmark_running_ = false;
    std::unique_ptr<ThreadPool> worker_;  // the benchmark's thread; last member
};

}  // namespace cloudscope::desktop
