#include "internal.h"
#include "surface_cache.h"

/* Like Q1/Q2 D_SCAlloc, one bounded arena recycles surface storage. The RGBA
 * cache is derived presentation data; geometry ownership keeps only its
 * retirement record, and never keeps an old map or asset pixels alive. */
#define CPU_SURFACE_CACHE_BYTES (32u * 1024u * 1024u)
#define CPU_SURFACE_CACHE_ENTRIES 8192u
#define CPU_SURFACE_MIPS 4u

typedef struct cpu_surface_stamp {
  uint64_t image[2], revision[2], light_revision;
  const void *base_pixels;
  uint32_t base_width, base_height;
  qa_q3_texture_format base_format, light_format;
  bool base_alpha, light_alpha, light_linear;
  qa_scene_vec4 color;
  qa_scene_texture_environment environment;
  qa_scene_lighting_kind lighting;
  uint8_t texture_count;
} cpu_surface_stamp;

typedef struct cpu_surface_slot cpu_surface_slot;
typedef struct cpu_surface_block {
  struct cpu_surface_block *next;
  size_t bytes;
  cpu_surface_slot *owner;
  uint64_t batch;
} cpu_surface_block;

struct cpu_surface_slot {
  cpu_surface_block *block;
  cpu_surface_stamp stamp;
  uint32_t width, height;
};

typedef struct cpu_surface_entry {
  struct cpu_surface_entry *previous, *next;
  const qa_scene_geometry *geometry;
  uint64_t identity, revision;
  cpu_surface_slot mips[CPU_SURFACE_MIPS];
} cpu_surface_entry;

struct cpu_surface_cache {
  qa_render_resource_index index;
  cpu_surface_entry *first, *last;
  size_t count;
  cpu_surface_block *arena, *rover;
  uint64_t batch;
  bool active;
  size_t hits, builds, evictions;
};

static bool block_pinned(const struct cpu_surface_cache *cache,
                          const cpu_surface_block *block) {
  return cache->active && block->owner && block->batch == cache->batch;
}

static void block_clear(struct cpu_surface_cache *cache,
                         cpu_surface_block *block) {
  if (block->owner) {
    block->owner->block = NULL;
    block->owner = NULL;
    ++cache->evictions;
  }
  block->batch = 0;
}

static cpu_surface_block *block_take(struct cpu_surface_cache *cache,
    cpu_surface_block *block, size_t bytes, const cpu_surface_block *limit) {
  if (block_pinned(cache, block)) return NULL;
  while (block->bytes < bytes && block->next && block->next != limit &&
         !block_pinned(cache, block->next)) {
    cpu_surface_block *next = block->next;
    block_clear(cache, next);
    block->bytes += sizeof(*next) + next->bytes;
    block->next = next->next;
    if (cache->rover == next) cache->rover = block;
  }
  if (block->bytes < bytes) return NULL;
  block_clear(cache, block);
  if (block->bytes - bytes >= sizeof(*block) + 64) {
    cpu_surface_block *tail = (cpu_surface_block *)
        ((uint8_t *)(block + 1) + bytes);
    *tail = (cpu_surface_block){.next = block->next,
        .bytes = block->bytes - bytes - sizeof(*tail)};
    block->bytes = bytes;
    block->next = tail;
  }
  cache->rover = block->next ? block->next : cache->arena;
  return block;
}

static cpu_surface_block *block_allocate(struct cpu_surface_cache *cache,
                                          size_t bytes) {
  bytes = (bytes + 7u) & ~(size_t)7u;
  cpu_surface_block *start = cache->rover;
  for (cpu_surface_block *block = start; block; block = block->next) {
    cpu_surface_block *result = block_take(cache, block, bytes, NULL);
    if (result) return result;
  }
  for (cpu_surface_block *block = cache->arena; block != start;
       block = block->next) {
    cpu_surface_block *result = block_take(cache, block, bytes, start);
    if (result) return result;
  }
  return NULL;
}

static bool cache_create(qa_cpu_renderer *renderer, qa_error *error) {
  struct cpu_surface_cache *cache = calloc(1, sizeof(*cache));
  if (!cache) goto failed;
  cache->arena = malloc(CPU_SURFACE_CACHE_BYTES);
  if (!cache->arena) {
    free(cache);
    goto failed;
  }
  *cache->arena = (cpu_surface_block){
      .bytes = CPU_SURFACE_CACHE_BYTES - sizeof(*cache->arena)};
  cache->rover = cache->arena;
  cache->batch = 1;
  cache->active = true;
  renderer->surface_cache = cache;
  return true;
failed:
  qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CPU surface cache");
  return false;
}

static void entry_unlink(struct cpu_surface_cache *cache,
                           cpu_surface_entry *entry) {
  if (entry->previous) entry->previous->next = entry->next;
  else cache->first = entry->next;
  if (entry->next) entry->next->previous = entry->previous;
  else cache->last = entry->previous;
}

static void entry_touch(struct cpu_surface_cache *cache,
                         cpu_surface_entry *entry) {
  if (entry == cache->first) return;
  entry_unlink(cache, entry);
  entry->previous = NULL;
  entry->next = cache->first;
  if (cache->first) cache->first->previous = entry;
  else cache->last = entry;
  cache->first = entry;
}

static bool entry_pinned(const struct cpu_surface_cache *cache,
                           const cpu_surface_entry *entry) {
  for (unsigned mip = 0; mip < CPU_SURFACE_MIPS; ++mip)
    if (entry->mips[mip].block && block_pinned(cache, entry->mips[mip].block))
      return true;
  return false;
}

static void entry_clear(struct cpu_surface_cache *cache,
                         cpu_surface_entry *entry) {
  render_resource_remove(&cache->index, entry->identity, entry->revision,
                         entry->geometry);
  for (unsigned mip = 0; mip < CPU_SURFACE_MIPS; ++mip)
    if (entry->mips[mip].block) block_clear(cache, entry->mips[mip].block);
  qa_scene_geometry_cache_release(entry->geometry);
}

static cpu_surface_entry *entry_admit(struct cpu_surface_cache *cache,
    const qa_scene_mesh *mesh, qa_error *error) {
  cpu_surface_entry *entry = cache->last;
  if (entry && !qa_scene_geometry_active(entry->geometry) &&
      !entry_pinned(cache, entry)) {
    entry_clear(cache, entry);
  } else if (cache->count < CPU_SURFACE_CACHE_ENTRIES) {
    if (!render_resource_reserve(&cache->index, cache->count + 1, error))
      return NULL;
    entry = calloc(1, sizeof(*entry));
    if (!entry) {
      qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CPU surface entry");
      return NULL;
    }
    entry->next = cache->first;
    if (cache->first) cache->first->previous = entry;
    else cache->last = entry;
    cache->first = entry;
    ++cache->count;
  } else {
    while (entry && entry_pinned(cache, entry)) entry = entry->previous;
    if (!entry) return NULL;
    entry_clear(cache, entry);
  }
  entry->geometry = mesh->geometry;
  entry->identity = mesh->identity;
  entry->revision = mesh->revision;
  qa_scene_geometry_cache_retain(entry->geometry);
  render_resource_put(&cache->index, entry->identity, entry->revision,
                       entry->geometry, entry);
  entry_touch(cache, entry);
  return entry;
}

static cpu_surface_stamp surface_stamp(const qa_scene_draw *draw,
    const cpu_sampler *base, const cpu_sampler *light) {
  cpu_surface_stamp stamp = {.color = draw->vertex_inputs.color,
      .light_revision = draw->brush.light_revision,
      .environment = draw->environment, .lighting = draw->lighting,
      .texture_count = draw->texture_count,
      .base_pixels = base->image->levels[0].pixels,
      .base_width = base->image->levels[0].width,
      .base_height = base->image->levels[0].height,
      .base_format = base->image->source_q3 ? base->image->source_format
                                          : QA_Q3_TEXTURE_RGBA8,
      .base_alpha = base->alpha};
  for (unsigned unit = 0; unit < draw->texture_count; ++unit) {
    stamp.image[unit] = draw->textures[unit]->identity;
    stamp.revision[unit] = unit == 1 && draw->textures[unit]->streamed
                              ? 0 : draw->textures[unit]->revision;
  }
  if (draw->texture_count == 2) {
    stamp.light_format = light->image->source_q3 ? light->image->source_format
                                               : QA_Q3_TEXTURE_RGBA8;
    stamp.light_alpha = light->alpha;
    stamp.light_linear = light->magnification_linear;
  }
  return stamp;
}

static bool stamp_equal(const cpu_surface_stamp *a,
                         const cpu_surface_stamp *b) {
  return a->image[0] == b->image[0] && a->image[1] == b->image[1] &&
         a->revision[0] == b->revision[0] && a->revision[1] == b->revision[1] &&
         a->light_revision == b->light_revision &&
         a->base_pixels == b->base_pixels &&
         a->base_width == b->base_width && a->base_height == b->base_height &&
         a->base_format == b->base_format && a->light_format == b->light_format &&
         a->base_alpha == b->base_alpha && a->light_alpha == b->light_alpha &&
         a->light_linear == b->light_linear &&
         a->color.x == b->color.x && a->color.y == b->color.y &&
         a->color.z == b->color.z && a->color.w == b->color.w &&
         a->environment == b->environment && a->lighting == b->lighting &&
         a->texture_count == b->texture_count;
}

static bool surface_supported(const qa_cpu_renderer *renderer,
                                const qa_scene_draw *draw, unsigned mip) {
  if (mip >= CPU_SURFACE_MIPS || !draw->brush.present ||
      !draw->mesh.identity || !draw->mesh.revision || !draw->mesh.geometry ||
      !draw->vertex_inputs.constant_color || draw->vertex_inputs.swap_uv ||
      draw->luminance_alpha || draw->light_count || draw->shadow_atlas ||
      draw->source_arrays || draw->source_direct || draw->source_stage_state ||
      draw->source_primitives || draw->source_retain_depth_range ||
      draw->source_retain_polygon_offset || renderer->controls.source.issuing ||
      !draw->brush.texture_size[0] || !draw->brush.texture_size[1] ||
      !draw->brush.texture_extents[0] || !draw->brush.texture_extents[1] ||
      draw->texture_count == 0 || draw->texture_count > 2)
    return false;
  bool lightmapped = draw->environment >= QA_TEXTURE_LIGHTMAP_MODULATE;
  if (lightmapped ? draw->texture_count != 2 :
      draw->texture_count != 1 || draw->environment != QA_TEXTURE_MODULATE ||
      draw->lighting != QA_LIGHT_VERTEX)
    return false;
  for (unsigned unit = 0; unit < draw->texture_count; ++unit) {
    const qa_scene_image *image = draw->textures[unit];
    if (!image || image->kind == QA_SCENE_DEPTH32F ||
        !image->level_count || !image->levels ||
        cpu_target_find(renderer, image) ||
        (image->streamed &&
         (unit == 0 || !cpu_stream_image_read(renderer, image))))
      return false;
  }
  return !lightmapped ||
      (draw->brush.lightmap_rect.width && draw->brush.lightmap_rect.height);
}

static double light_component(const cpu_sampler *sampler, uint8_t value) {
  if (sampler->components) return sampler->components[value];
  return sampler->image->source_q3
             ? qa_render_source_texture_component(sampler->image->source_format,
                                                    value)
             : value / 255.0;
}

static void light_sample(const cpu_sampler *sampler,
    const qa_scene_rect *rect, double s, double t, double out[4]) {
  s = fmin(rect->width - 1, fmax(0, s));
  t = fmin(rect->height - 1, fmax(0, t));
  const qa_scene_image_level *atlas = sampler->image->levels;
  const uint8_t *pixels = atlas->pixels;
  if (!sampler->magnification_linear) {
    size_t x = (size_t)floor(s + 0.5), y = (size_t)floor(t + 0.5);
    const uint8_t *pixel = pixels +
        (((y + (size_t)rect->y) * atlas->width + x + (size_t)rect->x) * 4);
    for (unsigned c = 0; c < 3; ++c)
      out[c] = light_component(sampler, pixel[c]);
    out[3] = sampler->alpha ? light_component(sampler, pixel[3]) : 1;
    return;
  }
  size_t x0 = (size_t)floor(s), y0 = (size_t)floor(t);
  size_t x1 = x0 + 1 < rect->width ? x0 + 1 : x0;
  size_t y1 = y0 + 1 < rect->height ? y0 + 1 : y0;
  double fx = s - (double)x0, fy = t - (double)y0;
  const uint8_t *taps[4] = {
      pixels + (((y0 + (size_t)rect->y) * atlas->width + x0 + (size_t)rect->x) * 4),
      pixels + (((y0 + (size_t)rect->y) * atlas->width + x1 + (size_t)rect->x) * 4),
      pixels + (((y1 + (size_t)rect->y) * atlas->width + x0 + (size_t)rect->x) * 4),
      pixels + (((y1 + (size_t)rect->y) * atlas->width + x1 + (size_t)rect->x) * 4)};
  for (unsigned c = 0; c < (sampler->alpha ? 4u : 3u); ++c)
    out[c] = light_component(sampler, taps[0][c]) * (1 - fx) * (1 - fy) +
             light_component(sampler, taps[1][c]) * fx * (1 - fy) +
             light_component(sampler, taps[2][c]) * (1 - fx) * fy +
             light_component(sampler, taps[3][c]) * fx * fy;
  if (!sampler->alpha) out[3] = 1;
}

static void surface_build(const qa_scene_draw *draw, unsigned mip,
    const cpu_sampler *base, const cpu_sampler *light,
    cpu_surface_slot *slot) {
  const qa_scene_brush_surface *brush = &draw->brush;
  uint8_t *pixels = (uint8_t *)(slot->block + 1);
  const double color[4] = {draw->vertex_inputs.color.x,
      draw->vertex_inputs.color.y, draw->vertex_inputs.color.z,
      draw->vertex_inputs.color.w};
  double step = (double)(1u << mip);
  bool lightmapped = draw->texture_count == 2;
  for (uint32_t y = 0; y < slot->height; ++y) {
    double t = brush->texture_mins[1] + ((double)y + 0.5) * step;
    for (uint32_t x = 0; x < slot->width; ++x) {
      double s = brush->texture_mins[0] + ((double)x + 0.5) * step;
      double texel[4], illumination[4];
      cpu_sample_texture(base, s / brush->texture_size[0],
                         t / brush->texture_size[1], 0, texel);
      if (lightmapped) {
        double ls = s * brush->lightmap_from_texel[0][0] +
                    t * brush->lightmap_from_texel[0][1] +
                        brush->lightmap_from_texel[0][2];
        double lt = s * brush->lightmap_from_texel[1][0] +
                    t * brush->lightmap_from_texel[1][1] +
                        brush->lightmap_from_texel[1][2];
        /* Sample the face's grid directly: +0.5 texel center and atlas origin
         * cancel when converting back to sample coordinates. Neighboring
         * allocations never participate in this face's lighting. */
        light_sample(light, &brush->lightmap_rect, ls, lt, illumination);
        if (draw->lighting == QA_LIGHT_Q2_WORLD) illumination[3] = 1;
      }
      uint8_t *pixel = pixels + ((size_t)y * slot->width + x) * 4;
      for (unsigned c = 0; c < 4; ++c) {
        double value = texel[c] * color[c];
        if (lightmapped) {
          double factor = draw->environment == QA_TEXTURE_LIGHTMAP_INVERT_ALPHA
                              ? 1 - cpu_clamp(illumination[3])
              : draw->environment == QA_TEXTURE_LIGHTMAP_INVERT_COLOR
                              ? 1 - cpu_clamp(illumination[c])
                              : cpu_clamp(illumination[c]);
          value = (cpu_byte(value) / 255.0) * factor;
        }
        pixel[c] = cpu_byte(value);
      }
    }
  }
}

bool cpu_surface_cache_prepare(qa_cpu_renderer *renderer,
    const qa_scene_draw *draw, unsigned mip, cpu_surface_mip *out,
    qa_error *error) {
  if (!surface_supported(renderer, draw, mip)) return false;
  uint32_t width = draw->brush.texture_extents[0] >> mip;
  uint32_t height = draw->brush.texture_extents[1] >> mip;
  if (!width || !height ||
      (size_t)width > (CPU_SURFACE_CACHE_BYTES - sizeof(cpu_surface_block)) /
                          4 / height)
    return false;
  cpu_sampler base, light = {0};
  if (!cpu_sampler_prepare(renderer, draw->textures[0], &base) ||
      (draw->texture_count == 2 &&
       !cpu_sampler_prepare(renderer, draw->textures[1], &light)))
    return false;
  /* Build from the selected Source texture mip, not a screen-pixel sampler.
   * Filtering of the finished lit surface belongs to the span consumer. */
  size_t source_mip = mip < base.level_count ? mip : base.level_count - 1;
  qa_scene_image selected_base = *base.image;
  selected_base.levels += source_mip;
  selected_base.level_count = 1;
  base.image = &selected_base;
  base.level_count = 1;
  base.linear = base.magnification_linear = false;
  base.blend = false;
  if (!renderer->surface_cache && !cache_create(renderer, error)) return false;
  struct cpu_surface_cache *cache = renderer->surface_cache;
  if (!cache->active) {
    ++cache->batch;
    if (!cache->batch) ++cache->batch;
    cache->active = true;
  }
  cpu_surface_entry *entry = render_resource_get(&cache->index,
      draw->mesh.identity, draw->mesh.revision, draw->mesh.geometry);
  if (!entry) entry = entry_admit(cache, &draw->mesh, error);
  if (!entry) return false;
  entry_touch(cache, entry);
  cpu_surface_slot *slot = entry->mips + mip;
  cpu_surface_stamp stamp = surface_stamp(draw, &base, &light);
  if (slot->block && stamp_equal(&slot->stamp, &stamp)) {
    ++cache->hits;
  } else {
    if (slot->block && block_pinned(cache, slot->block)) return false;
    if (!slot->block)
      slot->block = block_allocate(cache, (size_t)width * height * 4);
    if (!slot->block) return false;
    slot->block->owner = slot;
    slot->width = width;
    slot->height = height;
    surface_build(draw, mip, &base, &light, slot);
    slot->stamp = stamp;
    ++cache->builds;
  }
  slot->block->batch = cache->batch;
  *out = (cpu_surface_mip){.pixels = (const uint8_t *)(slot->block + 1),
      .width = slot->width, .height = slot->height,
      .stride = (size_t)slot->width * 4, .mip = mip};
  return true;
}

void cpu_surface_cache_begin(qa_cpu_renderer *renderer) {
  struct cpu_surface_cache *cache = renderer->surface_cache;
  if (!cache) return;
  ++cache->batch;
  if (!cache->batch) ++cache->batch;
  cache->active = true;
}

void cpu_surface_cache_end(qa_cpu_renderer *renderer) {
  if (renderer->surface_cache) renderer->surface_cache->active = false;
}

void cpu_surface_cache_destroy(qa_cpu_renderer *renderer) {
  struct cpu_surface_cache *cache = renderer->surface_cache;
  if (!cache) return;
  for (cpu_surface_entry *entry = cache->first; entry;) {
    cpu_surface_entry *next = entry->next;
    qa_scene_geometry_cache_release(entry->geometry);
    free(entry);
    entry = next;
  }
  render_resource_destroy(&cache->index);
  free(cache->arena);
  free(cache);
  renderer->surface_cache = NULL;
}
