/* Port of src/render/scene/resources.ts. GPL-2.0-or-later. */
#include "qa/render/scene/resources.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
/* A created image's content, copied when the create operation is queued: TS operations keep their RenderImage alive
 * by reference until the renderer applies them, while C producers register from scratch storage (stack levels,
 * decoded lumps freed after registration). A copy lives until the drain after the one that returned its image's
 * release, or until the registry is destroyed. */
typedef struct content_copy {
  double ordinal;
  int state; /* 0 live, 1 release drained (freed at the next drain) */
  qa_image_level *levels; qa_depth_image_level *depth_levels; void **buffers; size_t buffer_count;
  qa_fullbright_range fullbright; qa_byte_view translation;
} content_copy;
typedef struct lifetime {
  double ordinal;
  qa_image_resource_operation *operations; size_t count, capacity;
  content_copy **copies; size_t copy_count, copy_capacity;
  double epoch; /* TS lifetime.epoch: bumped when the backend was replaced */
} lifetime;
double qa_scene_image_registry_backend_epoch(const qa_scene_image_registry *r) { return ((const lifetime *)r->_lifetime)->epoch; }
void qa_scene_image_registry_invalidate_backend(qa_scene_image_registry *r) { ((lifetime *)r->_lifetime)->epoch++; }
typedef struct anim_entry {
  double ordinal; qa_scene_animation_fn update; void *ctx;
  qa_scene_animation_stop_fn on_stop; void *stop_ctx; bool active;
} anim_entry;
typedef struct anims { anim_entry *items; size_t count, capacity; } anims;
static void *xalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Allocation failed"); return p; }
static void *xg(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) qa_range_error("Allocation failed"); return q; }
static void closed_error(void) { qa_range_error("Scene image registry is closed"); }
static int owner_eq(qa_renderer_resource_owner a, qa_renderer_resource_owner b) {
  return a.identity == b.identity && a.session.token == b.session.token && a.generation == b.generation;
}
static qa_scene_image_handle *find_handle(qa_scene_image_handle **list, size_t n, qa_renderer_image image) {
  for (size_t i = 0; i < n; i++) if (list[i]->image.ordinal == image.ordinal && owner_eq(list[i]->image.owner, image.owner)) return list[i];
  return NULL;
}
static void list_push(qa_scene_image_handle ***list, size_t *n, size_t *cap, qa_scene_image_handle *h) {
  if (*n == *cap) { *cap = *cap ? *cap * 2 : 8; *list = xg(*list, *cap * sizeof(void *)); }
  (*list)[(*n)++] = h;
}
static void list_remove(qa_scene_image_handle **list, size_t *n, qa_scene_image_handle *h) {
  for (size_t i = 0; i < *n; i++) if (list[i] == h) { memmove(list + i, list + i + 1, (*n - i - 1) * sizeof(void *)); (*n)--; return; }
}
static void *copy_buffer(content_copy *c, const void *bytes, size_t length) {
  void *out = xalloc(length);
  if (length) memcpy(out, bytes, length);
  c->buffers = xg(c->buffers, (c->buffer_count + 1) * sizeof *c->buffers);
  c->buffers[c->buffer_count++] = out;
  return out;
}
static void content_copy_free(content_copy *c) {
  for (size_t i = 0; i < c->buffer_count; i++) free(c->buffers[i]);
  free(c->buffers); free(c->levels); free(c->depth_levels); free(c);
}
/* Queued updates own their level allocation until a drain packs it (see qa_scene_image_registry_update). */
static void free_pending_updates(qa_image_resource_operation *ops, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (ops[i].kind == QA_IMAGE_OP_UPDATE) free((void *)ops[i].update_level);
}
static qa_render_image keep_content(lifetime *lt, double ordinal, qa_render_image content) {
  content_copy *c = xalloc(sizeof *c); memset(c, 0, sizeof *c);
  c->ordinal = ordinal;
  if (content.level_count) {
    c->levels = xalloc(content.level_count * sizeof *c->levels);
    for (size_t i = 0; i < content.level_count; i++) {
      c->levels[i] = content.levels[i];
      c->levels[i].pixels.bytes = copy_buffer(c, content.levels[i].pixels.bytes, content.levels[i].pixels.length);
    }
    content.levels = c->levels;
  }
  if (content.depth_level_count) {
    c->depth_levels = xalloc(content.depth_level_count * sizeof *c->depth_levels);
    for (size_t i = 0; i < content.depth_level_count; i++) {
      c->depth_levels[i] = content.depth_levels[i];
      if (content.depth_levels[i].pixels != NULL)
        c->depth_levels[i].pixels = copy_buffer(c, content.depth_levels[i].pixels, content.depth_levels[i].pixel_count * sizeof *content.depth_levels[i].pixels);
    }
    content.depth_levels = c->depth_levels;
  }
  if (content.palette.colors.bytes != NULL)
    content.palette.colors.bytes = copy_buffer(c, content.palette.colors.bytes, content.palette.colors.length);
  if (content.fullbright != NULL) { c->fullbright = *content.fullbright; content.fullbright = &c->fullbright; }
  if (content.translation != NULL) {
    c->translation.bytes = copy_buffer(c, content.translation->bytes, content.translation->length);
    c->translation.length = content.translation->length;
    content.translation = &c->translation;
  }
  if (lt->copy_count == lt->copy_capacity) { lt->copy_capacity = lt->copy_capacity ? lt->copy_capacity * 2 : 16; lt->copies = xg(lt->copies, lt->copy_capacity * sizeof *lt->copies); }
  lt->copies[lt->copy_count++] = c;
  return content;
}
static void queue_op(qa_scene_image_registry *r, qa_image_resource_operation op) {
  lifetime *lt = r->_lifetime;
  if (op.kind == QA_IMAGE_OP_CREATE) op.content = keep_content(lt, op.image.ordinal, op.content);
  if (lt->count == lt->capacity) { lt->capacity = lt->capacity ? lt->capacity * 2 : 16; lt->operations = xg(lt->operations, lt->capacity * sizeof *lt->operations); }
  lt->operations[lt->count++] = op;
}
static anim_entry *anim_find(qa_scene_image_registry *r, double ordinal) {
  anims *a = r->_animations;
  if (!a) return NULL;
  for (size_t i = 0; i < a->count; i++) if (a->items[i].active && a->items[i].ordinal == ordinal) return &a->items[i];
  return NULL;
}
static void anim_stop_ordinal(qa_scene_image_registry *r, double ordinal) {
  anim_entry *e = anim_find(r, ordinal);
  if (e) { e->active = false; if (e->on_stop) e->on_stop(e->stop_ctx); }
}
void qa_scene_image_registry_init(qa_scene_image_registry *r, qa_renderer_resource_owner owner, const qa_media_clock *clock) {
  memset(r, 0, sizeof *r); r->owner = owner;
  if (clock) { r->clock = *clock; r->has_clock = true; }
  lifetime *lt = xalloc(sizeof *lt); memset(lt, 0, sizeof *lt); r->_lifetime = lt;
  anims *a = xalloc(sizeof *a); memset(a, 0, sizeof *a); r->_animations = a;
}
void qa_scene_image_registry_destroy(qa_scene_image_registry *r) {
  if (!r) return;
  /* Only the root frees the shared lifetime (forks share it). */
  if (!r->parent && r->_lifetime) {
    lifetime *lt = r->_lifetime;
    for (size_t i = 0; i < lt->copy_count; i++) content_copy_free(lt->copies[i]);
    free_pending_updates(lt->operations, lt->count);
    free(lt->copies); free(lt->operations); free(lt);
  }
  for (size_t i = 0; i < r->allocation_count; i++) free(r->allocations[i]);
  free(r->allocations); free(r->images); free(r->children);
  if (r->_animations) { anims *a = r->_animations; free(a->items); free(a); }
  memset(r, 0, sizeof *r);
}
bool qa_scene_image_registry_is_closed(const qa_scene_image_registry *r) { return r->closed; }
void qa_scene_image_registry_fork(qa_scene_image_registry *r, const qa_media_clock *clock, qa_scene_image_registry *child) {
  if (r->closed) closed_error();
  memset(child, 0, sizeof *child); child->owner = r->owner;
  if (clock) { child->clock = *clock; child->has_clock = true; }
  else { child->clock = r->clock; child->has_clock = r->has_clock; }
  child->_lifetime = r->_lifetime; child->parent = r;
  anims *a = xalloc(sizeof *a); memset(a, 0, sizeof *a); child->_animations = a;
  if (r->child_count == r->child_capacity) { r->child_capacity = r->child_capacity ? r->child_capacity * 2 : 4; r->children = xg(r->children, r->child_capacity * sizeof(void *)); }
  r->children[r->child_count++] = child;
}
const qa_scene_image_handle *qa_scene_image_registry_allocate(qa_scene_image_registry *r, double width, double height, qa_renderer_image source_image) {
  if (r->closed) closed_error();
  lifetime *lt = r->_lifetime;
  qa_scene_image_handle *h = xalloc(sizeof *h);
  h->image = source_image;
  h->image.owner = r->owner; h->image.ordinal = lt->ordinal++; h->image.width = width; h->image.height = height;
  list_push(&r->allocations, &r->allocation_count, &r->allocation_capacity, h);
  return h;
}
const qa_scene_image_handle *qa_scene_image_registry_register(qa_scene_image_registry *r, const char *name, qa_render_image content, qa_texture_sampling sampling, qa_renderer_image source_image) {
  (void)name;
  if (!content.level_count && content.kind != QA_RENDER_IMAGE_DEPTH32F)
    qa_range_error("Cannot read properties of undefined (reading 'width')");
  double w, h;
  if (content.kind == QA_RENDER_IMAGE_DEPTH32F) {
    if (!content.depth_level_count) qa_range_error("Cannot read properties of undefined (reading 'width')");
    w = content.depth_levels[0].width; h = content.depth_levels[0].height;
  } else { w = content.levels[0].width; h = content.levels[0].height; }
  const qa_scene_image_handle *handle = qa_scene_image_registry_allocate(r, w, h, source_image);
  qa_image_resource_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_IMAGE_OP_CREATE; op.image = handle->image; op.content = content; op.sampling = sampling;
  qa_scene_image_registry_commit(r, op);
  queue_op(r, op);
  return handle;
}
const qa_scene_image_handle *qa_scene_image_registry_register_generated(qa_scene_image_registry *r, const char *name, qa_render_image content, qa_texture_sampling sampling) {
  qa_renderer_image src; memset(&src, 0, sizeof src);
  src.source_is_resource = false; src.source_name = name;
  return qa_scene_image_registry_register(r, name, content, sampling, src);
}
void qa_scene_image_registry_require(qa_scene_image_registry *r, qa_renderer_image image) {
  if (!owner_eq(image.owner, r->owner) || !find_handle(r->images, r->image_count, image))
    qa_range_error("Scene image belongs to another resource owner or has been released");
}
bool qa_scene_image_registry_is_resident(qa_scene_image_registry *r, qa_renderer_image image) {
  return owner_eq(image.owner, r->owner) && find_handle(r->images, r->image_count, image) != NULL;
}
void qa_scene_image_registry_commit(qa_scene_image_registry *r, qa_image_resource_operation operation) {
  if (operation.kind == QA_IMAGE_OP_CREATE) {
    if (r->closed) closed_error();
    qa_scene_image_handle *alloc = find_handle(r->allocations, r->allocation_count, operation.image);
    if (!alloc || !owner_eq(operation.image.owner, r->owner) || find_handle(r->images, r->image_count, operation.image))
      qa_range_error("Image allocation is already resident or belongs to another owner");
    list_push(&r->images, &r->image_count, &r->image_capacity, alloc);
  } else if (operation.kind == QA_IMAGE_OP_RELEASE) {
    qa_scene_image_registry_require(r, operation.image);
    qa_scene_image_handle *h = find_handle(r->images, r->image_count, operation.image);
    list_remove(r->images, &r->image_count, h);
    list_remove(r->allocations, &r->allocation_count, h);
    anim_stop_ordinal(r, operation.image.ordinal);
    free(h);
  } else if (operation.kind == QA_IMAGE_OP_UPDATE) qa_scene_image_registry_require(r, operation.image);
}
void qa_scene_image_registry_update(qa_scene_image_registry *r, qa_renderer_image image, double level, qa_image_level content) {
  qa_scene_image_registry_require(r, image);
  qa_image_resource_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_IMAGE_OP_UPDATE; op.image = image; op.level = level;
  /* TS update keeps the caller's ImageLevel by reference until the renderer applies it. C producers update from
   * storage they free afterwards (rebuilt lightmaps) or keep (animation frames), so the queued operation owns one
   * allocation holding the level and a copy of its pixels. Draining moves it into the returned operation array's own
   * allocation (pack_operations), so it lives exactly as long as the drained array its consumer keeps and frees. */
  op.content.kind = QA_RENDER_IMAGE_RGBA8;
  qa_image_level *copy = xalloc(sizeof *copy + content.pixels.length);
  *copy = content;
  uint8_t *pixels = (uint8_t *)(copy + 1);
  if (content.pixels.length) memcpy(pixels, content.pixels.bytes, content.pixels.length);
  copy->pixels.bytes = pixels;
  op.update_level = copy;
  queue_op(r, op);
}
void qa_scene_image_registry_release(qa_scene_image_registry *r, qa_renderer_image image) {
  qa_scene_image_registry_require(r, image);
  qa_scene_image_handle *h = find_handle(r->images, r->image_count, image);
  list_remove(r->images, &r->image_count, h);
  list_remove(r->allocations, &r->allocation_count, h);
  anim_stop_ordinal(r, image.ordinal);
  free(h);
  qa_image_resource_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_IMAGE_OP_RELEASE; op.image = image; queue_op(r, op);
}
void qa_scene_image_registry_texture_mode(qa_scene_image_registry *r, qa_texture_filter filter) {
  qa_image_resource_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_IMAGE_OP_TEXTURE_MODE; op.filter = filter; queue_op(r, op);
}
qa_scene_animation_token qa_scene_image_registry_track_animation(qa_scene_image_registry *r, qa_renderer_image image,
    qa_scene_animation_fn update, void *ctx, qa_scene_animation_stop_fn on_stop, void *stop_ctx) {
  qa_scene_image_registry_require(r, image);
  if (anim_find(r, image.ordinal)) qa_range_error("Scene image already has an animation");
  anims *a = r->_animations;
  if (a->count == a->capacity) { a->capacity = a->capacity ? a->capacity * 2 : 8; a->items = xg(a->items, a->capacity * sizeof *a->items); }
  a->items[a->count++] = (anim_entry){ image.ordinal, update, ctx, on_stop, stop_ctx, true };
  return (qa_scene_animation_token){ r, image.ordinal };
}
void qa_scene_animation_token_stop(qa_scene_animation_token token) { if (token.registry) anim_stop_ordinal(token.registry, token.ordinal); }
/* One allocation: the operations, then each update's level and pixels (8-byte aligned). The pending per-update
 * allocations are freed; a consumer's free() of the result releases everything. */
static size_t align8(size_t n) { return (n + 7) & ~(size_t)7; }
static qa_image_resource_operation *pack_operations(qa_image_resource_operation *ops, size_t count) {
  if (ops == NULL) return NULL;
  size_t total = align8(count * sizeof *ops);
  for (size_t i = 0; i < count; i++)
    if (ops[i].kind == QA_IMAGE_OP_UPDATE && ops[i].update_level != NULL) total += sizeof(qa_image_level) + align8(ops[i].update_level->pixels.length);
  unsigned char *block = xalloc(total);
  qa_image_resource_operation *out = (qa_image_resource_operation *)block;
  if (count) memcpy(out, ops, count * sizeof *ops);
  size_t at = align8(count * sizeof *ops);
  for (size_t i = 0; i < count; i++) {
    if (out[i].kind != QA_IMAGE_OP_UPDATE || out[i].update_level == NULL) continue;
    qa_image_level *pending = (qa_image_level *)out[i].update_level;
    qa_image_level *level = (qa_image_level *)(block + at); at += sizeof *level;
    *level = *pending;
    if (pending->pixels.length) memcpy(block + at, pending->pixels.bytes, pending->pixels.length);
    level->pixels.bytes = block + at; at += align8(pending->pixels.length);
    out[i].update_level = level;
    free(pending);
  }
  free(ops);
  return out;
}
qa_image_resource_operation *qa_scene_image_registry_drain_pending(qa_scene_image_registry *r, size_t *out_count) {
  lifetime *lt = r->_lifetime;
  /* The previous batch has been applied: copies whose release it returned are no longer referenced. */
  size_t kept = 0;
  for (size_t i = 0; i < lt->copy_count; i++) {
    if (lt->copies[i]->state == 1) { content_copy_free(lt->copies[i]); continue; }
    lt->copies[kept++] = lt->copies[i];
  }
  lt->copy_count = kept;
  for (size_t j = 0; j < lt->count; j++) {
    if (lt->operations[j].kind != QA_IMAGE_OP_RELEASE) continue;
    for (size_t i = 0; i < lt->copy_count; i++)
      if (lt->copies[i]->ordinal == lt->operations[j].image.ordinal) lt->copies[i]->state = 1;
  }
  qa_image_resource_operation *result = pack_operations(lt->operations, lt->count);
  if (out_count) *out_count = lt->count;
  lt->operations = NULL; lt->count = 0; lt->capacity = 0;
  return result;
}
qa_image_resource_operation *qa_scene_image_registry_drain_operations(qa_scene_image_registry *r, size_t *out_count) {
  anims *a = r->_animations;
  int any = 0; if (a) for (size_t i = 0; i < a->count; i++) if (a->items[i].active) any = 1;
  if (any) {
    double ms = r->has_clock && r->clock.sample ? r->clock.sample(r->clock.ctx) : 0;
    if (!isfinite(ms)) qa_range_error("Scene image animation clock must be finite");
    for (size_t i = 0; i < a->count; i++) if (a->items[i].active && a->items[i].update) a->items[i].update(a->items[i].ctx, ms);
  }
  return qa_scene_image_registry_drain_pending(r, out_count);
}
void qa_scene_image_registry_isolate(qa_scene_image_registry *r, qa_scene_isolated_fn operation, void *ctx, qa_scene_collect_fn collect, void *collect_ctx) {
  lifetime *lt = r->_lifetime;
  qa_image_resource_operation *prev = lt->operations; size_t pc = lt->count, pcap = lt->capacity;
  lt->operations = NULL; lt->count = 0; lt->capacity = 0;
  /* TS try/finally: the previous queue is restored and the reached operations collected even when operation throws. */
  qa_caught_error caught;
  bool ok = qa_try(operation, ctx, &caught);
  qa_image_resource_operation *reached = pack_operations(lt->operations, lt->count); size_t rc = lt->count;
  lt->operations = prev; lt->count = pc; lt->capacity = pcap;
  if (collect) collect(collect_ctx, reached, rc); else free(reached);
  if (!ok) qa_rethrow(&caught);
}
void qa_scene_image_registry_close(qa_scene_image_registry *r) {
  if (r->closed) return;
  r->closed = true;
  for (size_t i = 0; i < r->child_count; i++) qa_scene_image_registry_close(r->children[i]);
  while (r->image_count) qa_scene_image_registry_release(r, r->images[0]->image);
  for (size_t i = 0; i < r->allocation_count; i++) free(r->allocations[i]);
  r->allocation_count = 0;
  if (r->parent) {
    for (size_t i = 0; i < r->parent->child_count; i++)
      if (r->parent->children[i] == r) { memmove(r->parent->children + i, r->parent->children + i + 1, (r->parent->child_count - i - 1) * sizeof(void *)); r->parent->child_count--; break; }
    r->parent = NULL;
  }
}
qa_render_image qa_scene_rgba_image(qa_image_level level, qa_image_level *level_storage) {
  *level_storage = level;
  qa_render_image image; memset(&image, 0, sizeof image);
  image.kind = QA_RENDER_IMAGE_RGBA8; image.levels = level_storage; image.level_count = 1;
  image.border_color = qa_math_vec4(0, 0, 0, 0);
  return image;
}
