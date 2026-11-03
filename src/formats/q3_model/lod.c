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
    memcpy(result + stem_length, i == 1 ? "_1.md3" : "_2.md3", 7);
    out_paths[i] = result;
  }
}

typedef struct lod_parse_job {
  const uint8_t *bytes;
  size_t length;
  const char *path;
  bool unknown_ident;
  qa_decoded_md3_model *model;
} lod_parse_job;

static void destroy_probe(void *raw) { qa_binary_reader_destroy(raw); }

static void lod_parse_run(void *raw) {
  lod_parse_job *job = raw;
  qa_binary_reader probe;
  if (!qa_binary_reader_init(&probe, (uint8_t *)job->bytes, job->length, job->path)) qa_range_error("Out of memory");
  qa_cleanup node;
  qa_cleanup_push(&node, destroy_probe, &probe);
  uint32_t ident = qa_binary_reader_u32(&probe);
  qa_cleanup_pop(&node);
  qa_binary_reader_destroy(&probe);
  /* Unknown dispatch IDs abort registration before the nonzero-LOD fallback. */
  if (ident != 0x33504449u) {
    job->unknown_ident = true;
    return;
  }
  qa_parse_md3(job->bytes, job->length, job->path, job->model);
}

/* Missing files keep their independent slots. A failed nonzero LOD aliases
 * only lower slots (TS catches BinaryError per LOD). */
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
  bool failed = false;
  for (int lod = 2; lod >= 0; lod--) {
    size_t length = 0;
    uint8_t *bytes = reader->read(reader->ctx, paths[lod], &length);
    if (bytes == NULL) continue;
    out->load_order = realloc(out->load_order, (out->load_order_count + 1) * sizeof *out->load_order);
    if (out->load_order == NULL) qa_range_error("Out of memory");
    out->load_order[out->load_order_count++] = lod;
    lod_parse_job job = {bytes, length, paths[lod], false, xmalloc(sizeof(qa_decoded_md3_model))};
    memset(job.model, 0, sizeof *job.model);
    qa_caught_error caught;
    bool parsed = qa_try(lod_parse_run, &job, &caught);
    free(bytes);
    if (parsed && job.unknown_ident) {
      free(job.model);
      failed = true;
      break;
    }
    if (!parsed) {
      free(job.model);
      if (strcmp(caught.name, "BinaryError") != 0) {
        for (int i = 0; i < 3; i++) free(paths[i]);
        qa_md3_lod_model_destroy(out);
        qa_rethrow(&caught);
      }
      if (lod == 0) {
        failed = true;
        break;
      }
      out->slots[lod].kind = QA_MD3_LOD_INVALID;
      out->slots[lod].error = xstrdup(caught.message);
      if (num_lods == 0) {
        failed = true;
        break;
      }
      for (int lower = lod - 1; lower >= 0; lower--) {
        out->slots[lower].kind = QA_MD3_LOD_ALIAS;
        out->slots[lower].source_slot = lower + 1;
        num_lods++;
      }
      break;
    }
    out->slots[lod].kind = QA_MD3_LOD_LOADED;
    out->slots[lod].model = job.model;
    byte_length += (double)job.model->byte_length;
    num_lods++;
  }
  for (int i = 0; i < 3; i++) free(paths[i]);
  if (failed || num_lods == 0) {
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
