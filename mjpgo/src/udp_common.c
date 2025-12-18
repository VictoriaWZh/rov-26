#include "../include/udp_common.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

int udp_create_socket(const char* ip, uint16_t port, udp_endpoint_t* out) {
    if (!out) return -1;
    
    memset(out, 0, sizeof(*out));
    out->sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (out->sock_fd < 0) return -1;
    
    int opt_val = 1;
    if (setsockopt(out->sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)) < 0) {
        close(out->sock_fd);
        out->sock_fd = -1;
        return -1;
    }
    
    int flags = fcntl(out->sock_fd, F_GETFL, 0);
    if (flags < 0) {
        close(out->sock_fd);
        out->sock_fd = -1;
        return -1;
    }
    flags &= ~O_NONBLOCK;
    if (fcntl(out->sock_fd, F_SETFL, flags) < 0) {
        close(out->sock_fd);
        out->sock_fd = -1;
        return -1;
    }
    
    out->addr.sin_family = AF_INET;
    out->addr.sin_port = htons(port);
    out->addr.sin_addr.s_addr = inet_addr(ip);
    
    if (bind(out->sock_fd, (struct sockaddr*)&out->addr, sizeof(out->addr)) < 0) {
        close(out->sock_fd);
        out->sock_fd = -1;
        return -1;
    }
    
    return 0;
}

void udp_close_socket(udp_endpoint_t* ep) {
    if (ep && ep->sock_fd >= 0) {
        close(ep->sock_fd);
        ep->sock_fd = -1;
    }
}

uint64_t udp_get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}
