/*
 * Port of src/formats/q3-model/md3.ts. Md3Vertex is the contract
 * qa_model_vertex (same shape). The skin generator becomes an
 * iterator struct. interpolateSurface may borrow the current frame
 * (out_owned false) exactly as the TS returns it. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q3_MODEL_MD3_H
#define QA_FORMATS_Q3_MODEL_MD3_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

typedef struct qa_md3_frame {
  qa_bounds bounds;
  qa_vec3 origin;
  double radius;
  char *name; /* owned */
} qa_md3_frame;

typedef struct qa_md3_tag {
  char *name; /* owned */
  qa_vec3 origin;
  qa_axis axes;
} qa_md3_tag;

typedef struct qa_md3_shader {
  char *name; /* owned */
  double index;
} qa_md3_shader;

typedef struct qa_md3_triangle {
  double indices[3];
} qa_md3_triangle;

typedef struct qa_md3_surface {
  char *name; /* owned */
  double flags;
  qa_md3_shader *shaders;
  size_t shader_count;
  qa_md3_triangle *triangles;
  size_t triangle_count;
  qa_vec2 *tex_coords;
  size_t tex_coord_count;
  qa_model_vertex **frame_vertices; /* owned rows */
  size_t *frame_vertex_counts;
  size_t frame_count;
} qa_md3_surface;

typedef struct qa_md3_model {
  char *name; /* owned */
  double flags;
  double skin_count;
  qa_md3_frame *frames;
  size_t frame_count;
  qa_md3_tag **tag_rows; /* owned rows */
  size_t *tag_row_counts;
  size_t tag_row_count;
  qa_md3_surface *surfaces;
  size_t surface_count;
} qa_md3_model;

typedef struct qa_decoded_md3_model {
  qa_md3_model model;
  char *source;    /* owned */
  uint8_t *bytes;  /* owned copy up to model end */
  size_t byte_length;
} qa_decoded_md3_model;

/* `source` NULL means "<md3>". */
void qa_parse_md3(const uint8_t *data, size_t data_length, const char *source, qa_decoded_md3_model *out);
void qa_md3_model_destroy(qa_md3_model *model);
void qa_decoded_md3_model_destroy(qa_decoded_md3_model *model);

qa_vec3 qa_decode_md3_normal(double packed);
/* Returned vertices are borrowed (*out_owned false) or malloc'd. */
const qa_model_vertex *qa_md3_interpolate_surface(const qa_md3_surface *surface, double frame,
                                                  double old_frame, double back_lerp, bool *out_owned,
                                                  size_t *out_count);
qa_model_vertex *qa_md3_interpolate_frames(const qa_model_vertex *current, const qa_model_vertex *previous,
                                           size_t count, double back_lerp);
/* False is TS null. out->name is an owned copy of `name`. */
bool qa_md3_lerp_tag(const qa_md3_model *model, const char *name, double start_frame, double end_frame,
                     double fraction, qa_md3_tag *out);
void qa_md3_interpolate_tags(const qa_md3_tag *start, const qa_md3_tag *end, const char *name,
                             double fraction, qa_md3_tag *out);

typedef struct qa_skin_surface {
  char *name;   /* owned */
  char *shader; /* owned */
} qa_skin_surface;

typedef struct qa_md3_skin_iterator {
  const char *text; /* borrowed, truncated at first NUL */
  size_t length;
  size_t offset;
} qa_md3_skin_iterator;

void qa_md3_skin_iterator_init(qa_md3_skin_iterator *iterator, const char *text);
/* False ends iteration; out strings owned on true. */
bool qa_md3_skin_iterator_next(qa_md3_skin_iterator *iterator, qa_skin_surface *out);
qa_skin_surface *qa_parse_skin(const char *text, size_t *out_count);
void qa_skin_surfaces_destroy(qa_skin_surface *surfaces, size_t count);

#endif
