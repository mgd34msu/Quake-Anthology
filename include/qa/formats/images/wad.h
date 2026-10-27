/*
 * Port of src/formats/images/wad.ts. C accommodations: byte inputs are
 * pointer+length; `source` NULL means "<wad>"; lump/archive/image
 * buffers are owned (the "raw" image borrows its lump, as the TS keeps
 * the lump object). BinaryError -> qa_binary_error. Functions
 * returning int return 0 only on allocation failure.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_WAD_H
#define QA_FORMATS_IMAGES_WAD_H

#include <stddef.h>
#include <stdint.h>

#include "qa/formats/images/indexed.h"

typedef struct qa_wad_lump {
  char *name; /* owned, lowercased like the TS */
  int64_t type, compression, offset, disk_size, byte_length;
  uint8_t *bytes; size_t bytes_length;
} qa_wad_lump;

typedef struct qa_wad_archive {
  const char *kind; /* "WAD2" | "WAD3" (static) */
  qa_wad_lump *lumps; size_t lump_count;
} qa_wad_archive;
void qa_wad_archive_destroy(qa_wad_archive *archive);

/* TS WadImage union on kind. palette is NULL when absent (miptex). */
typedef enum qa_wad_image_kind {
  QA_WAD_IMAGE_QPIC, QA_WAD_IMAGE_MIPTEX, QA_WAD_IMAGE_PALETTE, QA_WAD_IMAGE_RAW
} qa_wad_image_kind;
typedef struct qa_wad_image {
  qa_wad_image_kind kind;
  qa_indexed_image image;   /* qpic */
  qa_q1_miptex texture;     /* miptex */
  uint8_t *palette; size_t palette_length; /* miptex, owned or NULL */
  uint8_t *colors; size_t colors_length;   /* palette, owned */
  const qa_wad_lump *lump;                 /* raw, borrowed */
} qa_wad_image;
void qa_wad_image_destroy(qa_wad_image *image);

int qa_decode_wad(const uint8_t *bytes, size_t length, const char *source,
                  qa_wad_archive *out);
int qa_decode_wad_image(const qa_wad_archive *archive, const qa_wad_lump *lump,
                        const char *source, qa_wad_image *out);

#endif
