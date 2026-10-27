/*
 * Port of src/formats/q1-map/queries.ts. Returned byte/vertex arrays
 * are malloc'd copies the caller frees. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q1_MAP_QUERIES_H
#define QA_FORMATS_Q1_MAP_QUERIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

uint8_t *qa_q1_decompress_pvs(const uint8_t *data, size_t data_length, bool has_offset,
                              double offset, double visible_leaves, size_t *out_length);
uint8_t *qa_q1_leaf_pvs(const qa_q1_world_geometry *map, double leaf_index, size_t *out_length);
double qa_q1_find_leaf(const qa_q1_world_geometry *map, qa_vec3 point, double model_index);
qa_vec3 *qa_q1_face_vertices(const qa_q1_world_geometry *map, double face_index, size_t *out_count);
/* *out_found false is the TS null. */
double qa_q1_face_at_contact(const qa_q1_world_geometry *map, const double *candidates,
                             size_t candidate_count, qa_vec3 point, qa_plane plane, bool *out_found);

#endif
