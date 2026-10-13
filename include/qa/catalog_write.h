#ifndef QA_CATALOG_WRITE_H
#define QA_CATALOG_WRITE_H

#include "qa/catalog.h"

typedef struct qa_catalog_write_resolver qa_catalog_write_resolver;

/* NULL component keeps the primary product write root and borrowed files.
 * A component owns its writable overlay on the installed search order. */
bool qa_catalog_write_resolver_create(qa_catalog *, qa_product_id,
    const char *component, const qa_vfs *installed,
    qa_catalog_write_resolver **empty, qa_error *);
void qa_catalog_write_resolver_destroy(qa_catalog_write_resolver *);
/* Borrows remain valid until destroy. The actual host/frontend lease retains
 * owner while its write-view services are installed or entered. */
qa_fs_root *qa_catalog_write_resolver_root(const qa_catalog_write_resolver *);
qa_vfs *qa_catalog_write_resolver_files(const qa_catalog_write_resolver *);
qa_mount_id qa_catalog_write_resolver_mount(const qa_catalog_write_resolver *);
qa_fs_stream_resolver qa_catalog_write_resolver_services(qa_catalog_write_resolver *);

#endif
