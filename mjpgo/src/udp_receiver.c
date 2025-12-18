#include "../include/udp_receiver.h"
#include <endian.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static inline void bitmap_clear(uint64_t* bm) {
    memset(bm, 0, SEGMENT_BITMAP_SIZE * sizeof(uint64_t));
}

static inline void bitmap_set(uint64_t* bm, uint32_t idx) {
    bm[idx / 64] |= (1ULL << (idx % 64));
}

static inline bool bitmap_test(const uint64_t* bm, uint32_t idx) {
    return (bm[idx / 64] & (1ULL << (idx % 64))) != 0;
}

udp_receiver_t* udp_receiver_create(const char* local_ip, uint16_t local_port,
                                     uint32_t max_packet_size, uint32_t max_frame_size) {
    udp_receiver_t* recv = calloc(1, sizeof(*recv));
    if (!recv) return NULL;
    
    recv->max_packet_size = max_packet_size;
    recv->max_payload_per_packet = max_packet_size - PACKET_HEADER_SIZE;
    recv->max_frame_size = max_frame_size;
    
    recv->packet_buf = malloc(max_packet_size);
    if (!recv->packet_buf) {
        free(recv);
        return NULL;
    }
    
    recv->frame_buf = malloc(max_frame_size);
    if (!recv->frame_buf) {
        free(recv->packet_buf);
        free(recv);
        return NULL;
    }
    
    if (udp_create_socket(local_ip, local_port, &recv->local) < 0) {
        free(recv->frame_buf);
        free(recv->packet_buf);
        free(recv);
        return NULL;
    }
    
    recv->tracked_ts = 0;
    bitmap_clear(recv->segment_bitmap);
    
    return recv;
}

bool udp_receiver_get_frame(udp_receiver_t* recv) {
    if (!recv) return false;
    
    while (1) {
        ssize_t bytes_in;
        do {
            bytes_in = recvfrom(recv->local.sock_fd, recv->packet_buf, 
                               recv->max_packet_size, 0, NULL, NULL);
        } while (bytes_in < 0 && errno == EINTR);
        
        if (bytes_in < 0 && errno == EBADF) return false;
        if (bytes_in < (ssize_t)PACKET_HEADER_SIZE) continue;
        
        const packet_header_t* hdr = (const packet_header_t*)recv->packet_buf;
        uint64_t ts = be64toh(hdr->frame_ts_us);
        uint32_t seg_idx = ntohl(hdr->seg_idx);
        uint32_t seg_count = ntohl(hdr->seg_count);
        uint32_t payload_len = ntohl(hdr->payload_len);
        
        if ((ssize_t)(PACKET_HEADER_SIZE + payload_len) != bytes_in) continue;
        if (seg_idx >= MAX_SEGMENTS_PER_FRAME) continue;
        if (seg_count > MAX_SEGMENTS_PER_FRAME) continue;
        
        if (recv->tracked_ts != ts) {
            recv->tracked_ts = ts;
            recv->segments_received = 0;
            recv->segments_expected = seg_count;
            bitmap_clear(recv->segment_bitmap);
        }
        
        if (bitmap_test(recv->segment_bitmap, seg_idx)) continue;
        
        uint32_t offset = seg_idx * recv->max_payload_per_packet;
        if (offset + payload_len > recv->max_frame_size) continue;
        
        memcpy(recv->frame_buf + offset, recv->packet_buf + PACKET_HEADER_SIZE, payload_len);
        bitmap_set(recv->segment_bitmap, seg_idx);
        recv->segments_received++;
        
        if (seg_idx == seg_count - 1) {
            recv->frame_len = offset + payload_len;
            recv->frame_ts_us = ts;
        }
        
        if (recv->segments_received == recv->segments_expected) {
            return true;
        }
    }
}

void udp_receiver_destroy(udp_receiver_t* recv) {
    if (!recv) return;
    udp_close_socket(&recv->local);
    free(recv->frame_buf);
    free(recv->packet_buf);
    free(recv);
}
