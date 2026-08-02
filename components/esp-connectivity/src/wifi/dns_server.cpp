#include "dns_server.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include <cstring>

#define TAG "dns_srv"

#define DNS_PORT 53
#define DNS_MAX_PACKET_SIZE 512
#define DNS_TASK_STACK_SIZE 4096
#define DNS_TASK_PRIORITY 3

// DNS header structure (12 bytes)
struct __attribute__((packed)) dns_header_t {
  uint16_t id;
  uint16_t flags;
  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;
};

static TaskHandle_t s_dns_task = nullptr;
static int s_sock = -1;
static bool s_running = false;
static uint32_t s_capture_ip = 0;

/**
 * Parse IP string to network-byte-order uint32_t.
 */
static uint32_t _parse_ip(const char *ip_str) {
  struct in_addr addr;
  inet_pton(AF_INET, ip_str, &addr);
  return addr.s_addr;
}

/**
 * Skip over a DNS name in a packet (handles both labels and compression pointers).
 * Returns pointer past the name, or nullptr on error.
 */
static const uint8_t *_skip_dns_name(const uint8_t *ptr, const uint8_t *end) {
  while (ptr < end) {
    uint8_t len = *ptr;
    if (len == 0) {
      return ptr + 1; // End of name
    }
    if ((len & 0xC0) == 0xC0) {
      return ptr + 2; // Compression pointer (2 bytes)
    }
    ptr += 1 + len; // Label: length byte + label chars
  }
  return nullptr;
}

/**
 * Build a DNS response that answers all A-record queries with our capture IP.
 * Returns the response length, or 0 on error.
 */
static int _build_response(const uint8_t *query, int query_len, uint8_t *response, int response_max) {
  if (query_len < (int)sizeof(dns_header_t)) {
    return 0;
  }

  const dns_header_t *qhdr = (const dns_header_t *)query;

  // Only respond to standard queries (opcode 0)
  uint16_t flags = ntohs(qhdr->flags);
  uint8_t opcode = (flags >> 11) & 0x0F;
  if (opcode != 0) {
    return 0;
  }

  uint16_t qdcount = ntohs(qhdr->qdcount);
  if (qdcount == 0) {
    return 0;
  }

  // Copy the question section
  const uint8_t *qptr = query + sizeof(dns_header_t);
  const uint8_t *qend = query + query_len;

  // Find end of first question
  const uint8_t *name_end = _skip_dns_name(qptr, qend);
  if (name_end == nullptr || name_end + 4 > qend) {
    return 0;
  }

  // Question: QNAME + QTYPE(2) + QCLASS(2)
  int question_len = (int)(name_end + 4 - qptr);
  // int name_len = (int)(name_end - qptr);

  // Check QTYPE = A (1) and QCLASS = IN (1)
  uint16_t qtype = ntohs(*(uint16_t *)name_end);
  uint16_t qclass = ntohs(*(uint16_t *)(name_end + 2));

  if (qtype != 1 || qclass != 1) {
    // Not an A record query for IN class — still respond to trigger captive portal
    // but only for A records. For other types, just drop.
    // Actually, respond anyway to ensure captive portal detection works.
  }

  // Build response
  int resp_len = sizeof(dns_header_t) + question_len + 16; // 16 = answer RR for A record
  if (resp_len > response_max) {
    return 0;
  }

  memset(response, 0, resp_len);

  // Response header
  dns_header_t *rhdr = (dns_header_t *)response;
  rhdr->id = qhdr->id;
  rhdr->flags = htons(0x8180); // QR=1, AA=1, RD=1, RA=1
  rhdr->qdcount = htons(1);
  rhdr->ancount = htons(1);
  rhdr->nscount = 0;
  rhdr->arcount = 0;

  // Copy question
  uint8_t *rptr = response + sizeof(dns_header_t);
  memcpy(rptr, qptr, question_len);
  rptr += question_len;

  // Answer: compression pointer to name in question
  *rptr++ = 0xC0;
  *rptr++ = 0x0C; // Pointer to offset 12 (start of question name)

  // TYPE = A (1)
  *rptr++ = 0x00;
  *rptr++ = 0x01;

  // CLASS = IN (1)
  *rptr++ = 0x00;
  *rptr++ = 0x01;

  // TTL = 60 seconds
  *rptr++ = 0x00;
  *rptr++ = 0x00;
  *rptr++ = 0x00;
  *rptr++ = 0x3C;

  // RDLENGTH = 4 (IPv4)
  *rptr++ = 0x00;
  *rptr++ = 0x04;

  // RDATA = capture IP (already in network byte order)
  memcpy(rptr, &s_capture_ip, 4);
  rptr += 4;

  return (int)(rptr - response);
}

/**
 * DNS server task — listens on UDP port 53 and responds to all queries.
 */
static void _dns_task(void *arg) {
  uint8_t rx_buf[DNS_MAX_PACKET_SIZE];
  uint8_t tx_buf[DNS_MAX_PACKET_SIZE];

  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(DNS_PORT);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s_sock < 0) {
    ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
    s_running = false;
    vTaskDelete(NULL);
    return;
  }

  int opt = 1;
  setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  if (bind(s_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind socket: errno %d", errno);
    close(s_sock);
    s_sock = -1;
    s_running = false;
    vTaskDelete(NULL);
    return;
  }

  // Set receive timeout so we can check the running flag periodically
  struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
  setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  ESP_LOGI(TAG, "DNS server listening on port %d", DNS_PORT);

  while (s_running) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int len = recvfrom(s_sock, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&client_addr, &addr_len);
    if (len < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue; // Timeout, check running flag
      }
      ESP_LOGD(TAG, "recvfrom error: errno %d", errno);
      continue;
    }

    int resp_len = _build_response(rx_buf, len, tx_buf, sizeof(tx_buf));
    if (resp_len > 0) {
      sendto(s_sock, tx_buf, resp_len, 0, (struct sockaddr *)&client_addr, addr_len);
    }
  }

  close(s_sock);
  s_sock = -1;
  ESP_LOGI(TAG, "DNS server stopped");
  vTaskDelete(NULL);
}

void dns_server_start(const char *capture_ip) {
  if (s_running) {
    return;
  }

  s_capture_ip = _parse_ip(capture_ip);
  s_running = true;

  xTaskCreate(_dns_task, "dns_srv", DNS_TASK_STACK_SIZE, NULL, DNS_TASK_PRIORITY, &s_dns_task);
}

void dns_server_stop() {
  if (!s_running) {
    return;
  }

  s_running = false;

  // Close socket to unblock recvfrom
  if (s_sock >= 0) {
    close(s_sock);
    s_sock = -1;
  }

  // Wait for task to exit
  if (s_dns_task != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(200));
    s_dns_task = nullptr;
  }
}

bool dns_server_is_running() {
  return s_running;
}
