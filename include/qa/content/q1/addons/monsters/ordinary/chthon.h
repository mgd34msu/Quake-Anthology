/* Port of src/content/q1/addons/monsters/ordinary/chthon.ts.
 * quakec_mg1/monsters/boss.qc BOSS_KILLABLE and quakec_mg1/combat.qc: the 4000 HP Chthon
 * that only the lightning gun hurts. GPL-2.0-or-later. chthonFrames is composed into the
 * generated ordinary source tables (ordinary/source_frames.h). */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_CHTHON_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_CHTHON_H

#include "qa/content/q1/addons/context.h"

#define QA_Q1AM_BOSS_KILLABLE 2

bool qa_q1am_killable_chthon(const qa_q1_base_monster *monster);
/* TS chthonActions. */
extern const qa_q1_monster_action_entry qa_q1am_chthon_actions[];
extern const size_t qa_q1am_chthon_action_count;
/* boss_awake with BOSS_KILLABLE: 4000 HP, damageable, no longer re-triggerable into the rise. */
void qa_q1am_awake_killable_chthon(qa_q1_base_monster *monster, const qa_actor_id *activator);
/* boss_pain: shock animations on the first hit and when health crosses one third and two thirds. */
void qa_q1am_chthon_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage);
/* chthon: TS (entity) => BaseMonster | undefined (NULL = undefined). */
typedef qa_q1_base_monster *(*qa_q1am_chthon_lookup_fn)(void *ctx, qa_q1_actor *entity);
void qa_q1am_register_chthon(qa_q1ad_context *context, qa_q1am_chthon_lookup_fn chthon, void *chthon_ctx);
/* C ownership (TS GC): frees the game's registerChthon closures. */
void qa_q1am_release_chthon(qa_q1_services *game);

#endif
