/**
 * @file test_checksum.c
 * @brief Unit tests for SHA-256 cryptographic checksum routines.
 */

#include "checksum.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

void test_buffer_checksum(void) {
    printf("[TEST] Testing buffer SHA-256 calculation against known NIST vectors...\n");

    /* Vector 1: Empty string */
    const char *empty = "";
    const char *expected_empty_hash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    char hash[65];

    int res1 = checksum_calculate_buffer(empty, 0, hash);
    TEST_ASSERT(res1 == 0, "Empty buffer hash returns 0");
    TEST_ASSERT(strcmp(hash, expected_empty_hash) == 0, "Empty buffer produces standard NIST SHA-256");

    /* Vector 2: "abc" */
    const char *abc = "abc";
    const char *expected_abc_hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    int res2 = checksum_calculate_buffer(abc, 3, hash);
    TEST_ASSERT(res2 == 0, "'abc' buffer hash returns 0");
    TEST_ASSERT(strcmp(hash, expected_abc_hash) == 0, "'abc' produces exact standard SHA-256");
}

void test_file_checksum(void) {
    printf("[TEST] Testing file streaming SHA-256 calculation...\n");

    /* Create temporary test file with known content */
    const char *tmp_path = "/tmp/minidrop_test_file.txt";
    FILE *f = fopen(tmp_path, "wb");
    TEST_ASSERT(f != NULL, "Created temporary test file");

    const char *content = "abc";
    fwrite(content, 1, 3, f);
    fclose(f);

    char file_hash[65];
    int res = checksum_calculate_file(tmp_path, file_hash);
    TEST_ASSERT(res == 0, "File checksum returns 0");

    const char *expected_abc_hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    TEST_ASSERT(strcmp(file_hash, expected_abc_hash) == 0, "File streaming produces identical hash to buffer");

    unlink(tmp_path);
}

int main(void) {
    printf("=========================================\n");
    printf("   MiniDrop Checksum Unit Test Suite     \n");
    printf("=========================================\n");

    test_buffer_checksum();
    test_file_checksum();

    printf("-----------------------------------------\n");
    printf("Results: %d Passed, %d Failed\n", g_tests_passed, g_tests_failed);
    printf("=========================================\n");

    return (g_tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

