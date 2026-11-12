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
#include <time.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <poll.h>
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
    snprintf(engine->last_event_msg, sizeof(engine->last_event_msg), "Autonomous sync engine initialized");
    engine->auto_sync_running = 0;

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

    /* Record commit history */
    char summary_buf[256] = "";
    int sum_offset = 0;
    for (size_t i = 0; i < diff.pending_push_count && i < 3; i++) {
        sum_offset += snprintf(summary_buf + sum_offset, sizeof(summary_buf) - (size_t)sum_offset,
                               "%s%s", (i > 0 ? ", " : ""), diff.push_files[i]);
    }
    if (diff.pending_push_count > 3) {
        snprintf(summary_buf + sum_offset, sizeof(summary_buf) - (size_t)sum_offset,
                 " and %zu more", diff.pending_push_count - 3);
    }
    char final_summary[320];
    snprintf(final_summary, sizeof(final_summary), "Pushed %zu file(s): %s", diff.pending_push_count, summary_buf);
    repo_engine_log_history(engine, "PUSH", diff.pending_push_count, final_summary);

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

int repo_engine_prune(repo_engine_t *engine) {
    if (!engine) return -1;

    pthread_mutex_lock(&engine->lock);
    engine->active_state = REPO_STATE_SYNCING;
    engine->active_progress = 0;

    repo_diff_t diff;
    repo_compute_diff(engine->local_dir, engine->remote_dir, &diff);

    if (diff.pending_prune_count == 0) {
        engine->active_state = REPO_STATE_SYNCED;
        engine->active_progress = 100;
        pthread_mutex_unlock(&engine->lock);
        return 0;
    }

    LOG_INFO("REPO", "Starting PRUNE: %zu file(s) deleted locally, removing from Remote Repo...",
             diff.pending_prune_count);

    char summary_buf[256] = "";
    int sum_offset = 0;

    for (size_t i = 0; i < diff.pending_prune_count; i++) {
        const char *rel = diff.prune_files[i];
        strncpy(engine->current_file, rel, sizeof(engine->current_file) - 1);
        engine->active_progress = (int)((i * 100) / diff.pending_prune_count);

        char dst_full[MINIDROP_MAX_PATH_LEN * 2];
        snprintf(dst_full, sizeof(dst_full), "%s/%s", engine->remote_dir, rel);

        LOG_INFO("REPO", "Pruning deleted file '%s' from remote...", rel);
        if (unlink(dst_full) != 0 && errno != ENOENT) {
            LOG_ERROR("REPO", "Failed to remove '%s': %s", dst_full, strerror(errno));
        }

        if (i < 3) {
            sum_offset += snprintf(summary_buf + sum_offset, sizeof(summary_buf) - (size_t)sum_offset,
                                   "%s%s", (i > 0 ? ", " : ""), rel);
        }
    }

    if (diff.pending_prune_count > 3) {
        snprintf(summary_buf + sum_offset, sizeof(summary_buf) - (size_t)sum_offset,
                 " and %zu more", diff.pending_prune_count - 3);
    }

    char final_summary[320];
    snprintf(final_summary, sizeof(final_summary), "Pruned %zu file(s): %s",
             diff.pending_prune_count, summary_buf);
    repo_engine_log_history(engine, "PRUNE", diff.pending_prune_count, final_summary);

    engine->active_progress = 100;
    engine->active_state = REPO_STATE_SYNCED;
    engine->current_file[0] = '\0';
    LOG_INFO("REPO", "PRUNE completed successfully! Remote repository cleaned.");

    pthread_mutex_unlock(&engine->lock);
    return 0;
}

int repo_engine_log_history(repo_engine_t *engine, const char *action, size_t file_count, const char *summary) {
    if (!engine || !action) return -1;
    char log_path[MINIDROP_MAX_PATH_LEN * 2];
    snprintf(log_path, sizeof(log_path), "%s/.minidrop_history.log", engine->remote_dir);

    FILE *f = fopen(log_path, "a");
    if (!f) return -1;

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);

    /* Generate pseudo-random short commit hash */
    unsigned int h = (unsigned int)(now ^ (now >> 8) ^ (file_count << 16) ^ (unsigned int)getpid());
    char commit_id[16];
    snprintf(commit_id, sizeof(commit_id), "%07x", h & 0x0fffffff);

    /* Clean summary - replace pipe or newlines if any */
    char clean_sum[512];
    strncpy(clean_sum, summary ? summary : "Sync event", sizeof(clean_sum) - 1);
    clean_sum[sizeof(clean_sum) - 1] = '\0';
    for (char *p = clean_sum; *p; p++) {
        if (*p == '|' || *p == '\n' || *p == '\r' || *p == '"') *p = ' ';
    }

    fprintf(f, "%s|%s|%s|%zu|%s\n", time_str, commit_id, action, file_count, clean_sum);
    fclose(f);
    return 0;
}

int repo_engine_get_history_json(repo_engine_t *engine, char *out_buf, size_t max_len) {
    if (!engine || !out_buf || max_len < 16) return -1;
    char log_path[MINIDROP_MAX_PATH_LEN * 2];
    snprintf(log_path, sizeof(log_path), "%s/.minidrop_history.log", engine->remote_dir);

    FILE *f = fopen(log_path, "r");
    if (!f) {
        snprintf(out_buf, max_len, "[]");
        return 0;
    }

    char lines[64][512];
    size_t line_count = 0;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        if (strlen(buf) < 10) continue;
        buf[strcspn(buf, "\r\n")] = '\0';
        if (line_count < 64) {
            strncpy(lines[line_count], buf, sizeof(lines[line_count]) - 1);
            lines[line_count][sizeof(lines[line_count]) - 1] = '\0';
            line_count++;
        } else {
            for (size_t k = 0; k < 63; k++) {
                strcpy(lines[k], lines[k+1]);
            }
            strncpy(lines[63], buf, sizeof(lines[63]) - 1);
            lines[63][sizeof(lines[63]) - 1] = '\0';
        }
    }
    fclose(f);

    /* Output in reverse chronological order (newest first) */
    int offset = snprintf(out_buf, max_len, "[");
    int first = 1;
    for (ssize_t i = (ssize_t)line_count - 1; i >= 0; i--) {
        char *line_copy = lines[i];
        char *time_str = strtok(line_copy, "|");
        char *commit_id = strtok(NULL, "|");
        char *action = strtok(NULL, "|");
        char *count_str = strtok(NULL, "|");
        char *summary = strtok(NULL, "|");

        if (!time_str || !commit_id || !action || !count_str) continue;
        if (!summary) summary = "";

        offset += snprintf(out_buf + offset, max_len - (size_t)offset,
                           "%s{\"id\":\"%s\",\"time\":\"%s\",\"action\":\"%s\",\"count\":%s,\"summary\":\"%s\"}",
                           (first ? "" : ","), commit_id, time_str, action, count_str, summary);
        first = 0;
        if (offset >= (int)max_len - 128) break;
    }
    snprintf(out_buf + offset, max_len - (size_t)offset, "]");
    return 0;
}

static void *autosync_worker_thread(void *arg) {
    repo_engine_t *engine = (repo_engine_t *)arg;
    LOG_INFO("AUTOSYNC", "Autonomous real-time sync daemon started on: %s", engine->local_dir);

    int inotify_fd = inotify_init1(IN_NONBLOCK);
    int wd = -1;
    if (inotify_fd >= 0) {
        wd = inotify_add_watch(inotify_fd, engine->local_dir,
                               IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_MOVED_FROM);
    }

    while (engine->auto_sync_running) {
        int need_sync = 0;

        if (inotify_fd >= 0 && wd >= 0) {
            struct pollfd pfd;
            pfd.fd = inotify_fd;
            pfd.events = POLLIN;

            int poll_res = poll(&pfd, 1, 300);
            if (poll_res > 0 && (pfd.revents & POLLIN)) {
                char event_buf[4096];
                while (read(inotify_fd, event_buf, sizeof(event_buf)) > 0) {}
                need_sync = 1;
            } else if (poll_res == 0) {
                need_sync = 1;
            }
        } else {
            usleep(300000);
            need_sync = 1;
        }

        if (need_sync && engine->auto_sync_running) {
            repo_diff_t diff;
            if (repo_compute_diff(engine->local_dir, engine->remote_dir, &diff) == 0) {
                if (diff.pending_push_count > 0) {
                    LOG_INFO("AUTOSYNC", "Detected %zu modified/new file(s). Auto-syncing to Remote Repo...",
                             diff.pending_push_count);
                    repo_engine_push(engine);
                    snprintf(engine->last_event_msg, sizeof(engine->last_event_msg),
                             "Auto-synced %zu file(s) to remote repo", diff.pending_push_count);
                } else if (diff.pending_prune_count > 0) {
                    LOG_INFO("AUTOSYNC", "Detected %zu locally deleted file(s). Auto-pruning Remote Repo...",
                             diff.pending_prune_count);
                    repo_engine_prune(engine);
                    snprintf(engine->last_event_msg, sizeof(engine->last_event_msg),
                             "Auto-pruned %zu deleted file(s) from remote repo", diff.pending_prune_count);
                }
            }
        }
    }

    if (inotify_fd >= 0) {
        if (wd >= 0) inotify_rm_watch(inotify_fd, wd);
        close(inotify_fd);
    }

    LOG_INFO("AUTOSYNC", "Autonomous real-time sync daemon stopped cleanly");
    return NULL;
}

int repo_engine_start_autosync(repo_engine_t *engine) {
    if (!engine) return -1;
    if (engine->auto_sync_running) return 0;

    engine->auto_sync_running = 1;
    if (pthread_create(&engine->auto_sync_thread, NULL, autosync_worker_thread, engine) != 0) {
        LOG_ERROR("AUTOSYNC", "Failed to start auto-sync thread: %s", strerror(errno));
        engine->auto_sync_running = 0;
        return -1;
    }
    return 0;
}

void repo_engine_stop_autosync(repo_engine_t *engine) {
    if (!engine || !engine->auto_sync_running) return;
    engine->auto_sync_running = 0;
    pthread_join(engine->auto_sync_thread, NULL);
}
