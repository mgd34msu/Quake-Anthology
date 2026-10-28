/* Port of src/bots/navigation/aas-reachability-special.ts.
 * Entity, grapple and weapon-jump reachability from id Software's
 * be_aas_reach.c. GPL-2.0-or-later. */
#include "qa/bots/navigation/aas_reachability_special.h"
#include "qa/bots/navigation/aas_reachability_geometry.h"
#include "qa/bots/navigation/aas_reachability_spatial.h"
#include "qa/bots/navigation/aas_reachability_types.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EPAIRKEY 128
#define FACE_SOLID 1
#define FACE_GROUND 4
#define AREA_WEAPONJUMP 8192
#define LAND_EVENTS (QA_AAS_STOP_HIT_GROUND | QA_AAS_STOP_ENTER_WATER | QA_AAS_STOP_ENTER_SLIME | \
                     QA_AAS_STOP_ENTER_LAVA | QA_AAS_STOP_HIT_GROUND_DAMAGE | QA_AAS_STOP_TOUCH_JUMP_PAD | QA_AAS_STOP_TOUCH_TELEPORTER)
#define HAZARD_EVENTS (QA_AAS_STOP_ENTER_SLIME | QA_AAS_STOP_ENTER_LAVA | QA_AAS_STOP_HIT_GROUND_DAMAGE)

static float fr(double v) { return (float)v; }
static qa_vec3 v3f(double x, double y, double z) { return qa_math_vec3(x, y, z); }
static const qa_vec3 ZERO_V = {0, 0, 0}, UP_V = {0, 0, 1}, DOWN_V = {0, 0, -1};
static qa_vec3 with_z(qa_vec3 point, double height) { return v3f(point.x, point.y, height); }
static qa_vec3 ma(qa_vec3 point, double scale, qa_vec3 direction) { return qa_add3(point, qa_scale3(direction, scale)); }
static qa_vec3 middle(qa_vec3 a, qa_vec3 b) { return qa_scale3(qa_add3(a, b), 0.5); }
static double axis_of(qa_vec3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
static int64_t abs64(int64_t v) { return v < 0 ? -v : v; }
static void epair_text(const uint8_t *bytes, size_t length, char *out, size_t out_cap) {
  size_t n = 0;
  while (n < length && bytes[n] != 0 && n + 1 < out_cap) { out[n] = (char)bytes[n]; n++; }
  out[n] = 0;
}
static int64_t source_int(double value) {
  double integer = trunc(value);
  if (!isfinite(integer) || integer < -2147483648.0 || integer > 2147483647.0)
    qa_range_error("AAS special reachability float-to-int conversion is undefined");
  return (int64_t)integer;
}
/* C printf of the stored float (promoted to double, ties to even). */
static void fixed_format(double value, int digits, char *out, size_t out_cap) {
  uint32_t bits = qa_float32_to_bits(value);
  bool negative = (bits >> 31) != 0;
  uint32_t exponent = (bits >> 23) & 255;
  if (exponent == 255) {
    snprintf(out, out_cap, "%s", isnan(value) ? "nan" : negative ? "-inf" : "inf");
    return;
  }
  snprintf(out, out_cap, "%.*f", digits, (double)(float)value);
}
static void perimeter(qa_vec3 min, qa_vec3 mid, qa_vec3 max, qa_vec3 out[8]) {
  out[0] = v3f(min.x, mid.y, 0); out[1] = v3f(mid.x, max.y, 0);
  out[2] = v3f(max.x, mid.y, 0); out[3] = v3f(mid.x, min.y, 0);
  out[4] = v3f(min.x, max.y, 0); out[5] = v3f(max.x, max.y, 0);
  out[6] = v3f(max.x, min.y, 0); out[7] = v3f(min.x, min.y, 0);
}
void qa_aas_reachability_special_init(qa_aas_reachability_special *special, qa_aas_reachability_context *context) {
  special->context = context;
}
static bool area_grounded_s(qa_aas_reachability_special *s, int64_t area) {
  return (qa_aas_at_setting(&s->context->world->asset, area)->flags & 1) != 0;
}
static bool area_swim_s(qa_aas_reachability_special *s, int64_t area) {
  return (qa_aas_at_setting(&s->context->world->asset, area)->flags & 4) != 0;
}
static bool area_teleport_s(qa_aas_reachability_special *s, int64_t area) {
  return (qa_aas_at_setting(&s->context->world->asset, area)->contents & 64) != 0;
}
static bool area_jump_pad_s(qa_aas_reachability_special *s, int64_t area) {
  return (qa_aas_at_setting(&s->context->world->asset, area)->contents & 128) != 0;
}
static void link_reach_s(qa_aas_reachability_special *s, int64_t area, qa_aas_linked_reachability *reach) {
  reach->next = qa_aas_at_head(s->context, area);
  s->context->heads[area] = reach;
}
static void ctx_print(qa_aas_reachability_special *s, int64_t severity, const char *text) {
  qa_aas_reachability_context_print(s->context, severity, text);
}
int64_t qa_aas_reachability_special_travel_flags_for_team(qa_aas_reachability_special *special, int64_t entity) {
  int64_t not_team;
  if (!qa_aas_reachability_entities_int(special->context->bsp_entities, entity, "bot_notteam", &not_team)) return 0;
  return not_team == 1 ? QA_TRAVEL_NOTTEAM1 : not_team == 2 ? QA_TRAVEL_NOTTEAM2 : 0;
}
void qa_aas_reachability_special_teleport(qa_aas_reachability_special *special) {
  qa_aas_reachability_context *c = special->context;
  qa_aas_reachability_entities *entities = c->bsp_entities;
  qa_aas_reachability_spatial *spatial = c->spatial;
  qa_aas_reachability_world *world = c->world;
  const qa_aas_movement_settings *settings = c->settings;
  uint8_t classname_buffer[MAX_EPAIRKEY] = {0}, model_buffer[MAX_EPAIRKEY] = {0};
  uint8_t target_buffer[MAX_EPAIRKEY] = {0}, target_name_buffer[MAX_EPAIRKEY] = {0};
  char classname[MAX_EPAIRKEY + 1], model_text[MAX_EPAIRKEY + 1], target_text[MAX_EPAIRKEY + 1], name_text[MAX_EPAIRKEY + 1];
  bool model_buffer_initialized = false;
  char message[1024];
  for (int64_t entity = qa_aas_reachability_entities_next(entities, 0); entity != 0;
       entity = qa_aas_reachability_entities_next(entities, entity)) {
    if (!qa_aas_reachability_entities_value(entities, entity, "classname", classname_buffer, MAX_EPAIRKEY)) continue;
    epair_text(classname_buffer, MAX_EPAIRKEY, classname, sizeof classname);
    if (strcmp(classname, "trigger_multiple") && strcmp(classname, "trigger_teleport")) continue;
    if (qa_aas_reachability_entities_value(entities, entity, "model", model_buffer, MAX_EPAIRKEY)) model_buffer_initialized = true;
    epair_text(model_buffer, MAX_EPAIRKEY, model_text, sizeof model_text);
    snprintf(message, sizeof message, "%s model = \"%s\"\n", classname, model_text);
    ctx_print(special, 1, message);
    if (!model_buffer_initialized) qa_range_error("AAS_Reachability_Teleport would read an uninitialized model suffix");
    /* modelBuffer.subarray(1): skip the first received character */
    char suffix[MAX_EPAIRKEY + 1];
    size_t suffix_length = 0;
    for (size_t i = 1; i < MAX_EPAIRKEY && model_buffer[i] != 0; i++) suffix[suffix_length++] = (char)model_buffer[i];
    suffix[suffix_length] = 0;
    qa_bounds model_bounds; qa_vec3 model_origin;
    qa_aas_spatial_model_bounds(spatial, qa_native_atoi(suffix), ZERO_V, &model_bounds, &model_origin);
    if (!qa_aas_reachability_entities_value(entities, entity, "target", target_buffer, MAX_EPAIRKEY)) {
      char fx[64], fy[64], fz[64];
      fixed_format(model_origin.x, 0, fx, sizeof fx); fixed_format(model_origin.y, 0, fy, sizeof fy); fixed_format(model_origin.z, 0, fz, sizeof fz);
      snprintf(message, sizeof message, "%s at %s %s %s without target\n", classname, fx, fy, fz);
      ctx_print(special, 3, message);
      continue;
    }
    if (!strcmp(classname, "trigger_multiple")) {
      int64_t relay = qa_aas_reachability_entities_next(entities, 0);
      for (; relay != 0; relay = qa_aas_reachability_entities_next(entities, relay)) {
        if (!qa_aas_reachability_entities_value(entities, relay, "classname", classname_buffer, MAX_EPAIRKEY)) continue;
        epair_text(classname_buffer, MAX_EPAIRKEY, classname, sizeof classname);
        if (strcmp(classname, "target_teleporter")) continue;
        if (!qa_aas_reachability_entities_value(entities, relay, "targetname", target_name_buffer, MAX_EPAIRKEY)) continue;
        char relay_name[MAX_EPAIRKEY + 1], wanted[MAX_EPAIRKEY + 1];
        epair_text(target_name_buffer, MAX_EPAIRKEY, relay_name, sizeof relay_name);
        epair_text(target_buffer, MAX_EPAIRKEY, wanted, sizeof wanted);
        if (!strcmp(relay_name, wanted)) break;
      }
      if (relay == 0) continue;
      if (!qa_aas_reachability_entities_value(entities, relay, "target", target_buffer, MAX_EPAIRKEY)) {
        ctx_print(special, 3, "target_teleporter without target\n");
        continue;
      }
      /* TS re-reads classname into the same buffer during the relay scan */
      epair_text(classname_buffer, MAX_EPAIRKEY, classname, sizeof classname);
    }
    epair_text(target_buffer, MAX_EPAIRKEY, target_text, sizeof target_text);
    int64_t destination = qa_aas_reachability_entities_next(entities, 0);
    for (; destination != 0; destination = qa_aas_reachability_entities_next(entities, destination)) {
      if (!qa_aas_reachability_entities_value(entities, destination, "targetname", target_name_buffer, MAX_EPAIRKEY)) continue;
      epair_text(target_name_buffer, MAX_EPAIRKEY, name_text, sizeof name_text);
      if (!strcmp(name_text, target_text)) break;
    }
    if (destination == 0) {
      snprintf(message, sizeof message, "teleporter without misc_teleporter_dest (%s)\n", target_text);
      ctx_print(special, 3, message);
      continue;
    }
    qa_vec3 destination_origin;
    if (!qa_aas_reachability_entities_vector(entities, destination, "origin", &destination_origin)) {
      snprintf(message, sizeof message, "teleporter destination (%s) without origin\n", target_text);
      ctx_print(special, 3, message);
      continue;
    }
    int64_t destination_area = qa_aas_reachability_world_point_area(world, destination_origin);
    if (!area_teleport_s(special, destination_area) && !area_jump_pad_s(special, destination_area)) {
      qa_aas_trace trace = qa_aas_spatial_trace_client_bbox(spatial, destination_origin, with_z(destination_origin, destination_origin.z - 64), 4, -1);
      if (trace.start_solid) {
        snprintf(message, sizeof message, "teleporter destination (%s) in solid\n", target_text);
        ctx_print(special, 3, message);
        continue;
      }
      destination_area = qa_aas_reachability_world_point_area(world, trace.end);
      double angle;
      (void)qa_aas_reachability_entities_float(entities, destination, "angle", &angle);
      qa_vec3 velocity = angle != 0 ? qa_scale3(qa_math_angle_vectors(v3f(0, angle, 0)).forward, 400) : ZERO_V;
      qa_bot_movement_prediction request; memset(&request, 0, sizeof request);
      request.entity_num = -1; request.origin = destination_origin; request.presence = 2; request.on_ground = false;
      request.velocity = velocity; request.command_move = ZERO_V;
      request.command_frames = 0; request.max_frames = 30; request.frame_time = fr(0.1);
      request.stop_events = LAND_EVENTS; request.stop_area = 0; request.visualize = false;
      qa_aas_client_move move = qa_aas_spatial_predict_client_movement(spatial, request);
      destination_area = qa_aas_reachability_world_point_area(world, move.end);
      if ((move.stop_event & (QA_AAS_STOP_ENTER_SLIME | QA_AAS_STOP_ENTER_LAVA)) != 0) {
        snprintf(message, sizeof message, "teleported into slime or lava at dest %s\n", target_text);
        ctx_print(special, 2, message);
      }
      destination_origin = move.end;
    }
    qa_bounds bounds = {qa_add3(model_origin, model_bounds.min), qa_add3(model_origin, model_bounds.max)};
    qa_vec3 midpoint = middle(bounds.min, bounds.max);
    qa_aas_area_link *areas = qa_aas_spatial_link_client_bounds(spatial, -1, bounds, 4);
    if (!areas) ctx_print(special, 1, "trigger_multiple not in any area\n");
    for (qa_aas_area_link *link = areas; link; link = link->next_area) {
      if (!area_teleport_s(special, link->area)) continue;
      qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(c);
      if (!reach) break;
      qa_aas_link_set_area(reach, destination_area);
      qa_aas_link_set_face(reach, 0);
      qa_aas_link_set_edge(reach, 0);
      qa_aas_link_set_start(reach, midpoint);
      qa_aas_link_set_end(reach, destination_origin);
      qa_aas_link_set_travel_type(reach, QA_TRAVEL_TELEPORT);
      qa_aas_link_set_travel_type(reach, qa_aas_link_travel_type(reach) | qa_aas_reachability_special_travel_flags_for_team(special, entity));
      qa_aas_link_set_travel_time(reach, settings->teleport_time);
      link_reach_s(special, link->area, reach);
      qa_aas_reachability_debug_count(&c->debug_state, "teleport");
    }
    qa_aas_area_link_free(areas);
  }
}
void qa_aas_reachability_special_elevator(qa_aas_reachability_special *special) {
  qa_aas_reachability_context *c = special->context;
  qa_aas_reachability_entities *entities = c->bsp_entities;
  qa_aas_reachability_spatial *spatial = c->spatial;
  qa_aas_reachability_world *world = c->world;
  const qa_aas_movement_settings *settings = c->settings;
  uint8_t classname_buffer[MAX_EPAIRKEY] = {0}, model_buffer[MAX_EPAIRKEY] = {0};
  char classname[MAX_EPAIRKEY + 1], model_text[MAX_EPAIRKEY + 1], message[256];
  if (c->debug) qa_aas_reachability_context_log(c, "AAS_Reachability_Elevator\r\n");
  for (int64_t entity = qa_aas_reachability_entities_next(entities, 0); entity != 0;
       entity = qa_aas_reachability_entities_next(entities, entity)) {
    if (!qa_aas_reachability_entities_value(entities, entity, "classname", classname_buffer, MAX_EPAIRKEY)) continue;
    epair_text(classname_buffer, MAX_EPAIRKEY, classname, sizeof classname);
    if (strcmp(classname, "func_plat")) continue;
    if (c->debug) qa_aas_reachability_context_log(c, "found func plat\r\n");
    if (!qa_aas_reachability_entities_value(entities, entity, "model", model_buffer, MAX_EPAIRKEY)) {
      ctx_print(special, 3, "func_plat without model\n");
      continue;
    }
    epair_text(model_buffer, MAX_EPAIRKEY, model_text, sizeof model_text);
    int64_t model_number = qa_native_atoi(model_text[0] ? model_text + 1 : model_text);
    if (model_number <= 0) { ctx_print(special, 3, "func_plat with invalid model number\n"); continue; }
    qa_bounds model_bounds; qa_vec3 model_origin;
    qa_aas_spatial_model_bounds(spatial, model_number, ZERO_V, &model_bounds, &model_origin);
    (void)model_origin;
    qa_vec3 origin;
    (void)qa_aas_reachability_entities_vector(entities, entity, "origin", &origin);
    qa_vec3 min = model_bounds.min, max = model_bounds.max;
    double lip, height, speed;
    (void)qa_aas_reachability_entities_float(entities, entity, "lip", &lip);
    if (lip == 0) lip = 8;
    (void)qa_aas_reachability_entities_float(entities, entity, "height", &height);
    if (height == 0) height = fr((double)fr(max.z - min.z) - lip);
    (void)qa_aas_reachability_entities_float(entities, entity, "speed", &speed);
    if (speed == 0) speed = 200;
    qa_vec3 pos2 = with_z(origin, origin.z - height);
    qa_vec3 sum = qa_add3(min, max);
    qa_vec3 center = v3f(pos2.x + 0.5 * sum.x, pos2.y + 0.5 * sum.y, pos2.z + 0.5 * sum.z);
    qa_vec3 platform_bottom = with_z(center, (double)fr(max.z - (double)fr(origin.z - pos2.z)) + 2);
    qa_vec3 platform_top = with_z(center, max.z + 2);
    min = qa_sub3(min, v3f(1, 1, 1)); max = qa_add3(max, v3f(1, 1, 1));
    qa_vec3 mid = middle(min, max);
    qa_vec3 bottom_sides[8];
    perimeter(min, mid, max, bottom_sides);
    for (int i = 0; i < 9; i++) {
      qa_vec3 bottom_origin;
      int64_t area1;
      if (i < 8) {
        qa_vec3 offset = qa_aas_at_vec3(bottom_sides, 8, i);
        bottom_origin = v3f(origin.x + offset.x, origin.y + offset.y, platform_bottom.z + 16);
        area1 = qa_aas_reachability_world_point_area(world, bottom_origin);
        int k = 0;
        for (; k < 16; k++) {
          if (area1 != 0 && (area_grounded_s(special, area1) || area_swim_s(special, area1))) break;
          bottom_origin = with_z(bottom_origin, bottom_origin.z + 4);
          area1 = qa_aas_reachability_world_point_area(world, bottom_origin);
        }
        if (k >= 16) continue;
      } else {
        bottom_origin = with_z(platform_top, platform_top.z + 24);
        area1 = qa_aas_reachability_world_point_area(world, bottom_origin);
        if (area1 == 0) continue;
        bottom_origin = with_z(platform_bottom, platform_bottom.z + 24);
      }
      for (int64_t n = 0; n < 3; n++) {
        min = qa_sub3(min, v3f(4, 4, 4)); max = qa_add3(max, v3f(4, 4, 4));
        qa_vec3 top_sides[8];
        perimeter(min, mid, max, top_sides);
        for (int j = 0; j < 8; j++) {
          qa_vec3 offset = qa_aas_at_vec3(top_sides, 8, j);
          qa_vec3 top_origin = v3f(origin.x + offset.x, origin.y + offset.y, platform_top.z + 16);
          int64_t area2 = qa_aas_reachability_world_point_area(world, top_origin);
          int l = 0;
          for (; l < 16; l++) {
            if (area2 != 0 && (area_grounded_s(special, area2) || area_swim_s(special, area2))) {
              qa_aas_trace trace = qa_aas_spatial_trace_client_bbox(spatial, with_z(platform_top, platform_top.z + 32),
                                                                    with_z(top_origin, top_origin.z + 1), 4, -1);
              if (trace.fraction >= 1) break;
            }
            top_origin = with_z(top_origin, top_origin.z + 4);
            area2 = qa_aas_reachability_world_point_area(world, top_origin);
          }
          if (l >= 16 || area2 == area1 || !area_grounded_s(special, area2) || qa_aas_reachability_context_exists(c, area1, area2)) continue;
          qa_vec3 outward = qa_normalize3(qa_sub3(bottom_origin, platform_bottom));
          qa_vec3 start = v3f(bottom_origin.x + (double)fr(24 * outward.x), bottom_origin.y + (double)fr(24 * outward.y), bottom_origin.z);
          bool outside = false;
          for (int axis = 0; axis < 3; axis++)
            if (axis_of(start, axis) < (double)fr(axis_of(origin, axis) + axis_of(min, axis)) ||
                axis_of(start, axis) > (double)fr(axis_of(origin, axis) + axis_of(max, axis))) outside = true;
          if (!outside) continue;
          qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(c);
          if (!reach) continue;
          qa_aas_link_set_area(reach, area2);
          qa_aas_link_set_face(reach, model_number);
          qa_aas_link_set_edge(reach, source_int(height));
          qa_aas_link_set_start(reach, start);
          qa_aas_link_set_end(reach, top_origin);
          qa_aas_link_set_travel_type(reach, QA_TRAVEL_ELEVATOR);
          qa_aas_link_set_travel_type(reach, qa_aas_link_travel_type(reach) | qa_aas_reachability_special_travel_flags_for_team(special, entity));
          qa_aas_link_set_travel_time(reach, fr((double)settings->start_elevator_time + (double)fr((double)fr(height * 100) / speed)));
          link_reach_s(special, area1, reach);
          n = 9999;
          if (c->debug) {
            snprintf(message, sizeof message, "elevator reach from %lld to %lld\r\n", (long long)area1, (long long)area2);
            qa_aas_reachability_context_log(c, message);
          }
          qa_aas_reachability_debug_count(&c->debug_state, "elevator");
        }
      }
    }
  }
}
static qa_aas_linked_reachability *find_face_reachabilities(qa_aas_reachability_special *special,
                                                            const qa_vec3 *points, size_t point_count,
                                                            qa_aas_plane_rec plane, bool towards_face) {
  qa_aas_reachability_context *c = special->context;
  qa_aas_asset *world = &c->world->asset;
  qa_aas_reachability_spatial *spatial = c->spatial;
  const qa_aas_movement_settings *settings = c->settings;
  qa_aas_linked_reachability *links = NULL;
  int64_t best_face = 0;
  const qa_aas_plane_rec *best_plane = NULL;
  qa_aas_closest_edge_state state; memset(&state, 0, sizeof state);
  for (int64_t area_number = 1; area_number < (int64_t)world->area_count; area_number++) {
    const qa_aas_area_rec *area = qa_aas_at_area(world, area_number);
    double best_distance = 999999;
    for (int64_t j = 0; j < area->face_count; j++) {
      int64_t face_number = qa_aas_at_i64(world->face_indexes, world->face_index_count, area->first_face + j);
      const qa_aas_face_rec *face = qa_aas_at_face(world, abs64(face_number));
      if ((face->flags & FACE_GROUND) == 0) continue;
      const qa_aas_plane_rec *face_plane = qa_aas_at_plane(world, face->plane);
      for (int64_t k = 0; k < face->edge_count; k++) {
        const qa_aas_edge_rec *edge = qa_aas_at_edge(world, abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face->first_edge + k)));
        qa_vec3 v1 = qa_aas_at_vertex(world, edge->vertices[0]), v2 = qa_aas_at_vertex(world, edge->vertices[1]);
        for (size_t l = 0; l < point_count; l++) {
          double distance = qa_aas_closest_edge_points(v1, v2, qa_aas_at_vec3(points, point_count, (int64_t)l),
              qa_aas_at_vec3(points, point_count, (int64_t)((l + 1) % point_count)), face_plane, &plane, &state, best_distance);
          if (distance < best_distance) { best_face = face_number; best_plane = face_plane; best_distance = distance; }
        }
      }
    }
    if (best_distance > 192) continue;
    if (!state.has_range || !best_plane) qa_range_error("AAS_FindFaceReachabilities would read uninitialized closest-edge points");
    qa_vec3 start = middle(state.range.start1, state.range.start2), end = middle(state.range.end1, state.range.end2);
    if (!towards_face) { qa_vec3 tmp = start; start = end; end = tmp; }
    state.range.start1 = start; state.range.end1 = end;
    double horizontal_distance = qa_length3(with_z(qa_sub3(end, start), 0));
    if (horizontal_distance > (double)fr(2 * qa_aas_max_jump_distance(settings, settings->jump_velocity))) continue;
    if ((double)fr(end.z - 32) > start.z || end.z < (double)fr(start.z - 128)) continue;
    if (horizontal_distance > 32 && !qa_aas_spatial_horizontal_velocity_for_jump(spatial, 0, start, end).success) continue;
    start = with_z(start, start.z + 1); end = with_z(end, end.z + 1);
    state.range.start1 = start; state.range.end1 = end;
    qa_vec3 test = with_z(towards_face ? end : start, 0);
    qa_vec3 test_point = with_z(test, (double)fr(best_plane->distance - qa_dot3(best_plane->normal, test)) / best_plane->normal.z);
    if (!qa_aas_spatial_point_inside_face(spatial, best_face, test_point, fr(0.1)) && (double)fr(end.z - 16) > start.z) continue;
    qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(c);
    if (!reach) return links;
    qa_aas_link_set_area(reach, area_number);
    qa_aas_link_set_face(reach, 0);
    qa_aas_link_set_edge(reach, 0);
    qa_aas_link_set_start(reach, start);
    qa_aas_link_set_end(reach, end);
    qa_aas_link_set_travel_type(reach, 0);
    qa_aas_link_set_travel_time(reach, 0);
    reach->next = links; links = reach;
    qa_aas_reachability_context_permanent_line(c, qa_aas_link_start(reach), qa_aas_link_end(reach), towards_face ? 1 : 2);
  }
  return links;
}
void qa_aas_reachability_special_func_bobbing(qa_aas_reachability_special *special) {
  qa_aas_reachability_context *c = special->context;
  qa_aas_reachability_entities *entities = c->bsp_entities;
  qa_aas_reachability_spatial *spatial = c->spatial;
  qa_aas_reachability_world *world = c->world;
  const qa_aas_movement_settings *settings = c->settings;
  uint8_t classname_buffer[MAX_EPAIRKEY] = {0}, model_buffer[MAX_EPAIRKEY] = {0};
  char classname[MAX_EPAIRKEY + 1], model_text[MAX_EPAIRKEY + 1], message[1024];
  for (int64_t entity = qa_aas_reachability_entities_next(entities, 0); entity != 0;
       entity = qa_aas_reachability_entities_next(entities, entity)) {
    if (!qa_aas_reachability_entities_value(entities, entity, "classname", classname_buffer, MAX_EPAIRKEY)) continue;
    epair_text(classname_buffer, MAX_EPAIRKEY, classname, sizeof classname);
    if (strcmp(classname, "func_bobbing")) continue;
    double height;
    (void)qa_aas_reachability_entities_float(entities, entity, "height", &height);
    if (height == 0) height = 32;
    if (!qa_aas_reachability_entities_value(entities, entity, "model", model_buffer, MAX_EPAIRKEY)) {
      ctx_print(special, 3, "func_bobbing without model\n");
      continue;
    }
    epair_text(model_buffer, MAX_EPAIRKEY, model_text, sizeof model_text);
    int64_t model_number = qa_native_atoi(model_text[0] ? model_text + 1 : model_text);
    if (model_number <= 0) { ctx_print(special, 3, "func_bobbing with invalid model number\n"); continue; }
    qa_vec3 origin;
    (void)qa_aas_reachability_entities_vector(entities, entity, "origin", &origin);
    qa_bounds model_bounds; qa_vec3 model_origin;
    qa_aas_spatial_model_bounds(spatial, model_number, ZERO_V, &model_bounds, &model_origin);
    (void)model_origin;
    qa_vec3 min = qa_add3(model_bounds.min, origin), max = qa_add3(model_bounds.max, origin);
    qa_vec3 mid = middle(min, max);
    int64_t spawn_flags;
    (void)qa_aas_reachability_entities_int(entities, entity, "spawnflags", &spawn_flags);
    int axis = (spawn_flags & 1) != 0 ? 0 : (spawn_flags & 2) != 0 ? 1 : 2;
    qa_vec3 move_start = mid, move_end = mid;
    if (axis == 0) { move_start.x = fr(mid.x - height); move_end.x = fr(mid.x + height); }
    else if (axis == 1) { move_start.y = fr(mid.y - height); move_end.y = fr(mid.y + height); }
    else { move_start.z = fr(mid.z - height); move_end.z = fr(mid.z + height); }
    char fs[6][64];
    fixed_format(move_start.x, 1, fs[0], 64); fixed_format(move_start.y, 1, fs[1], 64); fixed_format(move_start.z, 1, fs[2], 64);
    fixed_format(move_end.x, 1, fs[3], 64); fixed_format(move_end.y, 1, fs[4], 64); fixed_format(move_end.z, 1, fs[5], 64);
    snprintf(message, sizeof message, "funcbob model %lld, start = {%s, %s, %s} end = {%s, %s, %s}\n",
             (long long)model_number, fs[0], fs[1], fs[2], fs[3], fs[4], fs[5]);
    qa_aas_reachability_context_log(c, message);
    qa_vec3 start_points[4], end_points[4];
    for (int side = 0; side < 2; side++) {
      qa_vec3 point = side == 0 ? move_start : move_end;
      qa_vec3 *points = side == 0 ? start_points : end_points;
      float top = fr((double)fr(point.z + (double)fr(max.z - mid.z)) + 24);
      points[0] = v3f(point.x + (double)fr(max.x - mid.x), point.y + (double)fr(max.y - mid.y), top);
      points[1] = v3f(point.x + (double)fr(max.x - mid.x), point.y + (double)fr(min.y - mid.y), top);
      points[2] = v3f(point.x + (double)fr(min.x - mid.x), point.y + (double)fr(min.y - mid.y), top);
      points[3] = v3f(point.x + (double)fr(min.x - mid.x), point.y + (double)fr(max.y - mid.y), top);
    }
    qa_aas_plane_rec start_plane = {UP_V, start_points[0].z, 2};
    qa_aas_plane_rec end_plane = {UP_V, end_points[0].z, 2};
    float top_offset = fr((double)fr(max.z - mid.z) + 24);
    qa_vec3 start_top = with_z(move_start, move_start.z + (double)top_offset);
    qa_vec3 end_top = with_z(move_end, move_end.z + (double)top_offset);
    if (qa_aas_reachability_world_point_area(world, start_top) == 0 || qa_aas_reachability_world_point_area(world, end_top) == 0) continue;
    for (int direction = 0; direction < 2; direction++) {
      qa_aas_linked_reachability *starts = find_face_reachabilities(special, direction == 0 ? start_points : end_points, 4,
                                                                     direction == 0 ? start_plane : end_plane, true);
      qa_aas_linked_reachability *ends = find_face_reachabilities(special, direction == 0 ? end_points : start_points, 4,
                                                                   direction == 0 ? end_plane : start_plane, false);
      qa_aas_linked_reachability *next_start = NULL, *next_end = NULL;
      for (qa_aas_linked_reachability *start_reach = starts; start_reach; start_reach = next_start) {
        next_start = start_reach->next;
        for (qa_aas_linked_reachability *end_reach = ends; end_reach; end_reach = next_end) {
          next_end = end_reach->next;
          snprintf(message, sizeof message, "funcbob reach from area %lld to %lld\n",
                   (long long)qa_aas_link_area(start_reach), (long long)qa_aas_link_area(end_reach));
          qa_aas_reachability_context_log(c, message);
          qa_vec3 start_now = qa_aas_link_start(start_reach);
          qa_vec3 outward = qa_normalize3(with_z(qa_sub3(start_now, direction == 0 ? start_top : end_top), 0));
          qa_vec3 start = ma(start_now, 1, outward), end = ma(start_now, 16, outward);
          start = with_z(start, start.z + 1); end = with_z(end, end.z + 1);
          size_t crossing_count = 0;
          qa_aas_area_crossing *crossings = qa_aas_spatial_trace_areas(spatial, start, end, 10, &crossing_count);
          if (crossing_count == 0) { free(crossings); continue; }
          qa_aas_link_set_start(start_reach, crossing_count > 1 ? crossings[1].point : end);
          free(crossings);
          if (qa_aas_reachability_world_point_area(world, qa_aas_link_start(start_reach)) == 0 ||
              qa_aas_reachability_world_point_area(world, qa_aas_link_end(end_reach)) == 0) continue;
          qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(c);
          if (!reach) qa_range_error("AAS_Reachability_FuncBobbing would dereference a failed reachability allocation");
          qa_aas_link_set_area(reach, qa_aas_link_area(end_reach));
          int64_t edge_value = direction == 0
              ? (source_int(axis_of(move_start, axis)) << 16) | (source_int(axis_of(move_end, axis)) & 0xffff)
              : (source_int(axis_of(move_end, axis)) << 16) | (source_int(axis_of(move_start, axis)) & 0xffff);
          qa_aas_link_set_edge(reach, edge_value);
          qa_aas_link_set_face(reach, (spawn_flags << 16) | model_number);
          qa_aas_link_set_start(reach, qa_aas_link_start(start_reach));
          qa_aas_link_set_end(reach, qa_aas_link_end(end_reach));
          qa_aas_link_set_travel_type(reach, QA_TRAVEL_FUNCBOB);
          qa_aas_link_set_travel_type(reach, qa_aas_link_travel_type(reach) | qa_aas_reachability_special_travel_flags_for_team(special, entity));
          qa_aas_link_set_travel_time(reach, settings->func_bob_time);
          qa_aas_reachability_debug_count(&c->debug_state, "funcbob");
          link_reach_s(special, qa_aas_link_area(start_reach), reach);
        }
      }
      for (qa_aas_linked_reachability *link = starts; link;) {
        qa_aas_linked_reachability *next = link->next;
        qa_aas_reachability_context_free_link(c, link);
        link = next;
      }
      for (qa_aas_linked_reachability *link = ends; link;) {
        qa_aas_linked_reachability *next = link->next;
        qa_aas_reachability_context_free_link(c, link);
        link = next;
      }
      if ((spawn_flags & 3) == 0) break;
    }
  }
}
void qa_aas_reachability_special_jump_pad(qa_aas_reachability_special *special) {
  qa_aas_reachability_context *c = special->context;
  qa_aas_reachability_entities *entities = c->bsp_entities;
  qa_aas_reachability_spatial *spatial = c->spatial;
  qa_aas_asset *world = &c->world->asset;
  const qa_aas_movement_settings *settings = c->settings;
  bool visualize_jump_pads = source_int(qa_aas_reachability_context_variable(c, "bot_visualizejumppads", "0")) != 0;
  uint8_t classname_buffer[MAX_EPAIRKEY] = {0};
  char classname[MAX_EPAIRKEY + 1], message[1024];
  for (int64_t entity = qa_aas_reachability_entities_next(entities, 0); entity != 0;
       entity = qa_aas_reachability_entities_next(entities, entity)) {
    if (!qa_aas_reachability_entities_value(entities, entity, "classname", classname_buffer, MAX_EPAIRKEY)) continue;
    epair_text(classname_buffer, MAX_EPAIRKEY, classname, sizeof classname);
    if (strcmp(classname, "trigger_push")) continue;
    qa_aas_jump_pad_info pad;
    if (!qa_aas_spatial_get_jump_pad_info(spatial, entity, &pad)) continue;
    qa_vec3 start = pad.start, velocity = pad.velocity;
    qa_aas_area_link *areas = qa_aas_spatial_link_client_bounds(spatial, -1, pad.bounds, 4);
    qa_aas_area_link *pad_link = areas;
    while (pad_link && !area_jump_pad_s(special, pad_link->area)) pad_link = pad_link->next_area;
    if (!pad_link) {
      ctx_print(special, 1, "trigger_push not in any jump pad area\n");
      qa_aas_area_link_free(areas);
      continue;
    }
    char fx[64], fy[64], fz[64];
    fixed_format(velocity.x, 6, fx, sizeof fx); fixed_format(velocity.y, 6, fy, sizeof fy); fixed_format(velocity.z, 6, fz, sizeof fz);
    snprintf(message, sizeof message, "found a trigger_push with velocity %s %s %s\n", fx, fy, fz);
    ctx_print(special, 1, message);
    if (velocity.x != 0 || velocity.y != 0) {
      qa_aas_client_move move; memset(&move, 0, sizeof move);
      bool has_move = false;
      int64_t destination_area = 0;
      int frame = 0;
      for (; frame < 20; frame++) {
        qa_bot_movement_prediction request; memset(&request, 0, sizeof request);
        request.entity_num = -1; request.origin = start; request.presence = 2; request.on_ground = false;
        request.velocity = velocity; request.command_move = ZERO_V;
        request.command_frames = 0; request.max_frames = 30; request.frame_time = fr(0.1);
        request.stop_events = LAND_EVENTS; request.stop_area = 0; request.visualize = visualize_jump_pads;
        move = qa_aas_spatial_predict_client_movement(spatial, request);
        has_move = true;
        destination_area = move.end_area;
        qa_aas_area_link *source_link = areas;
        for (; source_link; source_link = source_link->next_area)
          if (area_jump_pad_s(special, source_link->area) && source_link->area == destination_area) break;
        if (!source_link) break;
        start = move.end; velocity = move.velocity;
      }
      if (destination_area != 0 && frame < 20) {
        if (!has_move) qa_range_error("Jump-pad movement has no prediction output");
        for (qa_aas_area_link *link = areas; link; link = link->next_area) {
          if (!area_jump_pad_s(special, link->area) || qa_aas_reachability_context_exists(c, link->area, destination_area)) continue;
          qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(c);
          if (!reach) { qa_aas_area_link_free(areas); return; }
          qa_aas_link_set_area(reach, destination_area);
          qa_aas_link_set_face(reach, source_int(velocity.z));
          qa_aas_link_set_edge(reach, source_int(sqrt((double)fr((double)fr(velocity.x * velocity.x) + (double)fr(velocity.y * velocity.y)))));
          qa_aas_link_set_start(reach, start);
          qa_aas_link_set_end(reach, move.end);
          qa_aas_link_set_travel_type(reach, QA_TRAVEL_JUMPPAD);
          qa_aas_link_set_travel_type(reach, qa_aas_link_travel_type(reach) | qa_aas_reachability_special_travel_flags_for_team(special, entity));
          qa_aas_link_set_travel_time(reach, settings->jump_pad_time);
          link_reach_s(special, link->area, reach);
          qa_aas_reachability_debug_count(&c->debug_state, "jumppad");
        }
      }
    }
    if (fabs(velocity.x) > 100 || fabs(velocity.y) > 100) { qa_aas_area_link_free(areas); continue; }
    for (int64_t destination_area = 1; destination_area < (int64_t)world->area_count; destination_area++) {
      qa_aas_area_link *source_link = areas;
      for (; source_link; source_link = source_link->next_area)
        if (qa_aas_reachability_context_exists(c, source_link->area, destination_area) ||
            (area_jump_pad_s(special, source_link->area) && source_link->area == destination_area)) break;
      if (source_link) continue;
      const qa_aas_area_rec *area = qa_aas_at_area(world, destination_area);
      for (int64_t i = 0; i < area->face_count; i++) {
        int64_t face_number = qa_aas_at_i64(world->face_indexes, world->face_index_count, area->first_face + i);
        const qa_aas_face_rec *face = qa_aas_at_face(world, abs64(face_number));
        if ((face->flags & FACE_GROUND) == 0) continue;
        qa_vec3 face_center = qa_aas_face_center(world, face_number);
        if (face_center.z < start.z) continue;
        qa_aas_jump_speed jump = qa_aas_spatial_horizontal_velocity_for_jump(spatial, velocity.z, start, face_center);
        if (!(jump.success && jump.velocity < 150)) continue;
        qa_vec3 direction = qa_normalize3(with_z(qa_sub3(face_center, start), 0));
        qa_vec3 command = qa_scale3(direction, jump.velocity);
        qa_bot_movement_prediction request; memset(&request, 0, sizeof request);
        request.entity_num = -1; request.origin = start; request.presence = 2; request.on_ground = false;
        request.velocity = velocity; request.command_move = command;
        request.command_frames = 30; request.max_frames = 30; request.frame_time = fr(0.1);
        request.stop_events = (LAND_EVENTS & ~QA_AAS_STOP_HIT_GROUND) | QA_AAS_STOP_HIT_GROUND_AREA;
        request.stop_area = destination_area; request.visualize = false;
        qa_aas_client_move move = qa_aas_spatial_predict_client_movement(spatial, request);
        if (move.frames >= 30 || (move.stop_event & HAZARD_EVENTS) != 0 ||
            (move.stop_event & (QA_AAS_STOP_HIT_GROUND_AREA | QA_AAS_STOP_TOUCH_JUMP_PAD | QA_AAS_STOP_TOUCH_TELEPORTER)) == 0) continue;
        qa_aas_area_link *return_link = areas;
        while (return_link && return_link->area != move.end_area) return_link = return_link->next_area;
        if (return_link) continue;
        for (qa_aas_area_link *link = areas; link; link = link->next_area) {
          if (!area_jump_pad_s(special, link->area) || qa_aas_reachability_context_exists(c, link->area, destination_area)) continue;
          qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(c);
          if (!reach) { qa_aas_area_link_free(areas); return; }
          qa_aas_link_set_area(reach, move.end_area);
          qa_aas_link_set_face(reach, source_int(velocity.z));
          qa_aas_link_set_edge(reach, source_int(sqrt((double)fr((double)fr(command.x * command.x) + (double)fr(command.y * command.y)))));
          qa_aas_link_set_start(reach, start);
          qa_aas_link_set_end(reach, face_center);
          qa_aas_link_set_travel_type(reach, QA_TRAVEL_JUMPPAD);
          qa_aas_link_set_travel_type(reach, qa_aas_link_travel_type(reach) | qa_aas_reachability_special_travel_flags_for_team(special, entity));
          qa_aas_link_set_travel_time(reach, settings->air_controlled_jump_pad_time);
          link_reach_s(special, link->area, reach);
          qa_aas_reachability_debug_count(&c->debug_state, "jumppad");
        }
      }
    }
    qa_aas_area_link_free(areas);
  }
}
bool qa_aas_reachability_special_grapple(qa_aas_reachability_special *special, int64_t from, int64_t to) {
  qa_aas_reachability_context *c = special->context;
  qa_aas_asset *world = &c->world->asset;
  qa_aas_reachability_spatial *spatial = c->spatial;
  const qa_aas_movement_settings *settings = c->settings;
  char message[512];
  if ((!area_grounded_s(special, from) && !area_swim_s(special, from)) ||
      (qa_aas_at_setting(world, from)->presence & 2) == 0) return false;
  if (area_swim_s(special, from)) return false;
  const qa_aas_area_rec *area1 = qa_aas_at_area(world, from), *area2 = qa_aas_at_area(world, to);
  if (area2->bounds.max.z < area1->bounds.min.z) return false;
  if (qa_aas_reachability_world_point_area(c->world, area1->center) == 0) {
    char fx[64], fy[64], fz[64];
    fixed_format(area1->center.x, 6, fx, sizeof fx); fixed_format(area1->center.y, 6, fy, sizeof fy); fixed_format(area1->center.z, 6, fz, sizeof fz);
    snprintf(message, sizeof message, "area %lld center %s %s %s in solid?\r\n", (long long)from, fx, fy, fz);
    qa_aas_reachability_context_log(c, message);
  }
  qa_aas_trace floor_trace = qa_aas_spatial_trace_client_bbox(spatial, area1->center, with_z(area1->center, area1->center.z - 1000), 4, -1);
  if (floor_trace.start_solid) return false;
  qa_vec3 area_start = floor_trace.end;
  for (int64_t i = 0; i < area2->face_count; i++) {
    int64_t face_number = qa_aas_at_i64(world->face_indexes, world->face_index_count, area2->first_face + i);
    const qa_aas_face_rec *face = qa_aas_at_face(world, abs64(face_number));
    if ((face->flags & FACE_SOLID) == 0) continue;
    const qa_aas_edge_rec *edge = qa_aas_at_edge(world, abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face->first_edge)));
    qa_vec3 vertex = qa_aas_at_vertex(world, edge->vertices[0]);
    const qa_aas_plane_rec *plane = qa_aas_at_plane(world, face->plane);
    if (qa_dot3(plane->normal, qa_sub3(vertex, area_start)) > 0) continue;
    qa_vec3 face_center = qa_aas_face_center(world, face_number);
    if (face_center.z < (double)fr(area_start.z + 64) || qa_dot3(plane->normal, DOWN_V) < 0) continue;
    qa_vec3 delta = qa_sub3(face_center, area_start);
    double horizontal_distance = qa_length3(with_z(delta, 0));
    if (horizontal_distance == 0 || horizontal_distance > 2000 ||
        (double)fr(delta.z / horizontal_distance) < tan(2 * acos(-1.0) * 15 / 360)) continue;
    qa_aas_host_trace bsp_trace = qa_aas_spatial_trace(spatial, face_center, ma(face_center, -500, plane->normal),
                                                       false, (qa_bounds){{0, 0, 0}, {0, 0, 0}}, 0, 1);
    if (((int64_t)bsp_trace.surface_flags & 4) != 0 || (double)fr(bsp_trace.fraction * 500) > 32) continue;
    qa_vec3 start = ma(area_start, 4, qa_normalize3(qa_sub3(face_center, area_start)));
    qa_aas_trace approach = qa_aas_spatial_trace_client_bbox(spatial, start, bsp_trace.end, 2, -1);
    if (qa_length3(qa_sub3(approach.end, face_center)) > 24) continue;
    qa_aas_trace landing = qa_aas_spatial_trace_client_bbox(spatial, approach.end,
        with_z(approach.end, approach.end.z - qa_aas_fall_damage_distance(settings)), 2, -1);
    if (landing.fraction >= 1) continue;
    int64_t destination_area = qa_aas_reachability_world_point_area(c->world, landing.end);
    if ((qa_aas_at_setting(world, destination_area)->contents & (2 | 4)) != 0 || destination_area == from ||
        qa_aas_reachability_context_exists(c, from, destination_area) || !area_grounded_s(special, destination_area)) continue;
    size_t crossing_count = 0;
    qa_aas_area_crossing *crossings = qa_aas_spatial_trace_areas(spatial, area_start, bsp_trace.end, 20, &crossing_count);
    bool blocked = crossing_count >= 20;
    for (size_t ci = 0; ci < crossing_count; ci++)
      if ((qa_aas_at_setting(world, crossings[ci].area)->contents & 8) != 0) blocked = true;
    free(crossings);
    if (blocked) continue;
    qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(c);
    if (!reach) return false;
    qa_aas_link_set_area(reach, destination_area);
    qa_aas_link_set_face(reach, face_number);
    qa_aas_link_set_edge(reach, 0);
    qa_aas_link_set_start(reach, area_start);
    qa_aas_link_set_end(reach, bsp_trace.end);
    qa_aas_link_set_travel_type(reach, QA_TRAVEL_GRAPPLEHOOK);
    qa_aas_link_set_travel_time(reach, (double)settings->start_grapple_time +
        qa_length3(qa_sub3(qa_aas_link_end(reach), qa_aas_link_start(reach))) * 0.25);
    link_reach_s(special, from, reach);
    qa_aas_reachability_debug_count(&c->debug_state, "grapple");
  }
  return false;
}
static const char *const WEAPON_JUMP_ITEMS[] = {
  "item_armor_body", "item_armor_combat", "item_health_mega", "weapon_grenadelauncher", "weapon_rocketlauncher",
  "weapon_lightning", "weapon_plasmagun", "weapon_railgun", "weapon_bfg", "item_quad", "item_regen", "item_invulnerability",
};
void qa_aas_reachability_special_set_weapon_jump_area_flags(qa_aas_reachability_special *special) {
  qa_aas_reachability_context *c = special->context;
  qa_aas_reachability_world *world = c->world;
  qa_aas_reachability_spatial *spatial = c->spatial;
  qa_aas_reachability_entities *entities = c->bsp_entities;
  qa_bounds bounds = {v3f(-15, -15, -15), v3f(15, 15, 15)};
  uint8_t classname_buffer[MAX_EPAIRKEY] = {0};
  char classname[MAX_EPAIRKEY + 1], message[512];
  int64_t count = 0;
  for (int64_t entity = qa_aas_reachability_entities_next(entities, 0); entity != 0;
       entity = qa_aas_reachability_entities_next(entities, entity)) {
    if (!qa_aas_reachability_entities_value(entities, entity, "classname", classname_buffer, MAX_EPAIRKEY)) continue;
    epair_text(classname_buffer, MAX_EPAIRKEY, classname, sizeof classname);
    bool wanted = false;
    for (size_t i = 0; i < sizeof WEAPON_JUMP_ITEMS / sizeof *WEAPON_JUMP_ITEMS; i++)
      if (!strcmp(classname, WEAPON_JUMP_ITEMS[i])) wanted = true;
    if (!wanted) continue;
    qa_vec3 origin;
    if (!qa_aas_reachability_entities_vector(entities, entity, "origin", &origin)) continue;
    int64_t spawn_flags;
    (void)qa_aas_reachability_entities_int(entities, entity, "spawnflags", &spawn_flags);
    if ((spawn_flags & 1) == 0) {
      qa_vec3 dropped_origin;
      bool success = qa_aas_spatial_drop_to_floor(spatial, origin, bounds, &dropped_origin);
      origin = dropped_origin;
      if (!success) {
        char fx[64], fy[64], fz[64];
        fixed_format(origin.x, 1, fx, sizeof fx); fixed_format(origin.y, 1, fy, sizeof fy); fixed_format(origin.z, 1, fz, sizeof fz);
        snprintf(message, sizeof message, "%s in solid at (%s %s %s)\n", classname, fx, fy, fz);
        ctx_print(special, 1, message);
      }
    }
    int64_t area = qa_aas_spatial_best_reachable_area(spatial, origin, bounds).area;
    qa_aas_reachability_world_setting(world, area)->flags |= AREA_WEAPONJUMP;
    count++;
  }
  for (int64_t area = 1; area < (int64_t)world->asset.area_count; area++) {
    if (!area_jump_pad_s(special, area)) continue;
    qa_aas_reachability_world_setting(world, area)->flags |= AREA_WEAPONJUMP;
    count++;
  }
  snprintf(message, sizeof message, "%lld weapon jump areas\n", (long long)count);
  ctx_print(special, 1, message);
}
bool qa_aas_reachability_special_weapon_jump(qa_aas_reachability_special *special, int64_t from, int64_t to) {
  qa_aas_reachability_context *c = special->context;
  qa_aas_asset *world = &c->world->asset;
  qa_aas_reachability_spatial *spatial = c->spatial;
  const qa_aas_movement_settings *settings = c->settings;
  char message[512];
  if (!area_grounded_s(special, from) || area_swim_s(special, from) || !area_grounded_s(special, to) ||
      (qa_aas_at_setting(world, to)->flags & AREA_WEAPONJUMP) == 0) return false;
  const qa_aas_area_rec *area1 = qa_aas_at_area(world, from), *area2 = qa_aas_at_area(world, to);
  if (area2->bounds.max.z < area1->bounds.min.z) return false;
  if (qa_aas_reachability_world_point_area(c->world, area1->center) == 0) {
    char fx[64], fy[64], fz[64];
    fixed_format(area1->center.x, 6, fx, sizeof fx); fixed_format(area1->center.y, 6, fy, sizeof fy); fixed_format(area1->center.z, 6, fz, sizeof fz);
    snprintf(message, sizeof message, "area %lld center %s %s %s in solid?\r\n", (long long)from, fx, fy, fz);
    qa_aas_reachability_context_log(c, message);
  }
  qa_aas_trace floor_trace = qa_aas_spatial_trace_client_bbox(spatial, area1->center, with_z(area1->center, area1->center.z - 1000), 4, -1);
  if (floor_trace.start_solid) return false;
  qa_vec3 start = floor_trace.end;
  for (int64_t i = 0; i < area2->face_count; i++) {
    int64_t face_number = qa_aas_at_i64(world->face_indexes, world->face_index_count, area2->first_face + i);
    const qa_aas_face_rec *face = qa_aas_at_face(world, abs64(face_number));
    if ((face->flags & FACE_GROUND) == 0) continue;
    qa_vec3 face_center = qa_aas_face_center(world, face_number);
    if (face_center.z < (double)fr(start.z + 64)) continue;
    double velocity = qa_aas_spatial_rocket_jump_z_velocity(spatial, start);
    qa_aas_jump_speed jump = qa_aas_spatial_horizontal_velocity_for_jump(spatial, velocity, start, face_center);
    if (!(jump.success && jump.velocity < 300)) continue;
    qa_vec3 command = qa_scale3(qa_normalize3(with_z(qa_sub3(face_center, start), 0)), jump.velocity);
    qa_bot_movement_prediction request; memset(&request, 0, sizeof request);
    request.entity_num = -1; request.origin = start; request.presence = 2; request.on_ground = true;
    request.velocity = v3f(0, 0, velocity); request.command_move = command;
    request.command_frames = 30; request.max_frames = 30; request.frame_time = fr(0.1);
    request.stop_events = (LAND_EVENTS & ~QA_AAS_STOP_TOUCH_TELEPORTER) | QA_AAS_STOP_HIT_GROUND_AREA;
    request.stop_area = to; request.visualize = false;
    qa_aas_client_move move = qa_aas_spatial_predict_client_movement(spatial, request);
    if (move.frames >= 30 || (move.stop_event & HAZARD_EVENTS) != 0 ||
        (move.stop_event & (QA_AAS_STOP_HIT_GROUND_AREA | QA_AAS_STOP_TOUCH_JUMP_PAD)) == 0) continue;
    qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(c);
    if (!reach) return false;
    qa_aas_link_set_area(reach, to);
    qa_aas_link_set_face(reach, 0);
    qa_aas_link_set_edge(reach, 0);
    qa_aas_link_set_start(reach, start);
    qa_aas_link_set_end(reach, face_center);
    qa_aas_link_set_travel_type(reach, QA_TRAVEL_ROCKETJUMP);
    qa_aas_link_set_travel_time(reach, settings->rocket_jump_time);
    link_reach_s(special, from, reach);
    qa_aas_reachability_debug_count(&c->debug_state, "rocketjump");
    return true;
  }
  return false;
}
