#include <kernel/network.h>

#define ETHERNET_HEADER_BYTES 14u
#define ETHERNET_MIN_FRAME_BYTES 60u
#define ETHER_TYPE_ARP 0x0806u
#define ETHER_TYPE_IPV4 0x0800u
#define ARP_PACKET_BYTES 28u
#define ARP_CACHE_LIMIT 4u
#define IP_PROTOCOL_UDP 17u
#define DHCP_CLIENT_PORT 68u
#define DHCP_SERVER_PORT 67u
#define DHCP_FIXED_BYTES 240u
#define DNS_SERVER_PORT 53u
#define DNS_CLIENT_PORT 49152u
#define IP_PROTOCOL_TCP 6u
#define TCP_FLAG_SYN 0x02u
#define TCP_FLAG_FIN 0x01u
#define TCP_FLAG_PSH 0x08u
#define TCP_FLAG_ACK 0x10u
#define TCP_RECEIVE_LIMIT 4096u
#define HTTP_DOCUMENT_LIMIT 1024u

struct arp_entry {
    uint32_t address;
    uint8_t mac[6];
    bool valid;
};

static struct network_snapshot state;
static struct arp_entry arp_cache[ARP_CACHE_LIMIT];
static size_t arp_replace;
static struct network_dhcp_offer pending_offer;
static uint32_t pending_offer_transaction;
static bool pending_offer_valid;
static struct network_dhcp_offer pending_ack;
static uint32_t pending_ack_transaction;
static bool pending_ack_valid;
static struct network_dns_answer pending_dns_answer;
static uint16_t pending_dns_transaction;
static bool pending_dns_valid;
static struct network_tcp_syn_ack pending_tcp_syn_ack;
static uint32_t pending_tcp_address;
static uint32_t pending_tcp_acknowledgment;
static uint16_t pending_tcp_local_port;
static uint16_t pending_tcp_remote_port;
static bool pending_tcp_valid;
static uint8_t pending_tcp_data[TCP_RECEIVE_LIMIT];
static size_t pending_tcp_data_length;
static uint32_t pending_tcp_data_next_sequence;
static uint32_t pending_tcp_data_address;
static uint16_t pending_tcp_data_local_port;
static uint16_t pending_tcp_data_remote_port;
static bool pending_tcp_fin;
static uint32_t pending_tcp_fin_address;
static uint32_t pending_tcp_fin_next_sequence;
static uint16_t pending_tcp_fin_local_port;
static uint16_t pending_tcp_fin_remote_port;
static uint8_t http_document[HTTP_DOCUMENT_LIMIT];
static size_t http_document_length;
static uint64_t http_document_reads;
static struct network_route active_route;
static bool active_route_valid;

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
        ((uint32_t)bytes[2] << 8u) | bytes[3];
}

static void write_be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8u);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24u);
    bytes[1] = (uint8_t)(value >> 16u);
    bytes[2] = (uint8_t)(value >> 8u);
    bytes[3] = (uint8_t)value;
}

static void copy_mac(uint8_t destination[6], const uint8_t source[6]) {
    for (size_t index = 0u; index < 6u; ++index) destination[index] = source[index];
}

static bool equal_mac(const uint8_t left[6], const uint8_t right[6]) {
    for (size_t index = 0u; index < 6u; ++index)
        if (left[index] != right[index]) return false;
    return true;
}

static void learn_arp(uint32_t address, const uint8_t mac[6]) {
    for (size_t index = 0u; index < ARP_CACHE_LIMIT; ++index) {
        if (arp_cache[index].valid && arp_cache[index].address == address) {
            copy_mac(arp_cache[index].mac, mac);
            return;
        }
    }
    struct arp_entry *entry = &arp_cache[arp_replace];
    entry->address = address;
    copy_mac(entry->mac, mac);
    if (!entry->valid) ++state.arp_entries;
    entry->valid = true;
    arp_replace = (arp_replace + 1u) % ARP_CACHE_LIMIT;
}

void network_init(void) {
    state = (struct network_snapshot){ .link = NETWORK_LINK_NO_DEVICE };
    for (size_t index = 0u; index < ARP_CACHE_LIMIT; ++index)
        arp_cache[index] = (struct arp_entry){0};
    arp_replace = 0u;
    pending_offer = (struct network_dhcp_offer){0};
    pending_offer_transaction = 0u;
    pending_offer_valid = false;
    pending_ack = (struct network_dhcp_offer){0};
    pending_ack_transaction = 0u;
    pending_ack_valid = false;
    pending_dns_answer = (struct network_dns_answer){0};
    pending_dns_transaction = 0u;
    pending_dns_valid = false;
    pending_tcp_syn_ack = (struct network_tcp_syn_ack){0};
    pending_tcp_address = 0u;
    pending_tcp_acknowledgment = 0u;
    pending_tcp_local_port = 0u;
    pending_tcp_remote_port = 0u;
    pending_tcp_valid = false;
    pending_tcp_data_length = 0u;
    pending_tcp_data_next_sequence = 0u;
    pending_tcp_data_address = 0u;
    pending_tcp_data_local_port = 0u;
    pending_tcp_data_remote_port = 0u;
    pending_tcp_fin = false;
    http_document_length = 0u;
    http_document_reads = 0u;
    active_route = (struct network_route){0};
    active_route_valid = false;
}

void network_set_device(const uint8_t mac[6]) {
    if (mac == NULL) return;
    copy_mac(state.mac, mac);
    state.link = NETWORK_LINK_DOWN;
}

void network_set_link(bool up) {
    if (state.link == NETWORK_LINK_NO_DEVICE) return;
    state.link = up ? NETWORK_LINK_UP : NETWORK_LINK_DOWN;
}

void network_set_ipv4(uint32_t address) { state.ipv4_address = address; }

uint16_t network_ipv4_checksum(const uint8_t *header, size_t length) {
    if (header == NULL || length < 20u || length > 60u || (length & 1u) != 0u)
        return 0xFFFFu;
    uint32_t sum = 0u;
    for (size_t offset = 0u; offset < length; offset += 2u) {
        sum += read_be16(&header[offset]);
        while (sum > 0xFFFFu) sum = (sum & 0xFFFFu) + (sum >> 16u);
    }
    return (uint16_t)~sum;
}

static bool receive_arp(const uint8_t *packet, size_t length) {
    if (length < ARP_PACKET_BYTES || read_be16(packet) != 1u ||
        read_be16(&packet[2]) != ETHER_TYPE_IPV4 ||
        packet[4] != 6u || packet[5] != 4u) return false;
    const uint16_t operation = read_be16(&packet[6]);
    if (operation != 1u && operation != 2u) return false;
    learn_arp(read_be32(&packet[14]), &packet[8]);
    return true;
}

static bool receive_dhcp(const uint8_t *packet, size_t length) {
    if (length < DHCP_FIXED_BYTES || packet[0] != 2u || packet[1] != 1u ||
        packet[2] != 6u || read_be32(&packet[236]) != 0x63825363u ||
        !equal_mac(&packet[28], state.mac)) return false;
    struct network_dhcp_offer offer = {.address = read_be32(&packet[16])};
    const uint32_t transaction = read_be32(&packet[4]);
    uint8_t message_type = 0u;
    size_t offset = DHCP_FIXED_BYTES;
    while (offset < length) {
        const uint8_t option = packet[offset++];
        if (option == 0u) continue;
        if (option == 255u) break;
        if (offset >= length) return false;
        const size_t option_length = packet[offset++];
        if (option_length > length - offset) return false;
        if (option == 53u && option_length == 1u) message_type = packet[offset];
        else if (option == 54u && option_length == 4u)
            offer.server = read_be32(&packet[offset]);
        else if (option == 3u && option_length >= 4u)
            offer.gateway = read_be32(&packet[offset]);
        else if (option == 6u && option_length >= 4u)
            offer.dns = read_be32(&packet[offset]);
        else if (option == 1u && option_length == 4u)
            offer.subnet_mask = read_be32(&packet[offset]);
        offset += option_length;
    }
    if ((message_type != 2u && message_type != 5u) ||
        offer.address == 0u || offer.server == 0u)
        return false;
    if (message_type == 2u) {
        pending_offer = offer;
        pending_offer_transaction = transaction;
        pending_offer_valid = true;
    } else {
        pending_ack = offer;
        pending_ack_transaction = transaction;
        pending_ack_valid = true;
    }
    return true;
}

static size_t dns_skip_name(const uint8_t *packet, size_t length, size_t offset) {
    while (offset < length) {
        const uint8_t label = packet[offset++];
        if (label == 0u) return offset;
        if ((label & 0xC0u) == 0xC0u)
            return offset < length ? offset + 1u : 0u;
        if (label > 63u || label > length - offset) return 0u;
        offset += label;
    }
    return 0u;
}

static bool receive_dns(const uint8_t *packet, size_t length) {
    if (length < 12u || (read_be16(&packet[2]) & 0x800Fu) != 0x8000u)
        return false;
    const uint16_t transaction = read_be16(packet);
    const uint16_t questions = read_be16(&packet[4]);
    const uint16_t answers = read_be16(&packet[6]);
    size_t offset = 12u;
    for (uint16_t index = 0u; index < questions; ++index) {
        offset = dns_skip_name(packet, length, offset);
        if (offset == 0u || length - offset < 4u) return false;
        offset += 4u;
    }
    for (uint16_t index = 0u; index < answers; ++index) {
        offset = dns_skip_name(packet, length, offset);
        if (offset == 0u || length - offset < 10u) return false;
        const uint16_t type = read_be16(&packet[offset]);
        const uint16_t class_code = read_be16(&packet[offset + 2u]);
        const uint32_t ttl = read_be32(&packet[offset + 4u]);
        const size_t data_length = read_be16(&packet[offset + 8u]);
        offset += 10u;
        if (data_length > length - offset) return false;
        if (type == 1u && class_code == 1u && data_length == 4u) {
            pending_dns_answer.address = read_be32(&packet[offset]);
            pending_dns_answer.ttl_seconds = ttl;
            pending_dns_transaction = transaction;
            pending_dns_valid = true;
            return true;
        }
        offset += data_length;
    }
    return false;
}

static bool receive_udp(const uint8_t *packet, size_t length) {
    if (length < 8u) return false;
    const uint16_t source_port = read_be16(packet);
    const uint16_t destination_port = read_be16(&packet[2]);
    const size_t udp_length = read_be16(&packet[4]);
    if (udp_length < 8u || udp_length > length) return false;
    if (source_port == DHCP_SERVER_PORT && destination_port == DHCP_CLIENT_PORT)
        return receive_dhcp(&packet[8], udp_length - 8u);
    if (source_port == DNS_SERVER_PORT && destination_port == DNS_CLIENT_PORT)
        return receive_dns(&packet[8], udp_length - 8u);
    return true;
}

static uint32_t checksum_add(uint32_t sum, const uint8_t *bytes, size_t length) {
    while (length >= 2u) {
        sum += read_be16(bytes);
        bytes += 2u;
        length -= 2u;
    }
    if (length != 0u) sum += (uint16_t)bytes[0] << 8u;
    while (sum > 0xFFFFu) sum = (sum & 0xFFFFu) + (sum >> 16u);
    return sum;
}

static uint16_t transport_checksum(uint32_t source, uint32_t destination,
                                   uint8_t protocol, const uint8_t *packet,
                                   size_t length) {
    uint8_t pseudo[12];
    write_be32(pseudo, source);
    write_be32(&pseudo[4], destination);
    pseudo[8] = 0u;
    pseudo[9] = protocol;
    write_be16(&pseudo[10], (uint16_t)length);
    uint32_t sum = checksum_add(0u, pseudo, sizeof(pseudo));
    sum = checksum_add(sum, packet, length);
    return (uint16_t)~sum;
}

static bool receive_tcp(const uint8_t *packet, size_t length,
                        uint32_t source_address, uint32_t destination_address) {
    if (length < 20u || transport_checksum(source_address, destination_address,
                                            IP_PROTOCOL_TCP, packet, length) != 0u)
        return false;
    const size_t header_length = (size_t)(packet[12] >> 4u) * 4u;
    if (header_length < 20u || header_length > length) return false;
    const uint8_t flags = packet[13];
    const size_t data_length = length - header_length;
    if ((flags & TCP_FLAG_FIN) != 0u) {
        pending_tcp_fin = true;
        pending_tcp_fin_address = source_address;
        pending_tcp_fin_remote_port = read_be16(packet);
        pending_tcp_fin_local_port = read_be16(&packet[2]);
        pending_tcp_fin_next_sequence = read_be32(&packet[4]) +
            (uint32_t)data_length + 1u;
    }
    if (data_length != 0u) {
        const uint32_t sequence = read_be32(&packet[4]);
        const uint16_t remote_port = read_be16(packet);
        const uint16_t local_port = read_be16(&packet[2]);
        if (pending_tcp_data_length == 0u) {
            pending_tcp_data_address = source_address;
            pending_tcp_data_remote_port = remote_port;
            pending_tcp_data_local_port = local_port;
            pending_tcp_data_next_sequence = sequence;
        }
        if (pending_tcp_data_address != source_address ||
            pending_tcp_data_remote_port != remote_port ||
            pending_tcp_data_local_port != local_port ||
            pending_tcp_data_next_sequence != sequence ||
            data_length > sizeof(pending_tcp_data) - pending_tcp_data_length)
            return false;
        for (size_t index = 0u; index < data_length; ++index)
            pending_tcp_data[pending_tcp_data_length + index] =
                packet[header_length + index];
        pending_tcp_data_length += data_length;
        pending_tcp_data_next_sequence += (uint32_t)data_length;
        if ((flags & TCP_FLAG_FIN) != 0u) ++pending_tcp_data_next_sequence;
    }
    if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) !=
        (TCP_FLAG_SYN | TCP_FLAG_ACK)) return true;
    pending_tcp_remote_port = read_be16(packet);
    pending_tcp_local_port = read_be16(&packet[2]);
    pending_tcp_syn_ack.remote_sequence = read_be32(&packet[4]);
    pending_tcp_acknowledgment = read_be32(&packet[8]);
    pending_tcp_syn_ack.receive_window = read_be16(&packet[14]);
    pending_tcp_address = source_address;
    pending_tcp_valid = true;
    return true;
}

static bool receive_ipv4(const uint8_t *packet, size_t length) {
    if (length < 20u || (packet[0] >> 4u) != 4u) return false;
    const size_t header_length = (size_t)(packet[0] & 0x0Fu) * 4u;
    const size_t total_length = read_be16(&packet[2]);
    if (header_length < 20u || header_length > 60u ||
        header_length > length || total_length < header_length ||
        total_length > length) return false;
    if (network_ipv4_checksum(packet, header_length) != 0u) return false;
    if ((read_be16(&packet[6]) & 0x3FFFu) != 0u) return false;
    if (packet[9] == IP_PROTOCOL_UDP)
        return receive_udp(&packet[header_length], total_length - header_length);
    if (packet[9] == IP_PROTOCOL_TCP)
        return receive_tcp(&packet[header_length], total_length - header_length,
                           read_be32(&packet[12]), read_be32(&packet[16]));
    return true;
}

bool network_receive_ethernet(const uint8_t *frame, size_t length) {
    if (frame == NULL || length < ETHERNET_MIN_FRAME_BYTES ||
        state.link != NETWORK_LINK_UP) {
        ++state.dropped_frames;
        return false;
    }
    const uint16_t type = read_be16(&frame[12]);
    bool accepted = false;
    if (type == ETHER_TYPE_ARP)
        accepted = receive_arp(&frame[ETHERNET_HEADER_BYTES],
                               length - ETHERNET_HEADER_BYTES);
    else if (type == ETHER_TYPE_IPV4)
        accepted = receive_ipv4(&frame[ETHERNET_HEADER_BYTES],
                                length - ETHERNET_HEADER_BYTES);
    if (accepted) ++state.received_frames;
    else ++state.dropped_frames;
    return accepted;
}

size_t network_build_arp_request(uint8_t *frame, size_t capacity,
                                 uint32_t target_address) {
    if (frame == NULL || capacity < ETHERNET_MIN_FRAME_BYTES ||
        state.link != NETWORK_LINK_UP || state.ipv4_address == 0u)
        return 0u;
    for (size_t index = 0u; index < ETHERNET_MIN_FRAME_BYTES; ++index)
        frame[index] = 0u;
    for (size_t index = 0u; index < 6u; ++index) {
        frame[index] = 0xFFu;
        frame[6u + index] = state.mac[index];
    }
    write_be16(&frame[12], ETHER_TYPE_ARP);
    uint8_t *arp = &frame[ETHERNET_HEADER_BYTES];
    write_be16(arp, 1u);
    write_be16(&arp[2], ETHER_TYPE_IPV4);
    arp[4] = 6u;
    arp[5] = 4u;
    write_be16(&arp[6], 1u);
    copy_mac(&arp[8], state.mac);
    write_be32(&arp[14], state.ipv4_address);
    write_be32(&arp[24], target_address);
    return ETHERNET_MIN_FRAME_BYTES;
}

size_t network_build_dhcp_discover(uint8_t *frame, size_t capacity,
                                   uint32_t transaction_id) {
    const size_t dhcp_length = 250u;
    const size_t udp_length = 8u + dhcp_length;
    const size_t ip_length = 20u + udp_length;
    const size_t frame_length = ETHERNET_HEADER_BYTES + ip_length;
    if (frame == NULL || capacity < frame_length ||
        state.link != NETWORK_LINK_UP || transaction_id == 0u) return 0u;
    for (size_t index = 0u; index < frame_length; ++index) frame[index] = 0u;
    for (size_t index = 0u; index < 6u; ++index) {
        frame[index] = 0xFFu;
        frame[6u + index] = state.mac[index];
    }
    write_be16(&frame[12], ETHER_TYPE_IPV4);
    uint8_t *ip = &frame[14];
    ip[0] = 0x45u;
    write_be16(&ip[2], (uint16_t)ip_length);
    write_be16(&ip[4], 1u);
    ip[8] = 64u;
    ip[9] = IP_PROTOCOL_UDP;
    write_be32(&ip[16], 0xFFFFFFFFu);
    const uint16_t checksum = network_ipv4_checksum(ip, 20u);
    write_be16(&ip[10], checksum);
    uint8_t *udp = &ip[20];
    write_be16(udp, DHCP_CLIENT_PORT);
    write_be16(&udp[2], DHCP_SERVER_PORT);
    write_be16(&udp[4], (uint16_t)udp_length);
    uint8_t *dhcp = &udp[8];
    dhcp[0] = 1u;
    dhcp[1] = 1u;
    dhcp[2] = 6u;
    write_be32(&dhcp[4], transaction_id);
    write_be16(&dhcp[10], 0x8000u);
    copy_mac(&dhcp[28], state.mac);
    write_be32(&dhcp[236], 0x63825363u);
    dhcp[240] = 53u; dhcp[241] = 1u; dhcp[242] = 1u;
    dhcp[243] = 55u; dhcp[244] = 3u;
    dhcp[245] = 1u; dhcp[246] = 3u; dhcp[247] = 6u;
    dhcp[248] = 255u;
    pending_offer_valid = false;
    return frame_length;
}

bool network_dhcp_offer(uint32_t transaction_id,
                        struct network_dhcp_offer *offer) {
    if (!pending_offer_valid || pending_offer_transaction != transaction_id ||
        offer == NULL) return false;
    *offer = pending_offer;
    return true;
}

size_t network_build_dhcp_request(uint8_t *frame, size_t capacity,
                                  uint32_t transaction_id,
                                  uint32_t requested_address,
                                  uint32_t server_address) {
    const size_t dhcp_length = 261u;
    const size_t udp_length = 8u + dhcp_length;
    const size_t ip_length = 20u + udp_length;
    const size_t frame_length = ETHERNET_HEADER_BYTES + ip_length;
    if (frame == NULL || capacity < frame_length ||
        state.link != NETWORK_LINK_UP || transaction_id == 0u ||
        requested_address == 0u || server_address == 0u) return 0u;
    for (size_t index = 0u; index < frame_length; ++index) frame[index] = 0u;
    for (size_t index = 0u; index < 6u; ++index) {
        frame[index] = 0xFFu;
        frame[6u + index] = state.mac[index];
    }
    write_be16(&frame[12], ETHER_TYPE_IPV4);
    uint8_t *ip = &frame[14];
    ip[0] = 0x45u;
    write_be16(&ip[2], (uint16_t)ip_length);
    write_be16(&ip[4], 2u);
    ip[8] = 64u;
    ip[9] = IP_PROTOCOL_UDP;
    write_be32(&ip[16], 0xFFFFFFFFu);
    write_be16(&ip[10], network_ipv4_checksum(ip, 20u));
    uint8_t *udp = &ip[20];
    write_be16(udp, DHCP_CLIENT_PORT);
    write_be16(&udp[2], DHCP_SERVER_PORT);
    write_be16(&udp[4], (uint16_t)udp_length);
    uint8_t *dhcp = &udp[8];
    dhcp[0] = 1u;
    dhcp[1] = 1u;
    dhcp[2] = 6u;
    write_be32(&dhcp[4], transaction_id);
    write_be16(&dhcp[10], 0x8000u);
    copy_mac(&dhcp[28], state.mac);
    write_be32(&dhcp[236], 0x63825363u);
    dhcp[240] = 53u; dhcp[241] = 1u; dhcp[242] = 3u;
    dhcp[243] = 50u; dhcp[244] = 4u;
    write_be32(&dhcp[245], requested_address);
    dhcp[249] = 54u; dhcp[250] = 4u;
    write_be32(&dhcp[251], server_address);
    dhcp[255] = 55u; dhcp[256] = 3u;
    dhcp[257] = 1u; dhcp[258] = 3u; dhcp[259] = 6u;
    dhcp[260] = 255u;
    pending_ack_valid = false;
    return frame_length;
}

bool network_dhcp_ack(uint32_t transaction_id,
                      struct network_dhcp_offer *lease) {
    if (!pending_ack_valid || pending_ack_transaction != transaction_id ||
        lease == NULL) return false;
    *lease = pending_ack;
    return true;
}

size_t network_build_dns_query(uint8_t *frame, size_t capacity,
                               const uint8_t destination_mac[6],
                               uint32_t dns_address, uint16_t transaction_id,
                               const char *hostname) {
    if (frame == NULL || destination_mac == NULL || hostname == NULL ||
        state.link != NETWORK_LINK_UP || state.ipv4_address == 0u ||
        dns_address == 0u || transaction_id == 0u) return 0u;
    size_t hostname_length = 0u;
    while (hostname[hostname_length] != '\0' && hostname_length <= 253u)
        ++hostname_length;
    if (hostname_length == 0u || hostname_length > 253u) return 0u;
    const size_t dns_length = 12u + hostname_length + 2u + 4u;
    const size_t udp_length = 8u + dns_length;
    const size_t ip_length = 20u + udp_length;
    const size_t frame_length = ETHERNET_HEADER_BYTES + ip_length;
    if (capacity < frame_length) return 0u;
    for (size_t index = 0u; index < frame_length; ++index) frame[index] = 0u;
    copy_mac(frame, destination_mac);
    copy_mac(&frame[6], state.mac);
    write_be16(&frame[12], ETHER_TYPE_IPV4);
    uint8_t *ip = &frame[14];
    ip[0] = 0x45u;
    write_be16(&ip[2], (uint16_t)ip_length);
    write_be16(&ip[4], 3u);
    ip[8] = 64u;
    ip[9] = IP_PROTOCOL_UDP;
    write_be32(&ip[12], state.ipv4_address);
    write_be32(&ip[16], dns_address);
    write_be16(&ip[10], network_ipv4_checksum(ip, 20u));
    uint8_t *udp = &ip[20];
    write_be16(udp, DNS_CLIENT_PORT);
    write_be16(&udp[2], DNS_SERVER_PORT);
    write_be16(&udp[4], (uint16_t)udp_length);
    uint8_t *dns = &udp[8];
    write_be16(dns, transaction_id);
    write_be16(&dns[2], 0x0100u);
    write_be16(&dns[4], 1u);
    size_t source = 0u;
    size_t destination = 12u;
    while (source < hostname_length) {
        const size_t label_start = source;
        while (source < hostname_length && hostname[source] != '.') ++source;
        const size_t label_length = source - label_start;
        if (label_length == 0u || label_length > 63u) return 0u;
        dns[destination++] = (uint8_t)label_length;
        for (size_t index = 0u; index < label_length; ++index)
            dns[destination++] = (uint8_t)hostname[label_start + index];
        if (source < hostname_length) ++source;
    }
    dns[destination++] = 0u;
    write_be16(&dns[destination], 1u);
    write_be16(&dns[destination + 2u], 1u);
    pending_dns_valid = false;
    return frame_length;
}

bool network_dns_answer(uint16_t transaction_id,
                        struct network_dns_answer *answer) {
    if (!pending_dns_valid || pending_dns_transaction != transaction_id ||
        answer == NULL) return false;
    *answer = pending_dns_answer;
    return true;
}

static size_t build_tcp_frame(uint8_t *frame, size_t capacity,
                              const uint8_t destination_mac[6],
                              uint32_t remote_address, uint16_t local_port,
                              uint16_t remote_port, uint32_t sequence,
                              uint32_t acknowledgment, uint8_t flags) {
    const size_t tcp_length = 20u;
    const size_t ip_length = 20u + tcp_length;
    if (frame == NULL || destination_mac == NULL ||
        capacity < ETHERNET_MIN_FRAME_BYTES ||
        state.link != NETWORK_LINK_UP || state.ipv4_address == 0u ||
        remote_address == 0u || local_port == 0u || remote_port == 0u)
        return 0u;
    for (size_t index = 0u; index < ETHERNET_MIN_FRAME_BYTES; ++index)
        frame[index] = 0u;
    copy_mac(frame, destination_mac);
    copy_mac(&frame[6], state.mac);
    write_be16(&frame[12], ETHER_TYPE_IPV4);
    uint8_t *ip = &frame[14];
    ip[0] = 0x45u;
    write_be16(&ip[2], (uint16_t)ip_length);
    write_be16(&ip[4], 4u);
    ip[8] = 64u;
    ip[9] = IP_PROTOCOL_TCP;
    write_be32(&ip[12], state.ipv4_address);
    write_be32(&ip[16], remote_address);
    write_be16(&ip[10], network_ipv4_checksum(ip, 20u));
    uint8_t *tcp = &ip[20];
    write_be16(tcp, local_port);
    write_be16(&tcp[2], remote_port);
    write_be32(&tcp[4], sequence);
    write_be32(&tcp[8], acknowledgment);
    tcp[12] = 5u << 4u;
    tcp[13] = flags;
    write_be16(&tcp[14], 16384u);
    write_be16(&tcp[16], transport_checksum(state.ipv4_address,
                                             remote_address, IP_PROTOCOL_TCP,
                                             tcp, tcp_length));
    return ETHERNET_MIN_FRAME_BYTES;
}

size_t network_build_tcp_syn(uint8_t *frame, size_t capacity,
                             const uint8_t destination_mac[6],
                             uint32_t remote_address, uint16_t local_port,
                             uint16_t remote_port, uint32_t initial_sequence) {
    pending_tcp_valid = false;
    return build_tcp_frame(frame, capacity, destination_mac, remote_address,
                           local_port, remote_port, initial_sequence, 0u,
                           TCP_FLAG_SYN);
}

bool network_tcp_syn_ack(uint32_t remote_address, uint16_t local_port,
                         uint16_t remote_port, uint32_t initial_sequence,
                         struct network_tcp_syn_ack *answer) {
    if (!pending_tcp_valid || answer == NULL ||
        pending_tcp_address != remote_address ||
        pending_tcp_local_port != local_port ||
        pending_tcp_remote_port != remote_port ||
        pending_tcp_acknowledgment != initial_sequence + 1u) return false;
    *answer = pending_tcp_syn_ack;
    return true;
}

size_t network_build_tcp_ack(uint8_t *frame, size_t capacity,
                             const uint8_t destination_mac[6],
                             uint32_t remote_address, uint16_t local_port,
                             uint16_t remote_port, uint32_t local_sequence,
                             uint32_t remote_sequence) {
    return build_tcp_frame(frame, capacity, destination_mac, remote_address,
                           local_port, remote_port, local_sequence,
                           remote_sequence, TCP_FLAG_ACK);
}

size_t network_build_tcp_data(uint8_t *frame, size_t capacity,
                              const uint8_t destination_mac[6],
                              uint32_t remote_address, uint16_t local_port,
                              uint16_t remote_port, uint32_t local_sequence,
                              uint32_t remote_sequence, const uint8_t *data,
                              size_t data_length) {
    const size_t tcp_length = 20u + data_length;
    const size_t ip_length = 20u + tcp_length;
    const size_t frame_length = ETHERNET_HEADER_BYTES + ip_length;
    if (frame == NULL || destination_mac == NULL || data == NULL ||
        data_length == 0u || data_length > 1400u || capacity < frame_length ||
        state.link != NETWORK_LINK_UP || state.ipv4_address == 0u ||
        remote_address == 0u || local_port == 0u || remote_port == 0u)
        return 0u;
    for (size_t index = 0u; index < frame_length; ++index) frame[index] = 0u;
    copy_mac(frame, destination_mac);
    copy_mac(&frame[6], state.mac);
    write_be16(&frame[12], ETHER_TYPE_IPV4);
    uint8_t *ip = &frame[14];
    ip[0] = 0x45u;
    write_be16(&ip[2], (uint16_t)ip_length);
    write_be16(&ip[4], 5u);
    ip[8] = 64u;
    ip[9] = IP_PROTOCOL_TCP;
    write_be32(&ip[12], state.ipv4_address);
    write_be32(&ip[16], remote_address);
    write_be16(&ip[10], network_ipv4_checksum(ip, 20u));
    uint8_t *tcp = &ip[20];
    write_be16(tcp, local_port);
    write_be16(&tcp[2], remote_port);
    write_be32(&tcp[4], local_sequence);
    write_be32(&tcp[8], remote_sequence);
    tcp[12] = 5u << 4u;
    tcp[13] = TCP_FLAG_PSH | TCP_FLAG_ACK;
    write_be16(&tcp[14], 16384u);
    for (size_t index = 0u; index < data_length; ++index)
        tcp[20u + index] = data[index];
    write_be16(&tcp[16], transport_checksum(state.ipv4_address,
                                             remote_address, IP_PROTOCOL_TCP,
                                             tcp, tcp_length));
    pending_tcp_data_length = 0u;
    pending_tcp_fin = false;
    return frame_length;
}

size_t network_build_tcp_fin(uint8_t *frame, size_t capacity,
                             const uint8_t destination_mac[6],
                             uint32_t remote_address, uint16_t local_port,
                             uint16_t remote_port, uint32_t local_sequence,
                             uint32_t remote_sequence) {
    return build_tcp_frame(frame, capacity, destination_mac, remote_address,
                           local_port, remote_port, local_sequence,
                           remote_sequence, TCP_FLAG_FIN | TCP_FLAG_ACK);
}

bool network_tcp_remote_fin(uint32_t remote_address, uint16_t local_port,
                            uint16_t remote_port,
                            uint32_t *remote_next_sequence) {
    if (!pending_tcp_fin || remote_next_sequence == NULL ||
        pending_tcp_fin_address != remote_address ||
        pending_tcp_fin_local_port != local_port ||
        pending_tcp_fin_remote_port != remote_port) return false;
    *remote_next_sequence = pending_tcp_fin_next_sequence;
    return true;
}

size_t network_tcp_receive(uint32_t remote_address, uint16_t local_port,
                           uint16_t remote_port, uint8_t *destination,
                           size_t capacity, uint32_t *remote_next_sequence) {
    if (destination == NULL || remote_next_sequence == NULL ||
        pending_tcp_data_length == 0u ||
        pending_tcp_data_address != remote_address ||
        pending_tcp_data_local_port != local_port ||
        pending_tcp_data_remote_port != remote_port) return 0u;
    size_t amount = pending_tcp_data_length;
    if (amount > capacity) amount = capacity;
    for (size_t index = 0u; index < amount; ++index)
        destination[index] = pending_tcp_data[index];
    *remote_next_sequence = pending_tcp_data_next_sequence;
    const size_t result = amount;
    pending_tcp_data_length = 0u;
    return result;
}

void network_publish_http_response(const uint8_t *response, size_t length) {
    http_document_length = 0u;
    if (response == NULL) return;
    size_t body = 0u;
    while (body + 3u < length) {
        if (response[body] == '\r' && response[body + 1u] == '\n' &&
            response[body + 2u] == '\r' && response[body + 3u] == '\n') {
            body += 4u;
            break;
        }
        ++body;
    }
    if (body + 3u >= length) return;
    size_t amount = length - body;
    if (amount > sizeof(http_document)) amount = sizeof(http_document);
    for (size_t index = 0u; index < amount; ++index)
        http_document[index] = response[body + index];
    http_document_length = amount;
}

size_t network_http_response(uint8_t *destination, size_t capacity) {
    if (destination == NULL || capacity == 0u || http_document_length == 0u)
        return 0u;
    size_t amount = http_document_length;
    if (amount > capacity) amount = capacity;
    for (size_t index = 0u; index < amount; ++index)
        destination[index] = http_document[index];
    ++http_document_reads;
    return amount;
}

uint64_t network_http_reads(void) { return http_document_reads; }

void network_set_route(uint32_t gateway, uint32_t dns,
                       const uint8_t gateway_mac[6]) {
    active_route_valid = false;
    if (gateway == 0u || dns == 0u || gateway_mac == NULL) return;
    active_route.gateway = gateway;
    active_route.dns = dns;
    copy_mac(active_route.gateway_mac, gateway_mac);
    active_route_valid = true;
}

bool network_route(struct network_route *route) {
    if (!active_route_valid || route == NULL) return false;
    *route = active_route;
    return true;
}

bool network_arp_lookup(uint32_t address, uint8_t mac[6]) {
    if (mac == NULL) return false;
    for (size_t index = 0u; index < ARP_CACHE_LIMIT; ++index) {
        if (!arp_cache[index].valid || arp_cache[index].address != address) continue;
        copy_mac(mac, arp_cache[index].mac);
        return true;
    }
    return false;
}

void network_snapshot(struct network_snapshot *snapshot) {
    if (snapshot != NULL) *snapshot = state;
}

bool network_self_test(void) {
    const uint8_t device_mac[6] = {0x52u, 0x54u, 0x00u, 0x12u, 0x34u, 0x56u};
    const uint8_t peer_mac[6] = {0x52u, 0x54u, 0x00u, 0xABu, 0xCDu, 0xEFu};
    uint8_t frame[ETHERNET_MIN_FRAME_BYTES] = {0};
    network_set_device(device_mac);
    network_set_link(true);
    network_set_ipv4(0x0A00020Fu);
    for (size_t index = 0u; index < 6u; ++index) {
        frame[index] = 0xFFu;
        frame[6u + index] = peer_mac[index];
    }
    frame[12] = 0x08u; frame[13] = 0x06u;
    uint8_t *arp = &frame[14];
    arp[1] = 1u; arp[2] = 0x08u; arp[4] = 6u; arp[5] = 4u; arp[7] = 2u;
    copy_mac(&arp[8], peer_mac);
    arp[14] = 10u; arp[15] = 0u; arp[16] = 2u; arp[17] = 2u;
    copy_mac(&arp[18], device_mac);
    arp[24] = 10u; arp[25] = 0u; arp[26] = 2u; arp[27] = 15u;
    if (!network_receive_ethernet(frame, sizeof(frame))) return false;
    uint8_t resolved[6];
    if (!network_arp_lookup(0x0A000202u, resolved) ||
        !equal_mac(resolved, peer_mac)) return false;

    for (size_t index = 0u; index < sizeof(frame); ++index) frame[index] = 0u;
    copy_mac(frame, device_mac); copy_mac(&frame[6], peer_mac);
    frame[12] = 0x08u; frame[13] = 0x00u;
    uint8_t *ip = &frame[14];
    ip[0] = 0x45u; ip[2] = 0u; ip[3] = 20u; ip[8] = 64u; ip[9] = 1u;
    ip[12] = 10u; ip[15] = 2u; ip[16] = 10u; ip[18] = 2u; ip[19] = 15u;
    const uint16_t checksum = network_ipv4_checksum(ip, 20u);
    ip[10] = (uint8_t)(checksum >> 8u); ip[11] = (uint8_t)checksum;
    if (!network_receive_ethernet(frame, sizeof(frame))) return false;
    ip[0] = 0u;
    if (network_receive_ethernet(frame, sizeof(frame))) return false;
    return state.received_frames == 2u && state.dropped_frames == 1u &&
        state.arp_entries == 1u && state.link == NETWORK_LINK_UP;
}
