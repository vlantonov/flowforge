#include "output.hpp"

#include <cstdio>
#include <cstring>

namespace {

const char* severity_to_str(flowforge_severity_t sev) noexcept {
    switch (sev) {
        case FLOWFORGE_SEVERITY_INFO:    return "INFO";
        case FLOWFORGE_SEVERITY_WARN:    return "WARN";
        case FLOWFORGE_SEVERITY_ALERT:   return "ALERT";
        case FLOWFORGE_SEVERITY_UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

/* Write a JSON-escaped string value (without surrounding quotes). */
void write_json_escaped(FILE* f, const char* s) {
    for (; *s; ++s) {
        unsigned char c = static_cast<unsigned char>(*s);
        if (c == '"')       { fputs("\\\"", f); }
        else if (c == '\\') { fputs("\\\\", f); }
        else if (c < 0x20)  { fprintf(f, "\\u%04x", (unsigned)c); }
        else                { fputc(c, f); }
    }
}

} // namespace

void write_ndjson(const char* plugin_name, const flowforge_result_t& result) {
    /* Format: {"plugin":"…","protocol":"…","severity":"…","detail":"…"} */
    fputs("{\"plugin\":\"",  stdout);
    write_json_escaped(stdout, plugin_name);
    fputs("\",\"protocol\":\"", stdout);
    write_json_escaped(stdout, result.protocol);
    fputs("\",\"severity\":\"", stdout);
    fputs(severity_to_str(result.severity), stdout);
    fputs("\",\"detail\":\"", stdout);
    write_json_escaped(stdout, result.detail);
    fputs("\"}\n", stdout);
}
