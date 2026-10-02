#ifndef QA_FILESYSTEM_H
#define QA_FILESYSTEM_H

#include "qa/common.h"

#define QA_FS_IDENTITY_WORDS 7

typedef struct qa_fs_root qa_fs_root;
typedef struct qa_fs_file qa_fs_file;
typedef struct qa_fs_stream qa_fs_stream;
typedef struct qa_fs_opened_file qa_fs_opened_file;

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
typedef struct qa_fs_timestamp {
    int64_t seconds;
    uint32_t nanoseconds;
} qa_fs_timestamp;
/* Extracts retained modification metadata as Unix time without native I/O. */
bool qa_fs_identity_modified_time(const qa_fs_identity *, qa_fs_timestamp *);

/* A linked root is admitted and retained by its final directory handle. Child
 * reads may follow links only when the final opened object remains below that
 * root. Writable traversal never follows a child link or reparse point. */
bool qa_fs_root_open(const char *path, qa_fs_root **out, qa_error *error);
void qa_fs_root_retain(qa_fs_root *root);
void qa_fs_root_close(qa_fs_root *root);
/* Compares the native directory identity retained at admission, without I/O.
 * Both roots must be non-NULL and remain retained by their callers. */
bool qa_fs_root_same_object(const qa_fs_root *left, const qa_fs_root *right);
/* Creates an actual directory below the held writable root. Existing ordinary
 * directories are accepted; child links and non-directory leaves are rejected. */
bool qa_fs_root_create_directory(qa_fs_root *root, const char *relative,
                                  qa_error *error);
/* Normal startup only: admits the nearest existing directory as a held root,
 * then creates the missing native path through normalized nofollow children. */
bool qa_fs_path_create_directory(const char *path, qa_error *error);

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
/* Reads at most capacity bytes from offset zero of the retained file. Checks
 * the admitted handle identity before/after; never reopens the native path. */
bool qa_fs_file_read_prefix(qa_fs_file *, const qa_fs_identity *, void *,
                            size_t capacity, size_t *received, qa_error *);

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

typedef enum qa_fs_stream_open_stage {
    QA_FS_STREAM_OPEN_PREPARE,
    QA_FS_STREAM_OPEN_NATIVE,
    QA_FS_STREAM_OPEN_VALIDATE,
    QA_FS_STREAM_OPEN_TRUNCATE,
    QA_FS_STREAM_OPEN_READY
} qa_fs_stream_open_stage;

/* Stable native object provenance, without mutable size or timestamps.
 * Platform 1 is POSIX device/inode; 2 is Windows volume/file/creation identity.
 * These values are references, never native descriptors or handles. */
typedef struct qa_fs_object_reference {
    uint32_t platform;
    uint64_t words[3];
} qa_fs_object_reference;

enum { QA_FS_OPENED_READ = 1, QA_FS_OPENED_WRITE = 2 };
typedef enum qa_fs_opened_creation {
    QA_FS_CREATE_NEW = 1, QA_FS_CREATE_ALWAYS = 2, QA_FS_OPEN_EXISTING = 3,
    QA_FS_OPEN_ALWAYS = 4, QA_FS_TRUNCATE_EXISTING = 5
} qa_fs_opened_creation;
typedef struct qa_fs_opened_reference {
    qa_fs_object_reference root, object;
    uint32_t mode, creation;
    const char *path;
} qa_fs_opened_reference;
/* Named actual POSIX stat values, independent of the host struct stat layout.
 * A Windows file owner does not manufacture Unix ownership or permissions. */
typedef struct qa_fs_posix_status {
    uint64_t device, inode, links, special_device;
    uint32_t mode, uid, gid;
    int64_t size, block_size, blocks;
    qa_fs_timestamp access, modification, change;
} qa_fs_posix_status;
typedef struct qa_fs_native_error {
    uint32_t platform, code; /* POSIX errno (1), Win32 (2), Windows CRT errno (3). */
    bool available;
} qa_fs_native_error;
/* Immediate same-thread receipt of the last opened-file operation. Boundary
 * validation has no native error; neither diagnostics nor cleanup invent one. */
bool qa_fs_opened_native_error_read(qa_fs_native_error *);
/* Holds the actual contained native object. Zero mode admits metadata only.
 * Creating/writable traversal never follows child links; no parent directory
 * is synthesized. The owner is allocated before opening. opened=true and a
 * nonempty out retain cleanup responsibility even after validation/truncation
 * fails; only close is then admitted. No failed native open publishes an owner. */
bool qa_fs_root_opened_file(qa_fs_root *, const char *, uint32_t,
    qa_fs_opened_creation, qa_fs_opened_file **, bool *opened, qa_error *);
bool qa_fs_opened_file_reference_read(const qa_fs_opened_file *, qa_fs_opened_reference *);
bool qa_fs_opened_file_current(const qa_fs_opened_file *, qa_error *);
void qa_fs_opened_file_retain(qa_fs_opened_file *);
bool qa_fs_opened_file_read(qa_fs_opened_file *, uint64_t, void *, size_t,
    size_t *completed, qa_error *);
bool qa_fs_opened_file_write(qa_fs_opened_file *, uint64_t, qa_bytes,
    size_t *completed, qa_error *);
bool qa_fs_opened_file_size(qa_fs_opened_file *, uint64_t *, qa_error *);
bool qa_fs_opened_file_posix_status_read(qa_fs_opened_file *, qa_fs_posix_status *, qa_error *);
bool qa_fs_opened_file_truncate(qa_fs_opened_file *, uint64_t, qa_error *);
bool qa_fs_opened_file_flush(qa_fs_opened_file *, qa_error *);
/* Linux close consumes its descriptor even on a reported error. Windows close
 * refusal retains the actual owner. *owner always identifies the real result;
 * a retry never reuses a consumed numeric descriptor or reopens the path. */
bool qa_fs_opened_file_close(qa_fs_opened_file **, qa_error *);

typedef struct qa_fs_stream_reference {
    qa_fs_object_reference root, object;
    qa_fs_stream_mode mode;
    const char *path;
} qa_fs_stream_reference;

typedef struct qa_fs_stream_resolver {
    void *context;
    /* Qualifies the complete source reference against the real destination
     * owner and returns an owned mapped root, including on partial failure.
     * Mutable user-file contents and replacement are permitted. */
    bool (*root)(void *, const qa_fs_stream_reference *, qa_fs_root **, qa_error *);
} qa_fs_stream_resolver;

/* Pure observations of admitted native objects. The stream retains its root;
 * the returned root/path are borrowed until that stream is closed. */
bool qa_fs_root_reference_read(const qa_fs_root *, qa_fs_object_reference *);
qa_fs_root *qa_fs_stream_root(const qa_fs_stream *);
bool qa_fs_stream_reference_read(const qa_fs_stream *, qa_fs_stream_reference *);
bool qa_fs_stream_reference_valid(const qa_fs_stream_reference *, qa_error *);

/* Fresh WRITE truncates; resumed WRITE requires the existing file. Append
 * modes create missing files. initial_size returns the opened file length. */
bool qa_fs_root_stream_open(qa_fs_root *root, const char *relative,
                            qa_fs_stream_mode mode, bool resume,
                            qa_fs_stream **out, uint64_t *initial_size,
                            qa_error *error);
/* stage identifies the actual failed operation. Only NATIVE denotes failure
 * of the final open syscall; preparation/validation/truncation remain distinct. */
bool qa_fs_root_stream_open_result(qa_fs_root *, const char *, qa_fs_stream_mode,
    bool resume, qa_fs_stream **, uint64_t *initial_size,
    qa_fs_stream_open_stage *, qa_error *);
/* Ordinary cold resume requires the same retained root object. Mapped resume
 * requires the explicit real resolver above and verifies its returned root
 * against destination before opening. Neither checks old file size,
 * timestamps or object identity: WRITE preserves existing external contents;
 * append may create a missing destination. No truncate or file-byte replay. */
bool qa_fs_stream_resume(qa_fs_root *, const qa_fs_stream_reference *,
    qa_fs_stream **, uint64_t *initial_size, qa_error *);
bool qa_fs_stream_resume_mapped(qa_fs_root *destination, const qa_fs_stream_reference *,
    const qa_fs_stream_resolver *, qa_fs_stream **, uint64_t *initial_size, qa_error *);
/* One completed native write (interrupted calls may retry), without a size
 * query or durability sync. Zero and partial progress are literal results. */
bool qa_fs_stream_write_some(qa_fs_stream *, qa_bytes, uint64_t position,
    size_t *written, qa_error *);
/* Positional mode writes at position. Append modes ignore it and publish the
 * resulting file size. Partial progress is returned through written. */
bool qa_fs_stream_write(qa_fs_stream *stream, qa_bytes bytes,
                        uint64_t position, size_t *written,
                        uint64_t *resulting_size, qa_error *error);
bool qa_fs_stream_size(qa_fs_stream *stream, uint64_t *out,
                       qa_error *error);
/* Flush the retained writable handle without changing file identity/position. */
bool qa_fs_stream_sync(qa_fs_stream *, qa_error *);
/* Consumes the stream even when the actual operating-system close fails. */
bool qa_fs_stream_close_checked(qa_fs_stream *stream, qa_error *error);
void qa_fs_stream_close(qa_fs_stream *stream);

typedef struct qa_fs_stage qa_fs_stage;
/* Retains the contained target parent and a private temporary file named by
 * nonce. Fresh creation is exclusive; resume requires that exact regular file
 * and never truncates. The caller persists target/nonce/content identity for
 * recovery and revalidates retained bytes before resuming a transfer. */
bool qa_fs_stage_open(qa_fs_root *, const char *target, uint64_t nonce,
                       bool resume, qa_fs_stage **, uint64_t *initial_size, qa_error *);
/* Read-only inspection of an existing retained private stage. Never creates,
 * truncates, syncs, publishes or removes the artifact, even on close(false).
 * Its retained handle qualifies the same contained regular file identity. */
bool qa_fs_stage_open_readonly(qa_fs_root *, const char *target, uint64_t nonce,
                                qa_fs_stage **, uint64_t *initial_size, qa_error *);
/* Checked admission requires an empty output and returns any actual partial
 * owner there on failure. The caller must retire it with checked close. */
bool qa_fs_stage_open_checked(qa_fs_root *, const char *target, uint64_t nonce,
                               bool resume, qa_fs_stage **, uint64_t *initial_size, qa_error *);
bool qa_fs_stage_open_readonly_checked(qa_fs_root *, const char *target, uint64_t nonce,
                                        qa_fs_stage **, uint64_t *initial_size, qa_error *);
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
/* Checked retirement retains the actual owner on refusal. POSIX consumed
 * descriptors are cleared even when close reports failure; retries never
 * reuse them. Once started, only another checked close with the same keep
 * decision is admitted. */
bool qa_fs_stage_close_checked(qa_fs_stage **, bool keep, qa_error *);

#endif
