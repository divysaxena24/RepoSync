/**
 * @file queue.h
 * @brief Thread-safe bounded synchronization event queue for producer-consumer pipeline.
 */

#ifndef MINIDROP_QUEUE_H
#define MINIDROP_QUEUE_H

#include <stdint.h>
#include <pthread.h>
#include "protocol.h"

typedef enum {
    SYNC_EV_CREATE = 0,
    SYNC_EV_MODIFY,
    SYNC_EV_DELETE,
    SYNC_EV_RENAME,
    SYNC_EV_DIR_CREATE,
    SYNC_EV_DIR_DELETE
} sync_event_type_t;

typedef struct {
    sync_event_type_t type;
    char rel_path[MINIDROP_MAX_PATH_LEN];
    char old_rel_path[MINIDROP_MAX_PATH_LEN]; /* Used for rename operations */
    uint64_t timestamp;
} sync_event_t;

typedef struct {
    sync_event_t *events;
    size_t head;
    size_t tail;
    size_t count;
    size_t capacity;
    int is_shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
} sync_queue_t;

/**
 * @brief Initialize the thread-safe queue.
 */
int sync_queue_init(sync_queue_t *queue, size_t capacity);

/**
 * @brief Destroy the queue and free resources.
 */
void sync_queue_destroy(sync_queue_t *queue);

/**
 * @brief Enqueue a synchronization event with automatic deduplication.
 * Blocks if queue is full until space is available.
 * @return 0 on success, -1 on shutdown or error.
 */
int sync_queue_push(sync_queue_t *queue, const sync_event_t *event);

/**
 * @brief Dequeue a synchronization event.
 * Blocks if queue is empty until an event arrives or queue is shut down.
 * @return 0 on success, 1 on queue shutdown (empty), -1 on error.
 */
int sync_queue_pop(sync_queue_t *queue, sync_event_t *event_out);

/**
 * @brief Signal the queue to shut down, waking up all waiting consumer threads.
 */
void sync_queue_shutdown(sync_queue_t *queue);

#endif /* MINIDROP_QUEUE_H */

