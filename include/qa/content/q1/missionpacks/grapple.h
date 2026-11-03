/* Port of src/content/q1/missionpacks/grapple.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_GRAPPLE_H
#define QA_CONTENT_Q1_MISSIONPACKS_GRAPPLE_H
#include "qa/content/q1/missionpacks/types.h"

/* TS RogueGrapple. Must not move after init: the game borrows the weapon
 * definition and player extension, and the callbacks receive the struct. */
/* One entry of the TS hooks Map (key `${slot}:${generation}` of the owner).
 * hook is borrowed: the game keeps released entities until it is freed. */
typedef struct qa_q1mp_grapple_hook { int64_t slot, generation; qa_q1_actor *hook; } qa_q1mp_grapple_hook;
typedef struct qa_q1mp_rogue_grapple {
  qa_q1_services *game;
  qa_q1_weapon_definition weapon;
  qa_q1_player_extension extension;
  /* TS hooks: live hooks by owner; indexed: the entity table was scanned once. */
  qa_q1mp_grapple_hook *hooks; size_t hook_count, hook_capacity;
  bool indexed;
} qa_q1mp_rogue_grapple;
void qa_q1mp_rogue_grapple_init(qa_q1mp_rogue_grapple *grapple, qa_q1_services *game);
/* Frees the hooks index (TS GC). */
void qa_q1mp_rogue_grapple_destroy(qa_q1mp_rogue_grapple *grapple);

#endif
