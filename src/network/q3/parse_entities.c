/* Port of src/network/q3/parse-entities.ts. GPL-2.0-or-later. */
#include "qa/network/q3/parse_entities.h"
#include <stdlib.h>
struct qa_q3_source_parse_entities {
  qa_q3_entity_state_record cells[QA_Q3_MAX_PARSE_ENTITIES];
  int32_t number;
};
bool qa_q3_source_parse_entities_init(qa_q3_source_parse_entities **out) {
  qa_q3_source_parse_entities *p = calloc(1, sizeof *p);
  if (!p) return false;
  for (int i = 0; i < QA_Q3_MAX_PARSE_ENTITIES; i++) qa_q3_entity_state_record_init(&p->cells[i], 0);
  *out = p; return true;
}
void qa_q3_source_parse_entities_destroy(qa_q3_source_parse_entities *p) { free(p); }
int32_t qa_q3_source_parse_entities_number(const qa_q3_source_parse_entities *p) { return p->number; }
void qa_q3_source_parse_entities_set_number(qa_q3_source_parse_entities *p, int32_t number) { p->number = number; }
qa_q3_source_entity_state *qa_q3_source_parse_entities_at(qa_q3_source_parse_entities *p, int64_t absolute_index) {
  return &p->cells[((int32_t)absolute_index) & (QA_Q3_MAX_PARSE_ENTITIES - 1)];
}
void qa_q3_source_parse_entities_advance(qa_q3_source_parse_entities *p) { p->number = (int32_t)((uint32_t)p->number + 1u); }
void qa_q3_source_parse_entities_clear(qa_q3_source_parse_entities *p) {
  for (int i = 0; i < QA_Q3_MAX_PARSE_ENTITIES; i++) qa_q3_entity_state_record_init(&p->cells[i], 0);
  p->number = 0;
}
