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
 * actor/string/text operations require its actual table. Read-side interning changes only an isolated
 * candidate's table. Native owners define/version every field and validate
 * semantics. Exact floating bits include valid source infinity sentinels. */
bool qa_source_save_writer(qa_source_save_io *, qa_session *, qa_error *);
bool qa_source_save_reader(qa_source_save_io *, qa_session *, qa_bytes, qa_error *);
bool qa_source_save_finish(qa_source_save_io *, qa_buffer *owned_output);
void qa_source_save_dispose(qa_source_save_io *);
bool qa_source_save_bytes(qa_source_save_io *, void *, size_t);
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
/* Explicit presence + original generation/slot; retired provenance survives.
 * Live-use authority must be validated by the source field's actual owner. */
bool qa_source_save_actor(qa_source_save_io *, qa_actor_id *);
#endif
