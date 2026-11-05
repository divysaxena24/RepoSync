/**
 * @file receiver.c
 * @brief Server-side file receiver and protocol command dispatcher implementation.
 */

#include "receiver.h"
#include "storage.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int receiver_dispatch_message(int client_fd, const minidrop_header_t *hdr,
                              void *payload, const server_config_t *config,
                              file_receive_session_t *session) {
    if (client_fd < 0 || !hdr || !config || !session) return -1;

    switch (hdr->msg_type) {
        case MSG_FILE_TRANSFER_START: {
            if (!payload || hdr->payload_len == 0) return -1;

            const char *rel_path = (const char *)payload;
            size_t rel_len = strlen(rel_path) + 1;
            const char *sha256 = (hdr->payload_len > rel_len) ? ((const char *)payload + rel_len) : "";

            strncpy(session->target_rel_path, rel_path, sizeof(session->target_rel_path) - 1);
            strncpy(session->expected_sha256, sha256, sizeof(session->expected_sha256) - 1);
            session->active_req_id = hdr->request_id;

            session->staging_fd = storage_stage_file_start(config->storage_dir, hdr->client_id,
                                                           rel_path, hdr->request_id,
                                                           session->staging_path);
            if (session->staging_fd < 0) {
                LOG_ERROR("RECEIVER", "Failed to start staging file for '%s'", rel_path);
                minidrop_header_t resp_hdr;
                minidrop_header_init(&resp_hdr, MSG_ERROR, hdr->request_id,
                                     hdr->client_id, 0, STATUS_ERR_IO);
                minidrop_send_msg(client_fd, &resp_hdr, NULL);
                session->is_active = 0;
                return 0;
            }

            session->is_active = 1;
            LOG_INFO("SYNC", "Incoming transfer: '%s' from client '%s'", rel_path, hdr->client_id);
            return 0;
        }

        case MSG_FILE_TRANSFER_CHUNK: {
            if (!session->is_active || session->active_req_id != hdr->request_id) {
                LOG_ERROR("RECEIVER", "Received unexpected chunk frame (no active transfer session)");
                return -1;
            }

            if (payload && hdr->payload_len > 0) {
                if (storage_stage_file_write_chunk(session->staging_fd, payload, hdr->payload_len) != 0) {
                    LOG_ERROR("RECEIVER", "Failed writing chunk to staging file");
                    return -1;
                }
            }
            return 0;
        }

        case MSG_FILE_TRANSFER_END: {
            if (!session->is_active || session->active_req_id != hdr->request_id) {
                LOG_ERROR("RECEIVER", "Received unexpected FILE_TRANSFER_END frame");
                return -1;
            }

            const char *expected_hash = payload ? (const char *)payload : session->expected_sha256;
            char conflict_path[MINIDROP_MAX_PATH_LEN] = {0};

            status_code_t status = storage_stage_file_commit(session->staging_fd,
                                                             session->staging_path,
                                                             config->storage_dir,
                                                             hdr->client_id,
                                                             session->target_rel_path,
                                                             expected_hash,
                                                             conflict_path);
            session->is_active = 0;
            session->staging_fd = -1;

            minidrop_header_t resp_hdr;
            minidrop_header_init(&resp_hdr, (status == STATUS_OK) ? MSG_ACK : MSG_ERROR,
                                 hdr->request_id, hdr->client_id, 0, status);
            minidrop_send_msg(client_fd, &resp_hdr, NULL);

            if (status == STATUS_OK) {
                LOG_INFO("ACK", "Synchronization successful for '%s'", session->target_rel_path);
            }
            return 0;
        }

        case MSG_FILE_DELETE: {
            if (!payload) return -1;
            const char *rel_path = (const char *)payload;
            status_code_t status = storage_delete_file(config->storage_dir, hdr->client_id, rel_path);

            minidrop_header_t resp_hdr;
            minidrop_header_init(&resp_hdr, MSG_ACK, hdr->request_id, hdr->client_id, 0, status);
            minidrop_send_msg(client_fd, &resp_hdr, NULL);
            return 0;
        }

        case MSG_FILE_RENAME: {
            if (!payload) return -1;
            const char *old_path = (const char *)payload;
            size_t old_len = strlen(old_path) + 1;
            const char *new_path = (hdr->payload_len > old_len) ? ((const char *)payload + old_len) : "";

            status_code_t status = storage_rename(config->storage_dir, hdr->client_id, old_path, new_path);

            minidrop_header_t resp_hdr;
            minidrop_header_init(&resp_hdr, MSG_ACK, hdr->request_id, hdr->client_id, 0, status);
            minidrop_send_msg(client_fd, &resp_hdr, NULL);
            return 0;
        }

        case MSG_DIRECTORY_CREATE: {
            if (!payload) return -1;
            const char *rel_path = (const char *)payload;
            status_code_t status = storage_create_dir(config->storage_dir, hdr->client_id, rel_path);

            minidrop_header_t resp_hdr;
            minidrop_header_init(&resp_hdr, MSG_ACK, hdr->request_id, hdr->client_id, 0, status);
            minidrop_send_msg(client_fd, &resp_hdr, NULL);
            return 0;
        }

        case MSG_DIRECTORY_DELETE: {
            if (!payload) return -1;
            const char *rel_path = (const char *)payload;
            status_code_t status = storage_delete_dir(config->storage_dir, hdr->client_id, rel_path);

            minidrop_header_t resp_hdr;
            minidrop_header_init(&resp_hdr, MSG_ACK, hdr->request_id, hdr->client_id, 0, status);
            minidrop_send_msg(client_fd, &resp_hdr, NULL);
            return 0;
        }

        default:
            LOG_WARN("RECEIVER", "Unhandled message type %s", minidrop_msg_type_str(hdr->msg_type));
            minidrop_header_t resp_hdr;
            minidrop_header_init(&resp_hdr, MSG_ERROR, hdr->request_id, hdr->client_id, 0, STATUS_ERR_PROTOCOL);
            minidrop_send_msg(client_fd, &resp_hdr, NULL);
            return 0;
    }
}

