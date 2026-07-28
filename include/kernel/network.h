#ifndef KERNEL_NETWORK_H
#define KERNEL_NETWORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum network_link_state {
    NETWORK_LINK_NO_DEVICE = 0,
    NETWORK_LINK_DOWN = 1,
    NETWORK_LINK_UP = 2,
};

struct network_snapshot {
    enum network_link_state link;
    uint8_t mac[6];
    uint32_t ipv4_address;
    uint64_t received_frames;
    uint64_t transmitted_frames;
    uint64_t dropped_frames;
    size_t arp_entries;
};

struct network_dhcp_offer {
    uint32_t address;
    uint32_t server;
    uint32_t gateway;
    uint32_t dns;
    uint32_t subnet_mask;
};

struct network_dns_answer {
    uint32_t address;
    uint32_t ttl_seconds;
};

struct network_tcp_syn_ack {
    uint32_t remote_sequence;
    uint16_t receive_window;
};

struct network_route {
    uint32_t gateway;
    uint32_t dns;
    uint8_t gateway_mac[6];
};

void network_init(void);
void network_set_device(const uint8_t mac[6]);
void network_set_link(bool up);
void network_set_ipv4(uint32_t address);
bool network_receive_ethernet(const uint8_t *frame, size_t length);
size_t network_build_arp_request(uint8_t *frame, size_t capacity,
                                 uint32_t target_address);
size_t network_build_dhcp_discover(uint8_t *frame, size_t capacity,
                                   uint32_t transaction_id);
size_t network_build_dhcp_request(uint8_t *frame, size_t capacity,
                                  uint32_t transaction_id,
                                  uint32_t requested_address,
                                  uint32_t server_address);
bool network_dhcp_offer(uint32_t transaction_id,
                        struct network_dhcp_offer *offer);
bool network_dhcp_ack(uint32_t transaction_id,
                      struct network_dhcp_offer *lease);
size_t network_build_dns_query(uint8_t *frame, size_t capacity,
                               const uint8_t destination_mac[6],
                               uint32_t dns_address, uint16_t transaction_id,
                               const char *hostname);
bool network_dns_answer(uint16_t transaction_id,
                        struct network_dns_answer *answer);
size_t network_build_tcp_syn(uint8_t *frame, size_t capacity,
                             const uint8_t destination_mac[6],
                             uint32_t remote_address, uint16_t local_port,
                             uint16_t remote_port, uint32_t initial_sequence);
bool network_tcp_syn_ack(uint32_t remote_address, uint16_t local_port,
                         uint16_t remote_port, uint32_t initial_sequence,
                         struct network_tcp_syn_ack *answer);
size_t network_build_tcp_ack(uint8_t *frame, size_t capacity,
                             const uint8_t destination_mac[6],
                             uint32_t remote_address, uint16_t local_port,
                             uint16_t remote_port, uint32_t local_sequence,
                             uint32_t remote_sequence);
size_t network_build_tcp_data(uint8_t *frame, size_t capacity,
                              const uint8_t destination_mac[6],
                              uint32_t remote_address, uint16_t local_port,
                              uint16_t remote_port, uint32_t local_sequence,
                              uint32_t remote_sequence, const uint8_t *data,
                              size_t data_length);
size_t network_build_tcp_fin(uint8_t *frame, size_t capacity,
                             const uint8_t destination_mac[6],
                             uint32_t remote_address, uint16_t local_port,
                             uint16_t remote_port, uint32_t local_sequence,
                             uint32_t remote_sequence);
bool network_tcp_remote_fin(uint32_t remote_address, uint16_t local_port,
                            uint16_t remote_port,
                            uint32_t *remote_next_sequence);
size_t network_tcp_receive(uint32_t remote_address, uint16_t local_port,
                           uint16_t remote_port, uint8_t *destination,
                           size_t capacity, uint32_t *remote_next_sequence);
void network_publish_http_response(const uint8_t *response, size_t length);
size_t network_http_response(uint8_t *destination, size_t capacity);
uint64_t network_http_reads(void);
void network_set_route(uint32_t gateway, uint32_t dns,
                       const uint8_t gateway_mac[6]);
bool network_route(struct network_route *route);
bool network_arp_lookup(uint32_t address, uint8_t mac[6]);
uint16_t network_ipv4_checksum(const uint8_t *header, size_t length);
void network_snapshot(struct network_snapshot *snapshot);
bool network_self_test(void);

#endif
