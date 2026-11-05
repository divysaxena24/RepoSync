/**
 * @file queue.c
 * @brief Thread-safe bounded synchronization event queue implementation.
 */

#include "queue.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_QUEUE_CAPACITY 512

int sync_queue_init(sync_queue_t *queue, size_t capacity) {
    if (!queue) return -1;

    queue->capacity = (capacity == 0) ? DEFAULT_QUEUE_CAPACITY : capacity;
    queue->events = malloc(queue->capacity * sizeof(sync_event_t));
    if (!queue->events) {
        LOG_ERROR("QUEUE", "Failed to allocate memory for queue buffer");
        return -1;
    }

    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->is_shutdown = 0;

    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond_not_empty, NULL);
    pthread_cond_init(&queue->cond_not_full, NULL);

    return 0;
}

void sync_queue_destroy(sync_queue_t *queue) {
    if (!queue) return;

    pthread_mutex_lock(&queue->mutex);
    queue->is_shutdown = 1;
    pthread_cond_broadcast(&queue->cond_not_empty);
    pthread_cond_broadcast(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_empty);
    pthread_cond_destroy(&queue->cond_not_full);

    if (queue->events) {
        free(queue->events);
        queue->events = NULL;
    }
}

int sync_queue_push(sync_queue_t *queue, const sync_event_t *event) {
    if (!queue || !event) return -1;

    pthread_mutex_lock(&queue->mutex);

    if (queue->is_shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    /* Deduplication check: if the event is already queued, coalesce it */
    for (size_t i = 0; i < queue->count; i++) {
        size_t idx = (queue->head + i) % queue->capacity;
        if (strcmp(queue->events[idx].rel_path, event->rel_path) == 0) {
            /* Coalesce: if original was CREATE and new is MODIFY, keep CREATE */
            if (queue->events[idx].type == SYNC_EV_CREATE && event->type == SYNC_EV_MODIFY) {
                queue->events[idx].timestamp = event->timestamp;
            } else {
                queue->events[idx].type = event->type;
                queue->events[idx].timestamp = event->timestamp;
                strncpy(queue->events[idx].old_rel_path, event->old_rel_path,
                        sizeof(queue->events[idx].old_rel_path) - 1);
            }
            pthread_mutex_unlock(&queue->mutex);
            return 0;
        }
    }

    /* Wait while queue is full */
    while (queue->count == queue->capacity && !queue->is_shutdown) {
        pthread_cond_wait(&queue->cond_not_full, &queue->mutex);
    }

    if (queue->is_shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    queue->events[queue->tail] = *event;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    pthread_cond_signal(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

int sync_queue_pop(sync_queue_t *queue, sync_event_t *event_out) {
    if (!queue || !event_out) return -1;

    pthread_mutex_lock(&queue->mutex);

    /* Wait while queue is empty */
    while (queue->count == 0 && !queue->is_shutdown) {
        pthread_cond_wait(&queue->cond_not_empty, &queue->mutex);
    }

    if (queue->count == 0 && queue->is_shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return 1; /* Clean shutdown signal */
    }

    *event_out = queue->events[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;

    pthread_cond_signal(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

void sync_queue_shutdown(sync_queue_t *queue) {
    if (!queue) return;
    pthread_mutex_lock(&queue->mutex);
    queue->is_shutdown = 1;
    pthread_cond_broadcast(&queue->cond_not_empty);
    pthread_cond_broadcast(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
}

