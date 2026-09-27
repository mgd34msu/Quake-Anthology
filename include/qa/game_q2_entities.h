#ifndef QA_GAME_Q2_ENTITIES_H
#define QA_GAME_Q2_ENTITIES_H
#include "qa/game_q2.h"
#include "qa/targets.h"

typedef struct qa_q2_visual {
    qa_string_id models[4];
    int frame, old_frame, skin;
    uint64_t effects;
    uint32_t render_flags;
    float scale, alpha;
    bool visible;
} qa_q2_visual;
typedef struct qa_q2_map_fields {
    const qa_entity_property *properties;
    size_t count;
    uint32_t ordinal;
} qa_q2_map_fields;
typedef struct qa_q2_landmark {
    qa_string_id name;
    qa_vec3 relative_origin, relative_velocity, relative_view_angles;
    qa_actor_id player;
} qa_q2_landmark;
typedef struct qa_q2_fog {
    float density, sky_factor;
    qa_vec3 color, start_color, end_color;
    float start_distance, end_distance, falloff, height_density;
} qa_q2_fog;
typedef enum qa_q2_map_event_kind {
    QA_Q2_MAP_HELP,
    QA_Q2_MAP_MUSIC,
    QA_Q2_MAP_LIGHTSTYLE,
    QA_Q2_MAP_SKY,
    QA_Q2_MAP_STORY,
    QA_Q2_MAP_FOG,
    QA_Q2_MAP_POI,
    QA_Q2_MAP_HEALTHBAR,
    QA_Q2_MAP_WORLD_TEXT,
    QA_Q2_MAP_STEAM,
    QA_Q2_MAP_AUTOSAVE,
    QA_Q2_MAP_GOAL,
    QA_Q2_MAP_SECRET,
    QA_Q2_MAP_END_UNIT,
    QA_Q2_MAP_REMOVE_POI,
    QA_Q2_MAP_ACHIEVEMENT,
    QA_Q2_MAP_SCREEN_BLEND,
    QA_Q2_MAP_FORCE_WALL,
    QA_Q2_MAP_DYNAMIC_LIGHT,
    QA_Q2_MAP_MISSION_STATUS,
    QA_Q2_MAP_MISSION_OBJECTIVE,
    QA_Q2_MAP_HELP_COMPUTER
} qa_q2_map_event_kind;
typedef struct qa_q2_map_event {
    qa_q2_map_event_kind kind;
    qa_actor_id actor, recipient, target;
    qa_string_id text, resource;
    qa_vec3 origin, direction, color;
    qa_q2_fog fog;
    float value, duration, radius, alpha;
    float intensity, fade_start, fade_end, cone_cosine;
    int count, style, slot;
    uint32_t flags;
    uint32_t resolution;
    bool visible;
} qa_q2_map_event;
typedef struct qa_q2_path_follower {
    qa_actor_id move_target, enemy, old_enemy, activator;
    bool walking;
} qa_q2_path_follower;
typedef struct qa_q2_path_advance {
    qa_actor_id goal, move_target;
    qa_string_id target;
    uint64_t pause_until_ns;
    bool set_target, hold, finish;
} qa_q2_path_advance;
typedef struct qa_q2_entity_services {
    void *context;
    qa_targets *targets;
    bool ctf_map_rules;
    bool (*visual)(void *, qa_actor_id, const qa_q2_visual *, qa_error *);
    bool (*read_visual)(void *, qa_actor_id, qa_q2_visual *, qa_error *);
    bool (*event)(void *, const qa_q2_map_event *, qa_error *);
    bool (*area_portal)(void *, uint32_t portal, bool open, qa_error *);
    bool (*spawn)(void *, const char *classname, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity,
                  qa_actor_id *out, qa_error *);
    bool (*transition)(void *, qa_actor_id source, qa_actor_id activator, qa_string_id map,
                       const qa_q2_landmark *, bool end_unit, qa_error *);
    bool (*server_flags)(void *, bool write, bool cross_unit, uint32_t *, qa_error *);
    bool (*navigation)(void *, qa_actor_id, qa_vec3 start, qa_vec3 goal, qa_vec3 *points,
                       size_t capacity, size_t *count, bool *reachable, qa_error *);
    bool (*monsters_searching)(void *, qa_actor_id);
    bool (*in_phs)(void *, qa_vec3, qa_vec3);
    bool (*actor_gravity)(void *, qa_actor_id, float, qa_error *);
    bool (*world_gravity)(void *, float, qa_error *);
    bool (*player_push)(void *, qa_actor_id, qa_vec3, qa_error *);
    bool (*disguise)(void *, qa_actor_id, bool, qa_error *);
    bool (*target_name_changed)(void *, qa_actor_id, qa_string_id old_name, qa_string_id new_name,
                                qa_error *);
    bool (*path_follower)(void *, qa_actor_id, qa_q2_path_follower *);
    bool (*path_advance)(void *, qa_actor_id, const qa_q2_path_advance *, qa_error *);
    bool (*local_time)(void *, int *hour, int *minute, int *second, qa_error *);
    bool (*set_message)(void *, qa_actor_id, qa_string_id, qa_error *);
    bool (*invoke_use)(void *, qa_actor_id, qa_actor_id other, qa_actor_id activator, qa_error *);
    bool (*flashlight)(void *, qa_actor_id, bool enabled, qa_error *);
    bool (*player_fog)(void *, qa_actor_id, const qa_q2_fog *, unsigned affected, float transition,
                       qa_error *);
    bool (*relay_eligible)(void *, qa_actor_id, bool *, qa_error *);
    bool (*lightstyle)(void *, int style, qa_string_id *, qa_error *);
    bool (*holds_healthbar)(void *, qa_actor_id);
    bool (*camera_player)(void *, qa_actor_id, qa_vec3 origin, qa_vec3 angles, bool entering,
                          qa_error *);
    bool (*animate_reference)(void *, qa_actor_id owner, const qa_body_state *, qa_q2_visual *,
                              qa_error *);
} qa_q2_entity_services;
typedef enum q2_entity_kind {
    Q2E_POINT,
    Q2E_WORLD,
    Q2E_MULTI,
    Q2E_RELAY,
    Q2E_COUNTER,
    Q2E_KEY,
    Q2E_SPEAKER,
    Q2E_TIMER,
    Q2E_LIGHT,
    Q2E_PORTAL,
    Q2E_HELP,
    Q2E_SECRET,
    Q2E_GOAL,
    Q2E_CHANGELEVEL,
    Q2E_EXPLOSION,
    Q2E_SPLASH,
    Q2E_PUSH,
    Q2E_HURT,
    Q2E_GRAVITY,
    Q2E_MONSTERJUMP,
    Q2E_LASER,
    Q2E_LIGHTRAMP,
    Q2E_TEMP,
    Q2E_SPAWNER,
    Q2E_BLASTER,
    Q2E_CROSS_TRIGGER,
    Q2E_CROSS_TARGET,
    Q2E_EARTHQUAKE,
    Q2E_TELEPORT,
    Q2E_DISGUISE,
    Q2E_STEAM,
    Q2E_ANGER,
    Q2E_KILLPLAYERS,
    Q2E_SOUND_FX,
    Q2E_GLOBAL_GRAVITY,
    Q2E_DOOR,
    Q2E_BUTTON,
    Q2E_WATER,
    Q2E_TRAIN,
    Q2E_ROTATING,
    Q2E_PATH,
    Q2E_COMBAT_POINT,
    Q2E_DOOR_TRIGGER,
    Q2E_PLAT,
    Q2E_PLAT_TRIGGER,
    Q2E_SECRET_DOOR,
    Q2E_ELEVATOR,
    Q2E_CONVEYOR,
    Q2E_KILLBOX,
    Q2E_OBJECT,
    Q2E_FORCEWALL,
    Q2E_SCENERY,
    Q2E_FLARE,
    Q2E_WORLD_TEXT,
    Q2E_FLASHLIGHT,
    Q2E_FOG,
    Q2E_COOP_RELAY,
    Q2E_POI,
    Q2E_MUSIC,
    Q2E_SKY,
    Q2E_CROSS_UNIT_TRIGGER,
    Q2E_CROSS_UNIT_TARGET,
    Q2E_AUTOSAVE,
    Q2E_ACHIEVEMENT,
    Q2E_STORY,
    Q2E_HEALTHBAR,
    Q2E_DYNAMIC_LIGHT,
    Q2E_TURRET_BASE,
    Q2E_TURRET_BREACH,
    Q2E_TURRET_DRIVER,
    Q2E_EYE,
    Q2E_SPINNING,
    Q2E_CAMERA,
    Q2E_CAMERA_DUMMY
} q2_entity_kind;
typedef enum q2_entity_think {
    Q2ET_NONE,
    Q2ET_FREE,
    Q2ET_MULTI_READY,
    Q2ET_TIMER,
    Q2ET_EXPLOSION,
    Q2ET_LASER_START,
    Q2ET_LASER,
    Q2ET_LIGHTRAMP,
    Q2ET_CROSS,
    Q2ET_QUAKE,
    Q2ET_STEAM,
    Q2ET_SOUND_FX,
    Q2ET_PUSH,
    Q2ET_MOVE_BEGIN,
    Q2ET_MOVE_FINAL,
    Q2ET_MOVE_DONE,
    Q2ET_MOVE_ACCEL,
    Q2ET_DOOR_PREPARE,
    Q2ET_DOOR_DOWN,
    Q2ET_TRAIN_FIND,
    Q2ET_TRAIN_NEXT,
    Q2ET_SECRET_NEXT,
    Q2ET_OBJECT_FALL,
    Q2ET_SMART_WATER,
    Q2ET_COOP_RELAY,
    Q2ET_POI,
    Q2ET_HEALTHBAR,
    Q2ET_WORLD_TEXT,
    Q2ET_SCENERY,
    Q2ET_PLAT_UP,
    Q2ET_PLAT_DOWN,
    Q2ET_ELEVATOR,
    Q2ET_FORCEWALL,
    Q2ET_DYNAMIC_LIGHT,
    Q2ET_TURRET_INIT,
    Q2ET_TURRET,
    Q2ET_TURRET_LINK,
    Q2ET_TURRET_DRIVER,
    Q2ET_EYE_SETUP,
    Q2ET_EYE,
    Q2ET_SPINNING,
    Q2ET_CAMERA,
    Q2ET_CAMERA_DUMMY,
    Q2ET_PLAYER_SECURITY,
    Q2ET_PLAYER_COOP_FIX,
    Q2ET_PLAYER_START_DROP
} q2_entity_think;
typedef enum q2_move_done {
    Q2MD_NONE,
    Q2MD_DOOR_TOP,
    Q2MD_DOOR_BOTTOM,
    Q2MD_TRAIN_WAIT,
    Q2MD_PLAT_TOP,
    Q2MD_PLAT_BOTTOM,
    Q2MD_SECRET_NEXT
} q2_move_done;
typedef struct q2_motion {
    qa_vec3 direction, destination, reference;
    float remaining, current_speed, move_speed, next_speed, decel_distance;
    float curve_from, curve_to, curve_distance;
    uint64_t curve_time_ns;
    q2_move_done done;
    bool angular, accelerated, curve, final_sample;
} q2_motion;
typedef struct q2_mover {
    qa_vec3 start, end, intermediate, safe_direction;
    qa_actor_id master, next, destination;
    float distance, water_divisor;
    int phase, stage;
    bool angular, reversed, activated, ship, moving;
    q2_motion motion;
} q2_mover;
typedef struct q2_field {
    qa_string_id key, value;
} q2_field;
typedef struct q2_turret {
    qa_vec3 goal, muzzle;
    float pitch_min, pitch_max, yaw_min, yaw_max;
    float radius, yaw_offset, height;
    qa_actor_id breach;
} q2_turret;
typedef struct q2_q64 {
    qa_vec3 neutral, eye_position, angles;
    float vision_cone, remaining, distance, speed;
    float fade_remaining, fade_duration;
    uint32_t hackflags;
    bool fading;
} q2_q64;
typedef enum q2_scenery_kind {
    Q2S_NONE,
    Q2S_WALL,
    Q2S_EXPLOSIVE,
    Q2S_BARREL,
    Q2S_BANNER,
    Q2S_SATELLITE,
    Q2S_SOLDIER,
    Q2S_GIB,
    Q2S_ANIMATION,
    Q2S_BLACKHOLE,
    Q2S_COMMANDER,
    Q2S_BOMB,
    Q2S_CHARACTER,
    Q2S_STRING,
    Q2S_CLOCK,
    Q2S_TELEPORTER,
    Q2S_TELEPORT_TRIGGER,
    Q2S_ROTATING_LIGHT,
    Q2S_REPAIR,
    Q2S_MISSILE,
    Q2S_AMBIENCE,
    Q2S_NUKE,
    Q2S_MAL_LASER
} q2_scenery_kind;
typedef struct qa_q2_entity_state {
    q2_entity_kind kind;
    q2_entity_think think;
    qa_string_id classname, targetname, target, killtarget, message, team, map, noise;
    q2_field *fields;
    size_t field_count;
    uint32_t ordinal, spawnflags;
    qa_q2_visual visual;
    qa_actor_collision collision;
    qa_actor_id activator, owner, enemy, goal;
    qa_vec3 direction, beam_end;
    float speed, accel, decel, wait, delay, damage, health, random, volume, attenuation;
    uint64_t due_ns, timestamp_ns, debounce_ns, sound_ns, expires_ns;
    int count, style, stage;
    bool usable, touchable, active, dispatching, has_inline, dirty;
    q2_mover *mover;
    q2_turret *turret;
    q2_q64 *q64;
    int animation_first, animation_end, clock_value;
    q2_scenery_kind scenery;
} qa_q2_entity_state;
typedef struct q2_healthbar {
    qa_actor_id controller, target;
    uint64_t dead_until_ns;
    bool dying;
} q2_healthbar;
typedef struct q2_wind_time {
    qa_actor_id actor;
    uint64_t until_ns;
} q2_wind_time;
typedef struct qa_q2_entity_checkpoint {
    uint32_t version;
    bool present;
    qa_q2_entity_state value;
    qa_q2_saved_reference activator, owner, enemy, goal, collision_owner;
    qa_q2_saved_reference master, next, destination, turret_breach;
} qa_q2_entity_checkpoint;
typedef struct qa_q2_wind_checkpoint {
    qa_q2_saved_reference actor;
    uint64_t until_ns;
} qa_q2_wind_checkpoint;
typedef struct qa_q2_healthbar_checkpoint {
    qa_q2_saved_reference controller, target;
    uint64_t dead_until_ns;
    bool dying;
} qa_q2_healthbar_checkpoint;
typedef struct qa_q2_entities_checkpoint {
    uint32_t version;
    qa_q2_saved_reference poi, poi_dynamic;
    qa_string_id poi_image, story;
    int poi_stage, steam_id, total_secrets, found_secrets, total_goals, found_goals;
    uint64_t last_autosave_ns;
    qa_q2_healthbar_checkpoint bars[2];
    qa_q2_fog world_fog;
    qa_string_id sky, goals, primary, secondary;
    qa_vec3 sky_axis;
    float sky_rotation;
    uint32_t primary_changes, secondary_changes;
    unsigned goal_number;
    bool sky_auto, has_goals;
    qa_q2_wind_checkpoint *wind;
    size_t wind_count;
} qa_q2_entities_checkpoint;
/* Owned native value snapshots, not serialized C memory. Codecs remap string
 * identities and encode individual fields. Embedded live actor IDs are zero;
 * saved references name actors restored by the shared registry first. Restore
 * all actor extensions before validate_links; no spawn/use callbacks replay. */
bool qa_q2_entity_capture(qa_q2_game *, qa_actor_id, qa_q2_entity_checkpoint *, qa_error *);
bool qa_q2_entity_restore(qa_q2_game *, qa_actor_id, const qa_q2_entity_checkpoint *, qa_error *);
void qa_q2_entity_checkpoint_free(qa_q2_entity_checkpoint *);
bool qa_q2_entities_capture(qa_q2_game *, qa_q2_entities_checkpoint *, qa_error *);
bool qa_q2_entities_restore(qa_q2_game *, const qa_q2_entities_checkpoint *, qa_error *);
void qa_q2_entities_checkpoint_free(qa_q2_entities_checkpoint *);
bool qa_q2_entities_validate_links(qa_q2_game *, qa_error *);
bool qa_q2_entities_configure(qa_q2_game *, const qa_q2_entity_services *, qa_error *);
bool qa_q2_entity_spawn(qa_q2_game *, qa_actor_id, const qa_q2_map_fields *, bool *handled,
                        qa_error *);
bool qa_q2_entity_use(qa_q2_game *, qa_actor_id, qa_actor_id other, qa_actor_id activator,
                      qa_error *);
bool qa_q2_entity_blocked(qa_q2_game *, qa_actor_id, qa_actor_id obstacle, qa_error *);
bool qa_q2_entities_post_spawn(qa_q2_game *, qa_error *);
bool qa_q2_entities_present(qa_q2_game *, qa_error *);
bool qa_q2_turret_driver_detach(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_entity_target(qa_q2_game *, qa_actor_id, qa_string_id *targetname, qa_string_id *target,
                         qa_string_id *killtarget, qa_error *);
bool qa_q2_entity_authored(qa_q2_game *, qa_actor_id, qa_authored_target *);
bool qa_q2_entity_use_targets(qa_q2_game *, qa_actor_id, qa_actor_id activator, bool ignore_delay,
                              qa_error *);
bool qa_q2_entity_visual(qa_q2_game *, qa_actor_id, qa_q2_visual *, qa_error *);
bool qa_q2_entity_team(qa_q2_game *, qa_actor_id, qa_actor_id *master, qa_actor_id *next);
bool qa_q2_entity_field(qa_q2_game *, qa_actor_id, const char *key, qa_string_id *value);
bool qa_q2_entity_set_target(qa_q2_game *, qa_actor_id, qa_string_id, qa_error *);
bool qa_q2_entity_set_targetname(qa_q2_game *, qa_actor_id, qa_string_id, qa_error *);
bool qa_q2_entities_player_frame(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_entities_player_reset(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_healthbar_transfer(qa_q2_game *, qa_actor_id old_actor, qa_actor_id new_actor,
                              qa_error *);
bool qa_q2_entities_killbox(qa_q2_game *, qa_actor_id, qa_actor_id credited, bool *clear,
                            qa_error *);
#endif
