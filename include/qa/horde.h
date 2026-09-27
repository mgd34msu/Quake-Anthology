#ifndef QA_HORDE_H
#define QA_HORDE_H

#include "qa/modes.h"

typedef enum qa_horde_point_kind {
    QA_HORDE_NORMAL,
    QA_HORDE_RANGED,
    QA_HORDE_FLYING,
    QA_HORDE_BOSS,
    QA_HORDE_AMMO,
    QA_HORDE_ITEM,
    QA_HORDE_KEY
} qa_horde_point_kind;
typedef struct qa_horde_point {
    qa_actor_id actor;
    qa_horde_point_kind kind;
    qa_vec3 origin, angles;
    qa_string_id target;
    uint32_t flags;
    uint64_t next_ns;
    bool occupied;
} qa_horde_point;
typedef struct qa_horde_options {
    qa_actor_id manager;
    qa_string_id target;
    uint32_t starting_campaign_flags;
    int32_t skill, world_type;
    bool cooperative;
} qa_horde_options;
typedef struct qa_horde_view {
    int32_t wave, fodder, elites, bosses, countdown, silver_keys, gold_keys;
    uint64_t next_ns;
    float powerup_chance;
    bool army, spawning, key_spawned;
} qa_horde_view;
bool qa_modes_horde_configure(qa_modes *, qa_mode_id, const qa_horde_options *,
                              const qa_horde_point *, size_t, qa_error *);
bool qa_modes_horde_read(qa_modes *, qa_mode_id, qa_horde_view *, qa_error *);
bool qa_modes_horde_toggle_point(qa_modes *, qa_mode_id, qa_actor_id point, qa_error *);
bool qa_modes_horde_check(qa_modes *, qa_mode_id, qa_error *);
bool qa_modes_horde_keys(qa_modes *, qa_mode_id, bool gold, int change, qa_error *);
bool qa_modes_horde_request_respawn(qa_modes *, qa_mode_id, bool *handled, qa_error *);
bool qa_modes_horde_finish(qa_modes *, qa_mode_id, qa_error *);
bool qa_modes_horde_count_monster(qa_modes *, qa_mode_id, qa_actor_id);
/* The shared death dispatcher brackets the selected native monster death with
 * these calls. Player deaths use qa_modes_player_death_component instead. */
bool qa_modes_horde_before_death(qa_modes *, qa_mode_id, const qa_damage_outcome *, qa_error *);
bool qa_modes_horde_after_death(qa_modes *, qa_mode_id, qa_actor_id, qa_error *);
/* Route managed loot here before ordinary native touch. Armor and powerups
 * enter the shared pickup pipeline through hooks.grant_loot exactly once. */
bool qa_modes_horde_loot_touch(qa_modes *, qa_actor_id item, qa_actor_id player, bool *handled,
                               bool *accepted, qa_error *);

#endif
