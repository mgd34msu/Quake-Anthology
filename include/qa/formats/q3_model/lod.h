/*
 * Port of src/formats/q3-model/lod.ts. C accommodations: the async
 * reader becomes a synchronous fn ptr + ctx returning a malloc'd
 * buffer (NULL = missing, freed by the loader). A BinaryError from a
 * LOD is caught as in TS (invalid slot, lower slots alias it); any
 * other raise is rethrown.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q3_MODEL_LOD_H
#define QA_FORMATS_Q3_MODEL_LOD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/formats/q3_model/md3.h"

typedef enum qa_md3_lod_slot_kind {
  QA_MD3_LOD_MISSING,
  QA_MD3_LOD_LOADED,
  QA_MD3_LOD_INVALID,
  QA_MD3_LOD_ALIAS
} qa_md3_lod_slot_kind;

typedef struct qa_md3_lod_slot {
  qa_md3_lod_slot_kind kind;
  char *path;                      /* owned */
  qa_decoded_md3_model *model;     /* loaded; owned */
  char *error;                     /* invalid; owned */
  double source_slot;              /* alias */
} qa_md3_lod_slot;

typedef struct qa_md3_lod_model {
  char *path; /* owned */
  qa_md3_lod_slot slots[3];
  double *load_order;
  size_t load_order_count;
  double num_lods;
  double byte_length;
} qa_md3_lod_model;

typedef struct qa_md3_model_reader {
  void *ctx;
  uint8_t *(*read)(void *ctx, const char *path, size_t *out_length);
} qa_md3_model_reader;

/* Malloc'd paths [path, stem_1.md3, stem_2.md3]; caller frees each. */
void qa_md3_lod_paths(const char *path, char *out_paths[3]);
/* False is the TS null. */
bool qa_load_md3_lods(const char *path, const qa_md3_model_reader *reader, qa_md3_lod_model *out);
void qa_md3_lod_model_destroy(qa_md3_lod_model *model);
const qa_md3_model *qa_md3_at_lod(const qa_md3_lod_model *model, double lod);

#endif
