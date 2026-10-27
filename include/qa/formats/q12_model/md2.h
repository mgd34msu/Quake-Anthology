/* Port of src/formats/q12-model/md2.ts. Owned result. GPL-2.0-or-later. */
#ifndef QA_FORMATS_Q12_MODEL_MD2_H
#define QA_FORMATS_Q12_MODEL_MD2_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

typedef struct qa_md2_command_vertex {
  qa_vec2 tex_coord;
  double vertex;
} qa_md2_command_vertex;

typedef struct qa_md2_command {
  const char *kind; /* "strip" | "fan" */
  qa_md2_command_vertex *vertices; /* owned */
  size_t vertex_count;
} qa_md2_command;

/* `source` NULL means "<md2>". */
void qa_parse_md2(const uint8_t *data, size_t data_length, const char *source, qa_q2_alias_model *out);
void qa_q2_alias_model_destroy(qa_q2_alias_model *model);
qa_md2_command *qa_decode_md2_commands(const qa_q2_alias_model *model, size_t *out_count);
void qa_md2_commands_destroy(qa_md2_command *commands, size_t count);

#endif
