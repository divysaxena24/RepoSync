/**
 * @file metadata.c
 * @brief Directory traversal, metadata manifest inventory, diff engine, and path sanitization implementation.
 */

#include "metadata.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

#define INITIAL_MANIFEST_CAPACITY 64

void sync_manifest_init(sync_manifest_t *manifest) {
    if (!manifest) return;
    manifest->entries = NULL;
    manifest->count = 0;
    manifest->capacity = 0;
}

void sync_manifest_free(sync_manifest_t *manifest) {
    if (!manifest) return;
    if (manifest->entries) {
        free(manifest->entries);
        manifest->entries = NULL;
    }
    manifest->count = 0;
    manifest->capacity = 0;
}

int sync_manifest_add(sync_manifest_t *manifest, const sync_entry_t *entry) {
    if (!manifest || !entry) return -1;

    if (manifest->count >= manifest->capacity) {
        size_t new_cap = (manifest->capacity == 0) ? INITIAL_MANIFEST_CAPACITY : manifest->capacity * 2;
        sync_entry_t *new_entries = realloc(manifest->entries, new_cap * sizeof(sync_entry_t));
        if (!new_entries) {
            LOG_ERROR("METADATA", "Failed to allocate memory for manifest");
            return -1;
        }
        manifest->entries = new_entries;
        manifest->capacity = new_cap;
    }

    manifest->entries[manifest->count++] = *entry;
    return 0;
}

const sync_entry_t *sync_manifest_find(const sync_manifest_t *manifest, const char *rel_path) {
    if (!manifest || !rel_path) return NULL;
    for (size_t i = 0; i < manifest->count; i++) {
        if (strcmp(manifest->entries[i].rel_path, rel_path) == 0) {
            return &manifest->entries[i];
        }
    }
    return NULL;
}

int sanitize_rel_path(const char *input, char *output, size_t max_len) {
    if (!input || !output || max_len == 0) return -1;

    /* Skip leading slashes */
    while (*input == '/' || *input == '\\') {
        input++;
    }

    if (*input == '\0') {
        output[0] = '\0';
        return 0;
    }

    char temp[MINIDROP_MAX_PATH_LEN];
    strncpy(temp, input, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *segments[256];
    int depth = 0;

    char *token = strtok(temp, "/\\");
    while (token != NULL) {
        if (strcmp(token, ".") == 0 || strlen(token) == 0) {
            /* Current directory, ignore */
        } else if (strcmp(token, "..") == 0) {
            if (depth > 0) {
                depth--;
            } else {
                LOG_WARN("SECURITY", "Path traversal attack detected: '%s'", input);
                return -1;
            }
        } else {
            if (depth < 256) {
                segments[depth++] = token;
            } else {
                return -1;
            }
        }
        token = strtok(NULL, "/\\");
    }

    output[0] = '\0';
    size_t curr_len = 0;
    for (int i = 0; i < depth; i++) {
        size_t seg_len = strlen(segments[i]);
        if (curr_len + seg_len + 2 >= max_len) {
            return -1;
        }
        if (i > 0) {
            strcat(output, "/");
            curr_len++;
        }
        strcat(output, segments[i]);
        curr_len += seg_len;
    }

    return 0;
}

static int scan_dir_recursive(const char *root_base, const char *sub_dir, sync_manifest_t *manifest) {
    char current_full_dir[MINIDROP_MAX_PATH_LEN * 2];
    if (sub_dir && strlen(sub_dir) > 0) {
        snprintf(current_full_dir, sizeof(current_full_dir), "%s/%s", root_base, sub_dir);
    } else {
        snprintf(current_full_dir, sizeof(current_full_dir), "%s", root_base);
    }

    DIR *dir = opendir(current_full_dir);
    if (!dir) {
        return 0; /* Missing directory is simply empty */
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (entry->d_name[0] == '.') {
            continue; /* Ignore hidden & temp files */
        }

        char child_rel_path[MINIDROP_MAX_PATH_LEN];
        if (sub_dir && strlen(sub_dir) > 0) {
            snprintf(child_rel_path, sizeof(child_rel_path), "%s/%s", sub_dir, entry->d_name);
        } else {
            snprintf(child_rel_path, sizeof(child_rel_path), "%s", entry->d_name);
        }

        char child_full_path[MINIDROP_MAX_PATH_LEN * 2];
        snprintf(child_full_path, sizeof(child_full_path), "%s/%s", root_base, child_rel_path);

        struct stat st;
        if (lstat(child_full_path, &st) != 0) {
            continue;
        }

        sync_entry_t sync_item;
        memset(&sync_item, 0, sizeof(sync_item));
        strncpy(sync_item.rel_path, child_rel_path, sizeof(sync_item.rel_path) - 1);
        sync_item.size = (uint64_t)st.st_size;
        sync_item.mtime = (uint64_t)st.st_mtime;

        if (S_ISDIR(st.st_mode)) {
            sync_item.is_dir = 1;
            sync_manifest_add(manifest, &sync_item);
            scan_dir_recursive(root_base, child_rel_path, manifest);
        } else if (S_ISREG(st.st_mode)) {
            sync_item.is_dir = 0;
            if (checksum_calculate_file(child_full_path, sync_item.sha256) == 0) {
                sync_manifest_add(manifest, &sync_item);
            }
        }
    }

    closedir(dir);
    return 0;
}

int sync_manifest_scan(const char *root_path, sync_manifest_t *manifest) {
    if (!root_path || !manifest) return -1;
    sync_manifest_free(manifest);
    sync_manifest_init(manifest);

    if (scan_dir_recursive(root_path, "", manifest) != 0) {
        return -1;
    }
    return (int)manifest->count;
}

int repo_compute_diff(const char *local_dir, const char *remote_dir, repo_diff_t *out_diff) {
    if (!local_dir || !remote_dir || !out_diff) return -1;

    memset(out_diff, 0, sizeof(repo_diff_t));
    out_diff->state = REPO_STATE_SYNCED;

    sync_manifest_t local_man;
    sync_manifest_t remote_man;

    sync_manifest_init(&local_man);
    sync_manifest_init(&remote_man);

    sync_manifest_scan(local_dir, &local_man);
    sync_manifest_scan(remote_dir, &remote_man);

    out_diff->total_local_files = local_man.count;
    out_diff->total_remote_files = remote_man.count;

    /* 1. Identify files in Local that are missing or modified compared to Remote */
    for (size_t i = 0; i < local_man.count; i++) {
        const sync_entry_t *loc = &local_man.entries[i];
        if (loc->is_dir) continue; /* Focus on file payloads */

        const sync_entry_t *rem = sync_manifest_find(&remote_man, loc->rel_path);
        if (!rem || strcmp(loc->sha256, rem->sha256) != 0) {
            if (out_diff->pending_push_count < 64) {
                strncpy(out_diff->push_files[out_diff->pending_push_count],
                        loc->rel_path, MINIDROP_MAX_PATH_LEN - 1);
            }
            out_diff->pending_push_count++;
        }
    }

    /* 2. Identify files in Remote that are missing in Local */
    for (size_t i = 0; i < remote_man.count; i++) {
        const sync_entry_t *rem = &remote_man.entries[i];
        if (rem->is_dir) continue;

        const sync_entry_t *loc = sync_manifest_find(&local_man, rem->rel_path);
        if (!loc) {
            if (out_diff->pending_pull_count < 64) {
                strncpy(out_diff->pull_files[out_diff->pending_pull_count],
                        rem->rel_path, MINIDROP_MAX_PATH_LEN - 1);
            }
            out_diff->pending_pull_count++;
        }
    }

    /* 3. Determine Overall State */
    if (out_diff->pending_push_count > 0) {
        out_diff->state = REPO_STATE_NEEDS_PUSH;
        snprintf(out_diff->status_message, sizeof(out_diff->status_message),
                 "%zu file(s) modified or new. Push required.", out_diff->pending_push_count);
    } else if (out_diff->pending_pull_count > 0) {
        out_diff->state = REPO_STATE_NEEDS_PULL;
        snprintf(out_diff->status_message, sizeof(out_diff->status_message),
                 "%zu file(s) updated on remote. Pull available.", out_diff->pending_pull_count);
    } else {
        out_diff->state = REPO_STATE_SYNCED;
        snprintf(out_diff->status_message, sizeof(out_diff->status_message),
                 "Repository is up to date (All %zu files synchronized).", out_diff->total_local_files);
    }

    sync_manifest_free(&local_man);
    sync_manifest_free(&remote_man);
    return 0;
}

const char *repo_state_to_string(repo_state_t state) {
    switch (state) {
        case REPO_STATE_SYNCED:     return "SYNCED";
        case REPO_STATE_NEEDS_PUSH: return "NEEDS_PUSH";
        case REPO_STATE_NEEDS_PULL: return "NEEDS_PULL";
        case REPO_STATE_SYNCING:    return "SYNCING";
        case REPO_STATE_ERROR:      return "ERROR";
        default:                    return "UNKNOWN";
    }
}
