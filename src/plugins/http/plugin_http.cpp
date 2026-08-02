/**
 * plugin_http.cpp — HTTP/1.1 request/response classifier.
 * Hand-rolled single-pass parser; no allocations, no exceptions.
 */
#include <flowforge/plugin_v1.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

extern "C" {

/* extern forces external linkage (C++ const is internal by default) so dlsym can find this symbol. */
extern const uint32_t flowforge_plugin_abi_version __attribute__((used, visibility("default"))) = FLOWFORGE_PLUGIN_ABI_VERSION;

static uint32_t abi_version_fn() noexcept { return FLOWFORGE_PLUGIN_ABI_VERSION; }
static const char* name_fn()     noexcept { return "plugin_http"; }

static const char* const k_methods[] = {
    "GET ", "POST ", "PUT ", "DELETE ", "HEAD ",
    "OPTIONS ", "PATCH ", "TRACE ", "CONNECT ",
    nullptr
};

static int process(flowforge_buf_t buf, flowforge_result_t* out) {
    if (!out)          return -EINVAL;
    if (!buf.data)     return -EINVAL;
    if (buf.len < 4u)  return -EINVAL;

    std::memset(out, 0, sizeof(*out));
    std::strncpy(out->protocol, "HTTP/1.1", sizeof(out->protocol) - 1);
    out->abi_version = FLOWFORGE_PLUGIN_ABI_VERSION;

    const char* data = reinterpret_cast<const char*>(buf.data);
    const size_t len = buf.len;

    /* Detect direction. */
    bool is_request  = false;
    bool is_response = false;

    for (int i = 0; k_methods[i]; ++i) {
        size_t mlen = std::strlen(k_methods[i]);
        if (len >= mlen && std::memcmp(data, k_methods[i], mlen) == 0) {
            is_request = true;
            break;
        }
    }
    if (!is_request && len >= 5 && std::memcmp(data, "HTTP/", 5) == 0) {
        is_response = true;
    }

    if (!is_request && !is_response) {
        out->status   = FLOWFORGE_STATUS_NOT_APPLICABLE;
        out->severity = FLOWFORGE_SEVERITY_UNKNOWN;
        return 0;
    }

    /* Find end of first line (CRLF). */
    const char* crlf = nullptr;
    for (size_t i = 0; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            crlf = data + i;
            break;
        }
    }
    if (!crlf) {
        out->status   = FLOWFORGE_STATUS_PARSE_ERROR;
        out->severity = FLOWFORGE_SEVERITY_UNKNOWN;
        return 0;
    }
    const size_t line_len = static_cast<size_t>(crlf - data);

    if (is_request) {
        /* Request-line: METHOD SP request-target SP HTTP-version */
        const char* sp1 = static_cast<const char*>(std::memchr(data, ' ', line_len));
        if (!sp1) {
            out->status   = FLOWFORGE_STATUS_PARSE_ERROR;
            out->severity = FLOWFORGE_SEVERITY_UNKNOWN;
            return 0;
        }

        char method[17] = {};
        size_t mlen = static_cast<size_t>(sp1 - data);
        if (mlen > 16) mlen = 16;
        std::memcpy(method, data, mlen);

        /* Find request-target (between sp1+1 and next space or CRLF). */
        const char* target_start = sp1 + 1;
        size_t remaining = line_len - static_cast<size_t>(target_start - data);
        const char* sp2 = static_cast<const char*>(std::memchr(target_start, ' ', remaining));
        const char* target_end = sp2 ? sp2 : crlf;

        char path[129] = {};
        size_t plen = static_cast<size_t>(target_end - target_start);
        if (plen > 128) plen = 128;
        std::memcpy(path, target_start, plen);

        std::snprintf(out->detail, sizeof(out->detail), "%s %s", method, path);
        out->severity = FLOWFORGE_SEVERITY_INFO;
        out->status   = FLOWFORGE_STATUS_OK;

    } else {
        /* Status-line: HTTP-version SP status-code SP reason-phrase */
        const char* sp1 = static_cast<const char*>(std::memchr(data, ' ', line_len));
        if (!sp1) {
            out->status   = FLOWFORGE_STATUS_PARSE_ERROR;
            out->severity = FLOWFORGE_SEVERITY_UNKNOWN;
            return 0;
        }

        const char* code_start = sp1 + 1;
        int status_code = 0;
        if (code_start + 3 <= crlf) {
            status_code = (code_start[0] - '0') * 100
                        + (code_start[1] - '0') * 10
                        + (code_start[2] - '0');
        }

        char reason[65] = {};
        size_t rem2 = static_cast<size_t>(crlf - code_start);
        const char* sp2 = static_cast<const char*>(std::memchr(code_start, ' ', rem2));
        if (sp2) {
            size_t rlen = static_cast<size_t>(crlf - sp2 - 1);
            if (rlen > 64) rlen = 64;
            std::memcpy(reason, sp2 + 1, rlen);
        }

        if (status_code >= 100 && status_code < 400) {
            out->severity = FLOWFORGE_SEVERITY_INFO;
        } else if (status_code >= 400 && status_code < 500) {
            out->severity = FLOWFORGE_SEVERITY_WARN;
        } else if (status_code >= 500 && status_code < 600) {
            out->severity = FLOWFORGE_SEVERITY_ALERT;
        } else {
            out->severity = FLOWFORGE_SEVERITY_UNKNOWN;
        }

        std::snprintf(out->detail, sizeof(out->detail), "%d %s", status_code, reason);
        out->status = FLOWFORGE_STATUS_OK;
    }

    return 0;
}

static const flowforge_plugin_t k_vtable = {
    abi_version_fn,
    name_fn,
    nullptr,   /* init — not needed */
    process,
    nullptr    /* destroy — not needed */
};

const flowforge_plugin_t* flowforge_plugin_entry() noexcept {
    return &k_vtable;
}

} /* extern "C" */
