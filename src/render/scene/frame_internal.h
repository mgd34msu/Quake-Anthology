#ifndef QA_SCENE_FRAME_INTERNAL_H
#define QA_SCENE_FRAME_INTERNAL_H
#include "qa/scene.h"
#include "qa/pool.h"

enum { SCENE_COMMANDS, SCENE_IMAGES, SCENE_GEOMETRIES, SCENE_MODELS,
    SCENE_GROUPS, SCENE_SORT_GROUPS, SCENE_SORT_COMMANDS, SCENE_ARRAYS };
typedef struct qa_scene_frame_storage {
    qa_arena backing;
    qa_pool pages;
    struct { size_t slot, pages; } arrays[SCENE_ARRAYS];
} qa_scene_frame_storage;
bool scene_frame_reserve(qa_scene_frame *, unsigned, void **, size_t *, size_t,
    size_t, qa_error *);
#endif
