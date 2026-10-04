// Logging for the whole process (ADR-007).
//
//   logger("camera").info("opened {} at {}x{}", name, width, height);
//
// Every message passes a redaction filter (FR-SEC-06) and then goes to: standard error, a rotating log file,
// and an in-memory buffer that feeds the log console (FR-DSP-09) and the remote log stream (FR-REM-03).
// Timestamps are UTC. All functions are thread-safe.
#pragma once

#include "cloudscope/common/clock.hpp"
#include "cloudscope/common/error.hpp"

#include <spdlog/logger.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace cloudscope {

enum class LogLevel { Trace, Debug, Info, Warn, Error, Critical };

// "trace", "debug", "info", "warn", "error", "critical": the spelling used in configuration files.
[[nodiscard]] std::string_view to_string(LogLevel level);
[[nodiscard]] Expected<LogLevel> log_level_from_string(std::string_view text);

struct LogRecord {
    UtcTime time;
    LogLevel level = LogLevel::Info;
    std::string component;
    std::string message;  // already redacted
};

struct LoggingConfig {
    LogLevel level = LogLevel::Info;    // messages below this level are dropped
    bool console = true;                // write to standard error
    bool file = false;                  // write to <directory>/cloudscope.log
    std::filesystem::path directory;    // needed when `file` is true; created if missing
    int max_file_mb = 10;               // a log file is rotated when it would grow beyond this size
    int max_files = 5;                  // cloudscope.log plus rotated cloudscope.1.log ... (older ones are deleted)
    std::size_t memory_records = 2000;  // how many recent records the in-memory buffer keeps
};

inline constexpr std::string_view kLogFileName = "cloudscope.log";

// Sets up the outputs. Call at start-up; calling it again replaces the previous set-up (loggers stay valid).
// Fails if the log folder cannot be created or the log file cannot be opened; logging then keeps its
// previous set-up. Before the first call, messages of level Info and above go to standard error.
[[nodiscard]] Expected<void> init_logging(const LoggingConfig& config);

// Flushes and closes the log file. Loggers stay usable and write to standard error afterwards.
void shutdown_logging();

// The logger of one component, e.g. "camera", "recorder", "mount". The reference stays valid for the whole
// program; calling this repeatedly is cheap.
[[nodiscard]] spdlog::logger& logger(std::string_view component);

// The most recent records, oldest first.
[[nodiscard]] std::vector<LogRecord> recent_log_records();

// A listener is called for every new record, on the thread that logged it. It must return quickly;
// anything it logs itself is dropped. Returns a handle for remove_log_listener().
using LogListener = std::function<void(const LogRecord&)>;
int add_log_listener(LogListener listener);
void remove_log_listener(int handle);

// Secrets (FR-SEC-06). After registration the exact value never appears in any log output.
// Returns false and registers nothing for values shorter than 6 characters: they would also match ordinary text.
bool register_log_secret(std::string_view secret);

// The filter applied to every message: registered secrets, values of keys such as password, token, secret,
// api_key and Authorization, and passwords inside URLs are replaced by "[redacted]".
[[nodiscard]] std::string redact(std::string_view text);

// Sends Qt's own messages (qDebug, qWarning, ...) to the "qt" logger instead of standard error.
void route_qt_messages_to_log();

}  // namespace cloudscope
