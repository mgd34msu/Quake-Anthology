/*
 * Port of src/materials/q3-lighting.ts. Prepared grid samples are
 * malloc'd; free with qa_light_grid_destroy. GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_Q3_LIGHTING_H
#define QA_MATERIALS_Q3_LIGHTING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

enum { QA_RF_MINLIGHT = 1, QA_RF_LIGHTING_ORIGIN = 128 };

typedef struct qa_light_grid {
  qa_vec3 origin, size, inverse_size, bounds;
  qa_q3_light_grid_point *samples; size_t sample_count; /* owned */
} qa_light_grid;
void qa_light_grid_destroy(qa_light_grid *grid);

typedef struct qa_lighting_scales {
  double ambient_scale, directed_scale;
} qa_lighting_scales;

typedef struct qa_lighting_sample {
  qa_vec3 ambient_light, directed_light, light_dir;
} qa_lighting_sample;

typedef struct qa_lighting_entity {
  qa_vec3 origin, lighting_origin;
  qa_axis axis;
  double render_flags;
} qa_lighting_entity;

/* TS DynamicLight (q3). additive false also covers TS undefined. */
typedef struct qa_q3_dynamic_light {
  qa_vec3 origin;
  double radius;
  qa_vec3 color;
  bool additive;
} qa_q3_dynamic_light;

typedef struct qa_entity_lighting_state {
  double ambient_scale, directed_scale;
  const qa_light_grid *grid; /* nullable */
  bool no_world_model;
  double identity_light, identity_light_byte;
  qa_vec3 sun_direction;
  const qa_q3_dynamic_light *dynamic_lights; size_t dynamic_light_count;
} qa_entity_lighting_state;

typedef struct qa_entity_lighting {
  qa_vec3 ambient_light, directed_light, light_dir;
  uint32_t ambient_light_int;
} qa_entity_lighting;

/* TS EntityLightingDiagnostics: print closure as fn+ctx. */
typedef struct qa_entity_lighting_diagnostics {
  bool enabled;
  void *ctx;
  void (*print)(void *ctx, const char *text);
} qa_entity_lighting_diagnostics;

/* Mirrored LightingMap (q3-lighting.ts local interface). Entity
 * records are key/value pair lists (TS ReadonlyMap iteration order). */
typedef struct qa_lighting_map_pair {
  const char *key, *value;
} qa_lighting_map_pair;
typedef struct qa_lighting_map_record {
  const qa_lighting_map_pair *entries; size_t entry_count;
} qa_lighting_map_record;
typedef struct qa_lighting_map {
  const qa_q3_light_grid_point *light_grid; size_t light_grid_count;
  const qa_bounds *model_bounds; size_t model_count;
  const qa_lighting_map_record *entity_records; size_t entity_record_count;
} qa_lighting_map;

typedef struct qa_prepare_light_grid_result {
  bool has_grid;
  qa_light_grid grid;
  char diagnostic[160]; /* empty when no diagnostic */
} qa_prepare_light_grid_result;

qa_prepare_light_grid_result qa_prepare_light_grid(qa_lighting_map map,
    double map_overbright_bits, double overbright_bits);
/* False = TS null (grid NULL). */
bool qa_light_for_point(const qa_light_grid *grid, qa_vec3 point,
                        qa_lighting_scales scales, qa_lighting_sample *out);
/* output NULL uses static default storage matching the TS default
 * fresh object only by value; pass storage to retain it. */
qa_entity_lighting qa_setup_entity_lighting(qa_lighting_entity entity,
    qa_entity_lighting_state state, qa_entity_lighting *output,
    const qa_entity_lighting_diagnostics *diagnostics);

#endif
