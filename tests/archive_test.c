#define _POSIX_C_SOURCE 200809L
#include "qa/archive.h"
#include "qa/binary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

typedef struct member_fixture {
    size_t local;
    size_t central;
    size_t data;
    size_t descriptor;
    size_t name_length;
    size_t compressed_size;
    size_t size;
    uint32_t crc;
    uint16_t flags;
    uint16_t method;
} member_fixture;

typedef struct zip_fixture {
    uint8_t bytes[8192];
    size_t size;
    size_t prefix;
    size_t central;
    size_t end;
    size_t count;
    member_fixture members[8];
} zip_fixture;

static void append(zip_fixture *zip, const void *data, size_t size)
{
    CHECK(size <= sizeof(zip->bytes) - zip->size);
    if (data == NULL) memset(zip->bytes + zip->size, 0, size);
    else memcpy(zip->bytes + zip->size, data, size);
    zip->size += size;
}

static void add_member(zip_fixture *zip, const char *name, const char *data,
                        uint16_t method, int descriptor)
{
    CHECK(zip->count < sizeof(zip->members) / sizeof(zip->members[0]));
    member_fixture *member = &zip->members[zip->count++];
    member->name_length = strlen(name);
    member->size = strlen(data);
    member->crc = (uint32_t)crc32(0L, (const Bytef *)data, (uInt)member->size);
    member->local = zip->size;
    member->flags = descriptor == 0 ? 0 : 8;
    member->method = method;
    append(zip, NULL, 30);
    append(zip, name, member->name_length);
    member->data = zip->size;
    if (method == 0) {
        append(zip, data, member->size);
    } else {
        z_stream stream = {0};
        CHECK(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                          -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK);
        stream.next_in = (Bytef *)data;
        stream.avail_in = (uInt)member->size;
        stream.next_out = zip->bytes + zip->size;
        stream.avail_out = (uInt)(sizeof(zip->bytes) - zip->size);
        CHECK(deflate(&stream, Z_FINISH) == Z_STREAM_END);
        zip->size += stream.total_out;
        CHECK(deflateEnd(&stream) == Z_OK);
    }
    member->compressed_size = zip->size - member->data;
    uint8_t *header = zip->bytes + member->local;
    qa_store_u32le(header, UINT32_C(0x04034b50));
    qa_store_u16le(header + 4, 20);
    qa_store_u16le(header + 6, member->flags);
    qa_store_u16le(header + 8, method);
    qa_store_u16le(header + 26, (uint16_t)member->name_length);
    if (descriptor == 0) {
        qa_store_u32le(header + 14, member->crc);
        qa_store_u32le(header + 18, (uint32_t)member->compressed_size);
        qa_store_u32le(header + 22, (uint32_t)member->size);
    } else {
        member->descriptor = zip->size;
        if (descriptor == 2) {
            append(zip, NULL, 4);
            qa_store_u32le(zip->bytes + member->descriptor, UINT32_C(0x08074b50));
        }
        size_t record = zip->size;
        append(zip, NULL, 12);
        qa_store_u32le(zip->bytes + record, member->crc);
        qa_store_u32le(zip->bytes + record + 4, (uint32_t)member->compressed_size);
        qa_store_u32le(zip->bytes + record + 8, (uint32_t)member->size);
    }
}

static void finish_zip(zip_fixture *zip, const char *comment)
{
    zip->central = zip->size;
    for (size_t i = 0; i < zip->count; ++i) {
        member_fixture *member = &zip->members[i];
        member->central = zip->size;
        append(zip, NULL, 46);
        append(zip, zip->bytes + member->local + 30, member->name_length);
        uint8_t *record = zip->bytes + member->central;
        qa_store_u32le(record, UINT32_C(0x02014b50));
        qa_store_u16le(record + 4, 20);
        qa_store_u16le(record + 6, 20);
        qa_store_u16le(record + 8, member->flags);
        qa_store_u16le(record + 10, member->method);
        qa_store_u32le(record + 16, member->crc);
        qa_store_u32le(record + 20, (uint32_t)member->compressed_size);
        qa_store_u32le(record + 24, (uint32_t)member->size);
        qa_store_u16le(record + 28, (uint16_t)member->name_length);
        qa_store_u32le(record + 42, (uint32_t)(member->local - zip->prefix));
    }
    zip->end = zip->size;
    append(zip, NULL, 22);
    uint8_t *tail = zip->bytes + zip->end;
    qa_store_u32le(tail, UINT32_C(0x06054b50));
    qa_store_u16le(tail + 8, (uint16_t)zip->count);
    qa_store_u16le(tail + 10, (uint16_t)zip->count);
    qa_store_u32le(tail + 12, (uint32_t)(zip->end - zip->central));
    qa_store_u32le(tail + 16, (uint32_t)(zip->central - zip->prefix));
    qa_store_u16le(tail + 20, (uint16_t)strlen(comment));
    append(zip, comment, strlen(comment));
}

static qa_archive *open_zip(const zip_fixture *zip)
{
    qa_archive *archive = NULL;
    qa_error error = {0};
    if (!qa_archive_open_memory((qa_bytes){zip->bytes, zip->size}, QA_ARCHIVE_ZIP,
                               &archive, &error)) {
        fprintf(stderr, "unexpected ZIP failure at %zu: %s\n", error.offset, error.message);
        exit(EXIT_FAILURE);
    }
    return archive;
}

static void bad_zip(const zip_fixture *zip, qa_status status)
{
    qa_archive *archive = NULL;
    qa_error error = {0};
    CHECK(!qa_archive_open_memory((qa_bytes){zip->bytes, zip->size}, QA_ARCHIVE_ZIP,
                                 &archive, &error));
    CHECK(archive == NULL);
    CHECK(error.code == status);
}

static void expect_data(const qa_archive *archive, size_t ordinal, const char *expected)
{
    qa_archive_data data = {0};
    qa_error error = {0};
    CHECK(qa_archive_read(archive, ordinal, &data, &error));
    CHECK(data.bytes.size == strlen(expected));
    CHECK(memcmp(data.bytes.data, expected, data.bytes.size) == 0);
    qa_archive_data_free(&data);
    CHECK(data.bytes.data == NULL && data.owned.data == NULL && data.bytes.size == 0);
}

static void make_pak(uint8_t *bytes, const char *name)
{
    memset(bytes, 0, 142);
    memcpy(bytes, "PACK", 4);
    qa_store_u32le(bytes + 4, 14);
    qa_store_u32le(bytes + 8, 128);
    bytes[12] = 41;
    bytes[13] = 42;
    for (size_t i = 0; i < 2; ++i) {
        CHECK(strlen(name) <= 56);
        memcpy(bytes + 14 + i * 64, name, strlen(name));
        qa_store_u32le(bytes + 70 + i * 64, (uint32_t)(12 + i));
        qa_store_u32le(bytes + 74 + i * 64, 1);
    }
}

static void test_pak(void)
{
    uint8_t bytes[142];
    make_pak(bytes, "Models\\old\\..\\duplicate.mdl");
    qa_archive *archive = NULL;
    qa_error error = {0};
    CHECK(qa_archive_open_memory((qa_bytes){bytes, sizeof(bytes)}, QA_ARCHIVE_AUTO, &archive, &error));
    CHECK(qa_archive_get_kind(archive) == QA_ARCHIVE_PAK && qa_archive_count(archive) == 2);
    const qa_archive_entry *first = qa_archive_entry_at(archive, 0);
    CHECK(strcmp(first->raw_path, "Models\\old\\..\\duplicate.mdl") == 0);
    CHECK(strcmp(first->path, "Models/duplicate.mdl") == 0);
    const qa_archive_entry *found = NULL;
    CHECK(qa_archive_find(archive, "Models/duplicate.mdl", QA_ARCHIVE_EXACT, 0, &found, &error));
    CHECK(found == first);
    CHECK(qa_archive_find(archive, "Models/duplicate.mdl", QA_ARCHIVE_EXACT, 1, &found, &error));
    CHECK(found->ordinal == 1);
    CHECK(qa_archive_find(archive, "models/duplicate.mdl", QA_ARCHIVE_EXACT, 0, &found, &error));
    CHECK(found == NULL);
    CHECK(qa_archive_find(archive, "models/duplicate.mdl", QA_ARCHIVE_ASCII_INSENSITIVE, 0, &found, &error));
    CHECK(found == first);
    CHECK(qa_archive_find(archive, "missing", QA_ARCHIVE_EXACT, SIZE_MAX, &found, &error) && found == NULL);
    CHECK(!qa_archive_find(archive, "../outside", QA_ARCHIVE_EXACT, 0, &found, &error));
    qa_archive_data data = {0};
    CHECK(qa_archive_read(archive, 0, &data, &error));
    CHECK(data.bytes.data == bytes + 12 && data.bytes.size == 1 && data.owned.data == NULL);
    qa_archive_data_free(&data);
    CHECK(qa_archive_read(archive, 1, &data, &error) && data.bytes.data[0] == 42);
    qa_archive_data_free(&data);
    CHECK(!qa_archive_read(archive, 2, &data, &error) && data.bytes.data == NULL);
    qa_archive_close(archive);
    CHECK(!qa_archive_open_memory((qa_bytes){bytes, 100}, QA_ARCHIVE_PAK, &archive, &error));
    CHECK(archive == NULL);
    qa_store_u32le(bytes + 8, 127);
    CHECK(!qa_archive_open_memory((qa_bytes){bytes, sizeof(bytes)}, QA_ARCHIVE_PAK, &archive, &error));
    make_pak(bytes, "ok");
    qa_store_u32le(bytes + 70, UINT32_MAX);
    CHECK(!qa_archive_open_memory((qa_bytes){bytes, sizeof(bytes)}, QA_ARCHIVE_PAK, &archive, &error));
    const char *unsafe[] = {"../escape", "/root", "a//b", "a/./b", "C:\\file", "a/..", "a/../../b", "a//", ""};
    for (size_t i = 0; i < sizeof(unsafe) / sizeof(unsafe[0]); ++i) {
        make_pak(bytes, unsafe[i]);
        CHECK(!qa_archive_open_memory((qa_bytes){bytes, sizeof(bytes)}, QA_ARCHIVE_PAK, &archive, &error));
        CHECK(error.code == QA_ERROR_FORMAT);
    }
    make_pak(bytes, "dir/");
    CHECK(qa_archive_open_memory((qa_bytes){bytes, sizeof(bytes)}, QA_ARCHIVE_PAK, &archive, &error));
    CHECK(qa_archive_entry_at(archive, 0)->is_directory);
    CHECK(!qa_archive_read(archive, 0, &data, &error));
    qa_archive_close(archive);
    char full_name[57];
    memset(full_name, 'a', 56);
    full_name[56] = '\0';
    make_pak(bytes, full_name);
    CHECK(qa_archive_open_memory((qa_bytes){bytes, sizeof(bytes)}, QA_ARCHIVE_PAK, &archive, &error));
    CHECK(strlen(qa_archive_entry_at(archive, 0)->path) == 56);
    qa_archive_close(archive);
}

static void test_case_folding(void)
{
    uint8_t bytes[142];
    make_pak(bytes, "Models/\xc9lite.mdl");
    qa_archive *archive = NULL;
    qa_error error = {0};
    char *normalized = qa_archive_normalize_path("models\\tank/../ctank/skin.pcx", &error);
    CHECK(normalized != NULL && strcmp(normalized, "models/ctank/skin.pcx") == 0);
    free(normalized);
    CHECK(qa_archive_normalize_path("../outside", &error) == NULL);
    CHECK(qa_archive_normalize_path(NULL, &error) == NULL && error.code == QA_ERROR_ARGUMENT);
    CHECK(qa_archive_paths_equal("Models/skin", "models/SKIN", QA_ARCHIVE_ASCII_INSENSITIVE));
    CHECK(!qa_archive_paths_equal("Models/skin", "models/SKIN", QA_ARCHIVE_EXACT));
    const qa_archive_entry *entry = NULL;
    CHECK(qa_archive_open_memory((qa_bytes){bytes, sizeof(bytes)}, QA_ARCHIVE_PAK, &archive, &error));
    CHECK(qa_archive_find(archive, "models/\xe9lite.mdl", QA_ARCHIVE_ASCII_INSENSITIVE, 0, &entry, &error) && entry == NULL);
    CHECK(qa_archive_find(archive, "models/\xe9lite.mdl", QA_ARCHIVE_CASE_INSENSITIVE, 0, &entry, &error) && entry != NULL);
    qa_archive_close(archive);
    const char *preserved[] = {"\xdf", "\xd7", "\xf7"};
    const char *different[] = {"SS", "\xf7", "\x17"};
    for (size_t i = 0; i < 3; ++i) {
        make_pak(bytes, preserved[i]);
        CHECK(qa_archive_open_memory((qa_bytes){bytes, sizeof(bytes)}, QA_ARCHIVE_PAK, &archive, &error));
        CHECK(qa_archive_find(archive, preserved[i], QA_ARCHIVE_CASE_INSENSITIVE, 0, &entry, &error) && entry != NULL);
        CHECK(qa_archive_find(archive, different[i], QA_ARCHIVE_CASE_INSENSITIVE, 0, &entry, &error) && entry == NULL);
        qa_archive_close(archive);
    }
}

static void test_zip(void)
{
    zip_fixture zip = {0};
    append(&zip, "executable prefix", 17);
    zip.prefix = zip.size;
    add_member(&zip, "Models\\old\\..\\same", "first", 0, 0);
    add_member(&zip, "Models/same", "second second second", 8, 2);
    add_member(&zip, "empty", "", 8, 1);
    add_member(&zip, "dir/", "", 0, 0);
    finish_zip(&zip, "comment with PK\005\006 inside");
    qa_archive *archive = open_zip(&zip);
    CHECK(qa_archive_count(archive) == 4);
    CHECK(strcmp(qa_archive_entry_at(archive, 0)->path, "Models/same") == 0);
    expect_data(archive, 0, "first");
    expect_data(archive, 1, "second second second");
    expect_data(archive, 2, "");
    qa_error error = {0};
    qa_archive_data data = {0};
    CHECK(qa_archive_read(archive, 0, &data, &error));
    CHECK(data.bytes.data == zip.bytes + zip.members[0].data && data.owned.data == NULL);
    qa_archive_data_free(&data);
    CHECK(!qa_archive_read(archive, 3, &data, &error));
    CHECK(qa_archive_read(archive, 1, &data, &error));
    CHECK(data.bytes.data == data.owned.data && data.owned.size == strlen("second second second"));
    qa_archive_close(archive);
    CHECK(memcmp(data.bytes.data, "second second second", data.bytes.size) == 0);
    qa_archive_data_free(&data);

    zip_fixture altered = zip;
    qa_store_u16le(altered.bytes + altered.members[0].local + 6, 0x8000);
    archive = open_zip(&altered);
    expect_data(archive, 0, "first");
    qa_archive_close(archive);

    zip_fixture empty = {0};
    finish_zip(&empty, "");
    archive = open_zip(&empty);
    CHECK(qa_archive_count(archive) == 0);
    qa_archive_close(archive);
    const qa_archive_kind kinds[] = {QA_ARCHIVE_PK3, QA_ARCHIVE_PK4, QA_ARCHIVE_KPF};
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        CHECK(qa_archive_open_memory((qa_bytes){zip.bytes, zip.size}, kinds[i], &archive, &error));
        CHECK(qa_archive_get_kind(archive) == kinds[i]);
        expect_data(archive, 1, "second second second");
        qa_archive_close(archive);
    }

    size_t maximum_tail_size = 22 + UINT16_MAX;
    uint8_t *maximum_comment = calloc(maximum_tail_size, 1);
    CHECK(maximum_comment != NULL);
    qa_store_u32le(maximum_comment, UINT32_C(0x06054b50));
    qa_store_u16le(maximum_comment + 20, UINT16_MAX);
    CHECK(qa_archive_open_memory((qa_bytes){maximum_comment, maximum_tail_size},
                                 QA_ARCHIVE_ZIP, &archive, &error));
    CHECK(qa_archive_count(archive) == 0);
    qa_archive_close(archive);
    free(maximum_comment);
}

static void test_corruption(void)
{
    zip_fixture good = {0};
    add_member(&good, "member", "sample sample sample sample", 8, 2);
    finish_zip(&good, "");
    zip_fixture bad = good;
    bad.size -= 1;
    bad_zip(&bad, QA_ERROR_FORMAT);
    bad = good;
    qa_store_u16le(bad.bytes + bad.end + 4, 1);
    bad_zip(&bad, QA_ERROR_UNSUPPORTED);
    bad = good;
    qa_store_u16le(bad.bytes + bad.end + 8, UINT16_MAX);
    qa_store_u16le(bad.bytes + bad.end + 10, UINT16_MAX);
    bad_zip(&bad, QA_ERROR_UNSUPPORTED);
    bad = good;
    qa_store_u32le(bad.bytes + bad.end + 16, UINT32_MAX - 1);
    bad_zip(&bad, QA_ERROR_FORMAT);
    bad = good;
    qa_store_u16le(bad.bytes + bad.end + 8, 2);
    qa_store_u16le(bad.bytes + bad.end + 10, 2);
    bad_zip(&bad, QA_ERROR_FORMAT);
    const size_t offsets[] = {good.members[0].local, good.members[0].local + 30,
                              good.members[0].descriptor + 8, good.central};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        bad = good;
        bad.bytes[offsets[i]] ^= 1;
        bad_zip(&bad, QA_ERROR_FORMAT);
    }
    bad = good;
    qa_store_u16le(bad.bytes + bad.central + 8, 1);
    bad_zip(&bad, QA_ERROR_UNSUPPORTED);
    bad = good;
    qa_store_u16le(bad.bytes + bad.central + 10, 12);
    bad_zip(&bad, QA_ERROR_UNSUPPORTED);
    bad = good;
    qa_store_u32le(bad.bytes + bad.central + 20, UINT32_MAX);
    bad_zip(&bad, QA_ERROR_UNSUPPORTED);
    bad = good;
    qa_store_u32le(bad.bytes + bad.central + 42, (uint32_t)bad.central);
    bad_zip(&bad, QA_ERROR_FORMAT);
    bad = good;
    qa_store_u16le(bad.bytes + bad.members[0].local + 26, UINT16_MAX);
    bad_zip(&bad, QA_ERROR_FORMAT);
    bad = good;
    qa_store_u16le(bad.bytes + bad.central + 30, UINT16_MAX);
    bad_zip(&bad, QA_ERROR_FORMAT);
    bad = good;
    qa_store_u32le(bad.bytes + bad.members[0].local + 14, 1);
    bad_zip(&bad, QA_ERROR_FORMAT);
    bad = good;
    bad.bytes[bad.members[0].data] = 0xff;
    qa_archive *archive = open_zip(&bad);
    qa_archive_data data = {0};
    qa_error error = {0};
    CHECK(!qa_archive_read(archive, 0, &data, &error));
    CHECK(error.code == QA_ERROR_FORMAT && data.owned.data == NULL);
    qa_archive_close(archive);

    for (int difference = -1; difference <= 1; difference += 2) {
        bad = good;
        uint32_t changed = (uint32_t)((int)bad.members[0].size + difference);
        qa_store_u32le(bad.bytes + bad.central + 24, changed);
        qa_store_u32le(bad.bytes + bad.members[0].descriptor + 12, changed);
        archive = open_zip(&bad);
        CHECK(!qa_archive_read(archive, 0, &data, &error));
        CHECK(error.code == QA_ERROR_FORMAT && data.owned.data == NULL);
        qa_archive_close(archive);
    }

    zip_fixture stored = {0};
    add_member(&stored, "stored", "crc target", 0, 0);
    finish_zip(&stored, "");
    stored.bytes[stored.members[0].data] ^= 1;
    archive = open_zip(&stored);
    CHECK(!qa_archive_read(archive, 0, &data, &error));
    CHECK(error.code == QA_ERROR_FORMAT && strstr(error.message, "CRC32") != NULL);
    qa_archive_close(archive);
    const size_t crc_offsets[] = {good.central + 16, good.members[0].descriptor + 4};
    for (size_t i = 0; i < sizeof(crc_offsets) / sizeof(crc_offsets[0]); ++i)
        good.bytes[crc_offsets[i]] ^= 1;
    archive = open_zip(&good);
    CHECK(!qa_archive_read(archive, 0, &data, &error));
    CHECK(error.code == QA_ERROR_FORMAT && data.owned.data == NULL);
    qa_archive_close(archive);

    zip_fixture unsafe = {0};
    add_member(&unsafe, "../outside", "", 0, 0);
    finish_zip(&unsafe, "");
    bad_zip(&unsafe, QA_ERROR_FORMAT);
    unsafe = (zip_fixture){0};
    add_member(&unsafe, "name", "", 0, 0);
    finish_zip(&unsafe, "");
    unsafe.bytes[unsafe.members[0].local + 31] = 0;
    unsafe.bytes[unsafe.central + 47] = 0;
    bad_zip(&unsafe, QA_ERROR_FORMAT);
}

static void test_file_ownership(void)
{
    char name[] = "/tmp/qa-archive-test-XXXXXX";
    int descriptor = mkstemp(name);
    CHECK(descriptor >= 0);
    FILE *file = fdopen(descriptor, "wb");
    CHECK(file != NULL);
    uint8_t bytes[142];
    make_pak(bytes, "retained");
    CHECK(fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    CHECK(fclose(file) == 0);
    qa_archive *archive = NULL;
    qa_error error = {0};
    CHECK(qa_archive_open_file(name, QA_ARCHIVE_AUTO, &archive, &error));
    CHECK(unlink(name) == 0);
    qa_archive_data data = {0};
    CHECK(qa_archive_read(archive, 0, &data, &error));
    CHECK(data.bytes.data[0] == 41 && data.owned.data == NULL);
    qa_archive_data_free(&data);
    qa_archive_close(archive);
    CHECK(!qa_archive_open_file(name, QA_ARCHIVE_AUTO, &archive, &error));
    CHECK(archive == NULL && error.code == QA_ERROR_NOT_FOUND);
}

static void test_real_archive(const char *path)
{
    qa_archive *archive = NULL;
    qa_error error = {0};
    if (!qa_archive_open_file(path, QA_ARCHIVE_AUTO, &archive, &error)) {
        fprintf(stderr, "%s:%zu: %s\n", path, error.offset, error.message);
        exit(EXIT_FAILURE);
    }
    size_t reads = 0;
    size_t bytes = 0;
    for (size_t i = 0; i < qa_archive_count(archive); ++i) {
        const qa_archive_entry *entry = qa_archive_entry_at(archive, i);
        if (entry->is_directory) continue;
        qa_archive_data data = {0};
        if (!qa_archive_read(archive, i, &data, &error)) {
            fprintf(stderr, "%s:%s:%zu: %s\n", path, entry->path, error.offset, error.message);
            exit(EXIT_FAILURE);
        }
        CHECK(data.bytes.size == entry->size);
        ++reads;
        bytes += data.bytes.size;
        qa_archive_data_free(&data);
    }
    printf("%s: %zu entries, %zu files, %zu decoded bytes\n", path, qa_archive_count(archive), reads, bytes);
    qa_archive_close(archive);
}

int main(int argc, char **argv)
{
    test_pak();
    test_case_folding();
    test_zip();
    test_corruption();
    test_file_ownership();
    for (int i = 1; i < argc; ++i) test_real_archive(argv[i]);
    puts("archive checks passed");
    return EXIT_SUCCESS;
}
