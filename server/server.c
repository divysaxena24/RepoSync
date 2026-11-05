/**
 * @file server.c
 * @brief MiniDrop TCP Server implementation with thread-per-client concurrency.
 */

#include "server.h"
#include "connection.h"
#include "storage.h"
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
#include <sys/stat.h>
#include <pthread.h>
#include <poll.h>

volatile sig_atomic_t g_server_running = 1;

static void handle_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_server_running = 0;
    }
}

void server_setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Ignore SIGPIPE so a sudden client disconnect doesn't terminate server */
    signal(SIGPIPE, SIG_IGN);
}

void server_config_init(server_config_t *config) {
    if (!config) return;
    config->port = DEFAULT_SERVER_PORT;
    strncpy(config->storage_dir, DEFAULT_STORAGE_DIR, sizeof(config->storage_dir) - 1);
    config->storage_dir[sizeof(config->storage_dir) - 1] = '\0';
    strncpy(config->auth_token, DEFAULT_AUTH_TOKEN, sizeof(config->auth_token) - 1);
    config->auth_token[sizeof(config->auth_token) - 1] = '\0';
    config->verbose = 0;
}

int server_start(const server_config_t *config) {
    if (!config) return -1;

    server_setup_signals();

    if (storage_init(config->storage_dir) != 0) {
        LOG_ERROR("SERVER", "Failed to initialize storage at '%s'", config->storage_dir);
        return -1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERROR("SERVER", "socket() creation failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_WARN("SERVER", "setsockopt(SO_REUSEADDR) failed: %s", strerror(errno));
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons((uint16_t)config->port);

    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        LOG_ERROR("SERVER", "bind() failed on port %d: %s", config->port, strerror(errno));
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, SERVER_BACKLOG) < 0) {
        LOG_ERROR("SERVER", "listen() failed: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    LOG_INFO("SERVER", "=================================================");
    LOG_INFO("SERVER", " MiniDrop Multithreaded Server started on port %d", config->port);
    LOG_INFO("SERVER", " Storage Root: %s", config->storage_dir);
    LOG_INFO("SERVER", " Thread Model: Thread-per-Client Concurrent I/O");
    LOG_INFO("SERVER", " Waiting for incoming client connections...");
    LOG_INFO("SERVER", "=================================================");

    while (g_server_running) {
        struct pollfd pfd;
        pfd.fd = listen_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int poll_res = poll(&pfd, 1, 500); /* 500 ms poll to check g_server_running */
        if (poll_res < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (poll_res == 0) {
            continue; /* Timeout, check g_server_running */
        }
        if (!(pfd.revents & POLLIN)) {
            continue;
        }

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR("SERVER", "accept() failed: %s", strerror(errno));
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        client_conn_t *conn = malloc(sizeof(client_conn_t));
        if (!conn) {
            LOG_ERROR("SERVER", "Failed to allocate memory for incoming client");
            close(client_fd);
            continue;
        }

        conn->client_fd = client_fd;
        strncpy(conn->client_ip, client_ip, sizeof(conn->client_ip) - 1);
        conn->client_ip[sizeof(conn->client_ip) - 1] = '\0';
        conn->client_port = client_port;
        conn->config = config;

        pthread_t thread;
        if (pthread_create(&thread, NULL, connection_worker_thread, conn) != 0) {
            LOG_ERROR("SERVER", "Failed to spawn thread for client %s:%d: %s",
                      client_ip, client_port, strerror(errno));
            free(conn);
            close(client_fd);
            continue;
        }

        pthread_detach(thread);
    }

    LOG_INFO("SERVER", "Shutting down MiniDrop server gracefully...");
    close(listen_fd);
    storage_destroy();
    return 0;
}
