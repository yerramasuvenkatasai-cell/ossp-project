#ifndef MONITOR_H
#define MONITOR_H

#include "common.h"

typedef struct {
    time_t start_time;
    uint64_t total_requests;
    uint64_t total_bytes_sent;
    uint64_t total_errors;
    int active_connections;
    int peak_connections;
    pthread_mutex_t lock;
} server_monitor_t;

extern server_monitor_t g_monitor;

void monitor_init(void);
void monitor_connection_opened(void);
void monitor_connection_closed(void);
void monitor_record_request(size_t bytes_sent, bool is_error);

/* Memory and CPU metrics */
double monitor_get_memory_rss_mb(void);
void monitor_get_cpu_times(double *user_sec, double *sys_sec);

/* Reporting and Dashboard */
void monitor_generate_json(char *buffer, size_t max_len);
void monitor_print_summary(void);

#endif /* MONITOR_H */
