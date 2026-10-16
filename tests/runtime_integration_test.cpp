#include <atomic>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "qlog/log.hpp"
#include "qlog/runtime/appender/appender_base.hpp"
namespace {
struct entry {
    std::uint64_t id;
    std::int32_t category;
    std::string text;
};
std::mutex capture_lock;
std::vector<entry> captures;
std::atomic<bool> hold_consumer{false};
std::atomic<bool> consumer_held{false};
void QLOG_STDCALL capture(std::uint64_t id, std::int32_t category, qlog::LogLevel, const char* text,
                          std::int32_t length) {
    if (!id) {
        return;
    }
    if (hold_consumer.load(std::memory_order_acquire)) {
        consumer_held.store(true, std::memory_order_release);
        while (hold_consumer.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
    std::lock_guard<std::mutex> lock(capture_lock);
    captures.push_back({id, category, std::string(text, static_cast<std::size_t>(length))});
}
void check(bool value, const char* reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}
std::string config(const std::string& mode, const std::string& extra = "") {
    return "log.thread_mode=" + mode +
           "\nlog.buffer_size=65536\nlog.buffer_policy_when_full=block\n"
           "appenders_config.console.type=console\nappenders_config.console.levels=[all]\n" +
           extra;
}
void modes(const std::string& mode) {
    auto log = qlog::log::create_log("modes_" + mode, config(mode));
    check(log.is_valid(), "create mode");
    check(log.info("int={} double={:.2f} str={}", std::int32_t{42}, 1.25, "hello"),
          "UTF-8 record accepted");
    check(log.info(u"u16={} {}", u"世界", char32_t{0x1F600}), "UTF-16 record accepted");
    check(log.info(U"u32={} {}", U"中文", std::int32_t{7}), "UTF-32 format accepted as UTF-16");
    struct custom {
        const char* value;
        std::size_t log_format_str_size() const {
            return std::strlen(value);
        }
        const char* log_format_str_chars() const {
            return value;
        }
    };
    check(log.info(custom{"custom={}"}, custom{"value"}), "custom format and argument");
    log.force_flush();
    check(captures.size() == 4, "exactly four console records");
    check(captures[0].text.find("int=42 double=1.25 str=hello") != std::string::npos,
          "formatted arguments");
    check(captures[1].text.find("世界") != std::string::npos, "UTF-16 visible");
    check(captures[2].text.find("中文") != std::string::npos, "UTF-32 converted");
    check(captures[3].text.find("custom=value") != std::string::npos, "custom protocol visible");
    auto same = qlog::log::get_log_by_name(log.get_name());
    check(same.get_id() == log.get_id(), "public lookup identity");
}
void filters_reset() {
    auto base = config("sync",
                       "log.categories_mask=[A]\nappenders_config.console.categories_mask=[A]\n"
                       "snapshot.buffer_size=4096\nsnapshot.categories_mask=[A]\n");
    auto log = qlog::log::create_log("filters", base, {"", "A", "AB", "A.child"});
    check(log.is_valid(), "create filtered log");
    check(!log.is_enable_for(2, qlog::LogLevel::info), "Logger mask rejects AB for A");
    check(log.is_enable_for(3, qlog::LogLevel::info), "Logger accepts A.child");
    check(!log.do_log(2, qlog::LogLevel::info, "rejected AB"),
          "public filter applies before allocation");
    check(log.do_log(1, qlog::LogLevel::info, "accepted A"), "A accepted");
    check(log.do_log(3, qlog::LogLevel::info, "accepted child"), "child accepted");
    check(captures.size() == 2, "global and appender filtering");
    auto snapshot = log.take_snapshot("UTC");
    check(snapshot.find("accepted A") != std::string::npos &&
              snapshot.find("accepted child") != std::string::npos,
          "snapshot mask");
    check(qlog::log::reset_config("filters",
                                  config("async", "appenders_config.console.levels=[error]\n")),
          "reset config");
    check(log.get_thread_mode() == qlog::runtime::log_thread_mode::sync,
          "reset does not change mode");
    check(!log.info("filtered info"), "merged levels refreshed");
    check(log.error("visible error"), "new levels applied");
    log.set_appender_enable("con*", false);
    check(log.error("disabled target"), "enable is target decision");
    check(captures.size() == 3, "disabled target emits nothing");
    log.set_appender_enable("console", true);
    check(log.error("enabled again"), "target reenabled");
    check(captures.size() == 4, "reenabled target emits");
    // Appender's prefix rule differs from Logger's dotted descendant rule.
    auto prefix = qlog::log::create_log(
        "prefix_rule", config("sync", "appenders_config.console.categories_mask=[A]\n"),
        {"", "A", "AB", "A.child"});
    check(prefix.do_log(2, qlog::LogLevel::info, "AB allowed by Appender prefix"),
          "Appender prefix category");
    check(captures.size() == 5, "Appender permits AB with A prefix");
}
void concurrent() {
    auto log = qlog::log::create_log(
        "concurrent", config("independent", "log.high_perform_mode_freq_threshold_per_second=1\n"));
    check(log.is_valid(), "create concurrent");
    constexpr int producers = 4, records = 4000;
    std::atomic<int> accepted{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < producers; ++t) {
        threads.emplace_back([&, t] {
            if (log.info("warm={}", t)) {
                accepted.fetch_add(1, std::memory_order_relaxed);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            for (int i = 0; i < records; ++i) {
                if (log.info("producer={} sequence={}", t, i)) {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    log.force_flush();
    check(accepted.load() == producers * (records + 1), "all producer calls accepted");
    check(captures.size() == static_cast<std::size_t>(accepted.load()),
          "no missing or duplicate output");
    std::vector<std::vector<bool>> seen(producers, std::vector<bool>(records));
    std::vector<int> previous(producers, -1);
    for (const auto& item : captures) {
        auto pos = item.text.find("producer=");
        if (pos == std::string::npos) {
            continue;
        }
        int t = -1, i = -1;
        auto* end = item.text.data() + item.text.size();
        auto producer_parse = std::from_chars(item.text.data() + pos + 9, end, t);
        check(producer_parse.ec == std::errc{} &&
                  std::string_view(producer_parse.ptr,
                                   static_cast<std::size_t>(end - producer_parse.ptr))
                      .starts_with(" sequence="),
              "parse producer index");
        auto sequence_parse = std::from_chars(producer_parse.ptr + 10, end, i);
        check(sequence_parse.ec == std::errc{}, "parse producer sequence");
        check(t >= 0 && t < producers && i >= 0 && i < records, "sequence range");
        check(!seen[static_cast<std::size_t>(t)][static_cast<std::size_t>(i)],
              "duplicate sequence");
        check(i > previous[static_cast<std::size_t>(t)], "per-producer ordering");
        seen[static_cast<std::size_t>(t)][static_cast<std::size_t>(i)] = true;
        previous[static_cast<std::size_t>(t)] = i;
    }
    for (const auto& row : seen) {
        for (bool value : row) {
            check(value, "missing sequence");
        }
    }
}
void text_file() {
    auto root = std::filesystem::temp_directory_path() /
                ("qlog-runtime-" + std::to_string(qlog::platform::get_current_thread_id()));
    std::filesystem::create_directories(root);
    auto stem = (root / "text").generic_string();
    auto cfg =
        "log.thread_mode=async\nlog.buffer_policy_when_full=block\n"
        "appenders_config.file.type=text_file\nappenders_config.file.levels=[all]\n"
        "appenders_config.file.enable_rolling_log_file=false\nappenders_config.file.always_create_"
        "new_file=true\n"
        "appenders_config.file.max_file_size=512\nappenders_config.file.file_name=" +
        stem + "\n";
    auto log = qlog::log::create_log("text", cfg);
    check(log.is_valid(), "create text");
    for (int i = 0; i < 100; ++i) {
        check(log.info("text-sequence={}", i), "text accepted");
        if (i % 10 == 9) {
            log.force_flush();
        }
    }
    log.force_flush();
    std::string output;
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.path().extension() == ".log") {
            output += qlog::file_manager::read_all_text(entry.path().generic_string());
            ++files;
        }
    }
    check(files > 1, "file size rotation");
    std::size_t lines = 0, pos = 0;
    while ((pos = output.find("text-sequence=", pos)) != std::string::npos) {
        ++lines;
        ++pos;
    }
    check(lines == 100, "all text records visible after cache flush");
    qlog::log::uninit();
    // Runtime retains handles until process exit, matching reference globals. Do not remove open
    // files on Windows.
    std::cout << "text artifact " << root.generic_string() << '\n';
}
void oversize() {
    auto log = qlog::log::create_log(
        "oversize", config("async", "log.high_perform_mode_freq_threshold_per_second=0\n"));
    std::string large(128 * 1024, 'x');
    check(log.info("oversize={}", large), "oversize accepted");
    log.force_flush();
    check(captures.size() == 1, "oversize exactly one output");
    auto pos = captures[0].text.find("oversize=");
    check(pos != std::string::npos && captures[0].text.substr(pos + 9) == large,
          "oversize payload preserved");
}
void queue_policies() {
    for (const std::string policy : {"discard", "block", "expand"}) {
        captures.clear();
        auto cfg = config(
            "independent",
            "log.high_perform_mode_freq_threshold_per_second=0\nlog.buffer_policy_when_full=" +
                policy + "\n");
        auto log = qlog::log::create_log("policy_" + policy, cfg);
        hold_consumer.store(true);
        consumer_held.store(false);
        check(log.info("hold consumer"), "control record accepted");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!consumer_held.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() > deadline) {
                hold_consumer.store(false);
                throw std::runtime_error("consumer gate timeout");
            }
            std::this_thread::yield();
        }
        std::atomic<int> accepted{0}, rejected{0};
        std::string payload(1024, 'p');
        std::thread producer([&] {
            for (int i = 0; i < 500; ++i) {
                if (log.info("policy={} seq={} payload={}", policy.c_str(), i, payload)) {
                    ++accepted;
                } else {
                    ++rejected;
                }
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        hold_consumer.store(false, std::memory_order_release);
        producer.join();
        log.force_flush();
        check(accepted.load() + rejected.load() == 500, "attempted=accepted+rejected");
        check(captures.size() == static_cast<std::size_t>(accepted.load() + 1),
              "accepted records all emitted");
        if (policy == "discard") {
            check(rejected.load() > 0, "discard under full queue");
        } else {
            check(rejected.load() == 0, "block/expand retained records");
        }
    }
}
void unsupported_compressed() {
    using appender = qlog::runtime::appender_base;
    static_assert(appender::console == 0 && appender::text_file == 1 &&
                  appender::raw_file == 2 && appender::compressed_file == 3 &&
                  appender::type_count == 4);
    check(appender::get_config_name_by_type(appender::compressed_file) == "compressed_file",
          "reserved compressed configuration name");

    const auto output_dir = std::filesystem::temp_directory_path() /
                            ("qlog-unsupported-compressed-" + std::to_string(
                                 std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::string unsupported =
        "appenders_config.unavailable.type=compressed_file\n"
        "appenders_config.unavailable.levels=[all]\n"
        "appenders_config.unavailable.file_name=" + (output_dir / "out").generic_string() + "\n";
    auto mixed = qlog::log::create_log(
        "unsupported_compressed", config("sync", unsupported + "snapshot.buffer_size=4096\n"));
    check(mixed.is_valid(), "unsupported appender does not discard valid siblings");
    check(mixed.info("console-and-snapshot"), "valid console remains enabled");
    mixed.force_flush();
    check(captures.size() == 1 &&
              captures[0].text.find("console-and-snapshot") != std::string::npos,
          "console output survives unsupported compressed appender");
    check(mixed.take_snapshot("UTC").find("console-and-snapshot") != std::string::npos,
          "snapshot survives unsupported compressed appender");
    check(!std::filesystem::exists(output_dir), "unsupported appender creates no output files");

    check(qlog::log::reset_config(
              "unsupported_compressed",
              "log.thread_mode=sync\nsnapshot.buffer_size=4096\n" + unsupported),
          "reset follows existing partial-success rule");
    check(!mixed.info("filtered-without-appender"),
          "snapshot does not change existing appender-based admission");
    mixed.force_flush();
    check(captures.size() == 1, "compressed appender does not fall back to console");
    const auto retained_snapshot = mixed.take_snapshot("UTC");
    check(retained_snapshot.find("console-and-snapshot") != std::string::npos &&
              retained_snapshot.find("filtered-without-appender") == std::string::npos,
          "snapshot retains earlier data without admitting filtered records");

    auto only = qlog::log::create_log(
        "compressed_only", "log.thread_mode=sync\nsnapshot.buffer_size=0\n" + unsupported);
    check(only.is_valid(), "logger initialization keeps existing partial-success rule");
    check(!only.info("no-output"), "unsupported appender does not enable a fallback sink");

    check(qlog::log::reset_config("unsupported_compressed", config("sync")),
          "supported appender can be restored");
    check(mixed.info("console-restored"), "restored console accepts record");
    mixed.force_flush();
    check(captures.size() == 2 && captures.back().text.find("console-restored") != std::string::npos,
          "supported appender works after reset");
}
void snapshot_resize() {
    auto log =
        qlog::log::create_log("snapshot_resize", config("sync", "snapshot.buffer_size=4096\n"));
    for (int i = 0; i < 8; ++i) {
        check(log.info("before-resize={}", i), "snapshot before resize");
    }
    check(qlog::log::reset_config("snapshot_resize", config("sync", "snapshot.buffer_size=8192\n")),
          "snapshot resize reset");
    check(log.info("after-resize"), "snapshot after resize");
    auto text = log.take_snapshot("UTC");
    for (int i = 0; i < 8; ++i) {
        check(text.find("before-resize=" + std::to_string(i)) != std::string::npos,
              "snapshot transfer retained entry");
    }
    check(text.find("after-resize") != std::string::npos, "snapshot new entry");
}
void QLOG_STDCALL fetch_capture(void* pass, std::uint64_t, std::int32_t, qlog::LogLevel,
                                const char* value, std::int32_t length) {
    static_cast<std::vector<std::string>*>(pass)->emplace_back(value,
                                                               static_cast<std::size_t>(length));
}
void stack_and_console_buffer() {
    auto log = qlog::log::create_log("stack", config("sync", "log.print_stack_levels=[error]\n"));
    check(log.error("stack marker"), "stack record");
    check(captures.size() == 1 && captures[0].text.find("stack marker") != std::string::npos,
          "stack marker visible");
    check(captures[0].text.find('\n') != std::string::npos, "stack frames appended");
    qlog::log::set_console_buffer_enable(true);
    check(log.info("buffered console"), "console buffer insert");
    std::vector<std::string> fetched;
    while (qlog::log::fetch_and_remove_console_buffer(&fetch_capture, &fetched)) {
    }
    check(fetched.size() == 1 && fetched[0].find("buffered console") != std::string::npos,
          "console buffer consumed");
    qlog::log::set_console_buffer_enable(false);
}
}  // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "one case argument");
        qlog::log::register_console_callback(&capture);
        std::string group = argv[1];
        if (group == "sync" || group == "async" || group == "independent") {
            modes(group);
        } else if (group == "filters_reset") {
            filters_reset();
        } else if (group == "concurrent") {
            concurrent();
        } else if (group == "text_file") {
            text_file();
        } else if (group == "oversize") {
            oversize();
        } else if (group == "queue_policies") {
            queue_policies();
        } else if (group == "unsupported_compressed") {
            unsupported_compressed();
        } else if (group == "snapshot_resize") {
            snapshot_resize();
        } else if (group == "stack_console_buffer") {
            stack_and_console_buffer();
        } else {
            throw std::runtime_error("unknown case");
        }
        qlog::log::force_flush_all();
        qlog::log::unregister_console_callback(&capture);
        qlog::log::uninit();
        std::cout << "PASS " << group << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        qlog::log::uninit();
        return 1;
    }
}
