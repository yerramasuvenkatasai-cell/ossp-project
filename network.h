#ifndef NETWORK_H
#define NETWORK_H

#include "common.h"
#include "file_cache.h"

typedef struct {
    char method[16];
    char path[MAX_PATH_LENGTH];
    bool is_range_request;
    size_t range_start;
    size_t range_end;
    bool range_has_end;
} http_request_t;

int network_create_listener(int port);
ssize_t network_send_all(int socket_fd, const void *buffer, size_t length);

bool network_parse_request(int socket_fd, http_request_t *req);
void network_send_error(int socket_fd, int status_code, const char *status_text, const char *detail_msg);
void network_send_stats_response(int socket_fd);

/* Transfers memory-mapped file over TCP socket, supporting full or partial Range content */
bool network_send_file(int socket_fd, const mapped_file_t *mapped, const http_request_t *req,
                       size_t *out_bytes_sent, int *out_status_code);

#endif /* NETWORK_H */
