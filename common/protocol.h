/**
 * @file protocol.h
 * @brief Application-layer binary network protocol definitions for MiniDrop.
 */

#ifndef MINIDROP_PROTOCOL_H
#define MINIDROP_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define MINIDROP_MAGIC          0x4D44  /* "MD" in ASCII */
#define MINIDROP_VERSION        1
#define MINIDROP_HEADER_SIZE    32
#define MINIDROP_CLIENT_ID_LEN  16
#define MINIDROP_MAX_CHUNK_SIZE 65536   /* 64 KB per stream chunk */
#define MINIDROP_MAX_PATH_LEN   4096
#define MINIDROP_MAX_PAYLOAD    (16 * 1024 * 1024) /* 16 MB max single payload safeguard */

/**
 * @brief Protocol Message Types
 */
typedef enum {
    /* Authentication & Session */
    MSG_AUTH_REQUEST        = 0x01,
    MSG_AUTH_RESPONSE       = 0x02,
    MSG_PING                = 0x03,
    MSG_PONG                = 0x04,

    /* Directory Inventory & Reconciliation */
    MSG_SYNC_REQUEST        = 0x10,
    MSG_SYNC_RESPONSE       = 0x11,

    /* File System Change Notifications */
    MSG_FILE_CREATE         = 0x20,
    MSG_FILE_UPDATE         = 0x21,
    MSG_FILE_DELETE         = 0x22,
    MSG_FILE_RENAME         = 0x23,
    MSG_DIRECTORY_CREATE    = 0x24,
    MSG_DIRECTORY_DELETE    = 0x25,

    /* Chunked File Transfer Streaming */
    MSG_FILE_TRANSFER_START = 0x26,
    MSG_FILE_TRANSFER_CHUNK = 0x27,
    MSG_FILE_TRANSFER_END   = 0x28,

    /* Control Responses */
    MSG_ACK                 = 0xE0,
    MSG_ERROR               = 0xFF
} msg_type_t;

/**
 * @brief Protocol Status and Error Codes
 */
typedef enum {
    STATUS_OK               = 0,
    STATUS_ERR_AUTH         = 1,
    STATUS_ERR_NOT_FOUND    = 2,
    STATUS_ERR_EXISTS       = 3,
    STATUS_ERR_CONFLICT     = 4,
    STATUS_ERR_CHECKSUM     = 5,
    STATUS_ERR_INVALID_PATH = 6,
    STATUS_ERR_IO           = 7,
    STATUS_ERR_PROTOCOL     = 8,
    STATUS_ERR_INTERNAL     = 9
} status_code_t;

/**
 * @brief Binary Fixed Header Structure (32 Bytes total)
 * Packed to ensure zero compiler-inserted alignment padding.
 */
#pragma pack(push, 1)
typedef struct {
    uint16_t magic;                          /* 0x00: 0x4D44 ("MD") */
    uint8_t  version;                        /* 0x02: Protocol version (1) */
    uint8_t  msg_type;                       /* 0x03: Message type enum */
    uint32_t request_id;                     /* 0x04: Request/Sequence ID */
    uint32_t payload_len;                    /* 0x08: Size of payload following header */
    uint32_t status_code;                    /* 0x0C: Status or error code */
    char     client_id[MINIDROP_CLIENT_ID_LEN]; /* 0x10: Client identifier string (null-padded) */
} minidrop_header_t;
#pragma pack(pop)

/**
 * @brief Initialize a header structure with standard default values.
 */
void minidrop_header_init(minidrop_header_t *hdr, msg_type_t type, uint32_t req_id,
                          const char *client_id, uint32_t payload_len, status_code_t status);

/**
 * @brief Serialize header fields into network byte order (Big Endian).
 */
void minidrop_header_serialize(const minidrop_header_t *src, minidrop_header_t *dst);

/**
 * @brief Deserialize header fields from network byte order into host byte order.
 * Performs validation of magic number and protocol version.
 * @return 0 on success, -1 on invalid magic or version.
 */
int minidrop_header_deserialize(const minidrop_header_t *src, minidrop_header_t *dst);

/**
 * @brief Send exact number of bytes over TCP socket, handling partial writes.
 * @return Number of bytes sent, or -1 on network error / disconnection.
 */
ssize_t minidrop_send_all(int sockfd, const void *buf, size_t len);

/**
 * @brief Receive exact number of bytes from TCP socket, handling partial reads.
 * @return Number of bytes received, 0 on peer graceful shutdown (EOF), or -1 on error.
 */
ssize_t minidrop_recv_all(int sockfd, void *buf, size_t len);

/**
 * @brief High-level helper: Send a complete MiniDrop framed message (Header + Payload).
 * @return 0 on success, -1 on socket failure.
 */
int minidrop_send_msg(int sockfd, const minidrop_header_t *hdr, const void *payload);

/**
 * @brief High-level helper: Receive a complete MiniDrop framed message.
 * Allocates memory for *payload_out if payload_len > 0. Caller must free(*payload_out).
 * @return 0 on success, 1 on peer EOF, -1 on protocol/network error.
 */
int minidrop_recv_msg(int sockfd, minidrop_header_t *hdr, void **payload_out);

/**
 * @brief Helper string converters for logging.
 */
const char *minidrop_msg_type_str(msg_type_t type);
const char *minidrop_status_str(status_code_t status);

#endif /* MINIDROP_PROTOCOL_H */

