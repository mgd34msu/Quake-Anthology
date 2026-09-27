#ifndef QA_GAME_Q2_MONSTERS_H
#define QA_GAME_Q2_MONSTERS_H

#include "qa/game_q2.h"
#include "qa/monster_mission.h"

#define QA_Q2_MONSTER_NAME_CAPACITY 48
#define QA_Q2_MONSTER_MOVE_CAPACITY 64

typedef struct qa_q2_reinforcement_checkpoint {
  char classname[QA_Q2_MONSTER_NAME_CAPACITY];
  int strength;
} qa_q2_reinforcement_checkpoint;

typedef enum qa_q2_monster_count {
  QA_Q2_MONSTER_COUNT_TOTAL,
  QA_Q2_MONSTER_COUNT_KILLED
} qa_q2_monster_count;

typedef struct qa_q2_monster_services {
  qa_monster_missions missions;
  void *context;
  bool (*count)(void *, qa_actor_id, qa_q2_monster_count, qa_error *);
} qa_q2_monster_services;

bool qa_q2_monsters_bind_services(qa_q2_game *, const qa_q2_monster_services *, qa_error *);
bool qa_q2_monsters_end_frame(qa_q2_game *, qa_error *);

typedef struct qa_q2_monster_view {
  qa_string_id classname, model;
  qa_actor_id enemy, commander;
  uint64_t effects;
  int frame, old_frame, skin;
  uint32_t render_flags;
  float scale, view_height, ideal_yaw;
  bool can_take_damage, dead, corpse, gibbed, triggered, summoned, visible;
} qa_q2_monster_view;

typedef struct qa_q2_monster_path_corner {
  qa_actor_id corner, next;
  uint64_t pause_until_ns;
} qa_q2_monster_path_corner;

typedef struct qa_q2_monster_combat_point {
  qa_actor_id corner, next;
  bool has_target, hold_if_walking;
} qa_q2_monster_combat_point;

typedef struct qa_q2_monster_sound_checkpoint {
  qa_q2_saved_reference actor, owner;
  qa_vec3 origin;
  uint64_t time_ns;
  bool present;
} qa_q2_monster_sound_checkpoint;

enum { QA_Q2_MONSTER_TRAIL_POINTS = 8 };

typedef struct qa_q2_monster_trail_point_checkpoint {
  qa_vec3 origin;
  uint64_t time_ns;
  float yaw;
} qa_q2_monster_trail_point_checkpoint;

typedef struct qa_q2_monster_trail_checkpoint {
  qa_q2_saved_reference actor;
  qa_vec3 previous_origin;
  qa_q2_monster_trail_point_checkpoint points[QA_Q2_MONSTER_TRAIL_POINTS];
  size_t count;
  bool has_previous;
} qa_q2_monster_trail_checkpoint;

typedef struct qa_q2_monster_alert_checkpoint {
  qa_q2_saved_reference player, observer;
  uint64_t time_ns, hostile_ns;
} qa_q2_monster_alert_checkpoint;

typedef struct qa_q2_monsters_checkpoint {
  uint32_t version;
  qa_q2_saved_reference sight_client, sight_observer;
  uint64_t sight_time_ns, last_frame_ns;
  bool began_frame;
  qa_q2_monster_trail_checkpoint *trails;
  size_t trail_count;
  qa_q2_monster_alert_checkpoint *alerts;
  size_t alert_count;
} qa_q2_monsters_checkpoint;

typedef struct qa_q2_monster_checkpoint {
  uint32_t version;
  char definition[QA_Q2_MONSTER_NAME_CAPACITY];
  char move[QA_Q2_MONSTER_MOVE_CAPACITY];
  char next_move[QA_Q2_MONSTER_MOVE_CAPACITY];
  uint32_t spawnflags, attack_state, spawned_by, controller_kind;
  uint32_t start_phase;
  qa_string_id combat_target;
  uint64_t start_due_ns;
  bool death_notified;
  int frame, next_frame, old_frame, skin, style, count;
  uint32_t render_flags;
  float entity_scale, animation_scale, base_health, health_scaling;
  float max_health, max_power_armor;
  uint32_t initial_power_armor, medic_tries;
  float gib_health, normal_height, view_height, ideal_yaw, yaw_speed;
  float blind_fire_delay, fly_min_distance, fly_max_distance;
  float fly_acceleration, fly_speed;
  uint64_t next_frame_ns, pause_ns, idle_ns, pain_ns, fire_ns;
  uint64_t duck_ns, next_duck_ns, dodge_ns, attack_ns, check_attack_ns;
  uint64_t strafe_ns, melee_ns, search_ns, trail_ns, hostile_ns;
  uint64_t air_ns, environment_ns, jump_ns, flies_ns, fly_position_ns;
  uint64_t recovery_ns, death_ns, spawn_ns, timestamp_ns, coop_check_ns;
  uint64_t react_ns;
  qa_q2_saved_reference enemy, old_enemy, goal, move_target;
  qa_q2_saved_reference last_player_enemy;
  qa_q2_saved_reference commander, activator, resurrect_target, hazard, proboscis;
  qa_q2_saved_reference healer, bad_medic[2];
  qa_q2_saved_reference controller_owner, controller_target;
  qa_q2_monster_sound_checkpoint sound_target;
  qa_vec3 last_sighting, saved_goal, blind_fire_target;
  qa_vec3 fly_ideal_position, fly_recovery_direction, last_damage_point;
  qa_vec3 saved_attack_position, controller_direction;
  qa_attack last_attack;
  qa_q2_saved_reference attack_attacker, attack_inflictor, attack_projectile;
  float pending_damage, pending_kick, controller_damage;
  uint64_t controller_ns;
  int64_t monster_slots, monster_used;
  int water_level, water_type;
  bool has_summons;
  int summon_strength;
  uint32_t summon_count;
  qa_q2_reinforcement_checkpoint summons[5];
  uint64_t last_link_count;
  bool has_saved_goal, good_guy, target_anger, ignore_shots, do_not_count, source_blocked;
  bool brutal, medic, resurrecting, can_take_damage, dead, corpse, gibbed;
  bool stand_ground, temporary_stand_ground, hold_frame, ducked, dodging;
  bool charging, manual_steering, combat_point, lefty, had_visibility;
  bool close_sight_tripped, lost_sight, pursue_next, pursue_temporary;
  bool pursuit_last_seen, cocked, force_refire, triggered, visible;
  bool pending_pain, pending_death, alternate_fly, fly_buzzard, fly_above;
  bool fly_pinned, fly_thrusters, hint_path, summoned, touch_active;
  bool turret_attached, initialized, controller_medic, controller_fired;
} qa_q2_monster_checkpoint;

bool qa_q2_monster_read(const qa_q2_game *, qa_actor_id, qa_q2_monster_view *);
bool qa_q2_monster_holds_healthbar(const qa_q2_game *, qa_actor_id);
bool qa_q2_monster_turret_admit(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_monster_turret_aim(qa_q2_game *, qa_actor_id, qa_actor_id *enemy,
                              bool *fire_ready, qa_error *);
bool qa_q2_monster_turret_release(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_monster_capture(qa_q2_game *, qa_actor_id,
                           qa_q2_monster_checkpoint *, qa_error *);
bool qa_q2_monster_restore(qa_q2_game *, qa_actor_id,
                           const qa_q2_monster_checkpoint *, qa_error *);
bool qa_q2_monsters_capture(qa_q2_game *, qa_q2_monsters_checkpoint *,
                            qa_error *);
bool qa_q2_monsters_restore(qa_q2_game *, const qa_q2_monsters_checkpoint *,
                            qa_error *);
void qa_q2_monsters_checkpoint_free(qa_q2_monsters_checkpoint *);
bool qa_q2_monster_route_contact(const qa_q2_game *, qa_actor_id monster,
                                 qa_actor_id corner, bool combat_point,
                                 bool *eligible);
bool qa_q2_monster_touch_path_corner(qa_q2_game *, qa_actor_id monster,
                                     const qa_q2_monster_path_corner *,
                                     bool *accepted, qa_error *);
bool qa_q2_monster_touch_combat_point(qa_q2_game *, qa_actor_id monster,
                                      const qa_q2_monster_combat_point *,
                                      bool *accepted, bool *finished,
                                      qa_error *);
bool qa_q2_monster_path_activator(const qa_q2_game *, qa_actor_id monster,
                                  qa_actor_id *activator);
bool qa_q2_monster_target_anger(qa_q2_game *, qa_actor_id monster,
                                qa_actor_id target, qa_error *);

/* Rogue summon placement keeps the source trace rules while sharing the
 * authoritative world. A true return reports a completed query; found/valid
 * distinguishes ordinary blocked placement from a service error. */
bool qa_q2_rogue_find_spawn_point(qa_q2_game *, qa_vec3 start, qa_bounds bounds,
                                  float max_move_up, bool *found,
                                  qa_vec3 *position, qa_error *);
bool qa_q2_rogue_check_ground_spawn(qa_q2_game *, qa_vec3 origin,
                                    qa_bounds bounds, float height,
                                    float gravity, bool *valid, qa_error *);
bool qa_q2_rogue_spawn_growth(qa_q2_game *, qa_vec3 origin, unsigned size,
                              qa_error *);

#endif
