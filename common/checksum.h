/**
 * @file checksum.h
 * @brief SHA-256 cryptographic checksum calculation routines using OpenSSL EVP.
 */

#ifndef MINIDROP_CHECKSUM_H
#define MINIDROP_CHECKSUM_H

#include <stddef.h>

#define SHA256_HEX_LEN 64 /* 32 bytes in hexadecimal = 64 chars */

/**
 * @brief Calculate SHA-256 digest of a memory buffer.
 * @param data Pointer to buffer.
 * @param len Buffer length in bytes.
 * @param hex_output Buffer of at least 65 bytes (64 hex chars + null terminator).
 * @return 0 on success, -1 on failure.
 */
int checksum_calculate_buffer(const void *data, size_t len, char *hex_output);

/**
 * @brief Calculate SHA-256 digest of a file on disk by streaming chunks.
 * Memory-efficient: uses a fixed 64 KB buffer, preventing high RAM usage for large files.
 * @param filepath Path to the file.
 * @param hex_output Buffer of at least 65 bytes (64 hex chars + null terminator).
 * @return 0 on success, -1 on failure (e.g. file not found or permission error).
 */
int checksum_calculate_file(const char *filepath, char *hex_output);

#endif /* MINIDROP_CHECKSUM_H */

