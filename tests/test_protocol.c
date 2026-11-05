/**
 * @file test_protocol.c
 * @brief Unit tests for MiniDrop binary protocol framing, serialization, and socket I/O.
 */

#include "protocol.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); \
        g_tests_passed++; \
    } else { \
        printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
        g_tests_failed++; \
    } \
} while (0)

void test_header_size(void) {
    printf("[TEST] Checking exact 32-byte header alignment...\n");
    TEST_ASSERT(sizeof(minidrop_header_t) == 32, "minidrop_header_t is exactly 32 bytes");
}

void test_serialization(void) {
    printf("[TEST] Testing header serialization and deserialization...\n");

    minidrop_header_t original;
    minidrop_header_init(&original, MSG_FILE_CREATE, 1001, "client_alpha", 4096, STATUS_OK);

    minidrop_header_t wire;
    minidrop_header_serialize(&original, &wire);

    /* Verify wire format differs from host format on little-endian x86_64 */
    TEST_ASSERT(wire.version == 1, "Wire version is 1");
    TEST_ASSERT(wire.msg_type == (uint8_t)MSG_FILE_CREATE, "Wire msg_type matches");

    minidrop_header_t restored;
    int res = minidrop_header_deserialize(&wire, &restored);
    TEST_ASSERT(res == 0, "Header deserialization returns 0 (success)");
    TEST_ASSERT(restored.magic == MINIDROP_MAGIC, "Restored magic is 0x4D44");
    TEST_ASSERT(restored.version == MINIDROP_VERSION, "Restored version is 1");
    TEST_ASSERT(restored.msg_type == MSG_FILE_CREATE, "Restored msg_type is MSG_FILE_CREATE");
    TEST_ASSERT(restored.request_id == 1001, "Restored request_id is 1001");
    TEST_ASSERT(restored.payload_len == 4096, "Restored payload_len is 4096");
    TEST_ASSERT(restored.status_code == STATUS_OK, "Restored status_code is STATUS_OK");
    TEST_ASSERT(strcmp(restored.client_id, "client_alpha") == 0, "Restored client_id matches");
}

void test_corrupt_header_rejection(void) {
    printf("[TEST] Testing corrupt magic and version rejection...\n");

    minidrop_header_t original;
    minidrop_header_init(&original, MSG_PING, 42, "test_client", 0, STATUS_OK);

    minidrop_header_t wire;
    minidrop_header_serialize(&original, &wire);

    /* Corrupt magic */
    wire.magic = 0xFFFF;
    minidrop_header_t bad_hdr;
    int res_magic = minidrop_header_deserialize(&wire, &bad_hdr);
    TEST_ASSERT(res_magic == -1, "Deserialization rejects corrupt magic");

    /* Restore magic, corrupt version */
    wire.magic = htons(MINIDROP_MAGIC);
    wire.version = 99;
    int res_ver = minidrop_header_deserialize(&wire, &bad_hdr);
    TEST_ASSERT(res_ver == -1, "Deserialization rejects unsupported version");
}

void test_socket_framing(void) {
    printf("[TEST] Testing socket transmission framing with socketpair()...\n");

    int sv[2];
    int res = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    TEST_ASSERT(res == 0, "socketpair() created successfully");

    const char *payload_text = "MiniDrop Sync Engine Payload: Chunk #001 Integrity Test Data.";
    uint32_t payload_len = (uint32_t)strlen(payload_text) + 1;

    minidrop_header_t send_hdr;
    minidrop_header_init(&send_hdr, MSG_FILE_TRANSFER_CHUNK, 555, "client_beta", payload_len, STATUS_OK);

    /* Send message across sv[0] */
    int send_res = minidrop_send_msg(sv[0], &send_hdr, payload_text);
    TEST_ASSERT(send_res == 0, "minidrop_send_msg() transmitted without error");

    /* Receive message on sv[1] */
    minidrop_header_t recv_hdr;
    void *recv_payload = NULL;
    int recv_res = minidrop_recv_msg(sv[1], &recv_hdr, &recv_payload);
    TEST_ASSERT(recv_res == 0, "minidrop_recv_msg() received frame without error");

    TEST_ASSERT(recv_hdr.msg_type == MSG_FILE_TRANSFER_CHUNK, "Received msg_type matches");
    TEST_ASSERT(recv_hdr.request_id == 555, "Received request_id matches");
    TEST_ASSERT(recv_hdr.payload_len == payload_len, "Received payload_len matches");
    TEST_ASSERT(strcmp(recv_hdr.client_id, "client_beta") == 0, "Received client_id matches");
    TEST_ASSERT(recv_payload != NULL, "Received payload buffer is non-NULL");
    if (recv_payload) {
        TEST_ASSERT(strcmp((const char *)recv_payload, payload_text) == 0,
                    "Payload content matches transmitted bytes exactly");
        free(recv_payload);
    }

    close(sv[0]);
    close(sv[1]);
}

int main(void) {
    logger_init(NULL, LOG_LEVEL_WARN); /* Keep test output clean */

    printf("=========================================\n");
    printf("   MiniDrop Protocol Unit Test Suite     \n");
    printf("=========================================\n");

    test_header_size();
    test_serialization();
    test_corrupt_header_rejection();
    test_socket_framing();

    printf("-----------------------------------------\n");
    printf("Results: %d Passed, %d Failed\n", g_tests_passed, g_tests_failed);
    printf("=========================================\n");

    return (g_tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
