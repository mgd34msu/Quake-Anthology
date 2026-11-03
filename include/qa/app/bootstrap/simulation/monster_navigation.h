/* Port of src/app/bootstrap/simulation/monster-navigation.ts:
 * preloadApplicationMonsterNavigation and ApplicationMonsterNavigation
 * install (MG3 monster path walking over the shared navigation graph).
 *
 * TS takes LoadedApplicationContent and SharedSimulation; the members
 * read are the two views below, typed with the real lower-layer types.
 * Async preload is synchronous. C ownership: install registers with
 * the Q1 game (qa_q1am_register_mg3_monster_navigation); the graphs
 * and per-actor runtimes it hands out (TS Map / WeakMap entries) live
 * until qa_app_sim_application_monster_navigation_destroy, which runs
 * after the game released its registration
 * (qa_q1am_release_mg3_monster_navigation). A world's begin_route
 * prediction stays valid until the next begin_route on that actor's
 * world. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MONSTER_NAVIGATION_H
#define QA_APP_BOOTSTRAP_SIMULATION_MONSTER_NAVIGATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/bots/navigation/load.h"
#include "qa/content/mounts.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/contracts/content.h"
#include "qa/movement/q1/monsters.h"
#include "qa/world/collision/index.h"

/* The LoadedApplicationContent members the preload reads. */
typedef struct qa_app_sim_monster_navigation_content {
  void *ctx;
  const qa_executable_recipe *recipe; /* content.recipe */
  qa_decoded_world world;             /* content.world */
  qa_content_mounted *mounts;         /* content.mounts (read) */
  qa_content_mounted *(*for_content)(void *ctx, qa_content_id content); /* borrowed */
} qa_app_sim_monster_navigation_content;
/* The SharedSimulation members install reads. */
typedef struct qa_app_sim_monster_navigation_simulation {
  void *ctx;
  const qa_executable_recipe *recipe; /* simulation.recipe */
  qa_shared_scene_queries *scene;     /* simulation.scene */
  double (*time_seconds)(void *ctx);  /* simulation.timeSeconds */
  qa_q1_services *(*q1_game)(void *ctx); /* simulation.q1Source()?.game; NULL is TS null */
} qa_app_sim_monster_navigation_simulation;

typedef struct qa_app_sim_application_monster_navigation qa_app_sim_application_monster_navigation;
/* TS preloadApplicationMonsterNavigation: NULL is TS undefined (the map
 * entities are not MG3). content must outlive the result. */
qa_app_sim_application_monster_navigation *qa_app_sim_preload_application_monster_navigation(
    qa_app_sim_monster_navigation_content content);
/* TS install(simulation, movement). simulation and movement are
 * borrowed and must outlive the navigation. */
void qa_app_sim_application_monster_navigation_install(qa_app_sim_application_monster_navigation *navigation,
    qa_app_sim_monster_navigation_simulation simulation, qa_q1_monster_movement *movement);
void qa_app_sim_application_monster_navigation_destroy(qa_app_sim_application_monster_navigation *navigation);
#endif
