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

bool qa_fs_root_identity_is(const qa_fs_root *root, const qa_fs_identity *identity)
{
    qa_fs_object_reference actual;
    return identity && qa_fs_root_reference_read(root, &actual)
        && actual.words[0] == identity->words[0]
        && actual.words[1] == identity->words[1]
        && ((actual.platform == 1 && actual.words[2] == 0)
            || (actual.platform == 2 && actual.words[2] == identity->words[5]));
}

uint64_t qa_fs_identity_size(const qa_fs_identity *identity)
{
    return identity != NULL ? identity->words[2] : 0;
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

static bool file_read_begin(qa_fs_file *file, const qa_fs_identity *expected,
    const char *operation, qa_error *error)
{
    qa_fs_identity before;
    if (!qa_fs_file_identity(file, &before, error)) return false;
    if (!qa_fs_identity_equal(expected, &before)) {
        qa_error_set(error, QA_ERROR_IO, 0, "resource changed before %s", operation);
        return false;
    }
    return true;
}

static bool file_read_finish(qa_fs_file *file, const qa_fs_identity *expected,
    const char *operation, bool check_path, qa_error *error)
{
    qa_fs_identity after;
    if (!qa_fs_file_identity(file, &after, error)) return false;
    if (!qa_fs_identity_equal(expected, &after)) {
        qa_error_set(error, QA_ERROR_IO, 0, "resource changed during %s", operation);
        return false;
    }
    if (check_path) {
        bool unchanged = false;
        if (!qa_fs_file_path_unchanged(file, expected, &unchanged, error)) return false;
        if (!unchanged) {
            qa_error_set(error, QA_ERROR_IO, 0, "resource path changed during %s", operation);
            return false;
        }
    }
    return true;
}

static bool file_read_exact(qa_fs_file *file, size_t offset, void *bytes,
    size_t size, qa_error *error)
{
    size_t done = 0;
    while (done < size) {
        size_t received = 0;
        if (!qa_fs_file_read_at_native(file, (uint64_t)(offset + done),
            (uint8_t *)bytes + done, size - done, &received, error)) return false;
        done += received;
    }
    return true;
}

static bool file_snapshot_size(const qa_fs_identity *identity, qa_error *error)
{
    if (identity->words[2] > (uint64_t)PTRDIFF_MAX || identity->words[2] > (uint64_t)SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "file is too large for a memory snapshot");
        return false;
    }
    return true;
}

bool qa_fs_file_read_prefix(qa_fs_file *file, const qa_fs_identity *expected,
    void *bytes, size_t capacity, size_t *received, qa_error *error)
{
    if (received) *received = 0;
    if (!file || !expected || !received || (capacity && !bytes) || capacity > PTRDIFF_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prefix read needs a retained file, identity and bounded output");
        return false;
    }
    if (!file_read_begin(file, expected, "prefix read", error)) return false;
    size_t limit = expected->words[2] < (uint64_t)capacity ? (size_t)expected->words[2] : capacity;
    if (!file_read_exact(file, 0, bytes, limit, error) ||
        !file_read_finish(file, expected, "prefix read", false, error)) return false;
    *received = limit;
    return true;
}

bool qa_fs_file_read_snapshot(qa_fs_file *file, const qa_fs_identity *expected,
    qa_buffer *out, qa_error *error)
{
    if (out) *out = (qa_buffer){0};
    if (!file || !expected || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "snapshot read needs file, identity, and output");
        return false;
    }
    if (!file_read_begin(file, expected, "snapshot read", error) ||
        !file_snapshot_size(expected, error)) return false;
    qa_buffer result = {.size = (size_t)expected->words[2]};
    if (result.size) {
        result.data = malloc(result.size);
        if (!result.data) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate file snapshot");
            return false;
        }
    }
    if (!file_read_exact(file, 0, result.data, result.size, error) ||
        !file_read_finish(file, expected, "snapshot read", true, error)) {
        qa_buffer_free(&result);
        return false;
    }
    *out = result;
    return true;
}

bool qa_fs_file_read_range(qa_fs_file *file, const qa_fs_identity *expected,
    size_t offset, void *bytes, size_t size, qa_error *error)
{
    if (!file || !expected || (size && !bytes) || offset > PTRDIFF_MAX ||
        size > (size_t)PTRDIFF_MAX - offset || (uint64_t)offset > expected->words[2] ||
        (uint64_t)size > expected->words[2] - (uint64_t)offset) {
        qa_error_set(error, QA_ERROR_ARGUMENT, offset, "Range read needs an admitted file span");
        return false;
    }
    return file_read_begin(file, expected, "range read", error) &&
        file_read_exact(file, offset, bytes, size, error) &&
        file_read_finish(file, expected, "range read", true, error);
}

bool qa_fs_file_snapshot_matches(qa_fs_file *file, const qa_fs_identity *expected,
    qa_bytes bytes, bool *matches, qa_error *error)
{
    if (matches) *matches = false;
    if (!file || !expected || !matches || (bytes.size && !bytes.data) || bytes.size > PTRDIFF_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Snapshot comparison needs a retained file, identity, bytes and output");
        return false;
    }
    if (!file_read_begin(file, expected, "snapshot comparison", error) ||
        !file_snapshot_size(expected, error)) return false;
    size_t size = (size_t)expected->words[2];
    bool equal = size == bytes.size;
    uint8_t scratch[64u * 1024u];
    for (size_t offset = 0; offset < size;) {
        size_t amount = size - offset;
        if (amount > sizeof(scratch)) amount = sizeof(scratch);
        if (!file_read_exact(file, offset, scratch, amount, error)) return false;
        if (size == bytes.size && memcmp(scratch, bytes.data + offset, amount)) equal = false;
        offset += amount;
    }
    if (!file_read_finish(file, expected, "snapshot comparison", true, error)) return false;
    *matches = equal;
    return true;
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

bool qa_fs_stream_reference_valid(const qa_fs_stream_reference *reference, qa_error *error)
{
    if (!reference || reference->root.platform < 1 || reference->root.platform > 2 ||
        reference->object.platform != reference->root.platform ||
        (reference->root.platform == 1 && (reference->root.words[2] || reference->object.words[2])) ||
        reference->mode < QA_FS_STREAM_WRITE || reference->mode > QA_FS_STREAM_APPEND_SYNC) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid writable stream reference");
        return false;
    }
    return qa_fs_relative_valid(reference->path, false, error);
}

bool qa_fs_stream_resume(qa_fs_root *root, const qa_fs_stream_reference *reference,
    qa_fs_stream **out, uint64_t *size, qa_error *error)
{
    if (out) *out = NULL;
    if (size) *size = 0;
    if (!qa_fs_stream_reference_valid(reference, error)) return false;
    qa_fs_object_reference actual;
    if (!qa_fs_root_reference_read(root, &actual) || actual.platform != reference->root.platform ||
        memcmp(actual.words, reference->root.words, sizeof(actual.words))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Writable continuation has a different retained root");
        return false;
    }
    return qa_fs_root_stream_open(root, reference->path, reference->mode, true, out, size, error);
}

bool qa_fs_stream_resume_mapped(qa_fs_root *destination, const qa_fs_stream_reference *reference,
    const qa_fs_stream_resolver *resolver, qa_fs_stream **out, uint64_t *size, qa_error *error)
{
    if (out) *out = NULL;
    if (size) *size = 0;
    if (!destination || !out || !size || !resolver || !resolver->context || !resolver->root) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Writable continuation needs its actual root resolver");
        return false;
    }
    if (!qa_fs_stream_reference_valid(reference, error)) return false;
    qa_fs_root *mapped = NULL;
    bool ok = resolver->root(resolver->context, reference, &mapped, error);
    if (ok && !qa_fs_root_same_object(destination, mapped)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Writable root resolver differs from the actual destination owner");
        ok = false;
    }
    if (ok) ok = qa_fs_root_stream_open(mapped, reference->path, reference->mode, true, out, size, error);
    qa_fs_root_close(mapped);
    return ok;
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
