/* Port of src/bots/navigation/aas-reachability-spatial.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/aas_reachability_spatial.h"
#include "qa/bots/navigation/aas_reachability_geometry.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/world/collision/contents.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

static float fr(double v) { return (float)v; }
static qa_vec3 v3f(double x, double y, double z) { return qa_math_vec3(x, y, z); }
static qa_vec3 ma(qa_vec3 start, double amount, qa_vec3 direction) { return qa_add3(start, qa_scale3(direction, amount)); }
static bool is_space_char(char c) { return c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r' || c == ' '; }
static int64_t decimal_integer(const char *text) {
  size_t i = 0;
  while (text[i] && is_space_char(text[i])) i++;
  bool negative = false;
  size_t digits_start;
  if (text[i] == '+' || text[i] == '-') { negative = text[i] == '-'; i++; }
  digits_start = i;
  uint64_t magnitude = 0;
  bool overflow = false;
  for (; text[i] >= '0' && text[i] <= '9'; i++) {
    unsigned digit = (unsigned)(text[i] - '0');
    if (magnitude > (UINT64_MAX - digit) / 10) overflow = true;
    else magnitude = magnitude * 10 + digit;
  }
  /* Map text is untrusted and atoi has no failure path; an oversized value saturates. */
  if (i > digits_start && (overflow || magnitude > (negative ? 2147483648ULL : 2147483647ULL)))
    return negative ? INT32_MIN : INT32_MAX;
  return qa_native_atoi(text);
}
static bool is_digit_char(char c) { return c >= '0' && c <= '9'; }
static bool is_hex_char(char c) {
  return is_digit_char(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static bool is_word_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit_char(c) || c == '_';
}
static bool scan_float(const char *text, size_t start, double *out_value, size_t *out_end) {
  size_t length = strlen(text);
  size_t cursor = start;
  while (cursor < length && is_space_char(text[cursor])) cursor++;
  size_t beginning = cursor;
  if (cursor < length && (text[cursor] == '+' || text[cursor] == '-')) cursor++;
  char word[16];
  size_t wlen = 0;
  for (size_t i = cursor; i < length && wlen + 1 < sizeof word; i++) {
    char c = text[i];
    word[wlen++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  }
  word[wlen] = 0;
  if (strncmp(word, "inf", 3) == 0) {
    if (word[3] == 'i') {
      if (strncmp(word, "infinity", 8) != 0) return false;
      cursor += 8;
    } else cursor += 3;
  } else if (strncmp(word, "nan", 3) == 0) {
    cursor += 3;
    if (cursor < length && text[cursor] == '(') {
      cursor++;
      while (cursor < length && is_word_char(text[cursor])) cursor++;
      if (cursor >= length || text[cursor] != ')') return false;
      cursor++;
    }
  } else {
    bool hexadecimal = strncmp(word, "0x", 2) == 0;
    if (hexadecimal) cursor += 2;
    size_t digits = 0;
    while (cursor < length && (hexadecimal ? is_hex_char(text[cursor]) : is_digit_char(text[cursor]))) { cursor++; digits++; }
    if (cursor < length && text[cursor] == '.') {
      cursor++;
      while (cursor < length && (hexadecimal ? is_hex_char(text[cursor]) : is_digit_char(text[cursor]))) { cursor++; digits++; }
    }
    if (digits == 0) return false;
    char exponent = cursor < length ? text[cursor] : 0;
    if (exponent >= 'A' && exponent <= 'Z') exponent = (char)(exponent - 'A' + 'a');
    if (exponent == (hexadecimal ? 'p' : 'e')) {
      cursor++;
      if (cursor < length && (text[cursor] == '+' || text[cursor] == '-')) cursor++;
      size_t exponent_start = cursor;
      while (cursor < length && is_digit_char(text[cursor])) cursor++;
      if (cursor == exponent_start) return false;
    }
  }
  size_t slice = cursor - beginning;
  char *copy = malloc(slice + 1);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, text + beginning, slice);
  copy[slice] = 0;
  *out_value = qa_native_atof(copy);
  free(copy);
  *out_end = cursor;
  return true;
}
bool qa_aas_reachability_entities_init(qa_aas_reachability_entities *entities, const char *text) {
  memset(entities, 0, sizeof *entities);
  entities->records = qa_q3_parse_entities(text ? text : "", NULL, &entities->count);
  if (!entities->records && entities->count > 0) return false;
  if (entities->count >= 2048) qa_range_error("Too many BSP entities for AAS generation");
  return true;
}
void qa_aas_reachability_entities_destroy(qa_aas_reachability_entities *entities) {
  if (!entities) return;
  qa_q3_entities_destroy(entities->records, entities->count);
  memset(entities, 0, sizeof *entities);
}
int64_t qa_aas_reachability_entities_next(qa_aas_reachability_entities *entities, int64_t previous) {
  int64_t next = previous + 1;
  return next > 0 && next <= (int64_t)entities->count ? next : 0;
}
static const char *entity_raw(qa_aas_reachability_entities *entities, int64_t entity, const char *key) {
  if (entity < 1 || entity > (int64_t)entities->count) return NULL;
  return qa_q3_entity_get(&entities->records[entity - 1], key);
}
bool qa_aas_reachability_entities_value(qa_aas_reachability_entities *entities, int64_t entity, const char *key,
                                       uint8_t *output, size_t output_length) {
  if (output_length == 0) qa_range_error("Empty BSP epair output");
  output[0] = 0;
  const char *value = entity_raw(entities, entity, key);
  if (!value) return false;
  memset(output, 0, output_length);
  size_t length = strlen(value);
  size_t limit = length < output_length - 1 ? length : output_length - 1;
  for (size_t i = 0; i < limit; i++) output[i] = (uint8_t)value[i];
  return true;
}
static bool entity_text(qa_aas_reachability_entities *entities, int64_t entity, const char *key, char *out, size_t out_cap) {
  const char *value = entity_raw(entities, entity, key);
  if (!value) return false;
  size_t length = strlen(value);
  if (length > 127) length = 127;
  if (length >= out_cap) length = out_cap - 1;
  memcpy(out, value, length);
  out[length] = 0;
  return true;
}
bool qa_aas_reachability_entities_int(qa_aas_reachability_entities *entities, int64_t entity, const char *key, int64_t *out) {
  char text[128];
  if (!entity_text(entities, entity, key, text, sizeof text)) { *out = 0; return false; }
  *out = decimal_integer(text);
  return true;
}
bool qa_aas_reachability_entities_float(qa_aas_reachability_entities *entities, int64_t entity, const char *key, double *out) {
  char text[128];
  if (!entity_text(entities, entity, key, text, sizeof text)) { *out = 0; return false; }
  *out = fr(qa_native_atof(text));
  return true;
}
bool qa_aas_reachability_entities_vector(qa_aas_reachability_entities *entities, int64_t entity, const char *key, qa_vec3 *out) {
  char text[128];
  *out = (qa_vec3){0, 0, 0};
  if (!entity_text(entities, entity, key, text, sizeof text)) return false;
  size_t cursor = 0;
  double components[3] = {0, 0, 0};
  for (int axis = 0; axis < 3; axis++) {
    double value; size_t end;
    if (!scan_float(text, cursor, &value, &end)) break;
    components[axis] = fr(value);
    cursor = end;
  }
  *out = (qa_vec3){components[0], components[1], components[2]};
  return true;
}

void qa_aas_reachability_spatial_init(qa_aas_reachability_spatial *spatial, qa_aas_reachability_options options,
                                      qa_aas_reachability_world *world, qa_aas_reachability_entities *entities,
                                      const qa_aas_movement_settings *settings) {
  spatial->options = options;
  spatial->world = world;
  spatial->entities = entities;
  spatial->settings = settings;
}
static qa_trace_policy q3_policy(double mask) {
  qa_trace_policy policy; memset(&policy, 0, sizeof policy);
  policy.kind = "q3"; policy.contents_mask = mask; policy.curves = true; policy.player_curve_clip = true;
  return policy;
}
double qa_aas_spatial_point_contents(qa_aas_reachability_spatial *spatial, qa_vec3 point) {
  qa_point_contents_query query; memset(&query, 0, sizeof query);
  query.point = point;
  query.policy = q3_policy(-1);
  query.numeric = spatial->options.profile.movement.numeric;
  query.pass_actor = NULL;
  qa_point_contents_result value = spatial->options.scene.point_contents(spatial->options.scene.ctx, query);
  const char *kind = value.kind ? value.kind : "q3";
  if (!strcmp(kind, "q2")) return qa_convert_contents(value.merged, "q2", "q3");
  return qa_convert_contents(value.contents, kind, "q3");
}
qa_aas_host_trace qa_aas_spatial_trace(qa_aas_reachability_spatial *spatial, qa_vec3 start, qa_vec3 end,
                                       bool has_bounds, qa_bounds bounds, int64_t pass_entity, double mask) {
  (void)pass_entity;
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.start = start; query.end = end;
  query.shape.kind = has_bounds ? "box" : "point";
  query.shape.bounds = bounds;
  query.policy = q3_policy(mask);
  query.numeric = spatial->options.profile.movement.numeric;
  query.pass_actor = NULL;
  qa_trace_result trace = spatial->options.scene.trace(spatial->options.scene.ctx, query);
  const char *kind = trace.kind ? trace.kind : "q3";
  double flags;
  if (!strcmp(kind, "q2")) flags = trace.surface ? trace.surface->flags : 0;
  else flags = trace.has_surface_flags ? trace.surface_flags : 0;
  qa_aas_host_trace out;
  out.start_solid = trace.start_solid;
  out.fraction = trace.fraction;
  out.end = trace.end;
  out.surface_flags = qa_convert_surface_flags(flags, kind, "q3");
  return out;
}
void qa_aas_spatial_model_bounds(qa_aas_reachability_spatial *spatial, int64_t model, qa_vec3 angles,
                                 qa_bounds *out_bounds, qa_vec3 *out_origin) {
  qa_decoded_world geometry = spatial->options.geometry;
  qa_bounds bounds;
  size_t count = 0;
  bool found = true;
  switch (geometry.kind) {
    case QA_DECODED_WORLD_Q1:
      count = geometry.as.q1->model_count;
      if (model < 0 || model >= (int64_t)count) found = false; else bounds = geometry.as.q1->models[model].bounds;
      break;
    case QA_DECODED_WORLD_Q2:
      count = geometry.as.q2->model_count;
      if (model < 0 || model >= (int64_t)count) found = false; else bounds = geometry.as.q2->models[model].bounds;
      break;
    case QA_DECODED_WORLD_Q3:
      count = geometry.as.q3->model_count;
      if (model < 0 || model >= (int64_t)count) found = false; else bounds = geometry.as.q3->models[model].bounds;
      break;
    default: found = false; bounds = (qa_bounds){{0, 0, 0}, {0, 0, 0}}; break;
  }
  if (!found) qa_range_error("AAS reachability index %lld outside %zu", (long long)model, count);
  if (angles.x != 0 || angles.y != 0 || angles.z != 0) {
    double radius = qa_length3(v3f(qa_js_max(fabs(bounds.min.x), fabs(bounds.max.x)),
                                   qa_js_max(fabs(bounds.min.y), fabs(bounds.max.y)),
                                   qa_js_max(fabs(bounds.min.z), fabs(bounds.max.z))));
    bounds = (qa_bounds){v3f(-radius, -radius, -radius), v3f(radius, radius, radius)};
  }
  *out_bounds = bounds;
  *out_origin = v3f(0, 0, 0);
}
qa_aas_client_move qa_aas_spatial_predict_client_movement(qa_aas_reachability_spatial *spatial, qa_bot_movement_prediction request) {
  request.entity_num = spatial->options.prediction_client;
  qa_bot_travel_prediction_result result = spatial->options.predict_client_movement(spatial->options.predict_ctx, request);
  if (!result.has_end_area) qa_range_error("AAS generation prediction requires an AAS end area");
  double frames = result.stop_event == 0 ? result.frames : qa_js_max(0, result.frames - 1);
  qa_aas_client_move move;
  move.end = result.end;
  move.velocity = result.velocity;
  move.frames = (int64_t)frames;
  move.time = fr(frames * request.frame_time);
  move.stop_event = (int64_t)result.stop_event;
  move.end_area = (int64_t)result.end_area;
  return move;
}
qa_aas_jump_speed qa_aas_spatial_horizontal_velocity_for_jump(qa_aas_reachability_spatial *spatial, double z_velocity, qa_vec3 start, qa_vec3 end) {
  float gravity = spatial->settings->gravity, maximum = spatial->settings->max_velocity;
  float ascent = fr(z_velocity / gravity);
  float maximum_jump = fr(0.5 * (double)gravity * (double)ascent * (double)ascent);
  float height = fr((double)fr(start.z + maximum_jump) - end.z);
  if (height < 0) return (qa_aas_jump_speed){false, maximum};
  float time = fr(sqrt((double)height / (0.5 * (double)gravity)));
  float denominator = fr((double)time + (double)ascent);
  if (denominator == 0) return (qa_aas_jump_speed){false, maximum};
  qa_vec3 direction = qa_sub3(end, start);
  float speed = fr(sqrt((double)fr((double)fr(direction.x * direction.x) + (double)fr(direction.y * direction.y))) / (double)denominator);
  return speed > maximum ? (qa_aas_jump_speed){false, maximum} : (qa_aas_jump_speed){true, speed};
}
double qa_aas_spatial_rocket_jump_z_velocity(qa_aas_reachability_spatial *spatial, qa_vec3 origin) {
  qa_angle_vectors vectors = qa_math_angle_vectors(v3f(90, 0, 0));
  qa_vec3 forward = vectors.forward, right = vectors.right;
  qa_vec3 start = v3f(fr(origin.x + (double)fr((double)fr(forward.x * 8) + (double)fr(right.x * 8))),
                      fr(origin.y + (double)fr((double)fr(forward.y * 8) + (double)fr(right.y * 8))),
                      fr((double)fr(origin.z + 8) + (double)fr((double)fr((double)fr(forward.z * 8) + (double)fr(right.z * 8)) - 8)));
  qa_aas_host_trace trace = qa_aas_spatial_trace(spatial, start, ma(start, 500, forward), false, (qa_bounds){{0, 0, 0}, {0, 0, 0}}, 1, 1);
  double distance = qa_length3(qa_sub3(trace.end, v3f(origin.x, origin.y, origin.z + 4)));
  double points = fr(qa_js_max(0, (double)fr(120 - 0.5 * distance)) * 0.5);
  qa_vec3 direction = qa_normalize3(qa_sub3(origin, trace.end));
  return fr((double)fr(direction.z * (1600 * points / 200)) + (double)spatial->settings->jump_velocity);
}
qa_aas_area_crossing *qa_aas_spatial_trace_areas(qa_aas_reachability_spatial *spatial, qa_vec3 start, qa_vec3 end,
                                                 int64_t maximum, size_t *out_count) {
  return qa_aas_trace_areas(&spatial->world->asset, start, end, maximum, out_count);
}
static void spatial_trace_print(void *ctx, const char *text) {
  qa_aas_reachability_spatial *spatial = ctx;
  if (spatial->options.print) spatial->options.print(spatial->options.print_ctx, 3, text);
}
qa_aas_trace qa_aas_spatial_trace_client_bbox(qa_aas_reachability_spatial *spatial, qa_vec3 start, qa_vec3 end,
                                              int64_t presence, int64_t pass_entity) {
  (void)pass_entity;
  return qa_aas_trace_client_bbox(&spatial->world->asset, start, end, presence, spatial_trace_print, spatial);
}
void qa_aas_area_link_free(qa_aas_area_link *head) {
  while (head) { qa_aas_area_link *next = head->next_area; free(head); head = next; }
}
qa_aas_area_link *qa_aas_spatial_link_client_bounds(qa_aas_reachability_spatial *spatial, int64_t entity, qa_bounds bounds, int64_t presence) {
  (void)entity;
  qa_aas_asset *world = &spatial->world->asset;
  const qa_bounds *client = NULL;
  qa_bounds found;
  for (size_t i = 0; i < world->bbox_count; i++)
    if (world->bboxes[i].presence == presence) { found = world->bboxes[i].bounds; client = &found; break; }
  if (!client) {
    if (presence == 4) {
      if (!spatial->options.profile.has_crouched_shape) qa_range_error("Missing AAS presence bounds %lld", (long long)presence);
      found = spatial->options.profile.crouched_shape.bounds;
    } else found = spatial->options.profile.shape.bounds;
    client = &found;
  }
  qa_bounds expanded = {qa_sub3(bounds.min, client->max), qa_sub3(bounds.max, client->min)};
  size_t count = 0;
  int64_t *areas = qa_aas_bbox_areas(world, expanded, (int64_t)world->area_count, &count);
  qa_aas_area_link *head = NULL;
  for (size_t i = count; i-- > 0;) {
    qa_aas_area_link *link = malloc(sizeof *link);
    if (!link) { free(areas); qa_aas_area_link_free(head); qa_range_error("Out of memory"); }
    link->area = qa_aas_at_i64(areas, count, (int64_t)i);
    link->next_area = head;
    head = link;
  }
  free(areas);
  return head;
}
qa_aas_best_area qa_aas_spatial_best_reachable_area(qa_aas_reachability_spatial *spatial, qa_vec3 origin, qa_bounds bounds) {
  qa_vec3 start = origin;
  int64_t area = qa_aas_reachability_world_point_area(spatial->world, start);
  for (int i = 0; i < 5 && area == 0; i++)
    for (int j = 0; j < 5 && area == 0; j++)
      for (int k = -1; k <= 1 && area == 0; k++)
        for (int l = -1; l <= 1 && area == 0; l++) {
          start = v3f(origin.x + j * 4 * k, origin.y + j * 4 * l, origin.z + i * 4);
          area = qa_aas_reachability_world_point_area(spatial->world, start);
        }
  if (area != 0) {
    qa_vec3 end = v3f(start.x, start.y, start.z - 50);
    start = v3f(start.x, start.y, start.z + 0.25);
    qa_aas_trace trace = qa_aas_spatial_trace_client_bbox(spatial, start, end, 4, -1);
    if (trace.start_solid) return (qa_aas_best_area){area, start};
    area = qa_aas_reachability_world_point_area(spatial->world, trace.end);
    if (area != 0) return (qa_aas_best_area){area, trace.end};
  }
  qa_bounds expanded = {qa_add3(origin, bounds.min), qa_add3(origin, bounds.max)};
  qa_aas_area_link *head = qa_aas_spatial_link_client_bounds(spatial, -1, expanded, 4);
  qa_aas_best_area result = {0, origin};
  for (qa_aas_area_link *link = head; link; link = link->next_area)
    if ((qa_aas_at_setting(&spatial->world->asset, link->area)->flags & 5) != 0) { result = (qa_aas_best_area){link->area, origin}; goto done; }
  for (qa_aas_area_link *link = head; link; link = link->next_area)
    if (link->area != 0) { result = (qa_aas_best_area){link->area, origin}; goto done; }
done:
  qa_aas_area_link_free(head);
  return result;
}
bool qa_aas_spatial_drop_to_floor(qa_aas_reachability_spatial *spatial, qa_vec3 origin, qa_bounds bounds, qa_vec3 *out_origin) {
  qa_aas_host_trace trace = qa_aas_spatial_trace(spatial, origin, v3f(origin.x, origin.y, origin.z - 100), true, bounds, 0, 1);
  if (trace.start_solid) { *out_origin = origin; return false; }
  *out_origin = trace.end;
  return true;
}
bool qa_aas_spatial_point_inside_face(qa_aas_reachability_spatial *spatial, int64_t number, qa_vec3 point, double epsilon) {
  qa_aas_asset *world = &spatial->world->asset;
  const qa_aas_face_rec *face = qa_aas_at_face(world, number);
  qa_vec3 normal = qa_aas_at_plane(world, face->plane)->normal;
  for (int64_t i = 0; i < face->edge_count; i++) {
    int64_t signed_number = qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face->first_edge + i);
    const qa_aas_edge_rec *edge = qa_aas_at_edge(world, signed_number < 0 ? -signed_number : signed_number);
    qa_vec3 origin = qa_aas_at_vertex(world, edge->vertices[signed_number < 0 ? 1 : 0]);
    qa_vec3 direction = qa_sub3(qa_aas_at_vertex(world, edge->vertices[signed_number < 0 ? 0 : 1]), origin);
    if (qa_dot3(qa_sub3(point, origin), qa_cross3(direction, normal)) < -(double)fr(epsilon)) return false;
  }
  return true;
}
static void buffer_text(const uint8_t *bytes, size_t length, char *out, size_t out_cap) {
  size_t n = 0;
  while (n < length && bytes[n] != 0 && n + 1 < out_cap) { out[n] = (char)bytes[n]; n++; }
  out[n] = 0;
}
bool qa_aas_spatial_get_jump_pad_info(qa_aas_reachability_spatial *spatial, int64_t entity, qa_aas_jump_pad_info *out) {
  uint8_t model_buffer[128], target_buffer[128], name_buffer[128];
  char model_name[129], target_name[129], name[129];
  qa_aas_reachability_entities_value(spatial->entities, entity, "model", model_buffer, sizeof model_buffer);
  buffer_text(model_buffer, sizeof model_buffer, model_name, sizeof model_name);
  qa_bounds model_bounds; qa_vec3 model_origin;
  qa_aas_spatial_model_bounds(spatial, model_name[0] == 0 ? 0 : qa_native_atoi(model_name + 1), v3f(0, 0, 0), &model_bounds, &model_origin);
  qa_bounds bounds = {qa_add3(model_origin, model_bounds.min), qa_add3(model_origin, model_bounds.max)};
  qa_vec3 center = qa_scale3(qa_add3(bounds.min, bounds.max), 0.5);
  qa_aas_trace trace = qa_aas_spatial_trace_client_bbox(spatial, v3f(center.x, center.y, center.z + 64), center, 4, -1);
  qa_vec3 bottom = trace.start_solid ? center : trace.end;
  qa_vec3 start = v3f(bottom.x, bottom.y, bottom.z + 0.125);
  qa_aas_reachability_entities_value(spatial->entities, entity, "target", target_buffer, sizeof target_buffer);
  buffer_text(target_buffer, sizeof target_buffer, target_name, sizeof target_name);
  int64_t target = qa_aas_reachability_entities_next(spatial->entities, 0);
  for (; target != 0; target = qa_aas_reachability_entities_next(spatial->entities, target)) {
    if (!qa_aas_reachability_entities_value(spatial->entities, target, "targetname", name_buffer, sizeof name_buffer)) continue;
    buffer_text(name_buffer, sizeof name_buffer, name, sizeof name);
    if (!strcmp(name, target_name)) break;
  }
  if (target == 0) {
    if (spatial->options.print) {
      char message[512];
      snprintf(message, sizeof message, "trigger_push without target entity %s\n", target_name);
      spatial->options.print(spatial->options.print_ctx, 1, message);
    }
    return false;
  }
  qa_vec3 destination;
  (void)qa_aas_reachability_entities_vector(spatial->entities, target, "origin", &destination);
  float height = fr(destination.z - center.z);
  float time = fr(sqrt((double)height / (0.5 * (double)fr(spatial->settings->gravity))));
  if (time == 0) return false;
  if (!isfinite(time)) qa_range_error("Jump-pad target produces a non-finite source flight time");
  qa_vec3 delta = qa_sub3(destination, center);
  double distance = qa_length3(delta);
  float forward = fr((double)fr(distance / (double)time) * (double)fr(1.1));
  qa_vec3 push = qa_scale3(qa_normalize3(delta), forward);
  out->start = start;
  out->bounds = bounds;
  out->velocity = v3f(push.x, push.y, (double)time * (double)fr(spatial->settings->gravity));
  return true;
}
