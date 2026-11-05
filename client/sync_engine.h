/**
 * @file sync_engine.h
 * @brief Synchronization engine coordinator and background worker thread.
 */

#ifndef MINIDROP_SYNC_ENGINE_H
#define MINIDROP_SYNC_ENGINE_H

#include "client.h"
#include "queue.h"
#include "watcher.h"
#include <pthread.h>

typedef struct {
    int sockfd;
    client_config_t config;
    sync_queue_t queue;
    dir_watcher_t watcher;
    pthread_t worker_thread;
    volatile int is_running;
} sync_engine_t;

/**
 * @brief Initialize synchronization engine.
 */
int sync_engine_init(sync_engine_t *engine, int sockfd, const client_config_t *config);

/**
 * @brief Perform initial directory inventory scan and reconcile all files with server.
 */
int sync_engine_initial_sync(sync_engine_t *engine);

/**
 * @brief Start background worker thread and inotify monitoring.
 */
int sync_engine_start(sync_engine_t *engine);

/**
 * @brief Stop synchronization engine gracefully.
 */
void sync_engine_stop(sync_engine_t *engine);

#endif /* MINIDROP_SYNC_ENGINE_H */

