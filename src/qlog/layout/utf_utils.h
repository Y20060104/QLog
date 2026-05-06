// src/qlog/layout/utf_utils.h
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace qlog::layout::utf
{

// ─────────────────────────────────────────────────────────────────────────────
// UTF-32 码点 → UTF-8 编码
// 对标 BqLog utf_utils.h 中的 utf32_to_utf8_codepoint
//
// 编码规则（RFC 3629）：
//   U+0000   ~ U+007F   : 0xxxxxxx          (1 字节)
//   U+0080   ~ U+07FF   : 110xxxxx 10xxxxxx  (2 字节)
//   U+0800   ~ U+FFFF   : 1110xxxx 10xxxxxx 10xxxxxx (3 字节)
//   U+10000  ~ U+10FFFF : 11110xxx ...                (4 字节)
//
// 返回写入字节数（0 表示非法码点，以 U+FFFD 替换）
// ─────────────────────────────────────────────────────────────────────────────
inline size_t encode_codepoint(uint32_t cp, uint8_t* dst) noexcept
{
    // 非法码点（代理对区间 / 超出 Unicode 范围）→ U+FFFD (EF BF BD)
    if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
    {
        dst[0] = 0xEF;
        dst[1] = 0xBF;
        dst[2] = 0xBD;
        return 3;
    }
    if (cp <= 0x7F)
    {
        dst[0] = static_cast<uint8_t>(cp);
        return 1;
    }
    if (cp <= 0x7FF)
    {
        dst[0] = static_cast<uint8_t>(0xC0 | (cp >> 6));
        dst[1] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp <= 0xFFFF)
    {
        dst[0] = static_cast<uint8_t>(0xE0 | (cp >> 12));
        dst[1] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
        return 3;
    }
    // 4 字节
    dst[0] = static_cast<uint8_t>(0xF0 | (cp >> 18));
    dst[1] = static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F));
    dst[2] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
    dst[3] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
    return 4;
}

// ─────────────────────────────────────────────────────────────────────────────
// UTF-16LE → UTF-8 转换（原地写入 dst_buf）
//
// 处理代理对（Surrogate Pair）：
//   高代理 U+D800-U+DBFF，低代理 U+DC00-U+DFFF
//   合并公式: cp = 0x10000 + (high - 0xD800) * 0x400 + (low - 0xDC00)
//
// 参数：
//   src16_bytes : UTF-16 原始字节（小端序，字节长度，非字符数）
//   src_len     : 字节数（必须为偶数）
//   dst         : 输出缓冲区（调用方保证足够大：最坏情况 src_len * 1.5 字节）
//
// 返回：写入 dst 的 UTF-8 字节数
// ─────────────────────────────────────────────────────────────────────────────
inline size_t utf16le_to_utf8(const uint8_t* src16_bytes, size_t src_len, uint8_t* dst) noexcept
{
    if (src_len < 2)
        return 0;

    uint8_t* d = dst;
    const uint8_t* p = src16_bytes;
    const uint8_t* end = src16_bytes + (src_len & ~size_t{1}); // 向下对齐到偶数

    while (p < end)
    {
        // 小端 UTF-16：低字节在前
        const uint16_t u16 = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
        p += 2;

        uint32_t cp;
        if (u16 >= 0xD800 && u16 <= 0xDBFF)
        {
            // 高代理：需要读入紧跟的低代理
            if (p >= end)
            {
                // 孤立的高代理 → 替换字符
                cp = 0xFFFD;
            }
            else
            {
                const uint16_t low =
                    static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    // 合法代理对
                    cp = 0x10000u + (static_cast<uint32_t>(u16 - 0xD800u) << 10) +
                         static_cast<uint32_t>(low - 0xDC00u);
                    p += 2;
                }
                else
                {
                    // 孤立高代理 → 替换，不消耗下一个单元
                    cp = 0xFFFD;
                }
            }
        }
        else if (u16 >= 0xDC00 && u16 <= 0xDFFF)
        {
            // 孤立低代理
            cp = 0xFFFD;
        }
        else
        {
            cp = u16;
        }

        d += encode_codepoint(cp, d);
    }
    return static_cast<size_t>(d - dst);
}

// ─────────────────────────────────────────────────────────────────────────────
// UTF-32LE → UTF-8 转换
//
// 参数：
//   src32_bytes : UTF-32 原始字节（小端序，字节长度）
//   src_len     : 字节数（必须为 4 的倍数）
//   dst         : 输出缓冲区（最坏情况 src_len 字节，因每个 U+10000+ 最多 4B UTF-8）
//
// 返回：写入 dst 的 UTF-8 字节数
// ─────────────────────────────────────────────────────────────────────────────
inline size_t utf32le_to_utf8(const uint8_t* src32_bytes, size_t src_len, uint8_t* dst) noexcept
{
    if (src_len < 4)
        return 0;

    uint8_t* d = dst;
    const uint8_t* p = src32_bytes;
    const uint8_t* end = src32_bytes + (src_len & ~size_t{3}); // 向下对齐到 4 字节

    while (p < end)
    {
        // 小端 UTF-32
        const uint32_t cp = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                            (static_cast<uint32_t>(p[2]) << 16) |
                            (static_cast<uint32_t>(p[3]) << 24);
        p += 4;
        d += encode_codepoint(cp, d);
    }
    return static_cast<size_t>(d - dst);
}

} // namespace qlog::layout::utf