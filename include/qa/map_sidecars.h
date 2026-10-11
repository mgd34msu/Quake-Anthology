#ifndef QA_MAP_SIDECARS_H
#define QA_MAP_SIDECARS_H

#include "qa/catalog.h"
#include "qa/collision.h"

typedef struct qa_map_sidecars qa_map_sidecars;
typedef struct qa_map_sidecar {
    const char *path;
    const qa_resource *resource; /* NULL is an actual NOT_FOUND admission. */
    const qa_vfs_acquisition *acquisition; /* NULL only for an observed miss. */
    size_t observation;
} qa_map_sidecar;
/* Called at genuine map admission. Only the geometry product's ordinary
 * scoped mounts are searched, independently of presentation/provider orders. */
bool qa_map_sidecars_create(qa_catalog *, qa_product_id, const char *map_path,
    qa_resource_pool *map_pool, qa_resource *map, qa_map_sidecars **empty, qa_error *);
void qa_map_sidecars_retain(qa_map_sidecars *);
void qa_map_sidecars_release(qa_map_sidecars *);
bool qa_map_sidecars_current(const qa_map_sidecars *);
qa_catalog *qa_map_sidecars_catalog(const qa_map_sidecars *);
qa_product_id qa_map_sidecars_product(const qa_map_sidecars *);
const char *qa_map_sidecars_map_path(const qa_map_sidecars *);
qa_resource *qa_map_sidecars_map(const qa_map_sidecars *);
const qa_vfs *qa_map_sidecars_view(const qa_map_sidecars *);
size_t qa_map_sidecars_count(const qa_map_sidecars *);
const qa_map_sidecar *qa_map_sidecars_at(const qa_map_sidecars *, size_t);
/* The modified BSP borrows the owner's held .ent bytes. Collision geometry
 * borrows that BSP; retire it before releasing the holder. */
bool qa_map_sidecars_apply_entities(const qa_map_sidecars *, qa_bsp_view *, qa_error *);
/* Returns presence separately, so an admitted empty .ent is preserved. */
bool qa_map_sidecars_external_entities(const qa_map_sidecars *, qa_bytes *);
qa_bytes qa_map_sidecars_external_lit(const qa_map_sidecars *);
bool qa_map_sidecars_apply_materials(const qa_map_sidecars *, qa_collision_geometry *, qa_strings *, qa_error *);
/* Q2 consumes the first texinfo with this folded 31-byte texture name. The
 * bootstrap observations still use each complete authored name. */
bool qa_map_sidecars_material_path(const qa_bsp_view *, size_t texinfo, char path[1040], qa_error *);
/* First 15 bytes before NUL; invalid material names consume an empty value.
 * The acquisition continues to retain the entire original sidecar resource. */
qa_bytes qa_map_sidecars_material_input(qa_bytes);

struct qa_application_content_graph;
struct qa_application_content_visitor;
bool qa_map_sidecars_content_visit(const qa_map_sidecars *,
    const struct qa_application_content_visitor *, qa_error *);
bool qa_map_sidecars_checkpoint(const qa_map_sidecars *,
    const struct qa_application_content_graph *, qa_buffer *empty, qa_error *);
/* Restores issued receipts and genuine misses from retained content aliases;
 * no lookup, probe, map admission or native resource acquisition is replayed. */
bool qa_map_sidecars_create_restored(struct qa_application_content_graph *, qa_bytes,
    qa_map_sidecars **empty, qa_error *);

struct qa_application;
/* Borrowed canonical holder for the published map, or the isolated cold
 * candidate's genuinely prepared routing map during restoration. */
const qa_map_sidecars *qa_application_map_sidecars(const struct qa_application *);
#endif
