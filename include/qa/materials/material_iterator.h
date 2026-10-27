/*
 * Port of src/materials/material-iterator.ts. Iterator/stage types live
 * in shader_types.h (qa_mat_*). The returned passes array is malloc'd;
 * free it with qa_mat_material_iterator_destroy. GPL-2.0-or-later.
 */
#ifndef QA_MATERIALS_MATERIAL_ITERATOR_H
#define QA_MATERIALS_MATERIAL_ITERATOR_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/materials/shader_types.h"

typedef qa_mat_iterator_profile qa_material_iterator_profile;
typedef qa_mat_finished_iterator_stage qa_finished_iterator_stage;
typedef qa_mat_iterator_pass qa_iterator_pass;
typedef qa_mat_material_iterator qa_material_iterator_result;

/* TS MaterialIteratorInput. */
typedef struct qa_material_iterator_input {
  const qa_mat_finished_iterator_stage *stages; size_t stage_count;
  bool sky, polygon_offset;
  double deform_count;
} qa_material_iterator_input;

qa_mat_material_iterator qa_source_material_iterator(
    qa_material_iterator_input input, qa_mat_iterator_profile profile);
void qa_mat_material_iterator_destroy(qa_mat_material_iterator *iterator);

#endif
