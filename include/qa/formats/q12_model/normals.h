/* Port of src/formats/q12-model/normals.ts. GPL-2.0-or-later. */
#ifndef QA_FORMATS_Q12_MODEL_NORMALS_H
#define QA_FORMATS_Q12_MODEL_NORMALS_H

#include <stddef.h>

#include "qa/contracts/math.h"

/* TS ALIAS_NORMALS (162 entries); borrowed static storage. */
const qa_vec3 *qa_alias_normal_table(size_t *out_count);
qa_vec3 qa_decode_alias_normal(double index);

#endif
