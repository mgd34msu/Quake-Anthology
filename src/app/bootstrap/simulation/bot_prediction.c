/* Port of src/app/bootstrap/simulation/bot-prediction.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_prediction.h"
#include <string.h>
#include "qa/bots/navigation/aas_prediction_stop.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

enum { PHYS_MAXSTEP = 18, PHYS_MAXBARRIER = 32, SE_GAP = 64, SE_HITBOUNDINGBOX = 2048 };

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static qa_app_sim_locomotion_player locomotion(const qa_app_sim_movement_prediction_player *player) {
  qa_app_sim_locomotion_player out;
  memset(&out, 0, sizeof out);
  out.profile = player->profile; out.standing_bounds = player->standing_bounds;
  out.has_source_movement = player->has_source_movement; out.source_movement = player->source_movement;
  out.character = player->character; out.world_gravity = player->world_gravity;
  out.has_q2_movement_config = player->has_q2_movement_config; out.q2_movement_config = player->q2_movement_config;
  out.flight = player->flight;
  return out;
}
static bool liquid_below(const qa_app_sim_bot_simulation *simulation, const qa_app_sim_movement_prediction_player *player,
    qa_numeric_profile numeric, qa_vec3 point) {
  qa_point_contents_query query;
  memset(&query, 0, sizeof query);
  query.point = point; /* target: world */
  query.policy = qa_app_sim_player_trace_policy(locomotion(player)); query.numeric = numeric; query.pass_actor = NULL;
  qa_point_contents_result sample = qa_shared_point_contents(simulation->scene, query);
  if (is(sample.kind, "q1")) return sample.contents == -3;
  return (qa_js_to_int32(is(sample.kind, "q2") ? sample.merged : sample.contents) & 32) != 0;
}
/* AAS_PredictClientMovement's SE_GAP probe: a crouch-box trace below the origin that finds no floor within a step. */
static bool at_gap(const qa_app_sim_bot_simulation *simulation, const qa_app_sim_movement_prediction_player *player,
    qa_numeric_profile numeric, qa_vec3 origin) {
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = origin; query.end = origin; query.end.z = origin.z - (48 + PHYS_MAXBARRIER);
  query.shape.kind = "box"; query.shape.bounds = qa_app_sim_player_crouched_bounds(locomotion(player));
  query.policy = qa_app_sim_player_trace_policy(locomotion(player)); query.numeric = numeric; query.pass_actor = NULL;
  qa_trace_result trace = qa_shared_trace(simulation->scene, query);
  return !trace.start_solid && trace.end.z < origin.z - PHYS_MAXSTEP - 1 && !liquid_below(simulation, player, numeric, trace.end);
}
static bool boxes_overlap(qa_bounds a, qa_bounds b) {
  return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

typedef struct projection_ctx {
  const qa_app_sim_bot_simulation *simulation;
  const qa_app_sim_movement_prediction_player *player;
  qa_bot_movement_prediction query;
  qa_numeric_profile numeric;
  qa_app_sim_player_movement_prediction *projection;
  qa_navigation_runtime *runtime;
  const qa_aas_asset *asset; /* TS asset when kind "aas", else NULL */
  const qa_aas_asset *navigation_asset;
} projection_ctx;

/* TS areaAt; false is TS null. */
static bool area_at(projection_ctx *p, qa_vec3 origin, int64_t *out) {
  if (p->runtime != NULL) return qa_navigation_runtime_area_at(p->runtime, origin, out);
  *out = qa_aas_point_area(p->navigation_asset, origin);
  return true;
}
static qa_movement_input projection_input(void *ctx, const qa_movement_result *previous, int64_t index, qa_vec3 command) {
  projection_ctx *p = ctx;
  if (p->query.presence == 4) command.z = -400;
  return qa_app_sim_player_movement_prediction_input(p->projection, previous, (double)index, command);
}
static qa_bot_movement_stop projection_stop(void *ctx, const qa_movement_result *previous, const qa_movement_result *result, int64_t frame) {
  projection_ctx *p = ctx;
  if (!is(result->status, "active")) qa_raise("Error", 0, 0, "Navigation projection removed its actor");
  qa_app_sim_movement_observation observation = qa_app_sim_movement_observation_of(*result);
  bool grounded = observation.grounded;
  bool previous_active = previous != NULL && is(previous->status, "active");
  if (p->asset != NULL) {
    qa_vec3 start = previous_active ? qa_app_sim_movement_origin(previous->state) : p->query.origin;
    qa_bot_movement_stop crossing;
    if (qa_aas_prediction_stop(p->asset, start, observation.origin, frame, p->query.stop_events, p->query.stop_area, &crossing)) return crossing;
  }
  bool was_grounded = previous_active ? qa_app_sim_movement_observation_of(*previous).grounded : p->query.on_ground;
  /* be_aas_move.c reports ground events from the current state, not from a change since the last frame. */
  int64_t flags = grounded ? 1 : 2;
  if (!is(observation.medium, "dry")) flags |= is(observation.medium, "slime") ? 8 : is(observation.medium, "lava") ? 16 : 4;
  int64_t area = 0;
  if (p->query.stop_area != 0 && area_at(p, observation.origin, &area) && area == p->query.stop_area)
    flags |= 512 | (!was_grounded && grounded ? 1024 : 0);
  if (observation.damaging_fall) flags |= 32;
  if ((p->query.stop_events & SE_GAP) != 0 && at_gap(p->simulation, p->player, p->numeric, observation.origin)) flags |= SE_GAP;
  if ((p->query.stop_events & SE_HITBOUNDINGBOX) != 0 && p->query.has_hit_box) {
    qa_vec3 o = observation.origin;
    qa_bounds box = {{o.x + result->bounds.min.x, o.y + result->bounds.min.y, o.z + result->bounds.min.z},
        {o.x + result->bounds.max.x, o.y + result->bounds.max.y, o.z + result->bounds.max.z}};
    if (boxes_overlap(box, p->query.hit_box)) flags |= SE_HITBOUNDINGBOX;
  }
  qa_bot_movement_stop out;
  out.events = flags; out.origin = observation.origin;
  out.has_area = area_at(p, observation.origin, &out.area);
  return out;
}

qa_bot_travel_prediction_result qa_app_sim_predict_application_bot_movement(const qa_app_sim_bot_simulation *simulation,
    const qa_app_sim_movement_prediction_player *player, qa_bot_movement_prediction query,
    qa_navigation_runtime *runtime, const qa_aas_asset *asset) {
  projection_ctx p;
  memset(&p, 0, sizeof p);
  p.simulation = simulation; p.player = player; p.query = query; p.runtime = runtime; p.navigation_asset = asset;
  if (!qa_app_sim_bot_numeric(simulation, &p.numeric)) qa_raise("Error", 0, 0, "Bot prediction has no source numeric policy");
  p.projection = qa_app_sim_create_player_movement_prediction(simulation->prediction(simulation->ctx), *player, query.origin, query.velocity,
      qa_js_round(query.frame_time * 1000), query.presence == 4, NULL);
  if (p.projection == NULL) qa_range_error("Out of memory");
  p.asset = runtime != NULL ? runtime->graph->aas_asset : asset;
  qa_bot_movement_projection_bots_a projection;
  projection.ctx = &p;
  projection.provider = qa_app_sim_player_movement_prediction_provider(p.projection);
  projection.services = qa_app_sim_player_movement_prediction_services(p.projection);
  projection.input = projection_input;
  projection.stop = projection_stop;
  qa_bot_travel_prediction_result result = qa_project_bot_movement(query, projection);
  qa_app_sim_player_movement_prediction_destroy(p.projection);
  return result;
}
