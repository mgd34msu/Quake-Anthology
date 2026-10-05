#ifndef QA_FRONTEND_WORLD_INVENTORY_H
#define QA_FRONTEND_WORLD_INVENTORY_H

#include "scene_inventory.h"

typedef struct frontend_scene_heap { uint32_t kind; uint64_t ordinal,view; } frontend_scene_heap;
/* Borrow actual paired banks already owned by frontend producers. Retained
 * renderer private heaps are deliberately excluded from this lookup. */
bool frontend_scene_heap_find(const qa_frontend *,const qa_vfs *,qa_scene_resources *,
    qa_material_library *,frontend_scene_heap *,bool *,qa_error *);
bool frontend_scene_heap_read(const qa_frontend *,frontend_scene_heap,const qa_vfs **,
    qa_scene_resources **,qa_material_library **);

#endif
