/* Port of src/content/q2/missionpacks/weapons/player.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_WEAPONS_PLAYER_H
#define QA_CONTENT_Q2_MISSIONPACKS_WEAPONS_PLAYER_H
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/content/q2/missionpacks/projectiles/trap.h"
#include "qa/content/q2/missionpacks/types.h"

/* TS Q2MissionPackWeapons. defs/ext_ctxs hold the (possibly rerelease-
 * adjusted) definition copies and per-extension closure captures from
 * the latest register() call. */
typedef struct qa_q2mp_weapons_ext_ctx {
  struct qa_q2mp_weapons *owner;
  const qa_q2w_definition *definition;
} qa_q2mp_weapons_ext_ctx;
typedef struct qa_q2mp_weapons {
  qa_q2mp_projectiles *projectiles;
  /* register() appends (rerelease registers both packs: 3 + 6). */
  qa_q2w_definition defs[12];
  qa_q2mp_weapons_ext_ctx ext_ctxs[12];
  size_t def_count;
} qa_q2mp_weapons;

void qa_q2mp_weapons_init(qa_q2mp_weapons *weapons, qa_q2mp_projectiles *projectiles);
/* TS register(weapons, pack, edition = "classic"). */
void qa_q2mp_weapons_register(qa_q2mp_weapons *weapons, qa_q2w_weapons *target, qa_q2mp_pack pack, qa_q2f_edition edition);
#endif
