/**
 * @file transfer.c
 * @brief Client-side chunked file upload and synchronization network transfer implementation.
 */

#include "transfer.h"
#include "protocol.h"
#include "checksum.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

static uint32_t g_transfer_req_id = 100;

int transfer_upload_file(int sockfd, const char *sync_dir, const char *rel_path,
                         const char *client_id) {
    if (sockfd < 0 || !sync_dir || !rel_path || !client_id) return -1;

    char full_path[MINIDROP_MAX_PATH_LEN * 2];
    snprintf(full_path, sizeof(full_path), "%s/%s", sync_dir, rel_path);

    struct stat st;
    if (stat(full_path, &st) != 0) {
        LOG_WARN("TRANSFER", "Cannot stat local file '%s': %s", full_path, strerror(errno));
        return -1;
    }

    char sha256[SHA256_HEX_LEN + 1];
    if (checksum_calculate_file(full_path, sha256) != 0) {
        LOG_ERROR("TRANSFER", "Failed to compute checksum for '%s'", full_path);
        return -1;
    }

    int fd = open(full_path, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("TRANSFER", "Cannot open local file '%s': %s", full_path, strerror(errno));
        return -1;
    }

    uint32_t req_id = ++g_transfer_req_id;
    LOG_INFO("SYNC", "Upload started: '%s' (size=%lu bytes, sha256=%.8s...)",
             rel_path, (unsigned long)st.st_size, sha256);

    /* 1. Send FILE_TRANSFER_START */
    /* Payload format: rel_path\0 + sha256\0 */
    size_t rel_len = strlen(rel_path) + 1;
    size_t hash_len = strlen(sha256) + 1;
    size_t start_payload_len = rel_len + hash_len;
    char *start_payload = malloc(start_payload_len);
    if (!start_payload) {
        close(fd);
        return -1;
    }

    memcpy(start_payload, rel_path, rel_len);
    memcpy(start_payload + rel_len, sha256, hash_len);

    minidrop_header_t start_hdr;
    minidrop_header_init(&start_hdr, MSG_FILE_TRANSFER_START, req_id,
                         client_id, (uint32_t)start_payload_len, STATUS_OK);

    if (minidrop_send_msg(sockfd, &start_hdr, start_payload) != 0) {
        LOG_ERROR("TRANSFER", "Failed to send FILE_TRANSFER_START frame");
        free(start_payload);
        close(fd);
        return -1;
    }
    free(start_payload);

    /* 2. Stream File in 64 KB Chunks */
    unsigned char *chunk_buf = malloc(MINIDROP_MAX_CHUNK_SIZE);
    if (!chunk_buf) {
        close(fd);
        return -1;
    }

    ssize_t bytes_read;
    size_t total_uploaded = 0;
    while ((bytes_read = read(fd, chunk_buf, MINIDROP_MAX_CHUNK_SIZE)) > 0) {
        minidrop_header_t chunk_hdr;
        minidrop_header_init(&chunk_hdr, MSG_FILE_TRANSFER_CHUNK, req_id,
                             client_id, (uint32_t)bytes_read, STATUS_OK);

        if (minidrop_send_msg(sockfd, &chunk_hdr, chunk_buf) != 0) {
            LOG_ERROR("TRANSFER", "Failed to send chunk frame for '%s'", rel_path);
            free(chunk_buf);
            close(fd);
            return -1;
        }

        total_uploaded += (size_t)bytes_read;
        if (st.st_size > 0 && total_uploaded < (size_t)st.st_size) {
            int pct = (int)((total_uploaded * 100) / (size_t)st.st_size);
            LOG_DEBUG("TRANSFER", "Uploading '%s': %d%% (%zu/%lu bytes)",
                      rel_path, pct, total_uploaded, (unsigned long)st.st_size);
        }
    }

    free(chunk_buf);
    close(fd);

    /* 3. Send FILE_TRANSFER_END */
    minidrop_header_t end_hdr;
    minidrop_header_init(&end_hdr, MSG_FILE_TRANSFER_END, req_id,
                         client_id, (uint32_t)strlen(sha256) + 1, STATUS_OK);

    if (minidrop_send_msg(sockfd, &end_hdr, sha256) != 0) {
        LOG_ERROR("TRANSFER", "Failed to send FILE_TRANSFER_END frame");
        return -1;
    }

    /* 4. Await Server ACK */
    minidrop_header_t ack_hdr;
    void *ack_payload = NULL;
    if (minidrop_recv_msg(sockfd, &ack_hdr, &ack_payload) != 0) {
        LOG_ERROR("TRANSFER", "Failed to receive ACK from server");
        return -1;
    }

    if (ack_payload) free(ack_payload);

    if (ack_hdr.msg_type != MSG_ACK || ack_hdr.status_code != STATUS_OK) {
        LOG_ERROR("TRANSFER", "Server rejected file upload '%s' with status: %s",
                  rel_path, minidrop_status_str(ack_hdr.status_code));
        return -1;
    }

    LOG_INFO("TRANSFER", "100%% completed. SHA-256 verified by server for '%s'.", rel_path);
    return 0;
}

int transfer_delete_file(int sockfd, const char *rel_path, const char *client_id) {
    if (sockfd < 0 || !rel_path || !client_id) return -1;

    uint32_t req_id = ++g_transfer_req_id;
    uint32_t payload_len = (uint32_t)strlen(rel_path) + 1;

    minidrop_header_t hdr;
    minidrop_header_init(&hdr, MSG_FILE_DELETE, req_id, client_id, payload_len, STATUS_OK);

    if (minidrop_send_msg(sockfd, &hdr, rel_path) != 0) {
        LOG_ERROR("TRANSFER", "Failed to send FILE_DELETE frame for '%s'", rel_path);
        return -1;
    }

    minidrop_header_t ack_hdr;
    void *ack_payload = NULL;
    if (minidrop_recv_msg(sockfd, &ack_hdr, &ack_payload) != 0) return -1;
    if (ack_payload) free(ack_payload);

    if (ack_hdr.msg_type != MSG_ACK || ack_hdr.status_code != STATUS_OK) {
        LOG_WARN("TRANSFER", "Server failed to delete '%s' (status=%s)",
                 rel_path, minidrop_status_str(ack_hdr.status_code));
        return -1;
    }

    LOG_INFO("SYNC", "Remote file deleted: '%s'", rel_path);
    return 0;
}

int transfer_rename(int sockfd, const char *old_rel_path, const char *new_rel_path,
                    const char *client_id) {
    if (sockfd < 0 || !old_rel_path || !new_rel_path || !client_id) return -1;

    uint32_t req_id = ++g_transfer_req_id;
    size_t old_len = strlen(old_rel_path) + 1;
    size_t new_len = strlen(new_rel_path) + 1;
    size_t payload_len = old_len + new_len;

    char *payload = malloc(payload_len);
    if (!payload) return -1;
    memcpy(payload, old_rel_path, old_len);
    memcpy(payload + old_len, new_rel_path, new_len);

    minidrop_header_t hdr;
    minidrop_header_init(&hdr, MSG_FILE_RENAME, req_id, client_id, (uint32_t)payload_len, STATUS_OK);

    if (minidrop_send_msg(sockfd, &hdr, payload) != 0) {
        free(payload);
        return -1;
    }
    free(payload);

    minidrop_header_t ack_hdr;
    void *ack_payload = NULL;
    if (minidrop_recv_msg(sockfd, &ack_hdr, &ack_payload) != 0) return -1;
    if (ack_payload) free(ack_payload);

    if (ack_hdr.msg_type != MSG_ACK || ack_hdr.status_code != STATUS_OK) {
        LOG_WARN("TRANSFER", "Server failed to rename '%s' -> '%s'", old_rel_path, new_rel_path);
        return -1;
    }

    LOG_INFO("SYNC", "Remote rename confirmed: '%s' -> '%s'", old_rel_path, new_rel_path);
    return 0;
}

int transfer_create_dir(int sockfd, const char *rel_path, const char *client_id) {
    if (sockfd < 0 || !rel_path || !client_id) return -1;

    uint32_t req_id = ++g_transfer_req_id;
    uint32_t payload_len = (uint32_t)strlen(rel_path) + 1;

    minidrop_header_t hdr;
    minidrop_header_init(&hdr, MSG_DIRECTORY_CREATE, req_id, client_id, payload_len, STATUS_OK);

    if (minidrop_send_msg(sockfd, &hdr, rel_path) != 0) return -1;

    minidrop_header_t ack_hdr;
    void *ack_payload = NULL;
    if (minidrop_recv_msg(sockfd, &ack_hdr, &ack_payload) != 0) return -1;
    if (ack_payload) free(ack_payload);

    return (ack_hdr.msg_type == MSG_ACK && ack_hdr.status_code == STATUS_OK) ? 0 : -1;
}

int transfer_delete_dir(int sockfd, const char *rel_path, const char *client_id) {
    if (sockfd < 0 || !rel_path || !client_id) return -1;

    uint32_t req_id = ++g_transfer_req_id;
    uint32_t payload_len = (uint32_t)strlen(rel_path) + 1;

    minidrop_header_t hdr;
    minidrop_header_init(&hdr, MSG_DIRECTORY_DELETE, req_id, client_id, payload_len, STATUS_OK);

    if (minidrop_send_msg(sockfd, &hdr, rel_path) != 0) return -1;

    minidrop_header_t ack_hdr;
    void *ack_payload = NULL;
    if (minidrop_recv_msg(sockfd, &ack_hdr, &ack_payload) != 0) return -1;
    if (ack_payload) free(ack_payload);

    return (ack_hdr.msg_type == MSG_ACK && ack_hdr.status_code == STATUS_OK) ? 0 : -1;
}

