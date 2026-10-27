/* Port of src/content/q2/foundation/weapons/ballistics.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_BALLISTICS_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_BALLISTICS_H
#include "qa/content/q2/foundation/weapons/checkpoint.h"
#include "qa/content/q2/foundation/weapons/projection.h"

typedef struct qa_q2w_hand_grenade_launch {
  qa_vec3 start, direction;
  double damage, speed, timer, radius;
  bool held;
  double gravity;
  bool players_collide;
} qa_q2w_hand_grenade_launch;

typedef struct qa_q2w_actor_double { qa_actor_id actor; double value; } qa_q2w_actor_double;
typedef struct qa_q2w_actor_state { qa_actor_id actor; qa_q2w_state *state; } qa_q2w_actor_state;
typedef struct qa_q2w_actor_input { qa_actor_id actor; qa_q2w_input input; } qa_q2w_actor_input;
typedef struct qa_q2w_actor_noises {
  qa_actor_id actor; bool has_primary, has_secondary; qa_q2w_noise_record primary, secondary;
} qa_q2w_actor_noises;

/* TS Q2Ballistics. Maps become arrays keyed by actor (qa_same_actor).
 * states entries are owned by the object (bind transfers ownership). */
typedef struct qa_q2w_ballistics {
  qa_q2w_hooks hooks;
  qa_q2w_actor_double *blaster_causes; size_t blaster_cause_count, blaster_cause_cap;
  qa_q2w_actor_double *silencer_charges; size_t silencer_count, silencer_cap;
  qa_q2w_actor_state *states; size_t state_count, state_cap;
  qa_q2w_actor_input *inputs; size_t input_count, input_cap;
  qa_q2w_actor_noises *noises; size_t noise_count, noise_cap;
  bool has_sound_entity, has_sound2_entity;
  qa_q2w_noise_record sound_entity, sound2_entity;
  void **release_registries; size_t release_count, release_cap;
  qa_q2f_named_think cb_think[5];
  qa_q2f_named_touch cb_touch[4];
  qa_q2f_named_die cb_die[1];
} qa_q2w_ballistics;

bool qa_q2w_ballistics_init(qa_q2w_ballistics *b, qa_q2w_hooks hooks); /* false only on OOM */
void qa_q2w_ballistics_destroy(qa_q2w_ballistics *b);
qa_q2f_callback_definitions qa_q2w_ballistics_callbacks(qa_q2w_ballistics *b);
void qa_q2w_ballistics_register_callbacks(qa_q2w_ballistics *b, qa_q2f_game *game);
/* captureProjectiles: fills checkpoint's blaster_causes (malloc'd;
 * free with qa_q2w_free_cause_entries). */
bool qa_q2w_ballistics_capture_projectiles(qa_q2w_ballistics *b, qa_q2w_cause_entry **out, size_t *out_count);
void qa_q2w_free_cause_entries(qa_q2w_cause_entry *entries);
void qa_q2w_ballistics_restore_projectiles(qa_q2w_ballistics *b, qa_q2f_game *game,
                                           const qa_q2w_cause_entry *causes, size_t count);
double qa_q2w_ballistics_silencer_shots(qa_q2w_ballistics *b, qa_actor_id actor);
void qa_q2w_ballistics_grant_silencer(qa_q2w_ballistics *b, qa_actor_id actor, qa_q2f_game *game, double charges);
void qa_q2w_ballistics_reset_silencer(qa_q2w_ballistics *b, qa_actor_id actor);
void qa_q2w_ballistics_track_actors(qa_q2w_ballistics *b, qa_q2f_game *game);
double qa_q2w_ballistics_shot_mask(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2f_game *game);
/* kind: "self"|"weapon"|"impact" */
void qa_q2w_ballistics_player_noise(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 origin, const char *kind);
void qa_q2w_ballistics_player_noise_for_actor(qa_q2w_ballistics *b, qa_actor_id owner, qa_q2f_game *game, qa_vec3 origin, const char *kind);
void qa_q2w_ballistics_check_dodge(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start, qa_vec3 direction, double speed);
void qa_q2w_ballistics_fire_bullet(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start, qa_vec3 direction,
                                   double damage, double kick, double hspread, double vspread, double mod);
void qa_q2w_ballistics_fire_shotgun(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start, qa_vec3 direction,
                                    double damage, double kick, double hspread, double vspread, double count, double mod);
void qa_q2w_ballistics_fire_rail(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start, qa_vec3 direction, double damage, double kick);
bool qa_q2w_ballistics_fire_hit(qa_q2w_ballistics *b, qa_q2f_entity *self, qa_q2f_game *game, qa_vec3 aim, double damage, double kick);
/* means_of_death default (hyper ? hyperblaster : blaster): pass
 * QA_Q2W_BLASTER_DEFAULT_MOD (-1) via _default wrapper. */
qa_q2f_entity *qa_q2w_ballistics_fire_blaster(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start, qa_vec3 direction,
                                              double damage, double speed, double effects, bool hyper, double means_of_death);
qa_q2f_entity *qa_q2w_ballistics_fire_blaster_default(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start, qa_vec3 direction,
                                                      double damage, double speed, double effects, bool hyper);
qa_q2f_entity *qa_q2w_ballistics_fire_grenade(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start, qa_vec3 direction,
                                              double damage, double speed, double timer, double radius, bool hand, bool held, bool monster,
                                              const qa_q2w_grenade_adjustment *adjustment);
qa_q2f_entity *qa_q2w_ballistics_fire_hand_grenade(qa_q2w_ballistics *b, qa_actor_id owner, qa_q2f_game *game, qa_q2w_hand_grenade_launch spec);
qa_q2f_entity *qa_q2w_ballistics_fire_rocket(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start, qa_vec3 direction,
                                             double damage, double speed, double radius, double radius_damage);
qa_q2f_entity *qa_q2w_ballistics_fire_bfg(qa_q2w_ballistics *b, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start, qa_vec3 direction,
                                           double damage, double speed, double radius);

/* Map helpers shared with player.c (TS protected/public fields). */
qa_q2w_state *qa_q2w_ballistics_state(qa_q2w_ballistics *b, qa_actor_id actor);
bool qa_q2w_ballistics_set_state(qa_q2w_ballistics *b, qa_actor_id actor, qa_q2w_state *state);
const qa_q2w_input *qa_q2w_ballistics_input(qa_q2w_ballistics *b, qa_actor_id actor);
bool qa_q2w_ballistics_set_input(qa_q2w_ballistics *b, qa_actor_id actor, qa_q2w_input input);

typedef qa_touch_contact qa_q2w_projectile_contact;
#endif
