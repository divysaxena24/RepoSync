/**
 * @file logger.c
 * @brief Thread-safe colored logging subsystem implementation.
 */

#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

/* ANSI terminal color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_DEBUG   "\033[36m" /* Cyan */
#define COLOR_INFO    "\033[32m" /* Green */
#define COLOR_WARN    "\033[33m" /* Yellow */
#define COLOR_ERROR   "\033[31m" /* Red */
#define COLOR_TAG     "\033[1;34m" /* Bold Blue */

static pthread_mutex_t g_logger_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_log_file = NULL;
static log_level_t g_min_level = LOG_LEVEL_INFO;

static const char *level_to_string(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO ";
        case LOG_LEVEL_WARN:  return "WARN ";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "UNKN ";
    }
}

static const char *level_to_color(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return COLOR_DEBUG;
        case LOG_LEVEL_INFO:  return COLOR_INFO;
        case LOG_LEVEL_WARN:  return COLOR_WARN;
        case LOG_LEVEL_ERROR: return COLOR_ERROR;
        default:              return COLOR_RESET;
    }
}

void logger_init(FILE *log_file, log_level_t min_level) {
    pthread_mutex_lock(&g_logger_mutex);
    g_log_file = log_file;
    g_min_level = min_level;
    pthread_mutex_unlock(&g_logger_mutex);
}

void logger_set_level(log_level_t level) {
    pthread_mutex_lock(&g_logger_mutex);
    g_min_level = level;
    pthread_mutex_unlock(&g_logger_mutex);
}

void logger_log(log_level_t level, const char *tag, const char *fmt, ...) {
    if (level < g_min_level) {
        return;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_info);

    pthread_mutex_lock(&g_logger_mutex);

    FILE *out = g_log_file ? g_log_file : (level == LOG_LEVEL_ERROR ? stderr : stdout);
    int is_tty = isatty(fileno(out));

    if (is_tty) {
        /* Colored terminal output */
        fprintf(out, "%s.%03ld [%s%s%s] [%s%-8s%s] ",
                timestamp,
                (long)(tv.tv_usec / 1000),
                level_to_color(level),
                level_to_string(level),
                COLOR_RESET,
                COLOR_TAG,
                tag ? tag : "APP",
                COLOR_RESET);
    } else {
        /* Plain file / redirection output */
        fprintf(out, "%s.%03ld [%s] [%-8s] ",
                timestamp,
                (long)(tv.tv_usec / 1000),
                level_to_string(level),
                tag ? tag : "APP");
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fprintf(out, "\n");
    fflush(out);

    pthread_mutex_unlock(&g_logger_mutex);
}

