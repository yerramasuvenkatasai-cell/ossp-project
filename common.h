#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>

#include <pthread.h>
#include <semaphore.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Terminal ANSI Color Codes for clean visual logging */
#define ANSI_RESET   "\x1b[0m"
#define ANSI_BOLD    "\x1b[1m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_BLUE    "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_WHITE   "\x1b[37m"

/* Default Server Limits & Parameters */
#define DEFAULT_PORT           8080
#define DEFAULT_WORKER_THREADS 8
#define DEFAULT_QUEUE_CAPACITY 256
#define DEFAULT_STORAGE_DIR    "./storage"
#define DEFAULT_LOG_FILE       "server.log"
#define STREAM_CHUNK_SIZE      (64 * 1024)   /* 64 KB per network chunk */
#define MAX_PATH_LENGTH        512
#define MAX_HTTP_HEADER_SIZE   8192

#endif /* COMMON_H */
