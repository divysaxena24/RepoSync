/**
 * @file client.h
 * @brief MiniDrop Client core network definitions and session management.
 */

#ifndef MINIDROP_CLIENT_H
#define MINIDROP_CLIENT_H

#include <stdint.h>
#include <signal.h>
#include "protocol.h"

#define DEFAULT_CLIENT_SERVER_HOST "127.0.0.1"
#define DEFAULT_CLIENT_SERVER_PORT 8080
#define DEFAULT_CLIENT_ID          "client01"
#define DEFAULT_CLIENT_SYNC_DIR    "./sync_folder"
#define DEFAULT_CLIENT_AUTH_TOKEN  "minidrop_secret_key"

typedef struct {
    char server_host[256];
    int server_port;
    char client_id[MINIDROP_CLIENT_ID_LEN];
    char sync_dir[MINIDROP_MAX_PATH_LEN];
    char auth_token[256];
    int verbose;
} client_config_t;

/**
 * @brief Global client running flag for clean signal handling.
 */
extern volatile sig_atomic_t g_client_running;

/**
 * @brief Initialize client configuration with default settings.
 */
void client_config_init(client_config_t *config);

/**
 * @brief Connect to the remote MiniDrop server via TCP.
 * @return Connected socket descriptor, or -1 on network failure.
 */
int client_connect(const client_config_t *config);

/**
 * @brief Perform authentication handshake with the server.
 * @return 0 on successful authentication, -1 on rejection or error.
 */
int client_authenticate(int sockfd, const client_config_t *config);

/**
 * @brief Send a PING frame and verify PONG response.
 * @return Latency in milliseconds on success, or -1.0 on failure.
 */
double client_ping(int sockfd, const client_config_t *config);

/**
 * @brief Gracefully terminate the client connection.
 */
void client_disconnect(int sockfd);

#endif /* MINIDROP_CLIENT_H */

