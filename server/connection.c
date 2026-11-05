/**
 * @file connection.c
 * @brief Thread-per-client connection management implementation.
 */

#include "connection.h"
#include "receiver.h"
#include "protocol.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *connection_worker_thread(void *arg) {
    client_conn_t *conn = (client_conn_t *)arg;
    int client_fd = conn->client_fd;
    const server_config_t *config = conn->config;
    char client_ip[32];
    strncpy(client_ip, conn->client_ip, sizeof(client_ip) - 1);
    client_ip[sizeof(client_ip) - 1] = '\0';
    int client_port = conn->client_port;

    free(conn); /* Freed immediately; local variables retain connection context */

    LOG_INFO("SERVER", "[Thread %lu] Connection accepted from %s:%d (socket fd=%d)",
             (unsigned long)pthread_self(), client_ip, client_port, client_fd);

    int authenticated = 0;
    char client_id[MINIDROP_CLIENT_ID_LEN] = {0};
    file_receive_session_t session;
    memset(&session, 0, sizeof(session));
    session.staging_fd = -1;

    while (g_server_running) {
        minidrop_header_t req_hdr;
        void *payload = NULL;

        int res = minidrop_recv_msg(client_fd, &req_hdr, &payload);
        if (res == 1) {
            LOG_INFO("SERVER", "Client %s (%s:%d) disconnected cleanly",
                     client_id[0] ? client_id : "unauthenticated", client_ip, client_port);
            break;
        } else if (res < 0) {
            LOG_WARN("SERVER", "Client abrupt disconnect or socket error on fd=%d", client_fd);
            break;
        }

        LOG_DEBUG("SERVER", "Received frame %s (req_id=%u, len=%u, client=%s)",
                  minidrop_msg_type_str(req_hdr.msg_type), req_hdr.request_id,
                  req_hdr.payload_len, req_hdr.client_id);

        /* 1. Authentication */
        if (req_hdr.msg_type == MSG_AUTH_REQUEST) {
            strncpy(client_id, req_hdr.client_id, sizeof(client_id) - 1);
            const char *token = payload ? (const char *)payload : "";

            if (strcmp(token, config->auth_token) == 0) {
                authenticated = 1;
                LOG_INFO("SERVER", "Client '%s' successfully authenticated", client_id);

                minidrop_header_t resp_hdr;
                minidrop_header_init(&resp_hdr, MSG_AUTH_RESPONSE, req_hdr.request_id,
                                     client_id, 0, STATUS_OK);
                minidrop_send_msg(client_fd, &resp_hdr, NULL);
            } else {
                LOG_WARN("SERVER", "Authentication failed for client '%s'", client_id);
                minidrop_header_t resp_hdr;
                minidrop_header_init(&resp_hdr, MSG_AUTH_RESPONSE, req_hdr.request_id,
                                     client_id, 0, STATUS_ERR_AUTH);
                minidrop_send_msg(client_fd, &resp_hdr, NULL);
                if (payload) free(payload);
                break;
            }
            if (payload) free(payload);
            continue;
        }

        /* Enforce authentication */
        if (!authenticated) {
            LOG_WARN("SERVER", "Unauthenticated request rejected from %s:%d", client_ip, client_port);
            minidrop_header_t resp_hdr;
            minidrop_header_init(&resp_hdr, MSG_ERROR, req_hdr.request_id,
                                 client_id, 0, STATUS_ERR_AUTH);
            minidrop_send_msg(client_fd, &resp_hdr, NULL);
            if (payload) free(payload);
            break;
        }

        /* 2. Keepalive Ping */
        if (req_hdr.msg_type == MSG_PING) {
            minidrop_header_t resp_hdr;
            minidrop_header_init(&resp_hdr, MSG_PONG, req_hdr.request_id,
                                 client_id, 0, STATUS_OK);
            minidrop_send_msg(client_fd, &resp_hdr, NULL);
            if (payload) free(payload);
            continue;
        }

        /* 3. Dispatch file and sync operations */
        int dispatch_res = receiver_dispatch_message(client_fd, &req_hdr, payload,
                                                     config, &session);
        if (payload) free(payload);

        if (dispatch_res < 0) {
            LOG_ERROR("SERVER", "Fatal error during message dispatch, closing client connection");
            break;
        }
    }

    if (session.is_active && session.staging_fd >= 0) {
        close(session.staging_fd);
        unlink(session.staging_path);
    }

    close(client_fd);
    LOG_INFO("SERVER", "[Thread %lu] Client worker thread exiting", (unsigned long)pthread_self());
    return NULL;
}

