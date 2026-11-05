/**
 * @file repo_engine.c
 * @brief Core single-user repository push, pull, and state coordinator implementation.
 */

#include "repo_engine.h"
#include "logger.h"
#include "checksum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <libgen.h>

#define CHUNK_SIZE 65536

static int ensure_dir_exists(const char *dir) {
    char temp[MINIDROP_MAX_PATH_LEN * 2];
    strncpy(temp, dir, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char path_buf[MINIDROP_MAX_PATH_LEN * 2];
    path_buf[0] = '\0';

    char *p = temp;
    if (*p == '/') {
        strcat(path_buf, "/");
        p++;
    }

    char *token = strtok(p, "/\\");
    while (token != NULL) {
        strcat(path_buf, token);
        struct stat st;
        if (stat(path_buf, &st) != 0) {
            if (mkdir(path_buf, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
        }
        strcat(path_buf, "/");
        token = strtok(NULL, "/\\");
    }
    return 0;
}

static int copy_file_chunked_atomic(const char *src_path, const char *dst_path) {
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        LOG_ERROR("REPO", "Cannot open source file '%s': %s", src_path, strerror(errno));
        return -1;
    }

    /* Ensure destination parent directories */
    char temp_dst[MINIDROP_MAX_PATH_LEN * 2];
    strncpy(temp_dst, dst_path, sizeof(temp_dst) - 1);
    temp_dst[sizeof(temp_dst) - 1] = '\0';
    char *dir_name = dirname(temp_dst);
    ensure_dir_exists(dir_name);

    /* Stage into .<filename>.tmp.<pid> */
    char dir_copy[MINIDROP_MAX_PATH_LEN * 2];
    char base_copy[MINIDROP_MAX_PATH_LEN * 2];
    strncpy(dir_copy, dst_path, sizeof(dir_copy) - 1);
    strncpy(base_copy, dst_path, sizeof(base_copy) - 1);
    dir_copy[sizeof(dir_copy) - 1] = '\0';
    base_copy[sizeof(base_copy) - 1] = '\0';

    char staging_path[MINIDROP_MAX_PATH_LEN * 2];
    snprintf(staging_path, sizeof(staging_path), "%s/.%s.tmp.%d",
             dirname(dir_copy), basename(base_copy), (int)getpid());

    int dst_fd = open(staging_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        LOG_ERROR("REPO", "Cannot create staging file '%s': %s", staging_path, strerror(errno));
        close(src_fd);
        return -1;
    }

    unsigned char *buffer = malloc(CHUNK_SIZE);
    if (!buffer) {
        close(src_fd);
        close(dst_fd);
        unlink(staging_path);
        return -1;
    }

    ssize_t bytes_read;
    while ((bytes_read = read(src_fd, buffer, CHUNK_SIZE)) > 0) {
        ssize_t bytes_written = 0;
        while (bytes_written < bytes_read) {
            ssize_t n = write(dst_fd, buffer + bytes_written, (size_t)(bytes_read - bytes_written));
            if (n < 0) {
                if (errno == EINTR) continue;
                free(buffer);
                close(src_fd);
                close(dst_fd);
                unlink(staging_path);
                return -1;
            }
            bytes_written += n;
        }
    }

    free(buffer);
    close(src_fd);
    fsync(dst_fd);
    close(dst_fd);

    /* Atomic POSIX replacement */
    if (rename(staging_path, dst_path) != 0) {
        LOG_ERROR("REPO", "Atomic rename failed from '%s' to '%s': %s",
                  staging_path, dst_path, strerror(errno));
        unlink(staging_path);
        return -1;
    }

    return 0;
}

int repo_engine_init(repo_engine_t *engine, const char *local_dir, const char *remote_dir) {
    if (!engine || !local_dir || !remote_dir) return -1;

    strncpy(engine->local_dir, local_dir, sizeof(engine->local_dir) - 1);
    engine->local_dir[sizeof(engine->local_dir) - 1] = '\0';

    strncpy(engine->remote_dir, remote_dir, sizeof(engine->remote_dir) - 1);
    engine->remote_dir[sizeof(engine->remote_dir) - 1] = '\0';

    engine->active_state = REPO_STATE_SYNCED;
    engine->active_progress = 0;
    engine->current_file[0] = '\0';

    pthread_mutex_init(&engine->lock, NULL);

    ensure_dir_exists(engine->local_dir);
    ensure_dir_exists(engine->remote_dir);

    return 0;
}

int repo_engine_get_status(repo_engine_t *engine, repo_diff_t *out_diff) {
    if (!engine || !out_diff) return -1;

    pthread_mutex_lock(&engine->lock);

    if (engine->active_state == REPO_STATE_SYNCING) {
        memset(out_diff, 0, sizeof(repo_diff_t));
        out_diff->state = REPO_STATE_SYNCING;
        out_diff->progress_pct = engine->active_progress;
        snprintf(out_diff->status_message, sizeof(out_diff->status_message),
                 "Syncing in progress (%d%%): %.180s",
                 engine->active_progress, engine->current_file);
        pthread_mutex_unlock(&engine->lock);
        return 0;
    }

    int res = repo_compute_diff(engine->local_dir, engine->remote_dir, out_diff);
    engine->active_state = out_diff->state;

    pthread_mutex_unlock(&engine->lock);
    return res;
}

int repo_engine_push(repo_engine_t *engine) {
    if (!engine) return -1;

    pthread_mutex_lock(&engine->lock);
    engine->active_state = REPO_STATE_SYNCING;
    engine->active_progress = 0;

    repo_diff_t diff;
    repo_compute_diff(engine->local_dir, engine->remote_dir, &diff);

    if (diff.pending_push_count == 0) {
        engine->active_state = REPO_STATE_SYNCED;
        engine->active_progress = 100;
        pthread_mutex_unlock(&engine->lock);
        return 0;
    }

    LOG_INFO("REPO", "Starting PUSH: %zu file(s) to synchronize to Remote Repo...", diff.pending_push_count);

    for (size_t i = 0; i < diff.pending_push_count; i++) {
        const char *rel = diff.push_files[i];
        strncpy(engine->current_file, rel, sizeof(engine->current_file) - 1);
        engine->active_progress = (int)((i * 100) / diff.pending_push_count);

        char src_full[MINIDROP_MAX_PATH_LEN * 2];
        char dst_full[MINIDROP_MAX_PATH_LEN * 2];
        snprintf(src_full, sizeof(src_full), "%s/%s", engine->local_dir, rel);
        snprintf(dst_full, sizeof(dst_full), "%s/%s", engine->remote_dir, rel);

        LOG_INFO("REPO", "Pushing '%s' (SHA-256 chunked transfer)...", rel);
        if (copy_file_chunked_atomic(src_full, dst_full) != 0) {
            LOG_ERROR("REPO", "Failed to push file '%s'", rel);
            engine->active_state = REPO_STATE_ERROR;
            pthread_mutex_unlock(&engine->lock);
            return -1;
        }
    }

    engine->active_progress = 100;
    engine->active_state = REPO_STATE_SYNCED;
    engine->current_file[0] = '\0';
    LOG_INFO("REPO", "PUSH completed successfully! All files are in sync.");

    pthread_mutex_unlock(&engine->lock);
    return 0;
}

int repo_engine_pull(repo_engine_t *engine) {
    if (!engine) return -1;

    pthread_mutex_lock(&engine->lock);
    engine->active_state = REPO_STATE_SYNCING;
    engine->active_progress = 0;

    repo_diff_t diff;
    repo_compute_diff(engine->local_dir, engine->remote_dir, &diff);

    if (diff.pending_pull_count == 0) {
        engine->active_state = REPO_STATE_SYNCED;
        engine->active_progress = 100;
        pthread_mutex_unlock(&engine->lock);
        return 0;
    }

    LOG_INFO("REPO", "Starting PULL: %zu file(s) downloading from Remote Repo...", diff.pending_pull_count);

    for (size_t i = 0; i < diff.pending_pull_count; i++) {
        const char *rel = diff.pull_files[i];
        strncpy(engine->current_file, rel, sizeof(engine->current_file) - 1);
        engine->active_progress = (int)((i * 100) / diff.pending_pull_count);

        char src_full[MINIDROP_MAX_PATH_LEN * 2];
        char dst_full[MINIDROP_MAX_PATH_LEN * 2];
        snprintf(src_full, sizeof(src_full), "%s/%s", engine->remote_dir, rel);
        snprintf(dst_full, sizeof(dst_full), "%s/%s", engine->local_dir, rel);

        LOG_INFO("REPO", "Pulling '%s'...", rel);
        if (copy_file_chunked_atomic(src_full, dst_full) != 0) {
            LOG_ERROR("REPO", "Failed to pull file '%s'", rel);
            engine->active_state = REPO_STATE_ERROR;
            pthread_mutex_unlock(&engine->lock);
            return -1;
        }
    }

    engine->active_progress = 100;
    engine->active_state = REPO_STATE_SYNCED;
    engine->current_file[0] = '\0';
    LOG_INFO("REPO", "PULL completed successfully! Local workspace updated.");

    pthread_mutex_unlock(&engine->lock);
    return 0;
}
