#include "../include/udp_sender.h"
#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

udp_sender_t* udp_sender_create(const char* local_ip, uint16_t local_port,
                                 const char* remote_ip, uint16_t remote_port,
                                 uint32_t max_packet_size, uint32_t max_frame_size) {
    udp_sender_t* sender = calloc(1, sizeof(*sender));
    if (!sender) return NULL;
    
    sender->max_packet_size = max_packet_size;
    sender->max_payload_per_packet = max_packet_size - PACKET_HEADER_SIZE;
    sender->max_frame_size = max_frame_size;
    
    sender->packet_buf = malloc(max_packet_size);
    if (!sender->packet_buf) {
        free(sender);
        return NULL;
    }
    
    if (udp_create_socket(local_ip, local_port, &sender->local) < 0) {
        free(sender->packet_buf);
        free(sender);
        return NULL;
    }
    
    memset(&sender->remote_addr, 0, sizeof(sender->remote_addr));
    sender->remote_addr.sin_family = AF_INET;
    sender->remote_addr.sin_port = htons(remote_port);
    sender->remote_addr.sin_addr.s_addr = inet_addr(remote_ip);
    
    return sender;
}

int udp_sender_transmit(udp_sender_t* sender, uint64_t timestamp_us,
                        const void* frame_data, uint32_t frame_len,
                        uint32_t repeat_count) {
    if (!sender || !frame_data || frame_len == 0) return -1;
    if (frame_len > sender->max_frame_size) return -1;
    
    uint32_t seg_count = (frame_len + sender->max_payload_per_packet - 1) / sender->max_payload_per_packet;
    uint64_t ts_be = htobe64(timestamp_us);
    uint32_t count_be = htonl(seg_count);
    
    const uint8_t* src = (const uint8_t*)frame_data;
    
    for (uint32_t round = 0; round < repeat_count; round++) {
        for (uint32_t seg = 0; seg < seg_count; seg++) {
            uint32_t offset = seg * sender->max_payload_per_packet;
            uint32_t payload_len = (seg == seg_count - 1) 
                ? (frame_len - offset) 
                : sender->max_payload_per_packet;
            
            packet_header_t* hdr = (packet_header_t*)sender->packet_buf;
            hdr->frame_ts_us = ts_be;
            hdr->seg_idx = htonl(seg);
            hdr->seg_count = count_be;
            hdr->payload_len = htonl(payload_len);
            
            memcpy(sender->packet_buf + PACKET_HEADER_SIZE, src + offset, payload_len);
            
            ssize_t total_len = PACKET_HEADER_SIZE + payload_len;
            ssize_t sent;
            
            do {
                sent = sendto(sender->local.sock_fd, sender->packet_buf, total_len, 0,
                              (struct sockaddr*)&sender->remote_addr, sizeof(sender->remote_addr));
            } while (sent < 0 && errno == EINTR);
            
            if (sent < 0) return -1;
        }
    }
    
    return 0;
}

void udp_sender_destroy(udp_sender_t* sender) {
    if (!sender) return;
    udp_close_socket(&sender->local);
    free(sender->packet_buf);
    free(sender);
}
