#ifndef QA_MODES_INTERNAL_H
#define QA_MODES_INTERNAL_H

#include "qa/modes.h"
#include "qa/modes_save.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MODE_SECOND UINT64_C(1000000000)
#define MODE_MILLISECOND UINT64_C(1000000)

typedef struct mode_player {
    qa_modes *modes;
    qa_match_player value;
    qa_inventory_lease items;
    bool active;
} mode_player;
typedef qa_mode_member_state mode_member;
typedef qa_mode_ghost_state mode_ghost;
typedef struct mode_match_owner {
    qa_match_binding binding;
    uint64_t serial;
} mode_match_owner;
typedef struct mode_rank_entry {
    qa_actor_id actor;
    int32_t score;
    uint64_t spectator_since;
    uint32_t order;
    uint8_t group;
} mode_rank_entry;
typedef struct mode_instance {
    qa_mode_id id;
    qa_mode_view value;
    mode_member *members;
    mode_match_owner *bindings;
    mode_ghost *ghosts;
    qa_actor_id *sorted;
    mode_rank_entry *ranks;
    qa_mode_spawnpoint *spawns;
    size_t spawn_count;
    qa_mode_item_binding *items;
    size_t item_count, item_capacity;
    qa_actor_id bases[3], ball, tag, tag_owner, last_ball_touch;
    bool base_admitting[3];
    qa_actor_id last_spawns[3];
    qa_mode_vote votes[4];
    uint64_t vote_started[4], ready_since_ns, next_second_ns;
    uint64_t flag_sound_ns[3], attack_sound_ns[3];
    uint64_t relic_spawn_ns, team_location_ns;
    size_t rune_cursor;
    int32_t next_location;
    bool relics_started, rune_forward;
    uint64_t serial;
    int32_t tag_count, remaining_seconds;
    bool active, countdown_announced, restart_sent;
    void *horde;
} mode_instance;
typedef struct mode_object {
    qa_modes *modes;
    qa_actor_id actor;
    qa_mode_id mode;
    qa_mode_object_spec spec;
    qa_mode_object_view value;
    qa_vec3 home;
    qa_actor_id base, dropped_actor;
    qa_actor_collision collision;
    qa_physics_properties physics;
    qa_objective_lease objective;
    uint64_t next_ns, owner_until_ns, animation_ns, expire_ns, born_ns;
    int32_t tag_stage;
    bool active, admitting, targets_used, has_physics, dropped, global_animation;
} mode_object;
typedef struct mode_objective {
    qa_objective_binding binding;
    uint64_t serial;
    bool active, reserved;
} mode_objective;
struct qa_modes {
    qa_modes_options options;
    uint32_t actor_capacity, mode_capacity, objective_capacity;
    mode_player *players;
    mode_object *objects;
    mode_instance *instances;
    mode_objective *objectives;
    uint64_t next_serial, random;
    uint64_t attack_sequence;
    unsigned callback_depth;
    qa_mode_objective_checkpoint *restored_objectives;
    size_t restored_objective_count;
    bool source_restored;
    qa_builtin_actor_snapshot players_order, observations;
};

bool mode_fail(qa_error *, const char *);
bool mode_callback_end(qa_modes *, bool);
#define MODE_CALLBACK(m, call) ((m)->callback_depth++, mode_callback_end((m), (call)))
bool mode_live(const qa_modes *, qa_actor_id);
mode_player *mode_player_get(qa_modes *, qa_actor_id);
mode_instance *mode_get(qa_modes *, qa_mode_id);
mode_member *mode_member_get(qa_modes *, mode_instance *, qa_actor_id);
mode_object *mode_object_get(qa_modes *, qa_actor_id);
int mode_team_index(const mode_instance *, qa_team_id);
int32_t mode_add_i32(int32_t, int32_t);
uint32_t mode_random(qa_modes *);
float mode_random_float(qa_modes *);
bool mode_event(qa_modes *, mode_instance *, qa_mode_event_kind, qa_actor_id, qa_actor_id,
                qa_actor_id, qa_team_id, int32_t, int32_t, qa_error *);
bool mode_intent(qa_modes *, mode_instance *, qa_match_intent_kind, qa_actor_id, qa_team_id,
                 qa_string_id, qa_error *);
bool mode_alive(qa_modes *, qa_actor_id);
bool mode_sound(qa_modes *, mode_instance *, qa_actor_id, const char *, float, qa_error *);
bool mode_count(qa_modes *, qa_actor_id, qa_item_id, double *, qa_error *);
bool mode_set_count(qa_modes *, qa_actor_id, qa_item_id, double, qa_error *);
bool mode_object_count(qa_modes *, mode_instance *, mode_object *, qa_actor_id, double, qa_error *);
bool mode_inventory_item(qa_modes *, mode_instance *, qa_item_id, qa_item_id *, qa_error *);
bool mode_reserve_objective(qa_modes *, const qa_objective_binding *, qa_objective_lease *, qa_error *);
bool mode_commit_objective(qa_modes *, qa_objective_lease);
bool mode_set_phase(qa_modes *, mode_instance *, qa_mode_phase, uint64_t, qa_error *);
bool mode_match_frame(qa_modes *, mode_instance *, uint64_t, qa_error *);
bool mode_vote_frame(qa_modes *, mode_instance *, qa_error *);
bool mode_objects_frame(qa_modes *, mode_instance *, uint64_t, qa_error *);
bool mode_relic_frame(qa_modes *, mode_instance *, qa_actor_id, mode_member *, qa_error *);
bool mode_flag_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *, qa_error *);
bool mode_relic_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *, qa_error *);
bool mode_tag_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *, qa_error *);
bool mode_ball_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *, qa_error *);
bool mode_ball_frame(qa_modes *, mode_instance *, mode_object *, qa_error *);
bool mode_ball_reset(qa_modes *, mode_instance *, mode_object *, qa_error *);
bool mode_flag_reset(qa_modes *, mode_instance *, mode_object *, bool, qa_error *);
bool mode_object_hide(qa_modes *, mode_object *, bool, qa_error *);
bool mode_object_drop(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool, qa_error *);
bool mode_object_relocate(qa_modes *, mode_instance *, mode_object *, bool, qa_error *);
bool mode_object_sync(qa_modes *, mode_object *, qa_error *);
bool mode_object_notify(qa_modes *, mode_instance *, mode_object *, qa_error *);
bool mode_flag_bonus(qa_modes *, mode_instance *, qa_actor_id, qa_actor_id, qa_error *);
bool mode_tag_death(qa_modes *, mode_instance *, const qa_damage_outcome *, bool,
                    const qa_mode_frag *, qa_error *);
bool mode_rogue_tag_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *,
                          qa_error *);
bool mode_rogue_tag_frame(qa_modes *, mode_instance *, mode_object *, qa_error *);
bool mode_rogue_tag_drop(qa_modes *, mode_instance *, mode_object *, qa_error *);
bool mode_obelisk_touch(qa_modes *, mode_instance *, mode_object *, qa_actor_id, bool *,
                        qa_error *);
bool mode_horde_frame(qa_modes *, mode_instance *, uint64_t, qa_error *);
void mode_horde_free(mode_instance *);
bool mode_horde_retire(qa_modes *, mode_instance *, qa_error *);
bool mode_horde_reconcile_keys(qa_modes *, mode_instance *, qa_error *);
bool mode_horde_death(qa_modes *, mode_instance *, const qa_damage_outcome *, qa_error *);
bool mode_horde_respawn(qa_modes *, mode_instance *, qa_actor_id, qa_error *);
bool mode_near(qa_modes *, qa_actor_id, qa_actor_id, float);
bool mode_visible(qa_modes *, qa_actor_id, qa_actor_id, bool);
bool mode_rules_valid(const qa_mode_rules *);
bool mode_damage(qa_modes *, qa_game_family, qa_damage_request *, qa_error *);
bool mode_horde_capture(qa_modes *, mode_instance *, qa_horde_checkpoint **, qa_error *);
bool mode_horde_restore(qa_modes *, mode_instance *, const qa_horde_checkpoint *, qa_error *);
void mode_horde_checkpoint_free(qa_horde_checkpoint *);
bool mode_object_bind_objective(qa_modes *, mode_object *, qa_error *);
bool mode_join(qa_modes *, mode_instance *, qa_actor_id, qa_team_id, bool, bool, qa_error *);
bool mode_relic_place(qa_modes *, mode_instance *, mode_object *, bool initial, qa_error *);
bool mode_relic_spawn_all(qa_modes *, mode_instance *, qa_error *);
bool mode_team_info_frame(qa_modes *, mode_instance *, qa_error *);
bool mode_update_ghosts(qa_modes *, mode_instance *, qa_error *);
void mode_stat_add(mode_instance *, int32_t *, int32_t);
bool mode_checkpoint_restore_source(qa_modes *, const qa_modes_checkpoint *, qa_error *);
bool mode_items_reconnect(qa_modes *, qa_actor_id, qa_error *);

#endif
