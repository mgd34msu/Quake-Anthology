/* Port of src/formats/q12-model/sprite.ts. Owned results. GPL-2.0-or-later. */
#ifndef QA_FORMATS_Q12_MODEL_SPRITE_H
#define QA_FORMATS_Q12_MODEL_SPRITE_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/scene.h"

/* `source` NULL means "<spr>" / "<sp2>". */
void qa_parse_spr(const uint8_t *data, size_t data_length, const char *source, qa_q1_sprite_model *out);
void qa_q1_sprite_model_destroy(qa_q1_sprite_model *model);
void qa_parse_sp2(const uint8_t *data, size_t data_length, const char *source, qa_q2_sprite_model *out);
void qa_q2_sprite_model_destroy(qa_q2_sprite_model *model);

#endif
