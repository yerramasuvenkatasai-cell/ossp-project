#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"

/* Structure representing an accepted client request task */
typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
    struct timeval arrival_time;
} request_task_t;

/* Synchronized Circular Request Queue structure */
typedef struct {
    request_task_t *buffer;
    int capacity;
    int head;
    int tail;
    int count;

    pthread_mutex_t lock;      /* Mutex for queue state manipulation */
    sem_t sem_items;           /* Semaphore counting queued items */
    sem_t sem_slots;           /* Semaphore counting available empty slots */

    volatile bool shutdown;    /* Flag indicating server is shutting down */
} request_queue_t;

request_queue_t* queue_create(int capacity);
bool queue_push(request_queue_t *q, request_task_t task);
bool queue_pop(request_queue_t *q, request_task_t *task);
void queue_broadcast_shutdown(request_queue_t *q, int worker_count);
int queue_get_count(request_queue_t *q);
void queue_destroy(request_queue_t *q);

#endif /* QUEUE_H */
