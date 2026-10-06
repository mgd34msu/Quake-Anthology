#ifndef QA_ARCHIVE_H
#define QA_ARCHIVE_H

#include "qa/common.h"
#include "qa/filesystem.h"

typedef enum qa_archive_kind {
    QA_ARCHIVE_AUTO,
    QA_ARCHIVE_PAK,
    QA_ARCHIVE_ZIP,
    QA_ARCHIVE_PK3,
    QA_ARCHIVE_PK4,
    QA_ARCHIVE_KPF
} qa_archive_kind;

typedef enum qa_archive_comparison {
    QA_ARCHIVE_EXACT,
    QA_ARCHIVE_ASCII_INSENSITIVE,
    /* Match the donor's case folding for its byte-string member names. */
    QA_ARCHIVE_CASE_INSENSITIVE
} qa_archive_comparison;

typedef struct qa_archive qa_archive;

typedef struct qa_archive_entry {
    size_t ordinal;
    const char *raw_path;
    const char *path;
    size_t size;
    size_t compressed_size;
    /* ZIP payload offset is resolved by the first successful local-header read. */
    size_t data_offset;
    uint32_t crc32;
    uint16_t compression_method;
    uint16_t flags;
    bool is_directory;
} qa_archive_entry;

typedef struct qa_archive_data {
    qa_bytes bytes;
    qa_buffer owned;
} qa_archive_data;

/* Memory remains borrowed and must be immutable until the archive closes.
 * File opens own an immutable snapshot, including after path removal.
 * On failure, *out is NULL. */
bool qa_archive_open_memory(qa_bytes bytes, qa_archive_kind kind,
                            qa_archive **out, qa_error *error);
bool qa_archive_open_file(const char *path, qa_archive_kind kind,
                          qa_archive **out, qa_error *error);
/* Directory admission retains the handle; ZIP local headers are checked lazily
 * at payload admission. The admitted descriptor remains the read authority
 * across native path replacement. Reads enforce declared bounds, EOF/native
 * errors and ZIP structure/CRC, without rechecking identity per member. */
bool qa_archive_open_retained(qa_fs_file *, const qa_fs_identity *, qa_archive_kind,
    qa_archive **, qa_error *);
bool qa_archive_source_current(const qa_archive *, qa_error *);
/* Moves the first file snapshot to its containing owner without copying it. */
bool qa_archive_take_snapshot(qa_archive *, qa_buffer *, qa_error *);
void qa_archive_close(qa_archive *archive);

qa_archive_kind qa_archive_get_kind(const qa_archive *archive);
/* Extension policy only. AUTO leaves format detection to the content bytes. */
qa_archive_kind qa_archive_kind_for_path(const char *path);
/* Q3 FS_LoadZipFile checksums use nonempty ZIP members' CRC32 words in
 * central-directory order, with the feed prefixed for the pure checksum.
 * Either output may be NULL. No member payload is read. */
bool qa_archive_q3_checksums(const qa_archive *, uint32_t feed,
    uint32_t *checksum, uint32_t *pure_checksum, qa_error *);
size_t qa_archive_count(const qa_archive *archive);
const qa_archive_entry *qa_archive_entry_at(const qa_archive *archive,
                                           size_t ordinal);

/* Normalize a member byte-string, rejecting absolute paths and traversal out
 * of its root. The returned string belongs to the caller and uses free(). */
char *qa_archive_normalize_path(const char *path, qa_error *error);

/* Compare already normalized byte-strings; no normalization or allocation. */
bool qa_archive_paths_equal(const char *left, const char *right,
                            qa_archive_comparison comparison);

/* Search preserves directory order and duplicates. Start at zero, then at the
 * previous match's ordinal + 1. A successful search may return *out == NULL. */
bool qa_archive_find(const qa_archive *archive, const char *path,
                      qa_archive_comparison comparison, size_t start_ordinal,
                      const qa_archive_entry **out, qa_error *error);
/* Indexed lookup without allocating or normalizing a path. The caller supplies
 * a path already normalized by its mount/resource boundary. */
bool qa_archive_find_normalized(const qa_archive *archive, const char *path,
                                 qa_archive_comparison comparison, size_t start_ordinal,
                                 const qa_archive_entry **out, qa_error *error);

/* Stored data borrows the archive and expires when it closes. Deflated data
 * owns its allocation and survives close. Release either with data_free.
 * The output must be fresh or previously released. Failure leaves it empty. */
bool qa_archive_read(const qa_archive *archive, size_t ordinal,
                      qa_archive_data *out, qa_error *error);
void qa_archive_data_free(qa_archive_data *data);

#endif
