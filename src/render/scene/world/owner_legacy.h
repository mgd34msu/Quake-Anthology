#ifndef QA_WORLD_OWNER_LEGACY_H
#define QA_WORLD_OWNER_LEGACY_H
#include "owner_private.h"
/* The detached core/source owners must already be present. Texture/image
 * holders and immutable lighting descriptors are reconstructed from fields;
 * the accepted mutable lighting layer imports caches afterward. */
bool qaw_owner_legacy_fields(qa_source_save_io *, qa_scene_world *, const qaw_owner_refs *);
#endif
