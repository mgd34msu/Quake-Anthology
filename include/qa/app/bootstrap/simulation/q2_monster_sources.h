/* Port of src/app/bootstrap/simulation/q2-monster-sources.ts. Module
 * construction runs through the factory bundle (landed content
 * classes are built by the caller); pack/edition selection, module
 * ordering, and callback registration sequencing are this file's.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q2_MONSTER_SOURCES_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q2_MONSTER_SOURCES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
typedef struct qa_app_sim_q2_monster_pack { const char *pack; const void *monsters; const void *source; } qa_app_sim_q2_monster_pack;
typedef struct qa_app_sim_selected_q2_monster_modules {
  const void *game, *monsters, *ballistics, *movers;
  qa_app_sim_q2_monster_pack packs[2]; size_t pack_count;
} qa_app_sim_selected_q2_monster_modules;
typedef struct qa_app_sim_q2_monster_factory {
  void *ctx;
  const void *(*create_ballistics)(void *ctx, const void *weapons,
      void (*noise)(void *noise_ctx, qa_actor_id actor, qa_vec3 origin, bool secondary), void *noise_ctx);
  const void *(*create_monsters)(void *ctx, const void *ballistics, const void *behavior);
  void (*report_noise)(void *ctx, const void *monsters, qa_actor_id actor, qa_vec3 origin, bool secondary);
  const void *(*register_base)(void *ctx, const void *monsters);
  const void *(*create_movers)(void *ctx, const void *monsters);
  const void *(*create_projectiles)(void *ctx, const void *ballistics, const void *monsters);
  qa_app_sim_q2_monster_pack (*register_pack)(void *ctx, const void *monsters, const char *pack, const void *projectiles, const char *edition);
  const void *(*register_rerelease)(void *ctx, const void *monsters, const void *source, const void *projectiles, const char *expansion);
  const void *(*create_game)(void *ctx, const void *monsters, const void *const *modules, size_t module_count);
  void (*register_ballistics_callbacks)(void *ctx, const void *ballistics, const void *game);
  void (*register_source_callbacks)(void *ctx, const void *game, const void *callbacks);
  const void *(*projectile_callbacks)(void *ctx, const void *projectiles);
  const void *(*mover_callbacks)(void *ctx, const void *movers);
} qa_app_sim_q2_monster_factory;
typedef struct qa_app_sim_q2_monster_options {
  const char *edition;  /* Q2Edition */
  const char *program;  /* "baseq2"|"xatrix"|"rogue"|"mg2" */
  const void *weapons;  /* Omit<Q2WeaponHooks,"noise"> */
  const void *behavior;
} qa_app_sim_q2_monster_options;
qa_app_sim_selected_q2_monster_modules qa_app_sim_create_selected_q2_monster_modules(
    qa_app_sim_q2_monster_options options, qa_app_sim_q2_monster_factory factory);
#endif
