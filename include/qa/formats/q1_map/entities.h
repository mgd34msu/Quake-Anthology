/*
 * Port of src/formats/q1-map/entities.ts. Plain TS Error aborts printing
 * "Error: <message>", as in src/contracts. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q1_MAP_ENTITIES_H
#define QA_FORMATS_Q1_MAP_ENTITIES_H

#include <stddef.h>

#include "qa/formats/q1_map/types.h"

/* Owned result; free with qa_q1_entities_destroy. */
qa_q1_entity *qa_q1_parse_entities(const char *text, size_t *out_count);
void qa_q1_entities_destroy(qa_q1_entity *entities, size_t count);
/* Last duplicate wins; NULL when absent (TS null). */
const char *qa_q1_entity_value(const qa_q1_entity *entity, const char *key);

#endif
