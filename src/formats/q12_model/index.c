/* Port of src/formats/q12-model/index.ts. GPL-2.0-or-later. */
#include "qa/formats/q12_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* JSON.stringify of the magic for the error text. */
static void json_quote(const char *text, char *out, size_t capacity) {
  size_t w = 0;
  out[w++] = '"';
  for (const unsigned char *p = (const unsigned char *)text; *p != 0 && w + 8 < capacity; p++) {
    const char *escape = *p == '"' ? "\\\"" : *p == '\\' ? "\\\\" : *p == '\b' ? "\\b" : *p == '\f' ? "\\f"
                         : *p == '\n' ? "\\n" : *p == '\r' ? "\\r" : *p == '\t' ? "\\t" : NULL;
    if (escape != NULL) {
      w += (size_t)snprintf(out + w, capacity - w, "%s", escape);
    } else if (*p < 0x20) {
      w += (size_t)snprintf(out + w, capacity - w, "\\u%04x", *p);
    } else {
      out[w++] = (char)*p;
    }
  }
  out[w++] = '"';
  out[w] = '\0';
}

void qa_parse_q12_model(const uint8_t *data, size_t data_length, const char *source, qa_q12_model *out) {
  const char *name = source != NULL ? source : "<model>";
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)data, data_length, name)) qa_range_error("Out of memory");
  char *magic = qa_binary_reader_fixed_byte_string(&reader, 4);
  qa_binary_reader_destroy(&reader);
  memset(out, 0, sizeof *out);
  const char *text = magic != NULL ? magic : "";
  int kind = strcmp(text, "IDPO") == 0 ? 0 : strcmp(text, "IDP2") == 0 ? 1 : strcmp(text, "IDSP") == 0 ? 2
             : strcmp(text, "IDS2") == 0 ? 3 : -1;
  if (kind < 0) {
    char quoted[64];
    json_quote(text, quoted, sizeof quoted);
    free(magic);
    qa_binary_error(name, 0, "Unknown Q1/Q2 model magic %s", quoted);
  }
  free(magic);
  if (kind == 0) {
    out->kind = QA_Q12_MODEL_MDL;
    qa_parse_mdl(data, data_length, name, &out->as.mdl);
  } else if (kind == 1) {
    out->kind = QA_Q12_MODEL_MD2;
    qa_parse_md2(data, data_length, name, &out->as.md2);
  } else if (kind == 2) {
    out->kind = QA_Q12_MODEL_SPR;
    qa_parse_spr(data, data_length, name, &out->as.spr);
  } else {
    out->kind = QA_Q12_MODEL_SP2;
    qa_parse_sp2(data, data_length, name, &out->as.sp2);
  }
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
