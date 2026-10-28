/* Port of src/app/bootstrap/simulation/rerelease-guest-world.ts.
 * Guest module execution runs through the execution bundle; frame
 * phases, checkpoint admission and save/restore sequencing are here.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_WORLD_H
#include <stdbool.h>
#include "qa/app/bootstrap/simulation/rerelease_guest_services.h"
#include "qa/contracts/time.h"
typedef struct qa_app_sim_rerelease_guest_world qa_app_sim_rerelease_guest_world;
typedef qa_app_sim_rerelease_guest_world qa_app_sim_rerelease_guest_map;
typedef struct qa_app_sim_rerelease_guest_execution {
  void *ctx;
  /* The loaded guest module (compat opaque); non-NULL after load. */
  bool (*loaded)(void *ctx);
  void (*run_frame)(void *ctx);
  void (*read_entities)(void *ctx, const char *entities);
  void (*restart)(void *ctx);
  void (*save_game)(void *ctx, const char *name);
  void (*load_game)(void *ctx, const char *name);
} qa_app_sim_rerelease_guest_execution;
typedef struct qa_app_sim_rerelease_guest_world_options {
  double frame_milliseconds;
  qa_app_sim_rerelease_guest_services *services;
  qa_app_sim_rerelease_guest_execution execution;
} qa_app_sim_rerelease_guest_world_options;
typedef qa_app_sim_rerelease_guest_world_options qa_app_sim_rerelease_guest_map_options;
qa_app_sim_rerelease_guest_world *qa_app_sim_rerelease_guest_world_create(qa_app_sim_rerelease_guest_world_options options);
void qa_app_sim_rerelease_guest_world_destroy(qa_app_sim_rerelease_guest_world *world);
void qa_app_sim_rerelease_guest_world_init(qa_app_sim_rerelease_guest_world *world);
void qa_app_sim_rerelease_guest_world_spawn(qa_app_sim_rerelease_guest_world *world, const char *entities);
double qa_app_sim_rerelease_guest_world_frame_milliseconds(qa_app_sim_rerelease_guest_world *world);
bool qa_app_sim_rerelease_guest_world_is_loading(qa_app_sim_rerelease_guest_world *world);
void qa_app_sim_rerelease_guest_world_finish_spawn(qa_app_sim_rerelease_guest_world *world);
void qa_app_sim_rerelease_guest_world_advance(qa_app_sim_rerelease_guest_world *world, qa_frame_context frame);
void qa_app_sim_rerelease_guest_world_save(qa_app_sim_rerelease_guest_world *world, const char *name);
void qa_app_sim_rerelease_guest_world_restore(qa_app_sim_rerelease_guest_world *world, const char *name);
void qa_app_sim_rerelease_guest_world_restore_map(qa_app_sim_rerelease_guest_world *world, const char *entities);
void qa_app_sim_rerelease_guest_world_restart(qa_app_sim_rerelease_guest_world *world);
#endif
