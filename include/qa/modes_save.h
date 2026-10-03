#ifndef QA_MODES_SAVE_H
#define QA_MODES_SAVE_H

#include "qa/horde.h"
#include "qa/modes_q3_session.h"
#include "qa/modes_q1_source.h"

typedef struct qa_mode_member_state {
    qa_actor_id actor;
    qa_mode_player_state player;
    qa_actor_owner external_owner;
    qa_mode_statistics stats;
    qa_actor_id relic, flag;
    qa_team_id last_team;
    uint64_t tech_sound_ns, regen_ns, notice_ns, respawn_ns, team_switch_ns;
    uint64_t rune_sound_ns[4];
    uint32_t rogue_rune;
    int32_t regen_frame, extra_flags, location, spawn_state, suicide_count, introduction_frames;
    uint32_t ghost_code;
    uint32_t vote_calls[4];
    int8_t ballots[4];
    bool joined, admin, observer_jump;
    qa_mode_q1_source_state q1;
} qa_mode_member_state;
typedef struct qa_mode_item_binding {
    qa_item_id source, inventory;
} qa_mode_item_binding;
typedef struct qa_mode_ghost_state {
    qa_actor_id actor;
    qa_string_id name;
    qa_team_id team;
    qa_mode_statistics stats;
    uint32_t code;
    int32_t score;
} qa_mode_ghost_state;
typedef struct qa_horde_monster_state {
    qa_actor_id actor;
    bool zombie, counted, death_pending;
    uint64_t kill_ns;
} qa_horde_monster_state;
typedef enum qa_horde_loot_kind {
    HORDE_AMMO,
    HORDE_HEALTH,
    HORDE_ARMOR,
    HORDE_SILVER,
    HORDE_GOLD,
    HORDE_POWER
} qa_horde_loot_kind;
typedef struct qa_horde_loot_state {
    qa_actor_id actor;
    qa_horde_loot_kind kind;
    size_t point;
    qa_item_id item;
    float amount, capacity, alpha;
    uint64_t fade_ns;
} qa_horde_loot_state;
typedef struct qa_horde_checkpoint {
    qa_horde_options options;
    qa_horde_view value;
    qa_horde_point *points;
    size_t point_count;
    qa_horde_monster_state *monsters;
    size_t monster_count;
    qa_horde_loot_state *loot;
    size_t loot_count;
    bool prepared, checking, finishing;
} qa_horde_checkpoint;
typedef struct qa_mode_checkpoint {
    qa_mode_id id;
    qa_mode_view value;
    qa_mode_member_state *members;
    size_t member_count;
    qa_mode_ghost_state *ghosts;
    size_t ghost_count;
    qa_mode_spawnpoint *spawns;
    size_t spawn_count;
    qa_mode_item_binding *items;
    size_t item_count;
    qa_actor_id bases[3], ball, tag, tag_owner, last_ball_touch;
    qa_actor_id last_spawns[4];
    qa_actor_id rogue_spawn_spot;
    qa_mode_vote votes[4];
    uint64_t vote_started[4], ready_since_ns, next_second_ns;
    uint64_t flag_sound_ns[3], attack_sound_ns[3];
    uint64_t relic_spawn_ns, team_location_ns;
    size_t rune_cursor;
    int32_t next_location;
    int32_t tag_count, remaining_seconds;
    bool countdown_announced, restart_sent, relics_started, rune_forward;
    qa_mode_q3_settings q3_settings;
    int32_t q3_started_ms, q3_warmup_ms;
    uint64_t q3_warmup_seen;
    bool q3_settings_present;
    qa_horde_checkpoint *horde;
} qa_mode_checkpoint;
typedef struct qa_mode_object_checkpoint {
    qa_actor_id actor;
    qa_mode_id mode;
    qa_mode_object_spec spec;
    qa_mode_object_view value;
    qa_vec3 home;
    qa_actor_id base, dropped_actor;
    qa_actor_collision collision;
    qa_physics_properties physics;
    uint64_t next_ns, owner_until_ns, animation_ns, expire_ns, born_ns;
    int32_t tag_stage;
    bool targets_used, has_physics, dropped, global_animation, q3_source_owned;
} qa_mode_object_checkpoint;
typedef struct qa_mode_player_checkpoint {
    qa_match_player value;
} qa_mode_player_checkpoint;
typedef struct qa_mode_objective_checkpoint {
    qa_mode_id mode;
    qa_actor_owner owner;
    qa_string_id id;
    bool campaign_gate, bot_goal;
} qa_mode_objective_checkpoint;
typedef struct qa_modes_checkpoint {
    uint64_t random, attack_sequence;
    uint64_t *mode_generations;
    size_t generation_count;
    qa_mode_player_checkpoint *players;
    size_t player_count;
    qa_mode_checkpoint *modes;
    size_t mode_count;
    qa_mode_object_checkpoint *objects;
    size_t object_count;
    qa_mode_objective_checkpoint *external_objectives;
    size_t external_objective_count;
} qa_modes_checkpoint;
/* Owned typed memory only. The save codec encodes individual fields, remaps
 * every actor/resource ID, and restores shared stores exactly once. External
 * objective owners restore and bind themselves before this restore. Score/team
 * owners resolve per-mode bindings through restore_player_binding during restore.
 * Restore targets a prepared modes service with no active native modes; a
 * failure discards that candidate along with the prepared world. */
bool qa_modes_checkpoint_capture(qa_modes *, qa_modes_checkpoint *, qa_error *);
bool qa_modes_checkpoint_restore(qa_modes *, const qa_modes_checkpoint *, qa_error *);
void qa_modes_checkpoint_free(qa_modes_checkpoint *);
bool qa_modes_capture(qa_modes *, qa_buffer *, qa_error *);
/* Restore private continuation after shared actors and native source owners,
 * before shared stores. Source score/team bindings resolve through the hook.
 * External objective owners and catalogs reconnect after shared restoration. */
bool qa_modes_restore_bytes(qa_modes *, qa_bytes, qa_error *);
/* After exact shared stores and external objective owners restore. Catalog
 * resolvers adopt private saved leases before this validates reconnection. */
bool qa_modes_reconnect(qa_modes *, qa_error *);
bool qa_modes_inventory_group(qa_modes *, qa_actor_id, uint64_t saved_serial,
    const qa_inventory_source_group *, qa_inventory_items *, qa_error *);

#endif
