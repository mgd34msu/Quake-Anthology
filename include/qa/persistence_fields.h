#ifndef QA_PERSISTENCE_FIELDS_H
#define QA_PERSISTENCE_FIELDS_H

#include "qa/source_save.h"
#include "qa/physics.h"
#include "qa/gameplay.h"

/* Shared explicit field order. These are nested owner codec fields, not
 * standalone files. Actor provenance uses the candidate checkpoint history.
 * The producer validates source semantics and reconnects item/model IDs. */
bool qa_persistence_physics(qa_source_save_io *, qa_physics_properties *);
bool qa_persistence_collision(qa_source_save_io *, qa_actor_collision *);
bool qa_persistence_attack(qa_source_save_io *, qa_attack *);

#endif
