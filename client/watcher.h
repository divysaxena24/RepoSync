/**
 * @file watcher.h
 * @brief Recursive directory monitoring subsystem using Linux inotify API.
 */

#ifndef MINIDROP_WATCHER_H
#define MINIDROP_WATCHER_H

#include "queue.h"
#include <pthread.h>

#define MAX_WATCHES 1024

typedef struct {
    int inotify_fd;
    char sync_dir[MINIDROP_MAX_PATH_LEN];
    sync_queue_t *queue;
    pthread_t thread;
    volatile int is_running;
} dir_watcher_t;

/**
 * @brief Initialize the inotify directory watcher.
 */
int watcher_init(dir_watcher_t *watcher, const char *sync_dir, sync_queue_t *queue);

/**
 * @brief Start the watcher background thread.
 */
int watcher_start(dir_watcher_t *watcher);

/**
 * @brief Stop the watcher thread and clean up inotify watch descriptors.
 */
void watcher_stop(dir_watcher_t *watcher);

#endif /* MINIDROP_WATCHER_H */

