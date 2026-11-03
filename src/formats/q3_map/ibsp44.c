/* Port of src/formats/q3-map/ibsp44.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_map/ibsp44.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

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

static uint32_t get_u32(const uint8_t *data, size_t offset) {
  return (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) |
         ((uint32_t)data[offset + 3] << 24);
}

static int32_t get_i32(const uint8_t *data, size_t offset) { return (int32_t)get_u32(data, offset); }

typedef struct {
  const uint8_t *data;
  size_t length;
} slice;

typedef struct {
  const char *source;
  slice sections[15];
} context;

static void fail_at(const context *ctx, int64_t offset, const char *message) {
  qa_binary_error(ctx->source, offset, "%s", message);
}

static slice record_at(const context *ctx, int index, size_t stride, size_t which) {
  slice section = ctx->sections[index];
  if (section.length % stride != 0) {
    char message[128];
    snprintf(message, sizeof message, "invalid IBSP44 record size %zu", stride);
    qa_binary_error(ctx->source, 8 + index * 8, "%s", message);
  }
  if (which >= section.length / stride) {
    char message[128];
    snprintf(message, sizeof message, "invalid IBSP44 reference %zu", which);
    qa_binary_error(ctx->source, 0, "%s", message);
  }
  slice result = {section.data + which * stride, stride};
  return result;
}

static size_t record_count(const context *ctx, int index, size_t stride) {
  slice section = ctx->sections[index];
  if (section.length % stride != 0) {
    char message[128];
    snprintf(message, sizeof message, "invalid IBSP44 record size %zu", stride);
    qa_binary_error(ctx->source, 8 + index * 8, "%s", message);
  }
  return section.length / stride;
}

static void check_range(const context *ctx, int32_t first, int32_t count, size_t length) {
  if (first < 0 || count < 0 || first > (int64_t)length - count)
    fail_at(ctx, 0, "invalid IBSP44 record range");
}

/* Growable list of owned byte blocks. */
typedef struct {
  uint8_t **items;
  size_t *lengths;
  size_t count;
  size_t capacity;
} block_list;

static void block_push(block_list *list, uint8_t *data, size_t length) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 16;
    list->items = xrealloc(list->items, list->capacity * sizeof *list->items);
    list->lengths = xrealloc(list->lengths, list->capacity * sizeof *list->lengths);
  }
  list->items[list->count] = data;
  list->lengths[list->count] = length;
  list->count++;
}

static uint8_t *write_block(qa_binary_writer *writer) {
  size_t length = 0;
  return qa_binary_writer_finish(writer, &length);
}

/* Shader registry: dedup on (64 name bytes, flags, contents). */
typedef struct {
  uint8_t name[64];
  int32_t flags;
  int32_t contents;
} shader_key;

typedef struct {
  block_list records;
  shader_key *keys;
} shaders;

static int32_t shader_id(shaders *registry, const uint8_t *name, int32_t flags, int32_t contents) {
  for (size_t i = 0; i < registry->records.count; i++) {
    if (memcmp(registry->keys[i].name, name, 64) == 0 && registry->keys[i].flags == flags &&
        registry->keys[i].contents == contents)
      return (int32_t)i;
  }
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, 72)) qa_range_error("Out of memory");
  qa_binary_writer_bytes(&writer, name, 64);
  qa_binary_writer_i32(&writer, flags);
  qa_binary_writer_i32(&writer, contents);
  uint8_t *block = write_block(&writer);
  qa_binary_writer_destroy(&writer);
  registry->keys = xrealloc(registry->keys, (registry->records.count + 1) * sizeof *registry->keys);
  memcpy(registry->keys[registry->records.count].name, name, 64);
  registry->keys[registry->records.count].flags = flags;
  registry->keys[registry->records.count].contents = contents;
  block_push(&registry->records, block, 72);
  return (int32_t)(registry->records.count - 1);
}

static int compare_ints(const void *a, const void *b) {
  int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
  return (x > y) - (x < y);
}

/* Sorted unique int set helpers. */
static void set_add(int32_t **set, size_t *count, size_t *capacity, int32_t value) {
  for (size_t i = 0; i < *count; i++)
    if ((*set)[i] == value) return;
  if (*count == *capacity) {
    *capacity = *capacity ? *capacity * 2 : 16;
    *set = xrealloc(*set, *capacity * sizeof **set);
  }
  (*set)[(*count)++] = value;
}

static bool set_has(const int32_t *set, size_t count, int32_t value) {
  for (size_t i = 0; i < count; i++)
    if (set[i] == value) return true;
  return false;
}

static void model_range(const context *ctx, int32_t *ids, size_t id_count, block_list *output,
                        size_t originals, int32_t *out_first, int32_t *out_count) {
  qa_js_sort(ids, id_count, sizeof *ids, compare_ints);
  for (size_t i = 0; i < id_count; i++) {
    if (ids[i] < 0 || (size_t)ids[i] >= originals) fail_at(ctx, 0, "invalid IBSP44 model member");
  }
  int32_t first = id_count > 0 ? ids[0] : 0;
  bool contiguous = true;
  for (size_t i = 0; i < id_count; i++) {
    if (ids[i] != first + (int32_t)i) contiguous = false;
  }
  if (contiguous) {
    *out_first = first;
    *out_count = (int32_t)id_count;
    return;
  }
  size_t start = output->count;
  for (size_t i = 0; i < id_count; i++) {
    size_t source = (size_t)ids[i];
    uint8_t *copy = xmalloc(output->lengths[source]);
    memcpy(copy, output->items[source], output->lengths[source]);
    block_push(output, copy, output->lengths[source]);
  }
  *out_first = (int32_t)start;
  *out_count = (int32_t)id_count;
}

static uint8_t *join_blocks(const block_list *list, size_t *out_length) {
  size_t total = 0;
  for (size_t i = 0; i < list->count; i++) total += list->lengths[i];
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)total)) qa_range_error("Out of memory");
  for (size_t i = 0; i < list->count; i++) qa_binary_writer_bytes(&writer, list->items[i], list->lengths[i]);
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}

/* Everything normalize owns, released together on return or when a caller
 * catches the BinaryError (TS leaves it to the GC). */
typedef struct normalize_state {
  int32_t *indices;
  const uint8_t **side_names;
  int32_t *side_contents;
  uint8_t *side_has_name, *side_has_content;
  shaders registry;
  block_list brush_records, side_records, surface_records, model_records, fog_records, plane_records;
  int32_t *surface_ids, *brush_ids, *visited, *pending;
  uint8_t *index_bytes, *plane_bytes, *shader_bytes, *model_bytes, *brush_bytes, *side_bytes, *fog_bytes,
      *surface_bytes;
} normalize_state;

static void block_list_free(block_list *list) {
  for (size_t j = 0; j < list->count; j++) free(list->items[j]);
  free(list->items);
  free(list->lengths);
  memset(list, 0, sizeof *list);
}

static void normalize_release(void *raw) {
  normalize_state *st = raw;
  free(st->indices);
  free(st->side_names);
  free(st->side_contents);
  free(st->side_has_name);
  free(st->side_has_content);
  free(st->registry.keys);
  block_list_free(&st->registry.records);
  block_list_free(&st->brush_records);
  block_list_free(&st->side_records);
  block_list_free(&st->surface_records);
  block_list_free(&st->model_records);
  block_list_free(&st->fog_records);
  block_list_free(&st->plane_records);
  free(st->surface_ids);
  free(st->brush_ids);
  free(st->visited);
  free(st->pending);
  free(st->index_bytes);
  free(st->plane_bytes);
  free(st->shader_bytes);
  free(st->model_bytes);
  free(st->brush_bytes);
  free(st->side_bytes);
  free(st->fog_bytes);
  free(st->surface_bytes);
  memset(st, 0, sizeof *st);
}

uint8_t *qa_q3_normalize_bsp(const uint8_t *data, size_t data_length, const char *source,
                             size_t *out_length) {
  if (data_length < 8 || get_u32(data, 0) != 0x50534249u || get_i32(data, 4) != 44) {
    uint8_t *copy = xmalloc(data_length);
    if (data_length > 0) memcpy(copy, data, data_length);
    *out_length = data_length;
    return copy;
  }
  context ctx;
  ctx.source = source;
  if (data_length < 128) fail_at(&ctx, 8, "truncated IBSP44 directory");
  for (int index = 0; index < 15; index++) {
    int32_t offset = get_i32(data, 8 + (size_t)index * 8);
    int32_t length = get_i32(data, 12 + (size_t)index * 8);
    if (offset < 0 || length < 0 || offset > (int64_t)data_length - length ||
        (length > 0 && offset < 128))
      fail_at(&ctx, 8 + index * 8, "invalid IBSP44 lump range");
    ctx.sections[index].data = data + offset;
    ctx.sections[index].length = (size_t)length;
  }
  size_t plane_count = record_count(&ctx, 1, 20);
  size_t node_count = record_count(&ctx, 2, 36);
  size_t leaf_count = record_count(&ctx, 3, 48);
  size_t leaf_surface_count = record_count(&ctx, 4, 4);
  size_t leaf_brush_count = record_count(&ctx, 5, 4);
  size_t model_count = record_count(&ctx, 6, 48);
  size_t brush_count = record_count(&ctx, 7, 12);
  size_t side_count = record_count(&ctx, 8, 8);
  size_t surface_count = record_count(&ctx, 12, 164);
  size_t fog_count = record_count(&ctx, 13, 68);
  size_t vertex_lump_length = ctx.sections[11].length / 44;
  size_t index_count = record_count(&ctx, 14, 4);
  normalize_state st;
  memset(&st, 0, sizeof st);
  qa_cleanup state_node;
  qa_cleanup_push(&state_node, normalize_release, &st);
  st.indices = xmalloc(index_count * sizeof *st.indices);
  size_t indices_length = index_count, indices_capacity = index_count;
  for (size_t i = 0; i < index_count; i++) st.indices[i] = get_i32(record_at(&ctx, 14, 4, i).data, 0);
  /* side name / content maps (sparse; a cleared flag means absent) */
  st.side_names = xmalloc(side_count * sizeof *st.side_names);
  st.side_contents = xmalloc(side_count * sizeof *st.side_contents);
  st.side_has_name = xmalloc(side_count);
  st.side_has_content = xmalloc(side_count);
  memset(st.side_has_name, 0, side_count);
  memset(st.side_has_content, 0, side_count);
  for (size_t i = 0; i < surface_count; i++) {
    slice surface = record_at(&ctx, 12, 164, i);
    int32_t side = get_i32(surface.data, 68);
    if (side >= 0) {
      (void)record_at(&ctx, 8, 8, (size_t)side);
      st.side_names[side] = surface.data;
      st.side_has_name[side] = 1;
    }
  }
  for (size_t i = 0; i < brush_count; i++) {
    slice brush = record_at(&ctx, 7, 12, i);
    int32_t first = get_i32(brush.data, 0), count = get_i32(brush.data, 4);
    check_range(&ctx, first, count, side_count);
    for (int32_t j = first; j < first + count; j++) {
      st.side_contents[j] = get_i32(brush.data, 8);
      st.side_has_content[j] = 1;
    }
  }
  static const uint8_t empty_name[64] = {0};
  for (size_t i = 0; i < brush_count; i++) {
    slice brush = record_at(&ctx, 7, 12, i);
    int32_t first = get_i32(brush.data, 0);
    const uint8_t *name = first >= 0 && (size_t)first < side_count && st.side_has_name[first]
                              ? st.side_names[first]
                              : empty_name;
    int32_t shader = shader_id(&st.registry, name, 0, get_i32(brush.data, 8));
    qa_binary_writer writer;
    if (!qa_binary_writer_init(&writer, 12)) qa_range_error("Out of memory");
    qa_binary_writer_i32(&writer, first);
    qa_binary_writer_i32(&writer, get_i32(brush.data, 4));
    qa_binary_writer_i32(&writer, shader);
    block_push(&st.brush_records, write_block(&writer), 12);
    qa_binary_writer_destroy(&writer);
  }
  for (size_t i = 0; i < side_count; i++) {
    slice side = record_at(&ctx, 8, 8, i);
    const uint8_t *name = st.side_has_name[i] ? st.side_names[i] : empty_name;
    int32_t contents = st.side_has_content[i] ? st.side_contents[i] : 0;
    int32_t shader = shader_id(&st.registry, name, get_i32(side.data, 4), contents);
    qa_binary_writer writer;
    if (!qa_binary_writer_init(&writer, 8)) qa_range_error("Out of memory");
    qa_binary_writer_i32(&writer, get_i32(side.data, 0));
    qa_binary_writer_i32(&writer, shader);
    block_push(&st.side_records, write_block(&writer), 8);
    qa_binary_writer_destroy(&writer);
  }
  for (size_t i = 0; i < surface_count; i++) {
    slice surface = record_at(&ctx, 12, 164, i);
    int32_t side_index = get_i32(surface.data, 68);
    int32_t flags = side_index < 0 ? 0 : get_i32(record_at(&ctx, 8, 8, (size_t)side_index).data, 4);
    int32_t patch_width = get_i32(surface.data, 88), patch_height = get_i32(surface.data, 92);
    int32_t vertex_count = get_i32(surface.data, 76);
    int32_t first_index = get_i32(surface.data, 80), surface_indices = get_i32(surface.data, 84);
    check_range(&ctx, first_index, surface_indices, index_count);
    int32_t type = patch_width > 0 && patch_height > 0 ? 2 : surface_indices > 0 ? 3 : 1;
    if (type == 1) {
      if (vertex_count < 3) fail_at(&ctx, 0, "IBSP44 polygon has fewer than three vertices");
      check_range(&ctx, get_i32(surface.data, 72), vertex_count, vertex_lump_length);
      first_index = (int32_t)indices_length;
      for (int32_t j = 1; j < vertex_count - 1; j++) {
        int32_t fan[3] = {0, j, j + 1};
        for (int k = 0; k < 3; k++) {
          if (indices_length == indices_capacity) {
            indices_capacity = indices_capacity ? indices_capacity * 2 : 16;
            st.indices = xrealloc(st.indices, indices_capacity * sizeof *st.indices);
          }
          st.indices[indices_length++] = fan[k];
        }
      }
      surface_indices = (int32_t)indices_length - first_index;
    }
    int32_t contents = side_index >= 0 && (size_t)side_index < side_count && st.side_has_content[side_index]
                           ? st.side_contents[side_index]
                           : 0;
    int32_t shader = shader_id(&st.registry, surface.data, flags, contents);
    qa_binary_writer writer;
    if (!qa_binary_writer_init(&writer, 104)) qa_range_error("Out of memory");
    qa_binary_writer_i32(&writer, shader);
    qa_binary_writer_i32(&writer, get_i32(surface.data, 64));
    qa_binary_writer_i32(&writer, type);
    qa_binary_writer_i32(&writer, get_i32(surface.data, 72));
    qa_binary_writer_i32(&writer, vertex_count);
    qa_binary_writer_i32(&writer, first_index);
    qa_binary_writer_i32(&writer, surface_indices);
    qa_binary_writer_bytes(&writer, surface.data + 96, 68);
    qa_binary_writer_i32(&writer, patch_width);
    qa_binary_writer_i32(&writer, patch_height);
    block_push(&st.surface_records, write_block(&writer), 104);
    qa_binary_writer_destroy(&writer);
  }
  /* Each model owns its own subtree, so the walks together stay near the node
   * count; overlap beyond that is a crafted file. */
  double walked = 0;
  double walk_budget =
      4 * ((double)node_count + (double)leaf_count + (double)leaf_surface_count + (double)leaf_brush_count) +
      65536;
  for (size_t i = 0; i < model_count; i++) {
    slice model = record_at(&ctx, 6, 48, i);
    size_t surface_id_count = 0, surface_id_cap = 0, brush_id_count = 0, brush_id_cap = 0;
    size_t visited_count = 0, visited_cap = 0;
    size_t pending_count = 0, pending_cap = 16;
    st.pending = xmalloc(pending_cap * sizeof *st.pending);
    st.pending[pending_count++] = get_i32(model.data, 36);
    while (pending_count > 0) {
      int32_t node_id = st.pending[--pending_count];
      if (set_has(st.visited, visited_count, node_id)) continue;
      set_add(&st.visited, &visited_count, &visited_cap, node_id);
      if (++walked > walk_budget) fail_at(&ctx, 0, "IBSP44 model node walks overlap excessively");
      if (node_id >= 0) {
        slice node = record_at(&ctx, 2, 36, (size_t)node_id);
        if (pending_count + 2 > pending_cap) {
          pending_cap *= 2;
          st.pending = xrealloc(st.pending, pending_cap * sizeof *st.pending);
        }
        st.pending[pending_count++] = get_i32(node.data, 4);
        st.pending[pending_count++] = get_i32(node.data, 8);
        continue;
      }
      /* -nodeId - 1 in doubles: INT32_MIN maps to 2147483647, not overflow. */
      slice leaf = record_at(&ctx, 3, 48, (size_t)(-(int64_t)node_id - 1));
      int32_t first_surface = get_i32(leaf.data, 32), leaf_surfaces = get_i32(leaf.data, 36);
      int32_t first_brush = get_i32(leaf.data, 40), leaf_brushes = get_i32(leaf.data, 44);
      check_range(&ctx, first_surface, leaf_surfaces, leaf_surface_count);
      check_range(&ctx, first_brush, leaf_brushes, leaf_brush_count);
      walked += (double)leaf_surfaces + (double)leaf_brushes;
      if (walked > walk_budget) fail_at(&ctx, 0, "IBSP44 model node walks overlap excessively");
      for (int32_t j = first_surface; j < first_surface + leaf_surfaces; j++)
        set_add(&st.surface_ids, &surface_id_count, &surface_id_cap,
                get_i32(record_at(&ctx, 4, 4, (size_t)j).data, 0));
      for (int32_t j = first_brush; j < first_brush + leaf_brushes; j++)
        set_add(&st.brush_ids, &brush_id_count, &brush_id_cap,
                get_i32(record_at(&ctx, 5, 4, (size_t)j).data, 0));
    }
    int32_t first_surface = 0, model_surfaces = 0, first_brush = 0, model_brushes = 0;
    model_range(&ctx, st.surface_ids, surface_id_count, &st.surface_records, surface_count, &first_surface,
                &model_surfaces);
    model_range(&ctx, st.brush_ids, brush_id_count, &st.brush_records, brush_count, &first_brush,
                &model_brushes);
    qa_binary_writer writer;
    if (!qa_binary_writer_init(&writer, 40)) qa_range_error("Out of memory");
    qa_binary_writer_bytes(&writer, model.data, 24);
    qa_binary_writer_i32(&writer, first_surface);
    qa_binary_writer_i32(&writer, model_surfaces);
    qa_binary_writer_i32(&writer, first_brush);
    qa_binary_writer_i32(&writer, model_brushes);
    block_push(&st.model_records, write_block(&writer), 40);
    qa_binary_writer_destroy(&writer);
    free(st.surface_ids);
    free(st.brush_ids);
    free(st.visited);
    free(st.pending);
    st.surface_ids = st.brush_ids = st.visited = st.pending = NULL;
  }
  qa_binary_writer index_writer;
  if (!qa_binary_writer_init(&index_writer, (int64_t)(indices_length * 4))) qa_range_error("Out of memory");
  for (size_t i = 0; i < indices_length; i++) qa_binary_writer_i32(&index_writer, st.indices[i]);
  size_t index_bytes_length = 0;
  st.index_bytes = qa_binary_writer_finish(&index_writer, &index_bytes_length);
  qa_binary_writer_destroy(&index_writer);
  for (size_t i = 0; i < fog_count; i++) {
    slice fog = record_at(&ctx, 13, 68, i);
    qa_binary_writer writer;
    if (!qa_binary_writer_init(&writer, 72)) qa_range_error("Out of memory");
    qa_binary_writer_bytes(&writer, fog.data, 68);
    qa_binary_writer_i32(&writer, -1);
    block_push(&st.fog_records, write_block(&writer), 72);
    qa_binary_writer_destroy(&writer);
  }
  size_t plane_bytes_length = 0;
  for (size_t i = 0; i < plane_count; i++) {
    slice plane = record_at(&ctx, 1, 20, i);
    uint8_t *copy = xmalloc(16);
    memcpy(copy, plane.data, 16);
    block_push(&st.plane_records, copy, 16);
  }
  st.plane_bytes = join_blocks(&st.plane_records, &plane_bytes_length);
  size_t shader_length = 0, model_length = 0, brush_length = 0, side_length = 0, fog_length = 0,
         surface_length = 0;
  st.shader_bytes = join_blocks(&st.registry.records, &shader_length);
  st.model_bytes = join_blocks(&st.model_records, &model_length);
  st.brush_bytes = join_blocks(&st.brush_records, &brush_length);
  st.side_bytes = join_blocks(&st.side_records, &side_length);
  st.fog_bytes = join_blocks(&st.fog_records, &fog_length);
  st.surface_bytes = join_blocks(&st.surface_records, &surface_length);
  slice output[17] = {
      ctx.sections[0], {st.shader_bytes, shader_length}, {st.plane_bytes, plane_bytes_length},
      ctx.sections[2], ctx.sections[3], ctx.sections[4], ctx.sections[5],
      {st.model_bytes, model_length}, {st.brush_bytes, brush_length}, {st.side_bytes, side_length},
      ctx.sections[11], {st.index_bytes, index_bytes_length}, {st.fog_bytes, fog_length},
      {st.surface_bytes, surface_length}, ctx.sections[9], {NULL, 0}, ctx.sections[10]};
  size_t total = 144;
  for (int i = 0; i < 17; i++) total += output[i].length;
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)total)) qa_range_error("Out of memory");
  qa_binary_writer_u32(&writer, 0x50534249);
  qa_binary_writer_i32(&writer, 46);
  size_t offset = 144;
  for (int i = 0; i < 17; i++) {
    qa_binary_writer_i32(&writer, (int32_t)offset);
    qa_binary_writer_i32(&writer, (int32_t)output[i].length);
    offset += output[i].length;
  }
  for (int i = 0; i < 17; i++) {
    if (output[i].length > 0) qa_binary_writer_bytes(&writer, output[i].data, output[i].length);
  }
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  qa_cleanup_pop(&state_node);
  normalize_release(&st);
  return result;
}
