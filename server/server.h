/**
 * @file server.h
 * @brief MiniDrop TCP Server core definitions and state management.
 */

#ifndef MINIDROP_SERVER_H
#define MINIDROP_SERVER_H

#include <stdint.h>
#include <signal.h>
#include "protocol.h"

#define DEFAULT_SERVER_PORT 8080
#define DEFAULT_STORAGE_DIR "./storage/clients"
#define DEFAULT_AUTH_TOKEN  "minidrop_secret_key"
#define SERVER_BACKLOG      64

typedef struct {
    int port;
    char storage_dir[MINIDROP_MAX_PATH_LEN];
    char auth_token[256];
    int verbose;
} server_config_t;

/**
 * @brief Global server running flag for clean signal-driven shutdown.
 */
extern volatile sig_atomic_t g_server_running;

/**
 * @brief Initialize server configuration with default settings.
 */
void server_config_init(server_config_t *config);

/**
 * @brief Setup signal handlers (SIGINT, SIGTERM, SIGPIPE).
 */
void server_setup_signals(void);

/**
 * @brief Start the TCP server listener and main accept loop.
 * @return 0 on clean shutdown, -1 on fatal error.
 */
int server_start(const server_config_t *config);

#endif /* MINIDROP_SERVER_H */

