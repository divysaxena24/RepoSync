/**
 * @file sync_engine.c
 * @brief Synchronization engine coordinator and background worker thread implementation.
 */

#include "sync_engine.h"
#include "transfer.h"
#include "metadata.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

static void *sync_worker_thread_func(void *arg) {
    sync_engine_t *engine = (sync_engine_t *)arg;
    LOG_INFO("SYNC", "Background sync worker thread started (tid=%lu)", (unsigned long)pthread_self());

    sync_event_t ev;
    while (engine->is_running) {
        int res = sync_queue_pop(&engine->queue, &ev);
        if (res == 1) {
            /* Queue was shut down */
            break;
        }
        if (res < 0) {
            LOG_ERROR("SYNC", "sync_queue_pop failed");
            break;
        }

        LOG_DEBUG("SYNC", "Processing event type %d for '%s'", ev.type, ev.rel_path);

        switch (ev.type) {
            case SYNC_EV_CREATE:
            case SYNC_EV_MODIFY:
                transfer_upload_file(engine->sockfd, engine->config.sync_dir,
                                     ev.rel_path, engine->config.client_id);
                break;

            case SYNC_EV_DELETE:
                transfer_delete_file(engine->sockfd, ev.rel_path, engine->config.client_id);
                break;

            case SYNC_EV_RENAME:
                transfer_rename(engine->sockfd, ev.old_rel_path, ev.rel_path,
                                engine->config.client_id);
                break;

            case SYNC_EV_DIR_CREATE:
                transfer_create_dir(engine->sockfd, ev.rel_path, engine->config.client_id);
                break;

            case SYNC_EV_DIR_DELETE:
                transfer_delete_dir(engine->sockfd, ev.rel_path, engine->config.client_id);
                break;

            default:
                break;
        }
    }

    LOG_INFO("SYNC", "Background sync worker thread exiting");
    return NULL;
}

int sync_engine_init(sync_engine_t *engine, int sockfd, const client_config_t *config) {
    if (!engine || sockfd < 0 || !config) return -1;

    memset(engine, 0, sizeof(sync_engine_t));
    engine->sockfd = sockfd;
    engine->config = *config;
    engine->is_running = 0;

    /* Ensure sync directory exists locally */
    struct stat st;
    if (stat(config->sync_dir, &st) != 0) {
        if (mkdir(config->sync_dir, 0755) != 0 && errno != EEXIST) {
            LOG_ERROR("SYNC", "Failed to create local sync folder '%s': %s",
                      config->sync_dir, strerror(errno));
            return -1;
        }
    }

    if (sync_queue_init(&engine->queue, 512) != 0) {
        return -1;
    }

    return 0;
}

int sync_engine_initial_sync(sync_engine_t *engine) {
    if (!engine) return -1;

    LOG_INFO("SYNC", "=================================================");
    LOG_INFO("SYNC", " Performing Initial Directory Inventory Scan...");
    LOG_INFO("SYNC", " Local Path: %s", engine->config.sync_dir);
    LOG_INFO("SYNC", "=================================================");

    sync_manifest_t manifest;
    int count = sync_manifest_scan(engine->config.sync_dir, &manifest);
    if (count < 0) {
        LOG_ERROR("SYNC", "Failed to scan local sync directory");
        return -1;
    }

    LOG_INFO("SYNC", "Discovered %zu item(s) locally. Reconciling with server...", manifest.count);

    for (size_t i = 0; i < manifest.count; i++) {
        const sync_entry_t *entry = &manifest.entries[i];
        if (entry->is_dir) {
            transfer_create_dir(engine->sockfd, entry->rel_path, engine->config.client_id);
        } else {
            transfer_upload_file(engine->sockfd, engine->config.sync_dir,
                                 entry->rel_path, engine->config.client_id);
        }
    }

    sync_manifest_free(&manifest);
    LOG_INFO("SYNC", "Initial synchronization completed successfully!");
    return 0;
}

int sync_engine_start(sync_engine_t *engine) {
    if (!engine) return -1;

    engine->is_running = 1;

    /* 1. Start Watcher */
    if (watcher_init(&engine->watcher, engine->config.sync_dir, &engine->queue) != 0) {
        LOG_ERROR("SYNC", "Failed to initialize directory watcher");
        engine->is_running = 0;
        return -1;
    }

    if (watcher_start(&engine->watcher) != 0) {
        LOG_ERROR("SYNC", "Failed to start directory watcher thread");
        engine->is_running = 0;
        return -1;
    }

    /* 2. Start Worker Thread */
    if (pthread_create(&engine->worker_thread, NULL, sync_worker_thread_func, engine) != 0) {
        LOG_ERROR("SYNC", "Failed to spawn worker thread: %s", strerror(errno));
        watcher_stop(&engine->watcher);
        engine->is_running = 0;
        return -1;
    }

    return 0;
}

void sync_engine_stop(sync_engine_t *engine) {
    if (!engine || !engine->is_running) return;

    LOG_INFO("SYNC", "Stopping synchronization engine...");
    engine->is_running = 0;

    watcher_stop(&engine->watcher);
    sync_queue_shutdown(&engine->queue);
    pthread_join(engine->worker_thread, NULL);
    sync_queue_destroy(&engine->queue);
    LOG_INFO("SYNC", "Synchronization engine stopped cleanly");
}

