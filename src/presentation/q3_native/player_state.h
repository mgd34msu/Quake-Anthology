#ifndef QA_Q3_NATIVE_PLAYER_STATE_H
#define QA_Q3_NATIVE_PLAYER_STATE_H

#include "events.h"
#include "qa/application_native_q3_client.h"

typedef struct q3n_player_state q3n_player_state;
/* Prepared equipment admission for this actual source frame. Installation of
 * a reader alone does not replace the primary game's weapon HUD. */
typedef struct q3n_weapon_hud {
    bool selected;
    int32_t warning;
} q3n_weapon_hud;
typedef struct q3n_player_state_options {
    qa_application *application;
    const qa_application_native_q3_presentation *source;
    qa_q3_presentation_assets *assets;
    qa_native_q3_client_service *client;
    uint32_t seat;
    void *context;
    void (*print)(void *, const char *);
    /* Actual admitted equipment decision; warning is 0 none, 1 low, 2 empty.
     * A false selected result retains the donor's primary ammo calculation. */
    bool (*weapon_warning)(void *, const q3n_frame *, q3n_weapon_hud *, qa_error *);
} q3n_player_state_options;
typedef struct q3n_player_state_context {
    int32_t warmup, timelimit, fraglimit, scores1;
    bool intermission_started, show_miss;
} q3n_player_state_context;
typedef struct q3n_reward { int32_t sound, shader, count; } q3n_reward;
typedef struct q3n_player_feedback {
    int32_t duck_time, attacker_time, damage_kick_end_time, low_ammo_warning;
    float duck_change, damage_time, damage_x, damage_y, damage_roll, damage_pitch, damage_value;
    int32_t reward_time, reward_stack, timelimit_warnings, fraglimit_warnings;
    q3n_reward rewards[10];
    bool this_frame_teleport;
} q3n_player_feedback;
bool q3n_player_state_create(const q3n_player_state_options *, q3n_player_state **, qa_error *);
/* Pure installed-basis construction during aggregate PERSISTING import. */
bool q3n_player_state_create_restored(const q3n_player_state_options *, q3n_player_state **, qa_error *);
void q3n_player_state_destroy(q3n_player_state *);
bool q3n_player_state_idle(const q3n_player_state *);
const q3n_player_feedback *q3n_player_state_feedback(const q3n_player_state *);
bool q3n_player_state_transition(q3n_player_state *, const q3n_frame *,
    const q3n_player_state_context *, qa_error *);
bool q3n_player_state_changed_events(q3n_player_state *, const q3n_frame *, bool show_miss, qa_error *);
/* DrawReward owns queue advancement; actual sounds remain backend handles. */
bool q3n_player_state_reward(q3n_player_state *, const q3n_frame *, q3n_reward *, float *alpha, bool *visible, qa_error *);
void q3n_player_state_round(q3n_player_state *);
bool q3n_player_state_checkpoint(const q3n_player_state *, qa_buffer *, qa_error *);
bool q3n_player_state_restore(q3n_player_state *, qa_bytes, qa_error *);

#endif
