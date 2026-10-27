/*
 * Port of src/formats/q1-map/extensions.ts. Functions taking a lump's
 * bytes use (data, length) with has_data for the TS null; result
 * presence returns bool (false is TS null). GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q1_MAP_EXTENSIONS_H
#define QA_FORMATS_Q1_MAP_EXTENSIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/scene.h"
#include "qa/core/binary/index.h"
#include "qa/formats/q1_map/types.h"

qa_q1_bspx_lump *qa_q1_read_bspx(qa_binary_reader *reader, const qa_q1_lump *lumps,
                                 size_t lump_count, size_t *out_count);
/* False when absent. Borrowed view into the lump's owned bytes. */
bool qa_q1_bspx_data(const qa_q1_bspx_lump *lumps, size_t lump_count, const char *name,
                     const uint8_t **out_data, size_t *out_length);
bool qa_q1_read_decoupled_lightmaps(bool has_data, const uint8_t *data, size_t data_length,
                                    double face_count, qa_decoupled_lightmap **out, size_t *out_count);
bool qa_q1_read_brush_list(bool has_data, const uint8_t *data, size_t data_length,
                           double model_count, qa_q1_brush_list_model **out, size_t *out_count);
void qa_q1_read_bspx_metadata(const qa_q1_bspx_lump *lumps, size_t lump_count, double face_count,
                              double vertex_count, double corner_count, qa_q1_bspx_metadata *out);
void qa_q1_bspx_metadata_destroy(qa_q1_bspx_metadata *metadata);
void qa_q1_bspx_destroy(qa_q1_bspx_lump *lumps, size_t count);
void qa_q1_brush_list_destroy(qa_q1_brush_list_model *models, size_t count);

#endif
