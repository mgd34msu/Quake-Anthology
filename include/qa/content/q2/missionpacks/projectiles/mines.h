/* Port of src/content/q2/missionpacks/projectiles/mines.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_PROJECTILES_MINES_H
#define QA_CONTENT_Q2_MISSIONPACKS_PROJECTILES_MINES_H
#include "qa/content/q2/missionpacks/projectiles/bolts.h"

/* TS Q2MissionPackMines extends Q2MissionPackBolts. */
typedef struct qa_q2mp_mines {
  qa_q2mp_bolts base;
  qa_q2_named_think cb_think[13];
  qa_q2_named_touch cb_touch[9];
  qa_q2_named_die cb_die[2];
  qa_q2_think prox_explode, prox_open, prox_seek, prox_flight, tesla_open, tesla_activate, tesla_active;
  qa_q2_touch prox_land, prox_field, tesla_lava, bad_area_touch;
  qa_q2_die prox_die, tesla_die;
} qa_q2mp_mines;

void qa_q2mp_mines_init(qa_q2mp_mines *mines, qa_q2mp_projectile_hooks hooks);
qa_q2_callback_definitions qa_q2mp_mines_callbacks(qa_q2mp_mines *mines);
qa_q2_entity *qa_q2mp_mines_spawn_bad_area(qa_q2mp_mines *m, qa_q2_game_services *game, qa_vec3 min, qa_vec3 max,
                                            double lifespan, bool has_owner, qa_actor_id owner);
/* False return is TS null return of badAreaEntity. origin NULL = TS undefined. */
qa_q2_entity *qa_q2mp_mines_bad_area_entity(qa_q2mp_mines *m, qa_actor_id actor, qa_q2_game_services *game,
                                             const qa_vec3 *origin);
bool qa_q2mp_mines_bad_area(qa_q2mp_mines *m, qa_actor_id actor, qa_q2_game_services *game);
bool qa_q2mp_mines_mark_tesla_area(qa_q2mp_mines *m, qa_owned_actor self, qa_q2_game_services *game, qa_q2_entity *tesla);
/* TS protected throwMine / grenadeEffect (used by nuke.ts/trap.ts subclasses). */
qa_q2_entity *qa_q2mp_mines_throw_mine(qa_q2mp_mines *m, qa_owned_actor self, qa_q2_game_services *game, const char *classname,
                                        qa_vec3 start, qa_vec3 direction, double speed);
void qa_q2mp_mines_grenade_effect(qa_q2mp_mines *m, qa_q2_entity *entity, qa_q2_game_services *game);
qa_q2_entity *qa_q2mp_mines_fire_prox(qa_q2mp_mines *m, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start,
                                       qa_vec3 direction, double multiplier, double speed);
qa_q2_entity *qa_q2mp_mines_fire_tesla(qa_q2mp_mines *m, qa_owned_actor self, qa_q2_game_services *game, qa_vec3 start,
                                        qa_vec3 direction, double multiplier, double speed);
#endif
