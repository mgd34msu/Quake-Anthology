/*
 * Port of src/formats/q2-map/bspx.ts. The TS ReadonlyMap becomes an
 * owned lump array searched by qa_q2_bspx_find. Lightgrid records use
 * the contracts/scene.h shapes. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q2_MAP_BSPX_H
#define QA_FORMATS_Q2_MAP_BSPX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/core/binary/index.h"

typedef struct qa_q2_bspx_lump {
  char *name;    /* owned */
  double offset;
  uint8_t *bytes; /* owned */
  size_t length;
} qa_q2_bspx_lump;

typedef struct qa_q2_string_list {
  char **values; /* owned strings */
  size_t count;
} qa_q2_string_list;

typedef struct qa_q2_bspx_directory {
  double offset;
  qa_q2_bspx_lump *lumps;
  size_t lump_count;
  qa_q2_string_list diagnostics;
} qa_q2_bspx_directory;

/* False is the TS null directory. */
bool qa_read_q2_bspx(qa_binary_reader *reader, double end_of_lumps, qa_q2_bspx_directory *out);
const qa_q2_bspx_lump *qa_q2_bspx_find(const qa_q2_bspx_directory *directory, const char *name);
void qa_q2_bspx_directory_destroy(qa_q2_bspx_directory *directory);
void qa_q2_string_list_destroy(qa_q2_string_list *list);

typedef struct qa_q2_face_normals {
  qa_vec3 *normals; /* owned */
  size_t normal_count;
  double (*corner_indices)[3]; /* owned */
  size_t corner_count;
} qa_q2_face_normals;

typedef struct qa_q2_decoupled_lightmaps {
  qa_decoupled_lightmap *faces; /* owned */
  size_t face_count;
  qa_q2_string_list diagnostics;
} qa_q2_decoupled_lightmaps;

void qa_read_q2_face_normals(const qa_q2_bspx_lump *lump, double corner_count, qa_q2_face_normals *out);
void qa_q2_face_normals_destroy(qa_q2_face_normals *normals);
void qa_read_q2_decoupled_lightmaps(const qa_q2_bspx_lump *lump, double face_count, double lighting_bytes,
                                    qa_q2_decoupled_lightmaps *out);
void qa_q2_decoupled_lightmaps_destroy(qa_q2_decoupled_lightmaps *lightmaps);

void qa_read_q2_lightgrid(const qa_q2_bspx_lump *lump, qa_q2_lightgrid *out);
void qa_q2_lightgrid_destroy(qa_q2_lightgrid *grid);
/* False is TS null; *out borrows grid storage. */
bool qa_lookup_q2_lightgrid(const qa_q2_lightgrid *grid, qa_vec3 point,
                            const qa_q2_lightgrid_sample **out, size_t *out_count);

#endif
