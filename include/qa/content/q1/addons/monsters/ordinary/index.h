/* Port of src/content/q1/addons/monsters/ordinary/index.ts.
 * quakec_{mg1,mg3}/monsters native monster admission. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_INDEX_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_INDEX_H

#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/monsters/ai/controller.h"

/* TS class Q1OrdinaryMonster extends Mg3Monster. */
typedef struct qa_q1am_ordinary_monster {
  qa_q1am_mg3_monster base;
  qa_q1ad_context *context;
  qa_q1_base_monster_species spec; /* TS sourceSpec(context, entity) */
} qa_q1am_ordinary_monster;
/* TS class Q1DopaOrdinaryMonster extends BaseMonster (module-private in TS). */
typedef struct qa_q1am_dopa_ordinary_monster {
  qa_q1_base_monster base;
  qa_q1ad_context *context;
  qa_q1_base_monster_species spec;
} qa_q1am_dopa_ordinary_monster;

/* TS new Q1OrdinaryMonster(context, entity), in place. False only on OOM. */
bool qa_q1am_ordinary_monster_init(qa_q1am_ordinary_monster *monster, qa_q1ad_context *context, qa_q1_actor *entity);
/* instanceof Q1OrdinaryMonster. */
bool qa_q1am_is_ordinary_monster(const qa_q1_base_monster *monster);
/* TS ordinaryAddonMonster: stores.get(context)?.get(entity.actor) ?? context.base.monsters.get(entity.actor);
 * NULL = undefined. */
qa_q1_base_monster *qa_q1am_ordinary_addon_monster(qa_q1ad_context *context, qa_q1_actor *entity);
void qa_q1am_register_ordinary_addon_monsters(qa_q1ad_context *context);
/* C ownership (TS WeakMap entry dies with the context): frees the ordinary
 * stores of the game's contexts. Call after the last use of the game. */
void qa_q1am_release_ordinary_addon_monsters(qa_q1_services *game);

#endif
