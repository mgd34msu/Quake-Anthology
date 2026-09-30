#include "internal.h"
#include "qa/persistence_slots.h"

bool qa_save_slot_inspect(qa_fs_root *root, const char *name,
                          qa_save_metadata *out, qa_error *error)
{
    if (!out)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Missing save slot metadata output");
    qa_save_image *image = NULL;
    if (!qa_save_read(root, name, &image, error))
        return false;
    *out = *qa_save_image_metadata(image);
    qa_save_image_destroy(image);
    return true;
}

static char *slot_path(const char *directory, const char *name, qa_error *error)
{
    size_t prefix = strlen(directory), length = strlen(name);
    size_t separator = prefix ? 1u : 0u;
    if (prefix > SIZE_MAX - separator - 1 || length > SIZE_MAX - prefix - separator - 1) {
        persistence_fail(error, QA_ERROR_MEMORY, "Save slot path exceeds memory extent");
        return NULL;
    }
    char *path = malloc(prefix + separator + length + 1);
    if (!path) {
        persistence_fail(error, QA_ERROR_MEMORY, "Allocating contained save slot path");
        return NULL;
    }
    memcpy(path, directory, prefix);
    if (separator)
        path[prefix] = '/';
    memcpy(path + prefix + separator, name, length + 1);
    return path;
}

static int slot_compare(const void *left, const void *right)
{
    const qa_save_slot_entry *a = left, *b = right;
    return strcmp(a->name, b->name);
}

void qa_save_slot_listing_free(qa_save_slot_listing *listing)
{
    if (!listing)
        return;
    for (size_t i = 0; i < listing->count; ++i)
        free(listing->entries[i].name);
    free(listing->entries);
    *listing = (qa_save_slot_listing){0};
}

bool qa_save_slots_list(qa_fs_root *root, const char *directory,
                        qa_save_slot_listing *out, qa_error *error)
{
    if (!root || !directory || !out)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Missing save slot directory or output");
    char *probe = slot_path(directory, "listing.sav", error);
    if (!probe)
        return false;
    bool valid = qa_save_slot_name(probe, error);
    free(probe);
    if (!valid)
        return false;
    qa_fs_listing files = {0};
    qa_error local = {0};
    if (!qa_fs_root_list(root, directory, &files, &local)) {
        qa_fs_listing_free(&files);
        if (local.code == QA_ERROR_NOT_FOUND) {
            *out = (qa_save_slot_listing){0};
            return true;
        }
        if (error)
            *error = local;
        return false;
    }
    qa_save_slot_listing candidate = {0};
    if (files.count) {
        candidate.entries = calloc(files.count, sizeof(*candidate.entries));
        if (!candidate.entries) {
            qa_fs_listing_free(&files);
            return persistence_fail(error, QA_ERROR_MEMORY, "Allocating save slot inventory");
        }
    }
    bool ok = true;
    for (size_t i = 0; ok && i < files.count; ++i) {
        if (files.entries[i].kind != QA_FS_REGULAR)
            continue;
        char *path = slot_path(directory, files.entries[i].name, error);
        if (!path) {
            ok = false;
            break;
        }
        if (!qa_save_slot_name(path, NULL)) {
            free(path);
            continue;
        }
        qa_save_slot_entry *entry = candidate.entries + candidate.count++;
        entry->name = path;
        if (!qa_save_slot_inspect(root, path, &entry->metadata, &entry->error)) {
            if (entry->error.code == QA_ERROR_MEMORY) {
                if (error)
                    *error = entry->error;
                ok = false;
            } else if (entry->error.code == QA_OK) {
                qa_error_set(&entry->error, QA_ERROR_FORMAT, 0, "Unable to verify save slot image");
            }
        }
    }
    qa_fs_listing_free(&files);
    if (!ok) {
        qa_save_slot_listing_free(&candidate);
        return false;
    }
    if (candidate.count > 1)
        qsort(candidate.entries, candidate.count, sizeof(*candidate.entries), slot_compare);
    *out = candidate;
    return true;
}
