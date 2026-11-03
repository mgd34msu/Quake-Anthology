/* Port of src/content/q1/addons/monsters/demodog.ts.
 * quakec_mg3/monsters/mg3_demodog.qc and monsters.qc. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_DEMODOG_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_DEMODOG_H

#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/monsters/ai/controller.h"

/* TS class Q1Demodog extends Mg3Monster. */
typedef struct qa_q1am_demodog {
  qa_q1am_mg3_monster base;
  qa_q1ad_context *context;
} qa_q1am_demodog;

/* TS new Q1Demodog(context, entity), in place. False only on OOM. */
bool qa_q1am_demodog_init(qa_q1am_demodog *dog, qa_q1ad_context *context, qa_q1_actor *entity);
/* instanceof Q1Demodog. */
bool qa_q1am_is_demodog(const qa_q1_base_monster *monster);
void qa_q1am_demodog_jump_touch(qa_q1am_demodog *dog, qa_actor_id other);
/* TS registerMg3Demodog(context): ReadonlyMap<OwnedActor, Q1Demodog>. */
typedef struct qa_q1am_demodog_registry qa_q1am_demodog_registry;
qa_q1am_demodog_registry *qa_q1am_register_mg3_demodog(qa_q1ad_context *context);
qa_q1am_demodog *qa_q1am_demodog_registry_get(const qa_q1am_demodog_registry *monsters, qa_owned_actor actor);
/* C ownership (TS GC): frees the game's demodog registries. */
void qa_q1am_release_mg3_demodog(qa_q1_services *game);

#endif
