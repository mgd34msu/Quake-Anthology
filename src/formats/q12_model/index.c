/* Port of src/formats/q12-model/index.ts. GPL-2.0-or-later. */
#include "qa/formats/q12_model.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

void qa_parse_q12_model(const uint8_t *data, size_t data_length, const char *source, qa_q12_model *out) {
  const char *name = source != NULL ? source : "<model>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)data, data_length, name)) qa_range_error("Out of memory");
  char *magic = qa_binary_reader_fixed_byte_string(&reader, 4);
  qa_binary_reader_destroy(&reader);
  memset(out, 0, sizeof *out);
  if (magic != NULL && strcmp(magic, "IDPO") == 0) {
    out->kind = QA_Q12_MODEL_MDL;
    qa_parse_mdl(data, data_length, name, &out->as.mdl);
  } else if (magic != NULL && strcmp(magic, "IDP2") == 0) {
    out->kind = QA_Q12_MODEL_MD2;
    qa_parse_md2(data, data_length, name, &out->as.md2);
  } else if (magic != NULL && strcmp(magic, "IDSP") == 0) {
    out->kind = QA_Q12_MODEL_SPR;
    qa_parse_spr(data, data_length, name, &out->as.spr);
  } else if (magic != NULL && strcmp(magic, "IDS2") == 0) {
    out->kind = QA_Q12_MODEL_SP2;
    qa_parse_sp2(data, data_length, name, &out->as.sp2);
  } else {
    qa_binary_error(name, 0, "Unknown Q1/Q2 model magic \"%s\"", magic != NULL ? magic : "");
  }
  free(magic);
}

void qa_q12_model_destroy(qa_q12_model *model) {
  if (model == NULL) return;
  switch (model->kind) {
    case QA_Q12_MODEL_MDL: qa_q1_alias_model_destroy(&model->as.mdl); break;
    case QA_Q12_MODEL_MD2: qa_q2_alias_model_destroy(&model->as.md2); break;
    case QA_Q12_MODEL_SPR: qa_q1_sprite_model_destroy(&model->as.spr); break;
    case QA_Q12_MODEL_SP2: qa_q2_sprite_model_destroy(&model->as.sp2); break;
  }
  memset(model, 0, sizeof *model);
}

void qa_decode_q12_model_resource(const qa_resolved_resource_reference *source, const uint8_t *data,
                                  size_t data_length, qa_q12_model_resource *out) {
  if (source->byte_length < 0 || (size_t)source->byte_length != data_length) {
    qa_binary_error(source->requested_path, 0, "Resource byte length differs from resolved content");
  }
  out->source = *source;
  qa_parse_q12_model(data, data_length, source->requested_path, &out->model);
}
