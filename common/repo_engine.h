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

#endif /* MINIDROP_REPO_ENGINE_H */

