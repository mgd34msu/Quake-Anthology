#ifndef QA_GAME_Q1_MAPS_H
#define QA_GAME_Q1_MAPS_H

#include "qa/campaign_q1_sources.h"
#include "qa/horde.h"
#include "qa/navigation.h"

/* Authored values are borrowed only during spawn. The map provider interns
 * strings and retains typed fields beside the native actor continuation. */
typedef struct qa_q1_ctf_map_state {
    bool start_map, pregame_over, observer;
} qa_q1_ctf_map_state;

typedef struct qa_q1_map_fields {
    const char *model, *map, *noise, *noise1, *noise2, *noise3;
    const char *endtext, *intermissiontext, *netname, *event;
    const char *spawn_function, *spawn_classname;
    const char *group, *path, *category, *fog_info_entity;
    qa_vec3 mangle, movedir, view_offset, rotate;
    qa_vec3 dest, dest2, pos2, angular_velocity;
    qa_vec3 particle_size;
    qa_vec3 fog_color;
    float fog_density;
    bool has_movedir, has_view_offset, has_dest2;
    float height, lip, width, length, pause_time;
    float volume, duration, distance, next_think_seconds, local_time_seconds;
    float spawn_multi, spawn_silent, gravity, current_ammo, pain_finished, weapon, frags;
    int32_t sounds, style, world_type, color_map, impulse;
    int32_t initial_state, frame, skin;
    float counter_value, goal_state;
    int32_t particle_color;
} qa_q1_map_fields;

typedef struct qa_q1_static_model {
    qa_string_id model;
    qa_vec3 origin, angles;
    int32_t frame, skin, color_map;
} qa_q1_static_model;
typedef struct qa_q1_fog_state {
    qa_actor_id active;
    float density;
    qa_vec3 color;
} qa_q1_fog_state;
bool qa_q1_game_map_fog_read(const qa_q1_game *, qa_actor_id, qa_q1_fog_state *);
typedef struct qa_q1_map_finale_view {
    uint32_t stage;
    qa_string_id map, text;
    qa_vec3 origin, angles;
    double exit_after;
} qa_q1_map_finale_view;
typedef enum qa_q1_map_ending { QA_Q1_MAP_END_DOPA, QA_Q1_MAP_END_MG3 } qa_q1_map_ending;
typedef struct qa_q1_path_state {
    qa_actor_id move_target, enemy, old_enemy, previous_corner, owner;
    qa_string_id path;
    double pause_until, follow_until;
    bool monster;
} qa_q1_path_state;
typedef enum qa_q1_path_change_kind {
    QA_Q1_PATH_OWNER,
    QA_Q1_PATH_VISIT,
    QA_Q1_PATH_DESTINATION,
    QA_Q1_PATH_PAUSE_END,
    QA_Q1_PATH_CANCEL_PAUSE,
    QA_Q1_PATH_STAND,
    QA_Q1_PATH_FOLLOW_BEGIN,
    QA_Q1_PATH_FOLLOW_UNTIL,
    QA_Q1_PATH_FOUND
} qa_q1_path_change_kind;
typedef struct qa_q1_path_change {
    qa_q1_path_change_kind kind;
    qa_actor_id reference;
    qa_string_id target;
    double pause_until, follow_until;
} qa_q1_path_change;
typedef enum qa_q1_map_mover_kind {
    QA_Q1_MOVER_DOOR, QA_Q1_MOVER_ELEVATOR, QA_Q1_MOVER_TRAIN,
    QA_Q1_MOVER_BOBBING, QA_Q1_MOVER_STATIC
} qa_q1_map_mover_kind;
typedef struct qa_q1_map_mover_view {
    qa_actor_id actor, activation;
    qa_nav_entity_state navigation;
    qa_q1_map_mover_kind kind;
    uint32_t inline_model;
    bool has_inline_model, shootable, useable;
} qa_q1_map_mover_view;
typedef struct qa_q1_map_options {
    qa_targets *targets;
    qa_q1_level *level;
    qa_q1_campaign_source *campaign_source;
    uint32_t *server_flags;
    qa_string_id current_map;
    bool registered;
    void *context;
    bool (*static_model)(void *, const qa_q1_static_model *, qa_error *);
    bool (*ambient)(void *, qa_vec3, qa_string_id sound, float volume, float attenuation,
                    qa_error *);
    bool (*lightstyle)(void *, int32_t style, qa_string_id pattern, qa_error *);
    bool (*set_skill)(void *, int32_t, qa_error *);
    bool (*player_exited)(void *, qa_actor_id, qa_error *);
    bool (*secret_found)(void *, qa_actor_id source, qa_actor_id player, uint32_t total,
                         uint32_t found, qa_error *);
    /* An attached foreign character can follow authored Q1 monster paths. */
    bool (*path_touch)(void *, qa_actor_id corner, qa_actor_id follower, bool *handled, qa_error *);
    /* Path controls read and mutate the actual foreign continuation. Read is
     * nonmutating. A pause change invokes an installed path-end callback;
     * cancel restores normal monster use. Hipnotic follow preserves the old
     * enemy, pending walk frame and cooldown; STAND always enters the stand
     * callback and FOUND invokes the actual owner's found-target callback.
     * Supply both callbacks together. */
    bool (*path_read)(void *, qa_actor_id, qa_q1_path_state *);
    bool (*path_change)(void *, qa_actor_id, const qa_q1_path_change *, qa_error *);
    bool (*target_damage)(void *, qa_actor_id, float damage, qa_error *);
    bool (*relay_mover)(void *, qa_actor_id, bool close, qa_actor_id activator, qa_error *);
    /* Selected foreign door/teleporter owner handles ThreeWave passage. A
     * handled candidate ends nearby traversal even when passage is blocked. */
    bool (*observer_passage)(void *, qa_actor_id observer, qa_actor_id candidate,
                              bool *handled, qa_error *);
    /* Zero actor reads world state; a live player also reads its observer state.
     * The selected ThreeWave mode owns these values. */
    bool (*ctf_state)(void *, qa_actor_id, qa_q1_ctf_map_state *, qa_error *);
    bool (*ctf_pregame_end)(void *, qa_error *);
    bool (*egg_mover)(void *, qa_actor_id, qa_error *);
    bool (*grant_quad)(void *, qa_actor_id, double source_expiry, qa_error *);
    bool (*horde_control)(void *, qa_actor_id, bool check_wave, qa_error *);
    bool (*horde_keys)(void *, bool gold, int change, qa_error *);
    bool (*alpha_read)(void *, qa_actor_id, float *, qa_error *);
    bool (*alpha_write)(void *, qa_actor_id, float, qa_error *);
    bool (*retire_actor)(void *, qa_actor_id, qa_error *);
    bool (*schedule_remove)(void *, qa_actor_id, double delay, qa_error *);
    bool (*freeze_actor)(void *, qa_actor_id, qa_error *);
    bool (*fog_player)(void *, qa_actor_id, float density, qa_vec3 color, float duration,
                        qa_error *);
    /* Cinematic control mutates the selected movement/view/weapon owners.
     * Required when a map starts a cinematic; the map retains its own actors. */
    bool (*control_player)(void *, qa_actor_id, qa_vec3 origin, qa_vec3 angles, qa_vec3 view_offset,
                           qa_error *);
    bool (*finale)(void *, const qa_q1_map_finale_view *, qa_error *);
    bool (*finale_finished)(void *);
    bool (*finish_campaign)(void *, qa_error *);
    bool (*server_command)(void *, qa_string_id command, qa_error *);
} qa_q1_map_options;

/* Bind before authored spawning. Service owners remain valid until the next
 * begin_map call or destruction of the native game. */
bool qa_q1_game_maps_bind(qa_q1_game *, const qa_q1_map_options *, qa_error *);
/* After session world retirement and geometry publication, before spawning:
 * requires an empty registry, completed release fanout and a session safe
 * point. Replaces map services and resets map clocks, counters and actor
 * continuations. Retains provider RNG, attack sequence, resources and pooled
 * storage. The caller owns campaign state and player carry across retirement. */
bool qa_q1_game_begin_map(qa_q1_game *, const qa_q1_map_options *, qa_error *);
/* Link authored door groups after all map entities have been admitted. */
bool qa_q1_game_maps_finish(qa_q1_game *, qa_error *);
/* These adapters handle native actors only, for application owner dispatch. */
bool qa_q1_game_path_read(const qa_q1_game *, qa_actor_id, qa_q1_path_state *);
bool qa_q1_game_path_change(qa_q1_game *, qa_actor_id, const qa_q1_path_change *, qa_error *);
bool qa_q1_game_map_damage(qa_q1_game *, qa_actor_id, float damage, bool *handled, qa_error *);
bool qa_q1_game_map_relay_mover(qa_q1_game *, qa_actor_id, bool close,
                               qa_actor_id activator, bool *handled, qa_error *);
bool qa_q1_game_map_egg_mover(qa_q1_game *, qa_actor_id, bool *handled, qa_error *);
/* The selected mode owns waves, deadlines, loot and keys. Authored admission
 * reads canonical geometry into caller storage before mode configuration. */
bool qa_q1_game_map_horde_read(qa_q1_game *, qa_horde_options *, qa_horde_point *,
                              size_t capacity, size_t *count, bool *found, qa_error *);
/* Updates only authored fields; shared Horde keeps next_ns and occupied. */
bool qa_q1_game_map_horde_point_read(qa_q1_game *, qa_actor_id, qa_horde_point *,
                                    bool *found, qa_error *);
bool qa_q1_game_map_horde_manager_read(qa_q1_game *, qa_actor_id, qa_string_id *target,
                                      qa_actor_id *activator, bool *found, qa_error *);
/* Stops are detached into caller storage and remain valid until that storage
 * is reused. Reserve registry capacity; no source state or save payload is
 * allocated. Native activator chains select actual usable/shootable buttons. */
bool qa_q1_game_map_mover_read(qa_q1_game *, qa_actor_id, qa_q1_map_mover_view *,
                              qa_nav_train_stop *stops, size_t capacity, size_t *count,
                              bool *found, qa_error *);
/* Follower-owned adapter for native Rogue actors touching any authored corner. */
bool qa_q1_game_rogue_path_touch(qa_q1_game *, qa_actor_id corner, qa_actor_id follower,
                                 bool *handled, qa_error *);
/* Threewave spectator door/teleporter passage, after its velocity update. */
bool qa_q1_game_map_observer_nearby(qa_q1_game *, qa_actor_id, qa_error *);
/* Direct native capability for a foreign map's nearby traversal. */
bool qa_q1_game_map_observer_passage(qa_q1_game *, qa_actor_id observer, qa_actor_id candidate,
                                     bool *handled, qa_error *);
bool qa_q1_game_map_after_physics(qa_q1_game *, qa_actor_id, qa_error *);
/* Player frame extension; attack_finished is expressed on this source clock.
 * Native same-provider Q1 prethink calls this. The application supplies the
 * selected arsenal cooldown for other provider/family selections. */
bool qa_q1_game_map_coordinate_dump(qa_q1_game *, qa_actor_id, double attack_finished, qa_error *);
bool qa_q1_game_map_coordinates_enabled(const qa_q1_game *);
/* Dispatches authored world controls without entering the selected arsenal.
 * A consumed or source-gated command sets handled; unrelated impulses do not. */
bool qa_q1_game_map_impulse(qa_q1_game *, qa_actor_id, uint8_t impulse, bool *handled, qa_error *);
/* Shared ThreeWave match limits supply the chosen map; a native helper begins
 * the actual source campaign transition after .1 seconds. */
bool qa_q1_game_map_ctf_nextlevel(qa_q1_game *, qa_string_id map, qa_error *);
/* Rogue player order is earthquake, selected team/rune frame, then after_physics. */
bool qa_q1_game_rogue_earthquake(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_game_time_machine_crash(qa_q1_game *, qa_error *);
/* Native gameplay resets precede this campaign transition. The new-game flag
 * changes departing travel health and max health; live health remains intact. */
bool qa_q1_game_map_finish_addon(qa_q1_game *, qa_q1_map_ending, qa_error *);
bool qa_q1_game_map_new_game_travel(const qa_q1_game *);
bool qa_q1_game_map_finale(qa_q1_game *, qa_actor_id oldone, bool finish, qa_error *);
void qa_q1_game_map_dismiss_finale(qa_q1_game *);
qa_string_id qa_q1_game_map_name(const qa_q1_game *);
uint32_t qa_q1_game_campaign_flags(const qa_q1_game *);
qa_string_id qa_q1_game_map_text(const qa_q1_game *, qa_actor_id, qa_q1_campaign_text);
bool qa_q1_game_map_set_text(qa_q1_game *, qa_actor_id, qa_q1_campaign_text, qa_string_id,
                             qa_error *);
bool qa_q1_game_map_defer_targets(qa_q1_game *, const qa_target_use *, qa_error *);
bool qa_q1_game_map_defer_level(qa_q1_game *, double delay_seconds, qa_error *);
/* An authored Q1 helper owns the source-clock deadline; the target keeps its
 * selected actor owner's existing scheduler and is retired through retire_actor. */
bool qa_q1_game_map_defer_remove(qa_q1_game *, qa_actor_id, double delay_seconds, qa_error *);
bool qa_q1_game_map_defer_finale(qa_q1_game *, qa_q1_campaign_timer, double delay_seconds,
                                 qa_error *);
void qa_q1_game_map_secrets(const qa_q1_game *, uint32_t *total, uint32_t *found);
/* Actual selected player view offset; invoke once for the map source per frame. */
bool qa_q1_game_map_addon_player_frame(qa_q1_game *, qa_actor_id, qa_vec3 view_offset, qa_error *);
/* Authored additive source effects, independent of selected character. */
bool qa_q1_game_map_effects(const qa_q1_game *, qa_actor_id, uint32_t *out);

#endif
