#ifndef QA_FRONTEND_MODEL_INVENTORY_H
#define QA_FRONTEND_MODEL_INVENTORY_H
#include "qa/persistence_content.h"
#include "qa/scene_model_save.h"

typedef struct frontend_model_inventory frontend_model_inventory;
typedef struct frontend_model_lease frontend_model_lease;
typedef struct frontend_animation_lease frontend_animation_lease;
typedef struct frontend_model_source {
    const qa_model *model;
    const qa_resource *resource;
    const qa_vfs *files;
    /* Actual held subset borrows every parent allocation except its own mesh
     * descriptor and triangles. NULL denotes a complete independent holder. */
    const qa_model *parent;
} frontend_model_source;
typedef struct frontend_animation_source {
    const qa_model_animation *animation;
    const qa_resource *resource;
    const qa_vfs *files;
    const qa_resource *scale_resource;
} frontend_animation_source;

/* Capture borrows the actual immutable parsed holders under the enclosing
 * frontend lease and retains their resource versions. Repeated holder pointers
 * share one ordinal; distinct holders remain distinct even with equal bytes.
 * The current content graph stays leased through capture/checkpoint. Every
 * future checkpoint collects live producers against a fresh graph. */
bool frontend_models_capture(qa_application_content_graph *,
    const frontend_model_source *, size_t, const frontend_animation_source *, size_t,
    frontend_model_inventory **, qa_error *);
bool frontend_models_checkpoint(const frontend_model_inventory *, qa_buffer *, qa_error *);
/* Decode owns parsed arrays and source buffers, preserving actual subset
 * aliases and their parent lifetime. It runs no model
 * parser, animation/scaling operation, acquisition, registration or callback.
 * Restored rows initially have construction holds. Actual cache and scene
 * consumers retain their holders before install drops those holds. The source
 * VFS owners outlive their consumers; the construction graph is never retained. */
bool frontend_models_restore(qa_application_content_graph *, qa_bytes,
    frontend_model_inventory **, qa_error *);
void frontend_models_destroy(frontend_model_inventory *);
/* Each successful retain returns one owning token. Release it after dependent
 * scene children are destroyed. Tokens keep inventory metadata alive without
 * borrowing a temporary decoder context; the last token frees parsed arrays
 * and resource versions while their retired ordinal remains a stable hole. */
bool frontend_model_retain(frontend_model_inventory *, const qa_model *,
    frontend_model_lease **, qa_error *);
/* Clone a live owning token, including after enclosing inventory retirement.
 * The existing token keeps its exact parsed row and source buffers alive. */
bool frontend_model_lease_clone(const frontend_model_lease *, frontend_model_lease **, qa_error *);
void frontend_model_release(frontend_model_lease *);
bool frontend_animation_retain(frontend_model_inventory *, const qa_model_animation *,
    frontend_animation_lease **, qa_error *);
bool frontend_animation_lease_clone(const frontend_animation_lease *, frontend_animation_lease **, qa_error *);
void frontend_animation_release(frontend_animation_lease *);
/* Borrow actual retained source provenance for a fresh capture. Tokens keep
 * this row alive even after frontend inventory ownership is released. These
 * getters never consult the old content graph or acquire a resource. */
bool frontend_model_lease_source(const frontend_model_lease *, frontend_model_source *);
bool frontend_animation_lease_source(const frontend_animation_lease *, frontend_animation_source *);
/* Called once after all actual consumers have claimed their holders. It checks
 * the real claim counters and releases construction holds, including unused
 * rows. Destroy releases frontend ownership; outstanding tokens remain valid. */
bool frontend_models_install(frontend_model_inventory *, qa_error *);
/* Counts are physical extents, including retired holes. Read returns false for
 * a retired row. Fresh capture enumerates only live producer references. */
size_t frontend_model_count(const frontend_model_inventory *);
size_t frontend_animation_count(const frontend_model_inventory *);
bool frontend_model_source_at(const frontend_model_inventory *, size_t,
    frontend_model_source *);
bool frontend_animation_source_at(const frontend_model_inventory *, size_t,
    frontend_animation_source *);
/* Pure QMON callbacks. Their context is the inventory; the aggregate resolver
 * forwards to them when its shared scene dictionary owns the outer context. */
bool frontend_model_encode(void *, const qa_model *, uint64_t *, qa_error *);
bool frontend_model_decode(void *, uint64_t, const qa_model **, qa_error *);
bool frontend_animation_encode(void *, const qa_model_animation *, uint64_t *, qa_error *);
bool frontend_animation_decode(void *, uint64_t,
    const qa_model_animation **, qa_error *);
bool frontend_model_source_qualify(void *, const qa_model *, qa_scene_resources *,
    qa_material_library *, const qa_scene_image_options *, qa_error *);
#endif
