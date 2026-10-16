# BQLog source attribution

QLog contains C++ files and algorithms adapted from Tencent BQLog, Copyright (C) 2025-2026 Tencent, licensed under Apache License 2.0. The adopted reference is commit 78cbfbef4f87e7558335f0c04bf6dd6894b46451 of Tencent/BqLog. Original copyright headers remain in directly migrated runtime and Appender files; platform stack capture and record begin/finish are also adapted from that reference. The license text is retained in LICENSES/BQLog-APACHE-2.0.txt. This notice does not relicense unrelated QLog code.

This branch removes the compressed v10 writer/container, its template cache and encoding helpers, RSA/AES/Vernam, random helpers and test-key fixtures. Their attribution and historical alignment record remain available in the complete-version commit c1c5e6c and docs/decisions/FILE_COMPRESSED_ALIGNMENT_20261015_CHS.md. The reserved compressed_file Appender enum value and configuration name remain unchanged.

The retained Text benchmark entry and write-thread-cost test in benchmark/cpp are adapted from Tencent/BqLog commit 086f2fd831af86fcfa40e90015f89626d498d3d7, retrieved on 2026-10-15. Original copyright notices are retained where present. QLog uses its existing standard-library types and normal logging API; upstream FAST cases are not emulated. The original benchmark file hashes are recorded in benchmark/cpp/UPSTREAM.json. This benchmark reference does not change the earlier reference used for the production library.

The fused format-string copy/hash entry follows util::bq_memcpy_with_hash from production reference 78cbfbef, reusing the existing QLog software/x86 hash core. Its original software table and per-backend hash values are retained; see docs/decisions/COPY_HASH_FUSION_20261015_CHS.md.

The SISO half-full crossing notification rule and Layout digit_pairs/write_decimal_digits conversion were aligned on 2026-10-16 with Tencent/BqLog commit 086f2fd831af86fcfa40e90015f89626d498d3d7 (src/bq_log/types/buffer/siso_ring_buffer.cpp and src/bq_log/log/layout.cpp), Copyright (C) 2025-2026 Tencent, Apache-2.0. This selective adoption does not upgrade the entire production reference.
