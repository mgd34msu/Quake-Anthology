/* Port of src/content/q2/missionpacks/projectiles/bolts.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_PROJECTILES_BOLTS_H
#define QA_CONTENT_Q2_MISSIONPACKS_PROJECTILES_BOLTS_H
#include "qa/content/q2/missionpacks/types.h"

/* TS Q2MissionPackBolts. Subclasses embed this as first member; all
 * callback ctx point at it (same address through the chain). */
typedef struct qa_q2mp_bolts {
  qa_q2mp_projectile_hooks hooks;
  /* TS `this.callbacks` is virtual: every registration in the chain uses the
   * most-derived set. Each subclass init points these at itself. */
  void *callbacks_self;
  qa_q2_callback_definitions (*callbacks_of)(void *self);
  qa_q2_named_think cb_think[6];
  qa_q2_named_touch cb_touch[5];
  qa_q2_trajectory_def cb_traj[4];
  qa_q2_think ion_sparks, heat_think, heat_think_rerelease, tracker_fly, tracker_pain;
  qa_q2_touch ion_touch, plasma_touch, flechette_touch, green_touch, tracker_touch;
} qa_q2mp_bolts;

void qa_q2mp_bolts_init(qa_q2mp_bolts *bolts, qa_q2mp_projectile_hooks hooks);
qa_q2_callback_definitions qa_q2mp_bolts_callbacks(qa_q2mp_bolts *bolts);
/* game.sourceCallbacks.register(this.callbacks) for the most-derived set. */
void qa_q2mp_bolts_register(qa_q2mp_bolts *bolts, qa_q2_game_services *game);
/* TS protected playerCollision (used by mines.ts subclass). */
void qa_q2mp_bolts_player_collision(qa_q2mp_bolts *bolts, qa_owned_actor self, qa_q2_game_services *game,
                                    qa_q2_entity *projectile);
qa_q2_entity *qa_q2mp_bolts_fire_ion_ripper(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start,
                                             qa_vec3 direction, double damage, double speed, double effects);
qa_q2_entity *qa_q2mp_bolts_fire_blue_blaster(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start,
                                               qa_vec3 direction, double damage, double speed, double effects);
/* turn_fraction default 0.075 via qa_q2mp_bolts_fire_heat_rocket_default. */
qa_q2_entity *qa_q2mp_bolts_fire_heat_rocket(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start,
                                              qa_vec3 direction, double damage, double speed, double radius,
                                              double radius_damage, double turn_fraction);
qa_q2_entity *qa_q2mp_bolts_fire_heat_rocket_default(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2_game_services *game,
                                                       qa_vec3 start, qa_vec3 direction, double damage, double speed,
                                                       double radius, double radius_damage);
qa_q2_entity *qa_q2mp_bolts_fire_plasma(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start,
                                         qa_vec3 direction, double damage, double speed, double radius,
                                         double radius_damage);
qa_q2_entity *qa_q2mp_bolts_fire_flechette(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start,
                                            qa_vec3 direction, double damage, double speed, double kick);
qa_q2_entity *qa_q2mp_bolts_fire_blaster2(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start,
                                           qa_vec3 direction, double damage, double speed, double effects);
qa_q2_entity *qa_q2mp_bolts_fire_tracker(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start,
                                          qa_vec3 direction, double damage, double speed, bool has_enemy,
                                          qa_actor_id enemy);
void qa_q2mp_bolts_fire_heat_beam(qa_q2mp_bolts *b, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start,
                                  qa_vec3 direction, qa_vec3 offset, double damage, double kick);
#endif
