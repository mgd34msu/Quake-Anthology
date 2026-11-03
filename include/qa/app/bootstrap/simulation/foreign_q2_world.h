/* Port of src/app/bootstrap/simulation/foreign-q2-world.ts: a full Q2 world
 * (entities, targets, movers, scenery, items, monsters) hosted as an adjunct
 * for a foreign-authored map whose primary source is another family's
 * program. Player/match ownership stays with the primary source, so
 * player-facing hooks route to the caller's shared runtime services.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_FOREIGN_Q2_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_FOREIGN_Q2_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q2/base/entities/index.h"
#include "qa/content/q2/base/monsters/index.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/foundation/movers.h"
#include "qa/content/q2/foundation/runtime.h"
#include "qa/content/q2/foundation/weapons/ballistics.h"
#include "qa/content/q2/missionpacks/entities/index.h"
#include "qa/content/q2/missionpacks/monsters/index.h"
#include "qa/content/q2/missionpacks/projectiles/trap.h"
#include "qa/content/q2/missionpacks/types.h"
#include "qa/content/q2/rerelease/monsters/index.h"

/* TS createForeignQ2WorldModules options. weapons is Omit<Q2WeaponHooks,
 * "noise">: its noise leaf is ignored (the world reports noise to its
 * monsters); its other leaves are called with weapons.ctx. behavior and
 * item_hooks keep their own ctx; the remaining callbacks share ctx. */
typedef struct qa_app_sim_foreign_q2_world_options {
  qa_q2_edition edition;
  const char *program; /* "baseq2" | "xatrix" | "rogue" | "mg2" */
  qa_q2w_hooks weapons;
  qa_q2m_monster_hooks behavior;
  qa_q2_item_hooks item_hooks;
  void *ctx;
  qa_q2_foundation *(*create_game)(void *ctx, qa_q2m_monsters *monsters, const qa_q2_spawn_module *modules, size_t module_count);
  double (*gravity)(void *ctx);
  qa_q2mpm_powerups (*powerups)(void *ctx, qa_actor_id actor);
  void (*player_effect)(void *ctx, qa_q2mp_player_effect event);
  void (*teleport_player)(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles);
  void (*player_push)(void *ctx, qa_actor_id actor, qa_vec3 velocity);
  void (*set_actor_gravity)(void *ctx, qa_actor_id actor, double gravity);
} qa_app_sim_foreign_q2_world_options;

typedef struct qa_app_sim_foreign_q2_world_modules qa_app_sim_foreign_q2_world_modules;
/* TS ForeignQ2WorldModules.packs row. */
typedef struct qa_app_sim_foreign_q2_world_pack {
  const char *pack; /* "xatrix" | "rogue" */
  qa_q2mpm_monsters_module monsters;
  qa_q2mp_entities entities;
  /* C: the TS entities.callbacks the game registered, and the hook ctx. */
  qa_q2_callback_definitions entity_callbacks;
  struct { qa_app_sim_foreign_q2_world_modules *world; size_t index; } hooks;
} qa_app_sim_foreign_q2_world_pack;

/* TS ForeignQ2WorldModules (game, monsters, ballistics, movers, items,
 * baseEntities, packs) plus the C storage of the modules TS keeps alive by
 * reference. Does not move once created. */
struct qa_app_sim_foreign_q2_world_modules {
  qa_q2_foundation *game; /* created by options.create_game; owned by the caller */
  qa_q2m_monsters monsters;
  qa_q2w_ballistics ballistics;
  qa_q2_mover_module *movers;
  qa_q2_item_module *items;
  qa_q2_base_entity_module *base_entities;
  qa_app_sim_foreign_q2_world_pack packs[2];
  size_t pack_count;
  qa_app_sim_foreign_q2_world_options options;
  qa_q2bm_classic_base_module base;
  qa_q2mp_projectiles projectiles;
  bool has_rerelease;
  qa_q2rm_monsters_module rerelease;
  qa_q2_spawn_module modules[16];
  size_t module_count;
};

/* TS createForeignQ2WorldModules. */
qa_app_sim_foreign_q2_world_modules *qa_app_sim_create_foreign_q2_world_modules(qa_app_sim_foreign_q2_world_options options);
/* Frees the modules; the caller destroys world->game first. */
void qa_app_sim_foreign_q2_world_modules_destroy(qa_app_sim_foreign_q2_world_modules *world);
#endif
