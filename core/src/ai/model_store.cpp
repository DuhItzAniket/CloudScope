#include "cloudscope/ai/model_store.hpp"

#include "cloudscope/common/clock.hpp"

#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>

namespace cloudscope::ai {

namespace {

// The first line of a text file, empty when it cannot be read. (Reading the whole file through
// std::istreambuf_iterator trips GCC 14's -Wnull-dereference at -O3; getline stays in libstdc++.)
std::string first_line(const std::filesystem::path& file)
{
    std::ifstream in(file);
    std::string line;
    std::getline(in, line);
    return line;
}

Expected<nlohmann::json> read_json(const std::filesystem::path& file)
{
    std::ifstream in(file);
    if (!in) {
        return fail(ErrorCode::NotFound, fmt::format("could not read {}", file.string()));
    }
    try {
        return nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception& error) {
        return fail(ErrorCode::Parse, fmt::format("{}: {}", file.string(), error.what()));
    }
}

Expected<void> write_json(const std::filesystem::path& file, const nlohmann::json& document)
{
    std::ofstream out(file);
    out << document.dump(2) << '\n';
    if (!out) {
        return fail(ErrorCode::Io, fmt::format("could not write {}", file.string()));
    }
    return {};
}

std::string trim(const std::string& text)
{
    const char* const blanks = " \t\r\n";
    const auto first = text.find_first_not_of(blanks);
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(blanks);
    return text.substr(first, last - first + 1);
}

// The checksum the package's source declares for its ONNX file, if any.
std::optional<std::string> declared_checksum(const ModelPackage& package)
{
    const std::filesystem::path sidecar = package.model_file.string() + ".sha256";
    if (std::filesystem::is_regular_file(sidecar)) {
        const std::string text = trim(first_line(sidecar));
        const auto space = text.find_first_of(" \t");
        return text.substr(0, space == std::string::npos ? text.size() : space);
    }
    const std::filesystem::path report = package.folder / "export_report.json";
    if (std::filesystem::is_regular_file(report)) {
        auto document = read_json(report);
        if (document && document->contains("onnx_sha256")) {
            return document->at("onnx_sha256").get<std::string>();
        }
    }
    return std::nullopt;
}

}  // namespace

std::string file_sha256(const std::filesystem::path& file)
{
    QFile in(QString::fromStdU16String(file.u16string()));
    if (!in.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&in);
    return hash.result().toHex().toStdString();
}

std::string model_id(const ModelCard& card)
{
    std::string id = card.model_name + "-" + card.model_version;
    for (char& c : id) {
        const bool keep = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == '.';
        if (!keep) {
            c = '_';
        }
    }
    return id;
}

ModelStore::ModelStore(std::filesystem::path root) : root_(std::move(root)) {}

Expected<ModelStore::ActiveState> ModelStore::read_active() const
{
    const std::filesystem::path file = root_ / "active.json";
    if (!std::filesystem::exists(file)) {
        return ActiveState{};
    }
    auto document = read_json(file);
    if (!document) {
        return fail(document.error());
    }
    ActiveState state;
    if (document->contains("current") && document->at("current").is_string()) {
        state.current = document->at("current").get<std::string>();
    }
    if (document->contains("previous") && document->at("previous").is_string()) {
        state.previous = document->at("previous").get<std::string>();
    }
    return state;
}

Expected<void> ModelStore::write_active(const ActiveState& state) const
{
    nlohmann::json document;
    document["current"] = state.current ? nlohmann::json(*state.current) : nlohmann::json(nullptr);
    document["previous"] = state.previous ? nlohmann::json(*state.previous) : nlohmann::json(nullptr);
    return write_json(root_ / "active.json", document);
}

Expected<InstalledModel> ModelStore::read_installed(const std::filesystem::path& folder)
{
    auto document = read_json(folder / "install.json");
    if (!document) {
        return fail(document.error());
    }
    try {
        InstalledModel model;
        model.id = document->at("id").get<std::string>();
        model.name = document->at("name").get<std::string>();
        model.version = document->at("version").get<std::string>();
        model.folder = folder;
        model.model_file = folder / document->at("model_file").get<std::string>();
        model.onnx_sha256 = document->at("onnx_sha256").get<std::string>();
        model.installed_utc = document->at("installed_utc").get<std::string>();
        model.source = document->value("source", std::string());
        model.checksum_declared = document->value("checksum_declared", false);
        return model;
    } catch (const nlohmann::json::exception& error) {
        return fail(ErrorCode::Parse, fmt::format("{}: {}", (folder / "install.json").string(), error.what()));
    }
}

Expected<std::vector<InstalledModel>> ModelStore::list() const
{
    std::vector<InstalledModel> out;
    std::error_code error;
    if (!std::filesystem::is_directory(root_, error)) {
        return out;
    }
    auto state = read_active();
    if (!state) {
        return fail(state.error());
    }
    const ActiveState st = *state;
    for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
        if (!entry.is_directory() || !std::filesystem::is_regular_file(entry.path() / "install.json")) {
            continue;
        }
        auto model = read_installed(entry.path());
        if (!model) {
            return fail(model.error());
        }
        model->active = st.current.has_value() && st.current.value() == model->id;
        out.push_back(std::move(*model));
    }
    std::ranges::sort(out, {}, &InstalledModel::id);
    return out;
}

Expected<std::optional<InstalledModel>> ModelStore::find(const std::string& id) const
{
    auto models = list();
    if (!models) {
        return fail(models.error());
    }
    const auto it = std::ranges::find(*models, id, &InstalledModel::id);
    return it == models->end() ? std::optional<InstalledModel>{} : std::optional<InstalledModel>(*it);
}

Expected<InstalledModel> ModelStore::installed(const std::string& id) const
{
    auto found = find(id);
    if (!found) {
        return fail(found.error());
    }
    const std::optional<InstalledModel>& model = *found;
    if (!model.has_value()) {
        return fail(ErrorCode::NotFound, fmt::format("no installed model '{}'", id));
    }
    return model.value();
}

Expected<InstalledModel> ModelStore::install(const std::filesystem::path& package_path, bool activate_it)
{
    auto package = open_model_package(package_path);
    if (!package) {
        return fail(package.error());
    }
    const std::string computed = file_sha256(package->model_file);
    if (computed.empty()) {
        return fail(ErrorCode::Io, fmt::format("could not read {}", package->model_file.string()));
    }
    const std::optional<std::string> declared = declared_checksum(*package);
    if (declared && *declared != computed) {
        return fail(ErrorCode::Validation,
                    fmt::format("checksum mismatch for {}: the package declares {} but the file hashes to {}",
                                package->model_file.filename().string(), declared->substr(0, 12),
                                computed.substr(0, 12)));
    }
    const std::string id = model_id(package->card);
    const std::filesystem::path folder = root_ / id;
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (std::filesystem::exists(folder)) {
        std::filesystem::remove_all(folder, error);  // reinstalling the same version replaces it
    }
    std::filesystem::create_directories(folder, error);
    std::filesystem::copy_file(package->card_file, folder / "model_card.json",
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        return fail(ErrorCode::Io, fmt::format("could not copy the model card: {}", error.message()));
    }
    std::filesystem::copy_file(package->model_file, folder / package->model_file.filename(),
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        return fail(ErrorCode::Io, fmt::format("could not copy the model: {}", error.message()));
    }
    if (file_sha256(folder / package->model_file.filename()) != computed) {
        std::filesystem::remove_all(folder, error);
        return fail(ErrorCode::Io, "the copied model does not match its source");
    }
    const std::filesystem::path report = package->folder / "export_report.json";
    if (std::filesystem::is_regular_file(report)) {
        std::filesystem::copy_file(report, folder / "export_report.json",
                                   std::filesystem::copy_options::overwrite_existing, error);
    }
    InstalledModel model;
    model.id = id;
    model.name = package->card.model_name;
    model.version = package->card.model_version;
    model.folder = folder;
    model.model_file = folder / package->model_file.filename();
    model.onnx_sha256 = computed;
    model.installed_utc =
        format_iso8601(std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()));
    const std::u8string source = package->folder.u8string();
    model.source = std::string(source.begin(), source.end());  // UTF-8, whatever the platform's narrow encoding
    model.checksum_declared = declared.has_value();
    const nlohmann::json document{{"id", model.id},
                                  {"name", model.name},
                                  {"version", model.version},
                                  {"model_file", package->model_file.filename().string()},
                                  {"onnx_sha256", computed},
                                  {"card_sha256", file_sha256(folder / "model_card.json")},
                                  {"installed_utc", model.installed_utc},
                                  {"source", model.source},
                                  {"checksum_declared", model.checksum_declared}};
    if (auto written = write_json(folder / "install.json", document); !written) {
        return fail(written.error());
    }
    if (activate_it) {
        if (auto activated = activate(id); !activated) {
            return fail(activated.error());
        }
        model.active = true;
    }
    return model;
}

Expected<void> ModelStore::activate(const std::string& id)
{
    auto found = installed(id);
    if (!found) {
        return fail(found.error());
    }
    if (auto ok = verify(id); !ok) {
        return fail(ok.error());
    }
    auto state = read_active();
    if (!state) {
        return fail(state.error());
    }
    ActiveState st = *state;
    if (st.current.has_value() && st.current.value() == id) {
        return {};
    }
    if (st.current) {
        st.previous = st.current;
    }
    st.current = id;
    return write_active(st);
}

Expected<std::optional<InstalledModel>> ModelStore::active() const
{
    auto state = read_active();
    if (!state) {
        return fail(state.error());
    }
    const ActiveState st = *state;
    if (!st.current.has_value()) {
        return std::optional<InstalledModel>{};
    }
    return find(st.current.value());
}

Expected<std::optional<std::string>> ModelStore::previous() const
{
    auto state = read_active();
    if (!state) {
        return fail(state.error());
    }
    const ActiveState st = *state;
    return st.previous;
}

Expected<void> ModelStore::rollback()
{
    auto state = read_active();
    if (!state) {
        return fail(state.error());
    }
    ActiveState st = *state;
    if (!st.previous.has_value()) {
        return fail(ErrorCode::Unavailable, "no previous model to roll back to");
    }
    const std::string previous_id = st.previous.value();
    if (auto found = installed(previous_id); !found) {
        return fail(ErrorCode::NotFound, fmt::format("the previous model '{}' is no longer installed", previous_id));
    }
    if (auto ok = verify(previous_id); !ok) {
        return fail(ok.error());
    }
    std::swap(st.current, st.previous);
    return write_active(st);
}

Expected<void> ModelStore::remove(const std::string& id)
{
    auto state = read_active();
    if (!state) {
        return fail(state.error());
    }
    ActiveState st = *state;
    if (st.current.has_value() && st.current.value() == id) {
        return fail(ErrorCode::InvalidArgument,
                    fmt::format("'{}' is the active model; activate another one first", id));
    }
    auto found = installed(id);
    if (!found) {
        return fail(found.error());
    }
    std::error_code error;
    std::filesystem::remove_all(found->folder, error);
    if (error) {
        return fail(ErrorCode::Io, fmt::format("could not remove {}: {}", found->folder.string(), error.message()));
    }
    if (st.previous.has_value() && st.previous.value() == id) {
        st.previous.reset();
        return write_active(st);
    }
    return {};
}

Expected<void> ModelStore::verify(const std::string& id) const
{
    auto found = installed(id);
    if (!found) {
        return fail(found.error());
    }
    const InstalledModel& model = *found;
    const std::string now = file_sha256(model.model_file);
    if (now != model.onnx_sha256) {
        return fail(ErrorCode::Validation,
                    fmt::format("model '{}' is corrupt: {} hashes to {}, install.json recorded {}", id,
                                model.model_file.filename().string(), now.substr(0, 12),
                                model.onnx_sha256.substr(0, 12)));
    }
    const auto document = read_json(model.folder / "install.json");
    if (document && document->contains("card_sha256") &&
        file_sha256(model.folder / "model_card.json") != document->at("card_sha256").get<std::string>()) {
        return fail(ErrorCode::Validation, fmt::format("model '{}': its model card changed since installation", id));
    }
    return {};
}

}  // namespace cloudscope::ai
