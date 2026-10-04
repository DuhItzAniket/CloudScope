#include <cloudscope/common/log.hpp>

#include "test_support.hpp"

#include <QtCore/QDebug>
#include <QtCore/QLoggingCategory>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace cloudscope;
using cloudscope::test::read_file;
using Catch::Matchers::ContainsSubstring;

namespace {

// A log folder inside a temporary workspace; logging is reset when the test ends, which closes the log file
// so that the folder can be deleted.
class LogWorkspace {
public:
    LogWorkspace() : directory_(workspace_.path("logs")) {}
    ~LogWorkspace() { shutdown_logging(); }
    LogWorkspace(const LogWorkspace&) = delete;
    LogWorkspace& operator=(const LogWorkspace&) = delete;

    const std::filesystem::path& directory() const { return directory_; }
    std::filesystem::path file(int index = 0) const
    {
        return directory_ / (index == 0 ? "cloudscope.log" : "cloudscope." + std::to_string(index) + ".log");
    }
    LoggingConfig config() const
    {
        LoggingConfig config;
        config.console = false;
        config.file = true;
        config.directory = directory_;
        return config;
    }

private:
    cloudscope::test::TempWorkspace workspace_;
    std::filesystem::path directory_;
};

bool contains_message(const std::vector<LogRecord>& records, const std::string& message)
{
    for (const LogRecord& record : records) {
        if (record.message == message) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("log levels have the names used in configuration files", "[common][log]")
{
    for (const LogLevel level : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error,
                                 LogLevel::Critical}) {
        CHECK(log_level_from_string(to_string(level)).value() == level);
    }
    CHECK(to_string(LogLevel::Warn) == "warn");
    CHECK(log_level_from_string("verbose").error().code == ErrorCode::Parse);
    CHECK(log_level_from_string("INFO").error().code == ErrorCode::Parse);  // names are lower case
}

TEST_CASE("secrets are removed from text", "[common][log][redaction]")
{
    const auto [text, expected] = GENERATE(table<std::string, std::string>({
        {"connecting with password=hunter2 to host", "connecting with password=[redacted] to host"},
        {"Password: hunter2", "Password: [redacted]"},
        {R"({"user": "asha", "password": "hunter2", "port": 8080})",
         R"({"user": "asha", "password": [redacted], "port": 8080})"},
        {"token = abc.def-123", "token = [redacted]"},
        {"GET /api/frames?session_token=abc123&limit=5", "GET /api/frames?session_token=[redacted]&limit=5"},
        {"api_key=K1; apikey=K2; api-key=K3", "api_key=[redacted]; apikey=[redacted]; api-key=[redacted]"},
        {"Authorization: Bearer eyJhbGciOi.payload.sig", "Authorization: [redacted]"},
        {"authorization: Basic dXNlcjpwYXNz", "authorization: [redacted]"},
        {"sent header 'Bearer eyJhbGciOiJIUzI1NiJ9'", "sent header 'Bearer [redacted]'"},
        {"client_secret='s3cr3t value' ok", "client_secret=[redacted] ok"},
        {"uploading to https://asha:p4ssw0rd@storage.example.org/bucket",
         "uploading to https://asha:[redacted]@storage.example.org/bucket"},
        {"frame 7 exposure 12.5 ms", "frame 7 exposure 12.5 ms"},
        {"3 tokens parsed, keyboard ready, bypass mode", "3 tokens parsed, keyboard ready, bypass mode"},
        {"see https://example.org:8443/path", "see https://example.org:8443/path"},
    }));
    CAPTURE(text);
    CHECK(redact(text) == expected);
    CHECK(redact(redact(text)) == expected);  // applying the filter twice changes nothing more
}

TEST_CASE("registered secret values are removed wherever they appear", "[common][log][redaction]")
{
    const std::string secret = "Zx9-unit-test-secret-value";
    CHECK(redact("value is " + secret) == "value is " + secret);  // unknown so far, and no tell-tale key
    REQUIRE(register_log_secret(secret));
    CHECK(redact("value is " + secret + ", twice: " + secret) == "value is [redacted], twice: [redacted]");
    CHECK(redact("embedded:" + secret + ":end") == "embedded:[redacted]:end");

    CHECK_FALSE(register_log_secret("abc"));  // too short: would also match ordinary text
    CHECK(redact("abc and abcdef") == "abc and abcdef");
}

TEST_CASE("log records reach the file and the memory buffer, redacted and with UTC timestamps", "[common][log]")
{
    const LogWorkspace workspace;
    REQUIRE(init_logging(workspace.config()).has_value());

    logger("camera").info("opened {} at {}x{}", "B0268", 4656, 3496);
    logger("remote").warn("login with token=abcdef123456 refused");
    logger("camera").debug("below the configured level");

    const std::string text = read_file(workspace.file());
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    REQUIRE(lines.size() == 2);  // flushed after every record; the debug record was dropped
    const std::regex format(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}\+00:00 (info|warning) +\d+ \[(camera|remote)\] .+$)");
    CHECK(std::regex_match(lines[0], format));
    CHECK(std::regex_match(lines[1], format));
    CHECK_THAT(lines[0], ContainsSubstring("[camera] opened B0268 at 4656x3496"));
    CHECK_THAT(lines[1], ContainsSubstring("[remote] login with token=[redacted] refused"));
    CHECK_THAT(text, !ContainsSubstring("abcdef123456"));

    // The timestamp in the file is UTC: it parses and is within a minute of the system clock.
    const auto written = parse_iso8601(lines[0].substr(0, 29));
    REQUIRE(written.has_value());
    const auto age = SystemClock().now_utc() - *written;
    CHECK(std::chrono::abs(age) < std::chrono::minutes(1));

    const std::vector<LogRecord> records = recent_log_records();
    REQUIRE(records.size() >= 2);
    const LogRecord& last = records.back();
    CHECK(last.component == "remote");
    CHECK(last.level == LogLevel::Warn);
    CHECK(last.message == "login with token=[redacted] refused");
    CHECK(std::chrono::abs(SystemClock().now_utc() - last.time) < std::chrono::minutes(1));
    CHECK_FALSE(contains_message(records, "below the configured level"));
}

TEST_CASE("the log level can be changed and applies to existing loggers", "[common][log]")
{
    const LogWorkspace workspace;
    spdlog::logger& log = logger("level-test");
    LoggingConfig config = workspace.config();

    config.level = LogLevel::Error;
    REQUIRE(init_logging(config).has_value());
    log.warn("warn while level is error");
    log.error("error while level is error");

    config.level = LogLevel::Trace;
    REQUIRE(init_logging(config).has_value());
    log.trace("trace while level is trace");

    const std::vector<LogRecord> records = recent_log_records();
    CHECK_FALSE(contains_message(records, "warn while level is error"));
    CHECK(contains_message(records, "error while level is error"));
    CHECK(contains_message(records, "trace while level is trace"));
    CHECK(&logger("level-test") == &log);  // the same logger object for the whole program
}

TEST_CASE("log files rotate by size and old files are deleted", "[common][log]")
{
    const LogWorkspace workspace;
    LoggingConfig config = workspace.config();
    config.max_file_mb = 1;
    config.max_files = 3;
    REQUIRE(init_logging(config).has_value());

    const std::string filler(1000, 'x');
    spdlog::logger& log = logger("rotation");
    for (int i = 0; i < 3600; ++i) {  // about 3.8 MB in total: more than three files of 1 MB hold
        log.info("{:05} {}", i, filler);
    }

    REQUIRE(std::filesystem::exists(workspace.file(0)));
    REQUIRE(std::filesystem::exists(workspace.file(1)));
    REQUIRE(std::filesystem::exists(workspace.file(2)));
    CHECK_FALSE(std::filesystem::exists(workspace.file(3)));
    for (int index = 0; index < 3; ++index) {
        CHECK(std::filesystem::file_size(workspace.file(index)) <= 1024U * 1024U);
    }
    // The newest record is in the current file, older ones in higher-numbered files; the oldest are gone.
    CHECK_THAT(read_file(workspace.file(0)), ContainsSubstring("] 03599 "));
    const std::string oldest_kept = read_file(workspace.file(2));
    CHECK_THAT(oldest_kept, !ContainsSubstring("] 00000 "));
    const std::string newer = read_file(workspace.file(1));
    CHECK(oldest_kept.substr(40, 60) < newer.substr(40, 60));  // record numbers increase from file 2 to file 1
}

TEST_CASE("an existing log file is continued, not truncated", "[common][log]")
{
    const LogWorkspace workspace;
    REQUIRE(init_logging(workspace.config()).has_value());
    logger("append").info("first run");
    shutdown_logging();
    REQUIRE(init_logging(workspace.config()).has_value());
    logger("append").info("second run");

    const std::string text = read_file(workspace.file());
    CHECK_THAT(text, ContainsSubstring("first run"));
    CHECK_THAT(text, ContainsSubstring("second run"));
    CHECK(text.find("first run") < text.find("second run"));
}

TEST_CASE("a bad logging configuration is refused and the previous set-up stays", "[common][log]")
{
    const LogWorkspace workspace;
    REQUIRE(init_logging(workspace.config()).has_value());

    LoggingConfig no_directory = workspace.config();
    no_directory.directory.clear();
    CHECK(init_logging(no_directory).error().code == ErrorCode::InvalidArgument);

    LoggingConfig zero_files = workspace.config();
    zero_files.max_files = 0;
    CHECK(init_logging(zero_files).error().code == ErrorCode::InvalidArgument);

    // A regular file where the log folder should be: the folder cannot be created.
    const std::filesystem::path blocker = workspace.directory() / "blocker";
    std::ofstream(blocker) << "x";
    LoggingConfig blocked = workspace.config();
    blocked.directory = blocker / "logs";
    CHECK(init_logging(blocked).error().code == ErrorCode::Io);

    logger("still").info("still logging to the first file");
    CHECK_THAT(read_file(workspace.file()), ContainsSubstring("still logging to the first file"));
}

TEST_CASE("the memory buffer keeps only the most recent records", "[common][log]")
{
    const LogWorkspace workspace;
    LoggingConfig config = workspace.config();
    config.file = false;
    config.memory_records = 5;
    REQUIRE(init_logging(config).has_value());

    for (int i = 0; i < 12; ++i) {
        logger("buffer").info("record {}", i);
    }
    const std::vector<LogRecord> records = recent_log_records();
    REQUIRE(records.size() == 5);
    CHECK(records.front().message == "record 7");
    CHECK(records.back().message == "record 11");
}

TEST_CASE("listeners receive new records and may log without causing recursion", "[common][log]")
{
    const LogWorkspace workspace;
    LoggingConfig config = workspace.config();
    config.file = false;
    REQUIRE(init_logging(config).has_value());

    std::vector<std::string> seen;
    const int handle = add_log_listener([&seen](const LogRecord& record) {
        seen.push_back(record.component + ": " + record.message);
        logger("listener").info("logging from inside a listener");  // dropped, must not recurse or deadlock
    });
    logger("events").info("one");
    logger("events").error("two with secret=hidden-value");
    remove_log_listener(handle);
    logger("events").info("three");

    CHECK(seen == std::vector<std::string>{"events: one", "events: two with secret=[redacted]"});
    CHECK_FALSE(contains_message(recent_log_records(), "logging from inside a listener"));
}

TEST_CASE("logging from several threads loses nothing and keeps lines whole", "[common][log]")
{
    const LogWorkspace workspace;
    REQUIRE(init_logging(workspace.config()).has_value());

    constexpr int kThreads = 8;
    constexpr int kPerThread = 500;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t] {
            spdlog::logger& log = logger("worker");
            for (int i = 0; i < kPerThread; ++i) {
                log.info("thread {} message {} end", t, i);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    std::istringstream stream(read_file(workspace.file()));
    const std::regex whole_line(R"(^\S+ info +\d+ \[worker\] thread \d message \d+ end\r?$)");
    int lines = 0;
    for (std::string line; std::getline(stream, line);) {
        CHECK(std::regex_match(line, whole_line));
        ++lines;
    }
    CHECK(lines == kThreads * kPerThread);
}

TEST_CASE("Qt messages are routed into the log", "[common][log]")
{
    const LogWorkspace workspace;
    LoggingConfig config = workspace.config();
    config.file = false;
    config.level = LogLevel::Debug;
    REQUIRE(init_logging(config).has_value());
    route_qt_messages_to_log();

    qWarning("disk almost full: %d %% used", 97);
    qDebug() << "plain debug text";
    const QLoggingCategory category("cloudscope.test");
    qCCritical(category) << "category message with password=opensesame";
    qInstallMessageHandler(nullptr);

    const std::vector<LogRecord> records = recent_log_records();
    REQUIRE(records.size() >= 3);
    const LogRecord& warning = records[records.size() - 3];
    CHECK(warning.component == "qt");
    CHECK(warning.level == LogLevel::Warn);
    CHECK(warning.message == "disk almost full: 97 % used");
    CHECK(records[records.size() - 2].level == LogLevel::Debug);
    CHECK(records[records.size() - 2].message == "plain debug text");
    CHECK(records.back().level == LogLevel::Error);
    CHECK(records.back().message == "cloudscope.test: category message with password=[redacted]");
}
