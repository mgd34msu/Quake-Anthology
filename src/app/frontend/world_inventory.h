#ifndef QA_FRONTEND_WORLD_INVENTORY_H
#define QA_FRONTEND_WORLD_INVENTORY_H

#include "scene_inventory.h"

typedef struct frontend_world_inventory frontend_world_inventory;
typedef enum frontend_scene_owner_kind {
    FRONTEND_SCENE_OWNER_FRONTEND,
    FRONTEND_SCENE_OWNER_Q3,
    FRONTEND_SCENE_OWNER_VISUAL,
    FRONTEND_SCENE_OWNER_NATIVE_Q3,
    FRONTEND_SCENE_OWNER_EQUIPMENT,
    FRONTEND_SCENE_OWNER_SELECTED_Q3,
    FRONTEND_SCENE_OWNER_SOURCE,
    FRONTEND_SCENE_OWNER_CHARACTER,
    FRONTEND_SCENE_OWNER_EFFECTS,
    FRONTEND_SCENE_OWNER_GEAR,
    FRONTEND_SCENE_OWNER_REMOTE,
    FRONTEND_SCENE_OWNER_INITIAL,
    FRONTEND_SCENE_OWNER_REMOTE_MAP,
    FRONTEND_SCENE_OWNER_REMOTE_Q2,
    FRONTEND_SCENE_OWNER_REMOTE_Q2_MAP,
    FRONTEND_SCENE_OWNER_REMOTE_Q1,
    FRONTEND_SCENE_OWNER_REMOTE_Q1_MAP,
    FRONTEND_SCENE_OWNER_RENDERER,
    FRONTEND_SCENE_OWNER_UNIFIED_MAP,
    FRONTEND_SCENE_OWNER_UNIFIED_MODEL,
    FRONTEND_SCENE_OWNER_UNIFIED_Q3,
    FRONTEND_SCENE_OWNER_COMPONENT,
    FRONTEND_SCENE_OWNER_REGISTRY
} frontend_scene_owner_kind;
typedef struct frontend_scene_owner {
    frontend_scene_owner_kind kind;
    /* Q3 uses its actual registry's first source-group ordinal plus one.
     * Visual caches use their actual appearance-owner ordinal plus one.
     * Native Q3 uses its actual native-client ordinal plus one.
     * Equipment uses its actual retained media ordinal plus one and row one.
     * Selected Q3 uses its actual selected registry ordinal plus one.
     * Source maps use their actual source-group ordinal plus one and row one.
     * Character uses its actual selected-character registry ordinal plus one.
     * Effects uses its actual selected-effects registry ordinal plus one.
     * Gear uses its actual private gear registry ordinal plus one.
     * Remote and Initial use their actual parent registry ordinal plus one.
     * Remote map uses its resource parent ordinal plus one and row one.
     * Remote Q2 models use their physical parent ordinal plus one and real
     * model row ordinal plus one; its map uses that parent and row one.
     * Remote Q1 parsed models use the same physical parent/model ordinals;
     * its map uses row one and external BSP caches use model ordinal plus two.
     * The frontend map owner has owner and row zero. */
    uint64_t owner, row;
} frontend_scene_owner;
typedef struct frontend_scene_root_view {
    const qa_scene_model *scene;
    frontend_model_source source;
    frontend_scene_owner owner;
    const char *visual_path;
} frontend_scene_root_view;
typedef struct frontend_scene_heap { uint32_t kind; uint64_t ordinal,view; } frontend_scene_heap;
/* Borrow actual paired banks already owned by frontend producers. Retained
 * renderer private heaps are deliberately excluded from this lookup. */
bool frontend_scene_heap_find(const qa_frontend *,const qa_vfs *,qa_scene_resources *,
    qa_material_library *,frontend_scene_heap *,bool *,qa_error *);
bool frontend_scene_heap_read(const qa_frontend *,frontend_scene_heap,const qa_vfs **,
    qa_scene_resources **,qa_material_library **);

/* The real frontend/content capture remains held through all component codecs.
 * The shared namespace must already contain images, libraries, roots and the
 * stable frame address. Every QWON/QMON root is encoded exactly once. */
bool frontend_world_inventory_capture(qa_frontend *, const frontend_scene_inventory *,
    frontend_scene_namespace *, frontend_world_inventory **, qa_error *);
bool frontend_world_inventory_checkpoint(const frontend_world_inventory *, qa_buffer *, qa_error *);
/* Actual detached image/material owners, their state, immutable parsed holders
 * and the complete shared namespace precede root import. No live acquisition,
 * registration or scene builder runs. The inventory owns unadopted roots;
 * images, materials and the stable frame address outlive every root. */
bool frontend_world_inventory_restore(qa_frontend *, frontend_model_inventory *,
    frontend_scene_namespace *, qa_bytes, frontend_world_inventory **, qa_error *);
void frontend_world_inventory_destroy(frontend_world_inventory *);
size_t frontend_world_inventory_world_count(const frontend_world_inventory *);
size_t frontend_world_inventory_model_count(const frontend_world_inventory *);
bool frontend_world_inventory_world_at(const frontend_world_inventory *, size_t,
    frontend_world_source *, frontend_scene_owner *);
bool frontend_world_inventory_model_at(const frontend_world_inventory *, size_t,
    frontend_scene_root_view *);

/* Nonzero keys are physical root ordinal plus one, identical to the shared
 * scene namespace. Pure lookups borrow the exact already imported pointer.
 * Borrowed map references remain resolvable after their destructor adopts it. */
bool frontend_world_encode(void *, const qa_scene_world *, uint64_t *, qa_error *);
bool frontend_world_decode(void *, uint64_t, qa_scene_world **, qa_error *);
bool frontend_scene_root_encode(void *, const qa_scene_model *, uint64_t *, qa_error *);
bool frontend_scene_root_decode(void *, uint64_t, qa_scene_model **, qa_error *);
/* Actual owned consumers call the matching scoped check before a complete
 * codec/admission succeeds. It rejects another destructor or prior adoption.
 * No-fail adoption then transfers construction ownership exactly once. */
bool frontend_world_owner_ready(const frontend_world_inventory *, uint64_t,
    frontend_scene_owner_kind, uint64_t owner, qa_error *);
bool frontend_scene_root_owner_ready(const frontend_world_inventory *, uint64_t,
    frontend_scene_owner_kind, uint64_t owner, qa_error *);
void frontend_world_adopt(void *, uint64_t);
void frontend_scene_root_adopt(void *, uint64_t);
/* All genuine consumers have adopted before aggregate installation. Destroy
 * unadopted roots before releasing immutable inventory staging or child heaps. */
bool frontend_world_inventory_ready(const frontend_world_inventory *, qa_error *);
/* Adopts source-owned private map roots after all real groups construct and
 * before their presentation backends import the saved world borrow. */
bool frontend_source_roots_attach_restored(qa_frontend *,frontend_world_inventory *,qa_error *);
bool frontend_remote_roots_attach_restored(qa_frontend *,frontend_world_inventory *,qa_error *);

#endif
