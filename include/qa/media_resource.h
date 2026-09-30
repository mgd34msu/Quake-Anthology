#ifndef QA_MEDIA_RESOURCE_H
#define QA_MEDIA_RESOURCE_H
#include "qa/cinematic.h"
/* The actual immutable resource retained when this cache entry was admitted.
 * Borrows the asset's reference until its final release. Content aliases keep
 * the original winning record, including its pool-local identity and digest. */
const qa_resource *qa_cinematic_asset_resource(const qa_cinematic_asset *);
#endif
