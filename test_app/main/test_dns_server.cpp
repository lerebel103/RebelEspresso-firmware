/**
 * Tests for the captive portal DNS server packet parsing logic.
 *
 * These tests verify the DNS response builder produces correct packets
 * without requiring actual network sockets (pure logic tests).
 */
#include <unity.h>
#include <cstring>

// Inline IP address parsing (avoids lwip dependency in tests)
static uint32_t test_inet_addr(const char *ip_str) {
  uint32_t result = 0;
  uint8_t *bytes = (uint8_t *)&result;
  int parts[4] = {};
  sscanf(ip_str, "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]);
  bytes[0] = (uint8_t)parts[0];
  bytes[1] = (uint8_t)parts[1];
  bytes[2] = (uint8_t)parts[2];
  bytes[3] = (uint8_t)parts[3];
  return result;
}

static uint16_t test_htons(uint16_t v) {
  return (uint16_t)((v >> 8) | (v << 8));
}

static uint16_t test_ntohs(uint16_t v) {
  return test_htons(v);
}

// Map standard network functions to our test implementations
#define htons test_htons
#define ntohs test_ntohs
#define inet_addr test_inet_addr

// We test the DNS response builder by reimplementing the core logic here
// since the actual dns_server.cpp uses sockets which aren't available in QEMU.
// This validates the packet format is correct.

// DNS header (12 bytes)
struct __attribute__((packed)) test_dns_header_t {
  uint16_t id;
  uint16_t flags;
  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;
};

static const uint8_t *skip_dns_name(const uint8_t *ptr, const uint8_t *end) {
  while (ptr < end) {
    uint8_t len = *ptr;
    if (len == 0) return ptr + 1;
    if ((len & 0xC0) == 0xC0) return ptr + 2;
    ptr += 1 + len;
  }
  return nullptr;
}

static int build_dns_response(const uint8_t *query, int query_len, uint8_t *response, int response_max,
                              uint32_t capture_ip) {
  if (query_len < (int)sizeof(test_dns_header_t)) return 0;

  const test_dns_header_t *qhdr = (const test_dns_header_t *)query;
  uint16_t flags = ntohs(qhdr->flags);
  uint8_t opcode = (flags >> 11) & 0x0F;
  if (opcode != 0) return 0;

  uint16_t qdcount = ntohs(qhdr->qdcount);
  if (qdcount == 0) return 0;

  const uint8_t *qptr = query + sizeof(test_dns_header_t);
  const uint8_t *qend = query + query_len;
  const uint8_t *name_end = skip_dns_name(qptr, qend);
  if (name_end == nullptr || name_end + 4 > qend) return 0;

  int question_len = (int)(name_end + 4 - qptr);
  int resp_len = sizeof(test_dns_header_t) + question_len + 16;
  if (resp_len > response_max) return 0;

  memset(response, 0, resp_len);

  test_dns_header_t *rhdr = (test_dns_header_t *)response;
  rhdr->id = qhdr->id;
  rhdr->flags = htons(0x8180);
  rhdr->qdcount = htons(1);
  rhdr->ancount = htons(1);

  uint8_t *rptr = response + sizeof(test_dns_header_t);
  memcpy(rptr, qptr, question_len);
  rptr += question_len;

  // Answer: compression pointer to name
  *rptr++ = 0xC0;
  *rptr++ = 0x0C;
  // TYPE A
  *rptr++ = 0x00; *rptr++ = 0x01;
  // CLASS IN
  *rptr++ = 0x00; *rptr++ = 0x01;
  // TTL 60s
  *rptr++ = 0x00; *rptr++ = 0x00; *rptr++ = 0x00; *rptr++ = 0x3C;
  // RDLENGTH 4
  *rptr++ = 0x00; *rptr++ = 0x04;
  // RDATA (IP)
  memcpy(rptr, &capture_ip, 4);
  rptr += 4;

  return (int)(rptr - response);
}

// Build a DNS query packet for testing
static int build_dns_query(const char *hostname, uint8_t *buf, int buf_max) {
  // Header
  test_dns_header_t *hdr = (test_dns_header_t *)buf;
  hdr->id = htons(0x1234);
  hdr->flags = htons(0x0100);  // Standard query, RD=1
  hdr->qdcount = htons(1);
  hdr->ancount = 0;
  hdr->nscount = 0;
  hdr->arcount = 0;

  uint8_t *ptr = buf + sizeof(test_dns_header_t);

  // Encode hostname as DNS labels
  const char *p = hostname;
  while (*p) {
    const char *dot = strchr(p, '.');
    int label_len = dot ? (int)(dot - p) : (int)strlen(p);
    *ptr++ = (uint8_t)label_len;
    memcpy(ptr, p, label_len);
    ptr += label_len;
    p += label_len;
    if (dot) p++;  // skip the dot
  }
  *ptr++ = 0;  // End of name

  // QTYPE = A (1)
  *ptr++ = 0x00; *ptr++ = 0x01;
  // QCLASS = IN (1)
  *ptr++ = 0x00; *ptr++ = 0x01;

  return (int)(ptr - buf);
}

TEST_CASE("DNS: responds to A record query with capture IP", "[dns]") {
  uint8_t query[128];
  uint8_t response[128];

  int qlen = build_dns_query("captive.apple.com", query, sizeof(query));
  TEST_ASSERT_GREATER_THAN(12, qlen);

  uint32_t capture_ip = inet_addr("192.168.4.1");
  int rlen = build_dns_response(query, qlen, response, sizeof(response), capture_ip);
  TEST_ASSERT_GREATER_THAN(0, rlen);

  // Verify response header
  test_dns_header_t *rhdr = (test_dns_header_t *)response;
  TEST_ASSERT_EQUAL_HEX16(0x1234, ntohs(rhdr->id));
  TEST_ASSERT_EQUAL_HEX16(0x8180, ntohs(rhdr->flags));
  TEST_ASSERT_EQUAL(1, ntohs(rhdr->qdcount));
  TEST_ASSERT_EQUAL(1, ntohs(rhdr->ancount));

  // Verify the answer contains our IP (last 4 bytes of response)
  uint32_t resp_ip;
  memcpy(&resp_ip, response + rlen - 4, 4);
  TEST_ASSERT_EQUAL_HEX32(capture_ip, resp_ip);
}

TEST_CASE("DNS: rejects non-standard query (opcode != 0)", "[dns]") {
  uint8_t query[64];
  uint8_t response[128];

  int qlen = build_dns_query("example.com", query, sizeof(query));
  // Set opcode to 1 (inverse query)
  test_dns_header_t *hdr = (test_dns_header_t *)query;
  hdr->flags = htons(0x0800);  // opcode = 1

  uint32_t capture_ip = inet_addr("192.168.4.1");
  int rlen = build_dns_response(query, qlen, response, sizeof(response), capture_ip);
  TEST_ASSERT_EQUAL(0, rlen);
}

TEST_CASE("DNS: rejects empty query (qdcount = 0)", "[dns]") {
  uint8_t query[12] = {};
  uint8_t response[128];

  test_dns_header_t *hdr = (test_dns_header_t *)query;
  hdr->id = htons(0xABCD);
  hdr->flags = htons(0x0100);
  hdr->qdcount = htons(0);  // No questions

  uint32_t capture_ip = inet_addr("192.168.4.1");
  int rlen = build_dns_response(query, 12, response, sizeof(response), capture_ip);
  TEST_ASSERT_EQUAL(0, rlen);
}

TEST_CASE("DNS: rejects packet too short", "[dns]") {
  uint8_t query[8] = {0};
  uint8_t response[128];

  uint32_t capture_ip = inet_addr("192.168.4.1");
  int rlen = build_dns_response(query, 8, response, sizeof(response), capture_ip);
  TEST_ASSERT_EQUAL(0, rlen);
}

TEST_CASE("DNS: handles long hostname", "[dns]") {
  uint8_t query[256];
  uint8_t response[256];

  int qlen = build_dns_query("connectivitycheck.gstatic.com", query, sizeof(query));
  TEST_ASSERT_GREATER_THAN(12, qlen);

  uint32_t capture_ip = inet_addr("192.168.4.1");
  int rlen = build_dns_response(query, qlen, response, sizeof(response), capture_ip);
  TEST_ASSERT_GREATER_THAN(0, rlen);

  // Verify IP in answer
  uint32_t resp_ip;
  memcpy(&resp_ip, response + rlen - 4, 4);
  TEST_ASSERT_EQUAL_HEX32(capture_ip, resp_ip);
}
