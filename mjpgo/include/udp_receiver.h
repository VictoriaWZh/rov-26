#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

#include "udp_common.h"
#include <stdbool.h>
#include <stddef.h>

#define MAX_SEGMENTS_PER_FRAME 1024
#define SEGMENT_BITMAP_SIZE (MAX_SEGMENTS_PER_FRAME / 64)

typedef struct {
    udp_endpoint_t local;
    uint32_t max_packet_size;
    uint32_t max_payload_per_packet;
    uint32_t max_frame_size;
    uint8_t* packet_buf;
    uint8_t* frame_buf;
    uint32_t frame_len;
    uint64_t frame_ts_us;
    uint64_t tracked_ts;
    uint32_t segments_received;
    uint32_t segments_expected;
    uint64_t segment_bitmap[SEGMENT_BITMAP_SIZE];
} udp_receiver_t;

udp_receiver_t* udp_receiver_create(const char* local_ip, uint16_t local_port,
                                     uint32_t max_packet_size, uint32_t max_frame_size);

bool udp_receiver_get_frame(udp_receiver_t* receiver);

void udp_receiver_destroy(udp_receiver_t* receiver);

#endif
