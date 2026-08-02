#pragma once

/**
 * Start the captive portal DNS server.
 * Responds to all DNS A-record queries with the given IP address.
 *
 * @param capture_ip IP address to return for all DNS queries (e.g., "192.168.4.1")
 */
void dns_server_start(const char *capture_ip);

/**
 * Stop the captive portal DNS server.
 */
void dns_server_stop();

/**
 * Check if the DNS server is currently running.
 */
bool dns_server_is_running();
