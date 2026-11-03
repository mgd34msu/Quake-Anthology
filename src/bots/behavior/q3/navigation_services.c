/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/navigation_services.h"
#include <string.h>
#include "qa/core/math.h"
typedef qa_q3_bot_navigation_bots_a services;
static qa_navigation_runtime *for_client(void *ctx, int64_t client) {
  services *s = ctx;
  return s->selected.for_client(s->selected.ctx, client);
}
static qa_bot_travel_prediction_result predict(void *ctx, qa_bot_movement_prediction query) {
  services *s = ctx;
  return s->selected.predict_client_movement(s->selected.ctx, query);
}
static bool travel_weapon(void *ctx, int64_t client, const char *mode, int64_t *out) {
  services *s = ctx;
  return s->selected.travel_weapon(s->selected.ctx, client, mode, out);
}
static double time_fn(void *ctx) {
  services *s = ctx;
  return s->game->clock_time(s->game->ctx) / 1000;
}
static int64_t point_contents(void *ctx, qa_vec3 point) {
  services *s = ctx;
  return s->game->point_contents(s->game->ctx, point, -1);
}
static qa_bot_server_trace_result trace(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                                        int64_t pass_entity, int64_t mask) {
  services *s = ctx;
  qa_q3_base_server_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start;
  query.end = end;
  query.pass_entity_num = (double)pass_entity;
  query.mask = (double)mask;
  query.shape.kind = bounds == NULL ? QA_Q3_BASE_SHAPE_POINT : QA_Q3_BASE_SHAPE_BOX;
  if (bounds != NULL) {
    query.shape.mins = bounds->min;
    query.shape.maxs = bounds->max;
  }
  qa_q3_base_server_trace_result r = s->game->trace(s->game->ctx, query);
  qa_bot_server_trace_result out;
  memset(&out, 0, sizeof out);
  out.fraction = r.fraction;
  out.end = r.end;
  out.entity_num = r.entity_num;
  out.solidity = r.solidity == QA_Q3_BASE_SOLID_CLEAR         ? "clear"
                 : r.solidity == QA_Q3_BASE_SOLID_START_SOLID ? "start-solid"
                                                              : "all-solid";
  out.has_plane = r.contact_is_plane;
  out.plane = r.contact_plane;
  out.contents = r.contents;
  out.surface_flags = r.surface_flags;
  return out;
}
static int64_t entity_model_index(void *ctx, int64_t number) {
  services *s = ctx;
  return s->game->entity(s->game->ctx, number).state.modelindex;
}
static int64_t entity_type(void *ctx, int64_t number) {
  services *s = ctx;
  return s->game->entity(s->game->ctx, number).state.e_type;
}
static int64_t entity_weapon(void *ctx, int64_t number) {
  services *s = ctx;
  return s->game->entity(s->game->ctx, number).state.weapon;
}
static int64_t next_entity(void *ctx, int64_t after) {
  services *s = ctx;
  for (int64_t slot = after + 1; slot < s->game->entity_count(s->game->ctx); slot++)
    if (s->game->entity(s->game->ctx, slot).present)
      return slot;
  return 0;
}
static bool model_bounds(void *ctx, int64_t model, qa_bounds *out) {
  services *s = ctx;
  int64_t count = s->game->entity_count(s->game->ctx);
  for (int64_t slot = 0; slot < count; slot++) {
    qa_bot_observed_entity_bots_a entity = s->game->entity(s->game->ctx, slot);
    if (entity.present && entity.has_inline_model && entity.inline_model == model) {
      out->min = qa_add3(entity.origin, entity.bounds.min);
      out->max = qa_add3(entity.origin, entity.bounds.max);
      return true;
    }
  }
  return false;
}
static bool model_info(void *ctx, int64_t model, qa_bot_travel_model *out) {
  services *s = ctx;
  int64_t count = s->game->entity_count(s->game->ctx);
  for (int64_t slot = 0; slot < count; slot++) {
    qa_bot_observed_entity_bots_a entity = s->game->entity(s->game->ctx, slot);
    if (!entity.present || !entity.has_inline_model || entity.inline_model != model)
      continue;
    const char *name = entity.classname != NULL ? entity.classname : "";
    out->entity = (double)slot;
    out->origin = entity.origin;
    out->bounds = entity.bounds;
    out->kind = strcmp(name, "func_plat") == 0      ? "elevator"
                : strcmp(name, "func_bobbing") == 0 ? "bobbing"
                : strcmp(name, "func_door") == 0    ? "door"
                : strcmp(name, "func_train") == 0   ? "train"
                                                    : "static";
    return true;
  }
  return false;
}
void qa_q3_bot_navigation_bots_a_init(qa_q3_bot_navigation_bots_a *out, qa_source_bot_game_bots_a *game,
                                      qa_bot_library_bots_a *library,
                                      const qa_selected_bot_navigation_bots_a *selected,
                                      qa_aas_bsp_entities *bsp_entities) {
  memset(out, 0, sizeof *out);
  out->game = game;
  out->library = library;
  out->selected = *selected;
  qa_source_bot_navigation_host_bots_a host;
  memset(&host, 0, sizeof host);
  host.ctx = out;
  host.runtime = selected->runtime;
  host.bsp_entities = bsp_entities;
  host.for_client = for_client;
  host.move_states = &library->move_states;
  host.actions = &library->actions;
  host.random = library->options.random;
  host.crouched_bounds = selected->crouched_bounds;
  host.time = time_fn;
  host.point_contents = point_contents;
  host.entity_model_index = entity_model_index;
  host.trace = trace;
  host.predict_client_movement = predict;
  host.model_bounds = model_bounds;
  host.model_info = model_info;
  host.next_entity = next_entity;
  host.entity_type = entity_type;
  host.entity_weapon = entity_weapon;
  host.travel_weapon = travel_weapon;
  qa_source_bot_navigation_bots_a_init(&out->navigation, &host);
}
