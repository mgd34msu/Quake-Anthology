#ifndef QA_GAME_Q1_SOURCE_FLAGS_H
#define QA_GAME_Q1_SOURCE_FLAGS_H

#include "qa/game_q1.h"

typedef struct qa_q1_source_flags_services {
    void *context;
    bool (*current)(void *, const qa_q1_game *, qa_error *);
    bool (*touch)(void *, qa_actor_id flag, qa_actor_id player, qa_error *);
    bool (*return_flag)(void *, qa_actor_id flag, qa_error *);
    bool (*drop_flag)(void *, qa_actor_id player, qa_error *);
    bool (*update)(void *, qa_error *);
    bool (*player_frame)(void *, qa_actor_id player, double *, qa_error *);
} qa_q1_source_flags_services;

typedef struct qa_q1_source_flag_view {
    qa_actor_id actor, owner;
    qa_vec3 base, angles;
    double count;
    uint32_t movement_flags;
    bool blue, placed, trigger;
} qa_q1_source_flag_view;

bool qa_q1_source_flags_configure(qa_q1_game *, const qa_q1_source_flags_services *, qa_error *);
bool qa_q1_source_flag_read(const qa_q1_game *, qa_actor_id, qa_q1_source_flag_view *, qa_error *);
bool qa_q1_source_flag_carried(const qa_q1_game *, qa_actor_id player,
    qa_actor_id *flag, bool *found, qa_error *);
/* These source operations publish the genuine flag's body and physics. The
 * composition owns announcements, scores, player words and status ordering. */
bool qa_q1_source_flag_return(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_source_flag_drop(qa_q1_game *, qa_actor_id flag, qa_actor_id player, qa_error *);
bool qa_q1_source_flag_carry(qa_q1_game *, qa_actor_id flag, qa_actor_id player, qa_error *);
/* Actual worldspawn raw fields written by the capture source body. */
bool qa_q1_source_capture_words_read(qa_q1_game *, double *seconds, double *team, qa_error *);
bool qa_q1_source_capture_words_write(qa_q1_game *, double seconds, double team, qa_error *);

#endif
