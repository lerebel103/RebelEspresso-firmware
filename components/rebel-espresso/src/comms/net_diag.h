#pragma once

// Diagnostics for the shared LWIP socket pool (CONFIG_LWIP_MAX_SOCKETS).
// Used to track fd exhaustion that surfaces as `accept()` failing with
// errno 23 (ENFILE) in the esp_http_server "httpd" logs.

// Number of currently-open LWIP socket fds (0..CONFIG_LWIP_MAX_SOCKETS).
int net_diag_count_open_sockets();

// Log a per-socket census: type + local/peer address:port for every open
// socket, plus a used/total summary. `reason` tags the log line for context.
void net_diag_dump_sockets(const char *reason);
