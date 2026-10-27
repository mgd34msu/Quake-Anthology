/*
 * Port of src/formats/images/indexed.ts. C accommodations: byte inputs
 * are pointer+length; `source` NULL selects the TS default. MipLevels
 * are qa_image_level[4] whose pixels are palette indices (1 byte per
 * pixel, as in the TS). Owned buffers are malloc'd; destroy frees them.
 * BinaryError -> qa_binary_error, RangeError -> qa_range_error.
 * Functions returning int return 0 only on allocation failure.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_INDEXED_H
#define QA_FORMATS_IMAGES_INDEXED_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/render.h"

typedef struct qa_indexed_image {
  int64_t width, height;
  uint8_t *indices; size_t indices_length;
} qa_indexed_image;
void qa_indexed_image_destroy(qa_indexed_image *image);

/* palette NULL (length 0) when the PCX has no 256-color trailer. */
typedef struct qa_pcx_image {
  int64_t width, height;
  uint8_t *indices; size_t indices_length;
  uint8_t *palette; size_t palette_length;
} qa_pcx_image;
void qa_pcx_image_destroy(qa_pcx_image *image);

/* TS Q1MipTexture union on kind: "embedded" (levels) | "external".
 * Named qa_q1_miptex: qa_q1_mip_texture is the distinct contracts/scene
 * type (byte-view levels plus quake64Shift). */
typedef enum qa_q1_miptex_kind {
  QA_Q1_MIP_EMBEDDED, QA_Q1_MIP_EXTERNAL
} qa_q1_miptex_kind;
typedef struct qa_q1_miptex {
  qa_q1_miptex_kind kind;
  char *name; /* owned */
  int64_t width, height;
  qa_image_level levels[4]; /* embedded only */
} qa_q1_miptex;
void qa_q1_miptex_destroy(qa_q1_miptex *texture);

typedef struct qa_wal_image {
  char *name; /* owned */
  int64_t width, height;
  qa_image_level levels[4];
  char *animation; /* owned */
  int64_t flags, contents, value;
} qa_wal_image;
void qa_wal_image_destroy(qa_wal_image *image);

int qa_decode_q1_mip_texture(const uint8_t *bytes, size_t length,
                             const char *source, qa_q1_miptex *out);
int qa_decode_wal(const uint8_t *bytes, size_t length, const char *source,
                  qa_wal_image *out);
int qa_decode_qpic(const uint8_t *bytes, size_t length, const char *source,
                   qa_indexed_image *out);
int qa_decode_pcx(const uint8_t *bytes, size_t length, const char *source,
                  qa_pcx_image *out);
/* Malloc'd PCX bytes; caller frees. NULL on OOM, *out_length set. */
uint8_t *qa_encode_pcx(const qa_indexed_image *image, const uint8_t *palette,
                       size_t palette_length, size_t *out_length);

/* TS kind is always "qlit-rgb8". has_expected mirrors expectedSamples. */
typedef struct qa_lit {
  uint8_t *samples; size_t samples_length;
} qa_lit;
void qa_lit_destroy(qa_lit *lit);
int qa_decode_lit(const uint8_t *bytes, size_t length, int has_expected,
                  int64_t expected_samples, const char *source, qa_lit *out);

#endif
