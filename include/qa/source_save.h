#ifndef QA_SOURCE_SAVE_H
#define QA_SOURCE_SAVE_H
#include "qa/session.h"
#include "qa/math.h"

typedef enum qa_source_save_direction { QA_SOURCE_SAVE_WRITE, QA_SOURCE_SAVE_READ, QA_SOURCE_SAVE_FINISHED } qa_source_save_direction;
typedef struct qa_source_save_io {
    qa_session *session;
    qa_source_save_direction direction;
    qa_buffer output;
    qa_bytes input;
    size_t offset, capacity;
    qa_error *error;
    bool failed;
} qa_source_save_io;
/* Owned output, borrowed input. Session is optional for primitive-only owners;
 * actor/string/borrowed-text operations require its actual table. Read-side interning changes only an isolated
 * candidate's table. Native owners define every field and validate
 * semantics. Exact floating bits include valid source infinity sentinels. */
bool qa_source_save_writer(qa_source_save_io *, qa_session *, qa_error *);
/* WRITE reserves total capacity without advancing or publishing bytes. */
bool qa_source_save_writer_reserve(qa_source_save_io *, size_t total_capacity);
bool qa_source_save_reader(qa_source_save_io *, qa_session *, qa_bytes, qa_error *);
bool qa_source_save_finish(qa_source_save_io *, qa_buffer *owned_output);
void qa_source_save_dispose(qa_source_save_io *);
bool qa_source_save_bytes(qa_source_save_io *, void *, size_t);
/* READ advances over a borrowed input span without allocating or copying. */
bool qa_source_save_span(qa_source_save_io *, size_t, qa_bytes *);
/* Changed memory spans relative to pristine bytes, followed by an implicit
 * zero tail. Unchanged bytes and changed-to-zero payloads are omitted. READ
 * validates the whole delta before replacing memory; NULL memory validates
 * without applying it. The pristine span may be shorter than the extent. */
bool qa_source_save_memory_delta(qa_source_save_io *, uint8_t *memory,
    size_t extent, qa_bytes pristine);
/* A positioned baseline read fills the requested extent, including zero tail.
 * Its borrowed owner remains current throughout this synchronous operation. */
typedef struct qa_source_save_memory_source {
    qa_bytes bytes;
    bool (*read)(void *, size_t offset, void *, size_t bytes, qa_error *);
    void *context;
} qa_source_save_memory_source;
bool qa_source_save_memory_delta_source(qa_source_save_io *, uint8_t *memory,
    size_t extent, const qa_source_save_memory_source *);
bool qa_source_save_bool(qa_source_save_io *, bool *);
bool qa_source_save_u8(qa_source_save_io *, uint8_t *);
bool qa_source_save_u16(qa_source_save_io *, uint16_t *);
bool qa_source_save_u32(qa_source_save_io *, uint32_t *);
bool qa_source_save_u64(qa_source_save_io *, uint64_t *);
bool qa_source_save_i32(qa_source_save_io *, int32_t *);
bool qa_source_save_i64(qa_source_save_io *, int64_t *);
bool qa_source_save_f32(qa_source_save_io *, float *);
bool qa_source_save_f64(qa_source_save_io *, double *);
bool qa_source_save_vec3(qa_source_save_io *, qa_vec3 *);
bool qa_source_save_count(qa_source_save_io *, size_t *, size_t maximum);
/* Counted values distinguish absent and empty strings. Text decode borrows
 * the restored session table and rejects embedded NUL; IDs allow counted bytes. */
bool qa_source_save_string(qa_source_save_io *, qa_string_id *);
bool qa_source_save_text(qa_source_save_io *, const char **);
/* Same counted text bytes, checked against the actual owner on READ without
 * interning or retaining a decoded string. Null and empty remain distinct. */
bool qa_source_save_text_assert(qa_source_save_io *, const char *expected);
/* Same counted text representation; READ replaces a separately malloc-owned
 * value after successful decoding without changing the session string table. */
bool qa_source_save_owned_text(qa_source_save_io *, char **);
/* Explicit presence + original generation/slot; retired provenance survives.
 * Live-use authority must be validated by the source field's actual owner. */
bool qa_source_save_actor(qa_source_save_io *, qa_actor_id *);
#endif
