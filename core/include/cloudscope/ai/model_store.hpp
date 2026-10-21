// Installed STRATIA models (P071, FR-AI-08): a folder of model packages with checksums, one of them active, and
// the previous active one remembered so that an update can be rolled back.
//
//   <root>/<id>/model_card.json, <name>.onnx, install.json     one installed package (id = "<name>-<version>")
//   <root>/active.json                                          {"current": id, "previous": id}
//
// install() copies a package in after validating its card and verifying the ONNX file against the checksum its
// source declares (STRATIA's export_report.json `onnx_sha256`, or a `<file>.sha256` next to the model); without a
// declared checksum the computed one is recorded and every later verify() checks against it.

#pragma once

#include <cloudscope/ai/model_card.hpp>
#include <cloudscope/common/error.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cloudscope::ai {

struct InstalledModel {
    std::string id;
    std::string name;
    std::string version;
    std::filesystem::path folder;
    std::filesystem::path model_file;
    std::string onnx_sha256;
    std::string installed_utc;
    std::string source;              // where it was installed from
    bool checksum_declared = false;  // the source named the checksum (else computed at install)
    bool active = false;
};

[[nodiscard]] std::string model_id(const ModelCard& card);  // "<name>-<version>", file-name safe

class ModelStore {
public:
    explicit ModelStore(std::filesystem::path root);
    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

    [[nodiscard]] Expected<std::vector<InstalledModel>> list() const;
    [[nodiscard]] Expected<std::optional<InstalledModel>> find(const std::string& id) const;
    [[nodiscard]] Expected<InstalledModel> install(const std::filesystem::path& package, bool activate = false);
    [[nodiscard]] Expected<void> activate(const std::string& id);
    [[nodiscard]] Expected<std::optional<InstalledModel>> active() const;
    [[nodiscard]] Expected<std::optional<std::string>> previous() const;
    [[nodiscard]] Expected<void> rollback();                           // the previous active becomes active again
    [[nodiscard]] Expected<void> remove(const std::string& id);        // refused for the active model
    [[nodiscard]] Expected<void> verify(const std::string& id) const;  // files still match install.json

private:
    struct ActiveState {
        std::optional<std::string> current;
        std::optional<std::string> previous;
    };
    [[nodiscard]] Expected<ActiveState> read_active() const;
    [[nodiscard]] Expected<void> write_active(const ActiveState& state) const;
    [[nodiscard]] Expected<InstalledModel> read_installed(const std::filesystem::path& folder) const;

    std::filesystem::path root_;
};

[[nodiscard]] std::string file_sha256(const std::filesystem::path& file);

}  // namespace cloudscope::ai
