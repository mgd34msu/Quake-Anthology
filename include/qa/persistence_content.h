#ifndef QA_PERSISTENCE_CONTENT_H
#define QA_PERSISTENCE_CONTENT_H

#include "qa/application.h"
#include "qa/vfs_view_save.h"

typedef struct qa_application_content_graph qa_application_content_graph;
typedef struct qa_application_content_visitor {
    void *context;
    bool (*pool)(void *, const qa_resource_pool *, qa_error *);
    bool (*catalog)(void *, const qa_catalog *, qa_error *);
    bool (*view)(void *, const qa_vfs *, qa_error *);
} qa_application_content_visitor;
typedef bool (*qa_application_content_visit_fn)(void *, const qa_application *,
    const qa_application_content_visitor *, qa_error *);

/* Borrowed during the capture lease or isolated restoration. Actual holders
 * supply extra inventories through the visitor without discovering content. */
qa_application_content_graph *qa_application_content_graph_read(const qa_application *);
uint64_t qa_application_content_pool_id(const qa_application_content_graph *, const qa_resource_pool *);
uint64_t qa_application_content_catalog_id(const qa_application_content_graph *, const qa_catalog *);
uint64_t qa_application_content_view_id(const qa_application_content_graph *, const qa_vfs *);
qa_resource_pool *qa_application_content_pool(const qa_application_content_graph *, uint64_t);
qa_catalog *qa_application_content_catalog(const qa_application_content_graph *, uint64_t);
qa_vfs *qa_application_content_view(const qa_application_content_graph *, uint64_t);
const qa_resource *qa_application_content_resource(const qa_application_content_graph *,
    uint64_t pool, uint64_t resource);
/* Resolves the actual immutable owner, including resources retained outside
 * current caches. Both distinct outputs remain unchanged when it is absent. */
bool qa_application_content_resource_id(const qa_application_content_graph *,
    const qa_resource *, uint64_t *pool, uint64_t *resource);

/* Only a restored graph can transfer a real owning pool/view reference. Each
 * private VFS has one destructor owner; catalog-owned views cannot be claimed.
 * Outputs must be empty. Borrowed aliases use the lookup functions above. */
bool qa_application_content_claim_pool(qa_application_content_graph *, uint64_t,
    qa_resource_pool **, qa_error *);
bool qa_application_content_claim_view(qa_application_content_graph *, uint64_t,
    qa_vfs **, qa_error *);
bool qa_application_content_retain_catalog(qa_application_content_graph *, uint64_t,
    qa_catalog **, qa_error *);

#endif
