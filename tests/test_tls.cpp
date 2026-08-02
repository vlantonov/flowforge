#include <gtest/gtest.h>
#include <flowforge/plugin_v1.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

extern "C" const flowforge_plugin_t* flowforge_plugin_entry();

/* Build a minimal TLS ClientHello record for testing.
 *
 * Layout:
 *   [0]      content_type = 0x16
 *   [1-2]    record version  = 0x03 0x01
 *   [3-4]    record length   (filled in)
 *   [5]      handshake_type  = 0x01
 *   [6-8]    handshake len   (filled in, 24-bit)
 *   [9-10]   legacy_version  (supplied)
 *   [11-42]  random (zeros)
 *   [43]     session_id_length = 0
 *   [44-45]  cipher_suites_length
 *   [46..]   cipher suites
 *   [..]     compression_methods_length = 1, null compression
 */
static std::vector<uint8_t> build_client_hello(
    uint16_t legacy_version,
    const std::vector<uint16_t>& ciphers)
{
    std::vector<uint8_t> body;
    /* legacy_version */
    body.push_back(static_cast<uint8_t>(legacy_version >> 8));
    body.push_back(static_cast<uint8_t>(legacy_version & 0xFF));
    /* random (32 zeros) */
    body.insert(body.end(), 32, 0x00);
    /* session_id_length = 0 */
    body.push_back(0x00);
    /* cipher_suites_length */
    uint16_t cs_len = static_cast<uint16_t>(ciphers.size() * 2);
    body.push_back(static_cast<uint8_t>(cs_len >> 8));
    body.push_back(static_cast<uint8_t>(cs_len & 0xFF));
    for (uint16_t cs : ciphers) {
        body.push_back(static_cast<uint8_t>(cs >> 8));
        body.push_back(static_cast<uint8_t>(cs & 0xFF));
    }
    /* compression_methods */
    body.push_back(0x01); /* length = 1 */
    body.push_back(0x00); /* null */

    /* Handshake header: type + 24-bit length */
    uint32_t hs_len = static_cast<uint32_t>(body.size());
    std::vector<uint8_t> hs;
    hs.push_back(0x01); /* ClientHello */
    hs.push_back(static_cast<uint8_t>((hs_len >> 16) & 0xFF));
    hs.push_back(static_cast<uint8_t>((hs_len >>  8) & 0xFF));
    hs.push_back(static_cast<uint8_t>( hs_len        & 0xFF));
    hs.insert(hs.end(), body.begin(), body.end());

    /* TLS record header */
    uint16_t rec_len = static_cast<uint16_t>(hs.size());
    std::vector<uint8_t> rec;
    rec.push_back(0x16);                              /* content_type = Handshake */
    rec.push_back(0x03); rec.push_back(0x01);         /* record version TLS 1.0 */
    rec.push_back(static_cast<uint8_t>(rec_len >> 8));
    rec.push_back(static_cast<uint8_t>(rec_len & 0xFF));
    rec.insert(rec.end(), hs.begin(), hs.end());

    return rec;
}

class TlsPluginTest : public ::testing::Test {
protected:
    const flowforge_plugin_t* plugin = flowforge_plugin_entry();
};

TEST_F(TlsPluginTest, MinimalClientHelloProducesJA3) {
    auto pkt = build_client_hello(0x0303, {0x002Fu, 0x0035u}); /* TLS 1.2, RSA suites */
    flowforge_result_t r{};
    flowforge_buf_t buf = {pkt.data(), pkt.size()};
    ASSERT_EQ(0, plugin->process(buf, &r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_INFO, r.severity);
    EXPECT_EQ(FLOWFORGE_STATUS_OK,     r.status);
    EXPECT_STREQ("TLS", r.protocol);

    /* detail must be "JA3=<32 lowercase hex chars>" */
    ASSERT_GT(std::strlen(r.detail), 4u);
    EXPECT_EQ(0, std::strncmp(r.detail, "JA3=", 4));
    const char* md5_part = r.detail + 4;
    EXPECT_EQ(32u, std::strlen(md5_part));
    for (int i = 0; i < 32; ++i) {
        char c = md5_part[i];
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Non-hex char at position " << i;
    }
}

TEST_F(TlsPluginTest, GreaseFilteredCipherMatchesWithoutGrease) {
    /* Two ClientHellos: one with a GREASE cipher prepended, one without.
     * After GREASE filtering they should produce the same JA3 hash. */
    auto pkt_clean  = build_client_hello(0x0303, {0x002Fu});
    auto pkt_grease = build_client_hello(0x0303, {0x0A0Au, 0x002Fu}); /* 0x0a0a is GREASE */

    flowforge_result_t r_clean{}, r_grease{};
    flowforge_buf_t buf_clean  = {pkt_clean.data(),  pkt_clean.size()};
    flowforge_buf_t buf_grease = {pkt_grease.data(), pkt_grease.size()};

    ASSERT_EQ(0, plugin->process(buf_clean,  &r_clean));
    ASSERT_EQ(0, plugin->process(buf_grease, &r_grease));

    EXPECT_STREQ(r_clean.detail, r_grease.detail)
        << "GREASE cipher 0x0a0a was not filtered; JA3 hashes differ";
}

TEST_F(TlsPluginTest, NonClientHelloHandshakeIsInfo) {
    /* Construct a TLS record with content_type=0x16 but handshake_type=0x02 (ServerHello). */
    static const uint8_t pkt[] = {
        0x16, 0x03, 0x03,    /* Handshake, TLS 1.2 */
        0x00, 0x04,          /* record length = 4 */
        0x02,                /* handshake_type = ServerHello */
        0x00, 0x00, 0x00     /* handshake length = 0 */
    };
    flowforge_result_t r{};
    flowforge_buf_t buf = {pkt, sizeof(pkt)};
    ASSERT_EQ(0, plugin->process(buf, &r));
    EXPECT_EQ(FLOWFORGE_SEVERITY_INFO,          r.severity);
    EXPECT_EQ(FLOWFORGE_STATUS_NOT_APPLICABLE,  r.status);
    EXPECT_STREQ("TLS record type=2", r.detail);
}

TEST_F(TlsPluginTest, TruncatedRecordReturnsNegative) {
    /* Record header says length=100 but buffer is only 5+4=9 bytes. */
    static const uint8_t pkt[] = {
        0x16, 0x03, 0x01,
        0x00, 0x64,          /* length = 100 — but no payload follows */
    };
    flowforge_result_t r{};
    flowforge_buf_t buf = {pkt, sizeof(pkt)};
    EXPECT_LT(plugin->process(buf, &r), 0);
}

TEST_F(TlsPluginTest, NullDataReturnsNegative) {
    flowforge_result_t r{};
    flowforge_buf_t buf = {nullptr, 0};
    EXPECT_LT(plugin->process(buf, &r), 0);
}

TEST_F(TlsPluginTest, NonTlsContentTypeIsNotApplicable) {
    /* Application Data record (content_type = 0x17). */
    static const uint8_t pkt[] = {
        0x17, 0x03, 0x03, 0x00, 0x05,
        0xDE, 0xAD, 0xBE, 0xEF, 0x00
    };
    flowforge_result_t r{};
    flowforge_buf_t buf = {pkt, sizeof(pkt)};
    ASSERT_EQ(0, plugin->process(buf, &r));
    EXPECT_EQ(FLOWFORGE_STATUS_NOT_APPLICABLE, r.status);
    EXPECT_EQ(FLOWFORGE_SEVERITY_UNKNOWN,      r.severity);
}

TEST_F(TlsPluginTest, PluginNameIsCorrect) {
    EXPECT_STREQ("plugin_tls_ja3", plugin->name());
}

TEST_F(TlsPluginTest, AbiVersionIsCorrect) {
    EXPECT_EQ(FLOWFORGE_PLUGIN_ABI_VERSION, plugin->abi_version());
}
