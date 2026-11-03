/* Port of src/content/q1/addons/monsters/ordinary/slime.ts.
 * quakec_mg3/monsters/tarbaby.qc: monster_slime, SlimeMissile, SlimeMissileTouch and SlimeStartCheck.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_SLIME_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_SLIME_H

#include "qa/content/q1/addons/context.h"

/* TS MonsterFactory: (context, entity) => BaseMonster. */
typedef qa_q1_base_monster *(*qa_q1am_monster_factory)(qa_q1ad_context *context, qa_q1_actor *entity);
/* TS Map<OwnedActor, BaseMonster>.set over the caller's store. */
typedef struct qa_q1am_monster_store {
  void *ctx;
  void (*set)(void *ctx, qa_owned_actor actor, qa_q1_base_monster *monster);
} qa_q1am_monster_store;

/* tbaby_die2 for the MG3 tarbaby (TS tarbabyDie2(context) closure: ctx = the qa_q1ad_context). */
void qa_q1am_tarbaby_die2(void *ctx, qa_q1_base_monster *monster);
/* monsters is copied; its ctx must outlive the game's callbacks. */
void qa_q1am_register_slime(qa_q1ad_context *context, qa_q1am_monster_store monsters, qa_q1am_monster_factory create);
/* C ownership (TS GC): frees the game's registerSlime closures. */
void qa_q1am_release_slime(qa_q1_services *game);

#endif
