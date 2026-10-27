/* Port of src/formats/q12-model/mdl.ts. Owned result. GPL-2.0-or-later. */
#ifndef QA_FORMATS_Q12_MODEL_MDL_H
#define QA_FORMATS_Q12_MODEL_MDL_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/scene.h"

/* `source` NULL means "<mdl>". */
void qa_parse_mdl(const uint8_t *data, size_t data_length, const char *source, qa_q1_alias_model *out);
void qa_q1_alias_model_destroy(qa_q1_alias_model *model);

#endif
