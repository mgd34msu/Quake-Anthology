/* Port of src/content/model-attachment.ts. GPL-2.0-or-later. */
#include "qa/content/model_attachment.h"

#include <math.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/content.h"
#include "qa/core/math.h"

static noreturn void plain_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}
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
static qa_vec3 read_vector(const qa_save_reader *value) {
  qa_vec3 vector;
  qa_save_reader x = field(value, "x");
  vector.x = qa_save_reader_finite(&x);
  qa_save_reader_destroy(&x);
  qa_save_reader y = field(value, "y");
  vector.y = qa_save_reader_finite(&y);
  qa_save_reader_destroy(&y);
  qa_save_reader z = field(value, "z");
  vector.z = qa_save_reader_finite(&z);
  qa_save_reader_destroy(&z);
  return vector;
}

qa_model_transform qa_read_model_grip(const qa_save_reader *grip) {
  qa_save_reader axis = field(grip, "axis");
  size_t axis_count = qa_save_reader_list_count(&axis);
  qa_vec3 axes[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  size_t read_count = axis_count < 3 ? axis_count : 3;
  for (size_t i = 0; i < read_count; i++) {
    qa_save_reader element = item(&axis, i);
    axes[i] = read_vector(&element);
    qa_save_reader_destroy(&element);
  }
  qa_save_reader_destroy(&axis);
  if (axis_count != 3) plain_error("Model grip requires three source axes");
  qa_vec3 first = axes[0], second = axes[1], third = axes[2];
  bool bad = false;
  for (int i = 0; i < 3; i++)
    if (fabs(qa_dot3(axes[i], axes[i]) - 1) > 0.001) bad = true;
  if (fabs(qa_dot3(first, second)) > 0.001 ||
      fabs(qa_dot3(first, third)) > 0.001 ||
      fabs(qa_dot3(second, third)) > 0.001 ||
      qa_dot3(qa_cross3(first, second), third) < 0.999)
    bad = true;
  if (bad) plain_error("Model grip axes must form a rotation");
  qa_save_reader scale_reader = field(grip, "scale");
  qa_vec3 scale = {1, 1, 1};
  if (!qa_save_reader_is_undefined(&scale_reader))
    scale = read_vector(&scale_reader);
  qa_save_reader_destroy(&scale_reader);
  if (scale.x == 0 || scale.y == 0 || scale.z == 0)
    qa_save_reader_fail(grip, "Model grip scale must be invertible");
  qa_save_reader origin_reader = field(grip, "origin");
  qa_vec3 origin = read_vector(&origin_reader);
  qa_save_reader_destroy(&origin_reader);
  qa_model_transform transform;
  transform.origin = origin;
  transform.axis.vectors[0] = first;
  transform.axis.vectors[1] = second;
  transform.axis.vectors[2] = third;
  transform.scale = scale;
  return transform;
}

bool qa_content_read_model_attachment(const uint8_t *bytes, size_t length,
                                      qa_model_attachment_definition *out) {
  memset(out, 0, sizeof *out);
  qa_save_value *input = qa_parse_json_value(bytes, length, false);
  if (input == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, input, NULL)) {
    qa_save_value_free(input);
    return false;
  }
  qa_save_reader version = field(&reader, "version");
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader digest_reader = field(&reader, "digest");
  const char *digest_text = qa_save_reader_string(&digest_reader);
  qa_save_reader_destroy(&digest_reader);
  if (!qa_is_content_digest(digest_text))
    plain_error("Model attachment requires its source digest");
  char *digest = copy_text(digest_text);
  if (digest == NULL) {
    qa_save_reader_destroy(&reader);
    qa_save_value_free(input);
    return false;
  }
  qa_save_reader grip_reader = field(&reader, "grip");
  qa_model_transform grip = qa_read_model_grip(&grip_reader);
  qa_save_reader_destroy(&grip_reader);
  static const char *const kinds[] = {"joint", "mesh"};
  qa_save_reader kind_reader = field(&reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 2);
  qa_save_reader_destroy(&kind_reader);
  out->digest = digest;
  out->grip = grip;
  if (strcmp(kind, "joint") == 0) {
    qa_save_reader name_reader = field(&reader, "name");
    const char *name_text = qa_save_reader_string(&name_reader);
    qa_save_reader_destroy(&name_reader);
    char *name = copy_text(name_text);
    if (name == NULL) {
      qa_content_model_attachment_free(out);
      qa_save_reader_destroy(&reader);
      qa_save_value_free(input);
      return false;
    }
    out->is_joint = true;
    out->name = name;
    qa_save_reader_destroy(&reader);
    qa_save_value_free(input);
    return true;
  }
  qa_save_reader vertices_reader = field(&reader, "vertices");
  size_t vertex_count = qa_save_reader_list_count(&vertices_reader);
  double vertices[3] = {0, 0, 0};
  size_t read_count = vertex_count < 3 ? vertex_count : 3;
  for (size_t i = 0; i < read_count; i++) {
    qa_save_reader element = item(&vertices_reader, i);
    vertices[i] = qa_save_reader_integer_min(&element, 0);
    qa_save_reader_destroy(&element);
  }
  qa_save_reader_destroy(&vertices_reader);
  if (vertex_count != 3 || vertices[0] == vertices[1] ||
      vertices[0] == vertices[2] || vertices[1] == vertices[2])
    plain_error("Model attachment requires three distinct source vertices");
  qa_save_reader frame_reader = field(&reader, "referenceFrame");
  double reference_frame = qa_save_reader_integer_min(&frame_reader, 0);
  qa_save_reader_destroy(&frame_reader);
  out->is_joint = false;
  out->reference_frame = reference_frame;
  out->vertices[0] = vertices[0];
  out->vertices[1] = vertices[1];
  out->vertices[2] = vertices[2];
  qa_save_reader_destroy(&reader);
  qa_save_value_free(input);
  return true;
}

void qa_content_model_attachment_free(qa_model_attachment_definition *value) {
  free((void *)(const void *)value->digest);
  value->digest = NULL;
  free((void *)(const void *)value->name);
  value->name = NULL;
}
