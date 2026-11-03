/* Port of src/app/bootstrap/simulation/q2-monster-sources.ts: the Q2
 * monster modules a selected-monster source runs (ballistics, monsters,
 * base and mission-pack registrations, movers, projectiles, and the
 * rerelease registration) over a game the caller creates.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q2_MONSTER_SOURCES_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q2_MONSTER_SOURCES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q2/base/monsters/index.h"
#include "qa/content/q2/foundation/entity_services.h"
#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/foundation/movers.h"
#include "qa/content/q2/foundation/weapons/ballistics.h"
#include "qa/content/q2/missionpacks/monsters/index.h"
#include "qa/content/q2/missionpacks/projectiles/trap.h"
#include "qa/content/q2/missionpacks/types.h"
#include "qa/content/q2/rerelease/monsters/index.h"

/* TS createSelectedQ2MonsterModules options. weapons is Omit<Q2WeaponHooks,
 * "noise"> and keeps weapons.ctx; behavior keeps its own ctx; the other
 * callbacks share ctx. */
typedef struct qa_app_sim_q2_monster_options {
  qa_q2_edition edition;
  const char *program; /* "baseq2" | "xatrix" | "rogue" | "mg2" */
  qa_q2w_hooks weapons;
  qa_q2m_monster_hooks behavior;
  void *ctx;
  qa_q2_entity_services *(*create_game)(void *ctx, qa_q2m_monsters *monsters, const qa_q2_spawn_module *modules, size_t module_count);
  double (*gravity)(void *ctx);
  qa_q2mpm_powerups (*powerups)(void *ctx, qa_actor_id actor);
  void (*player_effect)(void *ctx, qa_q2mp_player_effect event);
} qa_app_sim_q2_monster_options;

/* TS SelectedQ2MonsterModules.packs row. */
typedef struct qa_app_sim_q2_monster_pack { const char *pack; qa_q2mpm_monsters_module monsters; } qa_app_sim_q2_monster_pack;
/* TS SelectedQ2MonsterModules { game, monsters, ballistics, movers, packs }
 * plus the C storage of the modules TS keeps alive by reference. Does not
 * move once created. */
typedef struct qa_app_sim_selected_q2_monster_modules {
  qa_q2_entity_services *game; /* created by options.create_game; owned by the caller */
  qa_q2m_monsters monsters;
  qa_q2w_ballistics ballistics;
  qa_q2_mover_module *movers;
  qa_app_sim_q2_monster_pack packs[2]; size_t pack_count;
  qa_app_sim_q2_monster_options options;
  qa_q2bm_classic_base_module base;
  qa_q2mp_projectiles projectiles;
  bool has_rerelease; qa_q2rm_monsters_module rerelease;
  qa_q2_spawn_module modules[4]; size_t module_count;
} qa_app_sim_selected_q2_monster_modules;

qa_app_sim_selected_q2_monster_modules *qa_app_sim_create_selected_q2_monster_modules(qa_app_sim_q2_monster_options options);
/* Frees the modules; the caller destroys modules->game first. */
void qa_app_sim_selected_q2_monster_modules_destroy(qa_app_sim_selected_q2_monster_modules *modules);
#endif
