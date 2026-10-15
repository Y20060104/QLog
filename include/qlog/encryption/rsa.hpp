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
 * \file rsa.h
 * \date 2025/08/18 01:22
 *
 * \author pippocao
 *
 * \brief
 * Simple RSA implementation (raw, no padding) with key parsing compatible with ssh-keygen:
 * - Public key: OpenSSH "ssh-rsa AAAA..." format
 * - Private key: PKCS#1 PEM "-----BEGIN RSA PRIVATE KEY-----"
 *
 */
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "qlog/platform/macros.hpp"
#include "qlog/platform/time_ops.hpp"
#include "qlog/utility/diagnostics.hpp"
#include "qlog/utility/hash.hpp"
#include "qlog/utility/random.hpp"

namespace qlog::encryption {

class rsa {
   public:
    struct public_key {
        std::vector<uint8_t> n_;
        std::vector<uint8_t> e_;
        bool operator==(const public_key& other) const {
            return n_ == other.n_ && e_ == other.e_;
        }
        bool operator!=(const public_key& other) const {
            return !(*this == other);
        }
    };
    struct private_key {
        std::vector<uint8_t> n_;
        std::vector<uint8_t> e_;
        std::vector<uint8_t> d_;
        std::vector<uint8_t> p_;
        std::vector<uint8_t> q_;
        std::vector<uint8_t> dp_;
        std::vector<uint8_t> dq_;
        std::vector<uint8_t> qinv_;
        bool operator==(const private_key& other) const {
            return n_ == other.n_ && e_ == other.e_ && d_ == other.d_ && p_ == other.p_ &&
                   q_ == other.q_ && dp_ == other.dp_ && dq_ == other.dq_ && qinv_ == other.qinv_;
        }
        bool operator!=(const private_key& other) const {
            return !(*this == other);
        }
    };

   public:
    // Parse OpenSSH public key text (std::string)
    static bool parse_public_key_ssh(const std::string& ssh_pub_text, public_key& out);

    // Parse PKCS#1 PEM private key text (std::string)
    static bool parse_private_key_pem(const std::string& pem, private_key& out);

    static uint64_t get_public_key_fingerprint(const public_key& key);

    // Encrypt: output ciphertext is k bytes, plaintext must be integer < modulus
    static bool encrypt(const public_key& pub, const std::vector<uint8_t>& plaintext,
                        std::vector<uint8_t>& out_ciphertext);

    // Decrypt: output minimal big-endian plaintext
    static bool decrypt(const private_key& pri, const std::vector<uint8_t>& ciphertext,
                        std::vector<uint8_t>& out_plaintext);
};

}  // namespace qlog::encryption
