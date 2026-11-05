/**
 * @file logger.h
 * @brief Thread-safe colored logging subsystem for MiniDrop.
 */

#ifndef MINIDROP_LOGGER_H
#define MINIDROP_LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

/**
 * @brief Initialize the logger subsystem with an optional target file.
 * @param log_file File pointer to output logs, or NULL to log to stdout/stderr.
 * @param min_level Minimum severity level to output.
 */
void logger_init(FILE *log_file, log_level_t min_level);

/**
 * @brief Set the minimum logging severity level at runtime.
 * @param level The new minimum level.
 */
void logger_set_level(log_level_t level);

/**
 * @brief Core logging function. Thread-safe via internal mutex.
 * @param level Severity level of the log message.
 * @param tag Component or source tag (e.g. "SERVER", "WATCHER", "SYNC").
 * @param fmt Printf-style format string.
 */
void logger_log(log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/**
 * @brief Convenience macros for formatted logging.
 */
#define LOG_DEBUG(tag, ...) logger_log(LOG_LEVEL_DEBUG, tag, __VA_ARGS__)
#define LOG_INFO(tag, ...)  logger_log(LOG_LEVEL_INFO,  tag, __VA_ARGS__)
#define LOG_WARN(tag, ...)  logger_log(LOG_LEVEL_WARN,  tag, __VA_ARGS__)
#define LOG_ERROR(tag, ...) logger_log(LOG_LEVEL_ERROR, tag, __VA_ARGS__)

#endif /* MINIDROP_LOGGER_H */

