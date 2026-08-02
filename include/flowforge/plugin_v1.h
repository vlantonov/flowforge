#ifndef FLOWFORGE_PLUGIN_V1_H
#define FLOWFORGE_PLUGIN_V1_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Single source of truth for binary compatibility. Bump on any ABI struct change. */
#define FLOWFORGE_PLUGIN_ABI_VERSION 1u

/** Non-owning buffer view. Host retains ownership for the duration of process(). */
typedef struct {
    const uint8_t *data; /**< pointer into caller-owned memory */
    size_t         len;  /**< byte count; 0 is valid */
} flowforge_buf_t;

typedef enum flowforge_severity {
    FLOWFORGE_SEVERITY_UNKNOWN = 0,
    FLOWFORGE_SEVERITY_INFO    = 1,
    FLOWFORGE_SEVERITY_WARN    = 2,
    FLOWFORGE_SEVERITY_ALERT   = 3
} flowforge_severity_t;

typedef enum flowforge_status {
    FLOWFORGE_STATUS_OK             = 0,
    FLOWFORGE_STATUS_PARSE_ERROR    = 1,
    FLOWFORGE_STATUS_NOT_APPLICABLE = 2,
    FLOWFORGE_STATUS_INTERNAL_ERROR = 3
} flowforge_status_t;

/**
 * Result written by plugin::process().
 * All fields use fixed-width types; no pointers cross the ABI boundary.
 * Total size on LP64: 32 + 4 + 4 + 256 + 4 = 300 bytes.
 */
typedef struct {
    char                 protocol[32];  /**< null-terminated, e.g. "HTTP/1.1", "DNS", "TLS" */
    flowforge_severity_t severity;
    flowforge_status_t   status;
    char                 detail[256];   /**< null-terminated, human-readable summary */
    uint32_t             abi_version;   /**< set by plugin to FLOWFORGE_PLUGIN_ABI_VERSION */
} flowforge_result_t;

/**
 * Plugin vtable. abi_version MUST remain the first field at offset 0 for all future ABI versions.
 * Nullable pointers (init, destroy) are documented; non-nullable pointers must not be NULL.
 */
typedef struct flowforge_plugin {
    /** Returns FLOWFORGE_PLUGIN_ABI_VERSION compiled into the plugin. Always first field. */
    uint32_t    (*abi_version)(void);
    /** Returns statically-allocated plugin name string; lifetime is the process. */
    const char* (*name)(void);
    /** Optional. Called once before first process(). Returns 0 or negative errno. */
    int         (*init)(void);
    /** Required. Returns 0 on success; negative errno-style on hard failure. Must not throw. */
    int         (*process)(flowforge_buf_t buf, flowforge_result_t *out);
    /** Optional. Called once after last process(), before dlclose(). */
    void        (*destroy)(void);
} flowforge_plugin_t;

/** Type of the well-known entry-point export symbol. */
typedef const flowforge_plugin_t* (*flowforge_plugin_entry_fn)(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* Compile-time layout guards — must not drift silently on new platforms */
#ifdef __cplusplus
#  define FLOWFORGE_STATIC_ASSERT(expr, msg) static_assert((expr), msg)
#else
#  define FLOWFORGE_STATIC_ASSERT(expr, msg) _Static_assert((expr), msg)
#endif

FLOWFORGE_STATIC_ASSERT(sizeof(flowforge_result_t) == 300,
    "flowforge_result_t layout changed — bump FLOWFORGE_PLUGIN_ABI_VERSION");
FLOWFORGE_STATIC_ASSERT(sizeof(flowforge_buf_t) == sizeof(const uint8_t *) + sizeof(size_t),
    "flowforge_buf_t has unexpected padding");

#endif /* FLOWFORGE_PLUGIN_V1_H */
