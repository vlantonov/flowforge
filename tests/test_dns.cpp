#include <gtest/gtest.h>
#include <flowforge/plugin_v1.h>
#include <cstdint>
#include <cstring>

extern "C" const flowforge_plugin_t* flowforge_plugin_entry();

class DnsPluginTest : public ::testing::Test {
protected:
    const flowforge_plugin_t* plugin = flowforge_plugin_entry();
};

/* Helpers to build DNS packets. */
[[maybe_unused]] static void be16_write(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFFu);
}

TEST_F(DnsPluginTest, ValidAQueryIsInfo) {
    /* Minimal DNS query: www.example.com A IN */
    static const uint8_t pkt[] = {
        0x12, 0x34,             /* ID */
        0x01, 0x00,             /* flags: QR=0, RD=1 */
        0x00, 0x01,             /* QDCOUNT=1 */
        0x00, 0x00,             /* ANCOUNT=0 */
        0x00, 0x00,             /* NSCOUNT=0 */
        0x00, 0x00,             /* ARCOUNT=0 */
        /* QNAME: www.example.com */
        0x03, 'w','w','w',
        0x07, 'e','x','a','m','p','l','e',
        0x03, 'c','o','m',
        0x00,
        0x00, 0x01,             /* QTYPE = A */
        0x00, 0x01              /* QCLASS = IN */
    };

    flowforge_result_t r{};
    flowforge_buf_t buf = {pkt, sizeof(pkt)};
    ASSERT_EQ(0, plugin->process(buf, &r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_INFO,  r.severity);
    EXPECT_EQ(FLOWFORGE_STATUS_OK,      r.status);
    EXPECT_STREQ("DNS", r.protocol);
}

TEST_F(DnsPluginTest, AnyQueryIsWarn) {
    /* DNS query with QTYPE=255 (ANY) */
    static const uint8_t pkt[] = {
        0xAB, 0xCD,
        0x01, 0x00,
        0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /* QNAME: . (root) */
        0x00,
        0x00, 0xFF,             /* QTYPE = ANY */
        0x00, 0x01              /* QCLASS = IN */
    };

    flowforge_result_t r{};
    flowforge_buf_t buf = {pkt, sizeof(pkt)};
    ASSERT_EQ(0, plugin->process(buf, &r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_WARN, r.severity);
    EXPECT_EQ(FLOWFORGE_STATUS_OK,     r.status);
    EXPECT_STREQ("DNS ANY query (potential amplification)", r.detail);
}

TEST_F(DnsPluginTest, ResponseRcode3IsWarn) {
    /* DNS response with RCODE=3 (NXDOMAIN) */
    static const uint8_t pkt[] = {
        0x12, 0x34,
        0x81, 0x83,             /* flags: QR=1, RD=1, RA=1, RCODE=3 */
        0x00, 0x01,             /* QDCOUNT=1 */
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x03, 'f','o','o',
        0x00,
        0x00, 0x01,
        0x00, 0x01
    };

    flowforge_result_t r{};
    flowforge_buf_t buf = {pkt, sizeof(pkt)};
    ASSERT_EQ(0, plugin->process(buf, &r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_WARN, r.severity);
    EXPECT_EQ(FLOWFORGE_STATUS_OK,     r.status);

    char expected[32];
    std::snprintf(expected, sizeof(expected), "DNS RCODE=3");
    EXPECT_STREQ(expected, r.detail);
}

TEST_F(DnsPluginTest, ShortBufferReturnsNegative) {
    static const uint8_t pkt[11] = {}; /* 11 bytes — too short */
    flowforge_result_t r{};
    flowforge_buf_t buf = {pkt, sizeof(pkt)};
    EXPECT_LT(plugin->process(buf, &r), 0);
}

TEST_F(DnsPluginTest, NullDataReturnsNegative) {
    flowforge_result_t r{};
    flowforge_buf_t buf = {nullptr, 0};
    EXPECT_LT(plugin->process(buf, &r), 0);
}

TEST_F(DnsPluginTest, ValidResponseRcode0IsInfo) {
    static const uint8_t pkt[] = {
        0x12, 0x34,
        0x81, 0x80,             /* QR=1, RD=1, RA=1, RCODE=0 */
        0x00, 0x01,
        0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x03, 'w','w','w',
        0x07, 'e','x','a','m','p','l','e',
        0x03, 'c','o','m',
        0x00,
        0x00, 0x01, 0x00, 0x01
    };

    flowforge_result_t r{};
    flowforge_buf_t buf = {pkt, sizeof(pkt)};
    ASSERT_EQ(0, plugin->process(buf, &r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_INFO, r.severity);
}

/* QA: FR-16 — pointer loop must be caught within ≤128 hops. */
TEST_F(DnsPluginTest, PointerLoopIsParseError) {
    /* Craft a QNAME pointer that points back to itself (byte offset 12). */
    static const uint8_t pkt[] = {
        0xDE, 0xAD,             /* ID */
        0x01, 0x00,             /* flags: QR=0, RD=1 */
        0x00, 0x01,             /* QDCOUNT=1 */
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        /* QNAME: pointer at offset 12 pointing back to offset 12 (self-loop) */
        0xC0, 0x0C,
        0x00, 0x01,             /* QTYPE = A */
        0x00, 0x01              /* QCLASS = IN */
    };

    flowforge_result_t r{};
    flowforge_buf_t buf = {pkt, sizeof(pkt)};
    int rc = plugin->process(buf, &r);
    /* Must not crash; must signal an error (negative rc OR PARSE_ERROR status). */
    EXPECT_TRUE(rc < 0 || r.status == FLOWFORGE_STATUS_PARSE_ERROR)
        << "Expected error for pointer-loop packet; got rc=" << rc
        << " status=" << r.status;
}

/* QA: design §5.2.4 — RCODE=2 (SERVFAIL) must map to ALERT, not WARN. */
TEST_F(DnsPluginTest, ServfailResponseIsAlert) {
    static const uint8_t pkt[] = {
        0x12, 0x34,
        0x81, 0x82,             /* QR=1, RD=1, RA=1, RCODE=2 (SERVFAIL) */
        0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 'f','o','o',
        0x00,
        0x00, 0x01, 0x00, 0x01
    };

    flowforge_result_t r{};
    flowforge_buf_t buf = {pkt, sizeof(pkt)};
    ASSERT_EQ(0, plugin->process(buf, &r));
    EXPECT_EQ(FLOWFORGE_STATUS_OK, r.status);
    /* Per design §5.2.4: RCODE=2 must be ALERT. */
    EXPECT_EQ(FLOWFORGE_SEVERITY_ALERT, r.severity)
        << "SERVFAIL (RCODE=2) must map to ALERT per design spec §5.2.4";
}

TEST_F(DnsPluginTest, PluginNameIsCorrect) {
    EXPECT_STREQ("plugin_dns", plugin->name());
}

TEST_F(DnsPluginTest, AbiVersionIsCorrect) {
    EXPECT_EQ(FLOWFORGE_PLUGIN_ABI_VERSION, plugin->abi_version());
}
