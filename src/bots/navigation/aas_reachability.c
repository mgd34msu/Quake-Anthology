/* Port of src/bots/navigation/aas-reachability.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/aas_reachability.h"
#include "qa/bots/navigation/aas_reachability_geometry.h"
#include "qa/bots/navigation/aas_reachability_spatial.h"
#include "qa/bots/navigation/aas_reachability_special.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

#define MAX_REACHABILITY 65536

int64_t qa_aas_reachability_world_point_area(const qa_aas_reachability_world *world, qa_vec3 point) {
  return qa_aas_point_area(world->source, point);
}
qa_aas_setting_rec *qa_aas_reachability_world_setting(qa_aas_reachability_world *world, int64_t area) {
  return qa_aas_at_setting_mut(&world->asset, area);
}
static char *dups(const char *s) {
  size_t n = strlen(s); char *p = malloc(n + 1); if (p) memcpy(p, s, n + 1); return p;
}
void qa_aas_reachability_debug_count(qa_aas_reachability_debug_state *state, const char *category) {
  for (size_t i = 0; i < state->count; i++)
    if (!strcmp(state->entries[i].category, category)) { state->entries[i].count++; return; }
  if (state->count == state->cap) {
    size_t c = state->cap ? state->cap * 2 : 8;
    qa_aas_reachability_debug_entry *p = realloc(state->entries, c * sizeof *p);
    if (!p) qa_range_error("Out of memory");
    state->entries = p; state->cap = c;
  }
  char *copy = dups(category);
  if (!copy) qa_range_error("Out of memory");
  state->entries[state->count++] = (qa_aas_reachability_debug_entry){copy, 1};
}
void qa_aas_reachability_debug_state_destroy(qa_aas_reachability_debug_state *state) {
  if (!state) return;
  for (size_t i = 0; i < state->count; i++) free(state->entries[i].category);
  free(state->entries);
  memset(state, 0, sizeof *state);
}
/* little-endian DataView access */
static int32_t read_i32(const uint8_t *b, size_t off) {
  uint32_t v = (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
  int32_t out; memcpy(&out, &v, sizeof out); return out;
}
static void write_i32(uint8_t *b, size_t off, int64_t value) {
  int32_t v = (int32_t)value; /* DataView setInt32 wraps via ToInt32 */
  uint32_t u; memcpy(&u, &v, sizeof u);
  b[off] = (uint8_t)u; b[off + 1] = (uint8_t)(u >> 8); b[off + 2] = (uint8_t)(u >> 16); b[off + 3] = (uint8_t)(u >> 24);
}
static double read_f32(const uint8_t *b, size_t off) {
  uint32_t u = (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
  float v; memcpy(&v, &u, sizeof v); return v;
}
static void write_f32(uint8_t *b, size_t off, double value) {
  float v = (float)value;
  uint32_t u; memcpy(&u, &v, sizeof u);
  b[off] = (uint8_t)u; b[off + 1] = (uint8_t)(u >> 8); b[off + 2] = (uint8_t)(u >> 16); b[off + 3] = (uint8_t)(u >> 24);
}
int64_t qa_aas_link_area(const qa_aas_linked_reachability *l) { return read_i32(l->bytes, 0); }
void qa_aas_link_set_area(qa_aas_linked_reachability *l, int64_t v) { write_i32(l->bytes, 0, v); }
int64_t qa_aas_link_face(const qa_aas_linked_reachability *l) { return read_i32(l->bytes, 4); }
void qa_aas_link_set_face(qa_aas_linked_reachability *l, int64_t v) { write_i32(l->bytes, 4, v); }
int64_t qa_aas_link_edge(const qa_aas_linked_reachability *l) { return read_i32(l->bytes, 8); }
void qa_aas_link_set_edge(qa_aas_linked_reachability *l, int64_t v) { write_i32(l->bytes, 8, v); }
qa_vec3 qa_aas_link_start(const qa_aas_linked_reachability *l) {
  return (qa_vec3){read_f32(l->bytes, 12), read_f32(l->bytes, 16), read_f32(l->bytes, 20)};
}
void qa_aas_link_set_start(qa_aas_linked_reachability *l, qa_vec3 v) {
  write_f32(l->bytes, 12, v.x); write_f32(l->bytes, 16, v.y); write_f32(l->bytes, 20, v.z);
}
qa_vec3 qa_aas_link_end(const qa_aas_linked_reachability *l) {
  return (qa_vec3){read_f32(l->bytes, 24), read_f32(l->bytes, 28), read_f32(l->bytes, 32)};
}
void qa_aas_link_set_end(qa_aas_linked_reachability *l, qa_vec3 v) {
  write_f32(l->bytes, 24, v.x); write_f32(l->bytes, 28, v.y); write_f32(l->bytes, 32, v.z);
}
int64_t qa_aas_link_travel_type(const qa_aas_linked_reachability *l) { return read_i32(l->bytes, 36); }
void qa_aas_link_set_travel_type(qa_aas_linked_reachability *l, int64_t v) { write_i32(l->bytes, 36, v); }
int64_t qa_aas_link_travel_time(const qa_aas_linked_reachability *l) {
  return (int64_t)((uint32_t)l->bytes[40] | ((uint32_t)l->bytes[41] << 8));
}
void qa_aas_link_set_travel_time(qa_aas_linked_reachability *l, double value) {
  double integer = trunc(value);
  if (!isfinite(integer) || integer < -2147483648.0 || integer > 2147483647.0)
    qa_range_error("AAS reachability travel time exceeds source integer conversion range");
  uint32_t u = (uint32_t)qa_js_to_int32(integer) & 0xffffu;
  l->bytes[40] = (uint8_t)u; l->bytes[41] = (uint8_t)(u >> 8);
}
void qa_aas_link_clear(qa_aas_linked_reachability *l) {
  memset(l->bytes, 0, sizeof l->bytes);
  l->next = NULL;
}
void qa_aas_reachability_context_print(qa_aas_reachability_context *c, int64_t severity, const char *text) {
  if (c->print) c->print(c->print_ctx, severity, text);
}
void qa_aas_reachability_context_log(qa_aas_reachability_context *c, const char *text) {
  qa_aas_reachability_context_print(c, 1, text);
}
void qa_aas_reachability_context_permanent_line(qa_aas_reachability_context *c, qa_vec3 start, qa_vec3 end, double color) {
  if (c->permanent_line) c->permanent_line(c->debug_line_ctx, start, end, color);
}
double qa_aas_reachability_context_variable(qa_aas_reachability_context *c, const char *name, const char *initial) {
  return c->variable(c->variable_ctx, name, initial);
}
qa_aas_linked_reachability *qa_aas_reachability_context_allocate(qa_aas_reachability_context *c) {
  if (c->allocated >= MAX_REACHABILITY) {
    qa_aas_reachability_context_print(c, 4, "AAS_MAX_REACHABILITYSIZE");
    return NULL;
  }
  c->allocated++;
  if (c->free_list) {
    qa_aas_linked_reachability *link = c->free_list;
    c->free_list = link->next;
    link->next = NULL;
    return link;
  }
  qa_aas_linked_reachability *link = calloc(1, sizeof *link);
  if (!link) c->allocated--;
  return link;
}
void qa_aas_reachability_context_free_link(qa_aas_reachability_context *c, qa_aas_linked_reachability *link) {
  qa_aas_link_clear(link);
  link->next = c->free_list;
  c->free_list = link;
  c->allocated--;
}
bool qa_aas_reachability_context_exists(qa_aas_reachability_context *c, int64_t from, int64_t to) {
  for (qa_aas_linked_reachability *link = qa_aas_at_head(c, from); link; link = link->next)
    if (qa_aas_link_area(link) == to) return true;
  return false;
}
void qa_aas_reachability_result_destroy(qa_aas_asset *asset) {
  if (!asset) return;
  free(asset->settings);
  free(asset->reachability);
  asset->settings = NULL; asset->setting_count = 0;
  asset->reachability = NULL; asset->reachability_count = 0;
}
static double default_variable(void *ctx, const char *name, const char *initial) {
  (void)ctx; (void)name;
  return qa_js_string_to_number(initial); /* Number(initial) */
}
static const char *geometry_entities(qa_decoded_world geometry) {
  switch (geometry.kind) {
    case QA_DECODED_WORLD_Q1: return geometry.as.q1->entities;
    case QA_DECODED_WORLD_Q2: return geometry.as.q2->entities;
    case QA_DECODED_WORLD_Q3: return geometry.as.q3->entities;
  }
  return "";
}
bool qa_build_aas_reachability(qa_aas_asset *out, bool *out_reused, qa_aas_reachability_options options) {
  *out_reused = false;
  const qa_aas_asset *asset = options.asset;
  if (asset->reachability_count > 0 && !options.force) { *out_reused = true; return true; }
  if (options.prediction_client < 0 || options.prediction_client > 9007199254740991LL)
    qa_range_error("AAS generation requires a selected prediction client");
  qa_aas_setting_rec *settings = NULL;
  if (asset->setting_count) {
    settings = malloc(asset->setting_count * sizeof *settings);
    if (!settings) return false;
    memcpy(settings, asset->settings, asset->setting_count * sizeof *settings);
  }
  qa_aas_reachability_world world;
  world.asset = *asset;
  world.asset.settings = settings;
  world.source = asset;
  qa_aas_reachability_variable_fn variable = options.variable ? options.variable : default_variable;
  void *variable_ctx = options.variable ? options.variable_ctx : NULL;
  qa_aas_movement_settings movement_settings;
  if (options.has_settings) movement_settings = options.settings;
  else qa_init_aas_movement_settings(&movement_settings, variable_ctx, variable);
  qa_aas_reachability_entities bsp_entities;
  if (!qa_aas_reachability_entities_init(&bsp_entities, geometry_entities(options.geometry))) {
    free(settings);
    return false;
  }
  qa_aas_reachability_spatial spatial;
  qa_aas_reachability_spatial_init(&spatial, options, &world, &bsp_entities, &movement_settings);
  qa_aas_linked_reachability **heads = calloc(asset->area_count ? asset->area_count : 1, sizeof *heads);
  if (!heads) {
    qa_aas_reachability_entities_destroy(&bsp_entities);
    free(settings);
    return false;
  }
  qa_aas_reachability_context context; memset(&context, 0, sizeof context);
  context.world = &world;
  context.spatial = &spatial;
  context.bsp_entities = &bsp_entities;
  context.settings = &movement_settings;
  context.variable_ctx = variable_ctx;
  context.variable = variable;
  context.debug = options.debug;
  context.heads = heads;
  context.head_count = asset->area_count;
  context.print_ctx = options.print_ctx;
  context.print = options.print;
  context.debug_line_ctx = options.debug_line_ctx;
  context.permanent_line = options.debug_line;
  qa_aas_reachability_geometry geometry;
  qa_aas_reachability_geometry_init(&geometry, &context);
  qa_aas_reachability_special special;
  qa_aas_reachability_special_init(&special, &context);
  qa_aas_reachability_special_set_weapon_jump_area_flags(&special);
  bool grapple = trunc(variable(variable_ctx, "grapplereach", "0")) != 0;
  for (int64_t from = 1; from < (int64_t)asset->area_count; from++) {
    if ((qa_aas_at_setting(&world.asset, from)->contents & 128) != 0) continue;
    for (int64_t to = 1; to < (int64_t)asset->area_count; to++) {
      if (from == to || qa_aas_reachability_context_exists(&context, from, to)) continue;
      if ((qa_aas_at_setting(&world.asset, from)->contents & (64 | 128)) != 0 &&
          (qa_aas_at_setting(&world.asset, to)->contents & (64 | 128)) == 0) continue;
      if (qa_aas_reachability_geometry_swim(&geometry, from, to) ||
          qa_aas_reachability_geometry_equal_floor_height(&geometry, from, to) ||
          qa_aas_reachability_geometry_step_barrier_water_jump_walk_off_ledge(&geometry, from, to) ||
          qa_aas_reachability_geometry_ladder(&geometry, from, to) ||
          qa_aas_reachability_geometry_jump(&geometry, from, to)) continue;
    }
    if ((qa_aas_at_setting(&world.asset, from)->contents & (64 | 128)) != 0) continue;
    for (int64_t to = 1; to < (int64_t)asset->area_count; to++) {
      if (from == to || qa_aas_reachability_context_exists(&context, from, to)) continue;
      if (grapple) qa_aas_reachability_special_grapple(&special, from, to);
      qa_aas_reachability_special_weapon_jump(&special, from, to);
    }
  }
  for (int64_t area = 1; area < (int64_t)asset->area_count; area++)
    if ((qa_aas_at_setting(&world.asset, area)->contents & 128) == 0)
      qa_aas_reachability_geometry_walk_off_ledge(&geometry, area);
  qa_aas_reachability_special_jump_pad(&special);
  qa_aas_reachability_special_teleport(&special);
  qa_aas_reachability_special_elevator(&special);
  qa_aas_reachability_special_func_bobbing(&special);
  size_t total = 1;
  for (size_t i = 0; i < asset->area_count; i++)
    for (qa_aas_linked_reachability *link = heads[i]; link; link = link->next) total++;
  qa_aas_reach_rec *reachability = calloc(total, sizeof *reachability);
  if (!reachability) {
    for (size_t i = 0; i < asset->area_count; i++) {
      qa_aas_linked_reachability *link = heads[i];
      while (link) { qa_aas_linked_reachability *next = link->next; free(link); link = next; }
    }
    while (context.free_list) { qa_aas_linked_reachability *next = context.free_list->next; free(context.free_list); context.free_list = next; }
    free(heads);
    qa_aas_reachability_debug_state_destroy(&context.debug_state);
    qa_aas_reachability_entities_destroy(&bsp_entities);
    free(settings);
    return false;
  }
  size_t at = 1;
  for (int64_t area = 0; area < (int64_t)asset->area_count; area++) {
    qa_aas_setting_rec *setting = qa_aas_at_setting_mut(&world.asset, area);
    setting->first_reach = (int64_t)at;
    setting->reach_count = 0;
    for (qa_aas_linked_reachability *link = qa_aas_at_head(&context, area); link; link = link->next) {
      reachability[at].area = qa_aas_link_area(link);
      reachability[at].face = qa_aas_link_face(link);
      reachability[at].edge = qa_aas_link_edge(link);
      reachability[at].start = qa_aas_link_start(link);
      reachability[at].end = qa_aas_link_end(link);
      reachability[at].travel_type = qa_aas_link_travel_type(link);
      reachability[at].travel_time = qa_aas_link_travel_time(link);
      reachability[at].padding = 0;
      at++;
      setting->reach_count++;
    }
  }
  for (size_t i = 0; i < asset->area_count; i++) {
    qa_aas_linked_reachability *link = heads[i];
    while (link) { qa_aas_linked_reachability *next = link->next; free(link); link = next; }
  }
  while (context.free_list) { qa_aas_linked_reachability *next = context.free_list->next; free(context.free_list); context.free_list = next; }
  free(heads);
  qa_aas_reachability_debug_state_destroy(&context.debug_state);
  qa_aas_reachability_entities_destroy(&bsp_entities);
  *out = *asset;
  out->settings = settings;
  out->reachability = reachability;
  out->reachability_count = total;
  return true;
}
