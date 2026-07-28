#include <kernel/http.h>
#include <kernel/e1000.h>
#include <kernel/network.h>

#include <stdbool.h>

#define HTTP_WAIT_ITERATIONS 20000000u
#define HTTP_RETRY_LIMIT 3u
#define HTTP_RESPONSE_LIMIT 1024u

static uint16_t next_dns_transaction = 0xD400u;
static uint16_t next_local_port = 49200u;
static uint32_t next_tcp_sequence = 0x57454200u;
static uint64_t completed_requests;
static uint64_t close_requests;

static void cpu_pause(void) { __asm__ volatile ("pause"); }

static bool prefix(const char *text, size_t length, const char *wanted,
                   size_t wanted_length) {
    if (length < wanted_length) return false;
    for (size_t index = 0u; index < wanted_length; ++index)
        if (text[index] != wanted[index]) return false;
    return true;
}

size_t http_get_url(const char *url, size_t url_length,
                    uint8_t *destination, size_t capacity) {
    if (url == NULL || destination == NULL || capacity == 0u ||
        url_length < 8u || url_length > 127u ||
        !prefix(url, url_length, "http://", 7u)) return 0u;
    size_t host_start = 7u;
    size_t host_length = 0u;
    while (host_start + host_length < url_length &&
           url[host_start + host_length] != '/') ++host_length;
    if (host_length == 0u || host_length > 63u) return 0u;
    char hostname[64];
    for (size_t index = 0u; index < host_length; ++index)
        hostname[index] = url[host_start + index];
    hostname[host_length] = '\0';
    const size_t path_start = host_start + host_length;
    const size_t path_length = path_start < url_length ?
        url_length - path_start : 1u;
    if (path_length > 95u) return 0u;

    struct network_route route;
    if (!network_route(&route) || !e1000_ready() || !e1000_link_up()) return 0u;
    uint8_t frame[1536];
    const uint16_t dns_transaction = ++next_dns_transaction;
    struct network_dns_answer resolved;
    bool have_dns = false;
    size_t length = 0u;
    for (size_t attempt = 0u; attempt < HTTP_RETRY_LIMIT && !have_dns;
         ++attempt) {
        length = network_build_dns_query(frame, sizeof(frame),
            route.gateway_mac, route.dns, dns_transaction, hostname);
        if (length == 0u || !e1000_transmit(frame, length)) return 0u;
        for (size_t wait = 0u; wait < HTTP_WAIT_ITERATIONS; ++wait) {
            (void)e1000_poll();
            if (network_dns_answer(dns_transaction, &resolved)) {
                have_dns = true;
                break;
            }
            cpu_pause();
        }
    }
    if (!have_dns) return 0u;

    const uint16_t local_port = ++next_local_port;
    const uint32_t initial_sequence = next_tcp_sequence += 0x101u;
    struct network_tcp_syn_ack syn_ack;
    bool connected = false;
    for (size_t attempt = 0u; attempt < HTTP_RETRY_LIMIT && !connected;
         ++attempt) {
        length = network_build_tcp_syn(frame, sizeof(frame), route.gateway_mac,
            resolved.address, local_port, 80u, initial_sequence);
        if (length == 0u || !e1000_transmit(frame, length)) return 0u;
        for (size_t wait = 0u; wait < HTTP_WAIT_ITERATIONS; ++wait) {
            (void)e1000_poll();
            if (network_tcp_syn_ack(resolved.address, local_port, 80u,
                                    initial_sequence, &syn_ack)) {
                connected = true;
                break;
            }
            cpu_pause();
        }
    }
    if (!connected) return 0u;
    length = network_build_tcp_ack(frame, sizeof(frame), route.gateway_mac,
        resolved.address, local_port, 80u, initial_sequence + 1u,
        syn_ack.remote_sequence + 1u);
    if (length == 0u || !e1000_transmit(frame, length)) return 0u;

    uint8_t request[256];
    size_t request_length = 0u;
#define APPEND_LITERAL(value) do { \
    static const char literal[] = value; \
    for (size_t i = 0u; i < sizeof(literal) - 1u; ++i) \
        request[request_length++] = (uint8_t)literal[i]; \
} while (0)
    APPEND_LITERAL("GET ");
    if (path_start < url_length) {
        for (size_t index = 0u; index < path_length; ++index)
            request[request_length++] = (uint8_t)url[path_start + index];
    } else {
        request[request_length++] = '/';
    }
    APPEND_LITERAL(" HTTP/1.0\r\nHost: ");
    for (size_t index = 0u; index < host_length; ++index)
        request[request_length++] = (uint8_t)hostname[index];
    APPEND_LITERAL("\r\nUser-Agent: DemonWeb/0.2\r\nConnection: close\r\n\r\n");
#undef APPEND_LITERAL
    const uint32_t local_sequence = initial_sequence + 1u;
    uint8_t response[HTTP_RESPONSE_LIMIT];
    uint32_t remote_next = 0u;
    size_t response_length = 0u;
    for (size_t attempt = 0u; attempt < HTTP_RETRY_LIMIT &&
         response_length == 0u; ++attempt) {
        length = network_build_tcp_data(frame, sizeof(frame), route.gateway_mac,
            resolved.address, local_port, 80u, local_sequence,
            syn_ack.remote_sequence + 1u, request, request_length);
        if (length == 0u || !e1000_transmit(frame, length)) return 0u;
        for (size_t wait = 0u; wait < HTTP_WAIT_ITERATIONS; ++wait) {
            (void)e1000_poll();
            response_length = network_tcp_receive(
                resolved.address, local_port, 80u, response, sizeof(response),
                &remote_next);
            if (response_length != 0u) break;
            cpu_pause();
        }
    }
    if (response_length < 12u || !prefix((const char *)response,
                                         response_length, "HTTP/", 5u))
        return 0u;
    length = network_build_tcp_ack(frame, sizeof(frame), route.gateway_mac,
        resolved.address, local_port, 80u,
        local_sequence + (uint32_t)request_length, remote_next);
    if (length != 0u) (void)e1000_transmit(frame, length);

    uint32_t close_remote_sequence = remote_next;
    (void)network_tcp_remote_fin(resolved.address, local_port, 80u,
                                 &close_remote_sequence);
    length = network_build_tcp_fin(frame, sizeof(frame), route.gateway_mac,
        resolved.address, local_port, 80u,
        local_sequence + (uint32_t)request_length, close_remote_sequence);
    if (length != 0u && e1000_transmit(frame, length)) ++close_requests;

    network_publish_http_response(response, response_length);
    const size_t amount = network_http_response(destination, capacity);
    if (amount != 0u) ++completed_requests;
    return amount;
}

uint64_t http_completed_requests(void) { return completed_requests; }
uint64_t http_close_requests(void) { return close_requests; }
