#ifndef UDP_COMMON_H
#define UDP_COMMON_H

#include <stdint.h>
#include <netinet/in.h>

#define PACKET_HEADER_SIZE 20

typedef struct __attribute__((packed)) {
    uint64_t frame_ts_us;
    uint32_t seg_idx;
    uint32_t seg_count;
    uint32_t payload_len;
} packet_header_t;

typedef struct {
    struct sockaddr_in addr;
    int sock_fd;
} udp_endpoint_t;

int udp_create_socket(const char* ip, uint16_t port, udp_endpoint_t* out);
void udp_close_socket(udp_endpoint_t* ep);
uint64_t udp_get_time_us(void);

#endif
