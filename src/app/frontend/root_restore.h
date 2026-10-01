#ifndef QA_FRONTEND_ROOT_RESTORE_H
#define QA_FRONTEND_ROOT_RESTORE_H
#include "world_inventory.h"
/* Adopt the genuine frontend map and physical appearance-cache roots after
 * QWON/QMON import. Q3 registry roots stay with their own late Q3AS codec.
 * The actual map/resource and cache row scopes qualify before any adoption.
 * Failure after a real cache attachment requires ordinary candidate cleanup. */
bool frontend_roots_attach_restored(qa_frontend *, frontend_world_inventory *,
    frontend_model_inventory *, qa_error *);
#endif
