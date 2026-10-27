/*
 * Port of src/formats/images/q3-pcx.ts (Q3 LoadPCX/LoadPCX32 profile).
 * The TS decodePcx/decodePcxIndexed/expandPcx become
 * qa_decode_q3_pcx/qa_decode_q3_pcx_indexed/qa_expand_q3_pcx
 * (qa_decode_pcx is the format-oriented reader in indexed.h). The TS
 * PcxRejection return becomes a -1 return with *rejection_message set
 * to a malloc'd copy of the message (caller frees); 1 means decoded,
 * 0 means allocation failure. `source` NULL means "<buffer>".
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_Q3_PCX_H
#define QA_FORMATS_IMAGES_Q3_PCX_H

#include <stddef.h>
#include <stdint.h>

#include "qa/formats/images/q3_tga.h"

typedef struct qa_q3_pcx_indexed {
  int64_t width, height;
  uint8_t *indices; size_t indices_length;
  uint8_t *palette; size_t palette_length;
} qa_q3_pcx_indexed;
void qa_q3_pcx_indexed_destroy(qa_q3_pcx_indexed *image);

int qa_decode_q3_pcx_indexed(const uint8_t *bytes, size_t length,
                             const char *source, qa_q3_pcx_indexed *out,
                             char **rejection_message);
int qa_expand_q3_pcx(const qa_q3_pcx_indexed *image, qa_image_data *out);
int qa_decode_q3_pcx(const uint8_t *bytes, size_t length, const char *source,
                     qa_image_data *out, char **rejection_message);

#endif
