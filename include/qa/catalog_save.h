#ifndef QA_CATALOG_SAVE_H
#define QA_CATALOG_SAVE_H
#include "qa/catalog.h"
typedef struct qa_catalog_checkpoint_refs {
    void *context;
    bool (*files_encode)(void *, const qa_vfs *, qa_buffer *, qa_error *);
    /* Returns an owned view in the supplied actual isolated resource pool. */
    bool (*files_decode)(void *, qa_resource_pool *, qa_bytes, qa_vfs **, qa_error *);
} qa_catalog_checkpoint_refs;
/* Independent catalog and VFS policy; retain native mounts and rebuild their
 * installed indexes and declaration metadata with the normal constructors. */
bool qa_catalog_clone(const qa_catalog *, qa_catalog **, qa_error *);
bool qa_catalog_checkpoint(const qa_catalog *, const qa_catalog_checkpoint_refs *, qa_buffer *, qa_error *);
/* Restore catalog references and rebuild installed archive member indexes.
 * References come from actual holders; the returned snapshot starts at one. */
bool qa_catalog_restore(qa_resource_pool *, const qa_catalog_checkpoint_refs *, qa_bytes, qa_catalog **, qa_error *);
#endif
