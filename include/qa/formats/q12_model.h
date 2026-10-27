/*
 * Umbrella for src/formats/q12-model/index.ts. Q12Model becomes a kind
 * tag over the four owned model structs (free with the matching
 * destroy). GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q12_MODEL_H
#define QA_FORMATS_Q12_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/scene.h"
#include "qa/formats/q12_model/animation.h"
#include "qa/formats/q12_model/common.h"
#include "qa/formats/q12_model/md2.h"
#include "qa/formats/q12_model/mdl.h"
#include "qa/formats/q12_model/normals.h"
#include "qa/formats/q12_model/sprite.h"

typedef enum qa_q12_model_kind {
  QA_Q12_MODEL_MDL,
  QA_Q12_MODEL_MD2,
  QA_Q12_MODEL_SPR,
  QA_Q12_MODEL_SP2
} qa_q12_model_kind;

typedef struct qa_q12_model {
  qa_q12_model_kind kind;
  union {
    qa_q1_alias_model mdl;
    qa_q2_alias_model md2;
    qa_q1_sprite_model spr;
    qa_q2_sprite_model sp2;
  } as;
} qa_q12_model;

typedef struct qa_q12_model_resource {
  qa_resolved_resource_reference source;
  qa_q12_model model;
} qa_q12_model_resource;

/* `source` NULL means "<model>". */
void qa_parse_q12_model(const uint8_t *data, size_t data_length, const char *source, qa_q12_model *out);
void qa_q12_model_destroy(qa_q12_model *model);
void qa_decode_q12_model_resource(const qa_resolved_resource_reference *source, const uint8_t *data,
                                  size_t data_length, qa_q12_model_resource *out);

#endif
