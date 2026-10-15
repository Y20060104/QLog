#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>

#include "compressed_test_keys.hpp"
#include "qlog/encryption/aes.hpp"
#include "qlog/encryption/rsa.hpp"
#include "qlog/encryption/vernam.hpp"
#include "qlog/log.hpp"
#include "qlog/utility/aligned_allocator.hpp"
#include "qlog/utility/bounded_hash_cache.hpp"
#include "qlog/utility/hash.hpp"
#include "qlog/utility/log_utils.hpp"
#include "qlog/utility/utf_conversion.hpp"
#include "qlog/utility/utf_mixed.hpp"
namespace {
using bytes = std::vector<std::uint8_t>;
void check(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(why);
}
std::string as_utf8(const bytes& data, bool mixed) {
    if (!mixed) return std::string(data.begin(), data.end());
    // Independent UTF-Mixed split; suffix is copied into aligned UTF-16 storage.
    auto mark = std::find(data.begin(), data.end(), std::uint8_t{255});
    if (mark == data.end()) return std::string(data.begin(), data.end());
    std::string prefix(data.begin(), mark);
    auto remaining = static_cast<std::size_t>(data.end() - mark - 1);
    check((remaining & 1) == 0, "odd UTF-Mixed suffix");
    std::u16string tail(remaining / 2, u'\0');
    if (remaining) std::memcpy(tail.data(), &*(mark + 1), remaining);
    std::vector<char> output(tail.size() * 3 + 1);
    auto n =
        qlog::utility::utf16_to_utf8_sw(tail.data(), static_cast<std::uint32_t>(tail.size()),
                                        output.data(), static_cast<std::uint32_t>(output.size()));
    return prefix + std::string(output.data(), n);
}
struct reader {
    const bytes& data;
    std::size_t pos;
    std::size_t end;
    std::uint8_t u8() {
        check(pos < end, "unexpected end");
        return data[pos++];
    }
    bytes take(std::size_t n) {
        check(n <= end - pos, "truncated data");
        bytes v(data.begin() + static_cast<std::ptrdiff_t>(pos),
                data.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
        return v;
    }
    template <typename T>
    T little() {
        auto v = take(sizeof(T));
        T value;
        std::memcpy(&value, v.data(), sizeof(T));
        return value;
    }
    std::uint64_t vlq(bool header = false) {
        auto first = u8();
        if (header) first &= 127;
        unsigned length = 1;
        for (unsigned mask = 128; length < 9 && !(first & mask); mask >>= 1) ++length;
        std::uint64_t minimum = 0;
        for (unsigned k = 1; k < length; ++k) minimum += std::uint64_t{1} << (7 * k);
        std::uint64_t value =
            length == 9 ? 0 : (std::uint64_t(first - (1U << (8 - length))) << (8 * (length - 1)));
        for (unsigned j = 1; j < length; ++j) value |= std::uint64_t(u8()) << (8 * (j - 1));
        return minimum + value;
    }
};
struct argument {
    std::uint8_t tag;
    std::uint64_t value = 0;
    std::string text;
};
struct record {
    std::uint64_t epoch;
    std::string format;
    std::uint64_t tid;
    std::vector<argument> args;
};
struct parsed_file {
    std::vector<record> records;
    std::size_t formats = 0;
    std::size_t threads = 0;
    std::size_t segments = 0;
};
std::uint64_t unzig(std::uint64_t n) {
    return (n >> 1) ^ (std::uint64_t{0} - (n & 1));
}
parsed_file parse(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "open compressed output");
    bytes file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    reader global{file, 0, file.size()};
    check(global.little<std::uint32_t>() == 10 && global.u8() == 2, "version 10 compressed header");
    global.take(3);
    std::vector<std::string> formats;
    std::map<std::uint64_t, std::uint64_t> threads;
    std::uint64_t epoch = 0;
    parsed_file out;
    std::size_t segment = 8;
    while (segment < file.size()) {
        reader head{file, segment, file.size()};
        auto next = head.little<std::uint64_t>();
        auto kind = head.u8();
        auto encryption = head.u8();
        head.take(2);
        check(kind >= 1 && kind <= 3, "segment type");
        ++out.segments;
        auto segment_end = next == UINT64_MAX ? file.size() : static_cast<std::size_t>(next);
        check(segment_end <= file.size() && segment_end >= head.pos, "segment topology");
        if (encryption == 2) {
            auto fingerprint = head.little<std::uint64_t>();
            check(fingerprint != 0, "RSA key fingerprint");
            auto ciphertext = head.take(256);
            auto iv = head.take(16);
            auto cipher_key = head.take(32768);
            qlog::encryption::rsa::private_key key;
            check(qlog::encryption::rsa::parse_private_key_pem(compressed_test_keys::private_key,
                                                               key),
                  "parse fixture private key");
            bytes aes_key;
            check(qlog::encryption::rsa::decrypt(key, ciphertext, aes_key),
                  "decrypt AES session key");
            qlog::encryption::aes aes(qlog::encryption::aes::enum_cipher_mode::AES_CBC,
                                      qlog::encryption::aes::enum_key_bits::AES_256);
            bytes xor_key;
            check(aes.decrypt(aes_key, iv, cipher_key, xor_key) && xor_key.size() == 32768,
                  "decrypt XOR key blob");
            for (std::size_t i = head.pos; i < segment_end; ++i) file[i] ^= xor_key[i & 32767];
        } else
            check(encryption == 1, "plaintext or reference encryption");
        reader payload{file, head.pos, segment_end};
        if (segment == 8) {
            check(payload.u8() == 2 && payload.u8() == 2 && payload.u8() == 7, "payload magic");
            payload.take(45);
            auto count = payload.little<std::uint32_t>();
            check(count == 2, "category count");
            for (std::uint32_t i = 0; i < count; ++i) {
                auto n = payload.little<std::uint32_t>();
                payload.take(n);
            }
        }
        while (payload.pos < payload.end) {
            auto first = file[payload.pos];
            bool entry = (first & 128) != 0;
            std::uint64_t size;
            if ((first & 127) == 0) {
                payload.u8();
                size = payload.vlq();
            } else
                size = payload.vlq(true);
            check(size <= payload.end - payload.pos, "item body bound");
            reader item{file, payload.pos, payload.pos + static_cast<std::size_t>(size)};
            payload.pos = item.end;
            if (!entry) {
                auto subtype = item.u8();
                if (subtype == 0 || subtype == 2) {
                    check(item.u8() <= 5, "level");
                    check(item.vlq() < 2, "category index");
                    formats.push_back(as_utf8(item.take(item.end - item.pos), subtype == 2));
                    ++out.formats;
                } else {
                    check(subtype == 1, "template subtype");
                    auto idx = item.vlq();
                    threads[idx] = item.vlq();
                    item.take(item.end - item.pos);
                    ++out.threads;
                }
                continue;
            }
            epoch += unzig(item.vlq());
            auto fmt = item.vlq();
            auto tid = item.vlq();
            check(fmt < formats.size() && threads.contains(tid), "template reference");
            record rec{epoch, formats[static_cast<std::size_t>(fmt)], threads.at(tid), {}};
            while (item.pos < item.end) {
                argument arg{item.u8(), 0, {}};
                switch (arg.tag) {
                    case 1:
                        break;
                    case 3:
                    case 4:
                    case 8:
                        arg.value = item.u8();
                        break;
                    case 7:
                        arg.value = static_cast<std::uint64_t>(
                            static_cast<std::int64_t>(static_cast<std::int8_t>(item.u8())));
                        break;
                    case 5:
                    case 6:
                    case 10:
                    case 12:
                    case 14:
                        arg.value = item.vlq();
                        break;
                    case 9:
                    case 11:
                    case 13:
                        arg.value = unzig(item.vlq());
                        break;
                    case 2:
                    case 16:
                        arg.value = item.little<std::uint64_t>();
                        break;
                    case 15:
                        arg.value = item.little<std::uint32_t>();
                        break;
                    case 17:
                    case 20: {
                        auto len = item.vlq();
                        if (len && file[item.pos] == 0) item.u8();
                        arg.text = as_utf8(item.take(static_cast<std::size_t>(len)), arg.tag == 20);
                        break;
                    }
                    default:
                        throw std::runtime_error("invalid compressed argument tag");
                }
                rec.args.push_back(std::move(arg));
            }
            out.records.push_back(std::move(rec));
        }
        if (next == UINT64_MAX) break;
        check(next > segment, "segment forward link");
        segment = static_cast<std::size_t>(next);
    }
    return out;
}
std::filesystem::path root(const std::string& group) {
    auto path = std::filesystem::current_path() / "compressed-output" /
                ("qlog-compressed-" + group + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
    std::cout << "ARTIFACT " << path.generic_string() << '\n';
    return path;
}
std::string config(const std::filesystem::path& path, const std::string& mode, bool fresh = true,
                   bool encrypted = false, const std::string& extra = "") {
    return "log.thread_mode=" + mode +
           "\nlog.recovery=false\nlog.buffer_size=65536\nlog.buffer_policy_when_full=block\nlog."
           "high_perform_mode_freq_threshold_per_second=0\nappenders_config.file.type=compressed_"
           "file\nappenders_config.file.levels=[all]\nappenders_config.file.file_name=" +
           (path / "events").generic_string() +
           "\nappenders_config.file.base_dir_type=0\nappenders_config.file.always_create_new_"
           "file=" +
           (fresh ? "true" : "false") +
           "\nappenders_config.file.enable_rolling_log_file=false\nappenders_config.file.time_zone="
           "UTC\n" +
           (encrypted ? std::string("appenders_config.file.pub_key=") +
                            compressed_test_keys::public_key + "\n"
                      : "") +
           extra;
}
std::vector<std::filesystem::path> files(const std::filesystem::path& path) {
    std::vector<std::filesystem::path> out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(path))
        if (e.is_regular_file() && e.path().extension() == ".logcompr") out.push_back(e.path());
    std::sort(out.begin(), out.end());
    check(!out.empty(), "compressed file produced");
    return out;
}
void primitives() {
    using utils = qlog::utility::log_utils;
    std::array<std::uint8_t, 16> encoded{};
    for (unsigned length = 1; length <= 9; ++length) {
        auto low = utils::vlq::get_vlq_data_min_size_by_encode_length(length);
        auto high = utils::vlq::get_vlq_data_max_size_by_encode_length(length);
        for (auto value : {low, high}) {
            auto n = utils::vlq::vlq_encode(value, encoded.data(), encoded.size());
            check(n == length, "VLQ threshold width");
            std::uint64_t round = 0;
            check(utils::vlq::vlq_decode(round, encoded.data()) == n && round == value,
                  "VLQ threshold roundtrip");
            bytes data(encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(n));
            reader independent{data, 0, data.size()};
            check(independent.vlq() == value, "VLQ independent decoder");
        }
    }
    for (auto value : {INT64_MIN, INT64_C(-1), INT64_C(0), INT64_C(1), INT64_MAX})
        check(utils::zigzag::decode(utils::zigzag::encode(value)) == value,
              "ZigZag signed boundary");
    for (const std::u16string& input :
         {std::u16string{}, std::u16string(100, u'x'), std::u16string(u"abc中文😀tail")}) {
        std::vector<char> mixed(input.size() * 3 + 1);
        auto n = qlog::utility::utf16_to_utf_mixed(
            input.data(), static_cast<std::uint32_t>(input.size()), mixed.data(),
            static_cast<std::uint32_t>(mixed.size()));
        check(qlog::utility::hash_utf_mixed_as_utf16(mixed.data(), n) ==
                  qlog::utility::get_hash_64(input.data(), input.size() * 2),
              "UTF-Mixed recovers original format hash");
        std::vector<char> utf(input.size() * 3 + 1), expected(input.size() * 3 + 1);
        auto a = qlog::utility::utf_mixed_to_utf8(mixed.data(), n, utf.data(),
                                                  static_cast<std::uint32_t>(utf.size()));
        auto z = qlog::utility::utf16_to_utf8_sw(
            input.data(), static_cast<std::uint32_t>(input.size()), expected.data(),
            static_cast<std::uint32_t>(expected.size()));
        check(a == z && std::equal(utf.begin(), utf.begin() + a, expected.begin()),
              "UTF-Mixed content");
    }
    qlog::utility::bounded_hash_cache<64> cache(8);
    for (std::uint64_t k = 0; k < 8; ++k) cache.insert(k, static_cast<std::uint32_t>(k));
    for (std::uint64_t k = 0; k < 8; ++k) {
        std::uint32_t v = UINT32_MAX;
        decltype(cache)::insert_token token;
        check(cache.find(k, v, token) && v == k, "bounded cache resident");
    }
    for (std::uint64_t k = 8; k < 72; ++k) cache.insert(k, static_cast<std::uint32_t>(k));
    unsigned residents = 0;
    for (std::uint64_t k = 0; k < 72; ++k) {
        std::uint32_t v;
        decltype(cache)::insert_token token;
        if (cache.find(k, v, token)) {
            ++residents;
            check(v == k, "cache payload");
        }
    }
    check(residents == 8, "hard cache entry limit");
    check(cache.set_max_size(32), "grow cache limit");
    check(!cache.set_max_size(8), "occupied cache refuses shrink");
    cache.clear();
    check(cache.set_max_size(4) && cache.get_max_size() == 8, "cache minimum clamp");
    using aes = qlog::encryption::aes;
    bytes key = {0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe, 0x2b, 0x73, 0xae,
                 0xf0, 0x85, 0x7d, 0x77, 0x81, 0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61,
                 0x08, 0xd7, 0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4};
    bytes iv = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    bytes plain = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
                   0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a};
    bytes expected = {0xf5, 0x8c, 0x4c, 0x04, 0xd6, 0xe5, 0xf1, 0xba,
                      0x77, 0x9e, 0xab, 0xfb, 0x5f, 0x7b, 0xfb, 0xd6};
    bytes cipher, round;
    aes algorithm(aes::enum_cipher_mode::AES_CBC, aes::enum_key_bits::AES_256);
    check(algorithm.encrypt(key, iv, plain, cipher) && cipher == expected,
          "AES-256-CBC known vector");
    check(algorithm.decrypt(key, iv, cipher, round) && round == plain, "AES decrypt vector");
    qlog::encryption::rsa::public_key pub;
    qlog::encryption::rsa::private_key priv;
    check(qlog::encryption::rsa::parse_public_key_ssh(compressed_test_keys::public_key, pub) &&
              pub.n_.size() == 256,
          "parse original 2048-bit SSH key");
    check(qlog::encryption::rsa::parse_private_key_pem(compressed_test_keys::private_key, priv),
          "parse original PEM key");
    bytes message = {0x42, 0x51, 0x4c, 0x6f, 0x67};
    check(qlog::encryption::rsa::encrypt(pub, message, cipher) &&
              qlog::encryption::rsa::decrypt(priv, cipher, round) && round == message,
          "RSA fixture roundtrip");
    bytes zero{0};
    check(!qlog::encryption::rsa::encrypt(pub, zero, cipher),
          "reference rejects leading-zero plaintext");
    // Upstream zero-integer conversion copies beyond the provided input span.
    // No deterministic result or in-bounds guarantee is assigned to that path.

    alignas(32) std::array<std::uint8_t, 64> xor_key{};
    alignas(32) std::array<std::uint8_t, 4096> buffer{};
    for (std::size_t i = 0; i < xor_key.size(); ++i)
        xor_key[i] = static_cast<std::uint8_t>(i * 3 + 1);
    for (std::size_t off = 0; off < 32; ++off) {
        buffer.fill(0x5a);
        qlog::encryption::vernam::vernam_encrypt_32bytes_aligned(
            buffer.data() + off, buffer.size() - off, xor_key.data(), xor_key.size(), off);
        for (std::size_t i = off; i < buffer.size(); ++i)
            check(buffer[i] == std::uint8_t(0x5a ^ xor_key[i % 64]),
                  "XOR independent byte oracle and key wrap");
        qlog::encryption::vernam::vernam_encrypt_32bytes_aligned(
            buffer.data() + off, buffer.size() - off, xor_key.data(), xor_key.size(), off);
        check(std::all_of(buffer.begin(), buffer.end(), [](auto v) { return v == 0x5a; }),
              "XOR restoration");
    }
}
void scalars() {
    auto dir = root("scalars");
    auto logger = qlog::log::create_log("compressed_scalars", config(dir, "sync"), {"", "child"});
    check(logger.is_valid(), "create scalar logger");
    check(logger.info(U"scalars {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {}", nullptr,
                      reinterpret_cast<void*>(std::uintptr_t{0x12345678}), true, 'A',
                      char16_t{0x4e2d}, char32_t{0x1f600}, std::int8_t{-128}, std::uint8_t{255},
                      std::int16_t{-32768}, std::uint16_t{65535}, INT32_MIN, UINT32_MAX, INT64_MIN,
                      UINT64_MAX, 1.5f, -2.25, "utf8", u"u16😀", U"u32正文"),
          "all primitive types accepted");
    logger.force_flush();
    qlog::log::uninit();
    auto parsed = parse(files(dir).front());
    check(parsed.records.size() == 1, "scalar record count");
    const auto& args = parsed.records[0].args;
    std::vector<std::uint8_t> tags = {1,  2,  3,  4,  5,  6,  7,  8,  9, 10,
                                      11, 12, 13, 14, 15, 16, 17, 20, 20};
    check(args.size() == tags.size(), "scalar argument count");
    for (std::size_t i = 0; i < tags.size(); ++i)
        check(args[i].tag == tags[i], "scalar argument tag");
    check(args[1].value == 0x12345678 && args[2].value == 1 && args[3].value == 65 &&
              args[4].value == 0x4e2d && args[5].value == 0x1f600,
          "pointer and character values");
    check(static_cast<std::int64_t>(args[6].value) == -128 && args[7].value == 255 &&
              static_cast<std::int64_t>(args[8].value) == -32768 && args[9].value == 65535,
          "small integer bounds");
    check(static_cast<std::int64_t>(args[10].value) == INT32_MIN && args[11].value == UINT32_MAX &&
              args[12].value == static_cast<std::uint64_t>(INT64_MIN) &&
              args[13].value == UINT64_MAX,
          "large integer bounds");
    check(args[14].value == std::bit_cast<std::uint32_t>(1.5f) &&
              args[15].value == std::bit_cast<std::uint64_t>(-2.25),
          "floating point raw bits");
    check(args[16].text == "utf8" && args[17].text == "u16😀" && args[18].text == "u32正文",
          "scalar string encodings");
}
void integration(const std::string& group) {
    auto dir = root(group);
    bool encrypted = group == "encrypted";
    bool rolling = group == "rollover";
    auto mode = (group == "async" || group == "independent") ? group : "sync";
    auto cfg = config(dir, mode, true, encrypted,
                      rolling ? "appenders_config.file.enable_rolling_log_file=true\nappenders_"
                                "config.file.max_file_size=4096\n"
                              : "");
    auto logger = qlog::log::create_log("compressed_" + group, cfg, {"", "child"});
    check(logger.is_valid(), "create compressed logger");
    int producers = group == "independent" ? 4 : 1;
    int count = rolling ? 300 : 600;
    std::vector<std::thread> threads;
    for (int p = 0; p < producers; ++p)
        threads.emplace_back([&, p] {
            for (int i = 0; i < count; ++i) {
                bool accepted;
                if ((i & 1) == 0)
                    accepted = logger.info("event p={} seq={} text={}", p, i, "hello-中文");
                else
                    accepted = logger.info(
                        u"wide p={} seq={} text={}", p, i,
                        i % 3 == 0 ? std::u16string(50, u'x') : std::u16string(u"ASCII后缀😀"));
                check(accepted, "compressed record accepted");
                if (rolling && i % 10 == 0) logger.force_flush();
            }
        });
    for (auto& thread : threads) thread.join();
    logger.force_flush();
    if (group == "cache_reset") {
        auto changed = cfg +
                       "appenders_config.file.format_template_cache_max_entries=8\nappenders_"
                       "config.file.thread_info_cache_max_entries=8\n";
        check(qlog::log::reset_config(logger.get_name(), changed),
              "shrink compressed cache config");
        for (int i = 0; i < 300; ++i) {
            auto fmt = "unique-" + std::to_string(i) + " {}";
            check(logger.info(fmt, i), "diverse template");
        }
        logger.force_flush();
    }
    if (group == "reuse") {
        check(qlog::log::reset_config(logger.get_name(),
                                      "log.thread_mode=sync\nappenders_config.console.type="
                                      "console\nappenders_config.console.enable=false\n"),
              "close first compressed appender");
        logger =
            qlog::log::create_log("compressed_reopened", config(dir, "sync", false), {"", "child"});
        check(logger.is_valid(), "reuse existing binary file");
        check(logger.info(u"wide p={} seq={} text={}", 0, count, std::u16string(u"ASCII后缀😀")),
              "append after template recovery");
        logger.force_flush();
        ++count;
    }
    qlog::log::uninit();
    auto output = files(dir);
    if (rolling) check(output.size() > 1, "file rotation");
    if (group == "reuse") check(output.size() == 1, "plaintext file actually reused");
    std::vector<std::vector<bool>> seen(static_cast<std::size_t>(producers),
                                        std::vector<bool>(static_cast<std::size_t>(count)));
    std::size_t total = 0, format_count = 0;
    std::vector<int> previous(static_cast<std::size_t>(producers), -1);
    for (const auto& path : output) {
        auto parsed = parse(path);
        format_count += parsed.formats;
        for (const auto& rec : parsed.records) {
            check(rec.epoch > 0 && rec.tid > 0, "epoch and thread template");
            if (rec.format.starts_with("unique-")) {
                check(rec.args.size() == 1, "diverse format argument");
                ++total;
                continue;
            }
            check(rec.args.size() == 3, "exact argument count");
            auto p = static_cast<std::size_t>(rec.args[0].value);
            auto i = static_cast<std::size_t>(rec.args[1].value);
            check(p < seen.size() && i < seen[p].size() && !seen[p][i],
                  "no loss or duplicate sequence");
            seen[p][i] = true;
            if (!rolling) {
                check(static_cast<int>(i) > previous[p], "per-producer order");
                previous[p] = static_cast<int>(i);
            }
            auto expected = (i & 1) == 0
                                ? std::string("hello-中文")
                                : (i % 3 == 0 ? std::string(50, 'x') : std::string("ASCII后缀😀"));
            if (group == "reuse" && i == static_cast<std::size_t>(count - 1))
                expected = "ASCII后缀😀";
            check(rec.args[2].text == expected, "UTF-8 / UTF-Mixed string and placeholder");
            ++total;
        }
    }
    check(
        total == static_cast<std::size_t>(producers * count) + (group == "cache_reset" ? 300U : 0U),
        "all compressed records decoded");
    for (const auto& producer : seen)
        check(std::all_of(producer.begin(), producer.end(), [](bool v) { return v; }),
              "every sequence present");
    if (!rolling && group != "cache_reset")
        check(format_count == 2, "UTF-8/UTF-16 format template reuse");
    std::cout << "PASS " << group << " records=" << total << " files=" << output.size()
              << " templates=" << format_count << '\n';
}
}  // namespace
int main(int argc, char** argv) {
    try {
        auto group = argc > 1 ? std::string(argv[1]) : "primitives";
        if (group == "primitives")
            primitives();
        else if (group == "scalars")
            scalars();
        else
            integration(group);
        std::cout << "PASS compressed " << group << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
