#ifndef QA_GAME_Q1_H
#define QA_GAME_Q1_H

#include "qa/builtin.h"
#include "qa/movement.h"
#include "qa/targets.h"

typedef struct qa_q1_game qa_q1_game;
struct qa_q1_map_fields;
typedef enum qa_q1_program {
    QA_Q1_ID1,
    QA_Q1_HIPNOTIC,
    QA_Q1_ROGUE,
    QA_Q1_DOPA,
    QA_Q1_MG1,
    QA_Q1_MG3,
    QA_Q1_CTF
} qa_q1_program;
typedef enum qa_q1_auto_switch {
    QA_Q1_SWITCH_ALWAYS,
    QA_Q1_SWITCH_NEW,
    QA_Q1_SWITCH_NEVER
} qa_q1_auto_switch;
typedef enum qa_q1_weapon {
    QA_Q1_AXE,
    QA_Q1_SHOTGUN,
    QA_Q1_SUPER_SHOTGUN,
    QA_Q1_NAILGUN,
    QA_Q1_SUPER_NAILGUN,
    QA_Q1_GRENADE,
    QA_Q1_ROCKET,
    QA_Q1_LIGHTNING,
    QA_Q1_LASER,
    QA_Q1_MJOLNIR,
    QA_Q1_PROXIMITY,
    QA_Q1_LAVA_NAILGUN,
    QA_Q1_LAVA_SUPER_NAILGUN,
    QA_Q1_MULTI_GRENADE,
    QA_Q1_MULTI_ROCKET,
    QA_Q1_PLASMA,
    QA_Q1_ROGUE_GRAPPLE,
    QA_Q1_MG3_LASER,
    QA_Q1_MG3_MJOLNIR,
    QA_Q1_CTF_GRAPPLE,
    QA_Q1_WEAPON_COUNT
} qa_q1_weapon;
typedef enum qa_q1_ammo {
    QA_Q1_SHELLS,
    QA_Q1_NAILS,
    QA_Q1_ROCKETS,
    QA_Q1_CELLS,
    QA_Q1_LAVA_NAILS,
    QA_Q1_MULTI_ROCKETS,
    QA_Q1_PLASMA_CELLS,
    QA_Q1_AMMO_COUNT
} qa_q1_ammo;
typedef enum qa_q1_power {
    QA_Q1_QUAD,
    QA_Q1_INVULNERABILITY,
    QA_Q1_INVISIBILITY,
    QA_Q1_SUIT,
    QA_Q1_WETSUIT,
    QA_Q1_EMPATHY,
    QA_Q1_SHIELD,
    QA_Q1_ANTIGRAV,
    QA_Q1_LAVA_SUIT,
    QA_Q1_POWER_COUNT
} qa_q1_power;
typedef enum qa_q1_species {
    QA_Q1_ARMY,
    QA_Q1_DOG,
    QA_Q1_KNIGHT,
    QA_Q1_ENFORCER,
    QA_Q1_DEMON,
    QA_Q1_OGRE,
    QA_Q1_HELLKNIGHT,
    QA_Q1_SHAMBLER,
    QA_Q1_WIZARD,
    QA_Q1_SHALRATH,
    QA_Q1_TARBABY,
    QA_Q1_FISH,
    QA_Q1_ZOMBIE,
    QA_Q1_BOSS,
    QA_Q1_OLDONE,
    QA_Q1_GREMLIN,
    QA_Q1_SCOURGE,
    QA_Q1_ARMAGON,
    QA_Q1_SPIKEMINE,
    QA_Q1_DECOY,
    QA_Q1_EEL,
    QA_Q1_SWORD,
    QA_Q1_WRATH,
    QA_Q1_SUPER_WRATH,
    QA_Q1_MUMMY,
    QA_Q1_LAVA_MAN,
    QA_Q1_MORPH,
    QA_Q1_DRAGON,
    QA_Q1_SPECIES_COUNT
} qa_q1_species;

typedef struct qa_q1_target {
    bool player, invisible, notarget, aimed_damage;
    float view_height;
    double hostile_until;
} qa_q1_target;
typedef struct qa_q1_options {
    qa_actor_owner provider, combat_provider, movement_provider, inventory_provider;
    qa_q1_program program;
    qa_q1_edition edition;
    bool quakeworld, coop;
    uint8_t skill;
    int32_t deathmatch, teamplay, world_type;
    float gravity, aim_threshold;
    uint32_t max_clients, random_seed;
} qa_q1_options;
typedef enum qa_q1_life { QA_Q1_ALIVE, QA_Q1_DYING, QA_Q1_DEAD, QA_Q1_RESPAWNABLE } qa_q1_life;
typedef enum qa_q1_character_attack {
    QA_Q1_CHARACTER_AXE,
    QA_Q1_CHARACTER_SHOTGUN,
    QA_Q1_CHARACTER_ROCKET,
    QA_Q1_CHARACTER_NAIL,
    QA_Q1_CHARACTER_LIGHTNING
} qa_q1_character_attack;
typedef struct qa_q1_frame_range {
    uint16_t first, count;
} qa_q1_frame_range;
typedef struct qa_q1_character_pose {
    bool custom_model, source_frame, axe_pose;
    qa_string_id model;
    int32_t frame;
    qa_q1_frame_range stand, run, pain, death;
} qa_q1_character_pose;
typedef struct qa_q1_character_input {
    bool axe_pose, attack, jump, use, invisible, invulnerable;
    uint8_t water_level;
    int32_t water_type;
} qa_q1_character_input;
typedef struct qa_q1_character_view {
    qa_string_id model;
    int32_t frame;
    qa_vec3 view_offset;
    qa_q1_life life;
    qa_physics_solid solid;
    qa_physics_motion motion;
    bool weapon_visible;
} qa_q1_character_view;
typedef struct qa_q1_weapon_parameters {
    float interval, nail_speed;
} qa_q1_weapon_parameters;
typedef enum qa_q1_cheat_grant { QA_Q1_CHEAT_WEAPONS, QA_Q1_CHEAT_AMMO } qa_q1_cheat_grant;
typedef enum qa_q1_path_result {
    QA_Q1_PATH_ERROR,
    QA_Q1_PATH_REACHED_GOAL,
    QA_Q1_PATH_REACHED_END,
    QA_Q1_PATH_BLOCKED,
    QA_Q1_PATH_IN_PROGRESS
} qa_q1_path_result;
typedef struct qa_q1_host {
    void *context;
    bool (*target)(void *, qa_actor_id, qa_q1_target *);
    bool (*check_client)(void *, qa_actor_id observer, qa_actor_id *);
    bool (*find_target)(void *, qa_string_id targetname, qa_actor_id *);
    bool (*find_targets)(void *, qa_string_id, qa_actor_id *, size_t capacity, size_t *count,
                         qa_error *);
    qa_actor_owner (*combat_provider)(void *, qa_actor_id);
    qa_supply *(*supply)(void *, qa_actor_id recipient);
    bool (*count_monster_kill)(void *, qa_actor_id);
    bool (*monster_killed)(void *, qa_actor_id, qa_actor_id killer, bool count_kill, qa_error *);
    bool (*monster_found)(void *, qa_actor_id, qa_actor_id enemy, qa_error *);
    bool (*finale)(void *, qa_actor_id, bool finish, qa_error *);
    bool (*set_gravity)(void *, qa_actor_id, float scale, qa_error *);
    bool (*character_pose)(void *, qa_actor_id, qa_q1_character_pose *);
    bool (*drop_inventory)(void *, qa_actor_id, qa_error *);
    bool (*request_respawn)(void *, qa_actor_id, qa_error *);
    bool (*fall_damage_allowed)(void *, qa_actor_id);
    bool (*grapple_allowed)(void *, qa_actor_id owner, qa_actor_id target, bool pulse);
    bool (*force_retouch)(void *, uint32_t source_frames, qa_error *);
    bool (*weapon_parameters)(void *, qa_actor_id, qa_q1_weapon, qa_q1_weapon_parameters *,
                              qa_error *);
    bool (*cheat_arsenal)(void *, qa_actor_id, qa_q1_cheat_grant, bool *handled, qa_error *);
    bool (*horde)(void *);
    bool (*monster_path)(void *, qa_actor_id, qa_vec3 goal, float distance, qa_q1_path_result *,
                         qa_error *);
    bool (*weapon_changed)(void *, qa_actor_id, qa_item_id acquired, qa_error *);
} qa_q1_host;
typedef struct qa_q1_boss_fields {
    const char *wave1, *wave2, *wave3, *teleport_target;
} qa_q1_boss_fields;
typedef struct qa_q1_spawn {
    const char *classname, *target, *targetname, *killtarget, *message;
    qa_vec3 origin, angles;
    uint32_t spawnflags, source_slot, upgrade_flag;
    bool has_source;
    float health, speed, wait, delay, damage, count;
    const struct qa_q1_map_fields *map_fields;
    const qa_q1_boss_fields *boss_fields;
} qa_q1_spawn;
typedef struct qa_q1_input {
    qa_vec3 view_angles;
    bool attack, jump, use, holstered;
    uint8_t impulse;
    uint8_t water_level;
    int32_t water_type;
    float teleport_until;
} qa_q1_input;
typedef struct qa_q1_drop_input {
    qa_item_id selected_weapon, selected_ammo;
    qa_vec3 view_angles;
} qa_q1_drop_input;
bool qa_q1_ctf_toss_ammo(qa_q1_game *, qa_actor_id, const qa_q1_drop_input *, qa_actor_id *dropped,
                         qa_error *);
bool qa_q1_ctf_toss_weapon(qa_q1_game *, qa_actor_id, const qa_q1_drop_input *,
                           qa_actor_id *dropped, qa_error *);
bool qa_q1_drop_backpack(qa_q1_game *, qa_actor_id, qa_item_id selected_weapon,
                         qa_actor_id *dropped, qa_error *);
typedef struct qa_q1_presentation {
    qa_actor_id actor;
    qa_string_id classname, model, targetname;
    int32_t frame, skin;
    uint32_t effects;
    float alpha, scale;
} qa_q1_presentation;
typedef struct qa_q1_player_view {
    qa_q1_weapon weapon;
    int32_t weapon_frame;
    qa_vec3 punch_angles;
    float max_health;
    double power_expires[QA_Q1_POWER_COUNT];
    bool holstered;
} qa_q1_player_view;
typedef struct qa_q1_mg3_progress {
    uint32_t health, shells, nails, rockets, cells, bloody;
} qa_q1_mg3_progress;
typedef struct qa_q1_obituary_actor {
    qa_actor_id actor;
    qa_string_id name, classname, kill_string;
    qa_team_id team;
    qa_q1_weapon weapon;
    float health;
    int32_t water_type;
    uint8_t water_level;
    double quad_expires, invulnerable_expires;
    bool player, monster, brush;
} qa_q1_obituary_actor;
typedef struct qa_q1_obituary_input {
    qa_q1_obituary_actor victim;
    const qa_q1_obituary_actor *attacker, *telefrag_owner;
    qa_string_id death_type;
    qa_string_id inflictor_classname, attacker_death_type;
    qa_team_id victim_saved_team;
    uint32_t gamecfg;
    int32_t teamplay;
    void *tag_context;
    bool (*tag_score)(void *, qa_actor_id victim, qa_actor_id attacker, int32_t *, qa_error *);
} qa_q1_obituary_input;
typedef struct qa_q1_obituary_result {
    qa_string_id text, achievement;
    qa_builtin_message_arg arguments[2];
    size_t argument_count;
    qa_actor_id credited_actor, achievement_actor;
    int32_t score_delta;
} qa_q1_obituary_result;
typedef enum qa_q1_client_notice {
    QA_Q1_CLIENT_CONNECT,
    QA_Q1_CLIENT_DISCONNECT,
    QA_Q1_CLIENT_SUICIDE,
    QA_Q1_CLIENT_EXIT
} qa_q1_client_notice;
/* Decisions only: the selected mode commits score and the application publishes
 * feedback once, before the native character reaction and item drops. */
bool qa_q1_obituary(qa_q1_game *, const qa_q1_obituary_input *, qa_q1_obituary_result *,
                    qa_error *);
bool qa_q1_client_notice_result(qa_q1_game *, qa_actor_id, qa_string_id name, qa_q1_client_notice,
                                int32_t frags, qa_q1_obituary_result *, qa_error *);
bool qa_q1_mg3_progress_read(const qa_q1_game *, qa_actor_id, qa_q1_mg3_progress *);
bool qa_q1_mg3_progress_restore(qa_q1_game *, qa_actor_id, const qa_q1_mg3_progress *, qa_error *);
bool qa_q1_mg3_hammer_body_frame(const qa_q1_game *, qa_actor_id, int32_t *);

/* Creation prepares private provider state and interns resource identities.
 * The application admits the borrowed component and combat policy together,
 * then removes both and forwards actor releases before destroying the game. */
bool qa_q1_game_create(const qa_builtin_services *, const qa_q1_options *, const qa_q1_host *,
                       qa_q1_game **, qa_error *);
void qa_q1_game_destroy(qa_q1_game *);
bool qa_q1_game_component(qa_q1_game *, qa_component *, qa_error *);
bool qa_q1_game_combat_policy(qa_q1_game *, qa_combat_policy *, qa_error *);
bool qa_q1_game_spawn(qa_q1_game *, const qa_q1_spawn *, qa_actor_id *, qa_error *);
bool qa_q1_game_clone(qa_q1_game *, qa_actor_id source, qa_actor_id *, qa_error *);
bool qa_q1_game_use_from(qa_q1_game *, qa_actor_id, qa_actor_id other, qa_actor_id activator,
                         qa_error *);
bool qa_q1_game_blocked(qa_q1_game *, qa_actor_id, qa_actor_id obstacle, qa_error *);
bool qa_q1_game_pusher_think(qa_q1_game *, qa_actor_id, const qa_source_frame *, qa_error *);
bool qa_q1_pickup_spawn_external(qa_q1_game *, const qa_q1_spawn *, const qa_body_state *,
                                 bool bounce, qa_actor_id *, qa_error *);
bool qa_q1_pickup_grant_external(qa_q1_game *, qa_actor_id item, qa_actor_id recipient,
                                 bool *accepted, qa_error *);
bool qa_q1_game_authored_target(const qa_q1_game *, qa_actor_id, qa_authored_target *);
bool qa_q1_spawn_teleport_fog(qa_q1_game *, qa_vec3 origin, qa_actor_id *, qa_error *);
bool qa_q1_spawn_teledeath(qa_q1_game *, qa_vec3 origin, qa_actor_id owner, qa_actor_id *,
                           qa_error *);
bool qa_q1_spawn_multi_explosion(qa_q1_game *, qa_vec3 origin, float radius, float damage,
                                 float duration, float pause, float volume, qa_actor_id *,
                                 qa_error *);
/* An arsenal can attach to any existing shared player. Character selection is
 * independent; attach never replaces body, health, armor or movement. */
bool qa_q1_player_attach(qa_q1_game *, qa_actor_id, bool initial_inventory, qa_error *);
bool qa_q1_player_input(qa_q1_game *, qa_actor_id, const qa_q1_input *, qa_error *);
bool qa_q1_player_select(qa_q1_game *, qa_actor_id, qa_q1_weapon, qa_error *);
bool qa_q1_player_read(const qa_q1_game *, qa_actor_id, qa_q1_player_view *);
bool qa_q1_player_power(qa_q1_game *, qa_actor_id, qa_q1_power, double expires, qa_error *);
bool qa_q1_player_auto_switch(qa_q1_game *, qa_actor_id, qa_q1_auto_switch, qa_error *);
bool qa_q1_game_damage_effect(qa_q1_game *, qa_damage_effect_stage, const qa_damage_request *,
                              qa_damage_effect *, qa_error *);
bool qa_q1_player_after_physics(qa_q1_game *, qa_actor_id, qa_error *);
qa_actor_id qa_q1_horn_charmer(const qa_q1_game *);
bool qa_q1_grapple_fire(qa_q1_game *, qa_actor_id, bool threewave, const qa_q1_input *, qa_error *);
bool qa_q1_grapple_input(qa_q1_game *, qa_actor_id, const qa_q1_input *, bool release, qa_error *);
bool qa_q1_grapple_release(qa_q1_game *, qa_actor_id, qa_error *);
typedef struct qa_q1_grapple_weapon_view {
    int32_t weapon_frame, character_frame;
    double attack_finished, release_time;
    bool selected, available, animating, pulling, axe_pose;
} qa_q1_grapple_weapon_view;
bool qa_q1_grapple_weapon_resume(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_grapple_weapon_holster(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_grapple_weapon_tick(qa_q1_game *, qa_actor_id, const qa_q1_input *, bool available,
                               qa_error *);
bool qa_q1_grapple_weapon_read(const qa_q1_game *, qa_actor_id, qa_q1_grapple_weapon_view *);
bool qa_q1_horde_spawn(qa_q1_game *, const char *classname, qa_vec3 origin, qa_vec3 angles,
                       qa_actor_id manager, qa_actor_id enemy, qa_actor_id *, qa_error *);
bool qa_q1_horde_after_death(qa_q1_game *, qa_actor_id, bool enabled, qa_error *);
bool qa_q1_horde_axe_chain(qa_q1_game *, qa_actor_id, uint32_t hits, double expires, qa_error *);
bool qa_q1_monster_charm(qa_q1_game *, qa_actor_id monster, qa_actor_id charmer, qa_error *);
bool qa_q1_grapple_pulling(const qa_q1_game *, qa_actor_id);
bool qa_q1_game_invulnerable(const qa_q1_game *, qa_actor_id);
double qa_q1_game_power_expires(const qa_q1_game *, qa_actor_id, qa_q1_power);
bool qa_q1_game_actor_traits(const qa_q1_game *, qa_actor_id, qa_builtin_actor_traits *);
bool qa_q1_player_prethink(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_player_postthink(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_player_environment(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_character_attach(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_character_frame(qa_q1_game *, qa_actor_id, const qa_q1_character_input *, qa_error *);
bool qa_q1_character_read(const qa_q1_game *, qa_actor_id, qa_q1_character_view *);
bool qa_q1_character_attack_frame(qa_q1_game *, qa_actor_id, qa_q1_character_attack,
                                  unsigned variant, qa_error *);
bool qa_q1_character_reaction(qa_q1_game *, const qa_damage_outcome *, qa_error *);
bool qa_q1_character_respawn(qa_q1_game *, qa_actor_id, const float *health, qa_error *);
bool qa_q1_character_suicide_pose(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_character_post_move(qa_q1_game *, qa_actor_id, qa_error *);
bool qa_q1_character_environment(qa_q1_game *, qa_actor_id, bool suit, bool noclip, qa_error *);
bool qa_q1_game_touch(qa_q1_game *, const qa_touch_contact *, qa_error *);
bool qa_q1_game_use(qa_q1_game *, qa_actor_id, qa_actor_id activator, qa_error *);
bool qa_q1_game_reaction(qa_q1_game *, const qa_damage_outcome *, qa_error *);
bool qa_q1_game_presentation(const qa_q1_game *, qa_actor_id, qa_q1_presentation *);
bool qa_q1_game_physics_read(const qa_q1_game *, qa_actor_id, qa_physics_properties *);
bool qa_q1_game_physics_write(qa_q1_game *, qa_actor_id, const qa_physics_properties *, qa_error *);
void qa_q1_game_actor_released(qa_q1_game *, qa_actor_record);
qa_item_id qa_q1_weapon_item(const qa_q1_game *, qa_q1_weapon);
qa_item_id qa_q1_ammo_item(const qa_q1_game *, qa_q1_ammo);

#endif
