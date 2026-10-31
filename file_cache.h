#ifndef FILE_CACHE_H
#define FILE_CACHE_H

#include "common.h"

/* Structure representing an entry in the active file registry */
typedef struct file_entry {
    char filepath[MAX_PATH_LENGTH];
    size_t file_size;
    int active_readers;          /* Number of client threads currently reading this file */
    uint64_t total_requests;     /* Total requests served for this file */
    uint64_t total_bytes_sent;   /* Total bytes delivered for this file */
    struct file_entry *next;
} file_entry_t;

/* Mapped file descriptor handle */
typedef struct {
    int fd;
    void *map_addr;
    size_t file_size;
    char safe_relative_path[MAX_PATH_LENGTH];
    char absolute_path[MAX_PATH_LENGTH];
    char mime_type[64];
} mapped_file_t;

bool file_cache_init(void);
void file_cache_destroy(void);

/* Validates and maps a file using mmap(). Returns true on success */
bool file_cache_map(const char *root_dir, const char *url_path, mapped_file_t *out_mapped, int *out_http_status);

/* Unmaps memory and releases file descriptors */
void file_cache_unmap(mapped_file_t *mapped);

/* Concurrent Reader Registry methods */
void file_registry_get_stats_summary(char *buffer, size_t max_len);

#endif /* FILE_CACHE_H */
