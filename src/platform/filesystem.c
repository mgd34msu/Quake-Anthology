#include "filesystem_internal.h"

#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *source)
{
    size_t length = strlen(source);
    if (length == SIZE_MAX)
        return NULL;
    char *copy = malloc(length + 1);
    if (copy != NULL)
        memcpy(copy, source, length + 1);
    return copy;
}

bool qa_fs_identity_equal(const qa_fs_identity *left,
                          const qa_fs_identity *right)
{
    return left != NULL && right != NULL
        && memcmp(left->words, right->words, sizeof(left->words)) == 0;
}

uint64_t qa_fs_identity_hash(const qa_fs_identity *identity)
{
    if (identity == NULL)
        return 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < QA_FS_IDENTITY_WORDS; ++i) {
        uint64_t word = identity->words[i];
        for (unsigned int byte = 0; byte < 8; ++byte) {
            hash ^= word & UINT64_C(0xff);
            hash *= UINT64_C(1099511628211);
            word >>= 8;
        }
    }
    hash ^= hash >> 33;
    hash *= UINT64_C(0xff51afd7ed558ccd);
    return hash ^ (hash >> 33);
}

bool qa_fs_relative_valid(const char *path, bool allow_root, qa_error *error)
{
    if (path == NULL || path[0] == '\0' || strcmp(path, ".") == 0) {
        if (allow_root && path != NULL)
            return true;
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "filesystem path is empty");
        return false;
    }
    if (path[0] == '/' || path[0] == '\\') {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "filesystem path is not relative");
        return false;
    }
    const char *component = path;
    for (const char *cursor = path;; ++cursor) {
        if (*cursor == '\\' || *cursor == ':') {
            qa_error_set(error, QA_ERROR_ARGUMENT, (size_t)(cursor - path),
                         "unsafe filesystem path");
            return false;
        }
        if (*cursor != '/' && *cursor != '\0')
            continue;
        size_t length = (size_t)(cursor - component);
        if (length == 0 || (length == 1 && component[0] == '.')
            || (length == 2 && component[0] == '.' && component[1] == '.')) {
            qa_error_set(error, QA_ERROR_ARGUMENT, (size_t)(component - path),
                         "unsafe filesystem path");
            return false;
        }
        if (*cursor == '\0')
            return true;
        component = cursor + 1;
    }
}

bool qa_fs_listing_add(qa_fs_listing *listing, const char *name,
                       qa_fs_entry_kind kind, qa_error *error)
{
    if (listing->count == listing->capacity) {
        size_t capacity = listing->capacity == 0 ? 16 : listing->capacity * 2;
        if (capacity < listing->capacity
            || capacity > SIZE_MAX / sizeof(*listing->entries)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "filesystem listing is too large");
            return false;
        }
        qa_fs_entry *entries = realloc(listing->entries,
                                       capacity * sizeof(*entries));
        if (entries == NULL) {
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "cannot grow filesystem listing");
            return false;
        }
        listing->entries = entries;
        listing->capacity = capacity;
    }
    char *copy = copy_string(name);
    if (copy == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0,
                     "cannot retain filesystem entry name");
        return false;
    }
    listing->entries[listing->count++] = (qa_fs_entry){copy, kind};
    return true;
}

void qa_fs_listing_free(qa_fs_listing *listing)
{
    if (listing == NULL)
        return;
    for (size_t i = 0; i < listing->count; ++i)
        free(listing->entries[i].name);
    free(listing->entries);
    *listing = (qa_fs_listing){0};
}

static bool exact_name(const char *left, const char *right, void *context)
{
    (void)context;
    return strcmp(left, right) == 0;
}

static bool append_component(char **path, size_t *length, size_t *capacity,
                             const char *component, qa_error *error)
{
    size_t component_length = strlen(component);
    size_t separator = *length == 0 ? 0 : 1;
    if (*length > SIZE_MAX - separator - 1
        || component_length > SIZE_MAX - *length - separator - 1) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "resolved path is too long");
        return false;
    }
    size_t needed = *length + separator + component_length + 1;
    if (needed > *capacity) {
        size_t next = *capacity == 0 ? 64 : *capacity;
        while (next < needed) {
            if (next > SIZE_MAX / 2) {
                next = needed;
                break;
            }
            next *= 2;
        }
        char *grown = realloc(*path, next);
        if (grown == NULL) {
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "cannot allocate resolved path");
            return false;
        }
        *path = grown;
        *capacity = next;
    }
    if (separator != 0)
        (*path)[(*length)++] = '/';
    memcpy(*path + *length, component, component_length + 1);
    *length += component_length;
    return true;
}

bool qa_fs_root_resolve(qa_fs_root *root, const char *relative,
                        qa_fs_name_equal_fn equality, void *context,
                        bool reject_ambiguous, char **out, qa_error *error)
{
    if (out != NULL)
        *out = NULL;
    if (root == NULL || out == NULL
        || !qa_fs_relative_valid(relative, true, error))
        return false;
    if (relative[0] == '\0' || strcmp(relative, ".") == 0) {
        *out = copy_string("");
        if (*out == NULL)
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "cannot allocate resolved root path");
        return *out != NULL;
    }
    if (equality == NULL)
        equality = exact_name;

    char *resolved = NULL;
    size_t resolved_length = 0, resolved_capacity = 0;
    const char *component = relative;
    while (*component != '\0') {
        const char *slash = strchr(component, '/');
        size_t length = slash == NULL ? strlen(component)
                                      : (size_t)(slash - component);
        char *wanted = malloc(length + 1);
        if (wanted == NULL) {
            qa_error_set(error, QA_ERROR_MEMORY, 0,
                         "cannot allocate path component");
            free(resolved);
            return false;
        }
        memcpy(wanted, component, length);
        wanted[length] = '\0';

        qa_fs_listing listing = {0};
        const char *parent = resolved_length == 0 ? "" : resolved;
        if (!qa_fs_root_list(root, parent, &listing, error)) {
            free(wanted);
            free(resolved);
            return false;
        }
        const char *choice = NULL;
        bool exact = false, ambiguous = false;
        for (size_t i = 0; i < listing.count; ++i) {
            const char *name = listing.entries[i].name;
            if (!equality(name, wanted, context))
                continue;
            if (strcmp(name, wanted) == 0) {
                choice = name;
                exact = true;
                break;
            }
            if (choice != NULL) {
                ambiguous = true;
                if (strcmp(name, choice) < 0)
                    choice = name;
            } else {
                choice = name;
            }
        }
        if (choice == NULL || (ambiguous && !exact && reject_ambiguous)) {
            qa_error_set(error, choice == NULL ? QA_ERROR_NOT_FOUND
                                               : QA_ERROR_FORMAT,
                         (size_t)(component - relative),
                         choice == NULL ? "filesystem path not found: %s"
                                        : "ambiguous filesystem path: %s",
                         relative);
            qa_fs_listing_free(&listing);
            free(wanted);
            free(resolved);
            return false;
        }
        bool appended = append_component(&resolved, &resolved_length,
                                         &resolved_capacity, choice, error);
        qa_fs_listing_free(&listing);
        free(wanted);
        if (!appended) {
            free(resolved);
            return false;
        }
        component = slash == NULL ? component + length : slash + 1;
    }
    *out = resolved;
    return true;
}
