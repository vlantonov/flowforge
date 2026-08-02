/**
 * plugin_tls_ja3.cpp — TLS ClientHello parser with JA3 fingerprinting.
 * Hand-rolled two-phase binary parser (RFC 8446 §4.1.2, RFC 8701 GREASE).
 */
#include <flowforge/plugin_v1.h>
#include "vendor/md5/md5.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

extern "C" {

/* Exported data symbol — pre-flight version check by host before calling entry. */
const uint32_t flowforge_plugin_abi_version __attribute__((used, visibility("default"))) = FLOWFORGE_PLUGIN_ABI_VERSION;

static uint32_t abi_version_fn() noexcept { return FLOWFORGE_PLUGIN_ABI_VERSION; }
static const char* name_fn()     noexcept { return "plugin_tls_ja3"; }

} /* extern "C" — temporarily closed to allow non-extern-C helpers */

namespace {

/* Big-endian uint16 read without aliasing UB. */
inline uint16_t be16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

/* GREASE values match the pattern (v & 0x0f0f) == 0x0a0a (RFC 8701). */
inline bool is_grease(uint16_t v) noexcept {
    return (v & 0x0f0fu) == 0x0a0au;
}

/* Append unsigned decimal to str. */
void append_u16(std::string& s, uint16_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
    s += buf;
}

/* Build a dash-separated list of uint16_t values. */
std::string build_list(const std::vector<uint16_t>& vals) {
    std::string s;
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i) s += '-';
        append_u16(s, vals[i]);
    }
    return s;
}

/* Compute MD5 of a C string and write 32 lowercase hex chars + NUL. */
void md5_hex(const char* str, char out[33]) {
    MD5_CTX ctx;
    unsigned char digest[16];
    MD5Init(&ctx);
    MD5Update(&ctx, str, static_cast<unsigned long>(std::strlen(str)));
    MD5Final(digest, &ctx);
    for (int i = 0; i < 16; ++i) {
        std::snprintf(out + i * 2, 3, "%02x", static_cast<unsigned>(digest[i]));
    }
    out[32] = '\0';
}

} // namespace

extern "C" {

static int process(flowforge_buf_t buf, flowforge_result_t* out) {
    if (!out)      return -EINVAL;
    if (!buf.data) return -EINVAL;

    std::memset(out, 0, sizeof(*out));
    std::strncpy(out->protocol, "TLS", sizeof(out->protocol) - 1);
    out->abi_version = FLOWFORGE_PLUGIN_ABI_VERSION;

    const uint8_t* data = buf.data;
    const size_t   len  = buf.len;

    /* Phase 1 — TLS Record Layer (5-byte header). */
    if (len < 5u) {
        out->status   = FLOWFORGE_STATUS_PARSE_ERROR;
        out->severity = FLOWFORGE_SEVERITY_UNKNOWN;
        return -EINVAL;
    }

    uint8_t  content_type   = data[0];
    uint16_t record_len     = be16(data + 3);

    if (content_type != 0x16u) {
        /* Not a Handshake record — not applicable for JA3. */
        out->status   = FLOWFORGE_STATUS_NOT_APPLICABLE;
        out->severity = FLOWFORGE_SEVERITY_UNKNOWN;
        std::snprintf(out->detail, sizeof(out->detail),
                      "TLS record type=%u", static_cast<unsigned>(content_type));
        return 0;
    }

    if (len < static_cast<size_t>(5u + record_len)) {
        out->status   = FLOWFORGE_STATUS_PARSE_ERROR;
        out->severity = FLOWFORGE_SEVERITY_UNKNOWN;
        return -EINVAL;
    }

    /* Phase 2 — Handshake Layer. */
    if (len < 9u) {
        out->status   = FLOWFORGE_STATUS_PARSE_ERROR;
        out->severity = FLOWFORGE_SEVERITY_UNKNOWN;
        return -EINVAL;
    }

    uint8_t handshake_type = data[5];

    if (handshake_type != 0x01u) {
        /* TLS Handshake but not a ClientHello. */
        out->status   = FLOWFORGE_STATUS_NOT_APPLICABLE;
        out->severity = FLOWFORGE_SEVERITY_INFO;
        std::snprintf(out->detail, sizeof(out->detail),
                      "TLS record type=%u", static_cast<unsigned>(handshake_type));
        return 0;
    }

    /* ClientHello body starts at offset 9. */
    size_t pos = 9;

#define NEED(n) do { if (pos + (n) > len) { \
    out->status = FLOWFORGE_STATUS_PARSE_ERROR; \
    out->severity = FLOWFORGE_SEVERITY_UNKNOWN; \
    return -EINVAL; } } while(0)

    NEED(2);
    uint16_t legacy_version = be16(data + pos);
    pos += 2;

    NEED(32); /* random */
    pos += 32;

    NEED(1);
    uint8_t sid_len = data[pos++];
    NEED(sid_len);
    pos += sid_len;

    NEED(2);
    uint16_t cs_len = be16(data + pos);
    pos += 2;
    NEED(cs_len);

    std::vector<uint16_t> ciphers;
    ciphers.reserve(cs_len / 2);
    for (uint16_t i = 0; i < cs_len; i += 2) {
        uint16_t cs = be16(data + pos + i);
        if (!is_grease(cs)) ciphers.push_back(cs);
    }
    pos += cs_len;

    NEED(1);
    uint8_t comp_len = data[pos++];
    NEED(comp_len);
    pos += comp_len;

    std::vector<uint16_t> extensions;
    std::vector<uint16_t> elliptic_curves;
    std::vector<uint16_t> ec_point_fmts;

    if (pos + 2u <= len) {
        uint16_t ext_total = be16(data + pos);
        pos += 2;
        size_t ext_end = pos + ext_total;
        if (ext_end > len) ext_end = len;

        while (pos + 4u <= ext_end) {
            uint16_t ext_type     = be16(data + pos);
            uint16_t ext_data_len = be16(data + pos + 2);
            size_t   ext_data_start = pos + 4;
            pos = ext_data_start + ext_data_len;
            if (pos > ext_end) break;

            if (is_grease(ext_type)) continue;
            extensions.push_back(ext_type);

            if (ext_type == 0x000au) {
                /* supported_groups: uint16_t total_len + uint16_t[] group_ids */
                size_t ep = ext_data_start;
                if (ext_data_len >= 2u) {
                    uint16_t gl = be16(data + ep);
                    ep += 2;
                    size_t curves_end = ext_data_start + 2u + gl;
                    if (curves_end > pos) curves_end = pos;
                    while (ep + 2u <= curves_end) {
                        uint16_t g = be16(data + ep);
                        ep += 2;
                        if (!is_grease(g)) elliptic_curves.push_back(g);
                    }
                }
            } else if (ext_type == 0x000bu) {
                /* ec_point_formats: uint8_t list_len + uint8_t[] formats */
                size_t ep = ext_data_start;
                if (ext_data_len >= 1u) {
                    uint8_t fl = data[ep++];
                    size_t  fmt_end = ext_data_start + 1u + fl;
                    if (fmt_end > pos) fmt_end = pos;
                    while (ep < fmt_end) {
                        ec_point_fmts.push_back(data[ep++]);
                    }
                }
            }
        }
    }

#undef NEED

    /* Build JA3 string: SSLVersion,Ciphers,Extensions,EllipticCurves,EllipticCurvePointFormats */
    std::string ja3;
    ja3.reserve(256);
    {
        char ver[8];
        std::snprintf(ver, sizeof(ver), "%u", static_cast<unsigned>(legacy_version));
        ja3 += ver;
    }
    ja3 += ',';
    ja3 += build_list(ciphers);
    ja3 += ',';
    ja3 += build_list(extensions);
    ja3 += ',';
    ja3 += build_list(elliptic_curves);
    ja3 += ',';
    ja3 += build_list(ec_point_fmts);

    char md5_str[33];
    md5_hex(ja3.c_str(), md5_str);

    out->severity = FLOWFORGE_SEVERITY_INFO;
    out->status   = FLOWFORGE_STATUS_OK;
    std::snprintf(out->detail, sizeof(out->detail), "JA3=%s", md5_str);

    return 0;
}

static const flowforge_plugin_t k_vtable = {
    abi_version_fn,
    name_fn,
    nullptr,
    process,
    nullptr
};

const flowforge_plugin_t* flowforge_plugin_entry() noexcept {
    return &k_vtable;
}

} /* extern "C" */
