/* Port of src/content/q1/addons/monsters/startup.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_STARTUP_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_STARTUP_H

#include "qa/content/q1/addons/context.h"

/* Native spawn functions set their own health and callbacks before InitMonster. */
void qa_q1am_init_mg3_monster(qa_q1_base_monster *monster, qa_q1ad_context *context,
                              const char *model, double type, double size);
void qa_q1am_start_mg3_monster(qa_q1_base_monster *monster, qa_q1ad_context *context);
/* activator NULL = TS null; false = TS null result. */
bool qa_q1am_mg3_monster_activator(qa_q1_services *game, const qa_actor_id *activator, qa_actor_id *out);
/* monster: TS (entity) => BaseMonster closure. */
void qa_q1am_register_mg3_monster_startup(qa_q1_services *game, const char *name,
                                          qa_q1_monster_lookup_fn monster, void *monster_ctx);
bool qa_q1am_waiting_mg3_monster(qa_q1_services *game, qa_q1_actor *entity);
/* C ownership (TS WeakMap entry dies with the game): frees the game's
 * start names and handler contexts. Call after the last use of the game. */
void qa_q1am_release_mg3_monster_startup(qa_q1_services *game);

#endif
