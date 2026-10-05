#ifndef QA_Q2_ORIGINAL_SAVE_INTERNAL_H
#define QA_Q2_ORIGINAL_SAVE_INTERNAL_H

#include "../entities/internal.h"
#include "../monsters/internal.h"
#include "qa/binary.h"
#include "qa/json.h"
#include "qa/json_writer.h"

typedef enum q2_original_record_kind {
    Q2_ORIGINAL_PERSISTENT,
    Q2_ORIGINAL_VIEW,
    Q2_ORIGINAL_CLIENT,
    Q2_ORIGINAL_WEAPON,
    Q2_ORIGINAL_POWERS,
    Q2_ORIGINAL_ENTITY,
    Q2_ORIGINAL_MOVER
} q2_original_record_kind;

typedef enum q2_original_field_kind {
    Q2_ORIGINAL_I32, Q2_ORIGINAL_U32, Q2_ORIGINAL_F32, Q2_ORIGINAL_BOOL,
    Q2_ORIGINAL_VECTOR, Q2_ORIGINAL_TIME, Q2_ORIGINAL_FRAME_TIME,
    Q2_ORIGINAL_FRAME_INDEX, Q2_ORIGINAL_SECONDS_TIME, Q2_ORIGINAL_WEAPON_PHASE,
    Q2_ORIGINAL_TEXT
} q2_original_field_kind;

typedef struct q2_original_field {
    const char *name;
    size_t offset, count;
    q2_original_field_kind kind;
    uint16_t classic_offsets[3];
} q2_original_field;

typedef struct q2_original_layout {
    size_t size;
    const q2_original_field *fields;
    size_t count;
} q2_original_layout;

/* The caller stages the real typed state on reads. Classic offsets describe
 * the original MSVC x86 ABI; they are independent of our native C layout. */
typedef struct q2_original_record_io {
    qa_q2_edition edition;
    qa_q2_product product;
    qa_json_writer *writer;
    const qa_json_document *document;
    qa_json_id object;
    qa_bytes input;
    qa_buffer output;
    qa_error *error;
    bool reading;
} q2_original_record_io;

bool q2_original_record(q2_original_record_io *, q2_original_record_kind, void *);
const q2_original_layout *q2_original_layout_for(q2_original_record_kind);
size_t q2_original_client_size(qa_q2_product);
size_t q2_original_entity_size(qa_q2_product);
size_t q2_original_level_size(qa_q2_product);

#endif
