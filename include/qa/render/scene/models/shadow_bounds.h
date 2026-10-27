/*
 * Port of src/render/scene/models/shadow-bounds.ts. The TS WeakMap
 * weight-envelope cache is dropped; envelopes are recomputed.
 * ShadowSphere is the mirrored type in types.h (TS owner:
 * src/render/scene/shadows.ts). GPL-2.0-or-later.
 */
#ifndef QA_RENDER_SCENE_MODELS_SHADOW_BOUNDS_H
#define QA_RENDER_SCENE_MODELS_SHADOW_BOUNDS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/scene.h"
#include "qa/render/scene/models/types.h"

/* False is TS null. */
bool qa_md5_shadow_envelope(const qa_md5_mesh *meshes, size_t mesh_count,
                            const qa_skeleton_joint_pose *joints,
                            size_t joint_count, qa_model_transform transform,
                            qa_render_scene_shadow_sphere *out);

#endif
