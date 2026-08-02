# Software Requirements Specification — FlowForge Plugin Host

**Document ID:** FF-SRS-001  
**Version:** 0.1.0  
**Date:** 2026-08-02  
**Author:** Requirements Analyst Agent  
**Status:** Draft — Pending Architect Review  

---

## Table of Contents

1. [Purpose and Scope](#1-purpose-and-scope)
2. [Stakeholders](#2-stakeholders)
3. [Functional Requirements](#3-functional-requirements)
4. [Non-Functional Requirements](#4-non-functional-requirements)
5. [Plugin ABI Contract Requirements](#5-plugin-abi-contract-requirements)
6. [Constraints](#6-constraints)
7. [Acceptance Criteria Summary](#7-acceptance-criteria-summary)
8. [Open Questions](#8-open-questions)
9. [Glossary](#9-glossary)
10. [References](#10-references)

---

## 1. Purpose and Scope

### 1.1 Problem Statement

Network traffic analysis tools commonly need to classify and decode multiple
application-layer protocols in the same process. Hard-coding protocol parsers
into a single binary creates tight coupling that makes adding, replacing, or
versioning parsers difficult without recompiling and redeploying the entire
application.

FlowForge solves this by introducing a **host binary** that discovers and loads
protocol-parser plugins at runtime using the POSIX `dlopen` API. Each plugin is
an independent shared library exporting a well-defined, stable **C ABI**. The
host dispatches raw byte buffers to whichever plugin claims the traffic type,
receives structured results through the ABI, and logs or prints them.

### 1.2 Portfolio Rationale

FlowForge is explicitly designed as a C++ portfolio project. It demonstrates
mastery of the following topics that senior hiring reviewers look for:

- **ABI stability engineering** — designing a plain-C interface that survives
  independent compilation of host and plugins.
- **Dynamic loading on POSIX** — correct use of `dlopen`, `dlsym`, `dlclose`,
  and `RTLD_*` flags.
- **Network protocol parsing** — binary wire-format decoding for DNS and TLS;
  text-framing for HTTP/1.1.
- **Semantic versioning of interfaces** — a version constant in the ABI header
  and a compatibility check at plugin load time.
- **CMake modern idioms** — target-based dependency model, Conan 2 integration,
  install rules.
- **GoogleTest / Catch2 testing** — unit tests for each parser, integration
  tests for the host–plugin boundary.

### 1.3 Scope

**In scope for v0.1:**

- A host binary (`flowforge`) that loads, validates, and exercises plugins.
- A public, stable C ABI header (`flowforge_plugin_v1.h`).
- Three sample plugins:
  - `plugin_http` — HTTP/1.1 request and response classification.
  - `plugin_dns` — DNS wire-format message parsing.
  - `plugin_tls_ja3` — TLS ClientHello parsing and JA3 fingerprint generation.
- A CMake build system that builds all targets from a single root
  `CMakeLists.txt`.
- Conan 2 integration for third-party dependencies.
- Unit and integration tests for every plugin and the host loader.
- A README describing build instructions and plugin authoring guide.

**Not in scope for v0.1** — see [Section 7](#7-acceptance-criteria-summary).

---

## 2. Stakeholders

| ID | Role | Interest |
|----|------|----------|
| SH-1 | **Portfolio Reviewer** (primary user) | Reads source code, runs the build, verifies tests pass. Evaluates code quality, ABI discipline, and protocol-correctness. |
| SH-2 | **Plugin Author** (secondary user) | Wants to write a new plugin against the published ABI header without touching host source. Needs a clear authoring guide and a working example. |
| SH-3 | **Project Maintainer** (owner) | Evolves the ABI across versions without breaking existing plugins; manages releases and CI. |

---

## 3. Functional Requirements

### 3.1 Host Binary

#### FR-01 — Plugin Discovery via Command-Line Path

**Description:** The host binary shall accept one or more shared-library paths
as positional command-line arguments and attempt to load each one in the order
provided.

**Acceptance Criterion:** Running `flowforge ./libplugin_dns.so` loads exactly
one plugin; running `flowforge ./libplugin_http.so ./libplugin_dns.so` loads
exactly two plugins, in the stated order.

**Priority:** Must

---

#### FR-02 — Plugin ABI Version Validation

**Description:** After calling `dlopen`, the host shall call `dlsym` to
retrieve the `flowforge_plugin_abi_version` symbol from the loaded library.
If the returned value does not match `FLOWFORGE_PLUGIN_ABI_VERSION` defined in
the host's own copy of the ABI header, the host shall unload the library with
`dlclose` and emit a descriptive error message to `stderr`.

**Acceptance Criterion:** Loading a plugin compiled against an older (mismatched)
ABI version causes the host to print an error containing the word "incompatible"
or "version" to `stderr`, exit with a non-zero status, and not crash.

**Priority:** Must

---

#### FR-03 — Plugin Descriptor Retrieval

**Description:** After ABI version validation, the host shall call `dlsym` to
retrieve the `flowforge_plugin_descriptor` symbol, which returns a pointer to a
`flowforge_plugin_desc_t` struct containing at minimum: `name` (C string),
`version` (C string), and function pointers for `init`, `process`, and
`destroy`.

**Acceptance Criterion:** The host successfully reads `name` and `version` from
all three sample plugins and prints them to `stdout` during startup.

**Priority:** Must

---

#### FR-04 — Plugin Lifecycle Management

**Description:** The host shall call the plugin's `init` function exactly once
after successful descriptor retrieval, call `process` zero or more times during
operation, and call `destroy` exactly once before calling `dlclose`. Out-of-order
calls (e.g., `process` before `init`, or double `destroy`) are explicitly
forbidden by the host.

**Acceptance Criterion:** A test-spy plugin that records call order confirms
init → (process)* → destroy → dlclose sequence with no deviations.

**Priority:** Must

---

#### FR-05 — Graceful Error Handling on dlopen Failure

**Description:** If `dlopen` fails for any path (file not found, not a shared
library, missing symbols), the host shall print the `dlerror()` string to
`stderr` and continue attempting to load remaining paths on the command line.
The host shall exit non-zero if any plugin failed to load.

**Acceptance Criterion:** `flowforge ./nonexistent.so ./libplugin_dns.so` loads
the DNS plugin successfully, prints an error for the missing file, and exits
with a non-zero code.

**Priority:** Must

---

#### FR-06 — Input Dispatch to Plugins

**Description:** The host shall read raw byte buffers from `stdin` (or from
named files passed via a `--input` flag) and dispatch each buffer to every
loaded plugin's `process` function in load order.

**Acceptance Criterion:** Piping a captured HTTP request to `flowforge
./libplugin_http.so` invokes `plugin_http`'s `process` function with the exact
bytes and length of the captured request.

**Priority:** Must

---

#### FR-07 — Structured Result Output

**Description:** Each call to a plugin's `process` function returns a
`flowforge_result_t` struct (defined in the ABI header). The host shall format
and print the contents of every non-null result to `stdout` as a single JSON
object per result, one per line (NDJSON).

**Acceptance Criterion:** Processing a valid DNS query produces at least one
NDJSON line on `stdout` containing the field `"plugin"` set to `"dns"`.

**Priority:** Must

---

### 3.2 HTTP Plugin (`plugin_http`)

#### FR-08 — HTTP/1.1 Request Line Parsing

**Description:** The plugin shall parse the request line of an HTTP/1.1 message
and extract: HTTP method, request-target (path + query string), and HTTP
version string.

**Acceptance Criterion:** A unit test feeding the byte sequence
`"GET /index.html HTTP/1.1\r\n..."` produces a result with `method="GET"`,
`target="/index.html"`, `http_version="HTTP/1.1"`.

**Priority:** Must

---

#### FR-09 — HTTP/1.1 Response Status Line Parsing

**Description:** The plugin shall parse the status line of an HTTP/1.1 response
and extract: HTTP version, status code (integer), and reason phrase.

**Acceptance Criterion:** A unit test feeding `"HTTP/1.1 200 OK\r\n..."` yields
`status_code=200` and `reason="OK"`.

**Priority:** Must

---

#### FR-10 — HTTP Message Direction Classification

**Description:** The plugin shall classify each buffer as either `REQUEST`,
`RESPONSE`, or `UNKNOWN` based on the first line of the HTTP message.

**Acceptance Criterion:** A buffer beginning with a known HTTP method keyword is
classified `REQUEST`; a buffer beginning with `"HTTP/"` is classified
`RESPONSE`; anything else is classified `UNKNOWN`. All three cases are covered
by unit tests.

**Priority:** Must

---

#### FR-11 — HTTP Header Extraction

**Description:** The plugin shall extract all HTTP header fields as key–value
pairs (case-insensitive field names, trimmed values) and include them in the
result.

**Acceptance Criterion:** A buffer containing `"Content-Type: text/html\r\n"`
produces a result where the header map contains `content-type` → `text/html`.

**Priority:** Should

---

#### FR-12 — HTTP Body Pass-Through

**Description:** The plugin shall include the raw body bytes (if present) in the
result as a base64-encoded string. No body decompression or interpretation is
required.

**Acceptance Criterion:** A POST request with a known 10-byte body produces a
result whose `body_b64` field decodes back to those exact 10 bytes.

**Priority:** Nice-to-have

---

### 3.3 DNS Plugin (`plugin_dns`)

#### FR-13 — DNS Header Parsing

**Description:** The plugin shall parse the 12-byte DNS message header (RFC 1035
§4.1.1) and extract: ID, QR bit, OPCODE, AA, TC, RD, RA, RCODE, QDCOUNT,
ANCOUNT, NSCOUNT, ARCOUNT.

**Acceptance Criterion:** A unit test feeding the raw bytes of a known DNS query
(captured with Wireshark or equivalent) produces the correct field values as
verified against the capture's known-good values.

**Priority:** Must

---

#### FR-14 — DNS Question Section Parsing

**Description:** The plugin shall parse the Question Section of a DNS message,
decoding the QNAME (including pointer compression per RFC 1035 §4.1.4), QTYPE,
and QCLASS for each entry.

**Acceptance Criterion:** A DNS query for `www.example.com` (type A, class IN)
produces `qname="www.example.com"`, `qtype=1`, `qclass=1`.

**Priority:** Must

---

#### FR-15 — DNS Answer Section Parsing

**Description:** The plugin shall parse each Resource Record in the Answer
Section, extracting NAME, TYPE, CLASS, TTL, RDLENGTH, and RDATA.

**Acceptance Criterion:** A DNS response for `www.example.com` with a known A
record produces a result with the correct IPv4 address in the RDATA field.

**Priority:** Must

---

#### FR-16 — DNS Pointer Compression Safety

**Description:** The DNS name-decompression logic shall detect and reject
infinite pointer loops. If a pointer loop is detected, `process` shall set the
result status to `FLOWFORGE_STATUS_PARSE_ERROR` and return without crashing.

**Acceptance Criterion:** A crafted packet containing a pointer that points to
itself causes `process` to return `FLOWFORGE_STATUS_PARSE_ERROR` within a
bounded number of iterations (≤ 128 pointer hops).

**Priority:** Must

---

#### FR-17 — DNS Message Direction Classification

**Description:** The plugin shall classify each DNS buffer as `QUERY` (QR=0) or
`RESPONSE` (QR=1) based on the QR bit in the header.

**Acceptance Criterion:** Unit tests confirm correct `QUERY`/`RESPONSE`
classification for both a captured query and a captured response.

**Priority:** Must

---

### 3.4 TLS JA3 Plugin (`plugin_tls_ja3`)

#### FR-18 — TLS Record Layer Header Parsing

**Description:** The plugin shall parse the TLS record layer header (content
type, legacy version, length) to identify TLS Handshake records (content type
0x16).

**Acceptance Criterion:** A buffer containing a TLS Handshake record is
identified; a buffer with content type 0x17 (Application Data) is classified
as not a ClientHello and the result status is set to
`FLOWFORGE_STATUS_NOT_APPLICABLE`.

**Priority:** Must

---

#### FR-19 — TLS ClientHello Parsing

**Description:** For TLS Handshake records, the plugin shall parse the
ClientHello message and extract: legacy version (2 bytes), random (32 bytes),
session ID, cipher suites list, compression methods list, and extensions list.

**Acceptance Criterion:** A captured TLS 1.3 ClientHello (which advertises TLS
1.3 via the `supported_versions` extension) is parsed such that the cipher
suites list and extensions list match the known-good values from the capture.

**Priority:** Must

---

#### FR-20 — JA3 String Construction

**Description:** The plugin shall construct the JA3 string by concatenating the
following comma-separated fields with `|` between field groups, using the
algorithm defined by the Salesforce JA3 specification:
`SSLVersion,Ciphers,Extensions,EllipticCurves,EllipticCurvePointFormats`.
GREASE values (RFC 8701) shall be excluded before construction.

**Acceptance Criterion:** A captured ClientHello whose JA3 hash is publicly
known (e.g., from the Salesforce JA3 test vector set) produces the exact same
JA3 string as the reference.

**Priority:** Must

---

#### FR-21 — JA3 MD5 Hash Production

**Description:** The plugin shall compute the MD5 hash of the JA3 string and
include the 32-character lowercase hex digest in the result under the field
`ja3`.

**Acceptance Criterion:** Processing the same ClientHello as FR-20 yields a
`ja3` field equal to the known reference MD5 hash.

**Priority:** Must

---

#### FR-22 — GREASE Value Filtering

**Description:** The plugin shall filter out GREASE values (as specified in
RFC 8701) from cipher suites, extensions, elliptic curves, and point formats
before building the JA3 string.

**Acceptance Criterion:** A ClientHello that contains at least one GREASE cipher
suite value produces a JA3 string and hash that match those produced by the
reference `ja3` Python library for the same input.

**Priority:** Must

---

### 3.5 Build System

#### FR-23 — Single-Command Build

**Description:** Running `cmake --preset <preset> && cmake --build --preset
<preset>` from the repository root shall build all targets: the host binary,
all three plugin shared libraries, and the test binary.

**Acceptance Criterion:** A clean clone followed by the documented build
commands succeeds on Ubuntu 22.04 LTS (x86-64) with no manual steps beyond
installing Conan 2 and CMake ≥ 3.21.

**Priority:** Must

---

#### FR-24 — Test Execution via CTest

**Description:** All unit and integration tests shall be registered with CTest
so that `ctest --preset <preset>` runs them and reports pass/fail.

**Acceptance Criterion:** `ctest --preset <preset>` exits zero when all tests
pass; exits non-zero when any test fails.

**Priority:** Must

---

#### FR-25 — Install Target

**Description:** The CMake build shall provide an install target that installs
the host binary, all plugin shared libraries, and the ABI header to the
`CMAKE_INSTALL_PREFIX`.

**Acceptance Criterion:** After `cmake --install`, the host binary, plugins, and
header are present under the install prefix; a new shell session can run the
host binary pointing at the installed plugins without referencing the build
directory.

**Priority:** Should

---

---

## 4. Non-Functional Requirements

#### NFR-01 — ABI Stability

**Description:** The `flowforge_plugin_v1.h` header shall constitute a stable
ABI boundary. All types crossing the boundary (structs, enums, function
pointers) shall be plain-C types only. No C++ standard-library types, virtual
function tables, or exceptions shall cross the boundary.

**Acceptance Criterion:** A plugin compiled with GCC and a host compiled with
Clang (both targeting the same `FLOWFORGE_PLUGIN_ABI_VERSION`) can be loaded
and exercised without undefined behaviour as confirmed by AddressSanitizer and
UBSan clean runs.

**Priority:** Must

---

#### NFR-02 — Build Reproducibility

**Description:** Two clean builds from the same commit on the same machine and
toolchain shall produce bit-identical binaries (deterministic builds). Conan 2
lock files shall be committed to the repository to pin dependency versions.

**Acceptance Criterion:** Running the build twice yields binary hashes (SHA-256)
that are identical for the host binary and all plugins.

**Priority:** Should

---

#### NFR-03 — Portability — Linux x86-64

**Description:** All targets shall build and all tests shall pass on Linux
x86-64 with GCC ≥ 12 and Clang ≥ 16.

**Acceptance Criterion:** CI (or a manually run matrix) reports green for both
toolchains.

**Priority:** Must

---

#### NFR-04 — Portability — macOS (Stretch Goal)

**Description:** All targets shall build and all tests shall pass on macOS 13+
(Apple Silicon or x86-64) with Apple Clang 15+.

**Acceptance Criterion:** A `macos` CI job (or documented manual steps) reports
green.

**Priority:** Nice-to-have

---

#### NFR-05 — Plugin Dispatch Overhead

**Description:** The per-buffer overhead introduced by the host's dispatch
loop (excluding the time spent inside a plugin's `process` function) shall be
less than 100 microseconds on a buffer of 1 500 bytes on the target hardware.

**Acceptance Criterion:** A microbenchmark (not part of the regular CTest suite)
reports p99 dispatch overhead < 100 µs on the reference machine.

**Priority:** Should

---

#### NFR-06 — No Dynamic Memory Allocation in the Hot Path

**Description:** The host's dispatch loop shall not call `malloc`/`free` (or
their C++ equivalents) on a per-buffer basis. Any per-plugin state shall be
pre-allocated during `init`.

**Acceptance Criterion:** Valgrind massif or HeapTrack confirms zero heap
allocations in the dispatch loop during a 10 000-buffer replay run.

**Priority:** Should

---

#### NFR-07 — Documentation Completeness

**Description:** The repository shall contain:
  (a) a README with build instructions and a quick-start example,
  (b) the ABI header with Doxygen-compatible comments on every exported type
      and function pointer,
  (c) a `PLUGIN_AUTHORING.md` guide explaining how to write a new plugin.

**Acceptance Criterion:** A developer unfamiliar with the project can build,
run, and write a skeleton plugin following only the README and
`PLUGIN_AUTHORING.md` within 30 minutes (validated by peer review).

**Priority:** Should

---

#### NFR-08 — C++20 Standard

**Description:** All C++ source files shall be compiled with `-std=c++20` (or
equivalent). No compiler extensions shall be relied upon.

**Acceptance Criterion:** The build succeeds with `-std=c++20 -pedantic-errors`
on GCC ≥ 12 and Clang ≥ 16.

**Priority:** Must

---

#### NFR-09 — Sanitizer-Clean

**Description:** All tests shall pass under AddressSanitizer (`-fsanitize=address`)
and UndefinedBehaviourSanitizer (`-fsanitize=undefined`) with no errors.

**Acceptance Criterion:** A CMake preset named `sanitize` enables both
sanitizers; `ctest --preset sanitize` exits zero.

**Priority:** Must

---

#### NFR-10 — License Compliance

**Description:** All source files shall be licensed under the MIT License
(matching the existing `LICENSE` file). Any third-party dependency incorporated
via Conan shall have a license compatible with MIT distribution.

**Acceptance Criterion:** No Conan dependency has a license that is
GPL-only or otherwise incompatible with MIT distribution as confirmed by
`conan graph info` output.

**Priority:** Must

---

---

## 5. Plugin ABI Contract Requirements

### 5.1 C Language Linkage

**ABI-01:** Every symbol exported by a plugin that is consumed by the host
shall be declared with `extern "C"` linkage (or be in a pure-C translation
unit). No mangled C++ symbols shall appear in the host–plugin interface.

### 5.2 No C++ Exceptions Across Boundary

**ABI-02:** Plugin implementations shall not allow C++ exceptions to propagate
across the ABI boundary. All error conditions shall be signalled via the
`flowforge_result_t.status` field using the `flowforge_status_t` enum.

### 5.3 No STL Types in the ABI

**ABI-03:** No `std::` types (including `std::string`, `std::vector`,
`std::span`, etc.) shall appear in any type that crosses the host–plugin
boundary. All strings shall be null-terminated `const char *`; all buffers
shall be `const uint8_t * data, size_t length` pairs.

### 5.4 Versioning Constant

**ABI-04:** The ABI header shall define an integer constant
`FLOWFORGE_PLUGIN_ABI_VERSION`. The current value for v0.1 is `1`. Every
plugin shall export a `uint32_t flowforge_plugin_abi_version` variable
initialised to this constant. The host shall compare the loaded value against
its compile-time constant before calling any other plugin symbol.

### 5.5 Struct Layout Stability

**ABI-05:** All structs in the ABI header shall use fixed-width integer types
(`uint8_t`, `uint16_t`, `uint32_t`, `uint64_t` from `<stdint.h>`). Struct
fields shall not be reordered or removed in a minor-version bump; new fields
may only be appended at the end of a struct, and `FLOWFORGE_PLUGIN_ABI_VERSION`
shall be incremented for any such change.

### 5.6 Ownership Rules

**ABI-06:** Any pointer returned inside `flowforge_result_t` is owned by the
plugin. The host shall not free it. The plugin shall guarantee those pointers
remain valid until the next call to `process` or `destroy` on the same plugin
instance.

---

## 6. Constraints

| ID | Constraint |
|----|-----------|
| CON-01 | C++ standard: C++20 minimum. No earlier standard permitted. |
| CON-02 | POSIX only. The host uses `dlopen`/`dlsym`/`dlclose` from POSIX; no Windows (`LoadLibrary`) support is required for v0.1. |
| CON-03 | CMake version: ≥ 3.21 (required for `cmake --preset` support). |
| CON-04 | Package manager: Conan 2.x only. Conan 1.x is not supported. |
| CON-05 | No vendored third-party source trees. All external dependencies shall be declared in `conanfile.py` or `conanfile.txt`. |
| CON-06 | The JA3 hash requires MD5. No cryptographic security is implied; the MD5 implementation may come from a Conan dependency or a public-domain single-file library. |
| CON-07 | No runtime configuration files, databases, or network access are required by the host. All input comes from command-line arguments or `stdin`. |

---

## 7. Acceptance Criteria Summary

### Out of Scope for v0.1

The following items have been explicitly deferred to future iterations:

- Windows (`LoadLibrary`) support.
- Plugin hot-reload (loading a new version of a plugin without restarting the
  host).
- A plugin registry / plugin discovery by scanning a directory.
- TLS ServerHello parsing or full TLS handshake reconstruction.
- HTTP/2 or HTTP/3 support.
- DNS over TLS or DNS over HTTPS.
- A graphical or web-based UI.
- Persistent storage of parsed results (database, file sink).
- Live packet capture via `libpcap` (input is currently raw byte buffers from
  files or stdin).

---

## 8. Open Questions

| ID | Question | Impact |
|----|----------|--------|
| OQ-01 | Should the host support loading plugins from a directory (glob `*.so`) as an alternative to explicit path arguments? | Affects FR-01 scope and directory-scan logic complexity. |
| OQ-02 | Is a `--input` flag for named files required at v0.1, or is `stdin`-only sufficient? | Affects FR-06 acceptance criterion. |
| OQ-03 | What is the maximum buffer size the host should accept per read? Is there an upper bound (e.g. 64 KiB, Ethernet MTU)? | Impacts NFR-06 (pre-allocation sizing) and protocol plugin assumptions. |
| OQ-04 | Should NDJSON output be the only format, or should the host support a `--format` flag (e.g., CSV, plain text)? | Affects FR-07 scope. |
| OQ-05 | Is the MD5 implementation for JA3 permitted to be pulled in via Conan, or must it be a single vendored header to keep dependencies minimal? | Impacts CON-05 and CON-06 reconciliation. |
| OQ-06 | Must the test suite include fuzzing (libFuzzer or AFL++) for the DNS pointer-compression decompressor and TLS parser? | If yes, adds significant scope to the testing requirements. |
| OQ-07 | Is macOS support (NFR-04) a hard requirement for a CI job, or a "best effort" stretch goal? | Determines whether CI infrastructure needs macOS runners. |
| OQ-08 | Should the `flowforge_result_t` include a confidence score (e.g., for UNKNOWN classifications), or is a binary classified/unclassified status sufficient? | Affects ABI struct design. |

---

## 9. Glossary

| Term | Definition |
|------|-----------|
| **Plugin** | A POSIX shared library (`.so` on Linux, `.dylib` on macOS) that exports the `flowforge_plugin_v1` ABI symbols and implements a specific protocol parser. |
| **Host** | The `flowforge` executable that uses `dlopen` to load one or more plugins, manages their lifecycle, and dispatches raw byte buffers to them. |
| **ABI Version** | An integer constant (`FLOWFORGE_PLUGIN_ABI_VERSION`) compiled into both the host and each plugin, used to detect binary-incompatible interface changes at plugin load time. |
| **JA3 Fingerprint** | A method of fingerprinting TLS clients by hashing a canonical string derived from selected fields of the TLS ClientHello message. Developed by John Althouse, Jeff Atkinson, and Josh Atkins at Salesforce. The hash is an MD5 digest of the form `SSLVersion,Ciphers,Extensions,EllipticCurves,EllipticCurvePointFormats`. |
| **ClientHello** | The first message sent by a TLS client during the TLS handshake (RFC 8446 §4.1.2). It contains the client's supported protocol versions, cipher suites, extensions, and random nonce. |
| **GREASE** | Generate Random Extensions And Sustain Extensibility (RFC 8701). Reserved values inserted into TLS fields to prevent middleboxes from ossifying on known values. GREASE values must be excluded from JA3 computation. |
| **Wire Format** | The byte-level encoding of a network protocol message as it appears on the network, before any application-layer interpretation. |
| **NDJSON** | Newline-Delimited JSON. A format where each line is a valid, self-contained JSON object. Used by the host for structured output. |
| **POSIX** | Portable Operating System Interface — the family of standards that defines `dlopen`, `dlsym`, `dlclose`, and related dynamic-linking APIs used by the host. |

---

## 10. References

| ID | Reference |
|----|-----------|
| REF-01 | RFC 7230 — Hypertext Transfer Protocol (HTTP/1.1): Message Syntax and Routing. IETF, 2014. https://www.rfc-editor.org/rfc/rfc7230 |
| REF-02 | RFC 1035 — Domain Names — Implementation and Specification. IETF, 1987. https://www.rfc-editor.org/rfc/rfc1035 |
| REF-03 | RFC 8446 — The Transport Layer Security (TLS) Protocol Version 1.3. IETF, 2018. https://www.rfc-editor.org/rfc/rfc8446 |
| REF-04 | RFC 8701 — Applying Generate Random Extensions And Sustain Extensibility (GREASE) to TLS Extensibility. IETF, 2020. https://www.rfc-editor.org/rfc/rfc8701 |
| REF-05 | `dlopen(3)` — Linux man page. https://man7.org/linux/man-pages/man3/dlopen.3.html |
| REF-06 | JA3 — A Method for Profiling SSL/TLS Client Hello Messages. Salesforce Engineering / John Althouse. https://github.com/salesforce/ja3 |
| REF-07 | CMake Presets documentation. https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html |
| REF-08 | Conan 2 documentation. https://docs.conan.io/2/ |
| REF-09 | GoogleTest documentation. https://google.github.io/googletest/ |

---

*Requirements complete — ready for the System Architect agent to design against.*
