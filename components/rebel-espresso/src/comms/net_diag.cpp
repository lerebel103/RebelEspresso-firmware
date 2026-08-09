#include "net_diag.h"

#include <esp_log.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <cstdio>
#include <cstring>

#define TAG "net_diag"

// Socket fds live in [LWIP_SOCKET_OFFSET, +CONFIG_LWIP_MAX_SOCKETS); a valid
// socket answers SO_TYPE, a free/closed slot returns -1.
static bool _fd_open(int fd, int *type_out) {
  int type;
  socklen_t len = sizeof(type);
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) != 0) {
    return false;
  }
  if (type_out) {
    *type_out = type;
  }
  return true;
}

static void _fmt_addr(const struct sockaddr_storage *ss, char *out, size_t n) {
  if (ss->ss_family == AF_INET) {
    const struct sockaddr_in *s = reinterpret_cast<const struct sockaddr_in *>(ss);
    char ip[16];
    inet_ntoa_r(s->sin_addr, ip, sizeof(ip));
    snprintf(out, n, "%s:%u", ip, static_cast<unsigned>(ntohs(s->sin_port)));
  } else if (ss->ss_family == AF_INET6) {
    const struct sockaddr_in6 *s = reinterpret_cast<const struct sockaddr_in6 *>(ss);
    char ip[48];
    inet_ntop(AF_INET6, &s->sin6_addr, ip, sizeof(ip));
    snprintf(out, n, "[%s]:%u", ip, static_cast<unsigned>(ntohs(s->sin6_port)));
  } else {
    snprintf(out, n, "af=%d", ss->ss_family);
  }
}

int net_diag_count_open_sockets() {
  int used = 0;
  for (int fd = LWIP_SOCKET_OFFSET; fd < LWIP_SOCKET_OFFSET + CONFIG_LWIP_MAX_SOCKETS; fd++) {
    if (_fd_open(fd, nullptr)) {
      used++;
    }
  }
  return used;
}

void net_diag_dump_sockets(const char *reason) {
  const char *tag = reason ? reason : "";
  int used = 0;

  ESP_LOGW(TAG, "socket census [%s]  pool=%d offset=%d", tag, CONFIG_LWIP_MAX_SOCKETS, LWIP_SOCKET_OFFSET);
  for (int fd = LWIP_SOCKET_OFFSET; fd < LWIP_SOCKET_OFFSET + CONFIG_LWIP_MAX_SOCKETS; fd++) {
    int type;
    if (!_fd_open(fd, &type)) {
      continue;
    }
    used++;

    const char *kind = (type == SOCK_STREAM) ? "TCP" : (type == SOCK_DGRAM) ? "UDP" : "?";

    struct sockaddr_storage local = {};
    struct sockaddr_storage peer = {};
    socklen_t llen = sizeof(local);
    socklen_t plen = sizeof(peer);
    char lbuf[56] = "?";
    char pbuf[56];

    if (getsockname(fd, reinterpret_cast<struct sockaddr *>(&local), &llen) == 0) {
      _fmt_addr(&local, lbuf, sizeof(lbuf));
    }
    if (getpeername(fd, reinterpret_cast<struct sockaddr *>(&peer), &plen) == 0) {
      _fmt_addr(&peer, pbuf, sizeof(pbuf));
    } else {
      // TCP listener or unconnected UDP socket has no peer.
      snprintf(pbuf, sizeof(pbuf), "(listen/unconnected)");
    }

    ESP_LOGW(TAG, "  fd=%d %s local=%s peer=%s", fd, kind, lbuf, pbuf);
  }
  ESP_LOGW(TAG, "socket census [%s]  %d/%d used", tag, used, CONFIG_LWIP_MAX_SOCKETS);
}
