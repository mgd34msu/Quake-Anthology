/*
 * Port of src/media/roq-codebook.ts (decodeCodeBook from id
 * Software cl_cin.c). The codebooks are the source's global
 * unsigned-short allocations; book2/book4/book8 are byte views of
 * the same storage. Math.fround staging in the YUV conversion is
 * reproduced with double->float casts. GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_ROQ_CODEBOOK_H
#define QA_MEDIA_ROQ_CODEBOOK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/core/binary/index.h"

typedef enum qa_roq_codebook_mode {
  QA_ROQ_CODEBOOK_NORMAL,
  QA_ROQ_CODEBOOK_HALF,
  QA_ROQ_CODEBOOK_SMOOTHED_DOUBLE
} qa_roq_codebook_mode;

/* TS RoqCodebookFormat: samplesPerPixel 1 uses the gray lookup. */
typedef struct qa_roq_codebook_format {
  int64_t samples_per_pixel; /* 1, 2 or 4 */
  const uint8_t *gray;       /* samplesPerPixel 1 only */
  size_t gray_length;
} qa_roq_codebook_format;

typedef enum qa_roq_codebook_profile {
  QA_ROQ_CODEBOOK_SOURCE,
  QA_ROQ_CODEBOOK_DIAGNOSTIC_2X2
} qa_roq_codebook_profile;

int64_t qa_roq_yuv_to_rgb565(int64_t y, int64_t u, int64_t v);
uint32_t qa_roq_yuv_to_rgba(int64_t y, int64_t u, int64_t v);

typedef struct qa_source_roq_codebooks {
  uint16_t *vq2; /* owned, 256*16*4 entries */
  uint16_t *vq4; /* owned, 256*64*4 entries */
  uint16_t *vq8; /* owned, 256*256*4 entries */
  uint8_t *book2; /* byte view of vq2 */
  uint8_t *book4; /* byte view of vq4 */
  uint8_t *book8; /* byte view of vq8 */
} qa_source_roq_codebooks;

/* False only on OOM. */
bool qa_source_roq_codebooks_init(qa_source_roq_codebooks *books);
void qa_source_roq_codebooks_destroy(qa_source_roq_codebooks *books);
void qa_source_roq_codebooks_clear(qa_source_roq_codebooks *books);
void qa_source_roq_codebooks_decode(qa_source_roq_codebooks *books,
                                    qa_binary_reader *reader, int64_t flags,
                                    qa_roq_codebook_mode mode,
                                    qa_roq_codebook_format format,
                                    qa_roq_codebook_profile profile);

#endif
