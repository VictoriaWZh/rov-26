#ifndef UDP_SENDER_H
#define UDP_SENDER_H

#include "udp_common.h"
#include <stddef.h>

typedef struct {
    udp_endpoint_t local;
    struct sockaddr_in remote_addr;
    uint32_t max_packet_size;
    uint32_t max_payload_per_packet;
    uint32_t max_frame_size;
    uint8_t* packet_buf;
} udp_sender_t;

udp_sender_t* udp_sender_create(const char* local_ip, uint16_t local_port,
                                 const char* remote_ip, uint16_t remote_port,
                                 uint32_t max_packet_size, uint32_t max_frame_size);

int udp_sender_transmit(udp_sender_t* sender, uint64_t timestamp_us,
                        const void* frame_data, uint32_t frame_len,
                        uint32_t repeat_count);

void udp_sender_destroy(udp_sender_t* sender);

#endif
