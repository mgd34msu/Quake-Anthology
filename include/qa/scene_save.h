#ifndef QA_SCENE_SAVE_H
#define QA_SCENE_SAVE_H
#include "qa/scene.h"

typedef struct qa_scene_image_set qa_scene_image_set;
/* Owner array order is qualified by the enclosing frontend/provider record.
 * Capture includes all live immutable versions, preserving cross-owner image
 * animation and lineage aliases. It does not capture resource cache/policy. */
bool qa_scene_images_checkpoint(const qa_scene_resources *const *owners, size_t count,
                                 qa_buffer *, qa_error *);
bool qa_scene_images_restore(qa_scene_resources *const *owners, size_t count, qa_bytes,
                              qa_scene_image_set **, qa_error *);
/* The set holds one construction reference per version. Consumers retain
 * their own references before releasing the set. References are borrowed. */
const qa_scene_image *qa_scene_image_set_at(const qa_scene_image_set *, size_t);
size_t qa_scene_image_set_count(const qa_scene_image_set *);
void qa_scene_image_set_destroy(qa_scene_image_set *);
bool qa_scene_image_owner_index(const qa_scene_resources *const *owners, size_t count,
                                 const qa_scene_image *, size_t *index);
#endif
