/**
 * @file metadata.h
 * @brief Directory traversal, metadata manifest inventory, diff engine, and path sanitization.
 */

#ifndef MINIDROP_METADATA_H
#define MINIDROP_METADATA_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "protocol.h"
#include "checksum.h"

typedef struct {
    char rel_path[MINIDROP_MAX_PATH_LEN];
    uint64_t size;
    uint64_t mtime;
    char sha256[SHA256_HEX_LEN + 1];
    int is_dir;
} sync_entry_t;

typedef struct {
    sync_entry_t *entries;
    size_t count;
    size_t capacity;
} sync_manifest_t;

/**
 * @brief High-level repository sync state enum for the Smart Button.
 */
typedef enum {
    REPO_STATE_SYNCED = 0,     /* Local and remote are identical */
    REPO_STATE_NEEDS_PUSH,     /* Local has new or modified files */
    REPO_STATE_NEEDS_PULL,     /* Remote repo has new or modified files */
    REPO_STATE_SYNCING,        /* File transfer in progress */
    REPO_STATE_ERROR           /* Storage or network error */
} repo_state_t;

/**
 * @brief Diff delta representation between local workspace and remote repository.
 */
typedef struct {
    repo_state_t state;
    size_t pending_push_count;
    size_t pending_pull_count;
    size_t total_local_files;
    size_t total_remote_files;
    char push_files[64][MINIDROP_MAX_PATH_LEN];
    char pull_files[64][MINIDROP_MAX_PATH_LEN];
    int progress_pct;
    char status_message[256];
} repo_diff_t;

/**
 * @brief Initialize an empty manifest container.
 */
void sync_manifest_init(sync_manifest_t *manifest);

/**
 * @brief Free all memory associated with the manifest.
 */
void sync_manifest_free(sync_manifest_t *manifest);

/**
 * @brief Add an entry to the manifest, dynamically resizing buffer if needed.
 */
int sync_manifest_add(sync_manifest_t *manifest, const sync_entry_t *entry);

/**
 * @brief Recursively scan a directory tree and construct a full manifest inventory.
 * @param root_path Absolute or relative base directory.
 * @param manifest Output manifest container.
 * @return Number of entries scanned, or -1 on error.
 */
int sync_manifest_scan(const char *root_path, sync_manifest_t *manifest);

/**
 * @brief Find an entry by relative path in the manifest.
 * @return Pointer to entry, or NULL if not found.
 */
const sync_entry_t *sync_manifest_find(const sync_manifest_t *manifest, const char *rel_path);

/**
 * @brief Compare local workspace vs remote repository and compute delta diff.
 * Determines whether the repository is SYNCED, NEEDS_PUSH, or NEEDS_PULL.
 */
int repo_compute_diff(const char *local_dir, const char *remote_dir, repo_diff_t *out_diff);

/**
 * @brief Convert repo_state_t to human-readable string.
 */
const char *repo_state_to_string(repo_state_t state);

/**
 * @brief Sanitize a relative path to prevent directory traversal attacks (e.g. "../").
 */
int sanitize_rel_path(const char *input, char *output, size_t max_len);

#endif /* MINIDROP_METADATA_H */
