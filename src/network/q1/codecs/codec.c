/* Port of src/network/q1/codecs/codec.ts data classes. GPL-2.0-or-later. */
#include "qa/network/q1/codecs/codec.h"
#include <string.h>
void qa_q1_entity_update_init(qa_q1_entity_update *u) {
  memset(u, 0, sizeof *u);
  u->alpha = QA_Q1_ENTALPHA_DEFAULT; u->scale = QA_Q1_ENTSCALE_DEFAULT;
  qa_q1_wire_entity_state_init(&u->baseline);
}
void qa_q1_clientdata_codec_init(qa_q1_clientdata_codec *c) {
  memset(c, 0, sizeof *c); c->standard_quake = true; c->alpha = QA_Q1_ENTALPHA_DEFAULT;
}
void qa_q1_sound_message_init(qa_q1_sound_message *s) { memset(s, 0, sizeof *s); }
void qa_q1_entity_update_tail_init(qa_q1_entity_update_tail *t) { qa_q1_entity_update_tail_clear(t); }
void qa_q1_entity_update_tail_clear(qa_q1_entity_update_tail *t) {
  memset(t, 0, sizeof *t); t->alpha = QA_Q1_ENTALPHA_DEFAULT; t->scale = QA_Q1_ENTSCALE_DEFAULT;
}
void qa_q1_clientdata_tail_init(qa_q1_clientdata_tail *t) { qa_q1_clientdata_tail_clear(t); }
void qa_q1_clientdata_tail_clear(qa_q1_clientdata_tail *t) {
  memset(t, 0, sizeof *t); t->weaponalpha = QA_Q1_ENTALPHA_DEFAULT;
}
void qa_q1_sound_header_init(qa_q1_sound_header *h) { memset(h, 0, sizeof *h); }
void qa_qw_entity_word_init(qa_qw_entity_word *w) { qa_qw_entity_word_clear(w); }
void qa_qw_entity_word_clear(qa_qw_entity_word *w) { memset(w, 0, sizeof *w); }
