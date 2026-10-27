/* Port of src/formats/q3-model/md4.ts; records use contracts/scene.h
 * Md4 shapes. Owned result. GPL-2.0-or-later. */
#ifndef QA_FORMATS_Q3_MODEL_MD4_H
#define QA_FORMATS_Q3_MODEL_MD4_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/scene.h"

typedef struct qa_decoded_md4_model {
  qa_md4_model model;
  char *source;    /* owned */
  uint8_t *bytes;  /* owned copy up to ofsEnd */
  size_t bytes_length;
  char **bone_names; /* owned, NULL is TS null */
  size_t bone_name_count;
} qa_decoded_md4_model;

/* `source` NULL means "<md4>". */
void qa_parse_md4(const uint8_t *bytes, size_t length, const char *source, qa_decoded_md4_model *out);
void qa_decoded_md4_model_destroy(qa_decoded_md4_model *model);
/* Malloc'd skinned vertices; caller frees. */
qa_model_vertex *qa_skin_md4_surface(const qa_md4_model *model, const qa_md4_surface *surface, double frame,
                                     double previous_frame, double back_lerp, size_t *out_count);

#endif
