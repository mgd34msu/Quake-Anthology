#ifndef QA_GAME_Q2_PREFERENCES_H
#define QA_GAME_Q2_PREFERENCES_H

#include "qa/game_q2_player.h"

/* The dictionary uses the Source's first value and insertion order. */
bool qa_q2_userinfo_field_of_view(const char *, double, qa_buffer *, qa_error *);
bool qa_q2_userinfo_field_of_view_read(const char *, double *, qa_error *);
bool qa_q2_player_field_of_view_read(qa_q2_game *, qa_actor_id,
    double *, bool *found, qa_error *);
bool qa_q2_player_field_of_view_set(qa_q2_game *, qa_actor_id, double,
    bool physical_source, bool preserve_camera, bool player_intermission,
    bool restoring, qa_error *);

#endif
