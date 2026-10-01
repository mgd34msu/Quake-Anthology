#ifndef QA_Q1_SAVE_H
#define QA_Q1_SAVE_H
#include "qa/save.h"

/* Byte strings and source-order pairs, including duplicate/unknown keys.
 * Empty entity records are free physical edicts. No UTF-8 conversion occurs. */
typedef struct qa_q1_save_pair { char *key, *value; } qa_q1_save_pair;
typedef struct qa_q1_save_record { qa_q1_save_pair *pairs; size_t count; } qa_q1_save_record;
typedef struct qa_q1_save_data {
    uint32_t version;
    char *game_directories, *comment, *map;
    double spawn_parameters[16], time;
    int32_t skill;
    char *lightstyles[64];
    qa_q1_save_record globals, *entities;
    size_t entity_count;
    qa_buffer extension;
} qa_q1_save_data;

bool qa_q1_save_decode(qa_bytes, qa_q1_save_data **, qa_error *);
bool qa_q1_save_encode(const qa_q1_save_data *, qa_buffer *, qa_error *);
void qa_q1_save_destroy(qa_q1_save_data *);
void qa_q1_save_record_destroy(qa_q1_save_record *);
/* Semantic admission used before selecting a product or preparing a world. */
bool qa_q1_save_singleplayer(const qa_q1_save_data *, qa_error *);
/* Select the shared codec by its QA signature before attempting source text.
 * A malformed signed shared image never falls back to the source decoder.
 * Both outputs must be empty; exactly one becomes non-NULL on success. */
bool qa_saved_game_decode(qa_bytes, qa_save_image **, qa_q1_save_data **, qa_error *);
bool qa_saved_game_read(qa_fs_root *, const char *, qa_save_image **,
                         qa_q1_save_data **, qa_error *);
bool qa_q1_save_write(qa_fs_root *, const char *, const qa_q1_save_data *, uint64_t,
                       qa_error *);
#endif
