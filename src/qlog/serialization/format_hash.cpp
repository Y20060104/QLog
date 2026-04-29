#include "qlog/serialization/format_hash.h"

#include <cstring>

// ============ 硬件指令头 ============
#if defined(QLOG_X86)
#include <immintrin.h>
#include <nmmintrin.h> // _mm_crc32_u*
#if defined(_MSC_VER)
#include <intrin.h> // __cpuid
#else
#include <cpuid.h> // __get_cpuid
#endif
#elif defined(QLOG_ARM) && defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif

// ============ 编译器属性：为 HW 函数加目标属性 ============
#if defined(QLOG_X86) && (defined(__GNUC__) || defined(__clang__))
#define QLOG_HW_CRC_TARGET __attribute__((target("sse4.2")))
#else
#define QLOG_HW_CRC_TARGET
#endif

namespace qlog::serialization
{

// CRC32C 软件查表（256 元素，1 KB）
// 对齐 BqLog util.cpp:30-63
// 多项式：0x82F63B78 (反射后)
static const uint32_t kCrc32cTable[256] = {
    0x00000000, 0xF26B8303, 0xE13B70F7, 0x1350F3F4, 0xC79A971F, 0x35F1141C, 0x26A1E7E8, 0xD4CA64EB,
    0x8AD958CF, 0x78B2DBCC, 0x6BE22838, 0x9989AB3B, 0x4D43CFD0, 0xBF284CD3, 0xAC78BF27, 0x5E133C24,
    0x105EC76F, 0xE235446C, 0xF165B798, 0x030E349B, 0xD7C45070, 0x25AFD373, 0x36FF2087, 0xC494A384,
    0x9A879FA0, 0x68EC1CA3, 0x7BBCEF57, 0x89D76C54, 0x5D1D08BF, 0xAF768BBC, 0xBC267848, 0x4E4DFB4B,
    0x20BD8EDE, 0xD2D60DDD, 0xC186FE29, 0x33ED7D2A, 0xE72719C1, 0x154C9AC2, 0x061C6936, 0xF477EA35,
    0xAA64D611, 0x580F5512, 0x4B5FA6E6, 0xB93425E5, 0x6DFE410E, 0x9F95C20D, 0x8CC531F9, 0x7EAEB2FA,
    0x30E349B1, 0xC288CAB2, 0xD1D83946, 0x23B3BA45, 0xF779DEAE, 0x05125DAD, 0x1642AE59, 0xE4292D5A,
    0xBA3A117E, 0x4851927D, 0x5B016189, 0xA96AE28A, 0x7DA08661, 0x8FCB0562, 0x9C9BF696, 0x6EF07595,
    0x417B1DBC, 0xB3109EBF, 0xA0406D4B, 0x522BEE48, 0x86E18AA3, 0x748A09A0, 0x67DAFA54, 0x95B17957,
    0xCBA24573, 0x39C9C670, 0x2A993584, 0xD8F2B687, 0x0C38D26C, 0xFE53516F, 0xED03A29B, 0x1F682198,
    0x5125DAD3, 0xA34E59D0, 0xB01EAA24, 0x42752927, 0x96BF4DCC, 0x64D4CECF, 0x77843D3B, 0x85EFBE38,
    0xDBFC821C, 0x2997011F, 0x3AC7F2EB, 0xC8AC71E8, 0x1C661503, 0xEE0D9600, 0xFD5D65F4, 0x0F36E6F7,
    0x61C69362, 0x93AD1061, 0x80FDE395, 0x72966096, 0xA65C047D, 0x5437877E, 0x4767748A, 0xB50CF789,
    0xEB1FCBAD, 0x197448AE, 0x0A24BB5A, 0xF84F3859, 0x2C855CB2, 0xDEEEDFB1, 0xCDBE2C45, 0x3FD5AF46,
    0x7198540D, 0x83F3D70E, 0x90A324FA, 0x62C8A7F9, 0xB602C312, 0x44694011, 0x5739B3E5, 0xA55230E6,
    0xFB410CC2, 0x092A8FC1, 0x1A7A7C35, 0xE811FF36, 0x3CDB9BDD, 0xCEB018DE, 0xDDE0EB2A, 0x2F8B6829,
    0x82F63B78, 0x709DB87B, 0x63CD4B8F, 0x91A6C88C, 0x456CAC67, 0xB7072F64, 0xA457DC90, 0x563C5F93,
    0x082F63B7, 0xFA44E0B4, 0xE9141340, 0x1B7F9043, 0xCFB5F4A8, 0x3DDE77AB, 0x2E8E845F, 0xDCE5075C,
    0x92A8FC17, 0x60C37F14, 0x73938CE0, 0x81F80FE3, 0x55326B08, 0xA759E80B, 0xB4091BFF, 0x466298FC,
    0x1871A4D8, 0xEA1A27DB, 0xF94AD42F, 0x0B21572C, 0xDFEB33C7, 0x2D80B0C4, 0x3ED04330, 0xCCBBC033,
    0xA24BB5A6, 0x502036A5, 0x4370C551, 0xB11B4652, 0x65D122B9, 0x97BAA1BA, 0x84EA524E, 0x7681D14D,
    0x2892ED69, 0xDAF96E6A, 0xC9A99D9E, 0x3BC21E9D, 0xEF087A76, 0x1D63F975, 0x0E330A81, 0xFC588982,
    0xB21572C9, 0x407EF1CA, 0x532E023E, 0xA145813D, 0x758FE5D6, 0x87E466D5, 0x94B49521, 0x66DF1622,
    0x38CC2A06, 0xCAA7A905, 0xD9F75AF1, 0x2B9CD9F2, 0xFF56BD19, 0x0D3D3E1A, 0x1E6DCDEE, 0xEC064EED,
    0xC38D26C4, 0x31E6A5C7, 0x22B65633, 0xD0DDD530, 0x0417B1DB, 0xF67C32D8, 0xE52CC12C, 0x1747422F,
    0x49547E0B, 0xBB3FFD08, 0xA86F0EFC, 0x5A048DFF, 0x8ECEE914, 0x7CA56A17, 0x6FF599E3, 0x9D9E1AE0,
    0xD3D3E1AB, 0x21B862A8, 0x32E8915C, 0xC083125F, 0x144976B4, 0xE622F5B7, 0xF5720643, 0x07198540,
    0x590AB964, 0xAB613A67, 0xB831C993, 0x4A5A4A90, 0x9E902E7B, 0x6CFBAD78, 0x7FAB5E8C, 0x8DC0DD8F,
    0xE330A81A, 0x115B2B19, 0x020BD8ED, 0xF0605BEE, 0x24AA3F05, 0xD6C1BC06, 0xC5914FF2, 0x37FACCF1,
    0x69E9F0D5, 0x9B8273D6, 0x88D28022, 0x7AB90321, 0xAE7367CA, 0x5C18E4C9, 0x4F48173D, 0xBD23943E,
    0xF36E6F75, 0x0105EC76, 0x12551F82, 0xE03E9C81, 0x34F4F86A, 0xC69F7B69, 0xD5CF889D, 0x27A40B9E,
    0x79B737BA, 0x8BDCB4B9, 0x988C474D, 0x6AE7C44E, 0xBE2DA0A5, 0x4C4623A6, 0x5F16D052, 0xAD7D5351,
};

// 软件实现 逐字节

inline uint32_t crc32c_sw_u8(uint32_t crc, uint8_t v) noexcept
{
    return kCrc32cTable[(crc ^ v) & 0xFF] ^ (crc >> 8);
}
inline uint32_t crc32c_sw_u16(uint32_t crc, uint16_t v) noexcept
{
    crc = crc32c_sw_u8(crc, static_cast<uint8_t>(v & 0xFF));
    crc = crc32c_sw_u8(crc, static_cast<uint8_t>(v >> 8) & 0xFF);
    return crc;
}
inline uint32_t crc32c_sw_u32(uint32_t crc, uint32_t v) noexcept
{
    crc = crc32c_sw_u8(crc, static_cast<uint8_t>(v & 0xFF));
    crc = crc32c_sw_u8(crc, static_cast<uint8_t>((v >> 8) & 0xFF));
    crc = crc32c_sw_u8(crc, static_cast<uint8_t>((v >> 16) & 0xFF));
    crc = crc32c_sw_u8(crc, static_cast<uint8_t>((v >> 24) & 0xFF));
    return crc;
}
inline uint32_t crc32c_sw_u64(uint32_t crc, uint64_t v) noexcept
{
    crc = crc32c_sw_u32(crc, static_cast<uint32_t>(v & 0xFFFFFFFF));
    crc = crc32c_sw_u32(crc, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFF));
    return crc;
}
// 硬件实现
#if defined(QLOG_X86)
inline QLOG_HW_CRC_TARGET uint32_t crc32c_hw_u8(uint32_t crc, uint8_t v) noexcept
{
    return _mm_crc32_u8(crc, v);
}
inline QLOG_HW_CRC_TARGET uint32_t crc32c_hw_u16(uint32_t crc, uint16_t v) noexcept
{
    return _mm_crc32_u16(crc, v);
}
inline QLOG_HW_CRC_TARGET uint32_t crc32c_hw_u32(uint32_t crc, uint32_t v) noexcept
{
    return _mm_crc32_u32(crc, v);
}
inline QLOG_HW_CRC_TARGET uint32_t crc32c_hw_u64(uint32_t crc, uint64_t v) noexcept
{
#if defined(QLOG_X86_64)
    return static_cast<uint32_t>(_mm_crc32_u64(crc, v));
#else
    // i386：拆成两个 u32
    crc = _mm_crc32_u32(crc, static_cast<uint32_t>(v & 0xFFFFFFFF));
    crc = _mm_crc32_u32(crc, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFF));
    return crc;
#endif
}
#elif defined(QLOG_ARM) && defined(__ARM_FEATURE_CRC32)
inline uint32_t crc32c_hw_u8(uint32_t crc, uint8_t v) noexcept
{
    return __crc32cb(crc, v);
}
inline uint32_t crc32c_hw_u16(uint32_t crc, uint16_t v) noexcept
{
    return __crc32ch(crc, v);
}
inline uint32_t crc32c_hw_u32(uint32_t crc, uint32_t v) noexcept
{
    return __crc32cw(crc, v);
}
inline uint32_t crc32c_hw_u64(uint32_t crc, uint64_t v) noexcept
{
    return __crc32cd(crc, v);
}
#endif

// 运行时cpu能力检测
static bool detect_hw_support() noexcept
{
#if defined(QLOG_X86)
#if defined(_MSC_VER)
    int info[4]{};
    __cpuid(info, 1);
    // ECX bit 20 = SSE4.2
    return (info[2] & (1 << 20)) != 0;
#else
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) == 0)
        return false;
    return (ecx & (1u << 20)) != 0;
#endif
#elif defined(QLOG_ARM) && defined(__ARM_FEATURE_CRC32)
    // 编译期已启用 CRC32 扩展；生产环境可用 getauxval(AT_HWCAP) 进一步运行时探测
    return true;
#else
    return false;
#endif
}
static const bool kHwCrc32cSupported = detect_hw_support();
bool is_hw_crc32c_enabled() noexcept
{
    return kHwCrc32cSupported;
}

// 4-way 并行核心宏
//  工作原理：
//    1) 主循环每次消费 32 字节，分 4 路独立计算
//    2) 当剩余 < 32 字节但总长度 >= 32 时，使用"重叠尾块"避免分支
//    3) 当总长度 < 32 时，按 16/8/4/2/1 字节逐级处理
#define QLOG_HASH_CORE(DO_COPY, CRC_U8, CRC_U16, CRC_U32, CRC_U64)                              \
    const uint8_t* s = static_cast<const uint8_t*>(src);                                        \
    uint8_t* d = static_cast<uint8_t*>(dst);                                                    \
    uint32_t h1 = CRC32C_INIT, h2 = CRC32C_INIT, h3 = CRC32C_INIT, h4 = CRC32C_INIT;            \
                                                                                                \
    if (QLOG_LIKELY(len >= HASH_BLOCK_SIZE))                                                    \
    {                                                                                           \
        const uint8_t* const s_end = s + len;                                                   \
        /* ---- 主循环：32 字节/轮 ---- */                                               \
        while (s + HASH_BLOCK_SIZE <= s_end)                                                    \
        {                                                                                       \
            uint64_t v1, v2, v3, v4;                                                            \
            std::memcpy(&v1, s, 8);                                                             \
            std::memcpy(&v2, s + 8, 8);                                                         \
            std::memcpy(&v3, s + 16, 8);                                                        \
            std::memcpy(&v4, s + 24, 8);                                                        \
            if constexpr (DO_COPY)                                                              \
            {                                                                                   \
                std::memcpy(d, &v1, 8);                                                         \
                std::memcpy(d + 8, &v2, 8);                                                     \
                std::memcpy(d + 16, &v3, 8);                                                    \
                std::memcpy(d + 24, &v4, 8);                                                    \
                d += HASH_BLOCK_SIZE;                                                           \
            }                                                                                   \
            h1 = CRC_U64(h1, v1);                                                               \
            h2 = CRC_U64(h2, v2);                                                               \
            h3 = CRC_U64(h3, v3);                                                               \
            h4 = CRC_U64(h4, v4);                                                               \
            s += HASH_BLOCK_SIZE;                                                               \
        }                                                                                       \
                                                                                                \
        if (s < s_end)                                                                          \
        {                                                                                       \
            const uint8_t* t = s_end - HASH_BLOCK_SIZE;                                         \
            uint64_t v1, v2, v3, v4;                                                            \
            std::memcpy(&v1, t, 8);                                                             \
            std::memcpy(&v2, t + 8, 8);                                                         \
            std::memcpy(&v3, t + 16, 8);                                                        \
            std::memcpy(&v4, t + 24, 8);                                                        \
            if constexpr (DO_COPY)                                                              \
            {                                                                                   \
                                                                                                \
                std::memcpy(d, s, static_cast<size_t>(s_end - s));                              \
            }                                                                                   \
            h1 = CRC_U64(h1, v1);                                                               \
            h2 = CRC_U64(h2, v2);                                                               \
            h3 = CRC_U64(h3, v3);                                                               \
            h4 = CRC_U64(h4, v4);                                                               \
        }                                                                                       \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
                                                                                                \
        size_t remain = len;                                                                    \
        if (remain >= 16)                                                                       \
        {                                                                                       \
            uint64_t v1, v2;                                                                    \
            std::memcpy(&v1, s, 8);                                                             \
            std::memcpy(&v2, s + 8, 8);                                                         \
            if constexpr (DO_COPY)                                                              \
            {                                                                                   \
                std::memcpy(d, &v1, 8);                                                         \
                std::memcpy(d + 8, &v2, 8);                                                     \
                d += 16;                                                                        \
            }                                                                                   \
            h1 = CRC_U64(h1, v1);                                                               \
            h2 = CRC_U64(h2, v2);                                                               \
            s += 16;                                                                            \
            remain -= 16;                                                                       \
        }                                                                                       \
        if (remain >= 8)                                                                        \
        {                                                                                       \
            uint64_t v;                                                                         \
            std::memcpy(&v, s, 8);                                                              \
            if constexpr (DO_COPY)                                                              \
            {                                                                                   \
                std::memcpy(d, &v, 8);                                                          \
                d += 8;                                                                         \
            }                                                                                   \
            h3 = CRC_U64(h3, v);                                                                \
            s += 8;                                                                             \
            remain -= 8;                                                                        \
        }                                                                                       \
        if (remain >= 4)                                                                        \
        {                                                                                       \
            uint32_t v;                                                                         \
            std::memcpy(&v, s, 4);                                                              \
            if constexpr (DO_COPY)                                                              \
            {                                                                                   \
                std::memcpy(d, &v, 4);                                                          \
                d += 4;                                                                         \
            }                                                                                   \
            h4 = CRC_U32(h4, v);                                                                \
            s += 4;                                                                             \
            remain -= 4;                                                                        \
        }                                                                                       \
        if (remain >= 2)                                                                        \
        {                                                                                       \
            uint16_t v;                                                                         \
            std::memcpy(&v, s, 2);                                                              \
            if constexpr (DO_COPY)                                                              \
            {                                                                                   \
                std::memcpy(d, &v, 2);                                                          \
                d += 2;                                                                         \
            }                                                                                   \
            h4 = CRC_U16(h4, v);                                                                \
            s += 2;                                                                             \
            remain -= 2;                                                                        \
        }                                                                                       \
        if (remain >= 1)                                                                        \
        {                                                                                       \
            uint8_t v = *s;                                                                     \
            if constexpr (DO_COPY)                                                              \
            {                                                                                   \
                *d = v;                                                                         \
            }                                                                                   \
            h4 = CRC_U8(h4, v);                                                                 \
        }                                                                                       \
    }                                                                                           \
                                                                                                \
    {                                                                                           \
        uint32_t h3_rot = (h3 << H3_ROTATE) | (h3 >> (32 - H3_ROTATE));                         \
        uint32_t h4_rot = (h4 << H4_ROTATE) | (h4 >> (32 - H4_ROTATE));                         \
        return (static_cast<uint64_t>(h1 ^ h3_rot) << 32) | static_cast<uint64_t>(h2 ^ h4_rot); \
    }

#if defined(QLOG_X86) || (defined(QLOG_ARM) && defined(__ARM_FEATURE_CRC32))
QLOG_HW_CRC_TARGET
static uint64_t hash_hw_impl(
    void* QLOG_RESTRICT DST, const void* QLOG_RESTRICT src, size_t len, bool do_copy
) noexcept
{
    if (do_copy)
    {
        QLOG_HASH_CORE(true, crc32c_hw_u8, crc32c_hw_u16, crc32c_hw_u32, crc32c_hw_u64)
    }
    else
    {
        QLOG_HASH_CORE(false, crc32c_hw_u8, crc32c_hw_u16, crc32c_hw_u32, crc32c_hw_u64)
    }
}
#endif

static uint64_t hash_sw_impl(
    void* QLOG_RESTRICT dst, const void* QLOG_RESTRICT src, size_t len, bool do_copy
) noexcept
{
    if (do_copy)
    {
        QLOG_HASH_CORE(true, crc32c_sw_u8, crc32c_sw_u16, crc32c_sw_u32, crc32c_sw_u64)
    }
    else
    {
        QLOG_HASH_CORE(false, crc32c_sw_u8, crc32c_sw_u16, crc32c_sw_u32, crc32c_sw_u64)
    }
}

uint64_t crc32c_hash(const void* data, size_t len) noexcept
{
    if (QLOG_UNLIKELY(len == 0))
    {
        // 空串哈希：全部返回 CRC32C_INIT 的混合）
        uint32_t h = CRC32C_INIT;
        uint32_t h3_rot = (h << H3_ROTATE) | (h >> (32 - H3_ROTATE));
        uint32_t h4_rot = (h << H4_ROTATE) | (h >> (32 - H4_ROTATE));
        return (static_cast<uint64_t>(h ^ h3_rot) << 32) | static_cast<uint64_t>(h ^ h4_rot);
    }
#if defined(QLOG_X86) || (defined(QLOG_ARM) && defined(__ARM_FEATURE_CRC32))
    if (QLOG_LIKELY(kHwCrc32cSupported))
    {
        return hash_hw_impl(nullptr, data, len, false);
    }
#endif
    return hash_sw_impl(nullptr, data, len, false);
}

uint64_t
crc32c_memcpy_with_hash(void* QLOG_RESTRICT dst, const void* QLOG_RESTRICT src, size_t len) noexcept
{
    if (QLOG_UNLIKELY(len == 0))
    {
        return crc32c_hash(src, 0);
    }
#if defined(QLOG_X86) || (defined(QLOG_ARM) && defined(__ARM_FEATURE_CRC32))
    if (QLOG_LIKELY(kHwCrc32cSupported))
    {
        return hash_hw_impl(dst, src, len, true);
    }
#endif
    return hash_sw_impl(dst, src, len, true);
}
} // namespace qlog::serialization