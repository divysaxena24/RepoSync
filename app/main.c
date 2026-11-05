/**
 * @file main.c
 * @brief MiniDrop Single-User Remote Repository CLI and Web Service entry point.
 */

#include "repo_engine.h"
#include "http_server.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define DEFAULT_LOCAL_DIR  "./workspace"
#define DEFAULT_REMOTE_DIR "./storage/repo"
#define DEFAULT_HTTP_PORT  8080

static http_server_t g_server;

static void handle_sig(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        http_server_stop(&g_server);
    }
}

static void print_banner(void) {
    printf("=========================================================\n");
    printf("       MiniDrop Single-User Remote Repository System     \n");
    printf("=========================================================\n");
}

static void print_help(const char *prog) {
    print_banner();
    printf("Usage: %s <COMMAND> [OPTIONS]\n\n", prog);
    printf("Commands:\n");
    printf("  web                Start the Smart Action Button Dashboard (default)\n");
    printf("  status             Check synchronization diff between local and remote\n");
    printf("  push               Push all local changes to the Remote Repository\n");
    printf("  pull               Pull all updates from Remote Repository to local\n");
    printf("  help               Show this help menu\n\n");
    printf("Options:\n");
    printf("  --port <port>      HTTP port for Web Dashboard (default: %d)\n", DEFAULT_HTTP_PORT);
    printf("  --local <dir>      Local workspace directory (default: %s)\n", DEFAULT_LOCAL_DIR);
    printf("  --remote <dir>     Remote repository directory (default: %s)\n", DEFAULT_REMOTE_DIR);
}

int main(int argc, char *argv[]) {
    char local_dir[MINIDROP_MAX_PATH_LEN] = DEFAULT_LOCAL_DIR;
    char remote_dir[MINIDROP_MAX_PATH_LEN] = DEFAULT_REMOTE_DIR;
    int port = DEFAULT_HTTP_PORT;
    const char *cmd = "web";

    /* Parse command */
    if (argc > 1 && argv[1][0] != '-') {
        cmd = argv[1];
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }

    /* Parse options */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--local") == 0 && i + 1 < argc) {
            strncpy(local_dir, argv[++i], sizeof(local_dir) - 1);
        } else if (strcmp(argv[i], "--remote") == 0 && i + 1 < argc) {
            strncpy(remote_dir, argv[++i], sizeof(remote_dir) - 1);
        }
    }

    logger_init(NULL, LOG_LEVEL_INFO);

    repo_engine_t engine;
    if (repo_engine_init(&engine, local_dir, remote_dir) != 0) {
        LOG_ERROR("APP", "Failed to initialize repository engine");
        return EXIT_FAILURE;
    }

    /* 1. STATUS COMMAND */
    if (strcmp(cmd, "status") == 0) {
        print_banner();
        printf("Local Workspace:   %s\n", local_dir);
        printf("Remote Repository: %s\n\n", remote_dir);

        repo_diff_t diff;
        repo_engine_get_status(&engine, &diff);

        if (diff.state == REPO_STATE_SYNCED) {
            printf("[STATUS] %s\n", diff.status_message);
        } else if (diff.state == REPO_STATE_NEEDS_PUSH) {
            printf("[STATUS] Push Required: %zu file(s) modified/created:\n", diff.pending_push_count);
            for (size_t i = 0; i < diff.pending_push_count; i++) {
                printf("  + [NEW/MODIFIED] %s\n", diff.push_files[i]);
            }
        } else if (diff.state == REPO_STATE_NEEDS_PULL) {
            printf("[STATUS] Pull Available: %zu file(s) updated on remote:\n", diff.pending_pull_count);
            for (size_t i = 0; i < diff.pending_pull_count; i++) {
                printf("  - [REMOTE UPDATE] %s\n", diff.pull_files[i]);
            }
        }
        return EXIT_SUCCESS;
    }

    /* 2. PUSH COMMAND */
    if (strcmp(cmd, "push") == 0) {
        print_banner();
        printf("Executing PUSH: %s -> %s\n\n", local_dir, remote_dir);
        if (repo_engine_push(&engine) == 0) {
            printf("\n[SUCCESS] All files pushed and verified with SHA-256!\n");
            return EXIT_SUCCESS;
        } else {
            printf("\n[ERROR] Push failed.\n");
            return EXIT_FAILURE;
        }
    }

    /* 3. PULL COMMAND */
    if (strcmp(cmd, "pull") == 0) {
        print_banner();
        printf("Executing PULL: %s -> %s\n\n", remote_dir, local_dir);
        if (repo_engine_pull(&engine) == 0) {
            printf("\n[SUCCESS] Local directory updated from Remote Repository!\n");
            return EXIT_SUCCESS;
        } else {
            printf("\n[ERROR] Pull failed.\n");
            return EXIT_FAILURE;
        }
    }

    /* 4. WEB DASHBOARD COMMAND (Default) */
    if (strcmp(cmd, "web") == 0) {
        signal(SIGINT, handle_sig);
        signal(SIGTERM, handle_sig);

        print_banner();
        printf(" Local Workspace:   %s\n", local_dir);
        printf(" Remote Repository: %s\n", remote_dir);
        printf(" Web Dashboard:     http://localhost:%d\n", port);
        printf(" Press Ctrl+C to stop.\n");
        printf("=========================================================\n\n");

        http_server_init(&g_server, port, &engine);
        http_server_run(&g_server);
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "Unknown command: '%s'. Run '%s help' for usage.\n", cmd, argv[0]);
    return EXIT_FAILURE;
}

