/**
 * @file watcher.c
 * @brief Recursive directory monitoring subsystem implementation using Linux inotify API.
 */

#include "watcher.h"
#include "metadata.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <poll.h>
#include <time.h>

#define EVENT_BUF_LEN (1024 * (sizeof(struct inotify_event) + 256))

typedef struct {
    int wd;
    char rel_path[MINIDROP_MAX_PATH_LEN];
} watch_entry_t;

static watch_entry_t g_watch_table[MAX_WATCHES];
static size_t g_num_watches = 0;
static pthread_mutex_t g_watch_mutex = PTHREAD_MUTEX_INITIALIZER;

static int add_single_watch(int inotify_fd, const char *sync_dir, const char *sub_dir) {
    char full_path[MINIDROP_MAX_PATH_LEN * 2];
    if (sub_dir && strlen(sub_dir) > 0) {
        snprintf(full_path, sizeof(full_path), "%s/%s", sync_dir, sub_dir);
    } else {
        snprintf(full_path, sizeof(full_path), "%s", sync_dir);
    }

    uint32_t mask = IN_CREATE | IN_MODIFY | IN_DELETE | IN_CLOSE_WRITE |
                    IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF;

    int wd = inotify_add_watch(inotify_fd, full_path, mask);
    if (wd < 0) {
        LOG_WARN("WATCHER", "inotify_add_watch failed for '%s': %s", full_path, strerror(errno));
        return -1;
    }

    pthread_mutex_lock(&g_watch_mutex);
    if (g_num_watches < MAX_WATCHES) {
        g_watch_table[g_num_watches].wd = wd;
        strncpy(g_watch_table[g_num_watches].rel_path, sub_dir ? sub_dir : "",
                sizeof(g_watch_table[g_num_watches].rel_path) - 1);
        g_watch_table[g_num_watches].rel_path[sizeof(g_watch_table[g_num_watches].rel_path) - 1] = '\0';
        g_num_watches++;
    }
    pthread_mutex_unlock(&g_watch_mutex);

    LOG_DEBUG("WATCHER", "Registered watch [wd=%d] for '%s'", wd, sub_dir ? sub_dir : "ROOT");
    return wd;
}

static void add_watches_recursive(int inotify_fd, const char *sync_dir, const char *sub_dir) {
    add_single_watch(inotify_fd, sync_dir, sub_dir);

    char full_dir[MINIDROP_MAX_PATH_LEN * 2];
    if (sub_dir && strlen(sub_dir) > 0) {
        snprintf(full_dir, sizeof(full_dir), "%s/%s", sync_dir, sub_dir);
    } else {
        snprintf(full_dir, sizeof(full_dir), "%s", sync_dir);
    }

    DIR *dir = opendir(full_dir);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (entry->d_name[0] == '.') continue;

        char child_full[MINIDROP_MAX_PATH_LEN * 3];
        snprintf(child_full, sizeof(child_full), "%s/%s", full_dir, entry->d_name);

        struct stat st;
        if (lstat(child_full, &st) == 0 && S_ISDIR(st.st_mode)) {
            char child_rel[MINIDROP_MAX_PATH_LEN];
            if (sub_dir && strlen(sub_dir) > 0) {
                snprintf(child_rel, sizeof(child_rel), "%s/%s", sub_dir, entry->d_name);
            } else {
                snprintf(child_rel, sizeof(child_rel), "%s", entry->d_name);
            }
            add_watches_recursive(inotify_fd, sync_dir, child_rel);
        }
    }
    closedir(dir);
}

static const char *get_rel_path_from_wd(int wd) {
    pthread_mutex_lock(&g_watch_mutex);
    for (size_t i = 0; i < g_num_watches; i++) {
        if (g_watch_table[i].wd == wd) {
            const char *p = g_watch_table[i].rel_path;
            pthread_mutex_unlock(&g_watch_mutex);
            return p;
        }
    }
    pthread_mutex_unlock(&g_watch_mutex);
    return NULL;
}

static void *watcher_thread_func(void *arg) {
    dir_watcher_t *w = (dir_watcher_t *)arg;
    char buffer[EVENT_BUF_LEN] __attribute__((aligned(__alignof__(struct inotify_event))));

    uint32_t rename_cookie = 0;
    char rename_old_path[MINIDROP_MAX_PATH_LEN] = {0};

    LOG_INFO("WATCHER", "Inotify recursive watcher thread started for '%s'", w->sync_dir);

    while (w->is_running) {
        struct pollfd pfd;
        pfd.fd = w->inotify_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int poll_res = poll(&pfd, 1, 250);
        if (poll_res < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (poll_res == 0) {
            continue; /* Timeout, check w->is_running */
        }
        if (!(pfd.revents & POLLIN)) {
            continue;
        }

        ssize_t len = read(w->inotify_fd, buffer, sizeof(buffer));
        if (len < 0) {
            if (errno == EINTR) continue;
            if (!w->is_running) break;
            LOG_ERROR("WATCHER", "read() on inotify fd failed: %s", strerror(errno));
            break;
        }

        ssize_t i = 0;
        while (i < len) {
            struct inotify_event *event = (struct inotify_event *)&buffer[i];

            if (event->len > 0) {
                /* Skip hidden/temporary files */
                if (event->name[0] != '.') {
                    const char *dir_rel = get_rel_path_from_wd(event->wd);
                    char item_rel[MINIDROP_MAX_PATH_LEN];

                    if (dir_rel && strlen(dir_rel) > 0) {
                        snprintf(item_rel, sizeof(item_rel), "%s/%s", dir_rel, event->name);
                    } else {
                        snprintf(item_rel, sizeof(item_rel), "%s", event->name);
                    }

                    time_t now = time(NULL);

                    /* 1. Directory creation */
                    if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
                        LOG_INFO("WATCHER", "New directory created: '%s'", item_rel);
                        add_watches_recursive(w->inotify_fd, w->sync_dir, item_rel);

                        sync_event_t ev;
                        memset(&ev, 0, sizeof(ev));
                        ev.type = SYNC_EV_DIR_CREATE;
                        strncpy(ev.rel_path, item_rel, sizeof(ev.rel_path) - 1);
                        ev.timestamp = (uint64_t)now;
                        sync_queue_push(w->queue, &ev);

                        /* Scan any files or subdirectories created during burst */
                        char full_new_dir[MINIDROP_MAX_PATH_LEN * 2];
                        snprintf(full_new_dir, sizeof(full_new_dir), "%s/%s", w->sync_dir, item_rel);
                        sync_manifest_t sub_man;
                        if (sync_manifest_scan(full_new_dir, &sub_man) > 0) {
                            for (size_t s = 0; s < sub_man.count; s++) {
                                char combined_rel[MINIDROP_MAX_PATH_LEN * 2];
                                snprintf(combined_rel, sizeof(combined_rel), "%s/%s",
                                         item_rel, sub_man.entries[s].rel_path);
                                sync_event_t sub_ev;
                                memset(&sub_ev, 0, sizeof(sub_ev));
                                sub_ev.type = sub_man.entries[s].is_dir ? SYNC_EV_DIR_CREATE : SYNC_EV_CREATE;
                                strncpy(sub_ev.rel_path, combined_rel, sizeof(sub_ev.rel_path) - 1);
                                sub_ev.timestamp = (uint64_t)now;
                                sync_queue_push(w->queue, &sub_ev);
                            }
                            sync_manifest_free(&sub_man);
                        }
                    }
                    /* 2. File editing complete (IN_CLOSE_WRITE is the safe commit signal) */
                    else if (event->mask & IN_CLOSE_WRITE) {
                        LOG_INFO("WATCHER", "File modified/written: '%s'", item_rel);
                        sync_event_t ev;
                        memset(&ev, 0, sizeof(ev));
                        ev.type = SYNC_EV_MODIFY;
                        strncpy(ev.rel_path, item_rel, sizeof(ev.rel_path) - 1);
                        ev.timestamp = (uint64_t)now;
                        sync_queue_push(w->queue, &ev);
                    }
                    /* 3. Deletion */
                    else if (event->mask & IN_DELETE) {
                        LOG_INFO("WATCHER", "%s deleted: '%s'",
                                 (event->mask & IN_ISDIR) ? "Directory" : "File", item_rel);
                        sync_event_t ev;
                        memset(&ev, 0, sizeof(ev));
                        ev.type = (event->mask & IN_ISDIR) ? SYNC_EV_DIR_DELETE : SYNC_EV_DELETE;
                        strncpy(ev.rel_path, item_rel, sizeof(ev.rel_path) - 1);
                        ev.timestamp = (uint64_t)now;
                        sync_queue_push(w->queue, &ev);
                    }
                    /* 4. Rename: MOVED_FROM */
                    else if (event->mask & IN_MOVED_FROM) {
                        rename_cookie = event->cookie;
                        strncpy(rename_old_path, item_rel, sizeof(rename_old_path) - 1);
                        rename_old_path[sizeof(rename_old_path) - 1] = '\0';
                    }
                    /* 5. Rename: MOVED_TO (Cookie matched) */
                    else if (event->mask & IN_MOVED_TO) {
                        if (event->cookie == rename_cookie && rename_cookie != 0) {
                            LOG_INFO("WATCHER", "Rename correlated (cookie=%u): '%s' -> '%s'",
                                     rename_cookie, rename_old_path, item_rel);

                            sync_event_t ev;
                            memset(&ev, 0, sizeof(ev));
                            ev.type = SYNC_EV_RENAME;
                            strncpy(ev.rel_path, item_rel, sizeof(ev.rel_path) - 1);
                            strncpy(ev.old_rel_path, rename_old_path, sizeof(ev.old_rel_path) - 1);
                            ev.timestamp = (uint64_t)now;
                            sync_queue_push(w->queue, &ev);
                            rename_cookie = 0;
                        } else {
                            /* Treat as new file */
                            sync_event_t ev;
                            memset(&ev, 0, sizeof(ev));
                            ev.type = (event->mask & IN_ISDIR) ? SYNC_EV_DIR_CREATE : SYNC_EV_CREATE;
                            strncpy(ev.rel_path, item_rel, sizeof(ev.rel_path) - 1);
                            ev.timestamp = (uint64_t)now;
                            sync_queue_push(w->queue, &ev);
                        }
                    }
                }
            }
            i += (ssize_t)(sizeof(struct inotify_event) + event->len);
        }
    }

    LOG_INFO("WATCHER", "Inotify watcher thread exiting");
    return NULL;
}

int watcher_init(dir_watcher_t *watcher, const char *sync_dir, sync_queue_t *queue) {
    if (!watcher || !sync_dir || !queue) return -1;

    watcher->inotify_fd = inotify_init();
    if (watcher->inotify_fd < 0) {
        LOG_ERROR("WATCHER", "inotify_init() failed: %s", strerror(errno));
        return -1;
    }

    strncpy(watcher->sync_dir, sync_dir, sizeof(watcher->sync_dir) - 1);
    watcher->sync_dir[sizeof(watcher->sync_dir) - 1] = '\0';
    watcher->queue = queue;
    watcher->is_running = 0;

    pthread_mutex_lock(&g_watch_mutex);
    g_num_watches = 0;
    pthread_mutex_unlock(&g_watch_mutex);

    add_watches_recursive(watcher->inotify_fd, watcher->sync_dir, "");
    return 0;
}

int watcher_start(dir_watcher_t *watcher) {
    if (!watcher) return -1;
    watcher->is_running = 1;
    if (pthread_create(&watcher->thread, NULL, watcher_thread_func, watcher) != 0) {
        LOG_ERROR("WATCHER", "Failed to start watcher thread: %s", strerror(errno));
        watcher->is_running = 0;
        return -1;
    }
    return 0;
}

void watcher_stop(dir_watcher_t *watcher) {
    if (!watcher || !watcher->is_running) return;
    watcher->is_running = 0;
    close(watcher->inotify_fd);
    pthread_join(watcher->thread, NULL);
}
