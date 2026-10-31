#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

typedef struct {
    int port;
    int worker_threads;
    int queue_capacity;
    char storage_dir[MAX_PATH_LENGTH];
    char log_file[MAX_PATH_LENGTH];
    size_t chunk_size;
    bool verbose;
} server_config_t;

/* Global server configuration */
extern server_config_t g_config;

void config_init_defaults(server_config_t *cfg);
void config_parse_args(server_config_t *cfg, int argc, char *argv[]);
void config_print(const server_config_t *cfg);

#endif /* CONFIG_H */
