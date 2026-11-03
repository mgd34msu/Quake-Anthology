/* Port of src/content/q2/missionpacks/index.ts (umbrella). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_H
#define QA_CONTENT_Q2_MISSIONPACKS_H
#include "qa/content/q2/missionpacks/damage.h"
#include "qa/content/q2/missionpacks/doppleganger.h"
#include "qa/content/q2/missionpacks/entities.h"
#include "qa/content/q2/missionpacks/items.h"
#include "qa/content/q2/missionpacks/modes.h"
#include "qa/content/q2/missionpacks/players.h"
#include "qa/content/q2/missionpacks/projectiles.h"
#include "qa/content/q2/missionpacks/random_items.h"
#include "qa/content/q2/missionpacks/spheres.h"
#include "qa/content/q2/missionpacks/types.h"
#include "qa/content/q2/missionpacks/weapons/definitions.h"
#include "qa/content/q2/missionpacks/weapons/player.h"

/* TS Q2MissionPackArmoryHooks: projectile hooks (base supplied from
 * weapons) + sphere hooks + session authorities, one shared ctx as in
 * the single TS hooks object. */
typedef struct qa_q2mp_armory_hooks {
  qa_q2w_weapons *weapons;
  qa_q2_item_module *items; /* TS Q2ItemModule (borrowed) */
  bool strong_mines;
  void *gravity_ctx;
  double (*gravity)(void *ctx); /* optional */
  bool hunter_camera;
  void *ctx;
  struct qa_q2m_context *(*monster)(void *ctx, qa_actor_id actor);
  void (*player_effect)(void *ctx, qa_q2mp_player_effect effect);
  bool (*intermission)(void *ctx);
} qa_q2mp_armory_hooks;

/* TS registerQ2MissionPackArmory return value (caller storage). */
typedef struct qa_q2mp_armory {
  qa_q2mp_projectiles projectiles;
  qa_q2mp_weapons weapons;
  qa_q2mp_spheres spheres;
  qa_q2mp_items items;
  qa_q2mp_doppleganger doppleganger;
} qa_q2mp_armory;

/* TS registerQ2MissionPackArmory(pack, hooks, edition = "classic").
 * Fills *out (inited in place); destroy items with qa_q2mp_items_destroy
 * via qa_q2mp_armory_destroy when done. */
void qa_q2mp_register_armory(qa_q2mp_pack pack, qa_q2mp_armory_hooks hooks, qa_q2_edition edition, qa_q2mp_armory *out);
void qa_q2mp_armory_destroy(qa_q2mp_armory *armory);
#endif
