/* Port of src/network/q1/qw-constants.ts classes. GPL-2.0-or-later. */
#include "qa/network/q1/qw_constants.h"
#include <string.h>
void qa_qw_entity_state_init(qa_qw_entity_state *s) { qa_qw_entity_state_clear(s); }
void qa_qw_entity_state_clear(qa_qw_entity_state *s) {
  memset(s, 0, sizeof *s);
  s->alpha = 0; /* ENTALPHA_DEFAULT */
  s->scale = 16; /* ENTSCALE_DEFAULT */
}
void qa_qw_entity_state_copy_from(qa_qw_entity_state *s, const qa_qw_entity_state *from) { *s = *from; }
void qa_qw_packet_entities_init(qa_qw_packet_entities *p) {
  p->num_entities = 0;
  for (int i = 0; i < 64; i++) qa_qw_entity_state_init(&p->entities[i]);
}
void qa_qw_usercmd_init(qa_qw_usercmd *c) { memset(c, 0, sizeof *c); }
