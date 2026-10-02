#ifndef QA_FRONTEND_Q3_INVENTORY_H
#define QA_FRONTEND_Q3_INVENTORY_H
#include "scene_identity.h"
#include "model_inventory.h"
#include "world_inventory.h"
#include "qa/audio_bank_graph_save.h"
#include "remote_q3_modules.h"
#include "qa/q3_presentation_media_save.h"

typedef struct frontend_q3_inventory frontend_q3_inventory;
typedef struct frontend_q3_refs {
    qa_application_content_graph *content;
    frontend_scene_namespace *scene;
    frontend_model_inventory *models;
    frontend_world_inventory *worlds;
    const qa_audio_asset_inventory *audio;
} frontend_q3_refs;

/* One record per genuine registry, presentation and media-library pointer,
 * including distinct private gear registries over borrowed visual heaps.
 * Gear namespaces and source views remain separate from selected providers.
 * The actual physical aliases and frontend/content/root
 * capture leases remain held through this call and all resolver callbacks. */
bool frontend_q3_checkpoint(qa_frontend *, const frontend_q3_refs *, qa_buffer *, qa_error *);
/* Retain the actual captured owner/cache roster for role-owned private codecs.
 * This borrows the same frontend/content/dictionary leases as checkpoint. */
bool frontend_q3_inventory_capture(qa_frontend *,const frontend_q3_refs *,frontend_q3_inventory **,qa_error *);
/* Call before entering a role codec. The returned cache resolver context is
 * inventory-owned; SystemCIN resolves the actual wrapper's retained role lease. */
bool frontend_q3_module_movie_refs(frontend_q3_inventory *,const frontend_remote_q3_module_topology *,
    qa_q3_movie_checkpoint_refs *,qa_error *);
/* Validate the entire envelope before importing source media-cache prefixes.
 * Stable empty source heaps, restored images and the shared content namespace
 * precede preparation. Input bytes and every referenced owner remain borrowed
 * until this inventory is destroyed. Failure requires candidate retirement;
 * this helper owns no destination source heap or imported scene root. */
bool frontend_q3_prepare(qa_frontend *, const frontend_q3_refs *, qa_bytes,
    frontend_q3_inventory **, qa_error *);
/* Actual QWON/QMON roots, materials, frame, collision, audio and map-resource
 * bindings precede late attachment. Registries adopt their unique scene/preview
 * roots once, then kind-specific private continuation imports under the actual
 * registry capture lease. Gear topology requires the restored private runtime
 * before dictionary import; QFGS follows its real asset and scene holders.
 * Q3PS/Q3MS import private continuation without source calls.
 * Media header and opaque decoder reconstruction belong to their existing
 * qualified codecs; no registration or procedural renderer construction runs. */
bool frontend_q3_restore(frontend_q3_inventory *, double wall_milliseconds, qa_error *);
void frontend_q3_inventory_destroy(frontend_q3_inventory *);
#endif
