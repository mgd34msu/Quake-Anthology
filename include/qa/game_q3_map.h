#ifndef QA_GAME_Q3_MAP_H
#define QA_GAME_Q3_MAP_H

#include "qa/bsp.h"
#include "qa/game_q3.h"
#include "qa/targets.h"

typedef struct qa_q3_map_fields {
    const qa_entity_property *properties;
    size_t count;
    uint32_t ordinal;
} qa_q3_map_fields;

typedef enum qa_q3_map_event_kind {
    QA_Q3_MAP_CONFIGSTRING,
    QA_Q3_MAP_CVAR,
    QA_Q3_MAP_SCORE,
    QA_Q3_MAP_RETURN_FLAG,
    QA_Q3_MAP_PRINT,
    QA_Q3_MAP_SOUND,
    QA_Q3_MAP_EVENT_PORTAL_SURFACE,
    QA_Q3_MAP_EVENT_PORTAL_CAMERA,
    QA_Q3_MAP_SPAWNPOINT,
    QA_Q3_MAP_LOCATION,
    QA_Q3_MAP_AREA_PORTAL
} qa_q3_map_event_kind;
typedef enum qa_q3_spawnpoint_kind {
    QA_Q3_SPAWN_START,
    QA_Q3_SPAWN_DEATHMATCH,
    QA_Q3_SPAWN_INTERMISSION,
    QA_Q3_SPAWN_RED_PLAYER,
    QA_Q3_SPAWN_BLUE_PLAYER,
    QA_Q3_SPAWN_RED,
    QA_Q3_SPAWN_BLUE
} qa_q3_spawnpoint_kind;
typedef struct qa_q3_map_event {
    qa_q3_map_event_kind kind;
    qa_actor_id actor, other;
    qa_string_id name, text;
    qa_vec3 origin, angles, direction, destination;
    float value;
    int32_t index, points, team, sound_interval_tenths, sound_random_tenths;
    uint32_t flags;
    bool no_bots, no_humans;
} qa_q3_map_event;
typedef struct qa_q3_map_options {
    qa_targets *targets;
    void *context;
    qa_string_id motd;
    uint32_t random_seed;
    int32_t start_time_ms, restarted;
    bool warmup;
    bool (*event)(void *, const qa_q3_map_event *, qa_error *);
    bool (*item_disabled)(void *, uint32_t item_index);
    bool (*world_gravity)(void *, float gravity, qa_error *);
    void (*diagnostic)(void *, qa_actor_id, const char *);
} qa_q3_map_options;

typedef enum qa_q3_map_spawn_status {
    QA_Q3_MAP_WORLD,
    QA_Q3_MAP_SPAWNED,
    QA_Q3_MAP_FILTERED,
    QA_Q3_MAP_UNKNOWN
} qa_q3_map_spawn_status;
typedef enum qa_q3_map_filter {
    QA_Q3_MAP_FILTER_NONE,
    QA_Q3_MAP_FILTER_NOT_SINGLE,
    QA_Q3_MAP_FILTER_NOT_TEAM,
    QA_Q3_MAP_FILTER_NOT_FREE,
    QA_Q3_MAP_FILTER_NOT_TA,
    QA_Q3_MAP_FILTER_NOT_Q3A,
    QA_Q3_MAP_FILTER_GAMETYPE,
    QA_Q3_MAP_FILTER_DISABLED_ITEM
} qa_q3_map_filter;
typedef struct qa_q3_map_spawn_result {
    qa_q3_map_spawn_status status;
    qa_q3_map_filter filter;
    qa_actor_id actor;
    qa_string_id classname;
} qa_q3_map_spawn_result;

typedef enum qa_q3_map_kind {
    QA_Q3_MAP_POINT,
    QA_Q3_MAP_ITEM,
    QA_Q3_MAP_TRIGGER_MULTIPLE,
    QA_Q3_MAP_TRIGGER_ALWAYS,
    QA_Q3_MAP_TRIGGER_PUSH,
    QA_Q3_MAP_TRIGGER_TELEPORT,
    QA_Q3_MAP_TRIGGER_HURT,
    QA_Q3_MAP_TIMER,
    QA_Q3_MAP_TARGET_GIVE,
    QA_Q3_MAP_TARGET_REMOVE_POWERUPS,
    QA_Q3_MAP_TARGET_DELAY,
    QA_Q3_MAP_TARGET_SCORE,
    QA_Q3_MAP_TARGET_PRINT,
    QA_Q3_MAP_TARGET_SPEAKER,
    QA_Q3_MAP_TARGET_PUSH,
    QA_Q3_MAP_TARGET_LASER,
    QA_Q3_MAP_TARGET_TELEPORTER,
    QA_Q3_MAP_TARGET_KILL,
    QA_Q3_MAP_TARGET_LOCATION,
    QA_Q3_MAP_TARGET_RELAY,
    QA_Q3_MAP_TARGET_POSITION,
    QA_Q3_MAP_PORTAL_SURFACE,
    QA_Q3_MAP_PORTAL_CAMERA,
    QA_Q3_MAP_SHOOTER,
    QA_Q3_MAP_PATH_CORNER,
    QA_Q3_MAP_MOVER_DOOR,
    QA_Q3_MAP_MOVER_PLAT,
    QA_Q3_MAP_MOVER_BUTTON,
    QA_Q3_MAP_MOVER_TRAIN,
    QA_Q3_MAP_MOVER_STATIC,
    QA_Q3_MAP_MOVER_ROTATING,
    QA_Q3_MAP_MOVER_BOBBING,
    QA_Q3_MAP_MOVER_PENDULUM,
    QA_Q3_MAP_MOVER_DOOR_TRIGGER,
    QA_Q3_MAP_MOVER_PLAT_TRIGGER
} qa_q3_map_kind;
typedef enum qa_q3_map_think {
    QA_Q3_MAP_THINK_NONE,
    QA_Q3_MAP_THINK_FREE,
    QA_Q3_MAP_THINK_MULTI_READY,
    QA_Q3_MAP_THINK_ALWAYS,
    QA_Q3_MAP_THINK_TIMER,
    QA_Q3_MAP_THINK_AIM,
    QA_Q3_MAP_THINK_LASER_START,
    QA_Q3_MAP_THINK_LASER,
    QA_Q3_MAP_THINK_DELAY,
    QA_Q3_MAP_THINK_LOCATIONS,
    QA_Q3_MAP_THINK_PORTAL,
    QA_Q3_MAP_THINK_SHOOTER,
    QA_Q3_MAP_THINK_ITEM_FINISH,
    QA_Q3_MAP_THINK_ITEM_RESPAWN,
    QA_Q3_MAP_THINK_MOVER_DOOR_SETUP,
    QA_Q3_MAP_THINK_MOVER_TRAIN_SETUP,
    QA_Q3_MAP_THINK_MOVER_TRAIN_RESUME
} qa_q3_map_think;
typedef struct qa_q3_map_actor_state {
    qa_actor_id actor, activator, enemy, team_master, team_next, parent, path_next;
    qa_q3_map_kind kind;
    qa_q3_map_think think;
    qa_string_id classname, model, model2, targetname, target, message, team;
    qa_string_id noise, shader_old, shader_new;
    qa_vec3 origin, angles, direction, launch_velocity, first, second, color;
    qa_bounds bounds;
    qa_q3_item_spawn item;
    uint32_t spawnflags, inline_model, ordinal;
    int32_t count, health, damage, due_ms, cooldown_ms, sound_frame, sound_random;
    int32_t noise_index, sound_1_to_2, sound_2_to_1, sound_pos_1, sound_pos_2;
    int32_t sound_loop;
    float speed, wait, random, delay, roll, light, alpha;
    bool active, linked, has_inline_model, touchable, usable, team_slave, item_bound;
    bool damageable;
    bool has_delay;
    bool sound_looping, has_color, has_light, no_bots, no_humans;
} qa_q3_map_actor_state;
typedef struct qa_q3_map_actor_checkpoint {
    qa_q3_map_actor_state state;
    qa_saved_actor_id actor, activator, enemy, team_master, team_next, parent, path_next;
} qa_q3_map_actor_checkpoint;
typedef struct qa_q3_map_checkpoint {
    uint32_t version;
    uint64_t registered_items;
    qa_string_id motd;
    uint32_t random_seed;
    int32_t start_time_ms, restarted, loaded_game_type;
    float gravity;
    bool warmup, world_spawned, post_spawned, locations_linked;
    qa_saved_actor_id location_head;
    qa_q3_map_actor_checkpoint *actors;
    size_t actor_count;
} qa_q3_map_checkpoint;
typedef struct qa_q3_map_spawnpoint {
    qa_actor_id actor;
    qa_q3_spawnpoint_kind kind;
    qa_vec3 origin, angles;
    uint32_t flags, ordinal;
    bool no_bots, no_humans;
} qa_q3_map_spawnpoint;

bool qa_q3_maps_bind(qa_q3_game *, const qa_q3_map_options *, qa_error *);
/* Bind only the immutable source services for an isolated save candidate.
 * The portable continuation restores its world, client and body queue rows. */
bool qa_q3_maps_bind_restore(qa_q3_game *, const qa_q3_map_options *, qa_error *);
/* After the session has retired every actor and committed replacement geometry,
 * replace only Q3's map-local runtime. Provider identities, rules and detached
 * component/policy descriptors survive. This does not advance or synthesize a
 * source frame. */
bool qa_q3_maps_reset(qa_q3_game *, const qa_q3_map_options *, qa_error *);
bool qa_q3_map_spawn(qa_q3_game *, const qa_q3_map_fields *, qa_q3_map_spawn_result *,
                     qa_error *);
bool qa_q3_maps_post_spawn(qa_q3_game *, qa_error *);
bool qa_q3_map_item_registered(const qa_q3_game *, uint32_t item_index, bool *, qa_error *);
bool qa_q3_map_use(qa_q3_game *, qa_actor_id, qa_actor_id other, qa_actor_id activator,
                   qa_error *);
bool qa_q3_map_spawnpoint_next(const qa_q3_game *, uint32_t *cursor,
                               qa_q3_map_spawnpoint *);
bool qa_q3_map_nearest_location(const qa_q3_game *, qa_vec3 origin, qa_actor_id *actor,
                                qa_string_id *message);
typedef struct qa_q3_map_team_location {
    qa_actor_id actor;
    qa_string_id message;
    int32_t id, count;
} qa_q3_map_team_location;
bool qa_q3_map_team_location_read(qa_q3_game *, qa_actor_id source_client,
                                  qa_q3_map_team_location *, bool *found, qa_error *);
bool qa_q3_map_location_count_set(qa_q3_game *, qa_actor_id, int32_t, qa_error *);
bool qa_q3_map_actor_capture(const qa_q3_game *, qa_actor_id, qa_q3_map_actor_state *);
bool qa_q3_map_actor_restore(qa_q3_game *, const qa_q3_map_actor_state *, qa_error *);
bool qa_q3_map_checkpoint_capture(const qa_q3_game *, qa_q3_map_checkpoint *, qa_error *);
bool qa_q3_map_checkpoint_restore(qa_q3_game *, const qa_q3_map_checkpoint *, qa_error *);
void qa_q3_map_checkpoint_free(qa_q3_map_checkpoint *);

#endif
