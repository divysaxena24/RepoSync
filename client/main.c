/**
 * @file main.c
 * @brief MiniDrop Client CLI entry point with live directory synchronization daemon.
 */

#include "client.h"
#include "sync_engine.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>

static void handle_client_signal(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_client_running = 0;
    }
}

static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_client_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

static void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS]\n\n", prog_name);
    printf("MiniDrop Client - Directory Synchronization Desktop Agent\n\n");
    printf("Options:\n");
    printf("  -s, --server <ip>         Server IPv4 address (default: %s)\n", DEFAULT_CLIENT_SERVER_HOST);
    printf("  -p, --port <port>         Server TCP port (default: %d)\n", DEFAULT_CLIENT_SERVER_PORT);
    printf("  -c, --client-id <id>      Unique client identifier (default: %s)\n", DEFAULT_CLIENT_ID);
    printf("  -d, --directory <dir>     Path to local directory to monitor (default: %s)\n", DEFAULT_CLIENT_SYNC_DIR);
    printf("  -t, --token <token>       Authentication secret token (default: %s)\n", DEFAULT_CLIENT_AUTH_TOKEN);
    printf("  -v, --verbose             Enable verbose DEBUG logging\n");
    printf("  -h, --help                Display this help menu\n");
}

int main(int argc, char *argv[]) {
    client_config_t config;
    client_config_init(&config);

    static struct option long_options[] = {
        {"server",    required_argument, 0, 's'},
        {"port",      required_argument, 0, 'p'},
        {"client-id", required_argument, 0, 'c'},
        {"directory", required_argument, 0, 'd'},
        {"token",     required_argument, 0, 't'},
        {"verbose",   no_argument,       0, 'v'},
        {"help",      no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "s:p:c:d:t:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 's':
                strncpy(config.server_host, optarg, sizeof(config.server_host) - 1);
                config.server_host[sizeof(config.server_host) - 1] = '\0';
                break;
            case 'p':
                config.server_port = atoi(optarg);
                if (config.server_port <= 0 || config.server_port > 65535) {
                    fprintf(stderr, "Error: Invalid port number: %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            case 'c':
                strncpy(config.client_id, optarg, sizeof(config.client_id) - 1);
                config.client_id[sizeof(config.client_id) - 1] = '\0';
                break;
            case 'd':
                strncpy(config.sync_dir, optarg, sizeof(config.sync_dir) - 1);
                config.sync_dir[sizeof(config.sync_dir) - 1] = '\0';
                break;
            case 't':
                strncpy(config.auth_token, optarg, sizeof(config.auth_token) - 1);
                config.auth_token[sizeof(config.auth_token) - 1] = '\0';
                break;
            case 'v':
                config.verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    setup_signals();
    logger_init(NULL, config.verbose ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO);

    LOG_INFO("CLIENT", "=================================================");
    LOG_INFO("CLIENT", " MiniDrop Client Starting");
    LOG_INFO("CLIENT", " Client ID:   %s", config.client_id);
    LOG_INFO("CLIENT", " Target:      %s:%d", config.server_host, config.server_port);
    LOG_INFO("CLIENT", " Sync Folder: %s", config.sync_dir);
    LOG_INFO("CLIENT", "=================================================");

    int sockfd = client_connect(&config);
    if (sockfd < 0) {
        return EXIT_FAILURE;
    }

    if (client_authenticate(sockfd, &config) != 0) {
        client_disconnect(sockfd);
        return EXIT_FAILURE;
    }

    /* Initialize and start synchronization engine */
    sync_engine_t engine;
    if (sync_engine_init(&engine, sockfd, &config) != 0) {
        client_disconnect(sockfd);
        return EXIT_FAILURE;
    }

    /* 1. Initial Directory Sync */
    sync_engine_initial_sync(&engine);

    /* 2. Start Live inotify Monitoring & Background Worker */
    if (sync_engine_start(&engine) != 0) {
        client_disconnect(sockfd);
        return EXIT_FAILURE;
    }

    LOG_INFO("CLIENT", "=================================================");
    LOG_INFO("CLIENT", " Live synchronization active! Watching '%s'...", config.sync_dir);
    LOG_INFO("CLIENT", " Press Ctrl+C to terminate cleanly.");
    LOG_INFO("CLIENT", "=================================================");

    while (g_client_running) {
        sleep(1);
    }

    LOG_INFO("CLIENT", "Signal caught. Shutting down client...");
    sync_engine_stop(&engine);
    client_disconnect(sockfd);

    return EXIT_SUCCESS;
}
