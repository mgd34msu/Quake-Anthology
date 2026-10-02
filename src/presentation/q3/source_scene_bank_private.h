#ifndef QA_Q3_SOURCE_SCENE_BANK_PRIVATE_H
#define QA_Q3_SOURCE_SCENE_BANK_PRIVATE_H
#include "qa/q3_source_scene_bank.h"
#include "qa/q3_assets_custody.h"
#include <stdlib.h>
#include <string.h>

struct qa_q3_source_scene_bank {
    uint64_t cycle;
    qa_q3_source_scene_membership membership;
    qa_q3_source_entity_cell entities[QA_Q3_SOURCE_ENTITY_CAPACITY];
    qa_q3_source_light_cell lights[QA_Q3_SOURCE_LIGHT_CAPACITY];
    qa_q3_source_polygon_cell *polygons;
    qa_q3_poly_vertex *vertices;
};
bool q3_source_bank_valid(const qa_q3_source_scene_bank *, qa_error *);
bool q3_source_bank_valid_keys(const qa_q3_source_scene_bank *, const uint64_t *, qa_error *);
#endif
