/**
 * @file checksum.c
 * @brief SHA-256 cryptographic checksum calculation implementation.
 */

#include "checksum.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/evp.h>

#define CHUNK_SIZE 65536 /* 64 KB streaming buffer */

static void bytes_to_hex(const unsigned char *md, unsigned int len, char *hex_output) {
    static const char hex_digits[] = "0123456789abcdef";
    for (unsigned int i = 0; i < len; i++) {
        hex_output[i * 2]     = hex_digits[(md[i] >> 4) & 0x0F];
        hex_output[i * 2 + 1] = hex_digits[md[i] & 0x0F];
    }
    hex_output[len * 2] = '\0';
}

int checksum_calculate_buffer(const void *data, size_t len, char *hex_output) {
    if (!data || !hex_output) return -1;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        LOG_ERROR("CHECKSUM", "EVP_MD_CTX_new failed");
        return -1;
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
        LOG_ERROR("CHECKSUM", "EVP_DigestInit_ex failed");
        EVP_MD_CTX_free(ctx);
        return -1;
    }

    if (EVP_DigestUpdate(ctx, data, len) != 1) {
        LOG_ERROR("CHECKSUM", "EVP_DigestUpdate failed");
        EVP_MD_CTX_free(ctx);
        return -1;
    }

    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;

    if (EVP_DigestFinal_ex(ctx, md_value, &md_len) != 1) {
        LOG_ERROR("CHECKSUM", "EVP_DigestFinal_ex failed");
        EVP_MD_CTX_free(ctx);
        return -1;
    }

    EVP_MD_CTX_free(ctx);
    bytes_to_hex(md_value, md_len, hex_output);
    return 0;
}

int checksum_calculate_file(const char *filepath, char *hex_output) {
    if (!filepath || !hex_output) return -1;

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        LOG_ERROR("CHECKSUM", "Cannot open file '%s': %s (errno=%d)",
                  filepath, strerror(errno), errno);
        return -1;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        LOG_ERROR("CHECKSUM", "EVP_MD_CTX_new failed");
        close(fd);
        return -1;
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
        LOG_ERROR("CHECKSUM", "EVP_DigestInit_ex failed");
        EVP_MD_CTX_free(ctx);
        close(fd);
        return -1;
    }

    unsigned char *buffer = malloc(CHUNK_SIZE);
    if (!buffer) {
        LOG_ERROR("CHECKSUM", "Memory allocation failed for streaming buffer");
        EVP_MD_CTX_free(ctx);
        close(fd);
        return -1;
    }

    ssize_t bytes_read;
    while ((bytes_read = read(fd, buffer, CHUNK_SIZE)) > 0) {
        if (EVP_DigestUpdate(ctx, buffer, (size_t)bytes_read) != 1) {
            LOG_ERROR("CHECKSUM", "EVP_DigestUpdate failed during file read");
            free(buffer);
            EVP_MD_CTX_free(ctx);
            close(fd);
            return -1;
        }
    }

    free(buffer);

    if (bytes_read < 0) {
        LOG_ERROR("CHECKSUM", "Error reading file '%s': %s", filepath, strerror(errno));
        EVP_MD_CTX_free(ctx);
        close(fd);
        return -1;
    }

    close(fd);

    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;

    if (EVP_DigestFinal_ex(ctx, md_value, &md_len) != 1) {
        LOG_ERROR("CHECKSUM", "EVP_DigestFinal_ex failed");
        EVP_MD_CTX_free(ctx);
        return -1;
    }

    EVP_MD_CTX_free(ctx);
    bytes_to_hex(md_value, md_len, hex_output);
    return 0;
}

