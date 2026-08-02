# flowforge QA Test Report

## Build Environment

| Item | Value |
|------|-------|
| Compiler | clang-18 / clang++-18 |
| CMake | 3.28.3 |
| Conan | 2.11.0 |
| Build type | Debug |
| OS | Linux x86-64 |
| Build preset | `conan-debug` |

## Test Results

**33 / 33 tests passed.**

| Executable | Tests | Pass | Fail |
|---|---|---|---|
| `flowforge_test_http` | 11 | 11 | 0 |
| `flowforge_test_dns` | 10 | 10 | 0 |
| `flowforge_test_tls` | 8 | 8 | 0 |
| `flowforge_test_loader` | 4 | 4 | 0 |

### Selected test coverage

| Test | Requirement | Result |
|---|---|---|
| `HttpPluginTest.ValidGetRequest` | FR-07 | PASS |
| `HttpPluginTest.Valid200Response` | FR-07 | PASS |
| `HttpPluginTest.Response404IsWarn` | FR-08 | PASS |
| `HttpPluginTest.Response500IsAlert` | FR-08 | PASS |
| `HttpPluginTest.NullDataReturnsNegative` | NFR-05 | PASS |
| `DnsPluginTest.ValidAQueryIsInfo` | FR-09 | PASS |
| `DnsPluginTest.AnyQueryIsWarn` | FR-10 | PASS |
| `DnsPluginTest.ResponseRcode3IsWarn` | FR-10 | PASS |
| `DnsPluginTest.ServfailResponseIsAlert` | FR-10 | PASS (after fix) |
| `DnsPluginTest.PointerLoopIsParseError` | NFR-05 | PASS |
| `TlsPluginTest.MinimalClientHelloProducesJA3` | FR-11 | PASS |
| `TlsPluginTest.GreaseFilteredCipherMatchesWithoutGrease` | FR-12 | PASS |
| `TlsPluginTest.TruncatedRecordReturnsNegative` | NFR-05 | PASS |
| `LoaderTest.NonExistentPathReturnsError` | FR-03 | PASS |
| `LoaderTest.NotAnElfFileReturnsError` | FR-03 | PASS |

## Defects Found and Fixed

### DEFECT-001 — SERVFAIL (RCODE=2) mapped to WARN instead of ALERT

| Field | Value |
|---|---|
| File | `src/plugins/dns/plugin_dns.cpp` |
| Test | `DnsPluginTest.ServfailResponseIsAlert` |
| Severity | Minor (incorrect severity classification) |
| Fix | Added explicit `rcode == 2 → FLOWFORGE_SEVERITY_ALERT` branch in the response RCODE handler |
| Status | Fixed, tests pass |

## ABI Symbol Verification

All three plugin `.so` files export unmangled C symbols (verified with `nm -D`):

| Plugin | `flowforge_plugin_abi_version` | `flowforge_plugin_entry` |
|---|---|---|
| `plugin_http.so` | R (read-only data) | T (text) |
| `plugin_dns.so` | R | T |
| `plugin_tls_ja3.so` | R | T |

No C++ mangled names appear for either symbol.

## Smoke Test Results

### HTTP plugin
```
Input: GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n
Output: {"plugin":"plugin_http","protocol":"HTTP/1.1","severity":"INFO","detail":"GET /index.html"}
```
✓ NDJSON format correct, severity INFO, detail contains method + path.

### DNS plugin
```
Input: DNS A query for www.example.com (wire format)
Output: {"plugin":"plugin_dns","protocol":"DNS","severity":"INFO","detail":"qr=QUERY opcode=0 rcode=0 id=0x0001 qname=www.example.com qtype=1"}
```
✓ NDJSON format correct, qname correctly decoded from wire format.

## Coverage Gaps Identified

The following paths have no test coverage but are lower-priority for v0.1:

1. **TLS JA3 — real-world ClientHello bytes**: the test constructs a minimal synthetic ClientHello; a captured real browser ClientHello would provide higher confidence in the JA3 string value.
2. **HTTP chunked/pipelined requests**: the parser handles single-buffer input; multi-buffer sessions are outside scope of v0.1.
3. **Loader version mismatch path**: no test exercises `FLOWFORGE_PLUGIN_ABI_VERSION` mismatch rejection; requires building a mock plugin with wrong ABI version.
4. **Multiple plugins dispatched simultaneously**: `test_loader` covers load failures; no integration test loads two plugins and dispatches one buffer to both.

None of these gaps block the v0.1 release.

## Recommendation

**PASS — ready for Release Engineer to tag and publish v0.1.0.**
