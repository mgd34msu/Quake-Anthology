#include "internal.h"

typedef struct mod_root {
    qa_fs_root *root;
    const char *prefix;
} mod_root;

static bool name_equal(const char *left, const char *right, void *context)
{
    (void)context; return catalog_ascii_equal(left, right);
}

static bool path(const mod_root *root, const char *directory, const char *leaf,
    char **out, qa_error *error)
{
    size_t a = strlen(root->prefix), b = strlen(directory), c = strlen(leaf);
    if (a > SIZE_MAX - 3 || b > SIZE_MAX - a - 3 || c > SIZE_MAX - a - b - 3) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Q3 mod path is too long"); return false;
    }
    char *text = malloc(a + b + c + 3);
    if (!text) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining Q3 mod path"); return false; }
    size_t used = 0;
    if (a) { memcpy(text, root->prefix, a); used = a; text[used++] = '/'; }
    memcpy(text + used, directory, b); used += b;
    if (c) { text[used++] = '/'; memcpy(text + used, leaf, c); used += c; }
    text[used] = 0; *out = text; return true;
}

/* Sys_ListFiles reports no entries when a directory cannot be opened. Memory
 * failures remain errors rather than publishing a partial native menu. */
static bool list(const mod_root *root, const char *relative, qa_fs_listing *out, qa_error *error)
{
    if (!root->root) return true;
    qa_error local = {0};
    if (qa_fs_root_list(root->root, relative, out, &local)) return true;
    if (local.code != QA_ERROR_MEMORY) return true;
    if (error) *error = local;
    return false;
}

static bool directory(const mod_root *root, const qa_fs_entry *entry, bool *out, qa_error *error)
{
    *out = entry->kind == QA_FS_DIRECTORY;
    if (entry->kind != QA_FS_LINK) return true;
    char *relative = NULL;
    if (!path(root, entry->name, "", &relative, error)) return false;
    qa_fs_entry_kind kind; qa_error local = {0};
    bool okay = qa_fs_root_status(root->root, relative, &kind, NULL, &local);
    free(relative);
    if (!okay && local.code == QA_ERROR_MEMORY) { if (error) *error = local; return false; }
    *out = okay && kind == QA_FS_DIRECTORY; return true;
}

static bool packages(const mod_root *root, const char *name, bool *present, qa_error *error)
{
    *present = false;
    if (!root->root) return true;
    char *relative = NULL; qa_fs_listing files = {0};
    if (!path(root, name, "", &relative, error)) return false;
    bool okay = list(root, relative, &files, error);
    for (size_t i = 0; okay && !*present && i < files.count; ++i) {
        const qa_fs_entry *entry = files.entries + i;
        if (!catalog_suffix(entry->name, ".pk3")) continue;
        if (entry->kind == QA_FS_LINK) {
            char *file_path = NULL; qa_fs_entry_kind kind; qa_error local = {0};
            okay = path(root, name, entry->name, &file_path, error);
            if (okay) {
                bool found = qa_fs_root_status(root->root, file_path, &kind, NULL, &local);
                if (!found && local.code == QA_ERROR_MEMORY) { if (error) *error = local; okay = false; }
                *present = found && kind != QA_FS_MISSING && kind != QA_FS_DIRECTORY;
            }
            free(file_path);
        } else *present = entry->kind != QA_FS_DIRECTORY && entry->kind != QA_FS_MISSING;
    }
    free(relative); qa_fs_listing_free(&files); return okay;
}

static bool description(const mod_root roots[2], const char *name, char out[49],
    bool *found, qa_error *error)
{
    *found = false;
    for (size_t i = 0; i < 2; ++i) {
        if (!roots[i].root) continue;
        char *relative = NULL; qa_fs_file *file = NULL; qa_fs_identity identity;
        if (!path(roots + i, name, "description.txt", &relative, error)) return false;
        qa_error local = {0};
        bool opened = qa_fs_root_file_open(roots[i].root, relative, &file, &identity, &local);
        free(relative);
        if (!opened) {
            if (local.code == QA_ERROR_MEMORY) { if (error) *error = local; return false; }
            continue;
        }
        if (!qa_fs_identity_size(&identity)) { qa_fs_file_close(file); return true; }
        size_t count = 0;
        bool okay = qa_fs_file_read_prefix(file, &identity, out, 48, &count, error);
        qa_fs_file_close(file);
        if (!okay) return false;
        out[count] = 0; *found = true; return true;
    }
    return true;
}

static bool pair(qa_vfs_listing *out, const char *name, const char *title, qa_error *error)
{
    size_t a = strlen(name), b = strlen(title);
    if (out->count > SIZE_MAX / sizeof(*out->names) - 2) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Q3 mod list is too large"); return false;
    }
    char *directory_copy = malloc(a + 1), *description_copy = malloc(b + 1);
    if (!directory_copy || !description_copy) {
        free(directory_copy); free(description_copy);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining Q3 mod list pair"); return false;
    }
    char **names = realloc(out->names, (out->count + 2) * sizeof(*names));
    if (!names) {
        free(directory_copy); free(description_copy);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining Q3 mod list"); return false;
    }
    memcpy(directory_copy, name, a + 1); memcpy(description_copy, title, b + 1);
    out->names = names; out->names[out->count++] = directory_copy;
    out->names[out->count++] = description_copy; return true;
}

bool qa_catalog_q3_mod_list(const qa_catalog *catalog, qa_vfs_listing *out, qa_error *error)
{
    if (!catalog || !out || out->names || out->count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q3 mod list requires its retained catalog and empty output"); return false;
    }
    mod_root roots[2] = {{qa_catalog_q3_download_root(catalog), ""},
        {qa_vfs_mount_root(catalog->mounts, catalog->corpus_mount), ""}};
    char *base_prefix = NULL;
    if (roots[1].root) {
        qa_error local = {0};
        if (qa_fs_root_resolve(roots[1].root, "q3a", name_equal, NULL, false, &base_prefix, &local))
            roots[1].prefix = base_prefix;
        else {
            if (local.code == QA_ERROR_MEMORY) { if (error) *error = local; return false; }
            roots[1].root = NULL;
        }
    }
    qa_fs_listing directories[2] = {{0}, {0}}; bool okay = true;
    for (size_t i = 0; okay && i < 2; ++i) okay = list(roots + i, roots[i].prefix, directories + i, error);
    for (size_t r = 0; okay && r < 2; ++r) for (size_t i = 0; okay && i < directories[r].count; ++i) {
        const qa_fs_entry *entry = directories[r].entries + i; bool is_directory;
        okay = directory(roots + r, entry, &is_directory, error);
        if (!okay || !is_directory || entry->name[0] == '.' || catalog_ascii_equal(entry->name, "baseq3")) continue;
        bool duplicate = false;
        for (size_t p = 0; !duplicate && p <= r; ++p) {
            size_t limit = p == r ? i : directories[p].count;
            for (size_t j = 0; !duplicate && j < limit; ++j) {
                if (!catalog_ascii_equal(entry->name, directories[p].entries[j].name)) continue;
                bool earlier_directory;
                okay = directory(roots + p, directories[p].entries + j, &earlier_directory, error);
                if (!okay) break;
                duplicate = earlier_directory;
            }
            if (!okay) break;
        }
        if (!okay || duplicate) continue;
        bool present;
        okay = packages(roots + 1, entry->name, &present, error);
        if (okay && !present) okay = packages(roots, entry->name, &present, error);
        if (!okay || !present) continue;
        char title[49]; bool described;
        okay = description(roots, entry->name, title, &described, error) &&
            pair(out, entry->name, described ? title : entry->name, error);
    }
    qa_fs_listing_free(directories); qa_fs_listing_free(directories + 1);
    free(base_prefix);
    if (!okay) qa_vfs_listing_free(out);
    return okay;
}
