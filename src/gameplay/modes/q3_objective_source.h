#ifndef QA_MODES_Q3_OBJECTIVE_SOURCE_H
#define QA_MODES_Q3_OBJECTIVE_SOURCE_H

#include "qa/modes.h"
#include "qa/game_q3_source.h"

typedef struct mode_q3_source_item {
    qa_actor_id actor;
    qa_vec3 origin, trajectory_base;
    int32_t flag_team, spawn_team;
    bool dropped;
} mode_q3_source_item;

typedef struct mode_q3_source_client {
    qa_actor_id actor;
    int32_t team;
    char name[36];
    bool in_use;
} mode_q3_source_client;

/* Borrowed for one genuine TEAM call. Enumeration follows physical GAME
 * slots; the callbacks change the existing source owners only. */
typedef struct mode_q3_objective_services {
    void *context;
    int32_t time_ms, game_type;
    uint32_t max_clients;
    bool missionpack;
    bool (*live)(void *, qa_error *);
    bool (*team_read)(void *, qa_q3_source_team_state *, qa_error *);
    bool (*team_write)(void *, const qa_q3_source_team_state *, qa_error *);
    bool (*player_team_read)(void *, qa_actor_id, qa_q3_source_player_team_state *, qa_error *);
    bool (*player_team_write)(void *, qa_actor_id, const qa_q3_source_player_team_state *, qa_error *);
    bool (*team_score)(void *, int32_t source_team, int32_t amount, qa_error *);
    bool (*ranks)(void *, qa_error *);
    bool (*tokens)(void *, qa_actor_id, int32_t *, qa_error *);
    bool (*set_tokens)(void *, qa_actor_id, int32_t, qa_error *);
    bool (*entity_count)(void *, uint32_t *, qa_error *);
    bool (*item_at)(void *, uint32_t, mode_q3_source_item *, bool *, qa_error *);
    bool (*client_at)(void *, uint32_t, mode_q3_source_client *, qa_error *);
    bool (*powerup)(void *, qa_actor_id, int32_t tag, int32_t *, qa_error *);
    bool (*set_powerup)(void *, qa_actor_id, int32_t tag, int32_t, qa_error *);
    bool (*respawn)(void *, qa_actor_id, qa_error *);
    bool (*retire)(void *, qa_actor_id, qa_error *);
    bool (*configstring)(void *, uint32_t, const char *, qa_error *);
    bool (*print)(void *, qa_actor_id recipient_or_none, const char *, qa_error *);
    bool (*warn)(void *, const char *, qa_error *);
    bool (*sound)(void *, qa_vec3, int32_t source_sound, qa_error *);
    bool (*gesture)(void *, int32_t source_team, qa_error *);
    bool (*award)(void *, qa_actor_id, uint32_t source_flags, qa_error *);
    bool (*pers_award)(void *, qa_actor_id, qa_q3_source_award, int32_t, qa_error *);
    bool (*score_plum)(void *, qa_actor_id, qa_vec3, int32_t, qa_error *);
    bool (*rank_capture)(void *, qa_actor_id, qa_error *);
    bool (*rank_pickup)(void *, qa_actor_id, qa_error *);
} mode_q3_objective_services;

bool qa_modes_q3_source_object_adopt(qa_modes *, qa_mode_id, qa_actor_owner source_owner,
    const qa_mode_object_spec *, bool dropped, qa_error *);
bool qa_modes_q3_source_object_available(qa_modes *, qa_mode_id, qa_actor_id,
    bool available, qa_error *);
bool qa_modes_q3_source_team_initialize(qa_modes *, qa_mode_id,
    const mode_q3_objective_services *, qa_error *);
bool qa_modes_q3_source_item_pickup(qa_modes *, qa_mode_id, qa_actor_id item,
    qa_actor_id player, int32_t flag_team, const mode_q3_objective_services *,
    bool *accepted, qa_error *);
bool qa_modes_q3_source_item_dropped(qa_modes *, qa_mode_id, qa_actor_id item,
    int32_t flag_team, const mode_q3_objective_services *, qa_error *);
bool qa_modes_q3_source_flags_cleared(qa_modes *, qa_mode_id, qa_actor_id player,
    const mode_q3_objective_services *, qa_error *);
bool qa_modes_q3_source_item_expired(qa_modes *, qa_mode_id, qa_actor_id item,
    int32_t flag_team, bool no_drop, const mode_q3_objective_services *, qa_error *);
bool qa_modes_q3_source_return_flag(qa_modes *, qa_mode_id, int32_t source_team,
    const mode_q3_objective_services *, qa_error *);
bool qa_modes_q3_source_obelisk_touch(qa_modes *, qa_mode_id,
    const mode_q3_source_item *physical_trigger, qa_actor_id player,
    const mode_q3_objective_services *, qa_error *);
bool qa_modes_q3_source_obelisk_die(qa_modes *, qa_mode_id,
    const mode_q3_source_item *physical_trigger, qa_actor_id attacker,
    qa_q3_obelisk_die_stage, const mode_q3_objective_services *, qa_error *);
bool qa_modes_q3_source_obelisk_pain(qa_modes *, qa_mode_id,
    const mode_q3_source_item *physical_trigger, qa_actor_id attacker, int32_t amount,
    const mode_q3_objective_services *, qa_error *);
bool qa_modes_q3_source_object_at(qa_modes *, qa_mode_id, size_t,
    qa_actor_id *, qa_mode_object_spec *, qa_mode_object_view *, qa_actor_id *base,
    qa_error *);

#endif
