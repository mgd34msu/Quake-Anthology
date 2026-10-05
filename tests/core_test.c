#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "qa/arena.h"
#include "qa/binary.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void test_errors_and_buffers(void)
{
    qa_error error;
    qa_error_set(&error, QA_ERROR_FORMAT, 19, "bad %s: %d", "input", 7);
    CHECK(error.code == QA_ERROR_FORMAT);
    CHECK(error.offset == 19);
    CHECK(strcmp(error.message, "bad input: 7") == 0);

    char long_message[512];
    memset(long_message, 'x', sizeof(long_message));
    long_message[sizeof(long_message) - 1] = '\0';
    qa_error_set(&error, QA_ERROR_IO, 0, "%s", long_message);
    CHECK(strlen(error.message) == sizeof(error.message) - 1);
    qa_error_set(&error, QA_OK, 0, NULL);
    CHECK(error.code == QA_OK && error.offset == 0 && error.message[0] == '\0');
    qa_error_set(NULL, QA_ERROR_IO, 0, "unused");

    qa_buffer buffer = {.data = malloc(32), .size = 32};
    CHECK(buffer.data != NULL);
    qa_buffer_free(&buffer);
    CHECK(buffer.data == NULL && buffer.size == 0);
    qa_buffer_free(&buffer);
    qa_buffer_free(NULL);
}

static void test_binary(void)
{
    /* All multi-byte reads and writes deliberately use unaligned addresses. */
    uint8_t storage[18] = {0};
    qa_store_u16le(storage + 1, UINT16_C(0x89ab));
    CHECK(storage[1] == 0xab && storage[2] == 0x89);
    CHECK(qa_load_u16le(storage + 1) == UINT16_C(0x89ab));
    qa_store_u32le(storage + 1, UINT32_C(0x89abcdef));
    CHECK(storage[1] == 0xef && storage[2] == 0xcd && storage[3] == 0xab && storage[4] == 0x89);
    CHECK(qa_load_u32le(storage + 1) == UINT32_C(0x89abcdef));
    qa_store_u64le(storage + 1, UINT64_C(0xfedcba9876543210));
    const uint8_t expected[] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe};
    CHECK(memcmp(storage + 1, expected, sizeof(expected)) == 0);
    CHECK(qa_load_u64le(storage + 1) == UINT64_C(0xfedcba9876543210));
    CHECK(storage[0] == 0 && storage[9] == 0);

    const uint16_t signed16_bits[] = {0, INT16_MAX, UINT16_C(0x8000), UINT16_MAX};
    const int16_t signed16_values[] = {0, INT16_MAX, INT16_MIN, -1};
    for (size_t i = 0; i < sizeof(signed16_bits) / sizeof(signed16_bits[0]); i++) {
        qa_store_u16le(storage + 1, signed16_bits[i]);
        CHECK(qa_load_i16le(storage + 1) == signed16_values[i]);
    }
    const uint32_t signed32_bits[] = {0, INT32_MAX, UINT32_C(0x80000000), UINT32_MAX};
    const int32_t signed32_values[] = {0, INT32_MAX, INT32_MIN, -1};
    for (size_t i = 0; i < sizeof(signed32_bits) / sizeof(signed32_bits[0]); i++) {
        qa_store_u32le(storage + 1, signed32_bits[i]);
        CHECK(qa_load_i32le(storage + 1) == signed32_values[i]);
    }

    const uint32_t float_bits[] = {
        0, UINT32_C(0x80000000), UINT32_C(0x3f800000), UINT32_C(0xc1200000),
        UINT32_C(0x00000001), UINT32_C(0x007fffff), UINT32_C(0x7f7fffff),
        UINT32_C(0x7f800000), UINT32_C(0xff800000), UINT32_C(0x7fcabcde)
    };
    for (size_t i = 0; i < sizeof(float_bits) / sizeof(float_bits[0]); i++) {
        qa_store_u32le(storage + 1, float_bits[i]);
        float value = qa_load_f32le(storage + 1);
        uint32_t actual;
        memcpy(&actual, &value, sizeof(actual));
        CHECK(actual == float_bits[i]);
    }
    qa_store_u32le(storage + 1, UINT32_C(0x3f800000));
    CHECK(qa_load_f32le(storage + 1) == 1.0f);
    qa_store_u32le(storage + 1, UINT32_C(0xc1200000));
    CHECK(qa_load_f32le(storage + 1) == -10.0f);
}

static void test_spans(void)
{
    uint8_t data[8] = {0};
    qa_bytes source = {data, sizeof(data)};
    qa_bytes result = {0};
    qa_error error = {0};
    CHECK(qa_bytes_slice(source, 2, 6, &result, &error));
    CHECK(result.data == data + 2 && result.size == 6);
    CHECK(qa_bytes_slice(source, sizeof(data), 0, &result, &error));
    CHECK(result.data == data + sizeof(data) && result.size == 0);
    const size_t bad_ranges[][2] = {
        {9, 0}, {8, 1}, {0, 9}, {1, SIZE_MAX}, {SIZE_MAX, 1}, {SIZE_MAX, SIZE_MAX}
    };
    for (size_t i = 0; i < sizeof(bad_ranges) / sizeof(bad_ranges[0]); i++) {
        CHECK(!qa_bytes_slice(source, bad_ranges[i][0], bad_ranges[i][1], &result, &error));
        CHECK(error.code == QA_ERROR_FORMAT && error.offset == bad_ranges[i][0]);
        CHECK(result.data == data + sizeof(data) && result.size == 0);
    }
    CHECK(qa_bytes_slice((qa_bytes){0}, 0, 0, &result, &error));
    CHECK(result.data == NULL && result.size == 0);
    CHECK(!qa_bytes_slice((qa_bytes){NULL, 1}, 0, 0, &result, &error));
    CHECK(error.code == QA_ERROR_ARGUMENT);
    CHECK(!qa_bytes_slice(source, 0, 1, NULL, &error));
    CHECK(error.code == QA_ERROR_ARGUMENT);
    CHECK(!qa_bytes_slice(source, SIZE_MAX, SIZE_MAX, &result, NULL));
}

static void test_arena(void)
{
    qa_arena arena = {0};
    qa_error error = {0};
    uint8_t *first = qa_arena_alloc(&arena, 17, 1, &error);
    CHECK(first != NULL);
    memset(first, 0x5a, 17);
    for (size_t alignment = 1; alignment <= 4096; alignment *= 2) {
        void *allocation = qa_arena_alloc(&arena, 53, alignment, &error);
        CHECK(allocation != NULL);
        CHECK((uintptr_t)allocation % alignment == 0);
        memset(allocation, 0xa5, 53);
    }
    uint8_t *large = qa_arena_alloc(&arena, 65536, 64, &error);
    CHECK(large != NULL && (uintptr_t)large % 64 == 0);
    memset(large, 0x33, 65536);
    for (size_t i = 0; i < 17; i++) {
        CHECK(first[i] == 0x5a);
    }
    CHECK(qa_arena_alloc(&arena, SIZE_MAX, 16, &error) == NULL);
    CHECK(error.code == QA_ERROR_MEMORY);
    CHECK(qa_arena_alloc(&arena, SIZE_MAX, 1, &error) == NULL);
    CHECK(error.code == QA_ERROR_MEMORY);
    CHECK(qa_arena_alloc(&arena, 8, 3, &error) == NULL);
    CHECK(error.code == QA_ERROR_ARGUMENT);
    CHECK(qa_arena_alloc(&arena, 8, 0, &error) == NULL);
    CHECK(qa_arena_alloc(&arena, 0, 8, &error) == NULL);
    CHECK(qa_arena_alloc(NULL, 1, 1, &error) == NULL);
    CHECK(first[0] == 0x5a && large[65535] == 0x33);

    qa_arena_reset(&arena);
    CHECK(qa_arena_alloc(&arena, 17, 1, &error) == first);
    qa_arena_destroy(&arena);
    CHECK(arena.first == NULL && arena.current == NULL && arena.block_size == 0);
    qa_arena_destroy(&arena);
    qa_arena_reset(&arena);

    qa_arena_init(&arena, 32);
    void *small[3];
    for (size_t i = 0; i < 3; i++) {
        small[i] = qa_arena_alloc(&arena, 32, 1, &error);
        CHECK(small[i] != NULL);
        memset(small[i], (int)i, 32);
    }
    qa_arena_reset(&arena);
    for (size_t i = 0; i < 3; i++) {
        CHECK(qa_arena_alloc(&arena, 32, 1, &error) == small[i]);
    }
    qa_arena_destroy(&arena);
    qa_arena_init(&arena, SIZE_MAX);
    CHECK(qa_arena_alloc(&arena, 1, 1, &error) == NULL);
    CHECK(arena.first == NULL && arena.current == NULL);
    qa_arena_destroy(&arena);
    qa_arena_destroy(NULL);
    qa_arena_reset(NULL);
    qa_arena_init(NULL, 0);
}

static void test_files(void)
{
    char directory[] = "/tmp/qa-core-test-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[128], link_path[128], fifo_path[128], missing_path[128];
    CHECK(snprintf(path, sizeof(path), "%s/data", directory) > 0);
    CHECK(snprintf(link_path, sizeof(link_path), "%s/link", directory) > 0);
    CHECK(snprintf(fifo_path, sizeof(fifo_path), "%s/fifo", directory) > 0);
    CHECK(snprintf(missing_path, sizeof(missing_path), "%s/missing", directory) > 0);
    int descriptor = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    CHECK(descriptor >= 0);
    qa_buffer buffer = {0};
    qa_error error = {0};
    CHECK(qa_file_read_all(path, &buffer, &error));
    CHECK(buffer.data == NULL && buffer.size == 0);

    const uint8_t contents[] = {0, 1, 0xfe, 0xff, 13, 10, 0, 42};
    CHECK(write(descriptor, contents, sizeof(contents)) == (ssize_t)sizeof(contents));
    CHECK(qa_file_read_all(path, &buffer, &error));
    CHECK(buffer.size == sizeof(contents) && memcmp(buffer.data, contents, sizeof(contents)) == 0);
    qa_buffer previous = buffer;
    CHECK(!qa_file_read_all(missing_path, &buffer, &error));
    CHECK(error.code == QA_ERROR_NOT_FOUND && buffer.data == previous.data && buffer.size == previous.size);
    CHECK(!qa_file_read_all(directory, &buffer, &error));
    CHECK(error.code == QA_ERROR_IO && buffer.data == previous.data);
    CHECK(mkfifo(fifo_path, 0600) == 0);
    CHECK(!qa_file_read_all(fifo_path, &buffer, &error));
    CHECK(error.code == QA_ERROR_IO && buffer.data == previous.data);
    CHECK(!qa_file_read_all(NULL, &buffer, &error));
    CHECK(error.code == QA_ERROR_ARGUMENT && buffer.data == previous.data);
    CHECK(!qa_file_read_all("", &buffer, &error));
    CHECK(!qa_file_read_all(path, NULL, &error));
    CHECK(!qa_file_read_all(missing_path, &buffer, NULL));
    qa_buffer_free(&buffer);

    CHECK(symlink(path, link_path) == 0);
    CHECK(qa_file_read_all(link_path, &buffer, &error));
    CHECK(buffer.size == sizeof(contents) && memcmp(buffer.data, contents, sizeof(contents)) == 0);
    qa_buffer_free(&buffer);
    CHECK(close(descriptor) == 0);
    CHECK(unlink(link_path) == 0);
    CHECK(unlink(path) == 0);
    CHECK(unlink(fifo_path) == 0);
    CHECK(rmdir(directory) == 0);
}

void test_q1_gameplay(void);
void test_guest(void);

int main(void)
{
    test_errors_and_buffers();
    test_binary();
    test_spans();
    test_arena();
    test_files();
    test_q1_gameplay();
    test_guest();
    puts("core tests passed");
    return EXIT_SUCCESS;
}
