# FlowForge

flowforge — a POSIX dlopen plugin host with a stable C ABI, demonstrated with HTTP/1.1, DNS, and TLS/JA3 fingerprinting plugins.

---

## Architecture

```
┌─────────────────────────────────────────┐
│           flowforge (host binary)       │
│                                         │
│  Loader ──dlopen()──► plugin_http.so    │
│          ──dlopen()──► plugin_dns.so    │
│          ──dlopen()──► plugin_tls_ja3.so│
│                                         │
│  Dispatcher ──► ABI vtable ──► process()│
│             ◄── flowforge_result_t ◄─── │
└─────────────────────────────────────────┘

ABI boundary (include/flowforge/plugin_v1.h):
  host binary                 plugin .so
       │                           │
       │  flowforge_plugin_entry() │
       │◄──────────────────────────│
       │  → flowforge_plugin_t*    │
       │    (vtable: name/init/    │
       │     process/destroy)      │
       │                           │
       │  process(buf, &result)    │
       │──────────────────────────►│
       │  ← flowforge_result_t     │
       │◄──────────────────────────│
```

---

## Build Instructions

Prerequisites: Conan 2, CMake ≥ 3.21, clang-18 or gcc.

```bash
# 1. Install dependencies via Conan
conan install . \
  --profile:host  conan/profiles/linux-clang18 \
  --profile:build conan/profiles/linux-clang18 \
  --build=missing \
  -s:h build_type=Debug \
  -s:b build_type=Debug

# 2. Configure
cmake --preset conan-debug

# 3. Build (host binary + all plugins + tests)
cmake --build --preset conan-debug --parallel
```

---

## Usage

```bash
./build/Debug/flowforge --plugin ./build/Debug/plugin_http.so --input req.bin
./build/Debug/flowforge --plugin ./build/Debug/plugin_dns.so  --input query.bin

# Load multiple plugins for a single buffer
./build/Debug/flowforge \
  --plugin ./build/Debug/plugin_http.so \
  --plugin ./build/Debug/plugin_dns.so  \
  --input  req.bin
```

---

## Plugin ABI

The sole header that crosses the binary boundary is `include/flowforge/plugin_v1.h`:

```c
/** Single source of truth for binary compatibility. Bump on any ABI struct change. */
#define FLOWFORGE_PLUGIN_ABI_VERSION 1u

/** Non-owning buffer view passed into process(). Host retains ownership. */
typedef struct {
    const uint8_t *data;
    size_t         len;
} flowforge_buf_t;

typedef enum flowforge_severity { ... } flowforge_severity_t;
typedef enum flowforge_status   { ... } flowforge_status_t;

/** Result written by process(). No pointers cross the ABI boundary. */
typedef struct {
    char                 protocol[32];
    flowforge_severity_t severity;
    flowforge_status_t   status;
    char                 detail[256];
    uint32_t             abi_version;
} flowforge_result_t;

/** Plugin vtable — abi_version must remain first field at offset 0. */
typedef struct flowforge_plugin {
    uint32_t    (*abi_version)(void);   /* always first */
    const char* (*name)(void);
    int         (*init)(void);          /* nullable */
    int         (*process)(flowforge_buf_t, flowforge_result_t *);
    void        (*destroy)(void);       /* nullable */
} flowforge_plugin_t;

/** Well-known entry-point symbol every plugin must export. */
typedef const flowforge_plugin_t* (*flowforge_plugin_entry_fn)(void);
```

### `FLOWFORGE_PLUGIN_ABI_VERSION`

This compile-time constant is the single gatekeeper for binary compatibility.
The host reads the `flowforge_plugin_abi_version` data symbol from the `.so`
via `dlsym` **before** touching any vtable bytes. If the value does not equal
`FLOWFORGE_PLUGIN_ABI_VERSION` compiled into the host, `Loader::load()` returns
`-1` and the plugin is never used. Bump this constant whenever any ABI struct
changes layout.

### `flowforge_plugin_entry` export contract

Every plugin `.so` must export exactly two symbols with default ELF visibility:

| Symbol | Kind | Description |
|---|---|---|
| `flowforge_plugin_abi_version` | `const uint32_t` data | Must equal `FLOWFORGE_PLUGIN_ABI_VERSION`. Checked before vtable access. |
| `flowforge_plugin_entry` | function | Returns a pointer to a static `flowforge_plugin_t` vtable. Must not return NULL. |

---

## Writing a New Plugin

Minimal skeleton (C or C++):

```c
#include <flowforge/plugin_v1.h>
#include <cerrno>
#include <cstring>

extern "C" {

/* Required: ABI version data symbol — must have default visibility. */
extern const uint32_t flowforge_plugin_abi_version
    __attribute__((used, visibility("default"))) = FLOWFORGE_PLUGIN_ABI_VERSION;

/* Required: five vtable functions. */
static uint32_t abi_version_fn(void)  { return FLOWFORGE_PLUGIN_ABI_VERSION; }
static const char* name_fn(void)      { return "plugin_myproto"; }
static int init_fn(void)              { return 0; }   /* 0 = success */
static void destroy_fn(void)          {}

static int process_fn(flowforge_buf_t buf, flowforge_result_t* out) {
    if (!out || !buf.data) return -EINVAL;
    memset(out, 0, sizeof(*out));
    strncpy(out->protocol, "MYPROTO", sizeof(out->protocol) - 1);
    out->abi_version = FLOWFORGE_PLUGIN_ABI_VERSION;
    out->severity    = FLOWFORGE_SEVERITY_INFO;
    out->status      = FLOWFORGE_STATUS_OK;
    strncpy(out->detail, "parsed ok", sizeof(out->detail) - 1);
    return 0;
}

static const flowforge_plugin_t k_vtable = {
    abi_version_fn, name_fn, init_fn, process_fn, destroy_fn
};

/* Required: well-known entry-point export. */
const flowforge_plugin_t* flowforge_plugin_entry(void) { return &k_vtable; }

} /* extern "C" */
```

Add to `CMakeLists.txt`:

```cmake
add_library(plugin_myproto MODULE src/plugins/myproto/plugin_myproto.cpp)
target_link_libraries(plugin_myproto PRIVATE flowforge_plugin_api)
set_target_properties(plugin_myproto PROPERTIES PREFIX "")
```

---

## Shipped Plugins

| Plugin | Protocol | Key detection | Severity mapping |
|---|---|---|---|
| `plugin_http` | HTTP/1.1 | Method prefix (`GET`, `POST`, …) or `HTTP/` response line | INFO for 1xx–3xx; WARN for 4xx; ALERT for 5xx; NOT_APPLICABLE for non-HTTP |
| `plugin_dns` | DNS (RFC 1035 §4) | Wire-format binary header; 12-byte minimum length | INFO for standard queries/responses; WARN for ANY queries or non-zero rcode; ALERT for SERVFAIL (rcode 2) |
| `plugin_tls_ja3` | TLS ClientHello | TLS record byte `0x16` + handshake type `0x01` | INFO for valid JA3 fingerprint; WARN for unusual cipher/extension combinations; PARSE_ERROR for malformed records |

---

## Running Tests

```bash
ctest --test-dir build/Debug --output-on-failure
```

To run a specific test binary directly:

```bash
./build/Debug/flowforge_test_http
./build/Debug/flowforge_test_dns
./build/Debug/flowforge_test_tls
./build/Debug/flowforge_test_loader
./build/Debug/flowforge_test_version_mismatch
./build/Debug/flowforge_test_multi_dispatch
```

---

## License

MIT — see [LICENSE](LICENSE).
