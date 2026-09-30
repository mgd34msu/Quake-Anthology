#ifndef QA_SCENE_WORLD_SAVE_H
#define QA_SCENE_WORLD_SAVE_H
#include "qa/scene.h"
uint64_t qa_scene_world_identity(const qa_scene_world *);
/* Actual submission transactions must return before world owner changes. */
bool qa_scene_world_idle(const qa_scene_world *);
#endif
