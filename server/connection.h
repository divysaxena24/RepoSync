/**
 * @file connection.h
 * @brief Thread-per-client connection management and concurrent session handling.
 */

#ifndef MINIDROP_CONNECTION_H
#define MINIDROP_CONNECTION_H

#include "server.h"
#include <pthread.h>

typedef struct {
    int client_fd;
    char client_ip[32];
    int client_port;
    const server_config_t *config;
} client_conn_t;

/**
 * @brief Thread entry point for an accepted client connection.
 */
void *connection_worker_thread(void *arg);

#endif /* MINIDROP_CONNECTION_H */

