#ifndef QA_Q2_ORIGINAL_EDICTS_H
#define QA_Q2_ORIGINAL_EDICTS_H

#include "original_internal.h"

typedef struct q2_original_string_field {
    const char *name;
    uint16_t offset;
} q2_original_string_field;
extern const q2_original_string_field q2_original_edict_strings[11];

bool q2_original_string(qa_q2_game *, q2_original_record_io *, const char *,
    uint16_t offset, qa_string_id *);
bool q2_original_reference(qa_q2_game *, q2_original_record_io *, const char *,
    uint16_t offset, qa_actor_id *);
bool q2_original_source_reference(qa_q2_game *, q2_original_record_io *, const char *,
    uint16_t offset, qa_actor_reference *);
bool q2_original_body(qa_q2_game *, q2_original_record_io *, qa_body_state *);
bool q2_original_function(qa_q2_game *, q2_original_record_io *, const char *,
    uint16_t offset, const char *source_name);
bool q2_original_function_matches(qa_q2_game *, q2_original_record_io *, const char *,
    uint16_t offset, const char *source_name, bool *);
bool q2_original_edict_record(qa_q2_game *, q2_original_record_io *, q2_actor *,
    const qa_q2_save_level *, qa_error *);
bool q2_original_edict_visual(qa_q2_game *, q2_original_record_io *, q2_actor *,
    const qa_q2_save_level *, qa_error *);

#endif
