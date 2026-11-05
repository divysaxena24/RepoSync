/**
 * @file storage.h
 * @brief Thread-safe sandboxed repository storage and atomic file commit operations.
 */

#ifndef MINIDROP_STORAGE_H
#define MINIDROP_STORAGE_H

#include <stdint.h>
#include <pthread.h>
#include "protocol.h"
#include "metadata.h"

/**
 * @brief Initialize the storage subsystem.
 */
int storage_init(const char *storage_root);

/**
 * @brief Cleanup storage subsystem.
 */
void storage_destroy(void);

/**
 * @brief Ensure client root and any nested directory exists.
 */
int storage_ensure_parent_dirs(const char *full_filepath);

/**
 * @brief Build absolute sandboxed path for a client repository item.
 * Validates path traversal.
 * @return 0 on success, -1 on path traversal violation or invalid path.
 */
int storage_get_sanitized_path(const char *storage_root, const char *client_id,
                               const char *rel_path, char *out_full_path, size_t max_len);

/**
 * @brief Start a staged file upload. Opens .<filename>.tmp.<req_id>
 * @return File descriptor of open staging file, or -1 on error.
 */
int storage_stage_file_start(const char *storage_root, const char *client_id,
                             const char *rel_path, uint32_t req_id, char *out_staging_path);

/**
 * @brief Append a chunk of data to an open staging file.
 */
int storage_stage_file_write_chunk(int fd, const void *buf, size_t len);

/**
 * @brief Finalize and commit a staged file.
 * Verifies SHA-256 against expected hash.
 * If verified, atomically renames staging file to final destination.
 * Detects conflicts and preserves conflict copies if necessary.
 * @return STATUS_OK on success, STATUS_ERR_CHECKSUM or error status code.
 */
status_code_t storage_stage_file_commit(int fd, const char *staging_path,
                                        const char *storage_root, const char *client_id,
                                        const char *rel_path, const char *expected_sha256,
                                        char *out_conflict_path);

/**
 * @brief Delete a regular file inside the client repository.
 */
status_code_t storage_delete_file(const char *storage_root, const char *client_id,
                                  const char *rel_path);

/**
 * @brief Rename a file or directory atomically within client repository.
 */
status_code_t storage_rename(const char *storage_root, const char *client_id,
                             const char *old_rel_path, const char *new_rel_path);

/**
 * @brief Create a nested directory in the client repository.
 */
status_code_t storage_create_dir(const char *storage_root, const char *client_id,
                                 const char *rel_path);

/**
 * @brief Delete a directory in the client repository.
 */
status_code_t storage_delete_dir(const char *storage_root, const char *client_id,
                                 const char *rel_path);

#endif /* MINIDROP_STORAGE_H */

