#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_ACCESS
} log_level_t;

bool logger_init(const char *log_filepath);
void logger_close(void);

void log_message(log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

void log_access(const char *client_ip, int client_port, pthread_t thread_id,
                const char *method, const char *path, int status_code,
                size_t bytes_sent, double duration_ms);

#define LOG_DEBUG(fmt, ...) log_message(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  log_message(LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_message(LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) log_message(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

#endif /* LOGGER_H */
