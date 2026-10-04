#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "qa/vfs.h"
#include "qa/binary.h"
#include "qa/vfs_save.h"
#include "qa/vfs_view_save.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void save(const char *path, const void *data, size_t size)
{
    int file = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    CHECK(file >= 0);
    const uint8_t *bytes = data;
    size_t offset = 0;
    while (offset < size) {
        ssize_t written = write(file, bytes + offset, size - offset);
        CHECK(written > 0);
        offset += (size_t)written;
    }
    CHECK(close(file) == 0);
}

static void make_pak(const char *path)
{
    uint8_t bytes[151] = {0};
    memcpy(bytes, "PACK", 4);
    qa_store_u32le(bytes + 4, 23);
    qa_store_u32le(bytes + 8, 128);
    memcpy(bytes + 12, "firstsecond", 11);
    memcpy(bytes + 23, "maps/item.txt", 13);
    qa_store_u32le(bytes + 23 + 56, 12);
    qa_store_u32le(bytes + 23 + 60, 5);
    memcpy(bytes + 87, "maps/item.txt", 13);
    qa_store_u32le(bytes + 87 + 56, 17);
    qa_store_u32le(bytes + 87 + 60, 6);
    save(path, bytes, sizeof(bytes));
}

static void make_zip(const char *path)
{
    const char *name = "maps/item.txt";
    const char *values[] = {"zip-first", "zip-last"};
    uint8_t bytes[1024] = {0};
    uint32_t local_offsets[2], compressed_sizes[2], sizes[2], checksums[2];
    size_t cursor = 0, name_size = strlen(name);
    for (size_t i = 0; i < 2; i++) {
        sizes[i] = (uint32_t)strlen(values[i]);
        checksums[i] = (uint32_t)crc32(0, (const Bytef *)values[i], (uInt)sizes[i]);
        local_offsets[i] = (uint32_t)cursor;
        uint8_t compressed[128];
        z_stream stream = {0};
        CHECK(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS,
                           8, Z_DEFAULT_STRATEGY) == Z_OK);
        stream.next_in = (Bytef *)values[i];
        stream.avail_in = (uInt)sizes[i];
        stream.next_out = compressed;
        stream.avail_out = sizeof(compressed);
        CHECK(deflate(&stream, Z_FINISH) == Z_STREAM_END);
        compressed_sizes[i] = (uint32_t)stream.total_out;
        CHECK(deflateEnd(&stream) == Z_OK);
        qa_store_u32le(bytes + cursor, UINT32_C(0x04034b50));
        qa_store_u16le(bytes + cursor + 4, 20);
        qa_store_u16le(bytes + cursor + 8, 8);
        qa_store_u32le(bytes + cursor + 14, checksums[i]);
        qa_store_u32le(bytes + cursor + 18, compressed_sizes[i]);
        qa_store_u32le(bytes + cursor + 22, sizes[i]);
        qa_store_u16le(bytes + cursor + 26, (uint16_t)name_size);
        memcpy(bytes + cursor + 30, name, name_size);
        cursor += 30 + name_size;
        memcpy(bytes + cursor, compressed, compressed_sizes[i]);
        cursor += compressed_sizes[i];
    }
    uint32_t central_offset = (uint32_t)cursor;
    for (size_t i = 0; i < 2; i++) {
        qa_store_u32le(bytes + cursor, UINT32_C(0x02014b50));
        qa_store_u16le(bytes + cursor + 4, 20);
        qa_store_u16le(bytes + cursor + 6, 20);
        qa_store_u16le(bytes + cursor + 10, 8);
        qa_store_u32le(bytes + cursor + 16, checksums[i]);
        qa_store_u32le(bytes + cursor + 20, compressed_sizes[i]);
        qa_store_u32le(bytes + cursor + 24, sizes[i]);
        qa_store_u16le(bytes + cursor + 28, (uint16_t)name_size);
        qa_store_u32le(bytes + cursor + 42, local_offsets[i]);
        memcpy(bytes + cursor + 46, name, name_size);
        cursor += 46 + name_size;
    }
    qa_store_u32le(bytes + cursor, UINT32_C(0x06054b50));
    qa_store_u16le(bytes + cursor + 8, 2);
    qa_store_u16le(bytes + cursor + 10, 2);
    qa_store_u32le(bytes + cursor + 12, (uint32_t)cursor - central_offset);
    qa_store_u32le(bytes + cursor + 16, central_offset);
    cursor += 22;
    CHECK(cursor <= sizeof(bytes));
    save(path, bytes, cursor);
}

static void expect_text(const qa_resource *resource, const char *text)
{
    qa_bytes bytes = qa_resource_bytes(resource);
    CHECK(bytes.size == strlen(text));
    CHECK(memcmp(bytes.data, text, bytes.size) == 0);
}

int main(void)
{
    char directory[] = "/tmp/qa-vfs-test-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char pak[256], zip[256], loose[256], map_dir[256], item[256];
    char uppercase[256], lowercase[256], latin[256], alias[256], absolute[256];
    char outside[256], escape[256], renamed[256], save_dir[256], saved[256];
    CHECK(snprintf(pak, sizeof(pak), "%s/test.pak", directory) > 0);
    CHECK(snprintf(zip, sizeof(zip), "%s/test.pk3", directory) > 0);
    CHECK(snprintf(loose, sizeof(loose), "%s/loose", directory) > 0);
    CHECK(snprintf(map_dir, sizeof(map_dir), "%s/loose/maps", directory) > 0);
    CHECK(snprintf(item, sizeof(item), "%s/loose/maps/item.txt", directory) > 0);
    CHECK(snprintf(uppercase, sizeof(uppercase), "%s/loose/Data.TXT", directory) > 0);
    CHECK(snprintf(lowercase, sizeof(lowercase), "%s/loose/data.txt", directory) > 0);
    CHECK(snprintf(latin, sizeof(latin), "%s/loose/CAF\xc9.BIN", directory) > 0);
    CHECK(snprintf(alias, sizeof(alias), "%s/loose/alias", directory) > 0);
    CHECK(snprintf(absolute, sizeof(absolute), "%s/loose/absolute", directory) > 0);
    CHECK(snprintf(outside, sizeof(outside), "%s/outside", directory) > 0);
    CHECK(snprintf(escape, sizeof(escape), "%s/loose/escape", directory) > 0);
    CHECK(snprintf(renamed, sizeof(renamed), "%s/renamed", directory) > 0);
    CHECK(snprintf(save_dir, sizeof(save_dir), "%s/loose/saves", directory) > 0);
    CHECK(snprintf(saved, sizeof(saved), "%s/loose/saves/test.sav", directory) > 0);
    CHECK(mkdir(loose, 0700) == 0 && mkdir(map_dir, 0700) == 0);
    make_pak(pak);
    make_zip(zip);
    save(item, "loose", 5);
    save(uppercase, "upper", 5);
    save(lowercase, "lower", 5);
    save(latin, "latin", 5);
    save(outside, "outside", 7);
    CHECK(symlink("Data.TXT", alias) == 0);
    CHECK(symlink(uppercase, absolute) == 0);
    CHECK(symlink("../outside", escape) == 0);

    qa_error error = {0};
    qa_resource_pool *pool = qa_resource_pool_create(&error);
    CHECK(pool != NULL);
    qa_vfs *vfs = qa_vfs_create(pool, &error);
    qa_vfs *other = qa_vfs_create(pool, &error);
    CHECK(vfs != NULL && other != NULL);
    qa_mount_id pak_id, zip_id, loose_id, other_pak, other_zip, ascii_id, exact_id;
    CHECK(qa_vfs_mount_archive(vfs, pak, QA_ARCHIVE_PAK, QA_ARCHIVE_CASE_INSENSITIVE, &pak_id, &error));
    CHECK(qa_vfs_mount_archive(vfs, zip, QA_ARCHIVE_PK3, QA_ARCHIVE_CASE_INSENSITIVE, &zip_id, &error));
    CHECK(qa_vfs_mount_directory(vfs, loose, QA_ARCHIVE_CASE_INSENSITIVE, true, &loose_id, &error));
    CHECK(qa_vfs_mount_archive(other, pak, QA_ARCHIVE_PAK, QA_ARCHIVE_EXACT, &other_pak, &error));
    CHECK(qa_vfs_mount_archive(other, zip, QA_ARCHIVE_PK3, QA_ARCHIVE_EXACT, &other_zip, &error));
    CHECK(qa_vfs_mount_directory(other, loose, QA_ARCHIVE_ASCII_INSENSITIVE, false, &ascii_id, &error));
    CHECK(qa_vfs_mount_directory(other, loose, QA_ARCHIVE_EXACT, false, &exact_id, &error));

    qa_resource *pak_resource, *zip_resource, *loose_resource, *again = NULL;
    qa_mount_id resolved;
    CHECK(qa_vfs_acquire(vfs, "MAPS\\ITEM.TXT", &pak_resource, &resolved, &error));
    CHECK(resolved == pak_id);
    expect_text(pak_resource, "first");
    CHECK(qa_vfs_acquire_from(other, other_pak, "maps/item.txt", &again, &error));
    CHECK(again == pak_resource && qa_resource_id(again) == qa_resource_id(pak_resource));
    qa_resource_release(again);
    CHECK(qa_vfs_acquire_from(vfs, zip_id, "maps/item.txt", &zip_resource, &error));
    expect_text(zip_resource, "zip-last");
    CHECK(qa_vfs_acquire_from(other, other_zip, "maps/item.txt", &again, &error));
    CHECK(again == zip_resource && qa_resource_bytes(again).data == qa_resource_bytes(zip_resource).data);
    qa_resource_release(again);

    qa_buffer pool_save = {0}, view_save = {0};
    const qa_vfs *saved_views[] = {vfs, other};
    CHECK(qa_resource_pool_checkpoint_linked(pool, saved_views, 2, &pool_save, &error));
    CHECK(qa_vfs_checkpoint(vfs, &view_save, &error));
    qa_resource_pool *restored_pool = qa_resource_pool_create(&error);
    CHECK(restored_pool != NULL && qa_resource_pool_restore_linked(restored_pool, NULL,
        (qa_bytes){pool_save.data, pool_save.size}, &error));
    qa_vfs *restored = NULL;
    CHECK(qa_vfs_create_restored(restored_pool, NULL, (qa_bytes){view_save.data, view_save.size}, &restored, &error));
    CHECK(qa_vfs_acquire_from(restored, pak_id, "maps/item.txt", &again, &error));
    expect_text(again, "first"); qa_resource_release(again); again = NULL;
    CHECK(qa_vfs_acquire_from(restored, zip_id, "maps/item.txt", &again, &error));
    expect_text(again, "zip-last"); qa_resource_release(again); again = NULL;
    qa_vfs_destroy(restored);
    uint64_t kept = qa_resource_id(qa_resource_pool_find(restored_pool, qa_resource_id(pak_resource)));
    CHECK(kept != 0 && !qa_resource_pool_restore_linked(restored_pool, NULL,
        (qa_bytes){pool_save.data, pool_save.size - 1}, &error));
    CHECK(qa_resource_pool_find(restored_pool, kept) != NULL);
    qa_resource_pool_destroy(restored_pool);
    qa_buffer_free(&pool_save); qa_buffer_free(&view_save);

    qa_mount_id order[] = {loose_id, pak_id, zip_id};
    CHECK(qa_vfs_set_order(vfs, order, 3, &error));
    CHECK(qa_vfs_acquire(vfs, "maps/item.txt", &loose_resource, &resolved, &error));
    CHECK(resolved == loose_id);
    expect_text(loose_resource, "loose");
    qa_mount_id invalid_order[] = {pak_id, pak_id, zip_id};
    CHECK(!qa_vfs_set_order(vfs, invalid_order, 3, &error));
    qa_mount_id prefix_order[] = {zip_id, pak_id, loose_id};
    CHECK(qa_vfs_set_prefix_order(vfs, "maps", prefix_order, 3, &error));
    CHECK(qa_vfs_acquire(vfs, "maps/item.txt", &again, &resolved, &error));
    CHECK(again == zip_resource && resolved == zip_id);
    qa_resource_release(again);
    CHECK(qa_vfs_set_prefix_order(vfs, "MAPS", NULL, 0, &error));

    CHECK(!qa_vfs_acquire_from(vfs, loose_id, "DATA.TXT", &again, &error));
    CHECK(error.code == QA_ERROR_FORMAT && again == NULL);
    CHECK(qa_vfs_acquire_from(vfs, loose_id, "Data.TXT", &again, &error));
    expect_text(again, "upper");
    qa_resource_release(again);
    CHECK(qa_vfs_acquire_from(vfs, loose_id, "caf\xe9.bin", &again, &error));
    expect_text(again, "latin");
    qa_resource_release(again);
    CHECK(!qa_vfs_acquire_from(other, ascii_id, "caf\xe9.bin", &again, &error));
    CHECK(error.code == QA_ERROR_NOT_FOUND);
    CHECK(!qa_vfs_acquire_from(other, exact_id, "Maps/item.txt", &again, &error));
    CHECK(qa_vfs_acquire_from(vfs, loose_id, "alias", &again, &error));
    expect_text(again, "upper");
    qa_resource_release(again);
    CHECK(qa_vfs_acquire_from(vfs, loose_id, "absolute", &again, &error));
    expect_text(again, "upper");
    qa_resource_release(again);
    CHECK(!qa_vfs_acquire_from(vfs, loose_id, "escape", &again, &error));
    CHECK(error.code == QA_ERROR_IO);

    const char *bad_paths[] = {"", "/maps/item.txt", "../outside", "maps/../item.txt", "maps//item.txt", "./maps/item.txt", "C:\\file", "maps/"};
    for (size_t i = 0; i < sizeof(bad_paths) / sizeof(bad_paths[0]); i++) {
        CHECK(!qa_vfs_acquire(vfs, bad_paths[i], &again, NULL, &error));
        CHECK(error.code == QA_ERROR_ARGUMENT && again == NULL);
    }
    CHECK(!qa_vfs_write(vfs, pak_id, "x", (qa_bytes){0}, &error));
    CHECK(!qa_vfs_write(vfs, loose_id, "alias", (qa_bytes){0}, &error));
    CHECK(qa_vfs_write(vfs, loose_id, "maps/item.txt", (qa_bytes){(const uint8_t *)"new", 3}, &error));
    CHECK(qa_vfs_acquire_from(vfs, loose_id, "maps/item.txt", &again, &error));
    CHECK(again != loose_resource && qa_resource_id(again) != qa_resource_id(loose_resource));
    expect_text(again, "new");
    expect_text(loose_resource, "loose");
    qa_resource_release(again);
    CHECK(qa_vfs_write(vfs, loose_id, "saves/test.sav", (qa_bytes){0}, &error));
    CHECK(qa_vfs_acquire_from(vfs, loose_id, "saves/test.sav", &again, &error));
    CHECK(qa_resource_bytes(again).size == 0);
    qa_resource_release(again);
    CHECK(qa_vfs_remove(vfs, loose_id, "saves/test.sav", &error));
    CHECK(!qa_vfs_remove(vfs, loose_id, "escape", &error));
    CHECK(rename(loose, renamed) == 0);
    CHECK(mkdir(loose, 0700) == 0);
    CHECK(qa_vfs_acquire_from(vfs, loose_id, "maps/item.txt", &again, &error));
    expect_text(again, "new");
    qa_resource_release(again);
    CHECK(rmdir(loose) == 0 && rename(renamed, loose) == 0);

    int changed = open(pak, O_WRONLY | O_APPEND);
    CHECK(changed >= 0 && write(changed, "x", 1) == 1 && close(changed) == 0);
    CHECK(!qa_vfs_acquire_from(vfs, pak_id, "maps/item.txt", &again, &error));
    CHECK(error.code == QA_ERROR_IO);
    expect_text(pak_resource, "first");
    CHECK(qa_vfs_unmount(vfs, pak_id, &error));
    CHECK(!qa_vfs_unmount(vfs, pak_id, &error));
    qa_resource_pool_destroy(pool);
    qa_vfs_destroy(vfs);
    qa_vfs_destroy(other);
    expect_text(pak_resource, "first");
    expect_text(zip_resource, "zip-last");
    expect_text(loose_resource, "loose");
    qa_resource_retain(pak_resource);
    qa_resource_release(pak_resource);
    qa_resource_release(pak_resource);
    qa_resource_release(zip_resource);
    qa_resource_release(loose_resource);
    qa_resource_release(NULL);
    qa_vfs_destroy(NULL);
    qa_resource_pool_destroy(NULL);

    CHECK(unlink(pak) == 0 && unlink(zip) == 0 && unlink(item) == 0);
    CHECK(unlink(uppercase) == 0 && unlink(lowercase) == 0 && unlink(latin) == 0);
    CHECK(unlink(alias) == 0 && unlink(absolute) == 0 && unlink(escape) == 0);
    CHECK(unlink(outside) == 0 && rmdir(map_dir) == 0 && rmdir(save_dir) == 0);
    CHECK(rmdir(loose) == 0 && rmdir(directory) == 0);
    puts("VFS tests passed");
    return EXIT_SUCCESS;
}
