#ifndef QA_Q2_MONSTERS_INTERNAL_H
#define QA_Q2_MONSTERS_INTERNAL_H

#include "../internal.h"
#include "qa/game_q2_monsters.h"
#include "qa/game_q2_entities.h"
#include "qa/game_q2_player.h"

#include <float.h>
#include <stdio.h>

#define Q2M_MONSTER_MASK UINT32_C(0x02020003)
#define Q2M_ATTACK_MASK UINT32_C(0x0600001b)
#define Q2M_OPAQUE_MASK UINT32_C(0x00000019)
#define Q2M_WATER_MASK UINT32_C(0x00000038)
#define Q2M_SECOND UINT64_C(1000000000)
#define Q2M_TENTH UINT64_C(100000000)
#define Q2M_NAME_CAPACITY 48
#define Q2M_MOVE_CAPACITY 64

typedef enum q2m_species {
  Q2M_INFANTRY,
  Q2M_SOLDIER_LIGHT,
  Q2M_SOLDIER,
  Q2M_SOLDIER_SS,
  Q2M_BERSERK,
  Q2M_BRAIN,
  Q2M_CHICK,
  Q2M_FLIPPER,
  Q2M_FLOATER,
  Q2M_FLYER,
  Q2M_GLADIATOR,
  Q2M_GUNNER,
  Q2M_HOVER,
  Q2M_JORG,
  Q2M_MAKRON,
  Q2M_MEDIC,
  Q2M_MUTANT,
  Q2M_PARASITE,
  Q2M_SUPERTANK,
  Q2M_TANK,
  Q2M_TANK_COMMANDER,
  Q2M_BOSS2,
  Q2M_ACTOR,
  Q2M_INSANE,
  Q2M_GEKK,
  Q2M_FIXBOT,
  Q2M_GLADB,
  Q2M_BOSS5,
  Q2M_CHICK_HEAT,
  Q2M_SOLDIER_RIPPER,
  Q2M_SOLDIER_HYPER,
  Q2M_SOLDIER_LASER,
  Q2M_STALKER,
  Q2M_KAMIKAZE,
  Q2M_DAEDALUS,
  Q2M_TURRET,
  Q2M_CARRIER,
  Q2M_MEDIC_COMMANDER,
  Q2M_WIDOW,
  Q2M_WIDOW2,
  Q2M_ARACHNID,
  Q2M_GUARDIAN,
  Q2M_GUN_COMMANDER,
  Q2M_SHAMBLER,
  Q2M_TURRET_DRIVER,
  Q2M_TANK_STAND,
  Q2M_BOSS3_STAND,
  Q2M_SPECIES_COUNT
} q2m_species;

typedef enum q2m_locomotion {
  Q2M_WALK,
  Q2M_FLY,
  Q2M_SWIM,
  Q2M_STATIONARY
} q2m_locomotion;

typedef enum q2m_attack_kind {
  Q2M_ATTACK_NONE,
  Q2M_ATTACK_HIT,
  Q2M_ATTACK_BULLET,
  Q2M_ATTACK_SHOTGUN,
  Q2M_ATTACK_BLASTER,
  Q2M_ATTACK_ROCKET,
  Q2M_ATTACK_GRENADE,
  Q2M_ATTACK_RAIL,
  Q2M_ATTACK_BFG,
  Q2M_ATTACK_HEAT,
  Q2M_ATTACK_TRACKER,
  Q2M_ATTACK_BEAM,
  Q2M_ATTACK_ION,
  Q2M_ATTACK_BLUE_BOLT,
  Q2M_ATTACK_GREEN_BOLT,
  Q2M_ATTACK_PLASMA,
  Q2M_ATTACK_FLECHETTE,
  Q2M_ATTACK_SUMMON
} q2m_attack_kind;

enum {
  Q2M_MOD_UNKNOWN = 0,
  Q2M_MOD_BLASTER = 1,
  Q2M_MOD_SHOTGUN = 2,
  Q2M_MOD_MACHINEGUN = 4,
  Q2M_MOD_GRENADE = 6,
  Q2M_MOD_GRENADE_SPLASH = 7,
  Q2M_MOD_ROCKET = 8,
  Q2M_MOD_ROCKET_SPLASH = 9,
  Q2M_MOD_HYPERBLASTER = 10,
  Q2M_MOD_RAILGUN = 11,
  Q2M_MOD_BFG_BLAST = 13,
  Q2M_MOD_WATER = 17,
  Q2M_MOD_SLIME = 18,
  Q2M_MOD_LAVA = 19,
  Q2M_MOD_HIT = 32,
  Q2M_MOD_HEATBEAM = 44,
  Q2M_MOD_TRACKER = 51
};

typedef struct q2m_fire_spec {
  q2m_attack_kind kind;
  int flash;
  qa_vec3 start, direction;
  float damage, kick, speed, radius, radius_damage, fuse;
  float horizontal_spread, vertical_spread;
  unsigned pellets;
  int direct_mod, splash_mod;
  uint64_t projectile_effects;
  float grenade_right, grenade_up, grenade_gravity, turn_fraction;
  bool has_projectile_effects;
  bool has_projectile_enemy;
  bool has_grenade_impulse;
  bool has_turn_fraction;
  qa_actor_id projectile_enemy;
} q2m_fire_spec;

typedef enum q2m_ai_kind {
  Q2M_AI_NONE,
  Q2M_AI_STAND,
  Q2M_AI_WALK,
  Q2M_AI_RUN,
  Q2M_AI_CHARGE,
  Q2M_AI_MOVE,
  Q2M_AI_SOLDIER_MOVE,
  Q2M_AI_TURN,
  Q2M_AI_SOURCE
} q2m_ai_kind;

typedef enum q2m_attack_state {
  Q2M_STRAIGHT,
  Q2M_SLIDING,
  Q2M_MELEE,
  Q2M_MISSILE,
  Q2M_BLIND
} q2m_attack_state;

typedef enum q2m_spawned_by {
  Q2M_SPAWN_NONE,
  Q2M_SPAWN_CARRIER,
  Q2M_SPAWN_MEDIC,
  Q2M_SPAWN_WIDOW
} q2m_spawned_by;

typedef enum q2m_start_phase {
  Q2M_START_ACTIVE,
  Q2M_START_PENDING,
  Q2M_START_DORMANT,
  Q2M_START_TRIGGER,
  Q2M_START_MANUAL
} q2m_start_phase;

typedef enum q2m_controller_kind {
  Q2M_CONTROLLER_NONE,
  Q2M_CONTROLLER_BEAM,
  Q2M_CONTROLLER_BOSS_EXPLODER,
  Q2M_CONTROLLER_MAKRON_SPAWN
} q2m_controller_kind;

typedef struct q2m_frame_action {
  const char *callback;
  int next_frame;
} q2m_frame_action;

typedef struct q2m_frame {
  q2m_ai_kind ai;
  const char *source_ai;
  float distance;
  int lerp_frame;
  uint32_t action_first;
  uint16_t action_count;
} q2m_frame;

typedef struct q2m_move {
  const char *name;
  int first_frame, last_frame;
  const char *end;
  float sidestep_scale;
  uint32_t frame_first;
} q2m_move;

typedef struct q2m_move_set {
  const char *key;
  const q2m_move *moves;
  size_t move_count;
  const q2m_frame *frames;
  size_t frame_count;
  const q2m_frame_action *actions;
  size_t action_count;
} q2m_move_set;

enum q2m_flags {
  Q2M_HAS_MELEE = 1u << 0,
  Q2M_HAS_RANGED = 1u << 1,
  Q2M_BLIND_FIRE = 1u << 2,
  Q2M_METALLIC = 1u << 3,
  Q2M_EXPLODES = 1u << 4,
  Q2M_BOSS = 1u << 5,
  Q2M_DUCKS = 1u << 6,
  Q2M_SIDESTEPS = 1u << 7,
  Q2M_JUMPS = 1u << 8,
  Q2M_POWER_SCREEN = 1u << 9,
  Q2M_POWER_SHIELD = 1u << 10,
  Q2M_GOOD_GUY = 1u << 11,
  Q2M_DO_NOT_COUNT = 1u << 12,
  Q2M_TOUCH_ATTACK = 1u << 13,
  Q2M_REGENERATES = 1u << 14,
  Q2M_NO_GIB = 1u << 15
};

typedef struct q2m_definition {
  const char *classname, *model, *move_set;
  q2m_species species;
  q2m_locomotion locomotion;
  qa_bounds bounds;
  float health, gib_health, mass, scale, view_height, yaw_speed;
  uint32_t flags;
  q2m_attack_kind primary, secondary;
  float primary_damage, secondary_damage, projectile_speed;
  const char *initial_move, *stand_move, *walk_move, *run_move;
  const char *attack_move, *attack2_move, *melee_move;
  const char *pain1_move, *pain2_move, *pain3_move;
  const char *death1_move, *death2_move;
  const char *sight_sound, *pain_sound, *death_sound, *idle_sound;
} q2m_definition;

typedef struct q2m_sound_target {
  qa_actor_id actor, owner;
  qa_vec3 origin;
  uint64_t time_ns;
  bool present;
} q2m_sound_target;

struct qa_q2_monster {
  struct q2m_summon_state *summons;
  const q2m_definition *definition;
  const q2m_move_set *move_set;
  const q2m_move *move, *next_move;
  qa_string_id classname, model;
  qa_string_id combat_target;
  q2m_start_phase start_phase;
  uint64_t start_due_ns;
  bool death_notified;
  uint32_t spawnflags;
  int frame, next_frame, old_frame, skin, style, count;
  uint32_t render_flags;
  float entity_scale, animation_scale, base_health, health_scaling;
  float gib_health, normal_height, view_height, ideal_yaw, yaw_speed;
  float blind_fire_delay, fly_min_distance, fly_max_distance;
  float fly_acceleration, fly_speed;
  uint64_t next_frame_ns, pause_ns, idle_ns, pain_ns, fire_ns;
  uint64_t duck_ns, next_duck_ns, dodge_ns, attack_ns, check_attack_ns;
  uint64_t strafe_ns, melee_ns, search_ns, trail_ns, hostile_ns;
  uint64_t air_ns, environment_ns, jump_ns, flies_ns, fly_position_ns;
  uint64_t recovery_ns, death_ns, spawn_ns, timestamp_ns, coop_check_ns;
  uint64_t react_ns;
  uint64_t corpse_check_ns;
  qa_actor_id enemy, old_enemy, goal, move_target, commander, activator;
  qa_actor_id last_player_enemy;
  qa_actor_id resurrect_target, hazard, proboscis;
  q2m_sound_target sound_target;
  qa_vec3 last_sighting, saved_goal, blind_fire_target;
  qa_vec3 fly_ideal_position, fly_recovery_direction;
  qa_vec3 last_damage_point, saved_attack_position;
  qa_vec3 controller_direction;
  qa_attack last_attack;
  float pending_damage, pending_kick;
  int64_t monster_slots, monster_used;
  int water_level, water_type;
  uint64_t last_link_count;
  q2m_attack_state attack_state;
  q2m_spawned_by spawned_by;
  q2m_controller_kind controller_kind;
  qa_actor_id controller_owner, controller_target;
  float controller_damage;
  uint64_t controller_ns;
  bool has_saved_goal, good_guy, target_anger, ignore_shots, do_not_count, source_blocked;
  bool brutal, medic, resurrecting, can_take_damage, dead, corpse, gibbed;
  bool stand_ground, temporary_stand_ground, hold_frame, ducked, dodging;
  bool charging, manual_steering, combat_point, lefty, had_visibility;
  bool close_sight_tripped, lost_sight, pursue_next, pursue_temporary;
  bool pursuit_last_seen, cocked, force_refire, triggered, visible;
  bool pending_pain, pending_death, alternate_fly, fly_buzzard, fly_above;
  bool fly_pinned, fly_thrusters, hint_path, summoned, touch_active;
  bool turret_attached, initialized;
  bool controller_medic, controller_fired;
};

typedef struct q2m_context {
  qa_q2_game *game;
  q2_actor *actor;
  struct qa_q2_monster *monster;
  qa_body_state body;
  qa_combat_state combat;
  float elapsed;
} q2m_context;

qa_vec3 q2m_vector_angles(qa_vec3);
bool q2m_mission(q2m_context *, qa_monster_mission *, bool *, qa_error *);
bool q2m_count(q2m_context *, qa_q2_monster_count, qa_error *);
bool q2m_lifecycle_admitted(q2m_context *, bool automatic, qa_error *);
bool q2m_lifecycle_tick(q2m_context *, bool *handled, qa_error *);
bool q2m_lifecycle_use(q2m_context *, qa_actor_id, qa_error *);
bool q2m_lifecycle_killed(q2m_context *, qa_error *);
bool q2m_lifecycle_route(q2m_context *, bool found_target, bool *routed, qa_error *);
bool q2m_show(q2m_context *, qa_error *);
void q2m_free_monster(struct qa_q2_monster *);
bool q2m_health_target(q2m_context *, qa_error *);

const q2m_move_set *q2m_moves_named(const char *);
const q2m_definition *q2m_definition_for(const qa_q2_game *, const char *);
const q2m_move_set *q2m_move_set_for(const qa_q2_game *,
                                     const q2m_definition *);
const q2m_move *q2m_move_named(const struct qa_q2_monster *, const char *);
bool q2m_set_move(q2m_context *, const char *, bool immediate, qa_error *);
bool q2m_refresh(q2m_context *, qa_error *);
bool q2m_damageable(q2m_context *, bool enabled, qa_error *);
bool q2m_write_body(q2m_context *, bool link, qa_error *);
bool q2m_link(q2m_context *, qa_error *);
bool q2m_emit(q2m_context *, qa_builtin_event_kind, const char *, int, qa_vec3,
              qa_vec3, float, qa_error *);
bool q2m_sound(q2m_context *, const char *, int, float, qa_error *);
bool q2m_animation(q2m_context *, qa_error *);
const q2m_frame *q2m_frame_at(const struct qa_q2_monster *, const q2m_move *, int, qa_error *);
bool q2m_release(q2m_context *, qa_error *);
bool q2m_alive(const q2m_context *);
uint64_t q2m_after(uint64_t, double);

static inline float q2m_random(qa_q2_game *game) { return q2_random(game); }
static inline float q2m_crandom(qa_q2_game *game) { return q2_crandom(game); }

bool q2m_visible(q2m_context *, qa_actor_id, bool *, qa_error *);
bool q2m_perception_begin(qa_q2_game *, qa_error *);
qa_actor_id q2m_current_sight_client(const qa_q2_game *);
bool q2m_perception_alert(q2m_context *, qa_actor_id, qa_error *);
bool q2m_react_to_damage(q2m_context *, qa_actor_id, qa_error *);
float q2m_distance(q2m_context *, qa_actor_id);
bool q2m_find_target(q2m_context *, bool *found, qa_error *);
bool q2m_found_target(q2m_context *, qa_actor_id, qa_error *);
bool q2m_medic_acquire(q2m_context *, bool preserve_enemy, bool *, qa_error *);
bool q2m_check_attack(q2m_context *, bool *, qa_error *);
bool q2m_run_ai(q2m_context *, q2m_ai_kind, const char *, float, qa_error *);
bool q2m_move_to_goal(q2m_context *, float, qa_error *);
bool q2m_change_yaw(q2m_context *, qa_error *);
bool q2m_face_enemy(q2m_context *, qa_error *);
bool q2m_clear_shot(q2m_context *, qa_vec3, bool *, qa_error *);

bool q2m_attack(q2m_context *, q2m_attack_kind, float, qa_error *);
bool q2m_attack_flash(q2m_context *, q2m_attack_kind, float damage, int flash,
                      float aim_offset, qa_error *);
bool q2m_attack_forward(q2m_context *, q2m_attack_kind, float damage,
                        int flash, qa_error *);
bool q2m_project_flash(const q2m_context *, int flash, qa_vec3 *,
                       qa_error *);
qa_vec3 q2m_project_offset(const q2m_context *, qa_vec3);
bool q2m_parasite_callback(q2m_context *, const char *, bool *, qa_error *);
bool q2m_parasite_interrupt(q2m_context *, bool death, qa_error *);
bool q2m_parasite_charge(q2m_context *, float, qa_error *);
bool q2m_muzzle_offset(const q2m_context *, int flash, qa_vec3 *, qa_error *);
bool q2m_source_shot(q2m_context *, int flash, float lead, qa_vec3 *, qa_vec3 *,
                     bool *available, qa_error *);
bool q2m_predict_shot(q2m_context *, int flash, float speed, bool eye,
                      float offset, qa_vec3 *, qa_vec3 *, bool *available,
                      qa_error *);
bool q2m_predict_from(q2m_context *, qa_vec3 start, float speed, bool eye,
                      float offset, qa_vec3 *point, qa_vec3 *direction,
                      bool *available, qa_error *);
q2m_fire_spec q2m_fire_default(q2m_context *, q2m_attack_kind, float damage,
                               int flash, qa_vec3 start, qa_vec3 direction);
bool q2m_fire(q2m_context *, const q2m_fire_spec *, qa_error *);
bool q2m_widow_disrupt(q2m_context *, qa_error *);
bool q2m_melee(q2m_context *, float range, float damage, float kick,
               qa_error *);
bool q2m_damage_enemy(q2m_context *, float range, int canonical_mod,
                      uint32_t flags, float damage, float kick, bool *hit,
                      qa_error *);
bool q2m_dispatch(q2m_context *, const char *, qa_error *);
bool q2m_pain(q2m_context *, qa_error *);
bool q2m_die(q2m_context *, qa_error *);
bool q2m_corpse(q2m_context *, qa_error *);
bool q2m_start_boss_explosion(q2m_context *, qa_error *);
bool q2m_boss_explosion_tick(q2m_context *, qa_error *);
bool q2m_finish_boss_death(q2m_context *, qa_error *);
bool q2m_world_effects(q2m_context *, qa_error *);
bool q2m_touch(q2m_context *, const qa_touch_contact *, qa_error *);
bool q2m_kamikaze(q2m_context *, qa_error *);
bool q2m_berserk_land(q2m_context *, qa_error *);
bool q2m_spawn_monster_beam(q2m_context *, qa_actor_id target, qa_vec3 origin,
                            qa_vec3 direction, float damage, bool medic,
                            qa_error *);
bool q2m_spawn_boss_exploder(q2m_context *, qa_error *);
bool q2m_schedule_makron_spawn(q2m_context *, qa_error *);
bool q2m_controller_tick(qa_q2_game *, q2_actor *, qa_error *);

#endif
