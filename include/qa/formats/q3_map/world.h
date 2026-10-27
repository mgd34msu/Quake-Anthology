/*
 * Port of src/formats/q3-map/world.ts. The adapted world borrows the
 * source map's shared arrays and owns its converted planes/nodes/
 * leaves/models/brushes/surfaces; qa_q3_adapted_world_destroy frees
 * only the converted arrays. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q3_MAP_WORLD_H
#define QA_FORMATS_Q3_MAP_WORLD_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/scene.h"
#include "qa/formats/q3_map/decode.h"

void qa_q3_adapt_bsp(const qa_q3_bsp_map *map, qa_q3_world_geometry *out);
void qa_q3_adapted_world_destroy(qa_q3_world_geometry *world);

typedef struct qa_q3_decoded_world {
  qa_q3_bsp_map map; /* owned; world borrows from it */
  qa_q3_world_geometry world;
} qa_q3_decoded_world;

/* `source` NULL means "<bsp>". */
void qa_q3_decode_world(const uint8_t *data, size_t data_length, const char *source,
                        qa_q3_decoded_world *out);
void qa_q3_decoded_world_destroy(qa_q3_decoded_world *world);

#endif
