#ifndef QA_Q1_INTERNAL_H
#define QA_Q1_INTERNAL_H

#include "boss_types.h"
#include "frame_actions.h"
#include "qa/game_q1.h"
#include "qa/game_q1_source_powers.h"
#include "qa/game_q1_rogue.h"
#include "qa/game_q1_source_flags.h"
#include "qa/game_q1_source_runes.h"
#include "qa/game_q1_source_rogue_tag.h"
#include "qa/game_q1_source_rogue_flags.h"
#include <stdlib.h>
#include <string.h>

/* SV_SpawnServer starts the source clock at one second, before entity spawn. */
#define Q1_SOURCE_INITIAL_TIME_NS UINT64_C(1000000000)

typedef struct q1_map_state q1_map_state;
typedef struct q1_map_runtime q1_map_runtime;

typedef enum q1_entity_kind {
    Q1_ENTITY,
    Q1_MONSTER,
    Q1_PROJECTILE,
    Q1_TIMER,
    Q1_PICKUP,
    Q1_GIB,
    Q1_MAP,
    Q1_BOSS_CHILD,
    Q1_ROGUE_TEAM_STATE,
    Q1_SOURCE_CTF_FLAG,
    Q1_SOURCE_CTF_RUNE,
    Q1_SOURCE_CTF_RUNE_TIMER,
    Q1_SOURCE_ROGUE_TAG,
    Q1_SOURCE_ROGUE_FLAG,
    Q1_SOURCE_ROGUE_FLAG_BASE,
    Q1_SOURCE_ROGUE_RUNE,
    Q1_SOURCE_ROGUE_RUNE_TIMER
} q1_entity_kind;
typedef enum q1_think_kind {
    Q1_THINK_NONE,
    Q1_THINK_REMOVE,
    Q1_THINK_MONSTER_START,
    Q1_THINK_MONSTER_FRAME,
    Q1_THINK_AXE,
    Q1_THINK_EXPLODE,
    Q1_THINK_VORE,
    Q1_THINK_SPRITE,
    Q1_THINK_WIZARD,
    Q1_THINK_RESPAWN,
    Q1_THINK_MEGA_ROT,
    Q1_THINK_MONSTER_FOUND,
    Q1_THINK_ITEM_PLACE,
    Q1_THINK_DEATH_BUBBLES,
    Q1_THINK_BUBBLE,
    Q1_THINK_HIP_LASER,
    Q1_THINK_PROX_WATCH,
    Q1_THINK_PROX_EXPLODE,
    Q1_THINK_HAMMER_STRIKE,
    Q1_THINK_HAMMER_BOLT,
    Q1_THINK_MULTI_SPLIT,
    Q1_THINK_MINI_EXPLODE,
    Q1_THINK_MULTI_EXPLODE,
    Q1_THINK_MULTI_ACQUIRE,
    Q1_THINK_MULTI_HOME,
    Q1_THINK_PLASMA_LAUNCH,
    Q1_THINK_SHIELD,
    Q1_THINK_SPHERE_ORBIT,
    Q1_THINK_SPHERE_ATTACK,
    Q1_THINK_HOOK_FLY,
    Q1_THINK_HOOK_TRACK,
    Q1_THINK_HOOK_RESET,
    Q1_THINK_HOOK_LINK,
    Q1_THINK_SCOURGE_TRIGGER,
    Q1_THINK_WRATH_HOME,
    Q1_THINK_WRATH_EXPLODE,
    Q1_THINK_TELEPORT_FOG,
    Q1_THINK_ARMAGON_BODY,
    Q1_THINK_ARMAGON_EXPLOSION,
    Q1_THINK_MULTI_EXPLOSION,
    Q1_THINK_HOOK_LAUNCH,
    Q1_THINK_MAP,
    Q1_THINK_MG3_HAMMER,
    Q1_THINK_MG3_ITEM_START,
    Q1_THINK_DEMODOG_EXPLODE,
    Q1_THINK_HORDE_HEAD_WAIT,
    Q1_THINK_HORDE_HEAD_STEP,
    Q1_THINK_HEAVY_SOURCE_DIE,
    Q1_THINK_GHOST_BUBBLES,
    Q1_THINK_HOMING_FLAME,
    Q1_THINK_BOSS_CHILD,
    Q1_THINK_SPAWN_TEMPLATE,
    Q1_THINK_SOURCE_CTF_FLAG_PLACE,
    Q1_THINK_SOURCE_CTF_FLAG,
    Q1_THINK_SOURCE_CTF_RUNE_SPAWN,
    Q1_THINK_SOURCE_CTF_RUNE_RESPAWN,
    Q1_THINK_SOURCE_ROGUE_TAG_PLACE,
    Q1_THINK_SOURCE_ROGUE_TAG,
    Q1_THINK_SOURCE_ROGUE_TAG_FALL,
    Q1_THINK_SOURCE_ROGUE_TAG_RESPAWN,
    Q1_THINK_SOURCE_ROGUE_FLAG_PLACE,
    Q1_THINK_SOURCE_ROGUE_FLAG,
    Q1_THINK_SOURCE_ROGUE_RUNE_SPAWN,
    Q1_THINK_SOURCE_ROGUE_RUNE_RESPAWN
} q1_think_kind;
typedef enum q1_projectile_kind {
    Q1_SPIKE,
    Q1_SUPERSPIKE,
    Q1_ROCKET,
    Q1_GRENADE,
    Q1_WIZARD_SPIKE,
    Q1_KNIGHT_SPIKE,
    Q1_ENFORCER_LASER,
    Q1_OGRE_GRENADE,
    Q1_ZOMBIE_GRENADE,
    Q1_VORE_BALL,
    Q1_LAVA_BALL,
    Q1_HIP_LASER,
    Q1_PROXIMITY,
    Q1_LAVA_SPIKE,
    Q1_MULTI_GRENADE,
    Q1_MULTI_ROCKET,
    Q1_PLASMA,
    Q1_VENGEANCE,
    Q1_ROGUE_HOOK,
    Q1_CTF_HOOK,
    Q1_WRATH_MISSILE,
    Q1_LAVAMAN_BALL,
    Q1_DRAGON_FIREBALL,
    Q1_MG3_OGRE_ROCKET,
    Q1_DEMODOG_GRENADE,
    Q1_HEAVY_SPIKE,
    Q1_MG3_LAVAMAN_BALL,
    Q1_ORB_ROCK,
    Q1_SHUB_GRENADE,
    Q1_BOSS_SPHERE_SHOT,
    Q1_BOSS_BLAST_SHOT,
    Q1_FINAL_ROCK
} q1_projectile_kind;
typedef enum q1_heavy_kind {
    Q1_HEAVY_NONE,
    Q1_HEAVY_SUPER_SHAMBLER,
    Q1_HEAVY_RUNE_KNIGHT
} q1_heavy_kind;
typedef enum q1_boss_kind {
    Q1_BOSS_NONE,
    Q1_BOSS_GHOST,
    Q1_BOSS_ORB,
    Q1_BOSS_SHUB_ZOMBIE,
    Q1_BOSS_OLDNEW,
    Q1_BOSS_FINAL
} q1_boss_kind;
typedef enum q1_ai {
    Q1_AI_STAND,
    Q1_AI_WALK,
    Q1_AI_RUN,
    Q1_AI_CHARGE_SIDE,
    Q1_AI_MELEE_SIDE,
    Q1_AI_CHARGE,
    Q1_AI_MELEE,
    Q1_AI_PAINFORWARD,
    Q1_AI_TURN,
    Q1_AI_FACE,
    Q1_AI_PAIN,
    Q1_AI_FORWARD
} q1_ai;
typedef enum q1_frame_operation_kind {
    Q1_FRAME_AI,
    Q1_FRAME_SOUND,
    Q1_FRAME_SOLID,
    Q1_FRAME_LIGHTSTYLE,
    Q1_FRAME_ACTION
} q1_frame_operation_kind;
typedef struct q1_frame_operation {
    q1_frame_operation_kind kind;
    q1_ai ai;
    q1_frame_action action;
    float distance, attenuation, chance;
    int32_t channel;
    bool greater;
    const char *text;
} q1_frame_operation;
typedef struct q1_frame {
    const char *name;
    uint16_t frame, next;
    uint32_t operation;
    uint8_t count;
} q1_frame;
typedef struct q1_species {
    qa_q1_species species;
    const char *classname, *model, *head, *sight, *stand, *walk, *run, *missile;
    float health, gib_health;
    qa_bounds bounds;
    uint32_t flags;
    bool melee;
} q1_species;
typedef struct q1_monster {
    const q1_species *species;
    uint64_t birth_epoch;
    bool dead;
    qa_string_id path;
    uint16_t current_frame, next_frame;
    qa_actor_id enemy, old_enemy, charmer, charm_goal, move_target, previous_corner;
    double pause_until, attack_finished, pain_finished, search_until, idle_until, straight_after,
        dodge_after, hostile_until, follow_until;
    uint32_t counter, lightning_count;
    uint8_t attack_state, in_pain, hunting_charmer;
    bool refired, sliding, lefty, counted_death, jump_touch, horde, path_end;
    struct {
        bool enabled, waiting, path_wait, started, rocket_ogre, allow_path, normal_use;
        bool infected, transformed, risen, infection_count_pending, demodog;
        uint8_t infected_kind, corpse;
        q1_heavy_kind heavy;
        q1_boss_kind boss;
        uint8_t projectiles, projectile_max, combat_style;
        double damage_at;
    } addon;
    union {
        struct {
            qa_vec3 anchor, destination;
            uint16_t death_frame;
            int32_t shots, shocks;
            bool touch, immune, awake, swipe_side, vortex_side;
            uint32_t phase, cycles, stage;
            qa_string_id waves[4];
        } boss;
        struct {
            qa_actor_id child;
            uint32_t lightning_count;
            int32_t nails;
        } heavy;
        struct {
            int16_t pitch;
        } eel;
        struct {
            bool asleep;
        } mummy;
        struct {
            bool awakened, pain_disabled;
        } sword;
        struct {
            qa_actor_id trigger;
            double dodge_until;
            bool initialized, silent, previous_silent;
        } scourge;
        struct {
            uint32_t children;
        } morph;
        struct {
            qa_vec3 last_velocity;
            uint16_t missile;
            uint8_t pain_sequence, death_state;
            bool attacking;
        } dragon;
        struct {
            qa_actor_id body;
            qa_vec3 old_origin;
            float torso_yaw, aim_threshold;
            double idle_at;
            uint8_t repulse_state;
            bool behind;
        } armagon;
        struct {
            qa_actor_id last_victim, flee_goal;
            qa_vec3 view_angles;
            qa_q1_weapon weapon;
            float current_ammo;
            double protection_sound;
            uint8_t touch;
            bool stolen, gorging, pain_disabled;
        } gremlin;
    } source;
} q1_monster;
typedef struct q1_projectile {
    q1_projectile_kind kind;
    qa_q1_weapon weapon;
    qa_attack attack;
    qa_actor_id enemy, activator, surface;
    qa_actor_id links[3];
    qa_vec3 right, movedir, launch_angles;
    float damage, radius_damage;
    double expires;
    uint32_t count;
    bool remove_touch, mini, detonating;
} q1_projectile;
typedef enum q1_drop_kind {
    Q1_DROP_NONE,
    Q1_DROP_CTF_AMMO,
    Q1_DROP_CTF_WEAPON,
    Q1_DROP_ROGUE_WEAPON
} q1_drop_kind;
typedef struct q1_pickup {
    qa_item_id item;
    qa_string_id original_model, sound;
    float count, respawn;
    uint32_t kind;
    uint32_t upgrade_flag;
    uint8_t upgrade;
    qa_actor_id holder;
    bool hidden, mega, artifact, mission, random, external;
    bool backpack_rank, avoid_underwater_lightning;
    float absorption, duration;
    qa_q1_weapon weapon;
    q1_drop_kind drop;
    float owner_delay;
    float ammo[QA_Q1_AMMO_COUNT];
} q1_pickup;
typedef struct q1_timed_effect {
    double expires;
    float volume;
    uint32_t count;
} q1_timed_effect;
typedef struct q1_actor {
    struct q1_actor *allocation_next, *pool_next;
    q1_map_state *map;
    qa_pickup_lease pickup_observation;
    bool restored_target;
    qa_actor_id id, owner, activator;
    qa_string_id classname, model, target, targetname, killtarget, message;
    qa_string_id source_netname, source_kill_string, source_death_type, source_team;
    qa_string_id rogue_next_update;
    qa_actor_id rogue_tag_owner;
    qa_string_id rogue_runes_spawned;
    qa_actor_id rogue_rune_spawn;
    qa_string_id ctf_last_capture, ctf_last_capture_team;
    qa_string_id ctf_runes_spawned;
    qa_actor_id ctf_rune_spawn;
    qa_vec3 initial_angles;
    qa_physics_properties physics;
    q1_entity_kind kind;
    q1_think_kind think;
    double next_think;
    float max_health, delay, wait, speed, damage, count, alpha, scale;
    uint32_t spawnflags, effects;
    /* Source-only movement bits such as FL_ITEM. Common physics bits retain
     * their existing authority in physics.flags. CTF actors use their unions. */
    uint32_t source_movement_flags;
    int32_t frame, skin;
    bool active, native, aimed_damage, consumed_corpse, axe_hit, touch_disabled;
    struct {
        q1_think_kind think;
        double next_think;
        bool active, damageable;
    } frozen;
    union {
        q1_monster monster;
        q1_projectile projectile;
        q1_pickup pickup;
        q1_timed_effect effect;
        q1_boss_child boss_child;
        qa_string_id rogue_fields[QA_Q1_ROGUE_FIELDS];
        struct { qa_string_id frags, message_time; } source_tag;
        qa_string_id rogue_rune;
        struct {
            qa_string_id words[3]; /* team, cnt, super_time */
            qa_vec3 origin, angles;
            bool placed;
        } rogue_flag;
        struct {
            qa_vec3 base, angles;
            qa_string_id return_word;
            uint32_t movement_flags;
            bool placed;
        } source_flag;
        struct {
            qa_string_id rune;
            uint32_t movement_flags;
        } source_rune;
    } state;
} q1_actor;
typedef struct q1_character {
    qa_q1_character_input input;
    qa_q1_character_pose pose;
    qa_q1_life life;
    uint64_t birth_epoch;
    qa_string_id model;
    int32_t frame;
    qa_q1_frame_range animation;
    uint16_t animation_frame, walk_frame;
    uint8_t locomotion;
    bool death_animation, attack_animation, in_water, weapon_hidden;
    qa_vec3 view_offset;
    double next_animation, pain_until, air_until, hazard_at;
    float fall_speed, drown_damage;
} q1_character;
struct qa_q1_source_client_view;
typedef struct q1_source_info {qa_string_id key,value;} q1_source_info;
typedef struct q1_player {
    struct q1_player *allocation_next, *pool_next;
    qa_actor_id id;
    qa_q1_input input;
    qa_q1_weapon weapon;
    int32_t weapon_frame, animation_base, nail_side;
    float current_ammo;
    double attack_finished, next_weapon_frame, animation_at, lightning_sound_at;
    double hostile_until, mega_rot_at, air_finished, drown_at, hazard_at;
    double power_expires[QA_Q1_POWER_COUNT];
    uint64_t power_order[QA_Q1_POWER_COUNT], power_sequence;
    double power_flash[QA_Q1_POWER_COUNT], scuba_at, shield_until, shield_sound_at;
    uint64_t wetsuit_scaled_frame;
    uint16_t power_warned, power_lost;
    uint8_t wetsuit_scaled_level;
    qa_q1_auto_switch auto_switch;
    qa_q1_mg3_progress mg3_progress;
    qa_actor_id mg3_hammer_target;
    double mg3_hammer_until;
    double horde_axe_chain_until;
    uint32_t horde_axe_chain;
    int32_t mg3_hammer_body;
    bool mg3_infinite_ammo, mg3_hammer_glow;
    qa_actor_id killer;
    qa_actor_id hook;
    qa_q1_input grapple_input;
    bool grapple_release, grapple_pulling;
    struct {
        qa_actor_id animation;
        double attack_finished, release_time;
        int32_t frame;
        bool selected, available;
    } grapple_weapon;
    float max_health, drown_damage;
    qa_vec3 punch;
    uint32_t client_slot;
    bool active, continuous, primary_holstered, arsenal, character, source_client;
    qa_q1_game *inventory_game;
    qa_inventory_lease weapon_definitions;
    q1_source_info *source_info;
    size_t source_info_count;
    float source_frags,source_team;
    bool source_observer,source_no_target,source_god_mode;
    qa_actor_id source_spectator_goal,source_spectator_track;
    uint32_t source_spectator_goal_ordinal,source_spectator_track_slot;
    int32_t source_impulse;
    bool source_use,source_death_recorded;
    bool finale_held_present, finale_held;
    double source_respawn_requested_at;
    q1_character character_state;
} q1_player;
typedef struct q1_rogue_rune_player {
    struct q1_rogue_rune_player *next;
    qa_actor_id actor;
    uint32_t rune;
    double notice, earth_noise, black_noise, hell_noise, regeneration;
} q1_rogue_rune_player;
struct qa_q1_game {
    qa_builtin_services services;
    qa_q1_options options;
    qa_q1_host host;
    qa_q1_source_flags_services source_flags;
    qa_q1_source_runes_services source_runes;
    qa_q1_source_rogue_tag_services source_rogue_tag;
    qa_q1_source_rogue_flags_services source_rogue_flags;
    q1_rogue_rune_player *rogue_rune_players;
    void *source_client_context;
    bool (*source_client_publish)(void *,const struct qa_q1_source_client_view *,qa_error *);
    bool (*source_client_observer)(void *,qa_actor_id,bool,qa_error *);
    double source_captures[2];
    q1_map_runtime *maps;
    struct q1_wire_state *wire;
    q1_actor **actors, *allocated_actors, *spare_actors, *retired_actors;
    q1_player **players, *allocated_players, *spare_players, *retired_players;
    uint32_t capacity, total_monsters, killed_monsters, hellknight_melee;
    uint32_t authored_gremlins, spawned_gremlins;
    qa_builtin_random random;
    qa_actor_id sight_actor, horn_charmer, rogue_runes_world;
    double time, elapsed, sight_time;
    double finale_last_poll;
    bool finale_polled, finale_acknowledged;
    uint64_t time_ns, attack_sequence;
    uint32_t force_retouch;
    uint32_t check_client_slot;
    double check_client_time;
    int32_t check_client_cluster;
    qa_vec3 forward, right, up;
    qa_actor_id qw_multi_entity;
    float qw_multi_damage, qw_blood_count, qw_puff_count;
    float qw_rj;
    qa_vec3 qw_blood_origin, qw_puff_origin;
    qa_item_id weapons[QA_Q1_WEAPON_COUNT], ammo[QA_Q1_AMMO_COUNT];
    qa_string_id weapon_models[QA_Q1_WEAPON_COUNT];
    qa_string_id hammer_glow_model, blood_shotgun_model, blood_super_shotgun_model;
    qa_string_id player_model, eyes_model, player_head_model;
    qa_item_id vengeance_item;
    qa_supply *source_supply;
    qa_builtin_snapshot_frame *snapshots;
    size_t observation_depth;
    size_t retention_depth;
    bool destroy_pending;
    bool continuation_pending;
    bool run_straight;
    bool rogue_runes_started;
    bool component_admitted;
    uint8_t rune_knight_melee, enemy_range;
    bool enemy_visible;
};
void q1_source_client_clear(q1_player *);
bool q1_source_number_read(qa_bytes, double *, qa_error *);
bool q1_source_flag_spawn(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_source_flag_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_source_flag_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_source_runes_start(qa_q1_game *, qa_error *);
bool q1_source_rune_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_source_rune_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_source_rogue_tag_spawn(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_source_rogue_tag_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_source_rogue_tag_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_source_rogue_flag_spawn(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_source_rogue_flag_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_source_rogue_flag_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_source_rogue_rune_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_source_rogue_rune_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
void q1_source_rogue_runes_release(qa_q1_game *, qa_actor_id);
void q1_source_rogue_runes_free(qa_q1_game *);
static inline void *q1_cvar_context(const qa_q1_game *game) {
    return game->services.cvar_context ? game->services.cvar_context : game->services.context;
}

extern const q1_frame q1_frames[];
bool q1_map_spawn(qa_q1_game *, q1_actor *, const qa_q1_spawn *, bool *, qa_error *);
bool q1_map_use(qa_q1_game *, q1_actor *, qa_actor_id other, qa_actor_id activator, qa_error *);
bool q1_map_touch(qa_q1_game *, q1_actor *, const qa_touch_contact *, qa_error *);
bool q1_map_blocked(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_map_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_reaction(qa_q1_game *, q1_actor *, const qa_damage_outcome *, qa_error *);
bool q1_map_radius_only(const qa_q1_game *, qa_actor_id);
bool q1_map_clone(qa_q1_game *, const q1_actor *, q1_actor *, qa_error *);
void q1_map_actor_released(qa_q1_game *, q1_actor *);
void q1_map_rotation_released(qa_q1_game *, qa_actor_id);
void q1_map_addon_released(qa_q1_game *, qa_actor_id);
void q1_map_addon_clone(qa_q1_game *, qa_actor_id, qa_actor_id);
void q1_map_destroy(qa_q1_game *);
void q1_map_frame_begin(qa_q1_game *);
bool q1_map_addon_frame(qa_q1_game *, qa_error *);
bool q1_map_level_frame(qa_q1_game *, const qa_source_frame *, qa_error *);
bool q1_map_ctf_frame(qa_q1_game *, qa_error *);
bool q1_map_collision(const q1_actor *, qa_actor_collision *);
bool q1_map_bind_target(qa_q1_game *, q1_actor *, qa_error *);
bool q1_spawn_template(qa_q1_game *, const qa_q1_spawn *, const qa_body_state *, qa_actor_id *,
                       qa_error *);
bool q1_map_spawn_template_wait(qa_q1_game *, q1_actor *, qa_error *);
bool q1_map_multi_explosion_begin(qa_q1_game *, q1_actor *, qa_error *);
bool q1_mg3_hammer_fire(qa_q1_game *, q1_player *, qa_error *);
bool q1_mg3_hammer_strike(qa_q1_game *, q1_actor *, qa_error *);
bool q1_mg3_weapon_frame(qa_q1_game *, q1_player *, qa_error *);
bool q1_mg3_capacities(qa_q1_game *, q1_player *, qa_error *);
bool q1_mg3_upgrade(qa_q1_game *, q1_player *, unsigned type, uint32_t flag, bool *collected,
                    float *maximum, qa_error *);
bool q1_mg3_impulse(qa_q1_game *, q1_player *, uint8_t, bool *, qa_error *);
bool q1_mg3_debug_upgrade(qa_q1_game *, qa_actor_id, unsigned, uint32_t, qa_error *);
bool q1_addon_target(qa_q1_game *, q1_actor *, qa_actor_id *, qa_error *);
bool q1_addon_contents(qa_q1_game *, q1_actor *, bool *, qa_error *);
unsigned q1_mg3_range(const q1_actor *, float distance);
bool q1_addon_move(qa_q1_game *, q1_actor *, float distance, bool seen, qa_error *);
bool q1_rocket_ogre_frame(qa_q1_game *, q1_actor *, const char *, qa_error *);
bool q1_rocket_ogre_override(const char *);
bool q1_rocket_ogre_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_sprite_explosion(qa_q1_game *, q1_actor *, qa_error *);
bool q1_sprite_prepare(qa_q1_game *, q1_actor *, qa_error *);
bool q1_hipnotic_hammer_base(qa_q1_game *, q1_player *, qa_vec3, qa_q1_weapon, qa_error *);
extern const q1_frame_operation q1_frame_operations[];
extern const size_t q1_frame_count;
uint16_t q1_frame_index(const char *);
uint16_t q1_infected_frame(uint16_t);
const q1_species *q1_infected_form(qa_q1_species, unsigned corpse);
bool q1_infected_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_infected_die(qa_q1_game *, q1_actor *, qa_error *);
bool q1_monster_count_kill(qa_q1_game *, q1_actor *, qa_actor_id killer, qa_error *);
bool q1_monster_death_report(qa_q1_game *, q1_actor *, qa_actor_id killer, bool count, qa_error *);
bool q1_demodog_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_demodog_die(qa_q1_game *, q1_actor *, qa_error *);
bool q1_demodog_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_demodog_grenade_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_demodog_explode(qa_q1_game *, q1_actor *, qa_actor_id ignore, qa_error *);
bool q1_heavy_melee(qa_q1_game *, q1_actor *, qa_error *);
bool q1_heavy_check_attack(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_heavy_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_heavy_pain(qa_q1_game *, q1_actor *, qa_actor_id, float, qa_error *);
bool q1_heavy_die(qa_q1_game *, q1_actor *, qa_error *);
bool q1_heavy_spike_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
uint16_t q1_mg3_lavaman_frame(uint16_t);
bool q1_lavaman_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_boss_spawn(qa_q1_game *, q1_actor *, bool *handled, qa_error *);
bool q1_boss_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_boss_die(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_boss_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_orb_check_attack(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_orb_rock_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_shub_grenade_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_spawn_shub_zombie(qa_q1_game *, qa_actor_id *, qa_error *);
bool q1_spawn_homing_flame(qa_q1_game *, q1_actor *, qa_actor_id *, qa_error *);
bool q1_homing_flame_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_ghost_bubbles(qa_q1_game *, q1_actor *, qa_error *);
bool q1_boss_pain_lightning(qa_q1_game *, q1_actor *, qa_vec3, qa_error *);
uint16_t q1_shub_zombie_frame(uint16_t);
bool q1_horde_head_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_horde_axe_delay(qa_q1_game *, q1_player *, float *interval, qa_error *);
const q1_species *q1_species_find(const char *);
q1_actor *q1_entity(qa_q1_game *, qa_actor_id);
const q1_actor *q1_entity_const(const qa_q1_game *, qa_actor_id);
q1_player *q1_player_get(qa_q1_game *, qa_actor_id);
q1_player *q1_player_allocate(qa_q1_game *, qa_actor_id, qa_error *);
bool q1_inventory_bind(qa_q1_game *, q1_player *, qa_error *);
bool q1_inventory_register(qa_q1_game_operation *, qa_actor_id, qa_error *);
bool q1_inventory_attach(qa_q1_game_operation *, q1_player *, qa_error *);
void q1_inventory_close(qa_q1_game *, q1_player *);
bool q1_alive(qa_q1_game *, qa_actor_id);
float q1_random(qa_q1_game *);
float q1_health(qa_q1_game *, qa_actor_id);
bool q1_damageable(qa_q1_game *, qa_actor_id);
bool q1_target(qa_q1_game *, qa_actor_id, qa_q1_target *);
bool q1_classnamed(qa_q1_game *, qa_actor_id, const char *);
bool q1_model(qa_q1_game *, q1_actor *, const char *, qa_error *);
bool q1_sound(qa_q1_game *, qa_actor_id, const char *, int32_t channel, float attenuation,
              qa_error *);
bool q1_sound_resource(qa_q1_game *, qa_actor_id, qa_string_id, int32_t channel, float attenuation,
                       float volume, qa_error *);
bool q1_effect(qa_q1_game *, qa_builtin_event_kind, qa_actor_id, qa_vec3, float, int32_t,
               qa_error *);
bool q1_create(qa_q1_game *, const char *, q1_entity_kind, qa_actor_id owner, q1_actor **,
               qa_error *);
bool q1_remove(qa_q1_game *, q1_actor *, qa_error *);
bool q1_schedule(qa_q1_game *, q1_actor *, double, q1_think_kind, qa_error *);
bool q1_current_ammo_select(qa_q1_game *, q1_player *, qa_error *);
bool q1_local_time(const q1_actor *, double *, qa_error *);
bool q1_think_deadline(double, double, double *, qa_error *);
bool q1_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_link(qa_q1_game *, q1_actor *, qa_error *);
bool q1_trace(qa_q1_game *, qa_vec3, qa_vec3, qa_actor_id, bool monsters, qa_trace_result *,
              qa_error *);
qa_attack q1_attack(qa_q1_game *, qa_actor_id attacker, qa_actor_id inflictor, qa_q1_weapon);
bool q1_damage(qa_q1_game *, qa_actor_id target, qa_actor_id inflictor, qa_actor_id attacker, float,
               qa_q1_weapon, qa_error *);
bool q1_damage_typed(qa_q1_game *, qa_actor_id, qa_actor_id, qa_actor_id, float, qa_q1_weapon,
                     qa_q1_armor_effect, qa_string_id death_type, qa_error *);
bool q1_radius(qa_q1_game *, qa_actor_id inflictor, qa_actor_id attacker, float, qa_actor_id ignore,
               qa_q1_weapon, qa_error *);
bool q1_radius_typed(qa_q1_game *, qa_actor_id, qa_actor_id, float, qa_actor_id, qa_q1_weapon,
                     const char *, qa_error *);
bool q1_can_damage(qa_q1_game *, qa_actor_id target, qa_actor_id from, bool *, qa_error *);
double q1_ammo_count(qa_q1_game *, qa_actor_id, qa_q1_ammo);
int q1_weapon_declared_ammo(qa_q1_weapon);
bool q1_weapon_ui_available_read(qa_q1_game *, qa_actor_id, qa_q1_weapon, bool *, qa_error *);
bool q1_consume(qa_q1_game *, qa_actor_id, qa_q1_ammo, float, qa_error *);
bool q1_fire(qa_q1_game *, q1_player *, qa_error *);
bool q1_weapon_parameters(qa_q1_game *, qa_actor_id, qa_q1_weapon, qa_q1_weapon_parameters *,
                          qa_error *);
bool q1_weapon_attack_delay(qa_q1_game *, q1_player *, float *, qa_error *);
bool q1_aim(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3 *, qa_error *);
bool q1_weapon_event(qa_q1_game *, q1_player *, float, int32_t, qa_error *);
bool q1_expansion_fire(qa_q1_game *, q1_player *, qa_error *);
bool q1_expansion_touch(qa_q1_game *, q1_actor *, qa_actor_id, const qa_touch_contact *,
                        qa_error *);
bool q1_expansion_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_hipnotic_fire(qa_q1_game *, q1_player *, qa_error *);
bool q1_hipnotic_launch_laser(qa_q1_game *, qa_actor_id, qa_q1_weapon, qa_vec3, qa_vec3, bool light,
                              qa_error *);
bool q1_hipnotic_launch_proximity(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3, qa_error *);
bool q1_hipnotic_touch(qa_q1_game *, q1_actor *, qa_actor_id, const qa_touch_contact *, qa_error *);
bool q1_hipnotic_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_proximity_arm(qa_q1_game *, q1_actor *, double delay, qa_error *);
bool q1_rogue_fire(qa_q1_game *, q1_player *, qa_error *);
bool q1_rogue_launch_plasma(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3, q1_actor **, qa_error *);
bool q1_rogue_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_rogue_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_launch_behavior(qa_q1_game *, q1_actor *, qa_builtin_projectile_role, qa_error *);
bool q1_native_trajectory(qa_q1_game *, qa_actor_id);
bool q1_missile_velocity(qa_q1_game *, q1_actor *, qa_vec3, qa_error *);
qa_vec3 q1_grenade_launch_velocity(const qa_q1_weapon_view *,bool level,qa_vec3 aim,
    qa_vec3 forward,qa_vec3 right,qa_vec3 up,float side,float vertical);
bool q1_grenade_velocity(qa_q1_game *, q1_player *, qa_vec3 *, qa_error *);
bool q1_bullets(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3, unsigned, float, float, qa_q1_weapon,
                qa_error *);
enum {
    Q1_LIGHTNING_DAMAGE_FIRST = 1u,
    Q1_LIGHTNING_REMEMBER_ALL = 2u,
    Q1_LIGHTNING_PARTICLES = 4u,
    Q1_LIGHTNING_WETSUIT = 8u
};
bool q1_lightning_rays(qa_q1_game *, qa_actor_id attacker, qa_actor_id inflictor, qa_vec3 start,
                       qa_vec3 end, float damage, float blood, int32_t color, qa_vec3 direction,
                       uint32_t flags, qa_q1_weapon, const char *cause, qa_error *);
bool q1_electric_rays(qa_q1_game *, qa_actor_id attacker, qa_actor_id inflictor,
                      qa_actor_id ignore, qa_vec3 start, qa_vec3 end, float damage, float blood,
                      int32_t color, qa_vec3 direction, uint32_t flags, qa_q1_weapon,
                      const char *cause, qa_error *);
bool q1_hipnotic_lightning_claimed(const qa_q1_game *, qa_actor_id);
bool q1_axe_strike(qa_q1_game *, q1_actor *, qa_error *);
bool q1_projectile_spawn(qa_q1_game *, qa_actor_id, qa_q1_weapon, q1_projectile_kind, qa_vec3,
                         qa_vec3, q1_actor **, qa_error *);
bool q1_projectile_touch(qa_q1_game *, q1_actor *, qa_actor_id, const qa_touch_contact *,
                         qa_error *);
bool q1_projectile_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_explode(qa_q1_game *, q1_actor *, qa_actor_id direct, qa_error *);
bool q1_gib(qa_q1_game *, q1_actor *, const char *, bool head, qa_error *);
bool q1_gib_head(qa_q1_game *, q1_actor *, const char *, float damage, qa_error *);
bool q1_gib_at(qa_q1_game *, qa_actor_id owner, qa_vec3, float health, const char *, qa_error *);
bool q1_meat_spray(qa_q1_game *, q1_actor *, qa_vec3 origin, qa_vec3 velocity, qa_error *);
bool q1_monster_spawn(qa_q1_game *, q1_actor *, const q1_species *, qa_error *);
bool q1_monster_drop_floor(qa_q1_game *, q1_actor *, qa_error *);
bool q1_lavaman_awake(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_lavaman_attack(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_lavaman_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_lavaman_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_teleport_fog_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_teledeath_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_spawn_teledeath(qa_q1_game *, qa_vec3, qa_actor_id, double duration, bool retouch,
                        qa_actor_id *, qa_error *);
bool q1_monster_start(qa_q1_game *, q1_actor *, qa_error *);
bool q1_monster_play(qa_q1_game *, q1_actor *, const char *, qa_error *);
bool q1_monster_frame(qa_q1_game *, q1_actor *, qa_error *);
bool q1_monster_ai(qa_q1_game *, q1_actor *, q1_ai, float, qa_error *);
bool q1_monster_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_mission_monster_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_mission_monster_pain(qa_q1_game *, q1_actor *, float, qa_error *);
bool q1_mission_monster_die(qa_q1_game *, q1_actor *, qa_error *);
bool q1_scourge_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_scourge_trigger(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_wrath_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_wrath_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_wrath_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_wrath_launch(qa_q1_game *, q1_actor *, unsigned attack, qa_error *);
bool q1_overlord_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_overlord_melee(qa_q1_game *, q1_actor *, qa_error *);
bool q1_overlord_destination(qa_q1_game *, qa_actor_id *, qa_error *);
bool q1_spawnpoint_empty(qa_q1_game *, qa_actor_id, qa_vec3);
bool q1_morph_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_morph_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_morph_melee(qa_q1_game *, q1_actor *, qa_error *);
bool q1_morph_pain(qa_q1_game *, q1_actor *, qa_error *);
bool q1_dragon_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_dragon_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_dragon_pain(qa_q1_game *, q1_actor *, qa_error *);
bool q1_dragon_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_dragon_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_dragon_corner_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_dragon_launch_fireball(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3, qa_error *);
bool q1_dragon_fireball_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_armagon_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_armagon_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_armagon_attack(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_armagon_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_multi_explosion_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_radius_snapshot(qa_q1_game *, qa_vec3, float, qa_builtin_snapshot_frame **, qa_error *);
bool q1_snapshot_actors(qa_q1_game *, qa_builtin_snapshot_frame **, qa_error *);
bool q1_snapshot_players(qa_q1_game *, qa_builtin_snapshot_frame **, qa_error *);
bool q1_snapshot_targets(qa_q1_game *, qa_targets *, qa_string_id, qa_builtin_snapshot_frame **,
                         qa_error *);
bool q1_gremlin_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_gremlin_action(qa_q1_game *, q1_actor *, q1_frame_action, qa_error *);
bool q1_gremlin_pain(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_gremlin_die(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_gremlin_touch(qa_q1_game *, q1_actor *, qa_error *);
bool q1_gremlin_melee(qa_q1_game *, q1_actor *, qa_error *);
bool q1_gremlin_run(qa_q1_game *, q1_actor *, float, qa_error *);
bool q1_gremlin_walk(qa_q1_game *, q1_actor *, float, qa_error *);
bool q1_gremlin_find_victim(qa_q1_game *, q1_actor *, qa_actor_id *, qa_error *);
bool q1_gremlin_has_ammo(q1_actor *);
bool q1_gremlin_steal(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_gremlin_weapon_attack(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_gremlin_fire_nail(qa_q1_game *, q1_actor *, bool laser, qa_error *);
bool q1_gremlin_lightning(qa_q1_game *, q1_actor *, qa_error *);
bool q1_gremlin_backpack(qa_q1_game *, q1_actor *, qa_error *);
bool q1_weapon_impulse(qa_q1_game *, q1_player *, uint8_t, qa_error *);
bool q1_source_impulse(qa_q1_game *, qa_actor_id, uint8_t, bool *handled, qa_error *);
bool q1_source_select_weapon(qa_q1_game *, qa_actor_id, qa_q1_weapon, bool *, qa_error *);
bool q1_developer_message(qa_q1_game *, const char *, qa_error *);
bool q1_addon_omnicide(qa_q1_game *, qa_actor_id, qa_error *);
bool q1_monster_pain(qa_q1_game *, q1_actor *, qa_actor_id, float, qa_error *);
bool q1_monster_die(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_monster_use(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_monster_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_monster_face(qa_q1_game *, q1_actor *, qa_error *);
qa_actor_id q1_find_target(const qa_q1_game *, qa_string_id);
qa_actor_id q1_monster_route(const qa_q1_game *, const q1_actor *);
bool q1_monster_found(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_monster_find_target(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_charmed_find_target(qa_q1_game *, q1_actor *, bool *, qa_error *);
bool q1_charmed_hunt(qa_q1_game *, q1_actor *, bool flee, qa_error *);
bool q1_charmed_walk(qa_q1_game *, q1_actor *, float, qa_error *);
bool q1_monster_visible(qa_q1_game *, q1_actor *, qa_actor_id, bool *, qa_error *);
bool q1_monster_melee(qa_q1_game *, q1_actor *, float range, float scale, unsigned rolls,
                      bool visible, qa_error *);
bool q1_pickup_spawn(qa_q1_game *, q1_actor *, qa_error *);
bool q1_pickup_supply_create(qa_q1_game *, qa_error *);
bool q1_pickup_observe(qa_q1_game *, q1_actor *, qa_error *);
bool q1_pickup_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_pickup_use(qa_q1_game *, q1_actor *, qa_error *);
bool q1_pickup_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_drop_backpack(qa_q1_game *, q1_actor *, qa_q1_weapon, const float ammo[QA_Q1_AMMO_COUNT],
                      qa_error *);
bool q1_spawn_backpack(qa_q1_game *, qa_actor_id, qa_vec3, qa_q1_weapon, const float ammo[QA_Q1_AMMO_COUNT],
                       q1_actor **, qa_error *);
bool q1_backpack_definition(qa_q1_game *, q1_actor *, qa_error *);
qa_q1_weapon q1_best_weapon(qa_q1_game *, q1_player *);
int q1_weapon_ammo(qa_q1_weapon);
float q1_weapon_interval(qa_q1_weapon);
const qa_q1_weapon_view *q1_weapon_shape(qa_q1_weapon);
bool q1_horde_axe_interval(qa_q1_game *, q1_player *, float *, bool *, qa_error *);
bool q1_character_bubbles(qa_q1_game *, q1_actor *, qa_error *);
bool q1_bubble_think(qa_q1_game *, q1_actor *, qa_error *);
bool q1_environment_damage(qa_q1_game *, qa_actor_id, float, qa_hazard, qa_error *);
bool q1_power_frame(qa_q1_game *, q1_player *, double seconds, uint64_t frame_ns, qa_error *);
bool q1_power_assign(qa_q1_game *, qa_actor_id, qa_q1_power, double, bool present, qa_error *);
bool q1_power_give(qa_q1_game *, qa_actor_id, qa_q1_power, double duration, qa_error *);
bool q1_powers_expire(qa_q1_game *, qa_actor_id, double seconds, qa_error *);
void q1_powers_forget(q1_player *);
bool q1_grapple_frame(qa_q1_game *, q1_player *, qa_error *);
bool q1_grapple_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_grapple_weapon_launch(qa_q1_game *, q1_actor *, qa_error *);
bool q1_grapple_weapon_frame(qa_q1_game *, q1_player *, qa_error *);
bool q1_grapple_touch(qa_q1_game *, q1_actor *, qa_actor_id, const qa_touch_contact *, qa_error *);
void q1_grapple_released(qa_q1_game *, qa_actor_id);
bool q1_power_think(qa_q1_game *, q1_actor *, q1_think_kind, qa_error *);
bool q1_sphere_pickup(qa_q1_game *, q1_actor *, qa_actor_id, bool *, qa_error *);
bool q1_sphere_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_message(qa_q1_game *, qa_actor_id, const char *, qa_error *);
bool q1_spawn_bubble(qa_q1_game *, qa_vec3 origin, qa_vec3 velocity, bool split, qa_error *);
bool q1_message_args(qa_q1_game *, qa_actor_id, const char *, const qa_builtin_message_arg *,
                     size_t, qa_error *);
bool q1_toss_backpack(qa_q1_game *, qa_actor_id, qa_vec3, qa_vec3, const float[QA_Q1_AMMO_COUNT],
                      q1_actor **, qa_error *);
bool q1_drop_touch(qa_q1_game *, q1_actor *, qa_actor_id, qa_error *);
bool q1_rogue_toss(qa_q1_game *, q1_player *, bool weapon, qa_error *);
int q1_weapon_rank(const qa_q1_game *, qa_q1_weapon);
bool q1_enable_combos(qa_q1_game *, q1_player *, qa_error *);
bool q1_enable_combos_read(qa_q1_game *, qa_actor_id, q1_player *, qa_error *);
qa_q1_weapon q1_combo_weapon(qa_q1_game *, q1_player *, qa_q1_weapon);
qa_q1_weapon q1_best_weapon_before(qa_q1_game *, q1_player *, const qa_pickup_receipt *, size_t);
bool q1_best_weapon_before_read(qa_q1_game *, qa_actor_id, q1_player *,
    const qa_pickup_receipt *, size_t, qa_q1_weapon *, qa_error *);
bool q1_player_select_read(qa_q1_game *, qa_actor_id, q1_player *, qa_q1_weapon, qa_error *);

#endif
