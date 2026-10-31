#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "common.h"
#include "queue.h"

typedef struct {
    pthread_t *threads;
    int worker_count;
    request_queue_t *queue;
    volatile bool is_running;
} threadpool_t;

threadpool_t* threadpool_create(int worker_count, request_queue_t *queue);
void threadpool_shutdown(threadpool_t *pool);
void threadpool_destroy(threadpool_t *pool);

#endif /* THREADPOOL_H */
