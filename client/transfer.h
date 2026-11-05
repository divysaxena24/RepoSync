/**
 * @file transfer.h
 * @brief Client-side chunked file upload and synchronization network transfer routines.
 */

#ifndef MINIDROP_TRANSFER_H
#define MINIDROP_TRANSFER_H

#include <stdint.h>
#include "protocol.h"

/**
 * @brief Upload a file in 64 KB chunks with SHA-256 integrity verification.
 * @return 0 on success, -1 on failure.
 */
int transfer_upload_file(int sockfd, const char *sync_dir, const char *rel_path,
                         const char *client_id);

/**
 * @brief Notify server to delete a remote file.
 */
int transfer_delete_file(int sockfd, const char *rel_path, const char *client_id);

/**
 * @brief Notify server to rename a remote file or folder.
 */
int transfer_rename(int sockfd, const char *old_rel_path, const char *new_rel_path,
                    const char *client_id);

/**
 * @brief Notify server to create a remote directory.
 */
int transfer_create_dir(int sockfd, const char *rel_path, const char *client_id);

/**
 * @brief Notify server to delete a remote directory.
 */
int transfer_delete_dir(int sockfd, const char *rel_path, const char *client_id);

#endif /* MINIDROP_TRANSFER_H */

