/* Port of src/formats/q3-model/lod.ts (synchronous reader; see the
 * header for the BinaryError-catch accommodation). GPL-2.0-or-later. */
#include "qa/formats/q3_model/lod.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static char *xstrdup(const char *text) {
  size_t length = strlen(text);
  char *copy = xmalloc(length + 1);
  memcpy(copy, text, length + 1);
  return copy;
}

void qa_md3_lod_paths(const char *path, char *out_paths[3]) {
  const char *dot = strrchr(path, '.');
  size_t stem_length = dot != NULL ? (size_t)(dot - path) : strlen(path);
  out_paths[0] = xstrdup(path);
  for (int i = 1; i <= 2; i++) {
    char *result = xmalloc(stem_length + 8);
    memcpy(result, path, stem_length);
    snprintf(result + stem_length, 8, "_%d.md3", i);
    out_paths[i] = result;
  }
}

bool qa_load_md3_lods(const char *path, const qa_md3_model_reader *reader, qa_md3_lod_model *out) {
  memset(out, 0, sizeof *out);
  char *paths[3];
  qa_md3_lod_paths(path, paths);
  out->path = xstrdup(path);
  for (int i = 0; i < 3; i++) {
    out->slots[i].kind = QA_MD3_LOD_MISSING;
    out->slots[i].path = xstrdup(paths[i]);
  }
  double num_lods = 0;
  double byte_length = 0;
  bool aborted = false;
  for (int lod = 2; lod >= 0; lod--) {
    size_t length = 0;
    uint8_t *bytes = reader->read(reader->ctx, paths[lod], &length);
    if (bytes == NULL) continue;
    out->load_order = realloc(out->load_order, (out->load_order_count + 1) * sizeof *out->load_order);
    if (out->load_order == NULL) qa_range_error("Out of memory");
    out->load_order[out->load_order_count++] = lod;
    qa_binary_reader probe;
    if (!qa_binary_reader_init(&probe, bytes, length, paths[lod])) qa_range_error("Out of memory");
    uint32_t ident = qa_binary_reader_u32(&probe);
    qa_binary_reader_destroy(&probe);
    if (ident != 0x33504449u) {
      free(bytes);
      aborted = true;
      break;
    }
    qa_decoded_md3_model *model = xmalloc(sizeof *model);
    qa_parse_md3(bytes, length, paths[lod], model);
    free(bytes);
    out->slots[lod].kind = QA_MD3_LOD_LOADED;
    out->slots[lod].model = model;
    byte_length += (double)model->byte_length;
    num_lods++;
  }
  for (int i = 0; i < 3; i++) free(paths[i]);
  if (aborted || num_lods == 0) {
    qa_md3_lod_model_destroy(out);
    return false;
  }
  out->num_lods = num_lods;
  out->byte_length = byte_length;
  return true;
}

void qa_md3_lod_model_destroy(qa_md3_lod_model *model) {
  if (model == NULL) return;
  free(model->path);
  for (int i = 0; i < 3; i++) {
    free(model->slots[i].path);
    if (model->slots[i].model != NULL) {
      qa_decoded_md3_model_destroy(model->slots[i].model);
      free(model->slots[i].model);
    }
    free(model->slots[i].error);
  }
  free(model->load_order);
  memset(model, 0, sizeof *model);
}

const qa_md3_model *qa_md3_at_lod(const qa_md3_lod_model *model, double lod) {
  if (lod != trunc(lod) || lod < 0 || lod > 2) qa_range_error("MD3 LOD must be 0..2");
  const qa_md3_lod_slot *slot = &model->slots[(size_t)lod];
  switch (slot->kind) {
    case QA_MD3_LOD_LOADED: return &slot->model->model;
    case QA_MD3_LOD_ALIAS: return qa_md3_at_lod(model, slot->source_slot);
    case QA_MD3_LOD_MISSING:
    case QA_MD3_LOD_INVALID: return NULL;
  }
  return NULL;
}
