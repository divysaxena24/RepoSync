/**
 * @file repo_engine.h
 * @brief Core single-user repository push, pull, and state coordinator.
 */

#ifndef MINIDROP_REPO_ENGINE_H
#define MINIDROP_REPO_ENGINE_H

#include "metadata.h"
#include <pthread.h>

typedef struct {
    char local_dir[MINIDROP_MAX_PATH_LEN];
    char remote_dir[MINIDROP_MAX_PATH_LEN];
    volatile repo_state_t active_state;
    volatile int active_progress;
    char current_file[MINIDROP_MAX_PATH_LEN];
    char last_event_msg[256];
    volatile int auto_sync_running;
    pthread_t auto_sync_thread;
    pthread_mutex_t lock;
} repo_engine_t;

/**
 * @brief Initialize the repository engine with paths.
 */
int repo_engine_init(repo_engine_t *engine, const char *local_dir, const char *remote_dir);

/**
 * @brief Get the current live diff snapshot.
 */
int repo_engine_get_status(repo_engine_t *engine, repo_diff_t *out_diff);

/**
 * @brief Execute PUSH action: Synchronize all local changes to remote repository.
 * Chunked file streaming with OpenSSL SHA-256 verification and atomic commit.
 */
int repo_engine_push(repo_engine_t *engine);

/**
 * @brief Execute PULL action: Download all remote repository updates to local directory.
 */
int repo_engine_pull(repo_engine_t *engine);

/**
 * @brief Execute PRUNE action: Remove files from remote repository that were deleted locally.
 */
int repo_engine_prune(repo_engine_t *engine);

/**
 * @brief Record a sync or commit event into the persistent repository history log.
 */
int repo_engine_log_history(repo_engine_t *engine, const char *action, size_t file_count, const char *summary);

/**
 * @brief Retrieve recent repository history as a JSON array string.
 */
int repo_engine_get_history_json(repo_engine_t *engine, char *out_buf, size_t max_len);

/**
 * @brief Start background autonomous real-time synchronization daemon.
 */
int repo_engine_start_autosync(repo_engine_t *engine);

/**
 * @brief Stop background autonomous synchronization daemon.
 */
void repo_engine_stop_autosync(repo_engine_t *engine);

#endif /* MINIDROP_REPO_ENGINE_H */

