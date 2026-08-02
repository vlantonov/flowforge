/**
 * plugin_dns.cpp — DNS wire-format parser (RFC 1035 §4).
 * Hand-rolled binary parser with strict bounds and pointer-loop guard.
 */
#include <flowforge/plugin_v1.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdint>

extern "C" {

/* extern forces external linkage (C++ const is internal by default) so dlsym can find this symbol. */
extern const uint32_t flowforge_plugin_abi_version __attribute__((used, visibility("default"))) = FLOWFORGE_PLUGIN_ABI_VERSION;

static uint32_t abi_version_fn() noexcept { return FLOWFORGE_PLUGIN_ABI_VERSION; }
static const char* name_fn()     noexcept { return "plugin_dns"; }

/* Big-endian uint16 read without pointer aliasing UB. */
static uint16_t be16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

/**
 * Decode a DNS QNAME label sequence starting at *pos, updating *pos to the
 * byte after the terminating zero or pointer.  Writes decoded name (with dots)
 * into out (size out_size).  Returns false on malformed input.
 */
static bool decode_qname(const uint8_t* msg, size_t msg_len,
                          size_t* pos, char* out, size_t out_size) {
    size_t write = 0;
    int hops     = 0;
    size_t cur   = *pos;
    bool jumped  = false;

    while (cur < msg_len) {
        uint8_t label_len = msg[cur];

        if ((label_len & 0xC0u) == 0xC0u) {
            /* Pointer compression. */
            if (++hops > 128) return false;
            if (cur + 1 >= msg_len) return false;
            uint16_t offset = static_cast<uint16_t>((label_len & 0x3Fu) << 8) | msg[cur + 1];
            if (offset >= msg_len) return false;
            if (!jumped) *pos = cur + 2;
            jumped = true;
            cur = offset;
            continue;
        }

        if ((label_len & 0xC0u) != 0) return false; /* reserved bits */

        if (label_len == 0) {
            if (!jumped) *pos = cur + 1;
            if (write == 0 && out_size > 0) out[write++] = '.'; /* root */
            break;
        }

        cur++;
        if (cur + label_len > msg_len) return false;

        /* Append dot separator (except before first label). */
        if (write > 0 && write + 1 < out_size) out[write++] = '.';
        size_t copy = label_len;
        if (write + copy >= out_size) copy = out_size - write - 1;
        std::memcpy(out + write, msg + cur, copy);
        write += copy;
        cur   += label_len;
    }

    if (write < out_size) out[write] = '\0';
    else if (out_size > 0) out[out_size - 1] = '\0';
    return true;
}

static int process(flowforge_buf_t buf, flowforge_result_t* out) {
    if (!out)        return -EINVAL;
    if (!buf.data)   return -EINVAL;
    if (buf.len < 12) {
        std::memset(out, 0, sizeof(*out));
        std::strncpy(out->protocol, "DNS", sizeof(out->protocol) - 1);
        out->abi_version = FLOWFORGE_PLUGIN_ABI_VERSION;
        out->status      = FLOWFORGE_STATUS_PARSE_ERROR;
        out->severity    = FLOWFORGE_SEVERITY_UNKNOWN;
        return -EINVAL;
    }

    std::memset(out, 0, sizeof(*out));
    std::strncpy(out->protocol, "DNS", sizeof(out->protocol) - 1);
    out->abi_version = FLOWFORGE_PLUGIN_ABI_VERSION;

    const uint8_t* data = buf.data;
    const size_t   len  = buf.len;

    uint16_t id      = be16(data + 0);
    uint16_t flags   = be16(data + 2);
    uint16_t qdcount = be16(data + 4);
    /* ancount / nscount / arcount logged but not fully parsed in v0.1 */

    int qr     = (flags >> 15) & 1;
    int opcode = (flags >> 11) & 0xF;
    int rcode  =  flags        & 0xF;

    /* Decode first question to get QNAME and QTYPE. */
    char qname[128] = {};
    uint16_t qtype  = 0;
    bool any_query  = false;

    if (qdcount > 0) {
        size_t pos = 12;
        if (!decode_qname(data, len, &pos, qname, sizeof(qname))) {
            out->status   = FLOWFORGE_STATUS_PARSE_ERROR;
            out->severity = FLOWFORGE_SEVERITY_UNKNOWN;
            return 0;
        }
        if (pos + 2 <= len) {
            qtype = be16(data + pos);
        }
        any_query = (qtype == 255);
    }

    if (qr == 0) {
        /* Query */
        if (any_query) {
            out->severity = FLOWFORGE_SEVERITY_WARN;
            std::snprintf(out->detail, sizeof(out->detail),
                          "DNS ANY query (potential amplification)");
        } else {
            out->severity = FLOWFORGE_SEVERITY_INFO;
            std::snprintf(out->detail, sizeof(out->detail),
                          "qr=QUERY opcode=%d rcode=%d id=0x%04X qname=%s qtype=%u",
                          opcode, rcode, (unsigned)id, qname, (unsigned)qtype);
        }
    } else {
        /* Response */
        if (rcode == 0) {
            out->severity = FLOWFORGE_SEVERITY_INFO;
        } else if (rcode == 2) {
            /* SERVFAIL — server-side failure, treat as alert */
            out->severity = FLOWFORGE_SEVERITY_ALERT;
        } else {
            out->severity = FLOWFORGE_SEVERITY_WARN;
        }
        std::snprintf(out->detail, sizeof(out->detail),
                      "DNS RCODE=%d", rcode);
    }

    out->status = FLOWFORGE_STATUS_OK;
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
