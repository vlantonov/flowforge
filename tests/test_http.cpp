#include <gtest/gtest.h>
#include <flowforge/plugin_v1.h>
#include <cstring>

extern "C" const flowforge_plugin_t* flowforge_plugin_entry();

class HttpPluginTest : public ::testing::Test {
protected:
    const flowforge_plugin_t* plugin = flowforge_plugin_entry();

    int call(const char* data, flowforge_result_t& result) {
        result = {};
        flowforge_buf_t buf = {reinterpret_cast<const uint8_t*>(data), std::strlen(data)};
        return plugin->process(buf, &result);
    }
};

TEST_F(HttpPluginTest, ValidGetRequest) {
    flowforge_result_t r{};
    ASSERT_EQ(0, call("GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n", r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_INFO, r.severity);
    EXPECT_EQ(FLOWFORGE_STATUS_OK, r.status);
    EXPECT_STREQ("HTTP/1.1", r.protocol);
    EXPECT_STREQ("GET /index.html", r.detail);
}

TEST_F(HttpPluginTest, Valid200Response) {
    flowforge_result_t r{};
    ASSERT_EQ(0, call("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_INFO, r.severity);
    EXPECT_EQ(FLOWFORGE_STATUS_OK, r.status);
    EXPECT_STREQ("HTTP/1.1", r.protocol);
}

TEST_F(HttpPluginTest, Response404IsWarn) {
    flowforge_result_t r{};
    ASSERT_EQ(0, call("HTTP/1.1 404 Not Found\r\n\r\n", r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_WARN, r.severity);
    EXPECT_EQ(FLOWFORGE_STATUS_OK, r.status);
}

TEST_F(HttpPluginTest, Response500IsAlert) {
    flowforge_result_t r{};
    ASSERT_EQ(0, call("HTTP/1.1 500 Internal Server Error\r\n\r\n", r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_ALERT, r.severity);
    EXPECT_EQ(FLOWFORGE_STATUS_OK, r.status);
}

TEST_F(HttpPluginTest, NullDataReturnsNegative) {
    flowforge_result_t r{};
    flowforge_buf_t buf = {nullptr, 0};
    EXPECT_LT(plugin->process(buf, &r), 0);
}

TEST_F(HttpPluginTest, TooShortBufferReturnsNegative) {
    flowforge_result_t r{};
    const uint8_t data[] = {0x47}; /* "G" — only 1 byte */
    flowforge_buf_t buf = {data, sizeof(data)};
    EXPECT_LT(plugin->process(buf, &r), 0);
}

TEST_F(HttpPluginTest, GarbageIsNotApplicable) {
    flowforge_result_t r{};
    ASSERT_EQ(0, call("XXXX garbage data\r\nblah blah\r\n\r\n", r));
    EXPECT_EQ(FLOWFORGE_STATUS_NOT_APPLICABLE, r.status);
    EXPECT_EQ(FLOWFORGE_SEVERITY_UNKNOWN, r.severity);
}

TEST_F(HttpPluginTest, PostRequestIsInfo) {
    flowforge_result_t r{};
    ASSERT_EQ(0, call("POST /api/v1/resource HTTP/1.1\r\nContent-Length: 0\r\n\r\n", r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_INFO, r.severity);
    EXPECT_EQ(FLOWFORGE_STATUS_OK, r.status);
    EXPECT_STREQ("POST /api/v1/resource", r.detail);
}

TEST_F(HttpPluginTest, Response301IsInfo) {
    flowforge_result_t r{};
    ASSERT_EQ(0, call("HTTP/1.1 301 Moved Permanently\r\n\r\n", r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_INFO, r.severity);
}

TEST_F(HttpPluginTest, PluginNameIsCorrect) {
    EXPECT_STREQ("plugin_http", plugin->name());
}

TEST_F(HttpPluginTest, AbiVersionIsCorrect) {
    EXPECT_EQ(FLOWFORGE_PLUGIN_ABI_VERSION, plugin->abi_version());
}
