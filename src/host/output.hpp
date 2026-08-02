#pragma once
#include <flowforge/plugin_v1.h>

/** Write one NDJSON line to stdout for a single plugin result. */
void write_ndjson(const char* plugin_name, const flowforge_result_t& result);
