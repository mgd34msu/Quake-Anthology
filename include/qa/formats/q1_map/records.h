/*
 * Port of src/formats/q1-map/records.ts. Record readers return owned
 * arrays (caller frees, including per-face styles/headnode storage).
 * The TS generic records() becomes per-type readers. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q1_MAP_RECORDS_H
#define QA_FORMATS_Q1_MAP_RECORDS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/core/binary/index.h"
#include "qa/formats/q1_map/types.h"

qa_vec3 qa_q1_read_vec3(qa_binary_reader *reader);
qa_vec4 qa_q1_read_vec4(qa_binary_reader *reader);
qa_bounds qa_q1_read_bounds(qa_binary_reader *reader, bool floats);

void qa_q1_check_index(double value, double count, const char *source);
void qa_q1_check_range(qa_index_range value, double count, const char *source);
/* Returns false for the TS null (-1 / 0xffffffff); *out set otherwise. */
bool qa_q1_optional_offset(double value, double *out);

qa_bsp_plane *qa_q1_read_planes(qa_binary_reader *reader, size_t *out_count);
qa_bsp_node *qa_q1_read_nodes(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count);
qa_q1_clip_node *qa_q1_read_clipnodes(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count);
qa_q1_leaf *qa_q1_read_leaves(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count);
qa_bsp_edge *qa_q1_read_edges(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count);
qa_bsp_face *qa_q1_read_faces(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count);
qa_q1_texture_info *qa_q1_read_texture_info(qa_binary_reader *reader, size_t *out_count);
qa_q1_world_model *qa_q1_read_models(qa_binary_reader *reader, size_t *out_count);
qa_vec3 *qa_q1_read_vertices(qa_binary_reader *reader, size_t *out_count);
double *qa_q1_read_surface_edges(qa_binary_reader *reader, size_t *out_count);
double *qa_q1_read_leaf_faces(qa_binary_reader *reader, qa_q1_bsp_format format, size_t *out_count);

#endif
