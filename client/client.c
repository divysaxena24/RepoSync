/**
 * @file client.c
 * @brief MiniDrop Client networking and authentication implementation.
 */

#include "client.h"
#include "protocol.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

volatile sig_atomic_t g_client_running = 1;

void client_config_init(client_config_t *config) {
    if (!config) return;
    strncpy(config->server_host, DEFAULT_CLIENT_SERVER_HOST, sizeof(config->server_host) - 1);
    config->server_host[sizeof(config->server_host) - 1] = '\0';
    config->server_port = DEFAULT_CLIENT_SERVER_PORT;
    strncpy(config->client_id, DEFAULT_CLIENT_ID, sizeof(config->client_id) - 1);
    config->client_id[sizeof(config->client_id) - 1] = '\0';
    strncpy(config->sync_dir, DEFAULT_CLIENT_SYNC_DIR, sizeof(config->sync_dir) - 1);
    config->sync_dir[sizeof(config->sync_dir) - 1] = '\0';
    strncpy(config->auth_token, DEFAULT_CLIENT_AUTH_TOKEN, sizeof(config->auth_token) - 1);
    config->auth_token[sizeof(config->auth_token) - 1] = '\0';
    config->verbose = 0;
}

int client_connect(const client_config_t *config) {
    if (!config) return -1;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG_ERROR("CLIENT", "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((uint16_t)config->server_port);

    if (inet_pton(AF_INET, config->server_host, &serv_addr.sin_addr) <= 0) {
        LOG_ERROR("CLIENT", "Invalid or unsupported server IP: %s", config->server_host);
        close(sockfd);
        return -1;
    }

    LOG_INFO("CLIENT", "Connecting to MiniDrop Server at %s:%d...",
             config->server_host, config->server_port);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        LOG_ERROR("CLIENT", "connect() to %s:%d failed: %s",
                  config->server_host, config->server_port, strerror(errno));
        close(sockfd);
        return -1;
    }

    LOG_INFO("CLIENT", "Connected successfully (socket fd=%d)", sockfd);
    return sockfd;
}

int client_authenticate(int sockfd, const client_config_t *config) {
    if (sockfd < 0 || !config) return -1;

    LOG_INFO("CLIENT", "Authenticating as '%s'...", config->client_id);

    uint32_t token_len = (uint32_t)strlen(config->auth_token);
    minidrop_header_t req_hdr;
    minidrop_header_init(&req_hdr, MSG_AUTH_REQUEST, 1, config->client_id, token_len, STATUS_OK);

    if (minidrop_send_msg(sockfd, &req_hdr, config->auth_token) != 0) {
        LOG_ERROR("CLIENT", "Failed to transmit authentication request");
        return -1;
    }

    minidrop_header_t resp_hdr;
    void *resp_payload = NULL;
    int res = minidrop_recv_msg(sockfd, &resp_hdr, &resp_payload);
    if (res != 0) {
        LOG_ERROR("CLIENT", "Failed to receive authentication response from server");
        return -1;
    }

    if (resp_payload) {
        free(resp_payload);
    }

    if (resp_hdr.msg_type != MSG_AUTH_RESPONSE) {
        LOG_ERROR("CLIENT", "Unexpected response type: %s", minidrop_msg_type_str(resp_hdr.msg_type));
        return -1;
    }

    if (resp_hdr.status_code != STATUS_OK) {
        LOG_ERROR("CLIENT", "Authentication rejected by server (status: %s)",
                  minidrop_status_str(resp_hdr.status_code));
        return -1;
    }

    LOG_INFO("CLIENT", "Authentication accepted! Session established with server.");
    return 0;
}

double client_ping(int sockfd, const client_config_t *config) {
    if (sockfd < 0 || !config) return -1.0;

    struct timeval start, end;
    gettimeofday(&start, NULL);

    minidrop_header_t ping_hdr;
    minidrop_header_init(&ping_hdr, MSG_PING, 2, config->client_id, 0, STATUS_OK);

    if (minidrop_send_msg(sockfd, &ping_hdr, NULL) != 0) {
        LOG_ERROR("CLIENT", "Failed to send PING frame");
        return -1.0;
    }

    minidrop_header_t pong_hdr;
    void *payload = NULL;
    if (minidrop_recv_msg(sockfd, &pong_hdr, &payload) != 0) {
        LOG_ERROR("CLIENT", "Failed to receive PONG frame");
        return -1.0;
    }

    if (payload) free(payload);

    if (pong_hdr.msg_type != MSG_PONG) {
        LOG_ERROR("CLIENT", "Expected PONG, received %s", minidrop_msg_type_str(pong_hdr.msg_type));
        return -1.0;
    }

    gettimeofday(&end, NULL);
    double elapsed_ms = (double)(end.tv_sec - start.tv_sec) * 1000.0 +
                        (double)(end.tv_usec - start.tv_usec) / 1000.0;

    LOG_INFO("CLIENT", "PING sent -> PONG received (round-trip latency: %.2f ms)", elapsed_ms);
    return elapsed_ms;
}

void client_disconnect(int sockfd) {
    if (sockfd >= 0) {
        LOG_INFO("CLIENT", "Disconnecting cleanly from server...");
        close(sockfd);
    }
}

