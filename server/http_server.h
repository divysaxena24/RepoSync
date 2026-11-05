/**
 * @file http_server.h
 * @brief Embedded micro HTTP/REST server serving the Smart Action Button dashboard.
 */

#ifndef MINIDROP_HTTP_SERVER_H
#define MINIDROP_HTTP_SERVER_H

#include "repo_engine.h"
#include <signal.h>

typedef struct {
    int port;
    repo_engine_t *engine;
    volatile sig_atomic_t is_running;
} http_server_t;

/**
 * @brief Initialize the embedded HTTP server.
 */
int http_server_init(http_server_t *server, int port, repo_engine_t *engine);

/**
 * @brief Run the HTTP server accept loop (blocks until stopped).
 */
int http_server_run(http_server_t *server);

/**
 * @brief Stop the HTTP server.
 */
void http_server_stop(http_server_t *server);

#endif /* MINIDROP_HTTP_SERVER_H */

