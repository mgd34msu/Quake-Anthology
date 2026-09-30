#ifndef QA_FILESYSTEM_H
#define QA_FILESYSTEM_H

#include "qa/common.h"

#define QA_FS_IDENTITY_WORDS 7

typedef struct qa_fs_root qa_fs_root;
typedef struct qa_fs_file qa_fs_file;
typedef struct qa_fs_stream qa_fs_stream;

typedef struct qa_fs_identity {
    uint64_t words[QA_FS_IDENTITY_WORDS];
} qa_fs_identity;

typedef enum qa_fs_entry_kind {
    QA_FS_MISSING,
    QA_FS_REGULAR,
    QA_FS_DIRECTORY,
    QA_FS_LINK,
    QA_FS_OTHER
} qa_fs_entry_kind;

typedef struct qa_fs_entry {
    char *name;
    qa_fs_entry_kind kind;
} qa_fs_entry;

typedef struct qa_fs_listing {
    qa_fs_entry *entries;
    size_t count;
    size_t capacity;
} qa_fs_listing;

typedef bool (*qa_fs_name_equal_fn)(const char *left, const char *right,
                                     void *context);

bool qa_fs_identity_equal(const qa_fs_identity *left,
                          const qa_fs_identity *right);
uint64_t qa_fs_identity_hash(const qa_fs_identity *identity);
uint64_t qa_fs_identity_size(const qa_fs_identity *identity);

/* A linked root is admitted and retained by its final directory handle. Child
 * reads may follow links only when the final opened object remains below that
 * root. Writable traversal never follows a child link or reparse point. */
bool qa_fs_root_open(const char *path, qa_fs_root **out, qa_error *error);
void qa_fs_root_retain(qa_fs_root *root);
void qa_fs_root_close(qa_fs_root *root);

/* Components are already normalized, slash-separated relative names. NULL
 * equality requests exact spelling. Exact matches win; otherwise ambiguous
 * folded matches may either fail or select the bytewise-smallest spelling. */
bool qa_fs_root_resolve(qa_fs_root *root, const char *relative,
                        qa_fs_name_equal_fn equality, void *context,
                        bool reject_ambiguous, char **out, qa_error *error);
bool qa_fs_root_join(const qa_fs_root *root, const char *relative,
                     char **out, qa_error *error);

/* Listings own entry names and never follow child links for classification. */
bool qa_fs_path_list(const char *path, qa_fs_listing *out, qa_error *error);
bool qa_fs_root_list(qa_fs_root *root, const char *relative,
                     qa_fs_listing *out, qa_error *error);
void qa_fs_listing_free(qa_fs_listing *listing);

/* Opens the final object through the retained root and follows links only when
 * the opened object remains below that root. Missing paths publish MISSING. */
bool qa_fs_root_status(qa_fs_root *root, const char *relative,
                       qa_fs_entry_kind *kind, qa_fs_identity *identity,
                       qa_error *error);

/* Missing paths publish QA_FS_MISSING without failing. follow_links controls
 * final-component link handling; identity may be NULL. */
bool qa_fs_path_status(const char *path, bool follow_links,
                       qa_fs_entry_kind *kind, qa_fs_identity *identity,
                       qa_error *error);

bool qa_fs_file_open(const char *path, qa_fs_file **out,
                     qa_fs_identity *identity, qa_error *error);
bool qa_fs_root_file_open(qa_fs_root *root, const char *relative,
                          qa_fs_file **out, qa_fs_identity *identity,
                          qa_error *error);
void qa_fs_file_retain(qa_fs_file *file);
void qa_fs_file_close(qa_fs_file *file);
bool qa_fs_file_identity(qa_fs_file *file, qa_fs_identity *out,
                         qa_error *error);
/* Rechecks the original native path as well as the retained handle. Missing or
 * replaced paths report unchanged=false without turning that change into I/O. */
bool qa_fs_file_path_unchanged(qa_fs_file *file,
                               const qa_fs_identity *expected,
                               bool *unchanged, qa_error *error);
/* Reads from the retained handle and rejects mutation before publication. */
bool qa_fs_file_read_snapshot(qa_fs_file *file,
                              const qa_fs_identity *expected,
                              qa_buffer *out, qa_error *error);

bool qa_fs_root_replace(qa_fs_root *root, const char *relative,
                        qa_bytes bytes, uint64_t nonce, qa_error *error);
/* Publish a complete temporary file. Exclusive admission returns true with
 * created=false when a leaf already exists. Private files are owner-only.
 * A failed durability sync may follow publication; callers must not retry a
 * private credential write with stale data. */
bool qa_fs_root_publish(qa_fs_root *, const char *, qa_bytes, uint64_t nonce,
                        bool exclusive, bool private_file, bool *created, qa_error *);
bool qa_fs_root_remove(qa_fs_root *root, const char *relative,
                       qa_error *error);

typedef enum qa_fs_stream_mode {
    QA_FS_STREAM_WRITE,
    QA_FS_STREAM_APPEND,
    QA_FS_STREAM_APPEND_SYNC
} qa_fs_stream_mode;

/* Fresh WRITE truncates; resumed WRITE requires the existing file. Append
 * modes create missing files. initial_size returns the opened file length. */
bool qa_fs_root_stream_open(qa_fs_root *root, const char *relative,
                            qa_fs_stream_mode mode, bool resume,
                            qa_fs_stream **out, uint64_t *initial_size,
                            qa_error *error);
/* Positional mode writes at position. Append modes ignore it and publish the
 * resulting file size. Partial progress is returned through written. */
bool qa_fs_stream_write(qa_fs_stream *stream, qa_bytes bytes,
                        uint64_t position, size_t *written,
                        uint64_t *resulting_size, qa_error *error);
bool qa_fs_stream_size(qa_fs_stream *stream, uint64_t *out,
                       qa_error *error);
/* Flush the retained writable handle without changing file identity/position. */
bool qa_fs_stream_sync(qa_fs_stream *, qa_error *);
void qa_fs_stream_close(qa_fs_stream *stream);

typedef struct qa_fs_stage qa_fs_stage;
/* Retains the contained target parent and a private temporary file named by
 * nonce. Fresh creation is exclusive; resume requires that exact regular file
 * and never truncates. The caller persists target/nonce/content identity for
 * recovery and revalidates retained bytes before resuming a transfer. */
bool qa_fs_stage_open(qa_fs_root *, const char *target, uint64_t nonce,
                       bool resume, qa_fs_stage **, uint64_t *initial_size, qa_error *);
bool qa_fs_stage_size(qa_fs_stage *, uint64_t *, qa_error *);
bool qa_fs_stage_read(qa_fs_stage *, uint64_t offset, void *, size_t capacity,
                       size_t *read, qa_error *);
bool qa_fs_stage_write(qa_fs_stage *, uint64_t offset, qa_bytes,
                        size_t *written, qa_error *);
/* Flushes and freezes writes. Consumers inspect/hash by read-at, then publish
 * the same retained file identity. Exclusive publication never replaces an
 * installed target. created is valid even after a durability-sync failure. */
bool qa_fs_stage_seal(qa_fs_stage *, qa_fs_identity *, qa_error *);
typedef struct qa_fs_stage_mapping qa_fs_stage_mapping;
/* Read-only, demand-paged inspection of the same sealed file handle. No file
 * bytes are copied into a heap buffer. The mapping survives stage close and
 * expires on unmap; callers unmap before publication/teardown. */
bool qa_fs_stage_map(qa_fs_stage *, qa_fs_stage_mapping **, qa_error *);
qa_bytes qa_fs_stage_mapping_bytes(const qa_fs_stage_mapping *);
void qa_fs_stage_unmap(qa_fs_stage_mapping *);
bool qa_fs_stage_publish(qa_fs_stage *, const qa_fs_identity *,
                          bool exclusive, bool *created, qa_error *);
/* keep=true retains an unpublished temporary for restart; false removes it.
 * Publication consumes only the temporary name, not the retained stage handle. */
void qa_fs_stage_close(qa_fs_stage *, bool keep);

#endif
