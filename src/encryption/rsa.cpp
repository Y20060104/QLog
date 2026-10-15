/* Copyright (C) 2025 Tencent.
 * BQLOG is licensed under the Apache License, Version 2.0.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */
/*!
 * \file rsa.cpp
 * \date 2025/08/18 01:22
 *
 * \author pippocao
 *
 * \brief
 * RSA raw implementation, compatible with ssh-keygen output.
 */

#include "qlog/encryption/rsa.hpp"

namespace qlog::encryption {
static uint64_t get_public_key_fingerprint(const std::vector<uint8_t>& n,
                                           const std::vector<uint8_t>& e) {
    if (n.empty() || e.empty()) {
        return 0;
    }
    std::vector<uint8_t> data;
    data.resize(n.size() + e.size() + sizeof(uint32_t) * 2);
    uint8_t* data_begin = data.data();
    if (!data_begin) {
        return 0;
    }
    uint8_t* cursor = data_begin;
    const uint32_t n_size = static_cast<uint32_t>(n.size());
    const uint32_t e_size = static_cast<uint32_t>(e.size());
    cursor[0] = static_cast<uint8_t>(n_size >> 24);
    cursor[1] = static_cast<uint8_t>(n_size >> 16);
    cursor[2] = static_cast<uint8_t>(n_size >> 8);
    cursor[3] = static_cast<uint8_t>(n_size);
    cursor += sizeof(uint32_t);
    memcpy(cursor, n.data(), n.size());
    cursor += n.size();
    cursor[0] = static_cast<uint8_t>(e_size >> 24);
    cursor[1] = static_cast<uint8_t>(e_size >> 16);
    cursor[2] = static_cast<uint8_t>(e_size >> 8);
    cursor[3] = static_cast<uint8_t>(e_size);
    cursor += sizeof(uint32_t);
    memcpy(cursor, e.data(), e.size());
    return qlog::utility::get_hash_64(data_begin, data.size());
}

static uint32_t load_u32_be(const uint8_t* p) {
    uint32_t v = 0;
    v |= static_cast<uint32_t>(p[0]) << 24;
    v |= static_cast<uint32_t>(p[1]) << 16;
    v |= static_cast<uint32_t>(p[2]) << 8;
    v |= static_cast<uint32_t>(p[3]);
    return v;
}

static void trim_be(const uint8_t* in, size_t in_len, std::vector<uint8_t>& out) {
    size_t i = 0;
    while (i < in_len && in[i] == 0) {
        ++i;
    }
    size_t n = 0;
    if (i == in_len) {
        n = 1;
    } else {
        n = in_len - i;
    }
    out.clear();
    out.insert(out.end(), in + i, (in + i) + (n));
}

// base64 decode (RFC 4648, ignores whitespace)
static bool base64_decode(const uint8_t* in, size_t in_len, std::vector<uint8_t>& out) {
    static const uint8_t b64_table[256] = {
        /* 0..15  */ 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x81, 0x81, 0x80, 0x81,
        0x81, 0x80, 0x80,
        /* 16..31 */ 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80,
        /* 32..47 */ 0x81, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 62, 0x80, 62,
        0x80, 63,
        /* 48..63 */ 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0x80, 0x80, 0x80, 0x82, 0x80, 0x80,
        /* 64..79 */ 0x80, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
        /* 80..95 */ 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0x80, 0x80, 0x80, 0x80, 63,
        /* 96..111*/ 0x80, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        /*112..127*/ 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 0x80, 0x80, 0x80, 0x80, 0x80,
        /*128..255*/
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    out.clear();
    uint32_t buf = 0;
    int32_t bits = 0;
    for (size_t i = 0; i < in_len; ++i) {
        uint8_t c = in[i];
        uint8_t v = b64_table[c];
        if (v == 0x81) {
            continue;
        }
        if (v == 0x80) {
            qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                    "base64_decode: invalid character");
            return false;
        }
        if (v == 0x82) {
            break;
        }
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            uint8_t byte = static_cast<uint8_t>((buf >> bits) & 0xFF);
            out.insert(out.end(), &byte, (&byte) + (1));
        }
    }
    return true;
}

static bool parse_ssh_wire_rsa(const uint8_t* data, size_t len, std::vector<uint8_t>& e_be,
                               std::vector<uint8_t>& n_be) {
    if (len < 4) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "ssh-rsa: truncated key");
        return false;
    }
    uint32_t l1 = load_u32_be(data);
    data += 4;
    len -= 4;
    if (len < l1) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "ssh-rsa: bad type length");
        return false;
    }
    if (l1 != 7) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "ssh-rsa: bad type length");
        return false;
    }
    if (memcmp(data, "ssh-rsa", 7) != 0) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "ssh-rsa: wrong type");
        return false;
    }
    data += 7;
    len -= 7;
    if (len < 4) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "ssh-rsa: missing e length");
        return false;
    }
    uint32_t le = load_u32_be(data);
    data += 4;
    len -= 4;
    if (len < le) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "ssh-rsa: truncated e");
        return false;
    }
    trim_be(data, le, e_be);
    data += le;
    len -= le;

    if (len < 4) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "ssh-rsa: missing n length");
        return false;
    }
    uint32_t ln = load_u32_be(data);
    data += 4;
    len -= 4;
    if (len < ln) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "ssh-rsa: truncated n");
        return false;
    }
    trim_be(data, ln, n_be);
    return true;
}

// Minimal DER reader for PKCS#1 RSAPrivateKey
struct der {
    const uint8_t* p_;
    size_t n_;
    size_t i_;
};
static bool der_next_tlv(der& d, uint8_t& tag, const uint8_t*& val, size_t& vlen) {
    if (d.i_ >= d.n_) {
        return false;
    }
    tag = d.p_[d.i_];
    d.i_++;
    if (d.i_ >= d.n_) {
        return false;
    }
    uint8_t l = d.p_[d.i_];
    d.i_++;
    size_t len = 0;
    if ((l & 0x80) == 0) {
        len = l;
    } else {
        uint8_t ll = l & 0x7F;
        if (ll == 0 || ll > 4) {
            return false;
        }
        if (d.i_ + ll > d.n_) {
            return false;
        }
        len = 0;
        for (uint8_t k = 0; k < ll; ++k) {
            len = (len << 8) | d.p_[d.i_];
            d.i_++;
        }
    }
    if (d.i_ + len > d.n_) {
        return false;
    }
    val = d.p_ + d.i_;
    vlen = len;
    d.i_ += len;
    return true;
}

static bool der_expect_tag(uint8_t tag, uint8_t expected) {
    return tag == expected;
}

static void der_copy_int_to_be_no_lead(const uint8_t* val, size_t vlen, std::vector<uint8_t>& out) {
    trim_be(val, vlen, out);
}

// Big number helpers
static size_t limbs_for_bytes(size_t bytes) {
    return (bytes + 3u) / 4u;
}

static void bn_zero(uint32_t* x, size_t l) {
    memset(x, 0, l * sizeof(uint32_t));
}

static void bn_copy(uint32_t* dst, const uint32_t* src, size_t l) {
    if (dst != src) {
        memcpy(dst, src, l * sizeof(uint32_t));
    }
}

static int32_t bn_ucmp(const uint32_t* a, const uint32_t* b, size_t l) {
    for (size_t i = 0; i < l; ++i) {
        size_t j = l - 1 - i;
        if (a[j] < b[j]) {
            return -1;
        }
        if (a[j] > b[j]) {
            return 1;
        }
    }
    return 0;
}

static uint32_t bn_add(uint32_t* r, const uint32_t* a, const uint32_t* b, size_t l) {
    uint64_t c = 0;
    for (size_t i = 0; i < l; ++i) {
        c = c + (uint64_t)a[i];
        c = c + (uint64_t)b[i];
        r[i] = (uint32_t)c;
        c >>= 32;
    }
    return (uint32_t)c;
}

static uint32_t bn_sub(uint32_t* r, const uint32_t* a, const uint32_t* b, size_t l) {
    uint64_t c = 0;
    for (size_t i = 0; i < l; ++i) {
        uint64_t ai = a[i];
        uint64_t bi = b[i];
        uint64_t t = ai - bi - c;
        r[i] = (uint32_t)t;
        c = (t >> 63) & 1u;
    }
    return (uint32_t)c;
}

static void bn_from_be_bytes(uint32_t* x, size_t l, const uint8_t* be, size_t be_len) {
    bn_zero(x, l);
    size_t o = 0;
    for (size_t i = 0; i < be_len; ++i) {
        size_t bi = be_len - 1 - i;
        size_t wi = o >> 2;
        size_t bo = o & 3u;
        if (wi < l) {
            x[wi] |= (uint32_t)be[bi] << (8u * bo);
        }
        o++;
    }
}

static void bn_to_be_bytes(const uint32_t* x, size_t l, uint8_t* out, size_t out_len) {
    for (size_t i = 0; i < out_len; ++i) {
        out[i] = 0;
    }
    // write i-th least-significant byte to out[out_len-1-i]
    for (size_t i = 0; i < out_len; ++i) {
        size_t wi = i >> 2;
        size_t bo = i & 3u;
        uint8_t v = 0;
        if (wi < l) {
            v = (uint8_t)((x[wi] >> (8u * bo)) & 0xFFu);
        }
        out[out_len - 1 - i] = v;
    }
}

static size_t bn_bitlen(const uint32_t* a, size_t l) {
    for (size_t i = 0; i < l; ++i) {
        size_t j = l - 1 - i;
        uint32_t w = a[j];
        if (w) {
            uint32_t x = w;
            uint32_t pos = 31;
            while ((x & (1u << 31)) == 0) {
                x <<= 1;
                pos--;
            }
            return j * 32 + pos + 1;
        }
    }
    return 0;
}

static int32_t bn_getbit(const uint32_t* a, size_t l, size_t bit_index) {
    size_t wi = bit_index >> 5;
    size_t bo = bit_index & 31u;
    if (wi >= l) {
        return 0;
    }
    return (int32_t)((a[wi] >> bo) & 1u);
}

static void mod_add(uint32_t* r, const uint32_t* a, const uint32_t* b, const uint32_t* m, size_t l,
                    uint32_t* tmp) {
    uint32_t carry = bn_add(tmp, a, b, l);
    bool ge = false;
    if (carry != 0) {
        ge = true;
    }
    if (bn_ucmp(tmp, m, l) >= 0) {
        ge = true;
    }
    if (ge) {
        (void)bn_sub(tmp, tmp, m, l);
    }
    bn_copy(r, tmp, l);
}

static void mod_double(uint32_t* r, const uint32_t* a, const uint32_t* m, size_t l, uint32_t* tmp) {
    uint32_t carry = bn_add(tmp, a, a, l);
    bool ge = false;
    if (carry != 0) {
        ge = true;
    }
    if (bn_ucmp(tmp, m, l) >= 0) {
        ge = true;
    }
    if (ge) {
        (void)bn_sub(tmp, tmp, m, l);
    }
    bn_copy(r, tmp, l);
}

static void mod_mul_bin(uint32_t* r, const uint32_t* a, const uint32_t* b, const uint32_t* m,
                        size_t l, uint32_t* acc, uint32_t* cur, uint32_t* tmp) {
    bn_zero(acc, l);
    bn_copy(cur, a, l);
    size_t bits = bn_bitlen(b, l);
    for (size_t i = 0; i < bits; ++i) {
        if (bn_getbit(b, l, i)) {
            mod_add(acc, acc, cur, m, l, tmp);
        }
        mod_double(cur, cur, m, l, tmp);
    }
    bn_copy(r, acc, l);
}

static void mod_pow(uint32_t* r, const uint32_t* a, const uint32_t* e, const uint32_t* m, size_t l,
                    uint32_t* t0, uint32_t* t1, uint32_t* t2, uint32_t* t3) {
    bn_zero(r, l);
    r[0] = 1u;
    bn_copy(t0, a, l);
    size_t bits = bn_bitlen(e, l);
    for (size_t i = 0; i < bits; ++i) {
        if (bn_getbit(e, l, i)) {
            mod_mul_bin(r, r, t0, m, l, t1, t2, t3);
        }
        mod_mul_bin(t0, t0, t0, m, l, t1, t2, t3);
    }
}

static void be_to_fixed_limbs(uint32_t* x, size_t l, const std::vector<uint8_t>& be) {
    bn_zero(x, l);
    bn_from_be_bytes(x, l, be.data(), be.size());
}

static void fixed_limbs_to_be_bytes(const uint32_t* x, size_t l, size_t k_bytes,
                                    std::vector<uint8_t>& out) {
    out.clear();
    out.resize(k_bytes);
    bn_to_be_bytes(x, l, out.data(), k_bytes);
}

static void fixed_limbs_to_min_be(const uint32_t* x, size_t l, std::vector<uint8_t>& out) {
    size_t k = l * 4u;
    std::vector<uint8_t> tmp;
    tmp.resize(k);
    bn_to_be_bytes(x, l, tmp.data(), k);
    size_t i = 0;
    while (i < k && tmp[i] == 0) {
        i++;
    }
    size_t n = 0;
    if (i == k) {
        n = 1;
    } else {
        n = k - i;
    }
    out.clear();
    out.insert(out.end(), tmp.data() + static_cast<ptrdiff_t>(i),
               (tmp.data() + static_cast<ptrdiff_t>(i)) + (n));
}

static bool parse_two_tokens(const uint8_t* p, size_t n, const uint8_t*& t1, size_t& l1,
                             const uint8_t*& t2, size_t& l2) {
    size_t i = 0;
    while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) {
        ++i;
    }
    size_t s1 = i;
    while (i < n && !(p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) {
        ++i;
    }
    size_t e1 = i;
    while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) {
        ++i;
    }
    size_t s2 = i;
    while (i < n && !(p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n')) {
        ++i;
    }
    size_t e2 = i;
    if (e1 <= s1 || e2 <= s2) {
        return false;
    }
    t1 = p + s1;
    l1 = e1 - s1;
    t2 = p + s2;
    l2 = e2 - s2;
    return true;
}

static bool bn_check_nonzero(const std::vector<uint8_t>& x) {
    for (size_t i = 0; i < x.size(); ++i) {
        if (x[i] != 0) {
            return true;
        }
    }
    return false;
}

bool rsa::parse_public_key_ssh(const std::string& ssh_pub_text, public_key& out) {
    std::vector<uint8_t> str_bytes;
    str_bytes.clear();
    str_bytes.insert(str_bytes.end(), (const uint8_t*)ssh_pub_text.c_str(),
                     ((const uint8_t*)ssh_pub_text.c_str()) + (ssh_pub_text.size()));
    const uint8_t* t1 = nullptr;
    size_t l1 = 0;
    const uint8_t* t2 = nullptr;
    size_t l2 = 0;
    if (!parse_two_tokens(str_bytes.data(), str_bytes.size(), t1, l1, t2, l2)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_public_key_ssh: cannot tokenize");
        return false;
    }
    std::string type_str = "ssh-rsa";
    if (l1 != type_str.size()) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_public_key_ssh: not ssh-rsa");
        return false;
    }
    if (memcmp(t1, type_str.c_str(), l1) != 0) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_public_key_ssh: not ssh-rsa");
        return false;
    }
    std::vector<uint8_t> blob;
    if (!base64_decode(t2, l2, blob)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_public_key_ssh: base64 decode failed");
        return false;
    }
    std::vector<uint8_t> e_be;
    std::vector<uint8_t> n_be;
    if (!parse_ssh_wire_rsa(blob.data(), blob.size(), e_be, n_be)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_public_key_ssh: wire parse failed");
        return false;
    }
    if (!bn_check_nonzero(n_be)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_public_key_ssh: empty n");
        return false;
    }
    if (!bn_check_nonzero(e_be)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_public_key_ssh: empty e");
        return false;
    }
    out.n_.clear();
    out.n_.insert(out.n_.end(), n_be.data(), (n_be.data()) + (n_be.size()));
    out.e_.clear();
    out.e_.insert(out.e_.end(), e_be.data(), (e_be.data()) + (e_be.size()));
    return true;
}

bool rsa::parse_private_key_pem(const std::string& pem, private_key& out) {
    std::string h1 = "-----BEGIN RSA PRIVATE KEY-----";
    std::string h2 = "-----END RSA PRIVATE KEY-----";
    const uint8_t* p = (const uint8_t*)pem.c_str();
    size_t n = pem.size();

    size_t i = 0;
    bool found = false;
    for (; i + h1.size() <= n; ++i) {
        if (memcmp(p + i, h1.c_str(), h1.size()) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_private_key_pem: header not found");
        return false;
    }
    while (i < n && p[i] != '\n') {
        ++i;
    }
    if (i < n) {
        ++i;
    }

    size_t j = i;
    found = false;
    for (; j + h2.size() <= n; ++j) {
        if (memcmp(p + j, h2.c_str(), h2.size()) == 0) {
            found = true;
            break;
        }
    }
    if (!found || j <= i) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_private_key_pem: footer not found");
        return false;
    }

    std::vector<uint8_t> der_bytes;
    if (!base64_decode(p + i, j - i, der_bytes)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_private_key_pem: base64 decode failed");
        return false;
    }

    der d;
    d.p_ = der_bytes.data();
    d.n_ = der_bytes.size();
    d.i_ = 0;
    uint8_t tag = 0;
    const uint8_t* val = nullptr;
    size_t vlen = 0;
    if (!der_next_tlv(d, tag, val, vlen)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_private_key_pem: not a SEQUENCE");
        return false;
    }
    if (!der_expect_tag(tag, 0x30)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_private_key_pem: not a SEQUENCE");
        return false;
    }
    der s;
    s.p_ = val;
    s.n_ = vlen;
    s.i_ = 0;
    if (!der_next_tlv(s, tag, val, vlen)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_private_key_pem: version missing");
        return false;
    }
    if (!der_expect_tag(tag, 0x02)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_private_key_pem: version missing");
        return false;
    }
    std::vector<uint8_t> n_be;
    std::vector<uint8_t> e_be;
    std::vector<uint8_t> d_be;
    std::vector<uint8_t> p_be;
    std::vector<uint8_t> q_be;
    std::vector<uint8_t> dp_be;
    std::vector<uint8_t> dq_be;
    std::vector<uint8_t> qi_be;
    if (!der_next_tlv(s, tag, val, vlen)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: n missing");
        return false;
    }
    if (!der_expect_tag(tag, 0x02)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: n missing");
        return false;
    }
    der_copy_int_to_be_no_lead(val, vlen, n_be);
    if (!der_next_tlv(s, tag, val, vlen)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: e missing");
        return false;
    }
    if (!der_expect_tag(tag, 0x02)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: e missing");
        return false;
    }
    der_copy_int_to_be_no_lead(val, vlen, e_be);
    if (!der_next_tlv(s, tag, val, vlen)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: d missing");
        return false;
    }
    if (!der_expect_tag(tag, 0x02)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: d missing");
        return false;
    }
    der_copy_int_to_be_no_lead(val, vlen, d_be);
    if (!der_next_tlv(s, tag, val, vlen)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: p missing");
        return false;
    }
    if (!der_expect_tag(tag, 0x02)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: p missing");
        return false;
    }
    der_copy_int_to_be_no_lead(val, vlen, p_be);
    if (!der_next_tlv(s, tag, val, vlen)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: q missing");
        return false;
    }
    if (!der_expect_tag(tag, 0x02)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: q missing");
        return false;
    }
    der_copy_int_to_be_no_lead(val, vlen, q_be);
    if (!der_next_tlv(s, tag, val, vlen)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: dp missing");
        return false;
    }
    if (!der_expect_tag(tag, 0x02)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: dp missing");
        return false;
    }
    der_copy_int_to_be_no_lead(val, vlen, dp_be);
    if (!der_next_tlv(s, tag, val, vlen)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: dq missing");
        return false;
    }
    if (!der_expect_tag(tag, 0x02)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: dq missing");
        return false;
    }
    der_copy_int_to_be_no_lead(val, vlen, dq_be);
    if (!der_next_tlv(s, tag, val, vlen)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: qInv missing");
        return false;
    }
    if (!der_expect_tag(tag, 0x02)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "rsa: qInv missing");
        return false;
    }
    der_copy_int_to_be_no_lead(val, vlen, qi_be);

    if (!bn_check_nonzero(n_be)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_private_key_pem: empty n");
        return false;
    }
    if (!bn_check_nonzero(e_be)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_private_key_pem: empty e");
        return false;
    }
    if (!bn_check_nonzero(d_be)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::parse_private_key_pem: empty d");
        return false;
    }

    out.n_.clear();
    out.n_.insert(out.n_.end(), n_be.data(), (n_be.data()) + (n_be.size()));
    out.e_.clear();
    out.e_.insert(out.e_.end(), e_be.data(), (e_be.data()) + (e_be.size()));
    out.d_.clear();
    out.d_.insert(out.d_.end(), d_be.data(), (d_be.data()) + (d_be.size()));
    out.p_.clear();
    out.p_.insert(out.p_.end(), p_be.data(), (p_be.data()) + (p_be.size()));
    out.q_.clear();
    out.q_.insert(out.q_.end(), q_be.data(), (q_be.data()) + (q_be.size()));
    out.dp_.clear();
    out.dp_.insert(out.dp_.end(), dp_be.data(), (dp_be.data()) + (dp_be.size()));
    out.dq_.clear();
    out.dq_.insert(out.dq_.end(), dq_be.data(), (dq_be.data()) + (dq_be.size()));
    out.qinv_.clear();
    out.qinv_.insert(out.qinv_.end(), qi_be.data(), (qi_be.data()) + (qi_be.size()));

    return true;
}

uint64_t rsa::get_public_key_fingerprint(const public_key& key) {
    return qlog::encryption::get_public_key_fingerprint(key.n_, key.e_);
}

bool rsa::encrypt(const public_key& pub, const std::vector<uint8_t>& plaintext,
                  std::vector<uint8_t>& out_ciphertext) {
    if (plaintext[0] == 0) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error,
            "rsa::encrypt (raw): plaintext starts with 0x00; unpadded RSA returns minimal "
            "big-endian on decrypt and drops leading zeros. ");
        return false;
    }
    if (pub.n_.size() == 0) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::encrypt: empty public key (n)");
        return false;
    }
    if (pub.e_.size() == 0) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::encrypt: empty public key (e)");
        return false;
    }
    size_t k_bytes = pub.n_.size();
    if (plaintext.size() > k_bytes) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::encrypt: plaintext too large");
        return false;
    }
    size_t l = limbs_for_bytes(k_bytes);
    std::vector<uint32_t> n_l;
    std::vector<uint32_t> e_l;
    std::vector<uint32_t> m_l;
    std::vector<uint32_t> c_l;
    std::vector<uint32_t> t0;
    std::vector<uint32_t> t1;
    std::vector<uint32_t> t2;
    std::vector<uint32_t> t3;
    n_l.resize(l);
    e_l.resize(l);
    m_l.resize(l);
    c_l.resize(l);
    t0.resize(l);
    t1.resize(l);
    t2.resize(l);
    t3.resize(l);

    be_to_fixed_limbs(n_l.data(), l, pub.n_);
    be_to_fixed_limbs(e_l.data(), l, pub.e_);
    be_to_fixed_limbs(m_l.data(), l, plaintext);

    if (bn_ucmp(m_l.data(), n_l.data(), l) >= 0) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error, "rsa::encrypt: plaintext integer not less than modulus");
        return false;
    }

    mod_pow(c_l.data(), m_l.data(), e_l.data(), n_l.data(), l, t0.data(), t1.data(), t2.data(),
            t3.data());

    fixed_limbs_to_be_bytes(c_l.data(), l, k_bytes, out_ciphertext);
    return true;
}

bool rsa::decrypt(const private_key& pri, const std::vector<uint8_t>& ciphertext,
                  std::vector<uint8_t>& out_plaintext) {
    if (pri.n_.size() == 0) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::decrypt: empty private key (n)");
        return false;
    }
    if (pri.d_.size() == 0) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "rsa::decrypt: empty private key (d)");
        return false;
    }
    size_t k_bytes = pri.n_.size();
    if (ciphertext.size() != k_bytes) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error, "rsa::decrypt: ciphertext length must equal modulus length");
        return false;
    }
    size_t l = limbs_for_bytes(k_bytes);
    std::vector<uint32_t> n_l;
    std::vector<uint32_t> d_l;
    std::vector<uint32_t> c_l;
    std::vector<uint32_t> m_l;
    std::vector<uint32_t> t0;
    std::vector<uint32_t> t1;
    std::vector<uint32_t> t2;
    std::vector<uint32_t> t3;
    n_l.resize(l);
    d_l.resize(l);
    c_l.resize(l);
    m_l.resize(l);
    t0.resize(l);
    t1.resize(l);
    t2.resize(l);
    t3.resize(l);

    be_to_fixed_limbs(n_l.data(), l, pri.n_);
    be_to_fixed_limbs(d_l.data(), l, pri.d_);
    be_to_fixed_limbs(c_l.data(), l, ciphertext);

    if (bn_ucmp(c_l.data(), n_l.data(), l) >= 0) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error, "rsa::decrypt: ciphertext integer not less than modulus");
        return false;
    }

    mod_pow(m_l.data(), c_l.data(), d_l.data(), n_l.data(), l, t0.data(), t1.data(), t2.data(),
            t3.data());

    fixed_limbs_to_min_be(m_l.data(), l, out_plaintext);
    return true;
}

}  // namespace qlog::encryption
