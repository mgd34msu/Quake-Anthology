/* Port of src/content/held-weapon.ts. GPL-2.0-or-later. */
#include "qa/content/held_weapon.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/model_attachment.h"
#include "qa/content/mounts/paths.h"
#include "qa/contracts/content.h"

static char *copy_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
/* TS readDigest: parsed string, or fail on that value's reader. */
static const char *read_digest(const qa_save_reader *value) {
  const char *parsed = qa_save_reader_string(value);
  if (!qa_is_content_digest(parsed))
    qa_save_reader_fail(value, "held model requires a SHA256 digest");
  return parsed;
}
static char *normalized_field_string(const qa_save_reader *reader,
                                     const char *name) {
  qa_save_reader value = field(reader, name);
  const char *text = qa_save_reader_string(&value);
  qa_save_reader_destroy(&value);
  return qa_normalize_resource_path(text);
}

void qa_content_held_weapon_free(qa_held_weapon_declaration *value) {
  qa_held_weapon_model *model = &value->model;
  free((void *)(const void *)model->digest);
  model->digest = NULL;
  free((void *)(const void *)model->path);
  model->path = NULL;
  free((void *)(const void *)model->fallback);
  model->fallback = NULL;
  if (model->part != NULL) {
    qa_held_weapon_part *part = (qa_held_weapon_part *)(const void *)model->part;
    for (size_t i = 0; i < part->digests.count; i++)
      free((void *)(const void *)part->digests.values[i]);
    free((void *)(const void *)part->digests.values);
    free((void *)(const void *)part->vertices.values);
    free(part);
    model->part = NULL;
  }
  value->is_model = false;
}

bool qa_read_held_weapon_declaration(const qa_save_reader *reader,
                                     qa_held_weapon_declaration *out) {
  memset(out, 0, sizeof *out);
  static const char *const kinds[] = {"none", "model"};
  qa_save_reader kind_reader = field(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 2);
  qa_save_reader_destroy(&kind_reader);
  if (strcmp(kind, "none") == 0) return true;

  qa_save_reader model = field(reader, "model");
  qa_save_reader part = field(&model, "part");
  qa_save_reader digest_reader = field(&model, "digest");

  qa_held_weapon_part *subset = NULL;
  if (!qa_save_reader_is_undefined(&part)) {
    qa_save_reader digests_reader = field(&part, "digests");
    size_t digest_count = qa_save_reader_list_count(&digests_reader);
    const char **digests =
        (const char **)calloc(digest_count ? digest_count : 1, sizeof *digests);
    qa_save_reader vertices_reader = field(&part, "vertices");
    size_t vertex_count = qa_save_reader_list_count(&vertices_reader);
    double *vertices =
        (double *)calloc(vertex_count ? vertex_count : 1, sizeof *vertices);
    subset = (qa_held_weapon_part *)calloc(1, sizeof *subset);
    if (digests == NULL || vertices == NULL || subset == NULL) {
      free(digests); free(vertices); free(subset);
      qa_save_reader_destroy(&digests_reader);
      qa_save_reader_destroy(&vertices_reader);
      qa_save_reader_destroy(&part);
      qa_save_reader_destroy(&digest_reader);
      qa_save_reader_destroy(&model);
      return false;
    }
    subset->digests.values = digests;
    subset->digests.count = digest_count;
    subset->vertices.values = vertices;
    subset->vertices.count = vertex_count;
    out->model.part = subset; /* for the OOM cleanup below */
    for (size_t i = 0; i < digest_count; i++) {
      qa_save_reader element = item(&digests_reader, i);
      const char *text = read_digest(&element);
      digests[i] = copy_text(text);
      qa_save_reader_destroy(&element);
      if (digests[i] == NULL) {
        qa_save_reader_destroy(&digests_reader);
        qa_save_reader_destroy(&vertices_reader);
        goto oom_part;
      }
    }
    for (size_t i = 0; i < vertex_count; i++) {
      qa_save_reader element = item(&vertices_reader, i);
      vertices[i] = qa_save_reader_integer_min(&element, 0);
      qa_save_reader_destroy(&element);
    }
    qa_save_reader_destroy(&digests_reader);
    qa_save_reader_destroy(&vertices_reader);
    bool distinct = true;
    for (size_t i = 0; i < vertex_count && distinct; i++)
      for (size_t j = i + 1; j < vertex_count; j++)
        if (vertices[i] == vertices[j]) distinct = false;
    if (digest_count == 0 || vertex_count == 0 || !distinct)
      qa_save_reader_fail(&part,
                          "held model subset requires source digests and distinct vertices");
  }
  {
    char *path = normalized_field_string(&model, "path");
    if (path == NULL) goto oom_part;
    out->model.path = path;
    qa_save_reader frame = field(&model, "referenceFrame");
    out->model.reference_frame = qa_save_reader_integer_min(&frame, 0);
    qa_save_reader_destroy(&frame);
    qa_save_reader grip = field(&model, "grip");
    out->model.grip = qa_read_model_grip(&grip);
    qa_save_reader_destroy(&grip);
    if (!qa_save_reader_is_undefined(&digest_reader)) {
      char *digest = copy_text(read_digest(&digest_reader));
      if (digest == NULL) goto oom_part;
      out->model.has_digest = true;
      out->model.digest = digest;
    }
    qa_save_reader fallback_reader = field(&model, "fallback");
    bool has_fallback = !qa_save_reader_is_undefined(&fallback_reader);
    qa_save_reader_destroy(&fallback_reader);
    if (has_fallback) {
      char *fallback = normalized_field_string(&model, "fallback");
      if (fallback == NULL) goto oom_part;
      out->model.fallback = fallback;
    }
  }
  qa_save_reader_destroy(&part);
  qa_save_reader_destroy(&digest_reader);
  qa_save_reader_destroy(&model);
  out->is_model = true;
  return true;
oom_part:
  qa_save_reader_destroy(&part);
  qa_save_reader_destroy(&digest_reader);
  qa_save_reader_destroy(&model);
  qa_content_held_weapon_free(out);
  return false;
}

bool qa_read_held_weapon_file(const uint8_t *bytes, size_t length,
                              qa_held_weapon_declaration *out) {
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, NULL)) {
    qa_save_value_free(value);
    return false;
  }
  qa_save_reader version = field(&reader, "version");
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  bool ok = qa_read_held_weapon_declaration(&reader, out);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(value);
  return ok;
}
