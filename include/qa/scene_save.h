#ifndef QA_SCENE_SAVE_H
#define QA_SCENE_SAVE_H
#include "qa/scene.h"

bool qa_scene_image_owner_index(const qa_scene_resources *const *owners, size_t count,
                                 const qa_scene_image *, size_t *index);
#endif
