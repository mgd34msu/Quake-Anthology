/* Port of src/render/scene/resources.ts. GPL-2.0-or-later. */
#include "qa/render/scene/resources.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
typedef struct lifetime {
  double ordinal;
  qa_image_resource_operation *operations; size_t count, capacity;
} lifetime;
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
static void queue_op(qa_scene_image_registry *r, qa_image_resource_operation op) {
  lifetime *lt = r->_lifetime;
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
  if (!r->parent && r->_lifetime) { lifetime *lt = r->_lifetime; free(lt->operations); free(lt); }
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
  /* TS update carries the ImageLevel; C op keeps it in update_level storage
   * is caller-owned scratch in most ports; copy level fields via content. */
  op.content.kind = QA_RENDER_IMAGE_RGBA8;
  /* store pointer via a leaked per-op copy to keep the level addressable */
  qa_image_level *copy = xalloc(sizeof *copy); *copy = content; op.update_level = copy;
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
qa_image_resource_operation *qa_scene_image_registry_drain_pending(qa_scene_image_registry *r, size_t *out_count) {
  lifetime *lt = r->_lifetime;
  qa_image_resource_operation *result = lt->operations;
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
  operation(ctx);
  qa_image_resource_operation *reached = lt->operations; size_t rc = lt->count;
  lt->operations = prev; lt->count = pc; lt->capacity = pcap;
  if (collect) collect(collect_ctx, reached, rc); else free(reached);
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
