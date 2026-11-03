/* Port of src/content/q1/addons/monsters/bosses/registry.ts. GPL-2.0-or-later.
 * The returned registry is TS ReadonlyMap<OwnedActor, T>; T is any
 * qa_q1_base_monster subclass (create returns the embedded base). */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_REGISTRY_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_REGISTRY_H

#include "qa/content/q1/addons/context.h"

typedef struct qa_q1am_boss_registry qa_q1am_boss_registry;
/* TS create: (entity: Q1Actor) => T. NULL only on OOM. */
typedef qa_q1_base_monster *(*qa_q1am_boss_create_fn)(void *ctx, qa_q1_actor *entity);

qa_q1am_boss_registry *qa_q1am_register_boss_controllers(qa_q1ad_context *context, const char *prefix,
                                                         const char *classname, qa_q1am_boss_create_fn create,
                                                         void *create_ctx);
qa_q1_base_monster *qa_q1am_require_boss(qa_q1am_boss_registry *monsters, qa_q1_actor *entity);
/* TS monsters.get(actor): NULL = undefined. */
qa_q1_base_monster *qa_q1am_boss_registry_get(const qa_q1am_boss_registry *monsters, qa_owned_actor actor);
/* The boss modules' TS loop `for (frame of frames.values()) for (op of
 * frame.operations) if (op.kind === "action") actions.set(op.name, closure)`:
 * one entry per distinct action name in Map order, entry ctx = the op name
 * (const char *) that the closure captures. Static storage, built once per
 * frame table and kept for the process (the tables are static). */
const qa_q1_monster_action_entry *qa_q1am_frame_actions(const qa_q1_monster_frame_table *frames,
                                                        void (*fn)(void *ctx, qa_q1_base_monster *monster),
                                                        size_t *count);
/* C ownership (TS GC): frees every boss registry of the game and the
 * monsters it holds or retired. Call after the last use of the game. */
void qa_q1am_release_boss_controllers(qa_q1_services *game);

#endif
