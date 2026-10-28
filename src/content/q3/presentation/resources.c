/* Port of src/content/q3/presentation/resources.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/resources.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

typedef struct picture_entry {
  const qa_material_picture *picture; /* host-owned */
  qa_q3_pres_scene_shader shader;     /* stable SceneShader identity */
  double handle;
} picture_entry;
typedef struct model_name_entry { char *path; size_t model_index; } model_name_entry;
typedef struct skin_entry { char *path; const qa_q3_pres_scene_skin *skin; } skin_entry;
typedef struct picture_key_entry { char *key; picture_entry *entry; } picture_key_entry;
typedef struct shader_request { char *path; bool mip; double handle; } shader_request;

struct qa_q3_pres_q3_renderer_resources {
  qa_q3_pres_resource_host host;
  bool has_world;
  qa_q3_pres_resource_world world;
  qa_q3_pres_resource_handles handles;
  qa_q3_pres_scene_model *models; size_t model_count, model_capacity;
  model_name_entry *model_names; size_t model_name_count, model_name_capacity;
  const qa_q3_pres_scene_skin **skin_handles; size_t skin_handle_count, skin_handle_capacity;
  skin_entry *skins; size_t skin_count, skin_capacity;
  picture_entry **pictures; size_t picture_count, picture_capacity;
  picture_key_entry *picture_keys; size_t picture_key_count, picture_key_capacity;
  shader_request *requests; size_t request_count, request_capacity;
  double operations;
  qa_common_parse_state entity_parser;
  qa_common_parse_cursor entity_cursor;
  bool world_loaded;
};
typedef qa_q3_pres_q3_renderer_resources resources_t;

static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}
static char *lower_key(const char *text) {
  char *copy = xstrdup(text);
  if (copy == NULL) return NULL;
  for (char *at = copy; *at != '\0'; at++) {
    if (*at >= 'A' && *at <= 'Z') *at = (char)(*at + 32);
  }
  return copy;
}
static bool grow(void **items, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return true;
  size_t next = *capacity == 0 ? 8 : *capacity * 2;
  void *grown = realloc(*items, next * size);
  if (grown == NULL) return false;
  *items = grown;
  *capacity = next;
  return true;
}
static bool string_equal(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
/* SceneModel object identity, by kind + path + resource id. */
static bool model_same(const qa_q3_pres_scene_model *a, const qa_q3_pres_scene_model *b) {
  return a->kind == b->kind && string_equal(a->path, b->path) && string_equal(a->resource.id, b->resource.id);
}
static const char *model_resource_id(const qa_q3_pres_scene_model *model) {
  return model->kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT ? NULL : model->resource.id;
}

bool qa_q3_pres_q3_renderer_resources_init(resources_t **out, qa_q3_pres_resource_host host,
    const qa_q3_pres_resource_world *world, qa_q3_pres_resource_handles handles) {
  resources_t *resources = calloc(1, sizeof *resources);
  if (resources == NULL) return false;
  resources->host = host;
  resources->has_world = world != NULL;
  if (world != NULL) resources->world = *world;
  resources->handles = handles;
  if (!grow((void **)&resources->models, &resources->model_capacity, 0, sizeof *resources->models) ||
      !grow((void **)&resources->skin_handles, &resources->skin_handle_capacity, 0,
            sizeof *resources->skin_handles)) {
    qa_q3_pres_q3_renderer_resources_destroy(resources);
    return false;
  }
  resources->models[resources->model_count++] = qa_q3_pres_default_model;
  resources->skin_handles[resources->skin_handle_count++] = NULL;
  qa_common_parse_state_init(&resources->entity_parser);
  if (!qa_common_parse_cursor_init(&resources->entity_cursor, "", 0, false)) {
    qa_q3_pres_q3_renderer_resources_destroy(resources);
    return false;
  }
  *out = resources;
  return true;
}

void qa_q3_pres_q3_renderer_resources_destroy(resources_t *resources) {
  if (resources == NULL) return;
  free(resources->models);
  for (size_t i = 0; i < resources->model_name_count; i++) free(resources->model_names[i].path);
  free(resources->model_names);
  free(resources->skin_handles);
  for (size_t i = 0; i < resources->skin_count; i++) free(resources->skins[i].path);
  free(resources->skins);
  for (size_t i = 0; i < resources->picture_count; i++) free(resources->pictures[i]);
  free(resources->pictures);
  for (size_t i = 0; i < resources->picture_key_count; i++) free(resources->picture_keys[i].key);
  free(resources->picture_keys);
  for (size_t i = 0; i < resources->request_count; i++) free(resources->requests[i].path);
  free(resources->requests);
  qa_common_parse_cursor_destroy(&resources->entity_cursor);
  free(resources);
}

bool qa_q3_pres_q3_renderer_resources_register_model(resources_t *resources, const char *path,
    qa_q3_pres_scene_model *out) {
  if (path == NULL || path[0] == '\0') {
    *out = qa_q3_pres_default_model;
    return true;
  }
  for (size_t i = 0; i < resources->model_name_count; i++) {
    if (strcmp(resources->model_names[i].path, path) == 0) {
      *out = resources->models[resources->model_names[i].model_index];
      return true;
    }
  }
  resources->operations++;
  qa_q3_pres_scene_model model;
  bool ok = resources->host.model(resources->host.ctx, path, &model);
  resources->operations--;
  if (!ok) return false;
  size_t model_index = 0;
  bool found = false;
  if (model.kind != QA_Q3_PRES_SCENE_MODEL_DEFAULT) {
    for (size_t i = 0; i < resources->model_count; i++) {
      if (model_same(&resources->models[i], &model)) { model_index = i; found = true; break; }
    }
    if (!found) {
      if (!grow((void **)&resources->models, &resources->model_capacity, resources->model_count,
                sizeof *resources->models)) return false;
      model_index = resources->model_count;
      resources->models[resources->model_count++] = model;
    }
  }
  if (!grow((void **)&resources->model_names, &resources->model_name_capacity, resources->model_name_count,
            sizeof *resources->model_names)) return false;
  char *copy = xstrdup(path);
  if (copy == NULL) return false;
  resources->model_names[resources->model_name_count].path = copy;
  resources->model_names[resources->model_name_count].model_index = model_index;
  resources->model_name_count++;
  *out = resources->models[model_index];
  return true;
}

bool qa_q3_pres_q3_renderer_resources_register_skin(resources_t *resources, const char *path,
    const qa_q3_pres_scene_skin **out) {
  for (size_t i = 0; i < resources->skin_count; i++) {
    if (strcmp(resources->skins[i].path, path) == 0) {
      *out = resources->skins[i].skin;
      return true;
    }
  }
  resources->operations++;
  const qa_q3_pres_scene_skin *skin = NULL;
  bool ok = resources->host.skin(resources->host.ctx, path, &skin);
  resources->operations--;
  if (!ok) return false;
  if (!grow((void **)&resources->skins, &resources->skin_capacity, resources->skin_count,
            sizeof *resources->skins)) return false;
  char *copy = xstrdup(path);
  if (copy == NULL) return false;
  resources->skins[resources->skin_count].path = copy;
  resources->skins[resources->skin_count].skin = skin;
  resources->skin_count++;
  if (skin != NULL) {
    bool present = false;
    for (size_t i = 0; i < resources->skin_handle_count; i++) {
      if (resources->skin_handles[i] == skin) { present = true; break; }
    }
    if (!present) {
      if (!grow((void **)&resources->skin_handles, &resources->skin_handle_capacity,
                resources->skin_handle_count, sizeof *resources->skin_handles)) return false;
      resources->skin_handles[resources->skin_handle_count++] = skin;
    }
  }
  *out = skin;
  return true;
}

static picture_entry *picture_for_key(resources_t *resources, const char *key) {
  for (size_t i = 0; i < resources->picture_key_count; i++) {
    if (strcmp(resources->picture_keys[i].key, key) == 0) return resources->picture_keys[i].entry;
  }
  return NULL;
}
static bool set_picture_key(resources_t *resources, const char *key, picture_entry *entry) {
  for (size_t i = 0; i < resources->picture_key_count; i++) {
    if (strcmp(resources->picture_keys[i].key, key) == 0) {
      resources->picture_keys[i].entry = entry;
      return true;
    }
  }
  if (!grow((void **)&resources->picture_keys, &resources->picture_key_capacity, resources->picture_key_count,
            sizeof *resources->picture_keys)) return false;
  char *copy = xstrdup(key);
  if (copy == NULL) return false;
  resources->picture_keys[resources->picture_key_count].key = copy;
  resources->picture_keys[resources->picture_key_count].entry = entry;
  resources->picture_key_count++;
  return true;
}
static picture_entry *picture_by_handle(resources_t *resources, double handle) {
  for (size_t i = 0; i < resources->picture_count; i++) {
    if (resources->pictures[i]->handle == handle) return resources->pictures[i];
  }
  return NULL;
}

static bool register_shader_impl(resources_t *resources, const char *path, bool mip,
    const qa_q3_pres_scene_shader **out) {
  char *key = lower_key(path);
  if (key == NULL) return false;
  picture_entry *prior = picture_for_key(resources, key);
  if (prior != NULL) {
    free(key);
    *out = &prior->shader;
    return true;
  }
  resources->operations++;
  const qa_material_picture *picture = NULL;
  bool ok = resources->host.shader(resources->host.ctx, path, mip, &picture);
  resources->operations--;
  if (!ok) { free(key); return false; }
  if (picture == NULL) {
    free(key);
    *out = NULL;
    return true;
  }
  picture_entry *entry = NULL;
  for (size_t i = 0; i < resources->picture_count; i++) {
    if (resources->pictures[i]->picture == picture) { entry = resources->pictures[i]; break; }
  }
  if (entry == NULL) {
    entry = malloc(sizeof *entry);
    if (entry == NULL) { free(key); return false; }
    entry->picture = picture;
    entry->shader.name = picture->name;
    entry->handle = resources->handles == QA_Q3_PRES_HANDLES_CLIENT
        ? (double)resources->picture_count + 1 : picture->order;
    if (!grow((void **)&resources->pictures, &resources->picture_capacity, resources->picture_count,
              sizeof *resources->pictures)) {
      free(entry);
      free(key);
      return false;
    }
    resources->pictures[resources->picture_count++] = entry;
  }
  if (!set_picture_key(resources, key, entry)) { free(key); return false; }
  free(key);
  char *name_key = lower_key(picture->name);
  if (name_key == NULL) return false;
  bool key_ok = set_picture_key(resources, name_key, entry);
  free(name_key);
  if (!key_ok) return false;
  if (!grow((void **)&resources->requests, &resources->request_capacity, resources->request_count,
            sizeof *resources->requests)) return false;
  char *copy = xstrdup(path);
  if (copy == NULL) return false;
  resources->requests[resources->request_count].path = copy;
  resources->requests[resources->request_count].mip = mip;
  resources->requests[resources->request_count].handle = entry->handle;
  resources->request_count++;
  *out = &entry->shader;
  return true;
}

bool qa_q3_pres_q3_renderer_resources_register_shader(resources_t *resources, const char *path,
    const qa_q3_pres_scene_shader **out) {
  return register_shader_impl(resources, path, true, out);
}
bool qa_q3_pres_q3_renderer_resources_register_shader_no_mip(resources_t *resources, const char *path,
    const qa_q3_pres_scene_shader **out) {
  if (path == NULL) {
    *out = NULL;
    return true;
  }
  return register_shader_impl(resources, path, false, out);
}

static picture_entry *picture_entry_for_shader(resources_t *resources, const qa_q3_pres_scene_shader *shader) {
  char *key = lower_key(shader->name);
  if (key == NULL) qa_range_error("out of memory");
  picture_entry *entry = picture_for_key(resources, key);
  free(key);
  if (entry == NULL) {
    qa_range_error("Cgame shader is not registered in this resource owner: %s", shader->name);
  }
  return entry;
}
qa_material_picture qa_q3_pres_q3_renderer_resources_picture(resources_t *resources,
    const qa_q3_pres_scene_shader *shader) {
  if (shader == NULL) return resources->host.zero_picture;
  return *picture_entry_for_shader(resources, shader)->picture;
}
double qa_q3_pres_q3_renderer_resources_model_handle(resources_t *resources,
    const qa_q3_pres_scene_model *model) {
  for (size_t i = 0; i < resources->model_count; i++) {
    if (model_same(&resources->models[i], model)) return (double)i;
  }
  qa_range_error("Cgame model belongs to another resource owner");
}
static void handle_error(const char *kind, double handle) {
  if (handle == trunc(handle) && fabs(handle) < 1e21) {
    qa_range_error("Invalid cgame %s handle %lld", kind, (long long)handle);
  }
  qa_range_error("Invalid cgame %s handle %g", kind, handle);
}
qa_q3_pres_scene_model qa_q3_pres_q3_renderer_resources_model_for_handle(resources_t *resources, double handle) {
  if (handle != trunc(handle) || handle < 0 || handle >= (double)resources->model_count) {
    handle_error("model", handle);
  }
  return resources->models[(size_t)handle];
}
const qa_q3_pres_scene_shader *qa_q3_pres_q3_renderer_resources_shader_for_handle(resources_t *resources,
    double handle) {
  if (handle == 0) return NULL;
  picture_entry *entry = picture_by_handle(resources, handle);
  if (entry == NULL) handle_error("shader", handle);
  return &entry->shader;
}
double qa_q3_pres_q3_renderer_resources_shader_handle(resources_t *resources,
    const qa_q3_pres_scene_shader *shader) {
  if (shader == NULL) return 0;
  picture_entry *entry = picture_entry_for_shader(resources, shader);
  if (resources->handles == QA_Q3_PRES_HANDLES_RENDERER) return entry->picture->order;
  return entry->handle;
}
double qa_q3_pres_q3_renderer_resources_skin_handle(resources_t *resources, const qa_q3_pres_scene_skin *skin) {
  for (size_t i = 0; i < resources->skin_handle_count; i++) {
    if (resources->skin_handles[i] == skin) return (double)i;
  }
  qa_range_error("Cgame skin belongs to another resource owner");
}
const qa_q3_pres_scene_skin *qa_q3_pres_q3_renderer_resources_skin_for_handle(resources_t *resources,
    double handle) {
  if (handle != trunc(handle) || handle < 0 || handle >= (double)resources->skin_handle_count) {
    handle_error("skin", handle);
  }
  return resources->skin_handles[(size_t)handle];
}
void qa_q3_pres_q3_renderer_resources_clear_scene(resources_t *resources) {
  resources->host.scene.clear_scene(resources->host.scene.ctx);
}
void qa_q3_pres_q3_renderer_resources_add_ref_entity(resources_t *resources,
    const qa_q3_pres_admitted_ref_entity *entity) {
  resources->host.scene.add_ref_entity(resources->host.scene.ctx, entity);
}
void qa_q3_pres_q3_renderer_resources_add_poly(resources_t *resources, const qa_q3_pres_ref_poly *poly) {
  resources->host.scene.add_poly(resources->host.scene.ctx, poly);
}
void qa_q3_pres_q3_renderer_resources_add_light(resources_t *resources, qa_q3_pres_dynamic_light light) {
  resources->host.scene.add_light(resources->host.scene.ctx, light);
}
void qa_q3_pres_q3_renderer_resources_remap_shader(resources_t *resources, const char *original,
    const char *replacement, const char *offset) {
  resources->host.remap_shader(resources->host.ctx, original, replacement, offset);
}
void qa_q3_pres_q3_renderer_resources_render_scene(resources_t *resources, const qa_q3_pres_refdef *refdef) {
  resources->host.scene.render_scene(resources->host.scene.ctx, refdef);
}
bool qa_q3_pres_q3_renderer_resources_load_world(resources_t *resources, const char *path,
    qa_q3_pres_world_scene *out) {
  if (!resources->host.world(resources->host.ctx, path, out)) return false;
  const char *entities = resources->has_world && resources->world.map != NULL
      ? resources->world.map->entities : "";
  qa_common_parse_cursor_destroy(&resources->entity_cursor);
  if (!qa_common_parse_cursor_init(&resources->entity_cursor, entities, strlen(entities), false)) return false;
  resources->world_loaded = true;
  return true;
}
bool qa_q3_pres_q3_renderer_resources_get_entity_token(resources_t *resources,
    void (*write)(void *ctx, const char *token), void *write_ctx) {
  const char *token = qa_common_parse_state_parse(&resources->entity_parser, &resources->entity_cursor, true);
  write(write_ctx, token);
  if (!resources->entity_cursor.has_offset || token[0] == '\0') {
    qa_common_parse_cursor_set_offset(&resources->entity_cursor, 0);
    return false;
  }
  return true;
}
static double point_cluster(resources_t *resources, qa_vec3 (*read)(void *ctx), void *read_ctx) {
  if (!resources->world_loaded || !resources->has_world) {
    qa_common_error(QA_COMMON_ERROR_DROP, "R_PointInLeaf: bad model");
  }
  const qa_q3_world_geometry *map = resources->world.map;
  double leaf = 0;
  if (map->node_count != 0) {
    qa_vec3 point = read(read_ctx);
    double index = 0;
    for (;;) {
      if (index != trunc(index) || index < 0 || index >= (double)map->node_count) {
        qa_range_error("R_PointInLeaf: invalid node");
      }
      const qa_bsp_node *node = &map->nodes[(size_t)index];
      if (node->plane != trunc(node->plane) || node->plane < 0 || node->plane >= (double)map->plane_count) {
        qa_range_error("R_PointInLeaf: invalid plane");
      }
      const qa_plane *plane = &map->planes[(size_t)node->plane].plane;
      qa_bsp_child child = node->children[qa_dot3(point, plane->normal) - plane->distance > 0 ? 0 : 1];
      if (!child.is_node) { leaf = child.index; break; }
      index = child.index;
    }
  }
  if (leaf != trunc(leaf) || leaf < 0 || leaf >= (double)map->leaf_count) {
    qa_range_error("R_PointInLeaf: invalid leaf");
  }
  return map->leaves[(size_t)leaf].cluster;
}
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double truncated = trunc(fmod(fabs(value), 4294967296.0));
  uint32_t bits = (uint32_t)truncated;
  if (value < 0) bits = ~bits + 1u;
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}
bool qa_q3_pres_q3_renderer_resources_in_pvs(resources_t *resources, qa_vec3 (*read_first)(void *ctx),
    qa_vec3 (*read_second)(void *ctx), void *read_ctx) {
  double first = point_cluster(resources, read_first, read_ctx);
  if (!resources->has_world) qa_common_error(QA_COMMON_ERROR_DROP, "R_PointInLeaf: bad model");
  qa_source_cluster_pvs visibility = resources->world.cluster_pvs(resources->world.ctx, first);
  double second = point_cluster(resources, read_second, read_ctx);
  int32_t cluster = to_int32(second);
  double byte = qa_source_cluster_pvs_byte_at(visibility, cluster >> 3);
  return (to_int32(byte) & (1 << (cluster & 7))) != 0;
}

bool qa_q3_pres_q3_renderer_resources_registered_models(resources_t *resources,
    qa_q3_pres_registered_model **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (resources->model_count <= 1) return true;
  qa_q3_pres_registered_model *rows = malloc((resources->model_count - 1) * sizeof *rows);
  if (rows == NULL) return false;
  size_t count = 0;
  for (size_t handle = 1; handle < resources->model_count; handle++) {
    const char *path = NULL;
    for (size_t i = 0; i < resources->model_name_count; i++) {
      if (resources->model_names[i].model_index == handle) { path = resources->model_names[i].path; break; }
    }
    if (path == NULL) {
      free(rows);
      qa_range_error("Registered cgame model has no source name");
    }
    rows[count].path = path;
    rows[count].handle = (double)handle;
    rows[count].model = resources->models[handle];
    count++;
  }
  *out = rows;
  *out_count = count;
  return true;
}
bool qa_q3_pres_q3_renderer_resources_registered_skins(resources_t *resources,
    qa_q3_pres_registered_skin **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  size_t total = 0;
  for (size_t handle = 0; handle < resources->skin_handle_count; handle++) {
    if (resources->skin_handles[handle] != NULL) total++;
  }
  if (total == 0) return true;
  qa_q3_pres_registered_skin *rows = malloc(total * sizeof *rows);
  if (rows == NULL) return false;
  size_t count = 0;
  for (size_t handle = 0; handle < resources->skin_handle_count; handle++) {
    const qa_q3_pres_scene_skin *skin = resources->skin_handles[handle];
    if (skin == NULL) continue;
    const char *path = NULL;
    for (size_t i = 0; i < resources->skin_count; i++) {
      if (resources->skins[i].skin == skin) { path = resources->skins[i].path; break; }
    }
    if (path == NULL) {
      free(rows);
      qa_range_error("Registered cgame skin has no source name");
    }
    rows[count].path = path;
    rows[count].handle = (double)handle;
    rows[count].skin = skin;
    count++;
  }
  *out = rows;
  *out_count = count;
  return true;
}

static qa_save_value *save_string_or_null(const char *text) {
  return text == NULL ? qa_save_value_null() : qa_save_value_string(text);
}
static bool save_set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) {
    qa_save_value_free(child);
    return false;
  }
  return true;
}
static bool save_push(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) {
    qa_save_value_free(child);
    return false;
  }
  return true;
}

qa_save_value *qa_q3_pres_q3_renderer_resources_capture_checkpoint(resources_t *resources) {
  if (resources->handles != QA_Q3_PRES_HANDLES_CLIENT || resources->operations != 0) {
    qa_range_error("Resource checkpoint requires an idle client handle owner");
  }
  qa_save_value *root = qa_save_value_object();
  if (root == NULL) return NULL;
  qa_save_value *models = qa_save_value_array();
  if (!save_set(root, "models", models)) goto oom;
  for (size_t i = 0; i < resources->model_name_count; i++) {
    const qa_q3_pres_scene_model *model = &resources->models[resources->model_names[i].model_index];
    qa_save_value *row = qa_save_value_object();
    if (row == NULL || !save_set(row, "path", qa_save_value_string(resources->model_names[i].path)) ||
        !save_set(row, "handle", qa_save_value_number(
            qa_q3_pres_q3_renderer_resources_model_handle(resources, model))) ||
        !save_set(row, "resource", save_string_or_null(model_resource_id(model))) ||
        !save_push(models, row)) {
      qa_save_value_free(row);
      goto oom;
    }
  }
  qa_save_value *skins = qa_save_value_array();
  if (!save_set(root, "skins", skins)) goto oom;
  for (size_t i = 0; i < resources->skin_count; i++) {
    const qa_q3_pres_scene_skin *skin = resources->skins[i].skin;
    qa_save_value *row = qa_save_value_object();
    if (row == NULL || !save_set(row, "path", qa_save_value_string(resources->skins[i].path)) ||
        !save_set(row, "handle", qa_save_value_number(
            qa_q3_pres_q3_renderer_resources_skin_handle(resources, skin)))) {
      qa_save_value_free(row);
      goto oom;
    }
    qa_save_value *surfaces = NULL;
    if (skin == NULL) {
      surfaces = qa_save_value_null();
    } else {
      surfaces = qa_save_value_array();
      if (surfaces != NULL) {
        for (size_t s = 0; s < skin->surface_count; s++) {
          qa_save_value *surface = qa_save_value_object();
          if (surface == NULL ||
              !save_set(surface, "name", qa_save_value_string(skin->surfaces[s].name)) ||
              !save_set(surface, "shader", qa_save_value_string(skin->surfaces[s].shader)) ||
              !save_push(surfaces, surface)) {
            qa_save_value_free(surface);
            qa_save_value_free(surfaces);
            surfaces = NULL;
            break;
          }
        }
      }
    }
    if (!save_set(row, "surfaces", surfaces) || !save_push(skins, row)) {
      qa_save_value_free(row);
      goto oom;
    }
  }
  qa_save_value *shaders = qa_save_value_array();
  if (!save_set(root, "shaders", shaders)) goto oom;
  for (size_t i = 0; i < resources->request_count; i++) {
    picture_entry *entry = picture_by_handle(resources, resources->requests[i].handle);
    qa_save_value *row = qa_save_value_object();
    if (row == NULL || !save_set(row, "path", qa_save_value_string(resources->requests[i].path)) ||
        !save_set(row, "mip", qa_save_value_boolean(resources->requests[i].mip)) ||
        !save_set(row, "handle", qa_save_value_number(resources->requests[i].handle)) ||
        !save_set(row, "name", save_string_or_null(entry == NULL ? NULL : entry->picture->name)) ||
        !save_push(shaders, row)) {
      qa_save_value_free(row);
      goto oom;
    }
  }
  qa_save_value *bindings = qa_save_value_array();
  if (!save_set(root, "bindings", bindings)) goto oom;
  for (size_t i = 0; i < resources->picture_key_count; i++) {
    qa_save_value *row = qa_save_value_object();
    if (row == NULL || !save_set(row, "name", qa_save_value_string(resources->picture_keys[i].key)) ||
        !save_set(row, "handle", qa_save_value_number(resources->picture_keys[i].entry->handle)) ||
        !save_push(bindings, row)) {
      qa_save_value_free(row);
      goto oom;
    }
  }
  if (!save_set(root, "worldLoaded", qa_save_value_boolean(resources->world_loaded)) ||
      !save_set(root, "entitySource", qa_save_value_string(resources->entity_cursor.source)) ||
      !save_set(root, "entityOffset", resources->entity_cursor.has_offset
          ? qa_save_value_number((double)resources->entity_cursor.offset) : qa_save_value_null()) ||
      !save_set(root, "entityParser", qa_common_parse_state_capture(&resources->entity_parser))) {
    goto oom;
  }
  return root;
oom:
  qa_save_value_free(root);
  return NULL;
}

static bool reader_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  return qa_save_reader_field(reader, name, out);
}
bool qa_q3_pres_q3_renderer_resources_restore_checkpoint(resources_t *resources, const qa_save_value *value) {
  if (resources->handles != QA_Q3_PRES_HANDLES_CLIENT || resources->operations != 0 ||
      resources->model_name_count != 0 || resources->skin_count != 0 || resources->picture_key_count != 0 ||
      resources->world_loaded) {
    qa_range_error("Resource restore requires an empty client handle owner");
  }
  qa_save_reader root;
  if (!qa_save_reader_init(&root, value, "client-resources")) return false;
  bool ok = false;
  qa_save_reader models, skins, shaders, bindings, field;
  memset(&models, 0, sizeof models);
  memset(&skins, 0, sizeof skins);
  memset(&shaders, 0, sizeof shaders);
  memset(&bindings, 0, sizeof bindings);
  memset(&field, 0, sizeof field);
  if (!reader_field(&root, "models", &models)) goto done;
  for (size_t i = 0; i < qa_save_reader_list_count(&models); i++) {
    qa_save_reader item, path_r, handle_r, resource_r;
    if (!qa_save_reader_item(&models, i, &item)) goto done;
    bool row_ok = reader_field(&item, "path", &path_r) && reader_field(&item, "handle", &handle_r) &&
                  reader_field(&item, "resource", &resource_r);
    const char *path = row_ok ? qa_save_reader_string(&path_r) : NULL;
    double handle = row_ok ? qa_save_reader_integer_min(&handle_r, 0) : 0;
    bool has_resource = row_ok && !qa_save_reader_is_null(&resource_r);
    const char *resource = has_resource ? qa_save_reader_string(&resource_r) : NULL;
    qa_q3_pres_scene_model model;
    bool registered = row_ok && qa_q3_pres_q3_renderer_resources_register_model(resources, path, &model);
    if (registered) {
      if (qa_q3_pres_q3_renderer_resources_model_handle(resources, &model) != handle ||
          !string_equal(model_resource_id(&model), resource)) {
        qa_save_reader_destroy(&resource_r);
        qa_save_reader_destroy(&handle_r);
        qa_save_reader_destroy(&path_r);
        qa_save_reader_destroy(&item);
        qa_save_reader_fail(&root, "model binding changed");
      }
    }
    qa_save_reader_destroy(&resource_r);
    qa_save_reader_destroy(&handle_r);
    qa_save_reader_destroy(&path_r);
    qa_save_reader_destroy(&item);
    if (!registered) goto done;
  }
  if (!reader_field(&root, "skins", &skins)) goto done;
  for (size_t i = 0; i < qa_save_reader_list_count(&skins); i++) {
    qa_save_reader item, path_r, handle_r, surfaces_r;
    if (!qa_save_reader_item(&skins, i, &item)) goto done;
    bool row_ok = reader_field(&item, "path", &path_r) && reader_field(&item, "handle", &handle_r) &&
                  reader_field(&item, "surfaces", &surfaces_r);
    const char *path = row_ok ? qa_save_reader_string(&path_r) : NULL;
    double handle = row_ok ? qa_save_reader_integer_min(&handle_r, 0) : 0;
    const qa_q3_pres_scene_skin *skin = NULL;
    bool registered = row_ok && qa_q3_pres_q3_renderer_resources_register_skin(resources, path, &skin);
    bool surfaces_match = false;
    if (registered) {
      if (qa_save_reader_is_null(&surfaces_r)) {
        surfaces_match = skin == NULL;
      } else if (skin != NULL && qa_save_reader_list_count(&surfaces_r) == skin->surface_count) {
        surfaces_match = true;
        for (size_t s = 0; s < skin->surface_count && surfaces_match; s++) {
          qa_save_reader surface, name_r, shader_r;
          if (!qa_save_reader_item(&surfaces_r, s, &surface)) { surfaces_match = false; break; }
          bool fields_ok = reader_field(&surface, "name", &name_r) && reader_field(&surface, "shader", &shader_r);
          surfaces_match = fields_ok && string_equal(qa_save_reader_string(&name_r), skin->surfaces[s].name) &&
                           string_equal(qa_save_reader_string(&shader_r), skin->surfaces[s].shader);
          qa_save_reader_destroy(&shader_r);
          qa_save_reader_destroy(&name_r);
          qa_save_reader_destroy(&surface);
        }
      }
    }
    if (registered && (qa_q3_pres_q3_renderer_resources_skin_handle(resources, skin) != handle ||
                       !surfaces_match)) {
      qa_save_reader_destroy(&surfaces_r);
      qa_save_reader_destroy(&handle_r);
      qa_save_reader_destroy(&path_r);
      qa_save_reader_destroy(&item);
      qa_save_reader_fail(&root, "skin binding changed");
    }
    qa_save_reader_destroy(&surfaces_r);
    qa_save_reader_destroy(&handle_r);
    qa_save_reader_destroy(&path_r);
    qa_save_reader_destroy(&item);
    if (!registered) goto done;
  }
  if (!reader_field(&root, "shaders", &shaders)) goto done;
  for (size_t i = 0; i < qa_save_reader_list_count(&shaders); i++) {
    qa_save_reader item, path_r, mip_r, handle_r, name_r;
    if (!qa_save_reader_item(&shaders, i, &item)) goto done;
    bool row_ok = reader_field(&item, "path", &path_r) && reader_field(&item, "mip", &mip_r) &&
                  reader_field(&item, "handle", &handle_r) && reader_field(&item, "name", &name_r);
    const char *path = row_ok ? qa_save_reader_string(&path_r) : NULL;
    bool mip = row_ok && qa_save_reader_boolean(&mip_r);
    double handle = row_ok ? qa_save_reader_integer_min(&handle_r, 1) : 0;
    const char *name = row_ok ? qa_save_reader_string(&name_r) : NULL;
    const qa_q3_pres_scene_shader *shader = NULL;
    bool registered = row_ok && register_shader_impl(resources, path, mip, &shader);
    if (registered) {
      double actual = qa_q3_pres_q3_renderer_resources_shader_handle(resources, shader);
      const char *actual_name = shader == NULL ? NULL : shader->name;
      if (actual != handle || !string_equal(actual_name, name)) {
        qa_save_reader_destroy(&name_r);
        qa_save_reader_destroy(&handle_r);
        qa_save_reader_destroy(&mip_r);
        qa_save_reader_destroy(&path_r);
        qa_save_reader_destroy(&item);
        qa_save_reader_fail(&root, "shader binding changed");
      }
    }
    qa_save_reader_destroy(&name_r);
    qa_save_reader_destroy(&handle_r);
    qa_save_reader_destroy(&mip_r);
    qa_save_reader_destroy(&path_r);
    qa_save_reader_destroy(&item);
    if (!registered) goto done;
  }
  if (!reader_field(&root, "bindings", &bindings)) goto done;
  {
    bool aliases_ok = qa_save_reader_list_count(&bindings) == resources->picture_key_count;
    for (size_t i = 0; aliases_ok && i < qa_save_reader_list_count(&bindings); i++) {
      qa_save_reader item, name_r, handle_r;
      if (!qa_save_reader_item(&bindings, i, &item)) { aliases_ok = false; break; }
      bool fields_ok = reader_field(&item, "name", &name_r) && reader_field(&item, "handle", &handle_r);
      const char *name = fields_ok ? qa_save_reader_string(&name_r) : NULL;
      double handle = fields_ok ? qa_save_reader_integer_min(&handle_r, 1) : 0;
      aliases_ok = fields_ok && picture_for_key(resources, name) != NULL &&
                   picture_for_key(resources, name) == picture_by_handle(resources, handle);
      qa_save_reader_destroy(&handle_r);
      qa_save_reader_destroy(&name_r);
      qa_save_reader_destroy(&item);
    }
    if (!aliases_ok) qa_save_reader_fail(&root, "shader aliases changed");
  }
  if (!reader_field(&root, "worldLoaded", &field)) goto done;
  resources->world_loaded = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  memset(&field, 0, sizeof field);
  if (!reader_field(&root, "entitySource", &field)) goto done;
  {
    const char *source = qa_save_reader_string(&field);
    qa_common_parse_cursor_destroy(&resources->entity_cursor);
    if (!qa_common_parse_cursor_init(&resources->entity_cursor, source, strlen(source), false)) {
      qa_save_reader_destroy(&field);
      goto done;
    }
  }
  qa_save_reader_destroy(&field);
  memset(&field, 0, sizeof field);
  if (!reader_field(&root, "entityOffset", &field)) goto done;
  if (qa_save_reader_is_null(&field)) {
    qa_common_parse_cursor_set_null(&resources->entity_cursor);
  } else {
    qa_common_parse_cursor_set_offset(&resources->entity_cursor,
        (size_t)qa_save_reader_integer_min(&field, 0));
  }
  qa_save_reader_destroy(&field);
  memset(&field, 0, sizeof field);
  if (!reader_field(&root, "entityParser", &field)) goto done;
  qa_common_parse_state_restore(&resources->entity_parser, field.value);
  ok = true;
done:
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&bindings);
  qa_save_reader_destroy(&shaders);
  qa_save_reader_destroy(&skins);
  qa_save_reader_destroy(&models);
  qa_save_reader_destroy(&root);
  return ok;
}

static bool iface_register_model(void *ctx, const char *path, qa_q3_pres_scene_model *out) {
  return qa_q3_pres_q3_renderer_resources_register_model(ctx, path, out);
}
static bool iface_register_skin(void *ctx, const char *path, const qa_q3_pres_scene_skin **out) {
  return qa_q3_pres_q3_renderer_resources_register_skin(ctx, path, out);
}
static bool iface_register_shader(void *ctx, const char *path, const qa_q3_pres_scene_shader **out) {
  return qa_q3_pres_q3_renderer_resources_register_shader(ctx, path, out);
}
static bool iface_register_shader_no_mip(void *ctx, const char *path, const qa_q3_pres_scene_shader **out) {
  return qa_q3_pres_q3_renderer_resources_register_shader_no_mip(ctx, path, out);
}
static qa_material_picture iface_picture(void *ctx, const qa_q3_pres_scene_shader *shader) {
  return qa_q3_pres_q3_renderer_resources_picture(ctx, shader);
}
static double iface_model_handle(void *ctx, const qa_q3_pres_scene_model *model) {
  return qa_q3_pres_q3_renderer_resources_model_handle(ctx, model);
}
static qa_q3_pres_scene_model iface_model_for_handle(void *ctx, double handle) {
  return qa_q3_pres_q3_renderer_resources_model_for_handle(ctx, handle);
}
static const qa_q3_pres_scene_shader *iface_shader_for_handle(void *ctx, double handle) {
  return qa_q3_pres_q3_renderer_resources_shader_for_handle(ctx, handle);
}
static void iface_clear_scene(void *ctx) { qa_q3_pres_q3_renderer_resources_clear_scene(ctx); }
static void iface_add_ref_entity(void *ctx, const qa_q3_pres_admitted_ref_entity *entity) {
  qa_q3_pres_q3_renderer_resources_add_ref_entity(ctx, entity);
}
static void iface_add_poly(void *ctx, const qa_q3_pres_ref_poly *poly) {
  qa_q3_pres_q3_renderer_resources_add_poly(ctx, poly);
}
static void iface_add_light(void *ctx, qa_q3_pres_dynamic_light light) {
  qa_q3_pres_q3_renderer_resources_add_light(ctx, light);
}
static void iface_remap_shader(void *ctx, const char *original, const char *replacement, const char *offset) {
  qa_q3_pres_q3_renderer_resources_remap_shader(ctx, original, replacement, offset);
}
static void iface_render_scene(void *ctx, const qa_q3_pres_refdef *refdef) {
  qa_q3_pres_q3_renderer_resources_render_scene(ctx, refdef);
}
static bool iface_load_world(void *ctx, const char *path, qa_q3_pres_world_scene *out) {
  return qa_q3_pres_q3_renderer_resources_load_world(ctx, path, out);
}
qa_q3_pres_renderer_resources qa_q3_pres_q3_renderer_resources_as_resources(resources_t *resources) {
  qa_q3_pres_renderer_resources result;
  memset(&result, 0, sizeof result);
  result.ctx = resources;
  result.register_model = iface_register_model;
  result.register_skin = iface_register_skin;
  result.register_shader = iface_register_shader;
  result.register_shader_no_mip = iface_register_shader_no_mip;
  result.picture = iface_picture;
  result.model_handle = iface_model_handle;
  result.model_for_handle = iface_model_for_handle;
  result.shader_for_handle = iface_shader_for_handle;
  result.clear_scene = iface_clear_scene;
  result.add_ref_entity = iface_add_ref_entity;
  result.add_poly = iface_add_poly;
  result.add_light = iface_add_light;
  result.remap_shader = iface_remap_shader;
  result.render_scene = iface_render_scene;
  result.load_world = iface_load_world;
  return result;
}
