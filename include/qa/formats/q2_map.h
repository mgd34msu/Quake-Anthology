/*
 * Umbrella for src/formats/q2-map/index.ts. C accommodations: the TS
 * try/catch around BSPX sub-parsers cannot be reproduced (BinaryError
 * aborts), so a malformed DECOUPLED_LM / LIGHTGRID_OCTREE / FACENORMALS
 * lump aborts instead of becoming a diagnostic; Q2MapResources becomes
 * a fn ptr + ctx returning a malloc'd buffer (NULL = missing), freed by
 * the decoder. World arrays the TS shares with the source borrow the
 * owned qa_q2_bsp storage. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q2_MAP_H
#define QA_FORMATS_Q2_MAP_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/scene.h"
#include "qa/formats/q2_map/bspx.h"
#include "qa/formats/q2_map/reader.h"

typedef struct qa_q2_map_resources {
  void *ctx;
  uint8_t *(*read_material)(void *ctx, const char *path, size_t *out_length);
} qa_q2_map_resources;

typedef struct qa_q2_decoded_map {
  qa_q2_world_geometry world;
  qa_q2_bsp *source;                 /* owned */
  qa_q2_face_normals *face_normals;  /* owned, NULL is TS null */
  qa_q2_string_list diagnostics;
  /* Owned storage backing the world views that are not borrowed. */
  qa_q2_leaf *owned_leaves;
  qa_bsp_face *owned_faces;
  qa_q2_texture_info *owned_texture_info;
  qa_q2_world_model *owned_models;
  qa_q2_lightgrid *owned_lightgrid;
  qa_decoupled_lightmap *owned_decoupled;
  size_t owned_decoupled_count;
  qa_bsp_extension *owned_extensions;
  uint8_t *owned_lighting;
} qa_q2_decoded_map;

/* Takes ownership of `map` contents (moved into the result source). */
void qa_to_q2_world_geometry(qa_q2_bsp *map, const qa_q2_map_resources *resources,
                             qa_q2_decoded_map *out);
/* `source` NULL means "<q2-map>"; resources may be NULL. */
void qa_decode_q2_map(const uint8_t *bytes, size_t length, const char *source,
                      const qa_q2_map_resources *resources, qa_q2_decoded_map *out);
void qa_q2_decoded_map_destroy(qa_q2_decoded_map *map);

#endif
