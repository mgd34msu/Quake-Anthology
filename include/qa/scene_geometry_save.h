#ifndef QA_SCENE_GEOMETRY_SAVE_H
#define QA_SCENE_GEOMETRY_SAVE_H
#include "qa/scene.h"
/* Capture the actual active immutable allocation, including a retained
 * geometry with no surviving draw command. Counters are reconstructed from
 * actual candidate holders. Restore returns one active construction reference
 * and requires an empty output. It invokes no geometry/renderer producer. */
bool qa_scene_geometry_checkpoint(const qa_scene_geometry *, qa_buffer *, qa_error *);
bool qa_scene_geometry_restore(qa_bytes, qa_scene_geometry **, qa_error *);
/* Readonly field/byte comparison against an installed qualified allocation.
 * The caller retains that allocation through dictionary publication. */
bool qa_scene_geometry_checkpoint_ready(const qa_scene_geometry *, qa_bytes, qa_error *);
#endif
