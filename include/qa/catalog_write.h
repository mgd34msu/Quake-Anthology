#ifndef QA_CATALOG_WRITE_H
#define QA_CATALOG_WRITE_H

#include "qa/catalog.h"

typedef struct qa_catalog_write_resolver qa_catalog_write_resolver;

/* Owns the exact selected product/catalog association and its admitted native
 * write root. No media view, path scan or filesystem open is performed. */
bool qa_catalog_write_resolver_create(qa_catalog *, qa_product_id,
    qa_catalog_write_resolver **empty, qa_error *);
void qa_catalog_write_resolver_destroy(qa_catalog_write_resolver *);
/* Borrows remain valid until destroy. The actual host/frontend lease retains
 * owner while its write-view services are installed or entered. */
qa_fs_root *qa_catalog_write_resolver_root(const qa_catalog_write_resolver *);
qa_fs_stream_resolver qa_catalog_write_resolver_services(qa_catalog_write_resolver *);

#endif
