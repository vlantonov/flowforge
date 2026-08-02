#include <gtest/gtest.h>
#include <flowforge/plugin_v1.h>
#include "../src/host/loader.hpp"
#include "../src/host/dispatcher.hpp"

#include <cstring>

// PLUGIN_HTTP_PATH and PLUGIN_DNS_PATH are injected by CMake as string macros.
#ifndef PLUGIN_HTTP_PATH
#  error "PLUGIN_HTTP_PATH must be defined by CMake"
#endif
#ifndef PLUGIN_DNS_PATH
#  error "PLUGIN_DNS_PATH must be defined by CMake"
#endif

class MultiDispatchTest : public ::testing::Test {
protected:
    Loader loader;

    void SetUp() override {
        ASSERT_EQ(0, loader.load(PLUGIN_HTTP_PATH))
            << "Failed to load plugin_http from: " PLUGIN_HTTP_PATH;
        ASSERT_EQ(0, loader.load(PLUGIN_DNS_PATH))
            << "Failed to load plugin_dns from: " PLUGIN_DNS_PATH;
        ASSERT_EQ(2u, loader.plugins().size());
    }
};

// Dispatch an HTTP/1.1 request to both loaded plugins and verify both produce a result.
TEST_F(MultiDispatchTest, BothPluginsProduceResultForHttpBuffer) {
    static const char raw[] = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n";
    flowforge_buf_t buf = {
        reinterpret_cast<const uint8_t*>(raw),
        sizeof(raw) - 1  // exclude null terminator
    };

    auto results = Dispatcher::dispatch(buf, loader.plugins());

    // Both plugins must return a non-negative status (even if NOT_APPLICABLE or PARSE_ERROR),
    // so both results should be collected.
    ASSERT_EQ(2u, results.size())
        << "Expected results from exactly 2 plugins";

    // HTTP plugin result
    const DispatchResult& http_res = results[0];
    EXPECT_STREQ("plugin_http", http_res.plugin_name);
    EXPECT_EQ(FLOWFORGE_STATUS_OK,    http_res.result.status);
    EXPECT_EQ(FLOWFORGE_SEVERITY_INFO, http_res.result.severity);
    EXPECT_STREQ("HTTP/1.1", http_res.result.protocol);

    // DNS plugin result — HTTP data is not valid DNS; plugin returns PARSE_ERROR with rc==0.
    const DispatchResult& dns_res = results[1];
    EXPECT_STREQ("plugin_dns", dns_res.plugin_name);
    EXPECT_STREQ("DNS", dns_res.result.protocol);
    // The DNS plugin processes the buffer (len >= 12) and returns 0 with either
    // PARSE_ERROR (malformed QNAME) or OK (accidental parse); either is acceptable here.
    // The key invariant is that a result was produced at all.
}

// Verify dispatch returns an empty vector when no plugins are loaded.
TEST(MultiDispatchNoPluginsTest, EmptyPluginListYieldsNoResults) {
    std::vector<LoadedPlugin> empty;
    flowforge_buf_t buf = {nullptr, 0};
    auto results = Dispatcher::dispatch(buf, empty);
    EXPECT_TRUE(results.empty());
}
