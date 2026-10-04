#include "cloudscope/common/log.hpp"

#include <QtCore/QString>
#include <QtCore/QtLogging>
#include <fmt/format.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <shared_mutex>
#include <system_error>
#include <unordered_map>

namespace cloudscope {

namespace {

constexpr std::string_view kRedacted = "[redacted]";
constexpr std::size_t kMinSecretLength = 6;
constexpr std::size_t kDefaultMemoryRecords = 2000;
// UTC time with the offset spelled out, level, thread id, component, message.
constexpr const char* kLinePattern = "%Y-%m-%dT%H:%M:%S.%e+00:00 %-8l %t [%n] %v";

constexpr std::array<std::pair<LogLevel, std::string_view>, 6> kLevelNames = {{
    {LogLevel::Trace, "trace"},
    {LogLevel::Debug, "debug"},
    {LogLevel::Info, "info"},
    {LogLevel::Warn, "warn"},
    {LogLevel::Error, "error"},
    {LogLevel::Critical, "critical"},
}};

spdlog::level::level_enum to_spdlog(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warn:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    case LogLevel::Critical:
        return spdlog::level::critical;
    }
    return spdlog::level::info;
}

LogLevel from_spdlog(spdlog::level::level_enum level)
{
    switch (level) {
    case spdlog::level::trace:
        return LogLevel::Trace;
    case spdlog::level::debug:
        return LogLevel::Debug;
    case spdlog::level::warn:
        return LogLevel::Warn;
    case spdlog::level::err:
        return LogLevel::Error;
    case spdlog::level::critical:
        return LogLevel::Critical;
    default:
        return LogLevel::Info;
    }
}

std::unique_ptr<spdlog::formatter> make_formatter()
{
    return std::make_unique<spdlog::pattern_formatter>(kLinePattern, spdlog::pattern_time_type::utc);
}

std::string display(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

// ----------------------------------------------------------------------------------- redaction

void replace_all(std::string& text, std::string_view what, std::string_view with)
{
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + with.size())) {
        text.replace(at, what.size(), with);
    }
}

bool mentions_credentials(std::string_view text)
{
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
    for (const std::string_view word : {"pass", "secret", "token", "key", "authorization", "bearer"}) {
        if (lower.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

class Redactor {
public:
    bool add(std::string_view secret)
    {
        if (secret.size() < kMinSecretLength) {
            return false;
        }
        const std::unique_lock lock(mutex_);
        if (std::find(secrets_.begin(), secrets_.end(), secret) == secrets_.end()) {
            secrets_.emplace_back(secret);
        }
        return true;
    }

    std::string apply(std::string_view text) const
    {
        static const auto kFlags = std::regex::ECMAScript | std::regex::icase | std::regex::optimize;
        // key = value, key: value, "key": "value", Authorization: Bearer value
        static const std::regex kKeyValue(
            R"(((?:password|passwd|secret|token|api[_-]?key|access[_-]?key|authorization)["']?\s*[:=]\s*))"
            R"((?:(?:bearer|basic)\s+)?("[^"]*"|'[^']*'|[^\s,;&]+))",
            kFlags);
        static const std::regex kBearer(R"((\bbearer\s+)[A-Za-z0-9._~+/=-]{8,})", kFlags);
        // scheme://user:password@host
        static const std::regex kUrlPassword(R"((\w+://[^/\s:@]+:)[^@\s/]+(@))", kFlags);

        std::string result(text);
        {
            const std::shared_lock lock(mutex_);
            for (const std::string& secret : secrets_) {
                replace_all(result, secret, kRedacted);
            }
        }
        if (mentions_credentials(result)) {
            result = std::regex_replace(result, kKeyValue, "$1[redacted]");
            result = std::regex_replace(result, kBearer, "$1[redacted]");
        }
        if (result.find("://") != std::string::npos) {
            result = std::regex_replace(result, kUrlPassword, "$1[redacted]$2");
        }
        return result;
    }

private:
    mutable std::shared_mutex mutex_;
    std::vector<std::string> secrets_;
};

// --------------------------------------------------------------------------------------- sinks

// Keeps the most recent records and notifies listeners. Listeners run without any lock held.
class MemorySink final : public spdlog::sinks::sink {
public:
    void log(const spdlog::details::log_msg& message) override
    {
        LogRecord record{std::chrono::time_point_cast<std::chrono::milliseconds>(message.time),
                         from_spdlog(message.level),
                         std::string(message.logger_name.data(), message.logger_name.size()),
                         std::string(message.payload.data(), message.payload.size())};
        std::vector<LogListener> listeners;
        {
            const std::lock_guard lock(mutex_);
            records_.push_back(record);
            while (records_.size() > capacity_) {
                records_.pop_front();
            }
            listeners.reserve(listeners_.size());
            for (const auto& [handle, listener] : listeners_) {
                listeners.push_back(listener);
            }
        }
        for (const LogListener& listener : listeners) {
            listener(record);
        }
    }
    void flush() override {}
    void set_pattern(const std::string& /*pattern*/) override {}
    void set_formatter(std::unique_ptr<spdlog::formatter> /*formatter*/) override {}

    void set_capacity(std::size_t capacity)
    {
        const std::lock_guard lock(mutex_);
        capacity_ = std::max<std::size_t>(capacity, 1);
        while (records_.size() > capacity_) {
            records_.pop_front();
        }
    }
    std::vector<LogRecord> snapshot()
    {
        const std::lock_guard lock(mutex_);
        return {records_.begin(), records_.end()};
    }
    int add_listener(LogListener listener)
    {
        const std::lock_guard lock(mutex_);
        listeners_.emplace(++last_handle_, std::move(listener));
        return last_handle_;
    }
    void remove_listener(int handle)
    {
        const std::lock_guard lock(mutex_);
        listeners_.erase(handle);
    }

private:
    std::mutex mutex_;
    std::deque<LogRecord> records_;
    std::size_t capacity_ = kDefaultMemoryRecords;
    std::map<int, LogListener> listeners_;
    int last_handle_ = 0;
};

// Appends to <directory>/cloudscope.log; when the file would exceed max_bytes it becomes cloudscope.1.log,
// the previous .1 becomes .2, and so on; the oldest is deleted. Uses std::filesystem paths throughout,
// so folders with non-ASCII names work on Windows.
class RotatingFileSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    static Expected<std::shared_ptr<RotatingFileSink>> open(const std::filesystem::path& directory,
                                                            std::uintmax_t max_bytes, int max_files)
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return fail(ErrorCode::Io,
                        fmt::format("cannot create log folder {}: {}", display(directory), error.message()));
        }
        auto sink = std::shared_ptr<RotatingFileSink>(new RotatingFileSink(directory, max_bytes, max_files));
        if (!sink->open_current()) {
            return fail(ErrorCode::Io, fmt::format("cannot open log file {} for writing", display(sink->path_for(0))));
        }
        return sink;
    }

protected:
    void sink_it_(const spdlog::details::log_msg& message) override
    {
        spdlog::memory_buf_t line;
        formatter_->format(message, line);
        if (size_ > 0 && size_ + line.size() > max_bytes_) {
            rotate();
        }
        stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
        if (!stream_) {
            stream_.clear();
            throw spdlog::spdlog_ex(fmt::format("cannot write to log file {}", display(path_for(0))));
        }
        size_ += line.size();
    }
    void flush_() override { stream_.flush(); }

private:
    RotatingFileSink(std::filesystem::path directory, std::uintmax_t max_bytes, int max_files)
        : directory_(std::move(directory)), max_bytes_(max_bytes), max_files_(max_files)
    {
    }

    // index 0: cloudscope.log, index n: cloudscope.<n>.log
    std::filesystem::path path_for(int index) const
    {
        const std::filesystem::path name(kLogFileName);
        if (index == 0) {
            return directory_ / name;
        }
        return directory_ / fmt::format("{}.{}{}", name.stem().string(), index, name.extension().string());
    }

    bool open_current()
    {
        const std::filesystem::path path = path_for(0);
        stream_.open(path, std::ios::binary | std::ios::app);
        std::error_code error;
        const std::uintmax_t existing = std::filesystem::file_size(path, error);
        size_ = error ? 0 : existing;
        return stream_.is_open();
    }

    void rotate()
    {
        stream_.close();
        std::error_code error;
        std::filesystem::remove(path_for(max_files_ - 1), error);
        for (int index = max_files_ - 2; index >= 0; --index) {
            if (std::filesystem::exists(path_for(index), error)) {
                std::filesystem::rename(path_for(index), path_for(index + 1), error);
            }
        }
        if (!open_current()) {
            throw spdlog::spdlog_ex(fmt::format("cannot reopen log file {}", display(path_for(0))));
        }
    }

    std::filesystem::path directory_;
    std::uintmax_t max_bytes_;
    int max_files_;
    std::ofstream stream_;
    std::uintmax_t size_ = 0;
};

// The one sink every logger writes to: redacts the message, then hands it to the current outputs.
class RootSink final : public spdlog::sinks::sink {
public:
    using Outputs = std::vector<spdlog::sink_ptr>;

    explicit RootSink(std::shared_ptr<Redactor> redactor) : redactor_(std::move(redactor)) {}

    void log(const spdlog::details::log_msg& message) override
    {
        // An output or a listener that logs would recurse without end: drop such messages.
        static thread_local bool busy = false;
        if (busy) {
            return;
        }
        struct Guard {
            Guard() { busy = true; }
            ~Guard() { busy = false; }
        } const guard;

        const std::string clean =
            redactor_->apply(std::string_view(message.payload.data(), message.payload.size()));
        spdlog::details::log_msg redacted = message;
        redacted.payload = clean;
        for (const spdlog::sink_ptr& output : *outputs()) {
            if (output->should_log(redacted.level)) {
                output->log(redacted);
            }
        }
    }
    void flush() override
    {
        for (const spdlog::sink_ptr& output : *outputs()) {
            output->flush();
        }
    }
    void set_pattern(const std::string& /*pattern*/) override {}
    void set_formatter(std::unique_ptr<spdlog::formatter> /*formatter*/) override {}

    void replace_outputs(Outputs outputs)
    {
        auto next = std::make_shared<const Outputs>(std::move(outputs));
        std::shared_ptr<const Outputs> previous;
        {
            const std::lock_guard lock(mutex_);
            previous = std::exchange(outputs_, std::move(next));
        }
        for (const spdlog::sink_ptr& output : *previous) {
            output->flush();
        }
    }

private:
    std::shared_ptr<const Outputs> outputs()
    {
        const std::lock_guard lock(mutex_);
        return outputs_;
    }

    std::shared_ptr<Redactor> redactor_;
    std::mutex mutex_;
    std::shared_ptr<const Outputs> outputs_ = std::make_shared<const Outputs>();
};

spdlog::sink_ptr make_console_sink()
{
    auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    sink->set_formatter(make_formatter());
    return sink;
}

// --------------------------------------------------------------------------------------- state

struct LogState {
    std::shared_ptr<Redactor> redactor = std::make_shared<Redactor>();
    std::shared_ptr<MemorySink> memory = std::make_shared<MemorySink>();
    std::shared_ptr<RootSink> root = std::make_shared<RootSink>(redactor);

    std::mutex mutex;  // guards loggers and level
    std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers;
    spdlog::level::level_enum level = spdlog::level::info;

    LogState() { root->replace_outputs({make_console_sink(), memory}); }
};

// Never destroyed: code that logs while the program shuts down must still find a valid object.
LogState& state()
{
    static LogState* const instance = new LogState;
    return *instance;
}

void qt_message_handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    spdlog::level::level_enum level = spdlog::level::info;
    switch (type) {
    case QtDebugMsg:
        level = spdlog::level::debug;
        break;
    case QtInfoMsg:
        level = spdlog::level::info;
        break;
    case QtWarningMsg:
        level = spdlog::level::warn;
        break;
    case QtCriticalMsg:
        level = spdlog::level::err;
        break;
    case QtFatalMsg:
        level = spdlog::level::critical;
        break;
    }
    const std::string_view category = context.category != nullptr ? context.category : "default";
    spdlog::logger& log = logger("qt");
    if (category == "default") {
        log.log(level, "{}", message.toStdString());
    } else {
        log.log(level, "{}: {}", category, message.toStdString());
    }
    if (type == QtFatalMsg) {
        log.flush();  // Qt aborts the program after a fatal message
    }
}

}  // namespace

std::string_view to_string(LogLevel level)
{
    for (const auto& [value, name] : kLevelNames) {
        if (value == level) {
            return name;
        }
    }
    return "info";
}

Expected<LogLevel> log_level_from_string(std::string_view text)
{
    for (const auto& [value, name] : kLevelNames) {
        if (name == text) {
            return value;
        }
    }
    return fail(ErrorCode::Parse,
                fmt::format("'{}' is not a log level (trace, debug, info, warn, error, critical)", text));
}

Expected<void> init_logging(const LoggingConfig& config)
{
    if (config.max_file_mb < 1 || config.max_files < 1) {
        return fail(ErrorCode::InvalidArgument, "logging: max_file_mb and max_files must be at least 1");
    }
    if (config.file && config.directory.empty()) {
        return fail(ErrorCode::InvalidArgument, "logging: a log folder is needed when file logging is on");
    }

    LogState& log_state = state();
    RootSink::Outputs outputs;
    if (config.console) {
        outputs.push_back(make_console_sink());
    }
    if (config.file) {
        const auto max_bytes = static_cast<std::uintmax_t>(config.max_file_mb) * 1024U * 1024U;
        auto file = RotatingFileSink::open(config.directory, max_bytes, config.max_files);
        if (!file) {
            return fail(file.error());
        }
        (*file)->set_formatter(make_formatter());
        outputs.push_back(std::move(*file));
    }
    log_state.memory->set_capacity(config.memory_records);
    outputs.push_back(log_state.memory);
    log_state.root->replace_outputs(std::move(outputs));

    const std::lock_guard lock(log_state.mutex);
    log_state.level = to_spdlog(config.level);
    for (const auto& [name, component_logger] : log_state.loggers) {
        component_logger->set_level(log_state.level);
    }
    return {};
}

void shutdown_logging()
{
    LogState& log_state = state();
    log_state.root->flush();
    log_state.root->replace_outputs({make_console_sink(), log_state.memory});
}

spdlog::logger& logger(std::string_view component)
{
    LogState& log_state = state();
    const std::lock_guard lock(log_state.mutex);
    std::string name(component);
    if (const auto found = log_state.loggers.find(name); found != log_state.loggers.end()) {
        return *found->second;
    }
    auto created = std::make_shared<spdlog::logger>(name, log_state.root);
    created->set_level(log_state.level);
    created->flush_on(spdlog::level::trace);  // the lines just before a crash are the ones that matter
    return *log_state.loggers.emplace(std::move(name), std::move(created)).first->second;
}

std::vector<LogRecord> recent_log_records()
{
    return state().memory->snapshot();
}

int add_log_listener(LogListener listener)
{
    return state().memory->add_listener(std::move(listener));
}

void remove_log_listener(int handle)
{
    state().memory->remove_listener(handle);
}

bool register_log_secret(std::string_view secret)
{
    return state().redactor->add(secret);
}

std::string redact(std::string_view text)
{
    return state().redactor->apply(text);
}

void route_qt_messages_to_log()
{
    qInstallMessageHandler(qt_message_handler);
}

}  // namespace cloudscope
