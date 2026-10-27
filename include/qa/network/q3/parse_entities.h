/* Port of src/network/q3/parse-entities.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_PARSE_ENTITIES_H
#define QA_NETWORK_Q3_PARSE_ENTITIES_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/network/q3/state/entity.h"
#define QA_Q3_MAX_PARSE_ENTITIES 2048
typedef struct qa_q3_source_parse_entities qa_q3_source_parse_entities;
bool qa_q3_source_parse_entities_init(qa_q3_source_parse_entities **out);
void qa_q3_source_parse_entities_destroy(qa_q3_source_parse_entities *p);
int32_t qa_q3_source_parse_entities_number(const qa_q3_source_parse_entities *p);
void qa_q3_source_parse_entities_set_number(qa_q3_source_parse_entities *p, int32_t number);
qa_q3_source_entity_state *qa_q3_source_parse_entities_at(qa_q3_source_parse_entities *p, int64_t absolute_index);
void qa_q3_source_parse_entities_advance(qa_q3_source_parse_entities *p);
void qa_q3_source_parse_entities_clear(qa_q3_source_parse_entities *p);
#endif
