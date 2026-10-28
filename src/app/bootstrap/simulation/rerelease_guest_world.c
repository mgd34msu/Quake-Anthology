/* Port of src/app/bootstrap/simulation/rerelease-guest-world.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_sim_rerelease_guest_world {
  double frame_milliseconds;
  qa_app_sim_rerelease_guest_services *services;
  qa_app_sim_rerelease_guest_execution execution;
  const char *stage; /* "new"|"ready"|"spawning"|"active"|"saving"|"restoring"|"map-restoring" */
};
static void require_module(qa_app_sim_rerelease_guest_world *w) {
  if (!w->execution.loaded(w->execution.ctx)) qa_range_error("Rerelease guest module is not loaded");
}
qa_app_sim_rerelease_guest_world *qa_app_sim_rerelease_guest_world_create(qa_app_sim_rerelease_guest_world_options options) {
  qa_app_sim_rerelease_guest_world *w = calloc(1, sizeof *w);
  if (!w) qa_range_error("Out of memory");
  w->frame_milliseconds = options.frame_milliseconds;
  w->services = options.services;
  w->execution = options.execution;
  w->stage = "new";
  return w;
}
void qa_app_sim_rerelease_guest_world_destroy(qa_app_sim_rerelease_guest_world *world) { free(world); }
void qa_app_sim_rerelease_guest_world_init(qa_app_sim_rerelease_guest_world *world) {
  if (strcmp(world->stage, "new") != 0) qa_range_error("Rerelease guest world is already initialized");
  world->stage = "ready";
}
void qa_app_sim_rerelease_guest_world_spawn(qa_app_sim_rerelease_guest_world *world, const char *entities) {
  if (strcmp(world->stage, "ready") != 0) qa_range_error("Rerelease guest world cannot accept entities now");
  require_module(world);
  world->stage = "spawning";
  world->execution.read_entities(world->execution.ctx, entities);
}
double qa_app_sim_rerelease_guest_world_frame_milliseconds(qa_app_sim_rerelease_guest_world *world) { return world->frame_milliseconds; }
bool qa_app_sim_rerelease_guest_world_is_loading(qa_app_sim_rerelease_guest_world *world) {
  const char *s = world->stage;
  return strcmp(s, "spawning") == 0 || strcmp(s, "saving") == 0 || strcmp(s, "restoring") == 0 || strcmp(s, "map-restoring") == 0;
}
void qa_app_sim_rerelease_guest_world_finish_spawn(qa_app_sim_rerelease_guest_world *world) {
  if (strcmp(world->stage, "spawning") != 0) qa_range_error("Rerelease guest world is not spawning");
  qa_app_sim_rerelease_guest_services_complete_spawn(world->services);
  world->stage = "active";
}
static void advance_dispatch(qa_app_sim_rerelease_guest_world *world, qa_frame_context frame) {
  (void)frame;
  if (strcmp(world->stage, "active") == 0) { world->execution.run_frame(world->execution.ctx); return; }
  if (strcmp(world->stage, "saving") == 0) {
    /* Saving: the source owns checkpoint admission through its own
     * module frame; run one frame to let it reach the source gate. */
    world->execution.run_frame(world->execution.ctx);
    return;
  }
  if (strcmp(world->stage, "restoring") == 0 || strcmp(world->stage, "map-restoring") == 0) {
    /* Restoring: source checkpoints are reached through RunFrame. */
    world->execution.run_frame(world->execution.ctx);
    return;
  }
  qa_range_error("Rerelease guest world cannot advance in this phase");
}
void qa_app_sim_rerelease_guest_world_advance(qa_app_sim_rerelease_guest_world *world, qa_frame_context frame) {
  advance_dispatch(world, frame);
}
void qa_app_sim_rerelease_guest_world_save(qa_app_sim_rerelease_guest_world *world, const char *name) {
  if (strcmp(world->stage, "active") != 0) qa_range_error("Rerelease guest world cannot save in this phase");
  world->stage = "saving";
  world->execution.save_game(world->execution.ctx, name);
  world->stage = "active";
  world->execution.run_frame(world->execution.ctx);
}
void qa_app_sim_rerelease_guest_world_restore(qa_app_sim_rerelease_guest_world *world, const char *name) {
  if (strcmp(world->stage, "active") != 0) qa_range_error("Rerelease guest world cannot restore in this phase");
  world->stage = "restoring";
  world->execution.load_game(world->execution.ctx, name);
  world->stage = "active";
}
void qa_app_sim_rerelease_guest_world_restore_map(qa_app_sim_rerelease_guest_world *world, const char *entities) {
  if (strcmp(world->stage, "active") != 0) qa_range_error("Rerelease guest world cannot restore a map in this phase");
  world->stage = "map-restoring";
  world->execution.read_entities(world->execution.ctx, entities);
  qa_app_sim_rerelease_guest_services_complete_spawn(world->services);
  world->stage = "active";
}
void qa_app_sim_rerelease_guest_world_restart(qa_app_sim_rerelease_guest_world *world) {
  world->execution.restart(world->execution.ctx);
  world->stage = "ready";
}
