/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qa/archive.h"
#include "qa/binary.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

struct qa_archive {
    qa_archive_kind kind;
    qa_bytes bytes;
    qa_buffer owned;
    qa_archive_entry *entries;
    size_t count;
};

static bool fail(qa_error *error, qa_status code, size_t offset,
                 const char *message)
{
    qa_error_set(error, code, offset, "%s", message);
    return false;
}

static bool span(size_t limit, size_t offset, size_t size, qa_error *error)
{
    if (offset > limit || size > limit - offset)
        return fail(error, QA_ERROR_FORMAT, offset, "archive range exceeds its boundary");
    return true;
}

static char *normalize_path(const uint8_t *path, size_t length, qa_error *error,
                            size_t offset)
{
    if (length == 0 || length == SIZE_MAX) {
        fail(error, QA_ERROR_FORMAT, offset, "empty or oversized archive path");
        return NULL;
    }
    char *result = malloc(length + 1);
    if (result == NULL) {
        fail(error, QA_ERROR_MEMORY, offset, "allocating archive path");
        return NULL;
    }
    size_t used = 0;
    size_t part = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i < length && path[i] != '/' && path[i] != '\\') {
            if (path[i] == 0 || path[i] == ':') goto unsafe;
            continue;
        }
        const size_t size = i - part;
        if (size == 0 || (size == 1 && path[part] == '.')) goto unsafe;
        if (size == 2 && path[part] == '.' && path[part + 1] == '.') {
            if (used == 0) goto unsafe;
            while (used > 0 && result[used - 1] != '/') --used;
            if (used > 0) --used;
        } else {
            if (used > 0) result[used++] = '/';
            memcpy(result + used, path + part, size);
            used += size;
        }
        part = i + 1;
        if (i + 1 == length && i < length) {
            if (used == 0) goto unsafe;
            result[used++] = '/';
            break;
        }
    }
    if (used == 0) goto unsafe;
    result[used] = '\0';
    return result;
unsafe:
    free(result);
    fail(error, QA_ERROR_FORMAT, offset, "unsafe archive member path");
    return NULL;
}

static bool set_name(qa_archive_entry *entry, const uint8_t *name, size_t length,
                     size_t offset, qa_error *error)
{
    char *normalized = normalize_path(name, length, error, offset);
    if (normalized == NULL) return false;
    char *raw = malloc(length + 1);
    if (raw == NULL) {
        free(normalized);
        return fail(error, QA_ERROR_MEMORY, offset, "allocating raw archive path");
    }
    memcpy(raw, name, length);
    raw[length] = '\0';
    entry->path = normalized;
    entry->raw_path = raw;
    entry->is_directory = normalized[strlen(normalized) - 1] == '/';
    return true;
}

static bool allocate_entries(qa_archive *archive, size_t count, qa_error *error)
{
    if (count > SIZE_MAX / sizeof(*archive->entries))
        return fail(error, QA_ERROR_MEMORY, 0, "archive directory is too large");
    if (count == 0) return true;
    archive->entries = calloc(count, sizeof(*archive->entries));
    if (archive->entries == NULL)
        return fail(error, QA_ERROR_MEMORY, 0, "allocating archive directory");
    archive->count = count;
    return true;
}

static bool parse_pak(qa_archive *archive, qa_error *error)
{
    const qa_bytes source = archive->bytes;
    if (!span(source.size, 0, 12, error)) return false;
    if (memcmp(source.data, "PACK", 4) != 0)
        return fail(error, QA_ERROR_FORMAT, 0, "invalid PACK signature");
    const int32_t directory = qa_load_i32le(source.data + 4);
    const int32_t length = qa_load_i32le(source.data + 8);
    if (directory < 12 || length < 0 || length % 64 != 0)
        return fail(error, QA_ERROR_FORMAT, 4, "invalid PACK directory range");
    if (!span(source.size, (size_t)directory, (size_t)length, error) ||
        !allocate_entries(archive, (size_t)length / 64, error)) return false;
    for (size_t i = 0; i < archive->count; ++i) {
        size_t offset = (size_t)directory + i * 64;
        const uint8_t *record = source.data + offset;
        qa_archive_entry *entry = &archive->entries[i];
        int32_t start = qa_load_i32le(record + 56);
        int32_t size = qa_load_i32le(record + 60);
        if (start < 0 || size < 0)
            return fail(error, QA_ERROR_FORMAT, offset + 56, "negative PACK member range");
        if (!span(source.size, (size_t)start, (size_t)size, error)) return false;
        size_t name_length = 0;
        while (name_length < 56 && record[name_length] != 0) ++name_length;
        if (!set_name(entry, record, name_length, offset, error)) return false;
        entry->ordinal = i;
        entry->data_offset = (size_t)start;
        entry->size = (size_t)size;
        entry->compressed_size = (size_t)size;
    }
    return true;
}

static bool descriptor_matches(const uint8_t *record, const qa_archive_entry *entry)
{
    return qa_load_u32le(record) == entry->crc32 &&
           qa_load_u32le(record + 4) == entry->compressed_size &&
           qa_load_u32le(record + 8) == entry->size;
}

static bool parse_local(qa_archive *archive, qa_archive_entry *entry,
                        size_t local, size_t central, size_t name_length,
                        qa_error *error)
{
    if (!span(central, local, 30, error)) return false;
    const uint8_t *header = archive->bytes.data + local;
    if (qa_load_u32le(header) != UINT32_C(0x04034b50))
        return fail(error, QA_ERROR_FORMAT, local, "invalid ZIP local header signature");
    uint16_t flags = qa_load_u16le(header + 6);
    uint16_t method = qa_load_u16le(header + 8);
    if (((flags ^ entry->flags) & 0x7fff) != 0 || method != entry->compression_method)
        return fail(error, QA_ERROR_FORMAT, local, "ZIP local and central methods or flags disagree");
    uint32_t crc = qa_load_u32le(header + 14);
    uint32_t compressed = qa_load_u32le(header + 18);
    uint32_t size = qa_load_u32le(header + 22);
    bool descriptor = (flags & 8) != 0;
    if (((!descriptor || crc != 0) && crc != entry->crc32) ||
        ((!descriptor || compressed != 0) && compressed != entry->compressed_size) ||
        ((!descriptor || size != 0) && size != entry->size))
        return fail(error, QA_ERROR_FORMAT, local, "ZIP local and central sizes or CRC disagree");
    size_t local_name_length = qa_load_u16le(header + 26);
    size_t extra_length = qa_load_u16le(header + 28);
    size_t variable = local + 30;
    if (!span(central, variable, local_name_length + extra_length, error)) return false;
    if (local_name_length != name_length ||
        memcmp(header + 30, entry->raw_path, name_length) != 0)
        return fail(error, QA_ERROR_FORMAT, variable, "ZIP local and central names disagree");
    entry->data_offset = variable + local_name_length + extra_length;
    if (!span(central, entry->data_offset, entry->compressed_size, error)) return false;
    if (descriptor) {
        size_t offset = entry->data_offset + entry->compressed_size;
        const uint8_t *record = archive->bytes.data + offset;
        size_t remaining = central - offset;
        if (remaining >= 16 && qa_load_u32le(record) == UINT32_C(0x08074b50) &&
            descriptor_matches(record + 4, entry)) return true;
        if (remaining >= 12 && descriptor_matches(record, entry)) return true;
        return fail(error, QA_ERROR_FORMAT, offset, "ZIP data descriptor is missing or disagrees");
    }
    return true;
}

static bool parse_zip(qa_archive *archive, qa_error *error)
{
    const qa_bytes source = archive->bytes;
    if (source.size < 22)
        return fail(error, QA_ERROR_FORMAT, 0, "ZIP end-of-central-directory record not found");
    size_t end = source.size - 22;
    size_t first = end > UINT16_MAX ? end - UINT16_MAX : 0;
    for (;;) {
        const uint8_t *record = source.data + end;
        if (qa_load_u32le(record) == UINT32_C(0x06054b50) &&
            qa_load_u16le(record + 20) == source.size - end - 22) break;
        if (end == first)
            return fail(error, QA_ERROR_FORMAT, first, "ZIP end-of-central-directory record not found");
        --end;
    }
    const uint8_t *tail = source.data + end;
    uint16_t count = qa_load_u16le(tail + 10);
    uint32_t length = qa_load_u32le(tail + 12);
    uint32_t relative = qa_load_u32le(tail + 16);
    if (qa_load_u16le(tail + 4) != 0 || qa_load_u16le(tail + 6) != 0 ||
        qa_load_u16le(tail + 8) != count)
        return fail(error, QA_ERROR_UNSUPPORTED, end, "multi-disk ZIP is unsupported");
    if (count == UINT16_MAX || length == UINT32_MAX || relative == UINT32_MAX)
        return fail(error, QA_ERROR_UNSUPPORTED, end, "ZIP64 is unsupported");
    if (!span(end, relative, length, error)) return false;
    size_t central = end - length;
    size_t prefix = central - relative;
    if (count > (size_t)length / 46)
        return fail(error, QA_ERROR_FORMAT, central, "ZIP entry count exceeds directory size");
    if (!allocate_entries(archive, count, error)) return false;
    size_t offset = central;
    for (size_t i = 0; i < count; ++i) {
        if (!span(end, offset, 46, error)) return false;
        const uint8_t *record = source.data + offset;
        if (qa_load_u32le(record) != UINT32_C(0x02014b50))
            return fail(error, QA_ERROR_FORMAT, offset, "invalid ZIP central directory signature");
        qa_archive_entry *entry = &archive->entries[i];
        entry->ordinal = i;
        entry->flags = qa_load_u16le(record + 8);
        entry->compression_method = qa_load_u16le(record + 10);
        entry->crc32 = qa_load_u32le(record + 16);
        entry->compressed_size = qa_load_u32le(record + 20);
        entry->size = qa_load_u32le(record + 24);
        size_t name_length = qa_load_u16le(record + 28);
        size_t extra_length = qa_load_u16le(record + 30);
        size_t comment_length = qa_load_u16le(record + 32);
        uint32_t local = qa_load_u32le(record + 42);
        if (entry->compressed_size == UINT32_MAX || entry->size == UINT32_MAX || local == UINT32_MAX)
            return fail(error, QA_ERROR_UNSUPPORTED, offset, "ZIP64 entry is unsupported");
        if (qa_load_u16le(record + 34) != 0)
            return fail(error, QA_ERROR_UNSUPPORTED, offset, "ZIP entry starts on another disk");
        if ((entry->flags & (1u | 64u)) != 0)
            return fail(error, QA_ERROR_UNSUPPORTED, offset, "encrypted ZIP entry is unsupported");
        if (entry->compression_method != 0 && entry->compression_method != 8)
            return fail(error, QA_ERROR_UNSUPPORTED, offset, "unsupported ZIP compression method");
        if (entry->compression_method == 0 && entry->compressed_size != entry->size)
            return fail(error, QA_ERROR_FORMAT, offset, "stored ZIP sizes disagree");
        size_t variable = offset + 46;
        size_t variable_size = name_length + extra_length + comment_length;
        if (!span(end, variable, variable_size, error) ||
            !set_name(entry, record + 46, name_length, variable, error)) return false;
        if (local > central - prefix)
            return fail(error, QA_ERROR_FORMAT, offset + 42, "ZIP local header offset exceeds payload range");
        if (!parse_local(archive, entry, prefix + local, central, name_length, error)) return false;
        offset = variable + variable_size;
    }
    if (offset != end)
        return fail(error, QA_ERROR_FORMAT, offset, "ZIP directory size disagrees with entry count");
    return true;
}

static unsigned char ascii_lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

bool qa_archive_paths_equal(const char *left, const char *right,
                             qa_archive_comparison comparison)
{
    if (left == NULL || right == NULL ||
        (comparison != QA_ARCHIVE_EXACT && comparison != QA_ARCHIVE_ASCII_INSENSITIVE &&
         comparison != QA_ARCHIVE_CASE_INSENSITIVE)) return false;
    if (comparison == QA_ARCHIVE_EXACT) return strcmp(left, right) == 0;
    while (*left != '\0' && *right != '\0') {
        unsigned char a = ascii_lower((unsigned char)*left++);
        unsigned char b = ascii_lower((unsigned char)*right++);
        if (comparison == QA_ARCHIVE_CASE_INSENSITIVE) {
            if ((a >= 0xc0 && a <= 0xd6) || (a >= 0xd8 && a <= 0xde)) a += 0x20;
            if ((b >= 0xc0 && b <= 0xd6) || (b >= 0xd8 && b <= 0xde)) b += 0x20;
        }
        if (a != b) return false;
    }
    return *left == *right;
}

bool qa_archive_open_memory(qa_bytes bytes, qa_archive_kind kind,
                            qa_archive **out, qa_error *error)
{
    if (out == NULL) return fail(error, QA_ERROR_ARGUMENT, 0, "archive output is NULL");
    *out = NULL;
    if ((bytes.data == NULL && bytes.size != 0) || kind < QA_ARCHIVE_AUTO || kind > QA_ARCHIVE_KPF)
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid archive input or kind");
    if (kind == QA_ARCHIVE_AUTO)
        kind = bytes.size >= 4 && memcmp(bytes.data, "PACK", 4) == 0 ? QA_ARCHIVE_PAK : QA_ARCHIVE_ZIP;
    qa_archive *archive = calloc(1, sizeof(*archive));
    if (archive == NULL) return fail(error, QA_ERROR_MEMORY, 0, "allocating archive");
    archive->kind = kind;
    archive->bytes = bytes;
    if (!(kind == QA_ARCHIVE_PAK ? parse_pak(archive, error) : parse_zip(archive, error))) {
        qa_archive_close(archive);
        return false;
    }
    *out = archive;
    return true;
}

bool qa_archive_open_file(const char *path, qa_archive_kind kind,
                          qa_archive **out, qa_error *error)
{
    if (out == NULL) return fail(error, QA_ERROR_ARGUMENT, 0, "archive output is NULL");
    *out = NULL;
    if (path == NULL) return fail(error, QA_ERROR_ARGUMENT, 0, "archive path is NULL");
    qa_buffer bytes = {0};
    if (!qa_file_read_all(path, &bytes, error)) return false;
    if (kind == QA_ARCHIVE_AUTO && !(bytes.size >= 4 && memcmp(bytes.data, "PACK", 4) == 0))
        kind = qa_archive_kind_for_path(path);
    if (!qa_archive_open_memory((qa_bytes){bytes.data, bytes.size}, kind, out, error)) {
        qa_buffer_free(&bytes);
        return false;
    }
    (*out)->owned = bytes;
    return true;
}

void qa_archive_close(qa_archive *archive)
{
    if (archive == NULL) return;
    for (size_t i = 0; i < archive->count; ++i) {
        free((void *)archive->entries[i].raw_path);
        free((void *)archive->entries[i].path);
    }
    free(archive->entries);
    qa_buffer_free(&archive->owned);
    free(archive);
}

qa_archive_kind qa_archive_get_kind(const qa_archive *archive)
{
    return archive == NULL ? QA_ARCHIVE_AUTO : archive->kind;
}

qa_archive_kind qa_archive_kind_for_path(const char *path)
{
    if (path == NULL) return QA_ARCHIVE_AUTO;
    const char *extension = strrchr(path, '.');
    if (extension == NULL) return QA_ARCHIVE_AUTO;
    if (qa_archive_paths_equal(extension, ".pak", QA_ARCHIVE_ASCII_INSENSITIVE)) return QA_ARCHIVE_PAK;
    if (qa_archive_paths_equal(extension, ".pk3", QA_ARCHIVE_ASCII_INSENSITIVE)) return QA_ARCHIVE_PK3;
    if (qa_archive_paths_equal(extension, ".pk4", QA_ARCHIVE_ASCII_INSENSITIVE)) return QA_ARCHIVE_PK4;
    if (qa_archive_paths_equal(extension, ".kpf", QA_ARCHIVE_ASCII_INSENSITIVE)) return QA_ARCHIVE_KPF;
    if (qa_archive_paths_equal(extension, ".zip", QA_ARCHIVE_ASCII_INSENSITIVE)) return QA_ARCHIVE_ZIP;
    return QA_ARCHIVE_AUTO;
}

size_t qa_archive_count(const qa_archive *archive)
{
    return archive == NULL ? 0 : archive->count;
}

const qa_archive_entry *qa_archive_entry_at(const qa_archive *archive, size_t ordinal)
{
    return archive == NULL || ordinal >= archive->count ? NULL : &archive->entries[ordinal];
}

char *qa_archive_normalize_path(const char *path, qa_error *error)
{
    if (path == NULL) {
        fail(error, QA_ERROR_ARGUMENT, 0, "archive member path is NULL");
        return NULL;
    }
    return normalize_path((const uint8_t *)path, strlen(path), error, 0);
}

bool qa_archive_find(const qa_archive *archive, const char *path,
                      qa_archive_comparison comparison, size_t start_ordinal,
                      const qa_archive_entry **out, qa_error *error)
{
    if (out == NULL) return fail(error, QA_ERROR_ARGUMENT, 0, "archive search output is NULL");
    *out = NULL;
    if (archive == NULL || path == NULL ||
        (comparison != QA_ARCHIVE_EXACT && comparison != QA_ARCHIVE_ASCII_INSENSITIVE &&
         comparison != QA_ARCHIVE_CASE_INSENSITIVE))
        return fail(error, QA_ERROR_ARGUMENT, 0, "invalid archive search arguments");
    char *normalized = qa_archive_normalize_path(path, error);
    if (normalized == NULL) return false;
    for (size_t i = start_ordinal; i < archive->count; ++i) {
        if (qa_archive_paths_equal(archive->entries[i].path, normalized, comparison)) {
            *out = &archive->entries[i];
            break;
        }
    }
    free(normalized);
    return true;
}

static uint32_t checksum(qa_bytes bytes)
{
    uLong crc = crc32(0L, Z_NULL, 0);
    size_t offset = 0;
    while (offset < bytes.size) {
        size_t chunk = bytes.size - offset;
        if (chunk > UINT_MAX) chunk = UINT_MAX;
        crc = crc32(crc, bytes.data + offset, (uInt)chunk);
        offset += chunk;
    }
    return (uint32_t)crc;
}

static bool inflate_entry(qa_bytes compressed, qa_buffer *output,
                          size_t offset, qa_error *error)
{
    z_stream stream = {0};
    int status = inflateInit2(&stream, -MAX_WBITS);
    if (status != Z_OK)
        return fail(error, status == Z_MEM_ERROR ? QA_ERROR_MEMORY : QA_ERROR_FORMAT,
                    offset, "initializing ZIP DEFLATE decoder");
    size_t fed = 0;
    size_t produced = 0;
    uint8_t overflow;
    bool valid = false;
    for (;;) {
        if (stream.avail_in == 0 && fed < compressed.size) {
            size_t size = compressed.size - fed;
            if (size > UINT_MAX) size = UINT_MAX;
            stream.next_in = (Bytef *)(compressed.data + fed);
            stream.avail_in = (uInt)size;
            fed += size;
        }
        size_t capacity = output->size - produced;
        if (capacity > UINT_MAX) capacity = UINT_MAX;
        stream.next_out = capacity == 0 ? &overflow : output->data + produced;
        stream.avail_out = capacity == 0 ? 1 : (uInt)capacity;
        uInt before_in = stream.avail_in;
        uInt before_out = stream.avail_out;
        status = inflate(&stream, Z_NO_FLUSH);
        size_t written = before_out - stream.avail_out;
        if (written > output->size - produced) break;
        produced += written;
        if (status == Z_STREAM_END) {
            valid = produced == output->size && fed - stream.avail_in == compressed.size;
            break;
        }
        if (status != Z_OK || (written == 0 && stream.avail_in == before_in)) break;
    }
    inflateEnd(&stream);
    if (!valid)
        return fail(error, status == Z_MEM_ERROR ? QA_ERROR_MEMORY : QA_ERROR_FORMAT,
                    offset, "invalid DEFLATE stream or decoded size");
    return true;
}

bool qa_archive_read(const qa_archive *archive, size_t ordinal,
                      qa_archive_data *out, qa_error *error)
{
    if (out == NULL) return fail(error, QA_ERROR_ARGUMENT, 0, "archive read output is NULL");
    *out = (qa_archive_data){0};
    const qa_archive_entry *entry = qa_archive_entry_at(archive, ordinal);
    if (entry == NULL) return fail(error, QA_ERROR_ARGUMENT, ordinal, "invalid archive entry ordinal");
    if (entry->is_directory)
        return fail(error, QA_ERROR_ARGUMENT, ordinal, "cannot read an archive directory");
    qa_bytes bytes = {archive->bytes.data + entry->data_offset, entry->compressed_size};
    if (entry->compression_method == 8) {
        out->owned.size = entry->size;
        out->owned.data = malloc(entry->size == 0 ? 1 : entry->size);
        if (out->owned.data == NULL) {
            *out = (qa_archive_data){0};
            return fail(error, QA_ERROR_MEMORY, entry->data_offset, "allocating decompressed archive entry");
        }
        if (!inflate_entry(bytes, &out->owned, entry->data_offset, error)) {
            qa_archive_data_free(out);
            return false;
        }
        bytes = (qa_bytes){out->owned.data, out->owned.size};
    }
    if (archive->kind != QA_ARCHIVE_PAK && checksum(bytes) != entry->crc32) {
        qa_archive_data_free(out);
        return fail(error, QA_ERROR_FORMAT, entry->data_offset, "ZIP entry CRC32 mismatch");
    }
    out->bytes = bytes;
    return true;
}

void qa_archive_data_free(qa_archive_data *data)
{
    if (data == NULL) return;
    qa_buffer_free(&data->owned);
    *data = (qa_archive_data){0};
}
