/* Port of src/formats/q3-model/md5.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_model/md5.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/formats/q3_model/quaternion.h"
#include "qa/formats/q3_model/text.h"
#include "qa/foundation/support.h"

#define FR(value) ((double)(float)(value))

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("Out of memory");
  return next;
}

static char *xstrdup(const char *text) {
  size_t length = strlen(text);
  char *copy = xmalloc(length + 1);
  memcpy(copy, text, length + 1);
  return copy;
}

static void list_push(qa_md5_string_list *list, char *value) {
  list->values = xrealloc(list->values, (list->count + 1) * sizeof *list->values);
  list->values[list->count++] = value;
}

static char *read_header(qa_model_tokens *tokens) {
  qa_model_tokens_expect(tokens, "MD5Version");
  qa_model_tokens_expect(tokens, "10");
  qa_model_tokens_expect(tokens, "commandline");
  return qa_model_tokens_token(tokens);
}

/* --- normal computation (computeNormals) --- */

static qa_vec3 vector_normal(qa_vec3 value) {
  double length = sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  return length == 0 ? value : qa_scale3(value, 1 / length);
}

static void compute_normals(qa_md5_mesh *mesh, const qa_skeleton_joint_pose *bind_pose, size_t joint_count) {
  size_t vertex_count = mesh->vertex_count;
  qa_vec3 *positions = xmalloc(vertex_count * sizeof *positions);
  for (size_t v = 0; v < vertex_count; v++) {
    const qa_md5_mesh_vertex *vertex = &mesh->vertices[v];
    qa_vec3 position = qa_math_vec3(0, 0, 0);
    for (double i = 0; i < vertex->weights.count; i++) {
      const qa_md5_mesh_weight *weight =
          &mesh->weights[qa_q3_model_at(vertex->weights.first + i, mesh->weight_count, "weight")];
      const qa_skeleton_joint_pose *joint = &bind_pose[qa_q3_model_at(weight->joint, joint_count, "joint")];
      qa_vec3 rotated = qa_rotate_quaternion(joint->orientation, weight->position);
      qa_vec3 world = qa_add3(joint->position, rotated);
      position = qa_math_vec3(position.x + world.x * weight->bias, position.y + world.y * weight->bias,
                              position.z + world.z * weight->bias);
    }
    positions[v] = position;
  }
  /* Position-keyed accumulation (linear search stands in for the Map). */
  qa_vec3 *keys = NULL;
  qa_vec3 *accumulated = NULL;
  size_t key_count = 0, key_capacity = 0;
  for (size_t i = 0; i + 2 < mesh->indices.count; i += 3) {
    qa_vec3 a = positions[qa_q3_model_at(
        mesh->indices.values[qa_q3_model_at((double)i, mesh->indices.count, "index")], vertex_count,
        "position")];
    qa_vec3 b = positions[qa_q3_model_at(
        mesh->indices.values[qa_q3_model_at((double)i + 1, mesh->indices.count, "index")], vertex_count,
        "position")];
    qa_vec3 c = positions[qa_q3_model_at(
        mesh->indices.values[qa_q3_model_at((double)i + 2, mesh->indices.count, "index")], vertex_count,
        "position")];
    qa_vec3 d1 = vector_normal(qa_math_vec3(c.x - a.x, c.y - a.y, c.z - a.z));
    qa_vec3 d2 = vector_normal(qa_math_vec3(b.x - a.x, b.y - a.y, b.z - a.z));
    qa_vec3 normal = vector_normal(qa_math_vec3(d1.y * d2.z - d1.z * d2.y, d1.z * d2.x - d1.x * d2.z,
                                                d1.x * d2.y - d1.y * d2.x));
    double dot = d1.x * d2.x + d1.y * d2.y + d1.z * d2.z;
    double angle = acos(fmax(-1, fmin(1, dot)));
    qa_vec3 weighted = qa_scale3(normal, angle);
    qa_vec3 corners[3] = {a, b, c};
    for (int k = 0; k < 3; k++) {
      size_t found = key_count;
      for (size_t j = 0; j < key_count; j++) {
        if (keys[j].x == corners[k].x && keys[j].y == corners[k].y && keys[j].z == corners[k].z) {
          found = j;
          break;
        }
      }
      if (found == key_count) {
        if (key_count == key_capacity) {
          key_capacity = key_capacity ? key_capacity * 2 : 16;
          keys = xrealloc(keys, key_capacity * sizeof *keys);
          accumulated = xrealloc(accumulated, key_capacity * sizeof *accumulated);
        }
        keys[key_count] = corners[k];
        accumulated[key_count] = qa_math_vec3(0, 0, 0);
        key_count++;
      }
      accumulated[found] = qa_add3(accumulated[found], weighted);
    }
  }
  for (size_t j = 0; j < key_count; j++) accumulated[j] = vector_normal(accumulated[j]);
  for (size_t v = 0; v < vertex_count; v++) {
    qa_md5_mesh_vertex *vertex = (qa_md5_mesh_vertex *)&mesh->vertices[v];
    qa_vec3 world_normal = qa_math_vec3(0, 0, 0);
    qa_vec3 position = positions[qa_q3_model_at((double)v, vertex_count, "position")];
    for (size_t j = 0; j < key_count; j++) {
      if (keys[j].x == position.x && keys[j].y == position.y && keys[j].z == position.z) {
        world_normal = accumulated[j];
        break;
      }
    }
    qa_vec3 normal = qa_math_vec3(0, 0, 0);
    for (double i = 0; i < vertex->weights.count; i++) {
      const qa_md5_mesh_weight *weight =
          &mesh->weights[qa_q3_model_at(vertex->weights.first + i, mesh->weight_count, "weight")];
      const qa_skeleton_joint_pose *joint = &bind_pose[qa_q3_model_at(weight->joint, joint_count, "joint")];
      qa_vec3 local = qa_rotate_quaternion(qa_conjugate_quaternion(joint->orientation), world_normal);
      normal = qa_math_vec3(normal.x + weight->bias * local.x, normal.y + weight->bias * local.y,
                            normal.z + weight->bias * local.z);
    }
    vertex->normal = normal;
  }
  free(positions);
  free(keys);
  free(accumulated);
}

/* --- mesh --- */

typedef struct {
  qa_model_tokens *tokens;
  size_t joint_count;
  double vertex_count;
} mesh_ctx;

static void read_vertex_record(void *ctx, void *out_element) {
  mesh_ctx *mesh = ctx;
  qa_md5_mesh_vertex *vertex = out_element;
  qa_model_tokens_expect(mesh->tokens, "(");
  vertex->tex_coord.x = qa_model_tokens_float(mesh->tokens);
  vertex->tex_coord.y = qa_model_tokens_float(mesh->tokens);
  qa_model_tokens_expect(mesh->tokens, ")");
  vertex->normal = qa_math_vec3(0, 0, 0);
  vertex->weights.first = qa_model_tokens_integer(mesh->tokens, 0, 2147483647.0);
  vertex->weights.count = qa_model_tokens_integer(mesh->tokens, 0, 2147483647.0);
}

typedef struct {
  double indices[3];
} triangle_record;

static void read_triangle_record(void *ctx, void *out_element) {
  mesh_ctx *mesh = ctx;
  triangle_record *triangle = out_element;
  for (int i = 0; i < 3; i++)
    triangle->indices[i] = qa_model_tokens_integer(mesh->tokens, 0, mesh->vertex_count - 1);
}

static void read_weight_record(void *ctx, void *out_element) {
  mesh_ctx *mesh = ctx;
  qa_md5_mesh_weight *weight = out_element;
  weight->joint = qa_model_tokens_integer(mesh->tokens, 0, (double)mesh->joint_count - 1);
  weight->bias = qa_model_tokens_float(mesh->tokens);
  if (weight->bias < 0 || weight->bias > 1) {
    char message[128];
    snprintf(message, sizeof message, "weight bias %g outside 0..1", weight->bias);
    qa_model_tokens_fail(mesh->tokens, message);
  }
  weight->position = qa_model_tokens_vector(mesh->tokens);
}

static void read_mesh(qa_model_tokens *tokens, const qa_skeleton_joint_pose *bind_pose, size_t joint_count,
                      qa_md5_mesh *out) {
  memset(out, 0, sizeof *out);
  qa_model_tokens_expect(tokens, "mesh");
  qa_model_tokens_expect(tokens, "{");
  qa_model_tokens_expect(tokens, "shader");
  out->shader = qa_model_tokens_token(tokens);
  qa_model_tokens_expect(tokens, "numverts");
  double vertex_count = qa_model_tokens_integer(tokens, 0, 65535);
  mesh_ctx ctx = {tokens, joint_count, vertex_count};
  out->vertices = qa_q3_indexed_records(tokens, (size_t)vertex_count, "vert", sizeof(qa_md5_mesh_vertex),
                                        read_vertex_record, &ctx);
  out->vertex_count = (size_t)vertex_count;
  qa_model_tokens_expect(tokens, "numtris");
  double triangle_count = qa_model_tokens_integer(tokens, 0, 65535);
  triangle_record *triangles = qa_q3_indexed_records(tokens, (size_t)triangle_count, "tri",
                                                     sizeof(triangle_record), read_triangle_record, &ctx);
  qa_model_tokens_expect(tokens, "numweights");
  double weight_count = qa_model_tokens_integer(tokens, 0, 1048576);
  out->weights = qa_q3_indexed_records(tokens, (size_t)weight_count, "weight", sizeof(qa_md5_mesh_weight),
                                       read_weight_record, &ctx);
  out->weight_count = (size_t)weight_count;
  qa_model_tokens_expect(tokens, "}");
  for (size_t i = 0; i < out->vertex_count; i++) {
    if (out->vertices[i].weights.first > weight_count - out->vertices[i].weights.count)
      qa_model_tokens_fail(tokens, "vertex weight range exceeds mesh weights");
  }
  double *indices = xmalloc((size_t)triangle_count * 3 * sizeof *indices);
  for (size_t i = 0; i < (size_t)triangle_count; i++)
    for (int j = 0; j < 3; j++) indices[i * 3 + (size_t)j] = triangles[i].indices[j];
  free(triangles);
  out->indices.values = indices;
  out->indices.count = (size_t)triangle_count * 3;
  compute_normals(out, bind_pose, joint_count);
}

void qa_parse_md5_mesh(const char *text, const char *source, qa_md5_mesh_file *out) {
  memset(out, 0, sizeof *out);
  const char *name = source != NULL ? source : "<md5mesh>";
  out->source = xstrdup(name);
  qa_model_tokens tokens;
  qa_model_tokens_init(&tokens, text, name);
  out->command_line = read_header(&tokens);
  qa_model_tokens_expect(&tokens, "numJoints");
  double joint_count = qa_model_tokens_integer(&tokens, 1, 256);
  qa_model_tokens_expect(&tokens, "numMeshes");
  double mesh_count = qa_model_tokens_integer(&tokens, 1, 32);
  qa_model_tokens_expect(&tokens, "joints");
  qa_model_tokens_expect(&tokens, "{");
  out->joints = xmalloc((size_t)joint_count * sizeof *out->joints);
  out->joint_count = (size_t)joint_count;
  out->bind_pose = xmalloc((size_t)joint_count * sizeof *out->bind_pose);
  out->bind_pose_count = (size_t)joint_count;
  for (size_t i = 0; i < out->joint_count; i++) {
    out->joints[i].name = qa_model_tokens_token(&tokens);
    out->joints[i].parent = qa_model_tokens_integer(&tokens, -1, joint_count - 1);
    out->joints[i].scale_positions = false;
    out->bind_pose[i].position = qa_model_tokens_vector(&tokens);
    out->bind_pose[i].orientation = qa_md5_quaternion(qa_model_tokens_vector(&tokens));
    out->bind_pose[i].scale = 1;
  }
  qa_model_tokens_expect(&tokens, "}");
  out->meshes = xmalloc((size_t)mesh_count * sizeof *out->meshes);
  out->mesh_count = (size_t)mesh_count;
  for (size_t i = 0; i < out->mesh_count; i++) read_mesh(&tokens, out->bind_pose, out->joint_count, &out->meshes[i]);
  qa_model_tokens_end(&tokens);
}

void qa_md5_mesh_file_destroy(qa_md5_mesh_file *file) {
  if (file == NULL) return;
  free(file->source);
  free(file->command_line);
  for (size_t i = 0; i < file->joint_count; i++) free((void *)file->joints[i].name);
  free(file->joints);
  free(file->bind_pose);
  for (size_t i = 0; i < file->mesh_count; i++) {
    free((void *)file->meshes[i].shader);
    free((void *)file->meshes[i].vertices);
    free((void *)file->meshes[i].indices.values);
    free((void *)file->meshes[i].weights);
  }
  free(file->meshes);
  memset(file, 0, sizeof *file);
}

/* --- minimal JSON for the optional scale object --- */

typedef struct {
  const char *p;
  bool ok;
} json;

static void json_ws(json *j) {
  while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') j->p++;
}

static char *json_string(json *j) {
  if (*j->p != '"') { j->ok = false; return NULL; }
  j->p++;
  char *result = xmalloc(1);
  size_t length = 0, capacity = 1;
  result[0] = '\0';
  while (*j->p != '"' && *j->p != '\0') {
    unsigned code = (unsigned char)*j->p;
    if (code == '\\') {
      j->p++;
      switch (*j->p) {
        case 'n': code = '\n'; break;
        case 't': code = '\t'; break;
        case 'r': code = '\r'; break;
        case 'b': code = '\b'; break;
        case 'f': code = '\f'; break;
        case 'u': {
          unsigned value = 0;
          for (int i = 0; i < 4; i++) {
            j->p++;
            value *= 16;
            if (*j->p >= '0' && *j->p <= '9') value += (unsigned)(*j->p - '0');
            else if (*j->p >= 'a' && *j->p <= 'f') value += (unsigned)(*j->p - 'a' + 10);
            else if (*j->p >= 'A' && *j->p <= 'F') value += (unsigned)(*j->p - 'A' + 10);
            else { j->ok = false; free(result); return NULL; }
          }
          code = value;
          break;
        }
        case '"': case '\\': case '/': code = (unsigned char)*j->p; break;
        default: j->ok = false; free(result); return NULL;
      }
      j->p++;
    } else {
      j->p++;
    }
    if (code < 0x80) {
      if (length + 1 >= capacity) { capacity *= 2; result = xrealloc(result, capacity); }
      result[length++] = (char)code;
    } else if (code < 0x800) {
      if (length + 2 >= capacity) { capacity *= 2; result = xrealloc(result, capacity); }
      result[length++] = (char)(0xc0 | (code >> 6));
      result[length++] = (char)(0x80 | (code & 0x3f));
    } else {
      if (length + 3 >= capacity) { capacity *= 2; result = xrealloc(result, capacity); }
      result[length++] = (char)(0xe0 | (code >> 12));
      result[length++] = (char)(0x80 | ((code >> 6) & 0x3f));
      result[length++] = (char)(0x80 | (code & 0x3f));
    }
    result[length] = '\0';
  }
  if (*j->p != '"') { j->ok = false; free(result); return NULL; }
  j->p++;
  return result;
}

typedef enum { JSON_OTHER, JSON_NUMBER, JSON_TRUE, JSON_OBJECT } json_kind;

static json_kind json_skip_value(json *j, double *out_number);

static json_kind json_skip_value(json *j, double *out_number) {
  json_ws(j);
  if (*j->p == '{') {
    j->p++;
    json_ws(j);
    if (*j->p == '}') { j->p++; return JSON_OBJECT; }
    for (;;) {
      char *key = json_string(j);
      free(key);
      if (!j->ok) return JSON_OTHER;
      json_ws(j);
      if (*j->p != ':') { j->ok = false; return JSON_OTHER; }
      j->p++;
      json_skip_value(j, out_number);
      if (!j->ok) return JSON_OTHER;
      json_ws(j);
      if (*j->p == ',') { j->p++; json_ws(j); continue; }
      if (*j->p == '}') { j->p++; return JSON_OBJECT; }
      j->ok = false;
      return JSON_OTHER;
    }
  }
  if (*j->p == '[') {
    j->p++;
    json_ws(j);
    if (*j->p == ']') { j->p++; return JSON_OTHER; }
    for (;;) {
      json_skip_value(j, out_number);
      if (!j->ok) return JSON_OTHER;
      json_ws(j);
      if (*j->p == ',') { j->p++; continue; }
      if (*j->p == ']') { j->p++; return JSON_OTHER; }
      j->ok = false;
      return JSON_OTHER;
    }
  }
  if (*j->p == '"') {
    char *value = json_string(j);
    free(value);
    return JSON_OTHER;
  }
  if (strncmp(j->p, "true", 4) == 0) { j->p += 4; return JSON_TRUE; }
  if (strncmp(j->p, "false", 5) == 0) { j->p += 5; return JSON_OTHER; }
  if (strncmp(j->p, "null", 4) == 0) { j->p += 4; return JSON_OTHER; }
  char *end = NULL;
  double value = strtod(j->p, &end);
  if (end == j->p) { j->ok = false; return JSON_OTHER; }
  j->p = end;
  *out_number = value;
  return JSON_NUMBER;
}

typedef struct {
  double *scales;       /* frame * joint_count + joint */
  uint8_t *scale_set;
  uint8_t *scale_positions;
} scale_result;

static void parse_scales(const qa_md5_scale_source *source, qa_md5_hierarchy_joint *hierarchy,
                         size_t joint_count, double frame_count, scale_result *out,
                         qa_md5_string_list *diagnostics) {
  size_t total = (size_t)frame_count * joint_count;
  out->scales = xmalloc(total * sizeof *out->scales);
  out->scale_set = xmalloc(total);
  out->scale_positions = xmalloc(joint_count);
  memset(out->scale_set, 0, total);
  memset(out->scale_positions, 0, joint_count);
  if (source == NULL) return;
  json j = {source->text, true};
  json_ws(&j);
  bool root_object = *j.p == '{';
  if (!root_object) {
    double ignored = 0;
    json_skip_value(&j, &ignored);
    json_ws(&j);
    if (!j.ok || *j.p != '\0') root_object = false;
  }
  if (!root_object) {
    char message[1024];
    snprintf(message, sizeof message, "%s: Invalid JSON scale object", source->source);
    list_push(diagnostics, xstrdup(message));
    return;
  }
  j.p++;
  json_ws(&j);
  bool stop = false;
  if (*j.p == '}') {
    j.p++;
  } else {
    while (!stop) {
      char *name = json_string(&j);
      if (!j.ok) { free(name); break; }
      json_ws(&j);
      if (*j.p != ':') { j.ok = false; free(name); break; }
      j.p++;
      json_ws(&j);
      bool entry_object = *j.p == '{';
      int joint = -1;
      for (size_t i = 0; i < joint_count; i++) {
        if (strcmp(hierarchy[i].joint.name, name) == 0) { joint = (int)i; break; }
      }
      if (!entry_object) {
        double ignored = 0;
        json_skip_value(&j, &ignored);
        char message[1024];
        snprintf(message, sizeof message, "%s: Invalid scale entry for %s", source->source, name);
        list_push(diagnostics, xstrdup(message));
        free(name);
        break;
      }
      if (joint < 0) {
        char message[1024];
        snprintf(message, sizeof message, "%s: No such joint %s", source->source, name);
        list_push(diagnostics, xstrdup(message));
      }
      j.p++;
      json_ws(&j);
      if (*j.p == '}') {
        j.p++;
      } else {
        for (;;) {
          char *key = json_string(&j);
          if (!j.ok) { free(key); stop = true; break; }
          json_ws(&j);
          if (*j.p != ':') { j.ok = false; free(key); stop = true; break; }
          j.p++;
          double number = 0;
          json_kind kind = json_skip_value(&j, &number);
          if (!j.ok) { free(key); stop = true; break; }
          if (joint >= 0) {
            if (strcmp(key, "scale_positions") == 0) {
              if (kind == JSON_TRUE) out->scale_positions[joint] = 1;
            } else {
              char *end = NULL;
              double frame = strtod(key, &end);
              bool frame_valid = key[0] != '\0' && end != key && *end == '\0' && frame == trunc(frame) &&
                                 frame >= 0 && frame < frame_count;
              if (frame_valid && kind == JSON_NUMBER && isfinite(FR(number))) {
                size_t at = (size_t)frame * joint_count + (size_t)joint;
                out->scales[at] = FR(number);
                out->scale_set[at] = 1;
              } else {
                char message[1024];
                snprintf(message, sizeof message, "%s: Invalid frame scale %s/%s", source->source, name, key);
                list_push(diagnostics, xstrdup(message));
              }
            }
          }
          free(key);
          json_ws(&j);
          if (*j.p == ',') { j.p++; json_ws(&j); continue; }
          if (*j.p == '}') { j.p++; break; }
          j.ok = false;
          stop = true;
          break;
        }
      }
      free(name);
      if (stop || !j.ok) break;
      json_ws(&j);
      if (*j.p == ',') { j.p++; json_ws(&j); continue; }
      if (*j.p == '}') { j.p++; break; }
      break;
    }
  }
  for (size_t i = 0; i < joint_count; i++) hierarchy[i].joint.scale_positions = out->scale_positions[i] != 0;
}

/* --- animation --- */

typedef struct {
  qa_model_tokens *tokens;
  size_t component_count;
} anim_ctx;

typedef struct {
  double *components;
} raw_frame;

static void read_raw_frame(void *ctx, void *out_element) {
  anim_ctx *anim = ctx;
  raw_frame *frame = out_element;
  qa_model_tokens_expect(anim->tokens, "{");
  frame->components = xmalloc(anim->component_count * sizeof *frame->components);
  for (size_t i = 0; i < anim->component_count; i++)
    frame->components[i] = qa_model_tokens_float(anim->tokens);
  qa_model_tokens_expect(anim->tokens, "}");
}

void qa_parse_md5_anim(const char *text, const char *source, const qa_md5_scale_source *scale_source,
                       qa_md5_animation *out) {
  memset(out, 0, sizeof *out);
  const char *name = source != NULL ? source : "<md5anim>";
  out->source = xstrdup(name);
  out->scale_source = scale_source != NULL ? xstrdup(scale_source->source) : NULL;
  qa_model_tokens tokens;
  qa_model_tokens_init(&tokens, text, name);
  out->command_line = read_header(&tokens);
  qa_model_tokens_expect(&tokens, "numFrames");
  double frame_count = qa_model_tokens_integer(&tokens, 1, 65535);
  qa_model_tokens_expect(&tokens, "numJoints");
  double joint_count = qa_model_tokens_integer(&tokens, 1, 256);
  qa_model_tokens_expect(&tokens, "frameRate");
  out->frame_rate = qa_model_tokens_integer(&tokens, 1, 1000);
  qa_model_tokens_expect(&tokens, "numAnimatedComponents");
  double component_count = qa_model_tokens_integer(&tokens, 0, joint_count * 6);
  qa_model_tokens_expect(&tokens, "hierarchy");
  qa_model_tokens_expect(&tokens, "{");
  out->hierarchy = xmalloc((size_t)joint_count * sizeof *out->hierarchy);
  out->hierarchy_count = (size_t)joint_count;
  for (size_t i = 0; i < out->hierarchy_count; i++) {
    qa_md5_hierarchy_joint *joint = &out->hierarchy[i];
    joint->joint.name = qa_model_tokens_token(&tokens);
    joint->joint.parent = qa_model_tokens_integer(&tokens, -1, (double)i - 1);
    joint->flags = qa_model_tokens_integer(&tokens, 0, 63);
    joint->start_index = qa_model_tokens_integer(&tokens, 0, component_count);
    joint->joint.scale_positions = false;
    int bits = 0;
    for (int bit = 0; bit < 6; bit++)
      if (((int)joint->flags & (1 << bit)) != 0) bits++;
    if (joint->start_index + bits > component_count)
      qa_model_tokens_fail(&tokens, "animated joint components exceed frame");
  }
  qa_model_tokens_expect(&tokens, "}");
  qa_model_tokens_expect(&tokens, "bounds");
  qa_model_tokens_expect(&tokens, "{");
  qa_bounds *bounds = xmalloc((size_t)frame_count * sizeof *bounds);
  for (size_t i = 0; i < (size_t)frame_count; i++) {
    bounds[i].min = qa_model_tokens_vector(&tokens);
    bounds[i].max = qa_model_tokens_vector(&tokens);
  }
  qa_model_tokens_expect(&tokens, "}");
  qa_model_tokens_expect(&tokens, "baseframe");
  qa_model_tokens_expect(&tokens, "{");
  out->base_frame = xmalloc((size_t)joint_count * sizeof *out->base_frame);
  out->base_frame_count = (size_t)joint_count;
  for (size_t i = 0; i < out->base_frame_count; i++) {
    out->base_frame[i].position = qa_model_tokens_vector(&tokens);
    out->base_frame[i].orientation = qa_md5_quaternion(qa_model_tokens_vector(&tokens));
    out->base_frame[i].scale = 1;
  }
  qa_model_tokens_expect(&tokens, "}");
  scale_result scales;
  parse_scales(scale_source, out->hierarchy, out->hierarchy_count, frame_count, &scales, &out->diagnostics);
  anim_ctx ctx = {&tokens, (size_t)component_count};
  raw_frame *raw_frames = qa_q3_indexed_records(&tokens, (size_t)frame_count, "frame", sizeof(raw_frame),
                                                read_raw_frame, &ctx);
  qa_model_tokens_end(&tokens);
  out->frames = xmalloc((size_t)frame_count * sizeof *out->frames);
  out->frame_count = (size_t)frame_count;
  for (size_t frame = 0; frame < out->frame_count; frame++) {
    qa_md5_animation_frame *entry = &out->frames[frame];
    entry->bounds = bounds[qa_q3_model_at((double)frame, (size_t)frame_count, "frame bounds")];
    entry->components = raw_frames[frame].components;
    entry->component_count = (size_t)component_count;
    entry->local_joints = xmalloc(out->hierarchy_count * sizeof *entry->local_joints);
    entry->local_joint_count = out->hierarchy_count;
    entry->joints = xmalloc(out->hierarchy_count * sizeof *entry->joints);
    entry->joint_count = out->hierarchy_count;
    for (size_t i = 0; i < out->hierarchy_count; i++) {
      const qa_md5_hierarchy_joint *info = &out->hierarchy[i];
      const qa_skeleton_joint_pose *base = &out->base_frame[i];
      size_t component = (size_t)info->start_index;
      int flags = (int)info->flags;
      double px = (flags & 1) == 0 ? base->position.x : entry->components[component++];
      double py = (flags & 2) == 0 ? base->position.y : entry->components[component++];
      double pz = (flags & 4) == 0 ? base->position.z : entry->components[component++];
      double ox = (flags & 8) == 0 ? base->orientation.x : entry->components[component++];
      double oy = (flags & 16) == 0 ? base->orientation.y : entry->components[component++];
      double oz = (flags & 32) == 0 ? base->orientation.z : entry->components[component++];
      qa_vec3 position = qa_math_vec3(px, py, pz);
      qa_vec4 orientation = qa_md5_quaternion(qa_math_vec3(ox, oy, oz));
      size_t scale_at = frame * out->hierarchy_count + i;
      double scale = scales.scale_set[scale_at] ? scales.scales[scale_at] : 1;
      entry->local_joints[i].position = position;
      entry->local_joints[i].orientation = orientation;
      entry->local_joints[i].scale = scale;
      qa_vec3 scaled_position = info->joint.scale_positions ? qa_scale3(position, scale) : position;
      if (info->joint.parent < 0) {
        entry->joints[i].position = scaled_position;
        entry->joints[i].orientation = orientation;
        entry->joints[i].scale = scale;
      } else {
        const qa_skeleton_joint_pose *parent =
            &entry->joints[qa_q3_model_at(info->joint.parent, i, "parent joint")];
        entry->joints[i].position = qa_add3(parent->position, qa_rotate_quaternion(parent->orientation,
                                                                                    scaled_position));
        entry->joints[i].orientation = qa_normalize_quaternion(
            qa_multiply_quaternion(parent->orientation, orientation));
        entry->joints[i].scale = scale;
      }
    }
  }
  free(raw_frames);
  free(bounds);
  free(scales.scales);
  free(scales.scale_set);
  free(scales.scale_positions);
}

void qa_md5_animation_destroy(qa_md5_animation *animation) {
  if (animation == NULL) return;
  free(animation->source);
  free(animation->command_line);
  for (size_t i = 0; i < animation->hierarchy_count; i++) free((void *)animation->hierarchy[i].joint.name);
  free(animation->hierarchy);
  free(animation->base_frame);
  for (size_t i = 0; i < animation->frame_count; i++) {
    free(animation->frames[i].components);
    free(animation->frames[i].local_joints);
    free(animation->frames[i].joints);
  }
  free(animation->frames);
  free(animation->scale_source);
  for (size_t i = 0; i < animation->diagnostics.count; i++) free(animation->diagnostics.values[i]);
  free(animation->diagnostics.values);
  memset(animation, 0, sizeof *animation);
}

void qa_create_md5_model(const qa_md5_mesh_file *mesh, const qa_md5_animation *animation,
                         const qa_md5_skin_selection *skin_selection, qa_decoded_md5_model *out) {
  memset(out, 0, sizeof *out);
  if (mesh->joint_count != animation->hierarchy_count) {
    qa_range_error("%s: mesh and animation joint counts differ", mesh->source);
  }
  out->mesh_file = mesh;
  out->animation = animation;
  qa_md5_joint *joints = xmalloc(animation->hierarchy_count * sizeof *joints);
  for (size_t i = 0; i < animation->hierarchy_count; i++) joints[i] = animation->hierarchy[i].joint;
  out->owned_joints = joints;
  qa_md5_model_frame *frames = xmalloc(animation->frame_count * sizeof *frames);
  for (size_t i = 0; i < animation->frame_count; i++) {
    frames[i].bounds = animation->frames[i].bounds;
    frames[i].joints = animation->frames[i].joints;
    frames[i].joint_count = animation->frames[i].joint_count;
  }
  out->owned_frames = frames;
  out->model.joints = joints;
  out->model.joint_count = animation->hierarchy_count;
  out->model.frame_rate = animation->frame_rate;
  out->model.frames = frames;
  out->model.frame_count = animation->frame_count;
  out->model.meshes = mesh->meshes;
  out->model.mesh_count = mesh->mesh_count;
  if (skin_selection != NULL) out->model.skin_selection = *skin_selection;
  else {
    memset(&out->model.skin_selection, 0, sizeof out->model.skin_selection);
    out->model.skin_selection.kind = "mesh-shaders";
  }
}

void qa_decoded_md5_model_destroy(qa_decoded_md5_model *model) {
  if (model == NULL) return;
  free(model->owned_frames);
  free(model->owned_joints);
  memset(model, 0, sizeof *model);
}

const qa_skeleton_joint_pose *qa_sample_md5_pose(const qa_md5_model *model, double frame,
                                                 double previous_frame, double back_lerp, bool *out_owned,
                                                 size_t *out_count) {
  if (frame != trunc(frame) || frame < 0 || previous_frame != trunc(previous_frame) || previous_frame < 0 ||
      !isfinite(back_lerp)) {
    qa_range_error("Invalid MD5 frame selection");
  }
  if (model->frame_count == 0) qa_range_error("Missing MD5 frame %g", NAN);
  double current_index = fmod(frame, (double)model->frame_count);
  const qa_md5_model_frame *current_frame =
      &model->frames[qa_q3_model_at(current_index, model->frame_count, "MD5 frame")];
  *out_count = current_frame->joint_count;
  if (back_lerp == 0 || frame == previous_frame) {
    *out_owned = false;
    return current_frame->joints;
  }
  double previous_index = fmod(previous_frame, (double)model->frame_count);
  const qa_md5_model_frame *previous_frame_entry =
      &model->frames[qa_q3_model_at(previous_index, model->frame_count, "MD5 previous frame")];
  qa_skeleton_joint_pose *result = xmalloc(current_frame->joint_count * sizeof *result);
  for (size_t i = 0; i < current_frame->joint_count; i++) {
    const qa_skeleton_joint_pose *joint = &current_frame->joints[i];
    const qa_skeleton_joint_pose *old =
        &previous_frame_entry->joints[qa_q3_model_at((double)i, previous_frame_entry->joint_count,
                                                    "previous joint")];
    result[i].position = qa_math_vec3(old->position.x * back_lerp + joint->position.x * (1 - back_lerp),
                                      old->position.y * back_lerp + joint->position.y * (1 - back_lerp),
                                      old->position.z * back_lerp + joint->position.z * (1 - back_lerp));
    result[i].orientation = qa_slerp_quaternion(old->orientation, joint->orientation, back_lerp);
    result[i].scale = joint->scale;
  }
  *out_owned = true;
  return result;
}

qa_model_vertex *qa_skin_md5_mesh(const qa_md5_mesh *mesh, const qa_skeleton_joint_pose *joints,
                                  size_t joint_count, size_t *out_count) {
  qa_quaternion_rotation_rows *rotations = xmalloc(joint_count * sizeof *rotations);
  uint8_t *rotation_set = xmalloc(joint_count);
  memset(rotation_set, 0, joint_count);
  qa_model_vertex *result = xmalloc(mesh->vertex_count * sizeof *result);
  for (size_t v = 0; v < mesh->vertex_count; v++) {
    const qa_md5_mesh_vertex *vertex = &mesh->vertices[v];
    qa_vec3 position = qa_math_vec3(0, 0, 0);
    qa_vec3 normal = qa_math_vec3(0, 0, 0);
    for (double i = 0; i < vertex->weights.count; i++) {
      const qa_md5_mesh_weight *weight =
          &mesh->weights[qa_q3_model_at(vertex->weights.first + i, mesh->weight_count, "weight")];
      size_t joint_index = qa_q3_model_at(weight->joint, joint_count, "joint");
      const qa_skeleton_joint_pose *joint = &joints[joint_index];
      if (!rotation_set[joint_index]) {
        rotations[joint_index] = qa_quaternion_rotation_rows_of(joint->orientation);
        rotation_set[joint_index] = 1;
      }
      qa_quaternion_rotation_rows rows = rotations[joint_index];
      qa_vec3 rotated = qa_rotate_quaternion_rows(rows, weight->position);
      qa_vec3 point = qa_math_vec3(joint->position.x + joint->scale * rotated.x,
                                   joint->position.y + joint->scale * rotated.y,
                                   joint->position.z + joint->scale * rotated.z);
      qa_vec3 direction = qa_rotate_quaternion_rows(rows, vertex->normal);
      position = qa_math_vec3(position.x + weight->bias * point.x, position.y + weight->bias * point.y,
                              position.z + weight->bias * point.z);
      normal = qa_math_vec3(normal.x + weight->bias * direction.x, normal.y + weight->bias * direction.y,
                            normal.z + weight->bias * direction.z);
    }
    result[v].position = position;
    result[v].normal = normal;
  }
  free(rotations);
  free(rotation_set);
  *out_count = mesh->vertex_count;
  return result;
}
