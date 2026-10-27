/*
 * Port of src/media/material.ts: a CinematicPlayback driving one
 * renderer image as a shader video map (a ShaderCinematicSource
 * from src/materials/cinematic.ts). GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_MATERIAL_H
#define QA_MEDIA_MATERIAL_H

#include <stdbool.h>

#include "qa/contracts/render.h"
#include "qa/materials/cinematic.h"
#include "qa/media/playback.h"
#include "qa/media/presentation.h"

typedef struct qa_material_cinematic {
  qa_cinematic_playback *playback; /* borrowed */
  qa_cinematic_image texture;
  bool enabled;
  void *uploaded_ctx;
  void (*uploaded)(void *ctx,
                   const qa_image_resource_operation *operation);
} qa_material_cinematic;

/* `uploaded` NULL is the TS no-op default. */
void qa_material_cinematic_init(
    qa_material_cinematic *cinematic, qa_cinematic_playback *playback,
    qa_renderer_image image, qa_cinematic_image_allocator_fn allocate,
    void *allocate_ctx,
    void (*uploaded)(void *ctx,
                     const qa_image_resource_operation *operation),
    void *uploaded_ctx);
void qa_material_cinematic_destroy(qa_material_cinematic *cinematic);
qa_renderer_image qa_material_cinematic_image(
    const qa_material_cinematic *cinematic);
void qa_material_cinematic_set_enabled(qa_material_cinematic *cinematic,
                                       bool enabled);
/* False only on OOM; *out is the resolved image. */
bool qa_material_cinematic_resolve(
    qa_material_cinematic *cinematic,
    void (*apply)(void *ctx, const qa_image_resource_operation *operation),
    void *apply_ctx, qa_renderer_image *out);
bool qa_material_cinematic_close(qa_material_cinematic *cinematic,
                                 qa_image_resource_operation *out);

/* The ShaderCinematicSource view of this cinematic; `image` is
 * the currently bound image at call time. */
qa_shader_cinematic_source qa_material_cinematic_as_source(
    qa_material_cinematic *cinematic);

#endif
