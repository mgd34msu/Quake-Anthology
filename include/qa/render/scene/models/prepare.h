/*
 * Port of src/render/scene/models/prepare.ts. C accommodations: the
 * TS WeakMap pose/world/lit caches are dropped; interpolation and
 * world transforms are pure functions, so values are identical and
 * only object identity differs. Prepared entities own their surface
 * vertex storage recursively. GPL-2.0-or-later.
 */
#ifndef QA_RENDER_SCENE_MODELS_PREPARE_H
#define QA_RENDER_SCENE_MODELS_PREPARE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/scene.h"
#include "qa/render/scene/models/types.h"

typedef struct qa_repaired_frames {
  double frame, previous_frame, back_lerp;
  bool fallback;
} qa_repaired_frames;
qa_repaired_frames qa_repair_frames(const qa_scene_entity *entity);

/* Malloc'd vertices; caller frees. */
qa_model_vertex *qa_interpolate_scene_md2(
    const qa_q2_alias_model *model, const qa_scene_entity *entity,
    double frame, double old_frame, double back_lerp, bool shell,
    size_t *out_count);

void qa_prepared_model_surface_destroy(qa_prepared_model_surface *surface);
void qa_prepared_model_entity_destroy(qa_prepared_model_entity *entity);

/* False only on OOM. *out owns its surfaces/attachments. */
bool qa_prepare_scene_entity(const qa_scene_entity *entity,
                             const qa_model_preparation_context *context,
                             qa_prepared_model_entity *out);
/* Owned groups; NULL/0 on OOM or when nothing draws. */
qa_scene_model_group *qa_prepare_scene_entity_groups(
    const qa_scene_entity *entity, const qa_model_preparation_context *context,
    const qa_model_group_context *material, size_t *out_count);
qa_scene_model_group *qa_prepared_model_groups(
    const qa_prepared_model_entity *model,
    const qa_model_group_context *material, size_t *out_count);
double qa_model_projected_radius(const qa_scene_entity *entity, double radius,
                                 const qa_model_preparation_context *context);

#endif
