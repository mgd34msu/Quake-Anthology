/*
 * Port of src/persistence/q2-rerelease.ts. Save structs borrow the
 * parsed source-json tree (caller frees `root`); entity maps become
 * slot/node pair arrays. Field readers take a borrowed record node;
 * writers mutate it and return false only on OOM.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_RERELEASE_H
#define QA_PERSISTENCE_Q2_RERELEASE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/persistence/source_json.h"

typedef struct qa_q2_rerelease_game_save {
  qa_save_json *root; /* owned */
  qa_save_json *game; /* borrowed */
  qa_save_json **clients; /* borrowed nodes; malloc'd array */
  size_t client_count;
} qa_q2_rerelease_game_save;
typedef struct qa_q2_rerelease_entity {
  double slot;
  qa_save_json *record; /* borrowed */
} qa_q2_rerelease_entity;
typedef struct qa_q2_rerelease_level_save {
  qa_save_json *root; /* owned */
  qa_save_json *level; /* borrowed */
  qa_q2_rerelease_entity *entities; /* malloc'd array */
  size_t entity_count;
} qa_q2_rerelease_level_save;

bool qa_decode_q2_rerelease_game(const char *text, qa_q2_rerelease_game_save *out);
char *qa_encode_q2_rerelease_game(const qa_q2_rerelease_game_save *save);
bool qa_decode_q2_rerelease_level(const char *text, qa_q2_rerelease_level_save *out);
char *qa_encode_q2_rerelease_level(const qa_q2_rerelease_level_save *save);

typedef struct qa_q2_fog_save {
  qa_vec3 color; double density; qa_vec3 color_off; double density_off;
  double sky_factor; double sky_factor_off;
} qa_q2_fog_save;
typedef struct qa_q2_height_fog_save {
  double falloff, density; qa_vec3 start_color; double start_distance;
  qa_vec3 end_color; double end_distance;
  double falloff_off, density_off; qa_vec3 start_color_off;
  double start_distance_off; qa_vec3 end_color_off; double end_distance_off;
} qa_q2_height_fog_save;
typedef struct qa_q2_brush_animation_save {
  double start, end, style, speed; bool no_wrap;
  double alternate_start, alternate_end, alternate_style, alternate_speed;
  bool alternate_no_wrap, enabled, alternate, currently_alternate;
  double next_tick;
} qa_q2_brush_animation_save;
typedef struct qa_q2_poi_save {
  bool valid; qa_vec3 point; double stage, image;
  bool has_dynamic_entity; /* TS number | null */
  double dynamic_entity;
} qa_q2_poi_save;

qa_q2_fog_save qa_read_q2_fog(const qa_save_json *record);
bool qa_write_q2_fog(qa_save_json *record, qa_q2_fog_save fog);
qa_q2_height_fog_save qa_read_q2_height_fog(const qa_save_json *record);
bool qa_write_q2_height_fog(qa_save_json *record, qa_q2_height_fog_save fog);
qa_q2_brush_animation_save qa_read_q2_brush_animation(const qa_save_json *record);
bool qa_write_q2_brush_animation(qa_save_json *record, qa_q2_brush_animation_save animation);
qa_q2_poi_save qa_read_q2_poi(const qa_save_json *level);
bool qa_write_q2_poi(qa_save_json *level, qa_q2_poi_save poi);
/* Malloc'd values (caller frees); *out_count set. NULL with count 0
 * when absent; NULL with count SIZE_MAX on OOM. */
double *qa_read_q2_ammo_capacity(const qa_save_json *persistent, size_t *out_count);
bool qa_write_q2_ammo_capacity(qa_save_json *persistent, const double *values, size_t count);

#endif
