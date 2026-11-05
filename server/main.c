/**
 * @file main.c
 * @brief MiniDrop Server CLI entry point.
 */

#include "server.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS]\n\n", prog_name);
    printf("MiniDrop Server - Central Repository & Directory Synchronization Daemon\n\n");
    printf("Options:\n");
    printf("  -p, --port <port>         TCP port to listen on (default: %d)\n", DEFAULT_SERVER_PORT);
    printf("  -s, --storage <dir>       Path to client storage root (default: %s)\n", DEFAULT_STORAGE_DIR);
    printf("  -t, --token <token>       Authentication secret token (default: %s)\n", DEFAULT_AUTH_TOKEN);
    printf("  -v, --verbose             Enable verbose DEBUG logging\n");
    printf("  -h, --help                Display this help menu\n");
}

int main(int argc, char *argv[]) {
    server_config_t config;
    server_config_init(&config);

    static struct option long_options[] = {
        {"port",    required_argument, 0, 'p'},
        {"storage", required_argument, 0, 's'},
        {"token",   required_argument, 0, 't'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:s:t:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'p':
                config.port = atoi(optarg);
                if (config.port <= 0 || config.port > 65535) {
                    fprintf(stderr, "Error: Invalid port number: %s\n", optarg);
                    return EXIT_FAILURE;
                }
                break;
            case 's':
                strncpy(config.storage_dir, optarg, sizeof(config.storage_dir) - 1);
                config.storage_dir[sizeof(config.storage_dir) - 1] = '\0';
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

    logger_init(NULL, config.verbose ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO);

    if (server_start(&config) != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

