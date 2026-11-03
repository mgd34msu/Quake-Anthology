/* Port of src/formats/q2-map/index.ts. GPL-2.0-or-later. */
#include "qa/formats/q2_map.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js_case.h"

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

static void list_push(qa_q2_string_list *list, char *value) {
  list->values = realloc(list->values, (list->count + 1) * sizeof *list->values);
  if (list->values == NULL) qa_range_error("Out of memory");
  list->values[list->count++] = value;
}

static bool valid_material(const char *material) {
  if (material[0] == '\0') return false;
  for (const char *p = material; *p != '\0'; p++) {
    if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return false;
  }
  return true;
}

typedef struct {
  char *key;
  char *material;
} material_entry;

static qa_q2_texture_info *convert_textures(qa_q2_bsp *map, const qa_q2_map_resources *resources,
                                            qa_q2_string_list *diagnostics) {
  qa_q2_texture_info *result = xmalloc(map->texture_info_count * sizeof *result);
  material_entry *cache = NULL;
  size_t cache_count = 0;
  for (size_t i = 0; i < map->texture_info_count; i++) {
    const qa_q2_raw_texture_info *texture = &map->texture_info[i];
    char name[32];
    size_t name_length = strlen(texture->name);
    if (name_length > 31) name_length = 31;
    memcpy(name, texture->name, name_length);
    name[name_length] = '\0';
    char key[32];
    /* TS name.toLowerCase() over a fixedByteString (Latin-1) texture name. */
    for (size_t j = 0; j <= name_length; j++) key[j] = (char)qa_js_latin1_lower((unsigned char)name[j]);
    const char *material = NULL;
    for (size_t j = 0; j < cache_count; j++) {
      if (strcmp(cache[j].key, key) == 0) { material = cache[j].material; break; }
    }
    if (material == NULL) {
      char path[64];
      snprintf(path, sizeof path, "textures/%s.mat", name);
      char *resolved;
      size_t byte_length = 0;
      uint8_t *bytes = resources != NULL && resources->read_material != NULL
                           ? resources->read_material(resources->ctx, path, &byte_length)
                           : NULL;
      if (bytes == NULL) {
        resolved = xstrdup("");
      } else {
        qa_binary_reader reader;
        if (!qa_binary_reader_init(&reader, bytes, byte_length, path)) qa_range_error("Out of memory");
        resolved = qa_binary_reader_fixed_byte_string(&reader,
                                                      (int64_t)(byte_length < 15 ? byte_length : 15));
        qa_binary_reader_destroy(&reader);
        free(bytes);
        if (resolved == NULL) resolved = xstrdup("");
      }
      if (resolved[0] != '\0' && !valid_material(resolved)) {
        char message[256];
        snprintf(message, sizeof message, "Invalid material in %s", path);
        list_push(diagnostics, xstrdup(message));
        free(resolved);
        resolved = xstrdup("");
      }
      cache = realloc(cache, (cache_count + 1) * sizeof *cache);
      if (cache == NULL) qa_range_error("Out of memory");
      cache[cache_count].key = xstrdup(key);
      cache[cache_count].material = resolved;
      material = resolved;
      cache_count++;
    }
    result[i].surface.name = texture->name;
    result[i].surface.flags = texture->flags;
    result[i].surface.value = texture->value;
    result[i].surface.material = material;
    result[i].projection = texture->projection;
    result[i].has_next = texture->next > 0;
    result[i].next = texture->next > 0 ? texture->next : 0;
  }
  /* The cache strings back surface.material; keep only the keys freed. */
  for (size_t j = 0; j < cache_count; j++) free(cache[j].key);
  free(cache);
  return result;
}

/* TS toQ2WorldGeometry reads each BSPX extension in a try that turns a
 * BinaryError into a diagnostic and rethrows anything else. */
typedef struct bspx_job {
  const qa_q2_bspx_lump *lump;
  double face_count, lighting_bytes, corners;
  qa_q2_decoupled_lightmaps decoupled;
  qa_q2_lightgrid *lightgrid;
  qa_q2_face_normals *face_normals;
} bspx_job;

static void read_decoupled_run(void *raw) {
  bspx_job *job = raw;
  qa_read_q2_decoupled_lightmaps(job->lump, job->face_count, job->lighting_bytes, &job->decoupled);
}

static void read_lightgrid_run(void *raw) {
  bspx_job *job = raw;
  qa_read_q2_lightgrid(job->lump, job->lightgrid);
}

static void read_face_normals_run(void *raw) {
  bspx_job *job = raw;
  qa_read_q2_face_normals(job->lump, job->corners, job->face_normals);
}

static void caught_diagnostic(const qa_caught_error *caught, qa_q2_string_list *diagnostics) {
  if (strcmp(caught->name, "BinaryError") != 0) qa_rethrow(caught);
  list_push(diagnostics, xstrdup(caught->message));
}

void qa_to_q2_world_geometry(qa_q2_bsp *map, const qa_q2_map_resources *resources,
                             qa_q2_decoded_map *out) {
  memset(out, 0, sizeof *out);
  out->source = map;
  for (size_t i = 0; i < map->diagnostics.count; i++)
    list_push(&out->diagnostics, xstrdup(map->diagnostics.values[i]));
  if (map->has_bspx) {
    for (size_t i = 0; i < map->bspx.diagnostics.count; i++)
      list_push(&out->diagnostics, xstrdup(map->bspx.diagnostics.values[i]));
  }
  qa_decoupled_lightmap *decoupled = NULL;
  size_t decoupled_count = 0;
  qa_q2_lightgrid *lightgrid = NULL;
  qa_q2_face_normals *face_normals = NULL;
  const qa_q2_bspx_directory *bspx = map->has_bspx ? &map->bspx : NULL;
  const qa_q2_bspx_lump *lump = bspx != NULL ? qa_q2_bspx_find(bspx, "DECOUPLED_LM") : NULL;
  qa_caught_error caught;
  if (lump != NULL) {
    bspx_job job;
    memset(&job, 0, sizeof job);
    job.lump = lump;
    job.face_count = (double)map->face_count;
    job.lighting_bytes = (double)map->lighting_length;
    if (qa_try(read_decoupled_run, &job, &caught)) {
      decoupled = job.decoupled.faces;
      decoupled_count = job.decoupled.face_count;
      for (size_t i = 0; i < job.decoupled.diagnostics.count; i++)
        list_push(&out->diagnostics, job.decoupled.diagnostics.values[i]);
      free(job.decoupled.diagnostics.values);
    } else {
      qa_q2_decoupled_lightmaps_destroy(&job.decoupled);
      caught_diagnostic(&caught, &out->diagnostics);
    }
  }
  lump = bspx != NULL ? qa_q2_bspx_find(bspx, "LIGHTGRID_OCTREE") : NULL;
  if (lump != NULL) {
    bspx_job job;
    memset(&job, 0, sizeof job);
    job.lump = lump;
    lightgrid = xmalloc(sizeof *lightgrid);
    memset(lightgrid, 0, sizeof *lightgrid);
    job.lightgrid = lightgrid;
    if (!qa_try(read_lightgrid_run, &job, &caught)) {
      qa_q2_lightgrid_destroy(lightgrid);
      free(lightgrid);
      lightgrid = NULL;
      caught_diagnostic(&caught, &out->diagnostics);
    }
  }
  lump = bspx != NULL ? qa_q2_bspx_find(bspx, "FACENORMALS") : NULL;
  if (lump != NULL) {
    bspx_job job;
    memset(&job, 0, sizeof job);
    job.lump = lump;
    for (size_t i = 0; i < map->face_count; i++) job.corners += map->faces[i].edges.count;
    face_normals = xmalloc(sizeof *face_normals);
    memset(face_normals, 0, sizeof *face_normals);
    job.face_normals = face_normals;
    if (!qa_try(read_face_normals_run, &job, &caught)) {
      qa_q2_face_normals_destroy(face_normals);
      free(face_normals);
      face_normals = NULL;
      caught_diagnostic(&caught, &out->diagnostics);
    }
  }
  qa_q2_leaf *leaves = xmalloc(map->leaf_count * sizeof *leaves);
  for (size_t i = 0; i < map->leaf_count; i++) {
    const qa_q2_raw_leaf *leaf = &map->leaves[i];
    int32_t merged = (int32_t)leaf->contents;
    if (i != 0) {
      double end = leaf->brushes.first + leaf->brushes.count;
      for (double offset = leaf->brushes.first; offset < end; offset++) {
        if (offset < 0 || offset >= (double)map->leaf_brush_count)
          qa_binary_error(map->source, 0, "invalid leaf brush reference");
        double brush_index = map->leaf_brushes[(size_t)offset];
        if (brush_index < 0 || brush_index >= (double)map->brush_count)
          qa_binary_error(map->source, 0, "invalid leaf brush reference");
        merged |= (int32_t)map->brushes[(size_t)brush_index].contents;
      }
    }
    leaves[i].contents = leaf->contents;
    leaves[i].merged_contents = merged;
    leaves[i].cluster = leaf->cluster != -1 && map->visibility == NULL ? 0 : leaf->cluster;
    leaves[i].area = leaf->area;
    leaves[i].bounds = leaf->bounds;
    leaves[i].faces = leaf->faces;
    leaves[i].brushes = leaf->brushes;
  }
  qa_bsp_face *faces = xmalloc(map->face_count * sizeof *faces);
  for (size_t i = 0; i < map->face_count; i++) {
    const qa_q2_raw_face *face = &map->faces[i];
    faces[i].plane = face->plane;
    faces[i].back = ((int32_t)face->draw_flags & 1) != 0;
    faces[i].edges = face->edges;
    faces[i].texture_info = face->texture_info;
    double *styles = xmalloc(4 * sizeof *styles);
    for (int j = 0; j < 4; j++) styles[j] = face->styles[j];
    faces[i].styles.values = styles;
    faces[i].styles.count = 4;
    if (decoupled != NULL && i < decoupled_count) {
      faces[i].has_lighting_offset = decoupled[i].has_lighting_offset;
      faces[i].lighting_offset = decoupled[i].lighting_offset;
    } else {
      faces[i].has_lighting_offset = !(face->lighting_offset == -1 || map->lighting_length == 0);
      faces[i].lighting_offset = face->lighting_offset;
    }
  }
  qa_q2_texture_info *texture_info = convert_textures(map, resources, &out->diagnostics);
  qa_q2_world_model *models = xmalloc(map->model_count * sizeof *models);
  for (size_t i = 0; i < map->model_count; i++) {
    models[i] = map->models[i];
    models[i].bounds.min.x -= 1;
    models[i].bounds.min.y -= 1;
    models[i].bounds.min.z -= 1;
    models[i].bounds.max.x += 1;
    models[i].bounds.max.y += 1;
    models[i].bounds.max.z += 1;
  }
  qa_bsp_extension *extensions = NULL;
  size_t extension_count = 0;
  if (bspx != NULL) {
    extension_count = bspx->lump_count;
    extensions = xmalloc(extension_count * sizeof *extensions);
    for (size_t i = 0; i < extension_count; i++) {
      extensions[i].name = bspx->lumps[i].name;
      extensions[i].bytes.bytes = bspx->lumps[i].bytes;
      extensions[i].bytes.length = bspx->lumps[i].length;
    }
  }
  uint8_t *lighting = xmalloc(map->lighting_length);
  if (map->lighting_length > 0) memcpy(lighting, map->lighting, map->lighting_length);
  qa_q2_world_geometry *world = &out->world;
  memset(world, 0, sizeof *world);
  world->format = map->format;
  world->entities = map->entities;
  world->planes = map->planes;
  world->plane_count = map->plane_count;
  world->vertices = map->vertices;
  world->vertex_count = map->vertex_count;
  world->edges = map->edges;
  world->edge_count = map->edge_count;
  world->surface_edges.values = map->surface_edges;
  world->surface_edges.count = map->surface_edge_count;
  world->nodes = map->nodes;
  world->node_count = map->node_count;
  world->leaves = leaves;
  world->leaf_count = map->leaf_count;
  world->leaf_faces.values = map->leaf_faces;
  world->leaf_faces.count = map->leaf_face_count;
  world->leaf_brushes.values = map->leaf_brushes;
  world->leaf_brushes.count = map->leaf_brush_count;
  world->texture_info = texture_info;
  world->texture_info_count = map->texture_info_count;
  world->faces = faces;
  world->face_count = map->face_count;
  world->brushes = map->brushes;
  world->brush_count = map->brush_count;
  world->brush_sides = map->brush_sides;
  world->brush_side_count = map->brush_side_count;
  world->models = models;
  world->model_count = map->model_count;
  world->areas = map->areas;
  world->area_count = map->area_count;
  world->area_portals = map->area_portals;
  world->area_portal_count = map->area_portal_count;
  world->visibility = map->visibility;
  world->lighting.is_rgb8 = true;
  world->lighting.source = "bsp";
  world->lighting.samples.bytes = lighting;
  world->lighting.samples.length = map->lighting_length;
  world->lightgrid = lightgrid;
  world->decoupled_lightmaps = decoupled;
  world->decoupled_lightmap_count = decoupled_count;
  world->extensions = extensions;
  world->extension_count = extension_count;
  out->face_normals = face_normals;
  out->owned_leaves = leaves;
  out->owned_faces = faces;
  out->owned_texture_info = texture_info;
  out->owned_models = models;
  out->owned_lightgrid = lightgrid;
  out->owned_decoupled = decoupled;
  out->owned_decoupled_count = decoupled_count;
  out->owned_extensions = extensions;
  out->owned_lighting = lighting;
}

void qa_decode_q2_map(const uint8_t *bytes, size_t length, const char *source,
                      const qa_q2_map_resources *resources, qa_q2_decoded_map *out) {
  qa_q2_bsp *map = xmalloc(sizeof *map);
  qa_read_q2_bsp(bytes, length, source, map);
  qa_to_q2_world_geometry(map, resources, out);
}

void qa_q2_decoded_map_destroy(qa_q2_decoded_map *map) {
  if (map == NULL) return;
  free(map->owned_leaves);
  if (map->owned_faces != NULL) {
    for (size_t i = 0; i < map->world.face_count; i++) free((void *)map->owned_faces[i].styles.values);
    free(map->owned_faces);
  }
  if (map->owned_texture_info != NULL) {
    /* Materials are shared cache strings; free each distinct pointer once. */
    for (size_t i = 0; i < map->world.texture_info_count; i++) {
      const char *material = map->owned_texture_info[i].surface.material;
      bool seen = false;
      for (size_t j = 0; j < i; j++) {
        if (map->owned_texture_info[j].surface.material == material) seen = true;
      }
      if (!seen) free((void *)material);
    }
    free(map->owned_texture_info);
  }
  free(map->owned_models);
  if (map->owned_lightgrid != NULL) {
    qa_q2_lightgrid_destroy(map->owned_lightgrid);
    free(map->owned_lightgrid);
  }
  free(map->owned_decoupled);
  free(map->owned_extensions);
  free(map->owned_lighting);
  if (map->face_normals != NULL) {
    qa_q2_face_normals_destroy(map->face_normals);
    free(map->face_normals);
  }
  qa_q2_string_list_destroy(&map->diagnostics);
  if (map->source != NULL) {
    qa_q2_bsp_destroy(map->source);
    free(map->source);
  }
  memset(map, 0, sizeof *map);
}
