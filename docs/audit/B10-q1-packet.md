# B10 frozen source acceptance evidence

Snapshot HEAD: 9b1dfaef2d781f1246e244e039b97c01bdc8be1d. Includes dirty source frozen for audit. This packet contains explicit source selections, not a truncated full-source dump. Read scope and findings are recorded in docs/audit/q1-shared.md.

Goal and criteria:
{
  "id": "B10",
  "label": "Q1 built-in gameplay",
  "depends_on": [
    "B08",
    "B09"
  ],
  "goal": "Implement Anthology Q1/QW base, expansions, rerelease, monsters, weapons, items, and source gameplay in native C.",
  "criteria": [
    "Full Q1 family roster and behavior are implemented through shared services, including mission-pack and rerelease distinctions."
  ],
  "status": "pending"
}

Reviewer assessment:
The full family behavior criterion is not fulfilled. Confirmed defects: Q1 context never sets has_momentum_direction while the Q1 policy gates impulse on it; STEP monsters also wrongly satisfy its current walk predicate (donor isPlayer only), direct requests omit knockback and normalization, radius requests omit knockback_scale and iterate the changing registry without donor's snapshot. Several required expansion/addon map classes are missing from native classification; the donor module list and native classification are shown. Q1 full private-state checkpoint API is absent; narrow MG3 restore is not one. This packet captures the pre-correction frozen source; root approved subsequent focused runtime corrections. Other Q1 controller branches remain under review and this is not a full roster qualification.

Snapshot selected SHA256 hashes:
6b1d901ba8b416faa382912b1b7296a276c7c4d9dbd8c1030e3960da4e5eaa5b  src/gameplay/q1/runtime.c
376d43df9d7812bd88873b2ecbe065902ee8bb24250f9e5bf37aa99a58d57d8d  src/gameplay/q1/queries.c
8c36feb2b149d7c98062f265351167d6c526a1f185a4813ecbb80ec1b73409b5  src/gameplay/q1/maps/runtime.c
eee0e959ad883ab18796a346de634b5d181333dbd3785688f365edaaa26dd17f  src/gameplay/policies.c
1a4be8c0b34459135d887f664dcb5ee5fe61f51d6793d132fb2f680032019517  src/gameplay/armor.c
33909f58875e89f0bfb43c1c851c26f0f3d87a086f169299dcc72777018d69cc  src/movement/common.c
c0b1c679af11906858a9e26211e7e3399cec31849fc29f1050c10f2af1272fc8  src/campaign/targets.c
1e48a455cf33dca65deefb3661ff1c8ae77c33f17b034302de64b63b97a874fa  src/campaign/q1/spawn.c
e5997203de73384986f6ffe9e3b6396651a6ba05cd09846cec5c8aee61c69f7f  src/main.c
915c4c50da8fcb60ed206f09f2d325eb7755f4c6f3f48b1c3d97d83d2eb98bae  docs/dependencies.json


## Source selection: cat include/qa/game_q1.h

```text
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

bool qa_q1_game_create(const qa_builtin_services *, const qa_q1_options *, const qa_q1_host *,
                       qa_q1_game **, qa_error *);
void qa_q1_game_destroy(qa_q1_game *);
bool qa_q1_game_component(qa_q1_game *, qa_component *, qa_error *);
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
```

## Source selection: sed -n '180,245p' src/gameplay/q1/runtime.c

```text
                              .code = code,
                              .time_ns = g->time_ns};
    return qa_builtin_emit(&g->services, &event, error);
}

static bool combat_context(void *context, const qa_damage_request *request,
                           const qa_combat_state *target, const qa_combat_state *attacker,
                           qa_combat_context *out, qa_error *error) {
    (void)attacker;
    (void)error;
    qa_q1_game *g = context;
    q1_player *player = q1_player_get(g, request->attack.attacker);
    qa_physics_properties physics;
    bool has_physics = g->services.physics && g->services.physics->services.read &&
                       g->services.physics->services.read(g->services.physics->services.context,
                                                          request->target, &physics);
    *out = (qa_combat_context){
        .armor.alive = target->health > 0,
        .game.q1 = {.quad = player && player->power_expires[QA_Q1_QUAD] > g->time,
                    .walk = has_physics && (physics.motion == QA_PHYSICS_STEP ||
                                            (physics.flags & QA_PHYSICS_PLAYER)),
                    .teamplay = g->options.teamplay}};
    return true;
}
static bool begin_frame(void *context, qa_session *session, const qa_source_frame *frame,
                        qa_error *error) {
    (void)session;
    (void)error;
    qa_q1_game *g = context;
    q1_map_frame_begin(g);
    while (g->retired_actors) {
        q1_actor *entity = g->retired_actors;
        g->retired_actors = entity->pool_next;
        entity->pool_next = g->spare_actors;
        g->spare_actors = entity;
    }
    while (g->retired_players) {
        q1_player *player = g->retired_players;
        g->retired_players = player->pool_next;
        player->pool_next = g->spare_players;
        g->spare_players = player;
    }
    g->time_ns = frame->time_ns;
    g->time = (double)frame->time_ns / 1000000000.0;
    g->elapsed = (double)frame->elapsed_ns / 1000000000.0;
    return true;
}
static bool actor_frame(void *context, qa_session *session, qa_actor_id actor,
                        const qa_source_frame *frame, qa_error *error) {
    (void)session;
    qa_q1_game *g = context;
    q1_actor *entity = q1_entity(g, actor);
    if (!entity || !entity->native || !g->services.physics)
        return true;
    if (entity->kind == Q1_PROJECTILE) {
        bool changed;
        if (!qa_builtin_step_projectile(&g->services, actor, frame->time_ns, &changed, error))
            return false;
        if (!q1_alive(g, actor))
            return true;
        if (changed) {
            if (entity->state.projectile.kind == Q1_HIP_LASER) {
                qa_body_state body;
                if (!qa_world_body_read(g->services.world, actor, &body, error))
                    return false;
                entity->state.projectile.movedir = body.velocity;
```

## Source selection: sed -n '565,705p' src/gameplay/q1/runtime.c

```text
    case Q1_THINK_HAMMER_BOLT:
    case Q1_THINK_MULTI_SPLIT:
    case Q1_THINK_MINI_EXPLODE:
    case Q1_THINK_MULTI_EXPLODE:
    case Q1_THINK_MULTI_ACQUIRE:
    case Q1_THINK_MULTI_HOME:
    case Q1_THINK_PLASMA_LAUNCH:
        return q1_expansion_think(g, entity, kind, error);
    }
    return false;
}
bool q1_trace(qa_q1_game *g, qa_vec3 start, qa_vec3 end, qa_actor_id pass, bool monsters,
              qa_trace_result *out, qa_error *error) {
    qa_trace_query query = {.start = start,
                            .end = end,
                            .shape.kind = QA_SHAPE_POINT,
                            .pass_actor = pass,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    query.policy.q1_move = monsters ? QA_Q1_MOVE_NORMAL : QA_Q1_MOVE_NO_MONSTERS;
    return qa_world_trace(g->services.world, &query, out, error);
}
qa_attack q1_attack(qa_q1_game *g, qa_actor_id attacker, qa_actor_id inflictor,
                    qa_q1_weapon weapon) {
    q1_actor *entity = q1_entity(g, inflictor);
    if (entity && entity->kind == Q1_PROJECTILE &&
        entity->state.projectile.attack.weapon_provider) {
        qa_attack attack = entity->state.projectile.attack;
        attack.attacker = attacker;
        attack.time_ns = g->time_ns;
        return attack;
    }
    return (qa_attack){.time_ns = g->time_ns,
                       .attacker = attacker,
                       .inflictor = inflictor,
                       .weapon = weapon < QA_Q1_WEAPON_COUNT ? g->weapons[weapon] : 0,
                       .weapon_provider = g->options.provider,
                       .combat_provider = g->options.combat_provider,
                       .inventory_provider = g->options.inventory_provider,
                       .movement_provider = g->options.movement_provider,
                       .cause = {.kind = QA_CAUSE_Q1}};
}
bool q1_damage(qa_q1_game *g, qa_actor_id target, qa_actor_id inflictor, qa_actor_id attacker,
               float amount, qa_q1_weapon weapon, qa_error *error) {
    qa_attack attack = q1_attack(g, attacker, inflictor, weapon);
    return q1_damage_typed(g, target, inflictor, attacker, amount, weapon, QA_Q1_ARMOR_NORMAL,
                           attack.cause.kind == QA_CAUSE_Q1 ? attack.cause.source.q1.death_type : 0,
                           error);
}
bool q1_damage_typed(qa_q1_game *g, qa_actor_id target, qa_actor_id inflictor, qa_actor_id attacker,
                     float amount, qa_q1_weapon weapon, qa_q1_armor_effect armor,
                     qa_string_id death_type, qa_error *error) {
    if (!q1_damageable(g, target))
        return true;
    qa_damage_request request = {
        .attack = q1_attack(g, attacker, inflictor, weapon), .target = target, .amount = amount};
    request.attack.cause = (qa_damage_cause){
        .kind = QA_CAUSE_Q1, .source.q1 = {.armor = armor, .death_type = death_type}};
    if (!qa_attack_next(&g->attack_sequence, &request.attack, error))
        return false;
    if (g->host.combat_provider)
        request.attack.combat_provider = g->host.combat_provider(g->host.context, target);
    qa_body_state from, to;
    if (qa_world_body_read(g->services.world, inflictor, &from, NULL) &&
        qa_world_body_read(g->services.world, target, &to, NULL)) {
        request.direction = qa_vec_sub(to.origin, from.origin);
        request.point = to.origin;
    }
    qa_damage_outcome outcome = {0};
    bool ok = qa_combat_apply(g->services.combat, &request, &outcome, error);
    qa_damage_outcome_free(&outcome);
    return ok;
}
static bool radius_adjust(void *context, qa_actor_id target, float *damage, float *knockback,
                          bool *allowed, qa_error *error) {
    (void)knockback;
    (void)allowed;
    (void)error;
    qa_q1_game *g = context;
    if (q1_classnamed(g, target, "monster_shambler"))
        *damage *= 0.5f;
    return true;
}
static bool radius_prepare(void *context, qa_damage_request *request, bool *allowed,
                           qa_error *error) {
    (void)allowed;
    qa_q1_game *g = context;
    if (g->host.combat_provider)
        request->attack.combat_provider = g->host.combat_provider(g->host.context, request->target);
    return qa_attack_next(&g->attack_sequence, &request->attack, error);
}
bool q1_radius(qa_q1_game *g, qa_actor_id inflictor, qa_actor_id attacker, float amount,
               qa_actor_id ignore, qa_q1_weapon weapon, qa_error *error) {
    return q1_radius_typed(g, inflictor, attacker, amount, ignore, weapon, NULL, error);
}
bool q1_radius_typed(qa_q1_game *g, qa_actor_id inflictor, qa_actor_id attacker, float amount,
                     qa_actor_id ignore, qa_q1_weapon weapon, const char *cause, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, inflictor, &body, error))
        return false;
    qa_builtin_radius radius = {.attack = q1_attack(g, attacker, inflictor, weapon),
                                .origin = body.origin,
                                .radius = amount + 40,
                                .damage = amount,
                                .distance_scale = 0.5f,
                                .self_scale = 0.5f,
                                .ignore = ignore,
                                .visibility_pass = inflictor,
                                .trace = qa_collision_default_policy(QA_COLLISION_Q1),
                                .check_visibility = true,
                                .context = g,
                                .adjust = radius_adjust,
                                .prepare = radius_prepare};
    radius.trace.q1_move = QA_Q1_MOVE_NO_MONSTERS;
    if (cause &&
        !qa_builtin_resource(&g->services, cause, &radius.attack.cause.source.q1.death_type, error))
        return false;
    return qa_builtin_radius_damage(&g->services, &radius, NULL, error);
}
bool q1_can_damage(qa_q1_game *g, qa_actor_id target, qa_actor_id from, bool *out,
                   qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, from, &body, error))
        return false;
    qa_trace_policy policy = qa_collision_default_policy(QA_COLLISION_Q1);
    policy.q1_move = QA_Q1_MOVE_NO_MONSTERS;
    return qa_builtin_can_damage(&g->services, body.origin, target, from, policy, false, out,
                                 error);
}

bool qa_q1_game_spawn(qa_q1_game *g, const qa_q1_spawn *spawn, qa_actor_id *out, qa_error *error) {
    if (!g || !spawn || !spawn->classname || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 native spawn");
        return false;
    }
    if (!strcmp(spawn->classname, "trigger_dragon")) {
        *out = (qa_actor_id){0};
        return true;
    }
    const q1_species *species = q1_species_find(spawn->classname);
    bool dormant_mine = !strcmp(spawn->classname, "monster_spikemine");
    if ((species || dormant_mine) && g->options.deathmatch) {
```

## Source selection: cat src/gameplay/q1/queries.c

```text
#include "internal.h"

static bool snapshot_acquire(qa_q1_game *g, bool players, q1_actor_snapshot **out, qa_error *error) {
    q1_actor_snapshot *snapshot = g->snapshots;
    while (snapshot && snapshot->borrowed)
        snapshot = snapshot->next;
    if (!snapshot) {
        snapshot = calloc(1, sizeof(*snapshot));
        if (!snapshot) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Q1 actor snapshot allocation failed");
            return false;
        }
        snapshot->next = g->snapshots;
        g->snapshots = snapshot;
    }
    if (!(players ? qa_builtin_players(&g->services, &snapshot->shared, error)
                  : qa_builtin_observations(&g->services, &snapshot->shared, error)))
        return false;
    snapshot->actors = snapshot->shared.ids;
    snapshot->count = snapshot->shared.count;
    snapshot->borrowed = true;
    *out = snapshot;
    return true;
}

bool q1_snapshot_actors(qa_q1_game *g, q1_actor_snapshot **out, qa_error *error) {
    return snapshot_acquire(g, false, out, error);
}
bool q1_snapshot_players(qa_q1_game *g, q1_actor_snapshot **out, qa_error *error) {
    return snapshot_acquire(g, true, out, error);
}

bool q1_radius_snapshot(qa_q1_game *g, qa_vec3 origin, float radius, q1_actor_snapshot **out,
                        qa_error *error) {
    q1_actor_snapshot *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    size_t count = 0;
    for (size_t i = 0; i < snapshot->count; ++i) {
        qa_actor_id actor = snapshot->actors[i];
        const q1_actor *native = q1_entity_const(g, actor);
        qa_body_state body;
        if ((native && native->physics.solid == QA_PHYSICS_NOT_SOLID) ||
            !qa_world_body_read(g->services.world, actor, &body, NULL))
            continue;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        if (qa_vec_length(qa_vec_sub(origin, center)) <= radius)
            snapshot->actors[count++] = actor;
    }
    snapshot->count = count;
    *out = snapshot;
    return true;
}
```

## Source selection: sed -n '115,245p' src/gameplay/builtin/attacks.c

```text
            return false;
        if (visible) {
            *out = true;
            return true;
        }
    }
    *out = false;
    return true;
}

static float distance_to_bounds(qa_vec3 point, qa_bounds bounds) {
    qa_vec3 delta = qa_v3(fmaxf(bounds.mins.x - point.x, fmaxf(0, point.x - bounds.maxs.x)),
                          fmaxf(bounds.mins.y - point.y, fmaxf(0, point.y - bounds.maxs.y)),
                          fmaxf(bounds.mins.z - point.z, fmaxf(0, point.z - bounds.maxs.z)));
    return qa_vec_length(delta);
}

bool qa_builtin_radius_damage(const qa_builtin_services *s, const qa_builtin_radius *radius,
                              size_t *damaged, qa_error *error) {
    if (!s || !radius || !qa_vec_finite(radius->origin) || !isfinite(radius->radius) ||
        radius->radius < 0 || !isfinite(radius->damage) || !isfinite(radius->distance_scale) ||
        !isfinite(radius->self_scale) || !isfinite(radius->knockback_scale) ||
        !isfinite(radius->direction_z_bias) ||
        (radius->has_candidates && radius->candidate_count && !radius->candidates) ||
        (radius->candidate_radius_only && !radius->has_candidates)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid native radius attack");
        return false;
    }
    size_t total = 0;
    uint32_t cursor = 0;
    const qa_actor_record *record;
    size_t candidate = 0;
    for (;;) {
        qa_actor_id actor;
        if (radius->has_candidates) {
            if (candidate == radius->candidate_count)
                break;
            actor = radius->candidates[candidate++];
            if (!qa_actors_get(qa_session_actors(s->session), actor))
                continue;
        } else {
            if (!qa_actors_next(qa_session_actors(s->session), &cursor, &record))
                break;
            actor = record->id;
        }
        if (qa_actor_id_equal(actor, radius->ignore))
            continue;
        bool can_damage;
        if (!damageable(s, actor, &can_damage, error))
            return false;
        if (!can_damage)
            continue;
        qa_body_state body;
        if (!qa_world_body_read(s->world, actor, &body, error))
            return false;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        float distance =
            radius->distance == QA_RADIUS_BOUNDS
                ? distance_to_bounds(radius->origin, qa_bounds_translate(body.bounds, body.origin))
                : qa_vec_length(qa_vec_sub(center, radius->origin));
        if (!radius->candidate_radius_only && distance > radius->radius)
            continue;
        float amount = radius->damage - radius->distance_scale * distance;
        if (qa_actor_id_equal(actor, radius->attack.attacker))
            amount *= radius->self_scale;
        float knockback = amount * radius->knockback_scale;
        bool allowed = amount > 0;
        if (radius->adjust &&
            !radius->adjust(radius->context, actor, &amount, &knockback, &allowed, error))
            return false;
        if (!allowed || !qa_actors_get(qa_session_actors(s->session), actor))
            continue;
        if (radius->check_visibility) {
            bool visible;
            if (!qa_builtin_can_damage(s, radius->origin, actor, radius->visibility_pass,
                                       radius->trace, radius->corner_visibility, &visible, error))
                return false;
            if (!visible)
                continue;
        }
        qa_damage_request request = {.attack = radius->attack,
                                     .target = actor,
                                     .amount = amount,
                                     .knockback = knockback,
                                     .direction = qa_vec_sub(body.origin, radius->origin),
                                     .point = radius->origin,
                                     .radius = true};
        request.direction.z += radius->direction_z_bias;
        if (radius->prepare && !radius->prepare(radius->context, &request, &allowed, error))
            return false;
        if (!allowed)
            continue;
        qa_damage_outcome outcome = {0};
        bool ok = qa_combat_apply(s->combat, &request, &outcome, error);
        if (ok && outcome.result.applied_damage > 0)
            ++total;
        if (ok && radius->after)
            ok = radius->after(radius->context, &outcome, error);
        qa_damage_outcome_free(&outcome);
        if (!ok)
            return false;
    }
    if (damaged)
        *damaged = total;
    return true;
}

bool qa_builtin_projectile_move(const qa_builtin_services *s,
                                const qa_builtin_projectile_step *step, qa_trace_result *out,
                                qa_error *error) {
    qa_body_state body;
    if (!s || !step || !qa_vec_finite(step->end)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid projectile movement");
        return false;
    }
    if (!qa_world_body_read(s->world, step->actor, &body, error))
        return false;
    qa_trace_query query = {
        .start = body.origin,
        .end = step->end,
        .shape = {.kind = step->point ? QA_SHAPE_POINT : QA_SHAPE_BOX, .bounds = body.bounds},
        .policy = step->trace,
        .pass_actor = step->actor};
    qa_trace_result trace;
    size_t excluded_count = step->owner.registry ? 1u : 0u;
    if (!qa_world_trace_excluding(s->world, &query, &step->owner, excluded_count, &trace, error))
        return false;
    body.origin = trace.end;
    if (!qa_world_body_write(s->world, step->actor, &body, error) ||
        !qa_world_link(s->world, step->actor, NULL, error))
```

## Source selection: sed -n '390,515p' ../quake-typescript/src/content/q1/foundation/entity-services.ts

```text
      .sort((a, b) => (this.host.actors.sourceOf(a.actor.id)?.slot ?? a.actor.id.slot) - (this.host.actors.sourceOf(b.actor.id)?.slot ?? b.actor.id.slot));
  }
  useTargets(entity: AuthoredTarget, activator: ActorId | null): undefined {
    if (entity.delay !== 0) {
      const delayed = this.create("DelayedUse"); delayed.target = entity.target; delayed.killtarget = entity.killtarget; delayed.message = entity.message;
      delayed.activator = activator;
      return this.schedule(delayed, entity.delay, this.named.action(delayed, "DelayThink"));
    }
    this.message(activator, entity.message);
    for (const victim of this.targetActors(entity.killtarget)) if (this.host.actors.isLive(victim.actor.id)) this.host.actors.release(victim.actor);
    // Each use executes synchronously; targets removed by a nested call are not invoked afterward.
    for (const target of this.targetActors(entity.target)) if (this.host.actors.isLive(target.actor.id)) this.host.callbacks.use(target.actor, entity.actor.id, activator);
    return undefined;
  }

  damage(target: ActorId, inflictor: ActorId, attacker: ActorId | null, amount: number, weapon: Q1Weapon | null = null, delivery: "direct" | "radius" = "direct", deathType = "", armorEffect?: "bypass" | "half-effectiveness"): DamageOutcome {
    const targetBody = this.host.bodies.read(target); const source = this.host.bodies.read(inflictor);
    const point = targetBody?.origin ?? ZERO;
    const direction = normalize(vsub(point, source?.origin ?? point));
    const scaled = Math.fround(Math.fround(amount) * Math.fround(attacker === null ? 1 : this.host.sourceDamageMultiplier?.(attacker) ?? 1));
    const request = applySourceDamageModifier({ target, amount: scaled, knockback: scaled, direction, point, normal: ZERO, delivery,
      attack: { sequence: this.sequence++, time: { kind: "seconds", value: this.time }, attacker, inflictor,
        ...(this.host.sourceDamagePowerupOwner === undefined ? {} : { damagePowerupOwner: this.host.sourceDamagePowerupOwner }),
        weapon: weapon === null ? null : this.weaponItem(weapon), weaponProvider: this.provider, combatProvider: this.options.combatProvider,
        inventoryProvider: this.options.inventoryProvider, movementProvider: this.options.movementProvider, cause: { kind: "q1", deathType, ...(armorEffect === undefined ? {} : { armorEffect }) } } }, this.host.sourceDamageModifier, actor => this.host.actors.isLive(actor));
    return this.host.combat.apply({ ...request, knockback: request.amount });
  }
  powerupExpires(actor: ActorId, powerup: Q1Powerup): number {
    return this.host.powerupExpires?.(actor, powerup) ?? this.player(actor)?.powerups.get(powerup) ?? 0;
  }
  combatContext(request: DamageRequest): Q1CombatContext {
    const projectile = request.attack.originatingProjectile === undefined ? null : this.host.bodies.read(request.attack.originatingProjectile);
    const inflictor = request.attack.inflictor === null || this.world?.actor.id.equals(request.attack.inflictor) === true
      ? null : this.host.bodies.linked(request.attack.inflictor);
    // A source may report world as the damage inflictor while retaining the actual projectile.
    const origin = projectile !== null ? vadd(projectile.origin, vscale(vadd(projectile.bounds.min, projectile.bounds.max), 0.5))
      : inflictor === null ? null : vscale(vadd(inflictor.absoluteBounds.min, inflictor.absoluteBounds.max), 0.5);
    const target = this.host.bodies.read(request.target);
    return { arithmetic: "binary32", quad: request.attack.damagePowerupOwner === undefined && request.attack.attacker !== null && this.powerupExpires(request.attack.attacker, "quad") > this.time,
      teamplay: this.options.teamplay ?? 0, baseTeamHealth: this.baseTeamHealth, walk: this.isPlayer(request.target), momentumDirection: target === null || origin === null ? null :
        normalize(vsub(target.origin, origin)) };
  }
  sourceTarget(actor: ActorId) {
    const observed = this.host.sourceTarget?.(actor);
    if (observed !== undefined) return observed;
    const entity = this.entity(actor);
    return { aimedDamage: entity?.aimedDamage ?? false, push: entity?.movement === "push", player: this.isPlayer(actor), slidebox: entity?.solid === "slidebox" };
  }
  monsterTarget(actor: ActorId) {
    if (this.host.monsterTarget !== undefined) return this.host.monsterTarget(actor);
    const entity = this.entity(actor), player = this.player(actor);
    if (entity === null && player === null) return null;
    return { viewHeight: player === null ? 25 : 22, notarget: ((entity?.movementFlags ?? 0) & 128) !== 0,
      invisible: (player?.powerups.get("invisibility") ?? 0) > this.time, lightLevel: null, hostileUntil: player?.hostileUntil ?? null };
  }
  canDamage(target: ActorId, inflictor: ActorId): boolean {
    const targetBody = this.host.bodies.read(target), source = this.host.bodies.read(inflictor); if (targetBody === null || source === null) return false;
    const push = this.sourceTarget(target).push;
    const destination = push ? vadd(targetBody.origin, vscale(vadd(targetBody.bounds.min, targetBody.bounds.max), 0.5)) : targetBody.origin;
    const offsets = push ? [ZERO] : [ZERO, { x: 15, y: 15, z: 0 }, { x: -15, y: -15, z: 0 }, { x: -15, y: 15, z: 0 }, { x: 15, y: -15, z: 0 }];
    return offsets.some(offset => { const trace = this.host.trace({ start: source.origin, end: vadd(destination, offset), bounds: POINT, ignore: inflictor, monsters: false }); return trace.fraction === 1 || trace.actor !== null && sameActor(trace.actor, target); });
  }
  radiusDamage(inflictor: ActorId, attacker: ActorId | null, amount: number, ignore: ActorId | null, weapon: Q1Weapon | null, deathType = ""): undefined {
    const source = this.host.bodies.read(inflictor); if (source === null) return undefined;
    for (const observation of this.host.actors.observations()) {
      const target = observation.id; const combat = this.host.combat.read(target), body = this.host.bodies.read(target);
      if (combat === null || !combat.canTakeDamage || body === null || ignore !== null && sameActor(ignore, target)) continue;
      const center = vadd(body.origin, vscale(vadd(body.bounds.min, body.bounds.max), 0.5));
      const distance = length(vsub(center, source.origin)); if (distance > amount + 40) continue;
      let points = Math.fround(amount - 0.5 * distance); if (attacker !== null && sameActor(target, attacker)) points *= 0.5;
      if (this.host.classname(target) === "monster_shambler") points *= 0.5;
      if (points > 0 && this.canDamage(target, inflictor)) this.damage(target, inflictor, attacker, points, weapon, "radius", deathType);
    }
    return undefined;
  }

  calcMove(entity: Q1Actor, destination: Vec3, speed: number, done: () => undefined): undefined {
    if (!(speed > 0)) throw new RangeError("Q1 mover speed must be positive");
    this.cancel(entity); const delta = vsub(destination, this.body(entity).origin);
    const travel = Math.fround(length(delta) / speed);
    entity.move = { destination, done };
    this.setBody(entity, { velocity: travel < 0.1 ? ZERO : vscale(delta, Math.fround(1 / travel)) });
    return this.scheduleAt(entity, Math.fround(entity.number("ltime") + Math.max(0.1, travel)), this.named.action(entity, "SUB_CalcMoveDone"));
  }

  /** Convenience for direct provider use. The unified session calls physicsEntity in source-slot order. */
  physicsStep(seconds: number, elapsedSeconds: number): undefined {
    for (const entity of [...this.entities.values()]) this.physicsEntity(entity.actor, seconds, elapsedSeconds);
    for (const player of this.players.values()) this.playerFrame(player.actor, seconds);
    return undefined;
  }
  launchProjectileBehavior(entity: Q1Actor, shooter: ActorId, weapon: Q1Weapon, role: ProjectileRole): void {
    if (!this.isPlayer(shooter)) return;
    const update = this.host.weaponBehavior?.launch({ projectile: entity.actor, shooter, weapon: this.weaponItem(weapon), role,
      timeSeconds: this.time, body: this.body(entity) });
    if (update !== undefined && update !== null) { this.named.projectTrajectory(entity, update); this.setBody(entity, update); this.link(entity); }
  }

  applyProjectileBehavior(actor: OwnedActor, seconds: number): void {
    const entity = this.entities.get(actor);
    if (entity === undefined || !this.live(entity)) return;
    const update = this.host.weaponBehavior?.step(actor, this.body(entity), seconds);
    if (update !== undefined && update !== null) { this.named.projectTrajectory(entity, update); this.setBody(entity, update); this.link(entity); }
  }

  physicsEntity(actor: OwnedActor, seconds: number, elapsedSeconds: number): undefined {
    this.time = seconds; this.frameSeconds = elapsedSeconds; const entity = this.entities.get(actor);
    if (entity === undefined || !this.live(entity)) return undefined;
    this.applyProjectileBehavior(actor, seconds);
    if (entity.movement === "push") {
      stepQ1Pusher({ actor: actor.id, elapsedSeconds, movement: "translate" }, this.host.pusherServices(this));
    } else if (entity.movement === "toss" || entity.movement === "bounce" || entity.movement === "gib" || entity.movement === "fly" || entity.movement === "flymissile") this.projectilePhysics(entity, elapsedSeconds);
    return undefined;
  }

  /** Weapon/powerup/environment state; jumping belongs to the independently selected movement provider. */
  playerFrame(actor: OwnedActor, seconds: number, waterLevel?: number): undefined {
    this.time = seconds; const player = this.players.get(actor);
    if (player === undefined) return undefined;
    if (waterLevel !== undefined) player.waterLevel = waterLevel;
    for (const extension of this.playerExtensions.values()) extension.frame?.(this, player, seconds);
    this.weaponFrame(actor, seconds);
    if (player.megaRotAt >= 0 && player.megaRotAt <= seconds) {
      const health = this.health(actor.id);
      if (health > player.maxHealth) { this.host.combat.setHealth(actor, health - 1); player.megaRotAt = seconds + 1; }
      else player.megaRotAt = -1;
```

## Source selection: sed -n '270,405p' src/gameplay/q1/maps/runtime.c

```text
    state->volume = source->volume;
    state->duration = source->duration;
    state->distance = source->distance;
    state->initial_think = source->next_think_seconds;
    state->sounds = source->sounds;
    state->style = source->style;
    state->color_map = source->color_map;
    state->impulse = source->impulse;
    state->counter_value = source->counter_value;
    state->particle_color = source->particle_color;
    if (source->model && source->model[0] == '*') {
        const char *number = source->model + 1;
        char *end;
        errno = 0;
        unsigned long model = strtoul(number, &end, 10);
        if (*number < '0' || *number > '9' || *end || errno || model > UINT32_MAX)
            return q1_map_fail(error, "invalid Q1 inline model name");
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
            !qa_collision_model_bounds(qa_world_geometry(g->services.world), (uint32_t)model,
                                       &body.bounds, error) ||
            !qa_world_body_write(g->services.world, entity->id, &body, error))
            return false;
        state->has_inline_model = true;
        state->inline_model = (uint32_t)model;
    }
    return true;
}
static q1_map_kind classify(const char *name) {
    static const struct {
        const char *name;
        q1_map_kind kind;
    } classes[] = {{"worldspawn", Q1_MAP_WORLD},
                   {"func_wall", Q1_MAP_WALL},
                   {"func_door", Q1_MAP_DOOR},
                   {"func_button", Q1_MAP_BUTTON},
                   {"func_door_secret", Q1_MAP_SECRET_DOOR},
                   {"func_plat", Q1_MAP_PLAT},
                   {"func_train", Q1_MAP_TRAIN},
                   {"misc_teleporttrain", Q1_MAP_TRAIN},
                   {"func_episodegate", Q1_MAP_GATE},
                   {"func_bossgate", Q1_MAP_GATE},
                   {"func_illusionary", Q1_MAP_STATIC},
                   {"item_sigil", Q1_MAP_SIGIL},
                   {"trap_spikeshooter", Q1_MAP_SHOOTER},
                   {"trap_shooter", Q1_MAP_SHOOTER},
                   {"misc_fireball", Q1_MAP_FIREBALL_SOURCE},
                   {"air_bubbles", Q1_MAP_BUBBLES},
                   {"light_globe", Q1_MAP_STATIC},
                   {"light_torch_small_walltorch", Q1_MAP_STATIC},
                   {"light_flame_large_yellow", Q1_MAP_STATIC},
                   {"light_flame_small_yellow", Q1_MAP_STATIC},
                   {"light_flame_small_white", Q1_MAP_STATIC},
                   {"ambient_suck_wind", Q1_MAP_AMBIENT},
                   {"ambient_flouro_buzz", Q1_MAP_AMBIENT},
                   {"ambient_drip", Q1_MAP_AMBIENT},
                   {"ambient_thunder", Q1_MAP_AMBIENT},
                   {"ambient_light_buzz", Q1_MAP_AMBIENT},
                   {"ambient_swamp1", Q1_MAP_AMBIENT},
                   {"ambient_swamp2", Q1_MAP_AMBIENT},
                   {"viewthing", Q1_MAP_VIEW},
                   {"misc_noisemaker", Q1_MAP_NOISE},
                   {"event_lightning", Q1_MAP_LIGHTNING},
                   {"play_sound", Q1_MAP_SOUND},
                   {"play_sound_triggered", Q1_MAP_SOUND},
                   {"random_thunder", Q1_MAP_SOUND},
                   {"random_thunder_triggered", Q1_MAP_SOUND},
                   {"ambient_humming", Q1_MAP_HIP_AMBIENT},
                   {"ambient_rushing", Q1_MAP_HIP_AMBIENT},
                   {"ambient_running_water", Q1_MAP_HIP_AMBIENT},
                   {"ambient_fan_blowing", Q1_MAP_HIP_AMBIENT},
                   {"ambient_waterfall", Q1_MAP_HIP_AMBIENT},
                   {"ambient_riftpower", Q1_MAP_HIP_AMBIENT},
                   {"info_command", Q1_MAP_COMMAND},
                   {"effect_teleport", Q1_MAP_TELEPORT_EFFECT},
                   {"func_exploder", Q1_MAP_EXPLODER},
                   {"func_multi_exploder", Q1_MAP_EXPLODER},
                   {"func_rubble", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble1", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble2", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble3", Q1_MAP_RUBBLE_SOURCE},
                   {"func_earthquake", Q1_MAP_EARTHQUAKE},
                   {"func_particlefield", Q1_MAP_PARTICLE_FIELD},
                   {"func_togglewall", Q1_MAP_TOGGLE_WALL},
                   {"wallsprite", Q1_MAP_WALL_SPRITE},
                   {"misc_sacrifice", Q1_MAP_SACRIFICE},
                   {"trigger_multiple", Q1_MAP_MULTI},
                   {"trigger_once", Q1_MAP_MULTI},
                   {"trigger_secret", Q1_MAP_MULTI},
                   {"trigger_counter", Q1_MAP_COUNTER},
                   {"trigger_relay", Q1_MAP_RELAY},
                   {"trigger_teleport", Q1_MAP_TELEPORT},
                   {"info_teleport_destination", Q1_MAP_DESTINATION},
                   {"trigger_hurt", Q1_MAP_HURT},
                   {"trigger_push", Q1_MAP_PUSH},
                   {"trigger_changelevel", Q1_MAP_CHANGELEVEL},
                   {"trigger_setskill", Q1_MAP_SETSKILL},
                   {"trigger_onlyregistered", Q1_MAP_REGISTERED},
                   {"trigger_monsterjump", Q1_MAP_MONSTERJUMP},
                   {"path_corner", Q1_MAP_PATH},
                   {"info_player_start", Q1_MAP_POINT},
                   {"info_player_start2", Q1_MAP_POINT},
                   {"info_player_coop", Q1_MAP_POINT},
                   {"info_player_deathmatch", Q1_MAP_POINT},
                   {"info_intermission", Q1_MAP_POINT},
                   {"info_notnull", Q1_MAP_POINT},
                   {"testplayerstart", Q1_MAP_POINT},
                   {"light", Q1_MAP_LIGHT},
                   {"light_fluoro", Q1_MAP_LIGHT},
                   {"light_fluorospark", Q1_MAP_LIGHT},
                   {"misc_explobox", Q1_MAP_BARREL},
                   {"misc_explobox2", Q1_MAP_BARREL}};
    for (size_t i = 0; i < sizeof(classes) / sizeof(*classes); ++i)
        if (!strcmp(name, classes[i].name))
            return classes[i].kind;
    return Q1_MAP_FIELDS;
}
bool q1_map_spawn(qa_q1_game *g, q1_actor *entity, const qa_q1_spawn *spawn, bool *handled,
                  qa_error *error) {
    q1_map_kind kind = classify(spawn->classname);
    *handled = kind != Q1_MAP_FIELDS;
    if (!*handled && !spawn->map_fields)
        return true;
    q1_map_state *state = q1_map_allocate(g, entity, error);
    if (!state || !fields(g, entity, spawn->map_fields, error))
        return false;
    state->kind = kind;
    if (!*handled)
        return true;
    entity->kind = Q1_MAP;
    if (kind == Q1_MAP_SACRIFICE)
        return q1_map_sacrifice_spawn(g, entity, error);
    if (q1_map_is_mover(kind))
        return q1_map_mover_spawn(g, entity, error);
    if (kind >= Q1_MAP_PARTICLE_FIELD)
        return q1_map_hip_particles_spawn(g, entity, error);
```

## Source selection: cat ../quake-typescript/src/content/q1/missionpacks/world/index.ts

```text
/* Official mission pack world entities over the shared Q1 source runtime. */
import type { ActorId } from "../../../../contracts/identity.ts";
import type { Vec3 } from "../../../../contracts/math.ts";
import type { Q1SourceFinale } from "../../base/rules.ts";
import type { Q1Actor } from "../../foundation/entity.ts";
import type { Q1EntityServices } from "../../foundation/entity-services.ts";
import type { Q1MissionPack } from "../types.ts";
import { registerHipnoticTriggers } from "./hipnotic-triggers.ts";
import { registerHipnoticTrain } from "./hipnotic-train.ts";
import { registerHipnoticRotation } from "./hipnotic-rotate.ts";
import { registerHipnoticMisc, earthquakeAfterPhysics } from "./hipnotic-misc.ts";
import { registerHipnoticParticles } from "./hipnotic-particles.ts";
import { registerHipnoticSpawn } from "./hipnotic-spawn.ts";
import { registerHipnoticHazards } from "./hipnotic-hazards.ts";
import { RogueRunes } from "./rogue-runes.ts";
import { registerRogueMisc } from "./rogue-misc.ts";
import { registerRogueTime, crashTimeMachine } from "./rogue-time.ts";
import { registerRoguePendulum } from "./rogue-pendulum.ts";
import { registerRoguePlats } from "./rogue-plats.ts";
import { registerRogueHazards, rogueEarthquake } from "./rogue-hazards.ts";
import { RogueTeams } from "./rogue-teams.ts";
import { registerMissionShooters } from "./shooters.ts";
import { registerMissionCampaign } from "./campaign.ts";
import { registerRogueEnding, startRogueEnding } from "./rogue-ending.ts";
import { RogueTag } from "./rogue-tag.ts";

export interface MissionpackWorldHooks {
  readonly charmer?: () => ActorId | null;
  readonly charm?: (entity: Q1Actor, charmer: ActorId) => undefined;
  readonly becomeDecoy?: (target: string, origin: Vec3) => Q1Actor;
  readonly presentFinale?: (result: Q1SourceFinale) => undefined;
  readonly gamecfg?: () => number;
  readonly teamColor?: (actor: ActorId) => number;
  readonly setTeamColor?: (actor: ActorId, team: number) => undefined;
  readonly addFrags?: (actor: ActorId, delta: number) => undefined;
  readonly frags?: (actor: ActorId) => number;
  readonly disconnect?: (actor: ActorId) => undefined;
  readonly playerFrame?: (actor: ActorId) => number;
  readonly playerName?: (actor: ActorId) => string;
}
export class Q1MissionpackWorld {
  private readonly runes: RogueRunes | null;
  private readonly teams: RogueTeams | null;
  private readonly tag: RogueTag | null;
  constructor(readonly game: Q1EntityServices, readonly pack: Q1MissionPack, readonly hooks: MissionpackWorldHooks = {}) {
    registerMissionShooters(game, pack);
    registerMissionCampaign(game, pack, hooks);
    if (pack === "hipnotic") {
      registerHipnoticTriggers(game); registerHipnoticTrain(game); registerHipnoticRotation(game); registerHipnoticMisc(game); registerHipnoticParticles(game); registerHipnoticSpawn(game, hooks); registerHipnoticHazards(game); this.runes = null; this.teams = null; this.tag = null;
    } else { registerRogueMisc(game); registerRogueTime(game); registerRogueEnding(game); registerRoguePendulum(game); registerRoguePlats(game); registerRogueHazards(game); this.runes = new RogueRunes(game, hooks.gamecfg ?? (() => 0)); this.teams = new RogueTeams(game, hooks); this.tag = new RogueTag(game, hooks); }
  }
  afterPhysics(actor: ActorId, _seconds: number): undefined { if (this.pack === "hipnotic") return earthquakeAfterPhysics(this.game, actor); if (this.game.world?.number("rogue:earthquake_active") === 1) rogueEarthquake(this.game, actor, this.game.world.number("rogue:earthquake_intensity")); this.teams?.frame(actor); this.runes?.frame(actor); return startRogueEnding(this.game, actor, this.hooks); }
  playerSpawned(actor: ActorId): undefined { return this.teams?.playerSpawned(actor); }
  dropCarriedFlag(actor: ActorId): undefined { return this.teams?.dropCarriedFlag(actor); }
  impulse(actor: ActorId, impulse: number): boolean { return this.teams?.impulse(actor, impulse) ?? false; }
  savedTeam(actor: ActorId): number { return this.teams?.team(actor) ?? 0; }
  selectSpawn(actor: ActorId): Q1Actor | undefined { return this.teams?.selectSpawn(actor); }
  tagScore(victim: ActorId, attacker: ActorId): number { return this.tag?.score(victim, attacker) ?? 1; }
  confirmedDamage(target: ActorId, attacker: ActorId | null): undefined { return this.teams?.confirmedDamage(target, attacker); }
  playerDied(actor: ActorId, attacker: ActorId | null = null): undefined { this.teams?.playerDied(actor, attacker); return this.runes?.drop(actor); }
  runeAttackDelay(actor: ActorId, delay: number): number { return this.runes?.attackDelay(actor, delay) ?? delay; }
  runeAttackSound(actor: ActorId): undefined { return this.runes?.attackSound(actor); }
  runeDamage(actor: ActorId, amount: number): number { return this.runes?.damage(actor, amount) ?? amount; }
  runeResistance(actor: ActorId, amount: number): number { return this.runes?.resistance(actor, amount) ?? amount; }
  hasRegenerationRune(actor: ActorId): boolean { return this.runes?.hasRegeneration(actor) ?? false; }
  crashTimeMachine(): undefined { return crashTimeMachine(this.game); }
}
export function registerMissionpackWorld(game: Q1EntityServices, pack: Q1MissionPack, hooks: MissionpackWorldHooks = {}): Q1MissionpackWorld { return new Q1MissionpackWorld(game, pack, hooks); }
```

## Source selection: rg -n 'checkpoint|capture|restore|serialize' include/qa/game_q1.h include/qa/game_q1_maps.h src/gameplay/q1

```text
include/qa/game_q1.h:276:bool qa_q1_mg3_progress_restore(qa_q1_game *, qa_actor_id, const qa_q1_mg3_progress *, qa_error *);
src/gameplay/q1/mg3_progress.c:108:bool qa_q1_mg3_progress_restore(qa_q1_game *g, qa_actor_id actor, const qa_q1_mg3_progress *state,
```

## Source selection: rg -n 'qa_combat_(reserve_protection|bind_protection)|qa_q1_game_create|qa_q1_level_create|qa_q1_spawn_select' src

```text
src/campaign/q1/level.c:38:qa_q1_level *qa_q1_level_create(const qa_q1_level_options *options, qa_error *error) {
src/campaign/q1/spawn.c:5:struct qa_q1_spawn_selector {
src/campaign/q1/spawn.c:16:static bool live(const qa_q1_spawn_selector *selector, qa_actor_id actor) {
src/campaign/q1/spawn.c:19:qa_q1_spawn_selector *qa_q1_spawn_selector_create(const qa_q1_spawn_options *options,
src/campaign/q1/spawn.c:41:    qa_q1_spawn_selector *selector = calloc(1, sizeof(*selector));
src/campaign/q1/spawn.c:60:void qa_q1_spawn_selector_destroy(qa_q1_spawn_selector *selector) {
src/campaign/q1/spawn.c:68:qa_actor_id qa_q1_spawn_last(const qa_q1_spawn_selector *selector) { return selector->last; }
src/campaign/q1/spawn.c:69:bool qa_q1_spawn_restore_last(qa_q1_spawn_selector *selector, qa_actor_id last, qa_error *error) {
src/campaign/q1/spawn.c:75:static bool player_body(qa_q1_spawn_selector *selector, qa_actor_id actor, bool living,
src/campaign/q1/spawn.c:104:static bool nearby(qa_q1_spawn_selector *selector, qa_actor_id point, float radius, bool living,
src/campaign/q1/spawn.c:126:static bool visible(qa_q1_spawn_selector *selector, qa_actor_id point, bool *out, qa_error *error) {
src/campaign/q1/spawn.c:155:static qa_actor_id first(const qa_q1_spawn_selector *selector, const qa_q1_spawn_point *points,
src/campaign/q1/spawn.c:162:static bool random_choice(qa_q1_spawn_selector *selector, qa_actor_id *out, qa_error *error) {
src/campaign/q1/spawn.c:172:static bool select_point(qa_q1_spawn_selector *selector, const qa_q1_spawn_point *points,
src/campaign/q1/spawn.c:285:bool qa_q1_spawn_select(qa_q1_spawn_selector *selector, const qa_q1_spawn_point *points,
src/gameplay/combat.c:459:bool qa_combat_reserve_protection(qa_combat *combat, qa_actor_id actor, qa_protection_channel channel,
src/gameplay/combat.c:495:bool qa_combat_bind_protection(qa_combat *combat, qa_protection_lease lease, const qa_protection_binding *binding, qa_error *error) {
src/gameplay/q1/runtime.c:262:bool qa_q1_game_create(const qa_builtin_services *services, const qa_q1_options *options,
```

