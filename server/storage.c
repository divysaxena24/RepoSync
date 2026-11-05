/**
 * @file storage.c
 * @brief Thread-safe sandboxed repository storage implementation.
 */

#include "storage.h"
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
#include <time.h>

static pthread_mutex_t g_storage_mutex = PTHREAD_MUTEX_INITIALIZER;

int storage_init(const char *storage_root) {
    pthread_mutex_lock(&g_storage_mutex);
    struct stat st;
    if (stat(storage_root, &st) != 0) {
        if (mkdir(storage_root, 0755) != 0 && errno != EEXIST) {
            LOG_ERROR("STORAGE", "Failed to create root storage directory '%s': %s",
                      storage_root, strerror(errno));
            pthread_mutex_unlock(&g_storage_mutex);
            return -1;
        }
    }
    pthread_mutex_unlock(&g_storage_mutex);
    return 0;
}

void storage_destroy(void) {
    /* No persistent handles to clean */
}

int storage_ensure_parent_dirs(const char *full_filepath) {
    if (!full_filepath) return -1;

    char temp[MINIDROP_MAX_PATH_LEN * 2];
    strncpy(temp, full_filepath, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *dir = dirname(temp);
    if (!dir || strcmp(dir, ".") == 0 || strcmp(dir, "/") == 0) {
        return 0;
    }

    /* Iterate through segments and create directory tree */
    char path_buf[MINIDROP_MAX_PATH_LEN * 2];
    path_buf[0] = '\0';

    char *p = dir;
    if (*p == '/') {
        strcat(path_buf, "/");
        p++;
    }

    char *token = strtok(p, "/");
    while (token != NULL) {
        strcat(path_buf, token);
        struct stat st;
        if (stat(path_buf, &st) != 0) {
            if (mkdir(path_buf, 0755) != 0 && errno != EEXIST) {
                LOG_ERROR("STORAGE", "mkdir failed for '%s': %s", path_buf, strerror(errno));
                return -1;
            }
        }
        strcat(path_buf, "/");
        token = strtok(NULL, "/");
    }

    return 0;
}

int storage_get_sanitized_path(const char *storage_root, const char *client_id,
                               const char *rel_path, char *out_full_path, size_t max_len) {
    if (!storage_root || !client_id || !rel_path || !out_full_path) {
        return -1;
    }

    char clean_rel[MINIDROP_MAX_PATH_LEN];
    if (sanitize_rel_path(rel_path, clean_rel, sizeof(clean_rel)) != 0) {
        LOG_WARN("STORAGE", "Rejected illegal path traversal attempt: '%s'", rel_path);
        return -1;
    }

    if (clean_rel[0] == '\0') {
        snprintf(out_full_path, max_len, "%s/%s", storage_root, client_id);
    } else {
        snprintf(out_full_path, max_len, "%s/%s/%s", storage_root, client_id, clean_rel);
    }

    return 0;
}

int storage_stage_file_start(const char *storage_root, const char *client_id,
                             const char *rel_path, uint32_t req_id, char *out_staging_path) {
    char target_full_path[MINIDROP_MAX_PATH_LEN * 2];
    if (storage_get_sanitized_path(storage_root, client_id, rel_path,
                                   target_full_path, sizeof(target_full_path)) != 0) {
        return -1;
    }

    pthread_mutex_lock(&g_storage_mutex);
    if (storage_ensure_parent_dirs(target_full_path) != 0) {
        pthread_mutex_unlock(&g_storage_mutex);
        return -1;
    }
    pthread_mutex_unlock(&g_storage_mutex);

    char dir_copy[MINIDROP_MAX_PATH_LEN * 2];
    char base_copy[MINIDROP_MAX_PATH_LEN * 2];
    strncpy(dir_copy, target_full_path, sizeof(dir_copy) - 1);
    strncpy(base_copy, target_full_path, sizeof(base_copy) - 1);
    dir_copy[sizeof(dir_copy) - 1] = '\0';
    base_copy[sizeof(base_copy) - 1] = '\0';

    char *dir_name = dirname(dir_copy);
    char *base_name = basename(base_copy);

    snprintf(out_staging_path, MINIDROP_MAX_PATH_LEN * 2, "%s/.%s.tmp.%u",
             dir_name, base_name, req_id);

    int fd = open(out_staging_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        LOG_ERROR("STORAGE", "Failed to create staging file '%s': %s",
                  out_staging_path, strerror(errno));
        return -1;
    }

    return fd;
}

int storage_stage_file_write_chunk(int fd, const void *buf, size_t len) {
    if (fd < 0 || !buf) return -1;
    size_t written = 0;
    const char *ptr = (const char *)buf;

    while (written < len) {
        ssize_t n = write(fd, ptr + written, len - written);
        if (n > 0) {
            written += (size_t)n;
        } else if (n < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("STORAGE", "write() to staging file failed: %s", strerror(errno));
            return -1;
        } else {
            return -1;
        }
    }
    return 0;
}

status_code_t storage_stage_file_commit(int fd, const char *staging_path,
                                        const char *storage_root, const char *client_id,
                                        const char *rel_path, const char *expected_sha256,
                                        char *out_conflict_path) {
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }

    if (out_conflict_path) {
        out_conflict_path[0] = '\0';
    }

    /* 1. Verify Checksum */
    char computed_hash[SHA256_HEX_LEN + 1];
    if (checksum_calculate_file(staging_path, computed_hash) != 0) {
        LOG_ERROR("STORAGE", "Failed to compute checksum of staged file '%s'", staging_path);
        unlink(staging_path);
        return STATUS_ERR_IO;
    }

    if (expected_sha256 && strlen(expected_sha256) > 0) {
        if (strcasecmp(computed_hash, expected_sha256) != 0) {
            LOG_ERROR("STORAGE", "Checksum mismatch for '%s'! Expected: %s, Computed: %s",
                      rel_path, expected_sha256, computed_hash);
            unlink(staging_path);
            return STATUS_ERR_CHECKSUM;
        }
    }

    char target_full_path[MINIDROP_MAX_PATH_LEN * 2];
    if (storage_get_sanitized_path(storage_root, client_id, rel_path,
                                   target_full_path, sizeof(target_full_path)) != 0) {
        unlink(staging_path);
        return STATUS_ERR_INVALID_PATH;
    }

    pthread_mutex_lock(&g_storage_mutex);

    /* 2. Check for conflict: If file exists and differs from both base and staging */
    struct stat st;
    if (stat(target_full_path, &st) == 0) {
        char existing_hash[SHA256_HEX_LEN + 1];
        if (checksum_calculate_file(target_full_path, existing_hash) == 0) {
            if (strcmp(existing_hash, computed_hash) != 0) {
                /* Existing file is different! Branch into conflict copy */
                time_t now = time(NULL);
                char conflict_full[MINIDROP_MAX_PATH_LEN * 3];
                snprintf(conflict_full, sizeof(conflict_full), "%s.conflict_%s_%ld",
                         target_full_path, client_id, (long)now);

                LOG_WARN("CONFLICT", "File conflict detected for '%s'! Preserving conflict copy: %s",
                         rel_path, conflict_full);

                if (rename(target_full_path, conflict_full) == 0) {
                    if (out_conflict_path) {
                        snprintf(out_conflict_path, MINIDROP_MAX_PATH_LEN, "%s.conflict_%s_%ld",
                                 rel_path, client_id, (long)now);
                    }
                }
            }
        }
    }

    /* 3. Atomic commit: POSIX rename replaces target atomically */
    if (rename(staging_path, target_full_path) != 0) {
        LOG_ERROR("STORAGE", "Atomic rename failed from '%s' to '%s': %s",
                  staging_path, target_full_path, strerror(errno));
        unlink(staging_path);
        pthread_mutex_unlock(&g_storage_mutex);
        return STATUS_ERR_IO;
    }

    pthread_mutex_unlock(&g_storage_mutex);
    LOG_INFO("STORAGE", "Atomically published '%s' (SHA-256: %s)", rel_path, computed_hash);
    return STATUS_OK;
}

status_code_t storage_delete_file(const char *storage_root, const char *client_id,
                                  const char *rel_path) {
    char target_full_path[MINIDROP_MAX_PATH_LEN * 2];
    if (storage_get_sanitized_path(storage_root, client_id, rel_path,
                                   target_full_path, sizeof(target_full_path)) != 0) {
        return STATUS_ERR_INVALID_PATH;
    }

    pthread_mutex_lock(&g_storage_mutex);
    if (unlink(target_full_path) != 0) {
        if (errno == ENOENT) {
            pthread_mutex_unlock(&g_storage_mutex);
            return STATUS_ERR_NOT_FOUND;
        }
        LOG_ERROR("STORAGE", "Failed to unlink '%s': %s", target_full_path, strerror(errno));
        pthread_mutex_unlock(&g_storage_mutex);
        return STATUS_ERR_IO;
    }
    pthread_mutex_unlock(&g_storage_mutex);

    LOG_INFO("STORAGE", "Deleted remote file: '%s'", rel_path);
    return STATUS_OK;
}

status_code_t storage_rename(const char *storage_root, const char *client_id,
                             const char *old_rel_path, const char *new_rel_path) {
    char old_full[MINIDROP_MAX_PATH_LEN * 2];
    char new_full[MINIDROP_MAX_PATH_LEN * 2];

    if (storage_get_sanitized_path(storage_root, client_id, old_rel_path, old_full, sizeof(old_full)) != 0 ||
        storage_get_sanitized_path(storage_root, client_id, new_rel_path, new_full, sizeof(new_full)) != 0) {
        return STATUS_ERR_INVALID_PATH;
    }

    pthread_mutex_lock(&g_storage_mutex);
    storage_ensure_parent_dirs(new_full);

    if (rename(old_full, new_full) != 0) {
        LOG_ERROR("STORAGE", "rename failed from '%s' to '%s': %s",
                  old_full, new_full, strerror(errno));
        pthread_mutex_unlock(&g_storage_mutex);
        return STATUS_ERR_IO;
    }
    pthread_mutex_unlock(&g_storage_mutex);

    LOG_INFO("STORAGE", "Renamed '%s' -> '%s'", old_rel_path, new_rel_path);
    return STATUS_OK;
}

status_code_t storage_create_dir(const char *storage_root, const char *client_id,
                                 const char *rel_path) {
    char target_full_path[MINIDROP_MAX_PATH_LEN * 2];
    if (storage_get_sanitized_path(storage_root, client_id, rel_path,
                                   target_full_path, sizeof(target_full_path)) != 0) {
        return STATUS_ERR_INVALID_PATH;
    }

    pthread_mutex_lock(&g_storage_mutex);
    storage_ensure_parent_dirs(target_full_path);

    struct stat st;
    if (stat(target_full_path, &st) == 0) {
        pthread_mutex_unlock(&g_storage_mutex);
        return STATUS_OK; /* Directory already exists */
    }

    if (mkdir(target_full_path, 0755) != 0 && errno != EEXIST) {
        LOG_ERROR("STORAGE", "mkdir failed for '%s': %s", target_full_path, strerror(errno));
        pthread_mutex_unlock(&g_storage_mutex);
        return STATUS_ERR_IO;
    }
    pthread_mutex_unlock(&g_storage_mutex);

    LOG_INFO("STORAGE", "Created remote directory: '%s'", rel_path);
    return STATUS_OK;
}

status_code_t storage_delete_dir(const char *storage_root, const char *client_id,
                                 const char *rel_path) {
    char target_full_path[MINIDROP_MAX_PATH_LEN * 2];
    if (storage_get_sanitized_path(storage_root, client_id, rel_path,
                                   target_full_path, sizeof(target_full_path)) != 0) {
        return STATUS_ERR_INVALID_PATH;
    }

    pthread_mutex_lock(&g_storage_mutex);
    if (rmdir(target_full_path) != 0) {
        if (errno == ENOENT) {
            pthread_mutex_unlock(&g_storage_mutex);
            return STATUS_ERR_NOT_FOUND;
        }
        LOG_ERROR("STORAGE", "rmdir failed for '%s': %s", target_full_path, strerror(errno));
        pthread_mutex_unlock(&g_storage_mutex);
        return STATUS_ERR_IO;
    }
    pthread_mutex_unlock(&g_storage_mutex);

    LOG_INFO("STORAGE", "Deleted remote directory: '%s'", rel_path);
    return STATUS_OK;
}
