/* Port of src/formats/q3-map/decode.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_map/decode.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/formats/q3_map/ibsp44.h"
#include "qa/foundation/support.h"

#define HEADER_SIZE (8 + 17 * 8)
/* Original MAX_MAP_AREAS is 256; collision allocates areas squared. */
#define MAX_MAP_AREAS 1024

/* Zeroed, so a caught raise can release a partly read map. */
static void *xmalloc(size_t size) {
  void *p = calloc(size == 0 ? 1 : size, 1);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

typedef struct {
  int32_t offset;
  int32_t length;
} lump;

typedef struct {
  const char *source;
  qa_binary_reader reader;
  lump lumps[17];
  qa_q3_bsp_map *map;
  /* Owned scratch, released with the map when a caller catches a raise. */
  uint8_t *normalized;
  uint8_t *visited;
  void *stack;
  uint8_t *lightmap_use;
} parser;

static void parser_release(void *raw) {
  parser *p = raw;
  qa_binary_reader_destroy(&p->reader);
  free(p->normalized);
  free(p->visited);
  free(p->stack);
  free(p->lightmap_use);
  p->normalized = p->visited = p->lightmap_use = NULL;
  p->stack = NULL;
  qa_q3_bsp_map_destroy(p->map);
}

static qa_vec2 read_vec2(qa_binary_reader *reader) {
  qa_vec2 v;
  v.x = qa_binary_reader_finite_f32(reader);
  v.y = qa_binary_reader_finite_f32(reader);
  return v;
}

static qa_vec3 read_vec3(qa_binary_reader *reader) {
  qa_vec3 v;
  v.x = qa_binary_reader_finite_f32(reader);
  v.y = qa_binary_reader_finite_f32(reader);
  v.z = qa_binary_reader_finite_f32(reader);
  return v;
}

static qa_vec3 int_vec3(qa_binary_reader *reader) {
  qa_vec3 v;
  v.x = qa_binary_reader_i32(reader);
  v.y = qa_binary_reader_i32(reader);
  v.z = qa_binary_reader_i32(reader);
  return v;
}

static qa_vec3 byte_vec3(qa_binary_reader *reader) {
  qa_vec3 v;
  v.x = qa_binary_reader_u8(reader);
  v.y = qa_binary_reader_u8(reader);
  v.z = qa_binary_reader_u8(reader);
  return v;
}

static size_t begin_records(parser *p, int index, size_t stride) {
  lump section = p->lumps[index];
  if (section.length < 0 || (size_t)section.length % stride != 0) {
    qa_binary_error(p->source, section.offset, "lump %d length %d is not a multiple of %zu", index,
                    (int)section.length, stride);
  }
  qa_binary_reader_seek(&p->reader, section.offset);
  return (size_t)section.length / stride;
}

static void fail_at(parser *p, int64_t offset, const char *message) {
  qa_binary_error(p->source, offset, "%s", message);
}

static void reference(parser *p, double value, double count, int64_t offset, const char *name) {
  if (value < 0 || value >= count) {
    char message[256];
    snprintf(message, sizeof message, "%s index %s outside 0..%s", name, QA_JS_NUMBER(value), QA_JS_NUMBER(count - 1));
    fail_at(p, offset, message);
  }
}

static void check_range(parser *p, double first, double count, double total, int64_t offset,
                        const char *name) {
  if (first < 0 || count < 0 || first > total - count) {
    char message[256];
    snprintf(message, sizeof message, "%s range %s+%s exceeds %s", name, QA_JS_NUMBER(first), QA_JS_NUMBER(count), QA_JS_NUMBER(total));
    fail_at(p, offset, message);
  }
}

static void read_surface(parser *p, qa_q3_raw_surface *out) {
  qa_binary_reader *reader = &p->reader;
  out->shader = qa_binary_reader_i32(reader);
  out->fog = qa_binary_reader_i32(reader);
  size_t type_offset = reader->cursor;
  int32_t type = qa_binary_reader_i32(reader);
  out->first_vertex = qa_binary_reader_i32(reader);
  out->vertex_count = qa_binary_reader_i32(reader);
  out->first_index = qa_binary_reader_i32(reader);
  out->index_count = qa_binary_reader_i32(reader);
  out->lightmap = qa_binary_reader_i32(reader);
  out->lightmap_x = qa_binary_reader_i32(reader);
  out->lightmap_y = qa_binary_reader_i32(reader);
  out->lightmap_width = qa_binary_reader_i32(reader);
  out->lightmap_height = qa_binary_reader_i32(reader);
  out->lightmap_origin = read_vec3(reader);
  for (int i = 0; i < 3; i++) out->lightmap_vectors[i] = read_vec3(reader);
  out->patch_width = qa_binary_reader_i32(reader);
  out->patch_height = qa_binary_reader_i32(reader);
  switch (type) {
    case 1: out->type = "planar"; break;
    case 2: out->type = "patch"; break;
    case 3: out->type = "triangles"; break;
    case 4: out->type = "flare"; break;
    default: {
      char message[128];
      snprintf(message, sizeof message, "unknown BSP surface type %d", (int)type);
      fail_at(p, (int64_t)type_offset, message);
    }
  }
}

void qa_q3_parse_bsp(const uint8_t *data, size_t data_length, const char *source, qa_q3_bsp_map *out) {
  memset(out, 0, sizeof *out);
  const char *name = source != NULL ? source : "<bsp>";
  size_t normalized_length = 0;
  uint8_t *normalized = qa_q3_normalize_bsp(data, data_length, name, &normalized_length);
  parser p;
  memset(&p, 0, sizeof p);
  p.source = name;
  p.map = out;
  p.normalized = normalized;
  if (!qa_binary_reader_init(&p.reader, normalized, normalized_length, name))
    qa_range_error("Out of memory");
  qa_cleanup parser_node;
  qa_cleanup_push(&parser_node, parser_release, &p);
  qa_binary_reader *reader = &p.reader;
  if (qa_binary_reader_u32(reader) != 0x50534249u) qa_binary_error(name, 0, "expected IBSP magic");
  if (qa_binary_reader_i32(reader) != 46) qa_binary_error(name, 4, "expected BSP version 46");
  for (int index = 0; index < 17; index++) {
    int32_t offset = qa_binary_reader_i32(reader);
    int32_t length = qa_binary_reader_i32(reader);
    if (offset < 0 || length < 0 || offset > (int64_t)normalized_length - length ||
        (length > 0 && offset < HEADER_SIZE)) {
      qa_binary_error(name, 8 + index * 8, "invalid lump %d range %d+%d", index, (int)offset, (int)length);
    }
    p.lumps[index].offset = offset;
    p.lumps[index].length = length;
  }
  size_t count;
  qa_binary_reader_seek(reader, p.lumps[0].offset);
  out->entities = qa_binary_reader_fixed_byte_string(reader, p.lumps[0].length);
  count = begin_records(&p, 1, 72);
  out->shaders = xmalloc(count * sizeof *out->shaders);
  out->shader_count = count;
  for (size_t i = 0; i < count; i++) {
    out->shaders[i].name = qa_binary_reader_fixed_byte_string(reader, 64);
    out->shaders[i].surface_flags = qa_binary_reader_i32(reader);
    out->shaders[i].content_flags = qa_binary_reader_i32(reader);
  }
  count = begin_records(&p, 2, 16);
  out->planes = xmalloc(count * sizeof *out->planes);
  out->plane_count = count;
  for (size_t i = 0; i < count; i++) {
    out->planes[i].normal = read_vec3(reader);
    out->planes[i].distance = qa_binary_reader_finite_f32(reader);
  }
  count = begin_records(&p, 3, 36);
  out->nodes = xmalloc(count * sizeof *out->nodes);
  out->node_count = count;
  for (size_t i = 0; i < count; i++) {
    out->nodes[i].plane = qa_binary_reader_i32(reader);
    out->nodes[i].children[0] = qa_binary_reader_i32(reader);
    out->nodes[i].children[1] = qa_binary_reader_i32(reader);
    out->nodes[i].bounds.min = int_vec3(reader);
    out->nodes[i].bounds.max = int_vec3(reader);
  }
  count = begin_records(&p, 4, 48);
  out->leaves = xmalloc(count * sizeof *out->leaves);
  out->leaf_count = count;
  for (size_t i = 0; i < count; i++) {
    out->leaves[i].cluster = qa_binary_reader_i32(reader);
    out->leaves[i].area = qa_binary_reader_i32(reader);
    out->leaves[i].bounds.min = int_vec3(reader);
    out->leaves[i].bounds.max = int_vec3(reader);
    out->leaves[i].first_surface = qa_binary_reader_i32(reader);
    out->leaves[i].surface_count = qa_binary_reader_i32(reader);
    out->leaves[i].first_brush = qa_binary_reader_i32(reader);
    out->leaves[i].brush_count = qa_binary_reader_i32(reader);
  }
  count = begin_records(&p, 5, 4);
  out->leaf_surfaces = xmalloc(count * sizeof *out->leaf_surfaces);
  out->leaf_surface_count = count;
  for (size_t i = 0; i < count; i++) out->leaf_surfaces[i] = qa_binary_reader_i32(reader);
  count = begin_records(&p, 6, 4);
  out->leaf_brushes = xmalloc(count * sizeof *out->leaf_brushes);
  out->leaf_brush_count = count;
  for (size_t i = 0; i < count; i++) out->leaf_brushes[i] = qa_binary_reader_i32(reader);
  count = begin_records(&p, 7, 40);
  out->models = xmalloc(count * sizeof *out->models);
  out->model_count = count;
  for (size_t i = 0; i < count; i++) {
    out->models[i].bounds.min = read_vec3(reader);
    out->models[i].bounds.max = read_vec3(reader);
    out->models[i].first_surface = qa_binary_reader_i32(reader);
    out->models[i].surface_count = qa_binary_reader_i32(reader);
    out->models[i].first_brush = qa_binary_reader_i32(reader);
    out->models[i].brush_count = qa_binary_reader_i32(reader);
  }
  count = begin_records(&p, 8, 12);
  out->brushes = xmalloc(count * sizeof *out->brushes);
  out->brush_count = count;
  for (size_t i = 0; i < count; i++) {
    out->brushes[i].first_side = qa_binary_reader_i32(reader);
    out->brushes[i].side_count = qa_binary_reader_i32(reader);
    out->brushes[i].shader = qa_binary_reader_i32(reader);
  }
  count = begin_records(&p, 9, 8);
  out->brush_sides = xmalloc(count * sizeof *out->brush_sides);
  out->brush_side_count = count;
  for (size_t i = 0; i < count; i++) {
    out->brush_sides[i].plane = qa_binary_reader_i32(reader);
    out->brush_sides[i].shader = qa_binary_reader_i32(reader);
  }
  count = begin_records(&p, 10, 44);
  out->vertices = xmalloc(count * sizeof *out->vertices);
  out->vertex_count = count;
  for (size_t i = 0; i < count; i++) {
    out->vertices[i].position = read_vec3(reader);
    out->vertices[i].tex_coord = read_vec2(reader);
    out->vertices[i].lightmap_coord.x = qa_binary_reader_f32(reader);
    out->vertices[i].lightmap_coord.y = qa_binary_reader_f32(reader);
    out->vertices[i].normal = read_vec3(reader);
    out->vertices[i].color.x = qa_binary_reader_u8(reader);
    out->vertices[i].color.y = qa_binary_reader_u8(reader);
    out->vertices[i].color.z = qa_binary_reader_u8(reader);
    out->vertices[i].color.w = qa_binary_reader_u8(reader);
  }
  count = begin_records(&p, 11, 4);
  out->indices = xmalloc(count * sizeof *out->indices);
  out->index_count = count;
  for (size_t i = 0; i < count; i++) out->indices[i] = qa_binary_reader_i32(reader);
  count = begin_records(&p, 12, 72);
  out->fogs = xmalloc(count * sizeof *out->fogs);
  out->fog_count = count;
  for (size_t i = 0; i < count; i++) {
    out->fogs[i].shader = qa_binary_reader_fixed_byte_string(reader, 64);
    out->fogs[i].brush = qa_binary_reader_i32(reader);
    out->fogs[i].visible_side = qa_binary_reader_i32(reader);
  }
  count = begin_records(&p, 13, 104);
  out->surfaces = xmalloc(count * sizeof *out->surfaces);
  out->surface_count = count;
  for (size_t i = 0; i < count; i++) read_surface(&p, &out->surfaces[i]);
  count = begin_records(&p, 14, QA_Q3_LIGHTMAP_BYTES);
  out->lightmaps = xmalloc(count * sizeof *out->lightmaps);
  out->lightmap_count = count;
  for (size_t i = 0; i < count; i++) {
    out->lightmaps[i].bytes = qa_binary_reader_bytes(reader, QA_Q3_LIGHTMAP_BYTES);
    out->lightmaps[i].length = QA_Q3_LIGHTMAP_BYTES;
  }
  count = begin_records(&p, 15, 8);
  out->light_grid = xmalloc(count * sizeof *out->light_grid);
  out->light_grid_count = count;
  for (size_t i = 0; i < count; i++) {
    out->light_grid[i].ambient = byte_vec3(reader);
    out->light_grid[i].directed = byte_vec3(reader);
    out->light_grid[i].lat_long.x = qa_binary_reader_u8(reader);
    out->light_grid[i].lat_long.y = qa_binary_reader_u8(reader);
  }
  lump visibility_lump = p.lumps[16];
  if (visibility_lump.length != 0) {
    qa_binary_reader_seek(reader, visibility_lump.offset);
    if (visibility_lump.length < 8) fail_at(&p, (int64_t)reader->cursor, "truncated visibility header");
    double cluster_count = qa_binary_reader_i32(reader);
    double bytes_per_cluster = qa_binary_reader_i32(reader);
    if (cluster_count < 0 || bytes_per_cluster < ceil(cluster_count / 8) ||
        cluster_count * bytes_per_cluster != (double)visibility_lump.length - 8) {
      fail_at(&p, visibility_lump.offset, "invalid visibility dimensions");
    }
    qa_q3_visibility *visibility = xmalloc(sizeof *visibility);
    visibility->cluster_count = cluster_count;
    visibility->bytes_per_cluster = bytes_per_cluster;
    visibility->bits.bytes = qa_binary_reader_bytes(reader, visibility_lump.length - 8);
    visibility->bits.length = (size_t)(visibility_lump.length - 8);
    out->visibility = visibility;
  }
  /* Validation, mirroring the TS order and messages. */
  for (size_t i = 0; i < out->node_count; i++) {
    int64_t offset = p.lumps[3].offset + (int64_t)i * 36;
    reference(&p, out->nodes[i].plane, (double)out->plane_count, offset, "node plane");
    for (int side = 0; side < 2; side++) {
      double child = out->nodes[i].children[side];
      reference(&p, child < 0 ? -child - 1 : child, child < 0 ? (double)out->leaf_count : (double)out->node_count,
                offset + 4 + side * 4, "node child");
    }
  }
  {
    uint8_t *visited = p.visited = xmalloc(out->node_count);
    typedef struct { size_t node; bool exiting; } stack_entry;
    stack_entry *stack = NULL;
    size_t stack_count = 0, stack_capacity = 0;
    for (size_t root = 0; root < out->node_count; root++) {
      if (visited[root] == 2) continue;
#define PUSH(node_, exiting_)                                          \
  do {                                                                 \
    if (stack_count == stack_capacity) {                               \
      stack_capacity = stack_capacity ? stack_capacity * 2 : 16;       \
      stack = realloc(stack, stack_capacity * sizeof *stack);          \
      if (stack == NULL) qa_range_error("Out of memory");              \
      p.stack = stack;                                                 \
    }                                                                  \
    stack[stack_count].node = (node_);                                \
    stack[stack_count].exiting = (exiting_);                           \
    stack_count++;                                                     \
  } while (0)
      PUSH(root, false);
      while (stack_count > 0) {
        stack_entry current = stack[--stack_count];
        if (current.exiting) { visited[current.node] = 2; continue; }
        if (visited[current.node] == 1)
          fail_at(&p, p.lumps[3].offset + (int64_t)current.node * 36, "cycle in BSP nodes");
        if (visited[current.node] == 2) continue;
        visited[current.node] = 1;
        PUSH(current.node, true);
        for (int side = 0; side < 2; side++) {
          double child = out->nodes[current.node].children[side];
          if (child >= 0) PUSH((size_t)child, false);
        }
      }
#undef PUSH
    }
    free(stack);
    free(visited);
    p.stack = NULL;
    p.visited = NULL;
  }
  for (size_t i = 0; i < out->leaf_count; i++) {
    int64_t offset = p.lumps[4].offset + (int64_t)i * 48;
    const qa_q3_raw_leaf *leaf = &out->leaves[i];
    if (leaf->cluster < -1 || (out->visibility != NULL && leaf->cluster >= out->visibility->cluster_count))
      fail_at(&p, offset, "invalid leaf cluster");
    if (leaf->area < -1 || leaf->area >= MAX_MAP_AREAS) fail_at(&p, offset + 4, "invalid leaf area");
    check_range(&p, leaf->first_surface, leaf->surface_count, (double)out->leaf_surface_count, offset + 32,
                "leaf surfaces");
    check_range(&p, leaf->first_brush, leaf->brush_count, (double)out->leaf_brush_count, offset + 40,
                "leaf brushes");
  }
  for (size_t i = 0; i < out->leaf_surface_count; i++)
    reference(&p, out->leaf_surfaces[i], (double)out->surface_count, p.lumps[5].offset + (int64_t)i * 4,
              "leaf surface");
  for (size_t i = 0; i < out->leaf_brush_count; i++)
    reference(&p, out->leaf_brushes[i], (double)out->brush_count, p.lumps[6].offset + (int64_t)i * 4,
              "leaf brush");
  for (size_t i = 0; i < out->model_count; i++) {
    int64_t offset = p.lumps[7].offset + (int64_t)i * 40;
    check_range(&p, out->models[i].first_surface, out->models[i].surface_count, (double)out->surface_count,
                offset + 24, "model surfaces");
    check_range(&p, out->models[i].first_brush, out->models[i].brush_count, (double)out->brush_count,
                offset + 32, "model brushes");
  }
  for (size_t i = 0; i < out->brush_count; i++) {
    int64_t offset = p.lumps[8].offset + (int64_t)i * 12;
    check_range(&p, out->brushes[i].first_side, out->brushes[i].side_count, (double)out->brush_side_count,
                offset, "brush sides");
    reference(&p, out->brushes[i].shader, (double)out->shader_count, offset + 8, "brush shader");
  }
  for (size_t i = 0; i < out->brush_side_count; i++) {
    reference(&p, out->brush_sides[i].plane, (double)out->plane_count, p.lumps[9].offset + (int64_t)i * 8,
              "brush side plane");
    reference(&p, out->brush_sides[i].shader, (double)out->shader_count, p.lumps[9].offset + (int64_t)i * 8 + 4,
              "brush side shader");
  }
  for (size_t i = 0; i < out->fog_count; i++) {
    int64_t offset = p.lumps[12].offset + (int64_t)i * 72;
    reference(&p, out->fogs[i].brush, (double)out->brush_count, offset + 64, "fog brush");
    const qa_q3_raw_brush *brush = &out->brushes[(size_t)out->fogs[i].brush];
    if (out->fogs[i].visible_side != -1)
      reference(&p, out->fogs[i].visible_side, brush->side_count, offset + 68, "fog visible side");
  }
  uint8_t *lightmap_use = p.lightmap_use = xmalloc(out->vertex_count);
  double surface_work = 0;
  for (size_t i = 0; i < out->surface_count; i++) {
    const qa_q3_raw_surface *item = &out->surfaces[i];
    int64_t offset = p.lumps[13].offset + (int64_t)i * 104;
    reference(&p, item->shader, (double)out->shader_count, offset, "surface shader");
    bool retail_flare_fog = strcmp(item->type, "flare") == 0 && item->fog == 0 && out->fog_count == 0;
    if (item->fog != -1 && !retail_flare_fog)
      reference(&p, item->fog, (double)out->fog_count, offset + 4, "surface fog");
    check_range(&p, item->first_vertex, item->vertex_count, (double)out->vertex_count, offset + 12,
                "surface vertices");
    check_range(&p, item->first_index, item->index_count, (double)out->index_count, offset + 20,
                "surface indices");
    /* Surfaces own disjoint vertex and index ranges in compiled maps;
     * overlapping ranges would multiply this validation work. */
    surface_work += item->vertex_count + item->index_count;
    if (surface_work > 4 * ((double)out->vertex_count + (double)out->index_count) + 65536)
      fail_at(&p, offset + 12, "surface ranges overlap excessively");
    bool planar_or_patch = strcmp(item->type, "planar") == 0 || strcmp(item->type, "patch") == 0;
    bool uses_lightmap = planar_or_patch && item->lightmap >= 0 && item->lightmap < (double)out->lightmap_count;
    if (planar_or_patch && item->lightmap < -4) fail_at(&p, offset + 28, "invalid lightmap sentinel");
    for (double v = item->first_vertex; v < item->first_vertex + item->vertex_count; v++) {
      if (uses_lightmap) lightmap_use[(size_t)v] = 2;
      else if (lightmap_use[(size_t)v] != 2) lightmap_use[(size_t)v] = 1;
    }
    if (strcmp(item->type, "patch") == 0) {
      if (item->patch_width < 3 || item->patch_height < 3 || fmod(item->patch_width, 2) != 1 ||
          fmod(item->patch_height, 2) != 1 || item->patch_width * item->patch_height != item->vertex_count)
        fail_at(&p, offset + 96, "invalid patch control grid");
    }
    if ((strcmp(item->type, "planar") == 0 || strcmp(item->type, "triangles") == 0) &&
        fmod(item->index_count, 3) != 0)
      fail_at(&p, offset + 24, "triangle index count is not a multiple of 3");
    for (double v = item->first_index; v < item->first_index + item->index_count; v++) {
      double value = out->indices[(size_t)v];
      reference(&p, value, item->vertex_count, p.lumps[11].offset + (int64_t)v * 4, "surface local vertex");
    }
  }
  for (size_t i = 0; i < out->vertex_count; i++) {
    if (lightmap_use[i] == 1) continue;
    if (!isfinite(out->vertices[i].lightmap_coord.x))
      fail_at(&p, p.lumps[10].offset + (int64_t)i * 44 + 20, "non-finite lightmap coordinate");
    if (!isfinite(out->vertices[i].lightmap_coord.y))
      fail_at(&p, p.lumps[10].offset + (int64_t)i * 44 + 24, "non-finite lightmap coordinate");
  }
  free(lightmap_use);
  p.lightmap_use = NULL;
  char entity_name[1024];
  snprintf(entity_name, sizeof entity_name, "%s:entities", name);
  out->entity_records = qa_q3_parse_entities(out->entities, entity_name, &out->entity_record_count);
  qa_cleanup_pop(&parser_node);
  qa_binary_reader_destroy(reader);
  free(normalized);
}

void qa_q3_bsp_map_destroy(qa_q3_bsp_map *map) {
  if (map == NULL) return;
  free(map->entities);
  qa_q3_entities_destroy(map->entity_records, map->entity_record_count);
  for (size_t i = 0; i < map->shader_count; i++) free((void *)map->shaders[i].name);
  free(map->shaders);
  free(map->planes);
  free(map->nodes);
  free(map->leaves);
  free(map->leaf_surfaces);
  free(map->leaf_brushes);
  free(map->models);
  free(map->brushes);
  free(map->brush_sides);
  free(map->vertices);
  free(map->indices);
  for (size_t i = 0; i < map->fog_count; i++) free((void *)map->fogs[i].shader);
  free(map->fogs);
  free(map->surfaces);
  for (size_t i = 0; i < map->lightmap_count; i++) free((void *)map->lightmaps[i].bytes);
  free(map->lightmaps);
  free(map->light_grid);
  if (map->visibility != NULL) {
    free((void *)map->visibility->bits.bytes);
    free(map->visibility);
  }
  memset(map, 0, sizeof *map);
}
