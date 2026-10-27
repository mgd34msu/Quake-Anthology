/*
 * Port of src/render/scene/models/md3-bounds.ts. The TS WeakMap frame
 * envelope cache is dropped; envelopes are recomputed (identical
 * values). GPL-2.0-or-later.
 */
#ifndef QA_RENDER_SCENE_MODELS_MD3_BOUNDS_H
#define QA_RENDER_SCENE_MODELS_MD3_BOUNDS_H

#include <stdbool.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

/* False is TS null. frame/previous_frame are TS number indices. */
bool qa_md3_world_envelope(const qa_q3_mesh_model *model, double frame,
                           double previous_frame, double back_lerp,
                           qa_model_transform transform, qa_bounds *out);

#endif
