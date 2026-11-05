/**
 * @file receiver.h
 * @brief Server-side file receiver and protocol command dispatcher.
 */

#ifndef MINIDROP_RECEIVER_H
#define MINIDROP_RECEIVER_H

#include <stdint.h>
#include "protocol.h"
#include "checksum.h"
#include "server.h"

typedef struct {
    int staging_fd;
    char staging_path[MINIDROP_MAX_PATH_LEN * 2];
    char target_rel_path[MINIDROP_MAX_PATH_LEN];
    char expected_sha256[SHA256_HEX_LEN + 1];
    uint32_t active_req_id;
    int is_active;
} file_receive_session_t;

/**
 * @brief Dispatch an incoming protocol message from an authenticated client.
 * @return 0 to continue session, 1 on clean disconnect, -1 on fatal error.
 */
int receiver_dispatch_message(int client_fd, const minidrop_header_t *hdr,
                              void *payload, const server_config_t *config,
                              file_receive_session_t *session);

#endif /* MINIDROP_RECEIVER_H */
