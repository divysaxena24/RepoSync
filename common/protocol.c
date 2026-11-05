/**
 * @file protocol.c
 * @brief Application-layer network protocol implementation and TCP socket framing.
 */

#include "protocol.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

void minidrop_header_init(minidrop_header_t *hdr, msg_type_t type, uint32_t req_id,
                          const char *client_id, uint32_t payload_len, status_code_t status) {
    if (!hdr) return;
    memset(hdr, 0, sizeof(minidrop_header_t));
    hdr->magic = MINIDROP_MAGIC;
    hdr->version = MINIDROP_VERSION;
    hdr->msg_type = (uint8_t)type;
    hdr->request_id = req_id;
    hdr->payload_len = payload_len;
    hdr->status_code = (uint32_t)status;

    if (client_id) {
        strncpy(hdr->client_id, client_id, MINIDROP_CLIENT_ID_LEN - 1);
        hdr->client_id[MINIDROP_CLIENT_ID_LEN - 1] = '\0';
    }
}

void minidrop_header_serialize(const minidrop_header_t *src, minidrop_header_t *dst) {
    if (!src || !dst) return;
    dst->magic = htons(src->magic);
    dst->version = src->version;
    dst->msg_type = src->msg_type;
    dst->request_id = htonl(src->request_id);
    dst->payload_len = htonl(src->payload_len);
    dst->status_code = htonl(src->status_code);
    memcpy(dst->client_id, src->client_id, MINIDROP_CLIENT_ID_LEN);
}

int minidrop_header_deserialize(const minidrop_header_t *src, minidrop_header_t *dst) {
    if (!src || !dst) return -1;
    dst->magic = ntohs(src->magic);
    dst->version = src->version;
    dst->msg_type = src->msg_type;
    dst->request_id = ntohl(src->request_id);
    dst->payload_len = ntohl(src->payload_len);
    dst->status_code = ntohl(src->status_code);
    memcpy(dst->client_id, src->client_id, MINIDROP_CLIENT_ID_LEN);
    dst->client_id[MINIDROP_CLIENT_ID_LEN - 1] = '\0';

    if (dst->magic != MINIDROP_MAGIC) {
        LOG_ERROR("PROTOCOL", "Magic mismatch: expected 0x%04X, got 0x%04X",
                  MINIDROP_MAGIC, dst->magic);
        return -1;
    }

    if (dst->version != MINIDROP_VERSION) {
        LOG_ERROR("PROTOCOL", "Version mismatch: expected %d, got %d",
                  MINIDROP_VERSION, dst->version);
        return -1;
    }

    return 0;
}

ssize_t minidrop_send_all(int sockfd, const void *buf, size_t len) {
    size_t total_sent = 0;
    const char *ptr = (const char *)buf;

    while (total_sent < len) {
        ssize_t n = send(sockfd, ptr + total_sent, len - total_sent, 0);
        if (n > 0) {
            total_sent += (size_t)n;
        } else if (n < 0) {
            if (errno == EINTR) {
                continue; /* Interrupted by signal, retry */
            }
            LOG_ERROR("NETWORK", "send() failed: %s (errno=%d)", strerror(errno), errno);
            return -1;
        } else {
            /* Peer closed connection unexpectedly */
            LOG_WARN("NETWORK", "send() returned 0 (connection closed)");
            return -1;
        }
    }

    return (ssize_t)total_sent;
}

ssize_t minidrop_recv_all(int sockfd, void *buf, size_t len) {
    size_t total_recv = 0;
    char *ptr = (char *)buf;

    while (total_recv < len) {
        ssize_t n = recv(sockfd, ptr + total_recv, len - total_recv, 0);
        if (n > 0) {
            total_recv += (size_t)n;
        } else if (n == 0) {
            /* Graceful EOF from peer */
            if (total_recv == 0) {
                return 0; /* Clean disconnect at message boundary */
            }
            LOG_WARN("NETWORK", "recv() connection closed unexpectedly mid-stream (got %zu/%zu bytes)",
                     total_recv, len);
            return -1;
        } else {
            if (errno == EINTR) {
                continue; /* Interrupted by signal, retry */
            }
            LOG_ERROR("NETWORK", "recv() failed: %s (errno=%d)", strerror(errno), errno);
            return -1;
        }
    }

    return (ssize_t)total_recv;
}

int minidrop_send_msg(int sockfd, const minidrop_header_t *hdr, const void *payload) {
    if (sockfd < 0 || !hdr) {
        return -1;
    }

    minidrop_header_t wire_hdr;
    minidrop_header_serialize(hdr, &wire_hdr);

    /* 1. Transmit the fixed 32-byte header */
    if (minidrop_send_all(sockfd, &wire_hdr, sizeof(wire_hdr)) != (ssize_t)sizeof(wire_hdr)) {
        LOG_ERROR("PROTOCOL", "Failed to send header (type=%s, req_id=%u)",
                  minidrop_msg_type_str(hdr->msg_type), hdr->request_id);
        return -1;
    }

    /* 2. Transmit the variable-length payload if present */
    if (hdr->payload_len > 0) {
        if (!payload) {
            LOG_ERROR("PROTOCOL", "payload_len=%u but payload buffer is NULL", hdr->payload_len);
            return -1;
        }
        if (minidrop_send_all(sockfd, payload, hdr->payload_len) != (ssize_t)hdr->payload_len) {
            LOG_ERROR("PROTOCOL", "Failed to send payload of %u bytes", hdr->payload_len);
            return -1;
        }
    }

    return 0;
}

int minidrop_recv_msg(int sockfd, minidrop_header_t *hdr, void **payload_out) {
    if (sockfd < 0 || !hdr) {
        return -1;
    }

    minidrop_header_t wire_hdr;
    ssize_t n = minidrop_recv_all(sockfd, &wire_hdr, sizeof(wire_hdr));
    if (n == 0) {
        return 1; /* Peer closed connection cleanly */
    }
    if (n != (ssize_t)sizeof(wire_hdr)) {
        LOG_ERROR("PROTOCOL", "Failed to receive complete 32-byte header");
        return -1;
    }

    if (minidrop_header_deserialize(&wire_hdr, hdr) != 0) {
        return -1;
    }

    /* Safeguard against malicious or corrupted payload sizes */
    if (hdr->payload_len > MINIDROP_MAX_PAYLOAD) {
        LOG_ERROR("PROTOCOL", "Payload length %u exceeds safety limit %d",
                  hdr->payload_len, MINIDROP_MAX_PAYLOAD);
        return -1;
    }

    if (payload_out) {
        if (hdr->payload_len > 0) {
            void *payload = malloc(hdr->payload_len + 1);
            if (!payload) {
                LOG_ERROR("PROTOCOL", "Memory allocation failed for payload (%u bytes)", hdr->payload_len);
                return -1;
            }

            if (minidrop_recv_all(sockfd, payload, hdr->payload_len) != (ssize_t)hdr->payload_len) {
                LOG_ERROR("PROTOCOL", "Failed to receive complete payload of %u bytes", hdr->payload_len);
                free(payload);
                return -1;
            }

            ((char *)payload)[hdr->payload_len] = '\0';
            *payload_out = payload;
        } else {
            *payload_out = NULL;
        }
    }

    return 0;
}

const char *minidrop_msg_type_str(msg_type_t type) {
    switch (type) {
        case MSG_AUTH_REQUEST:        return "AUTH_REQUEST";
        case MSG_AUTH_RESPONSE:       return "AUTH_RESPONSE";
        case MSG_PING:                return "PING";
        case MSG_PONG:                return "PONG";
        case MSG_SYNC_REQUEST:        return "SYNC_REQUEST";
        case MSG_SYNC_RESPONSE:       return "SYNC_RESPONSE";
        case MSG_FILE_CREATE:         return "FILE_CREATE";
        case MSG_FILE_UPDATE:         return "FILE_UPDATE";
        case MSG_FILE_DELETE:         return "FILE_DELETE";
        case MSG_FILE_RENAME:         return "FILE_RENAME";
        case MSG_DIRECTORY_CREATE:    return "DIRECTORY_CREATE";
        case MSG_DIRECTORY_DELETE:    return "DIRECTORY_DELETE";
        case MSG_FILE_TRANSFER_START: return "FILE_TRANSFER_START";
        case MSG_FILE_TRANSFER_CHUNK: return "FILE_TRANSFER_CHUNK";
        case MSG_FILE_TRANSFER_END:   return "FILE_TRANSFER_END";
        case MSG_ACK:                 return "ACK";
        case MSG_ERROR:               return "ERROR";
        default:                      return "UNKNOWN_MSG";
    }
}

const char *minidrop_status_str(status_code_t status) {
    switch (status) {
        case STATUS_OK:               return "STATUS_OK";
        case STATUS_ERR_AUTH:         return "STATUS_ERR_AUTH";
        case STATUS_ERR_NOT_FOUND:    return "STATUS_ERR_NOT_FOUND";
        case STATUS_ERR_EXISTS:       return "STATUS_ERR_EXISTS";
        case STATUS_ERR_CONFLICT:     return "STATUS_ERR_CONFLICT";
        case STATUS_ERR_CHECKSUM:     return "STATUS_ERR_CHECKSUM";
        case STATUS_ERR_INVALID_PATH: return "STATUS_ERR_INVALID_PATH";
        case STATUS_ERR_IO:           return "STATUS_ERR_IO";
        case STATUS_ERR_PROTOCOL:     return "STATUS_ERR_PROTOCOL";
        case STATUS_ERR_INTERNAL:     return "STATUS_ERR_INTERNAL";
        default:                      return "UNKNOWN_STATUS";
    }
}

