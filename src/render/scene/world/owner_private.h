#ifndef QA_WORLD_OWNER_PRIVATE_H
#define QA_WORLD_OWNER_PRIVATE_H
#include "internal.h"
#include "qa/scene_world_save.h"
#include "qa/source_save.h"

typedef struct qaw_owner_refs {
    void *context;
    bool (*geometry_encode)(void *, const qa_scene_geometry *, uint64_t *, qa_error *);
    bool (*geometry_decode)(void *, uint64_t, const qa_scene_geometry **, qa_error *);
    qa_scene_world_image_refs images;
} qaw_owner_refs;
/* READ operates on a detached, zero-core world with already owned, parsed and
 * qualified source bytes/options and borrowed resource/material owners. The
 * enclosing constructor destroys that entire world on failure. Saved numeric
 * identities remain private until the enclosing constructor maps them to a
 * fresh scene namespace; this layer does not publish a world. */
bool qaw_owner_core_fields(qa_source_save_io *, qa_scene_world *, const qaw_owner_refs *);
#endif
