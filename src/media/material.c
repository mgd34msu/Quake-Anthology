/* Port of src/media/material.ts. GPL-2.0-or-later. */
#include "qa/media/material.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void media_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

void qa_material_cinematic_init(
    qa_material_cinematic *cinematic, qa_cinematic_playback *playback,
    qa_renderer_image image, qa_cinematic_image_allocator_fn allocate,
    void *allocate_ctx,
    void (*uploaded)(void *ctx,
                     const qa_image_resource_operation *operation),
    void *uploaded_ctx) {
  memset(cinematic, 0, sizeof *cinematic);
  if (playback->target.kind != QA_CINEMATIC_TARGET_MATERIAL) {
    media_error("Material cinematic requires a material target");
  }
  cinematic->playback = playback;
  cinematic->enabled = true;
  cinematic->uploaded = uploaded;
  cinematic->uploaded_ctx = uploaded_ctx;
  qa_cinematic_image_init(&cinematic->texture, image, allocate,
                          allocate_ctx);
}

void qa_material_cinematic_destroy(qa_material_cinematic *cinematic) {
  qa_cinematic_image_destroy(&cinematic->texture);
  memset(cinematic, 0, sizeof *cinematic);
}

qa_renderer_image qa_material_cinematic_image(
    const qa_material_cinematic *cinematic) {
  return qa_cinematic_image_image(&cinematic->texture);
}

void qa_material_cinematic_set_enabled(qa_material_cinematic *cinematic,
                                       bool enabled) {
  cinematic->enabled = enabled;
  qa_cinematic_playback_pause(cinematic->playback, !enabled);
}

typedef struct resolve_ctx {
  qa_material_cinematic *cinematic;
  void (*apply)(void *ctx, const qa_image_resource_operation *operation);
  void *apply_ctx;
} resolve_ctx;

static void resolve_apply(void *raw,
                          const qa_image_resource_operation *operation) {
  resolve_ctx *ctx = (resolve_ctx *)raw;
  ctx->apply(ctx->apply_ctx, operation);
  if (ctx->cinematic->uploaded != NULL) {
    ctx->cinematic->uploaded(ctx->cinematic->uploaded_ctx, operation);
  }
}

bool qa_material_cinematic_resolve(
    qa_material_cinematic *cinematic,
    void (*apply)(void *ctx, const qa_image_resource_operation *operation),
    void *apply_ctx, qa_renderer_image *out) {
  qa_cinematic_frame frame;
  bool has_frame = false;
  if (cinematic->enabled) {
    qa_cinematic_tick tick;
    if (!qa_cinematic_playback_tick(cinematic->playback, &tick)) {
      return false;
    }
    has_frame = tick.has_frame;
    if (has_frame) {
      frame = tick.frame;
    } else {
      memset(&frame, 0, sizeof frame);
      qa_cinematic_tick_dispose(&tick);
    }
  }
  if (!has_frame) {
    has_frame = qa_cinematic_playback_current_frame(cinematic->playback,
                                                    &frame);
  }
  /* Source RoQ may publish INFO before its first image. Keep the
   * texture clear until a frame exists. */
  qa_renderer_image image = qa_cinematic_image_image(&cinematic->texture);
  qa_cinematic_frame clear;
  const qa_cinematic_frame *picture = &frame;
  if (!has_frame) {
    memset(&clear, 0, sizeof clear);
    clear.width = (int64_t)image.width;
    clear.height = (int64_t)image.height;
    clear.rgba_length =
        (size_t)(clear.width * clear.height * 4);
    clear.rgba = (uint8_t *)calloc(clear.rgba_length == 0 ? 1
                                                          : clear.rgba_length,
                                   1);
    if (clear.rgba == NULL) return false;
    picture = &clear;
  }
  int64_t revision = has_frame
                         ? qa_cinematic_playback_revision(cinematic->playback)
                         : -2;
  resolve_ctx ctx;
  ctx.cinematic = cinematic;
  ctx.apply = apply;
  ctx.apply_ctx = apply_ctx;
  *out = qa_cinematic_image_resolve(&cinematic->texture, picture, revision,
                                    resolve_apply, &ctx);
  if (!has_frame) {
    qa_cinematic_frame owned = clear;
    qa_cinematic_frame_dispose(&owned);
  } else {
    qa_cinematic_frame_dispose(&frame);
  }
  return true;
}

bool qa_material_cinematic_close(qa_material_cinematic *cinematic,
                                 qa_image_resource_operation *out) {
  bool has = qa_cinematic_image_release(&cinematic->texture, out);
  qa_cinematic_playback_close(cinematic->playback);
  return has;
}

typedef struct value_bridge {
  void (*apply)(void *ctx, qa_image_resource_operation operation);
  void *apply_ctx;
} value_bridge;

static void bridge_apply(void *raw,
                         const qa_image_resource_operation *operation) {
  value_bridge *bridge = (value_bridge *)raw;
  bridge->apply(bridge->apply_ctx, *operation);
}

static qa_renderer_image source_resolve(
    void *ctx,
    void (*apply)(void *apply_ctx, qa_image_resource_operation operation),
    void *apply_ctx) {
  qa_material_cinematic *cinematic = (qa_material_cinematic *)ctx;
  value_bridge bridge = {apply, apply_ctx};
  qa_renderer_image out;
  memset(&out, 0, sizeof out);
  if (!qa_material_cinematic_resolve(cinematic, bridge_apply, &bridge,
                                     &out)) {
    media_error("out of memory");
  }
  return out;
}

qa_shader_cinematic_source qa_material_cinematic_as_source(
    qa_material_cinematic *cinematic) {
  qa_shader_cinematic_source source;
  memset(&source, 0, sizeof source);
  source.source.ctx = cinematic;
  source.source.resolve = source_resolve;
  source.image = qa_material_cinematic_image(cinematic);
  return source;
}
