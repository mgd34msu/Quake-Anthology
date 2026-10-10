#ifndef QA_PERSISTENCE_FIELDS_H
#define QA_PERSISTENCE_FIELDS_H

#include "qa/source_save.h"
#include "qa/physics.h"
#include "qa/gameplay.h"
#include "qa/movement.h"

/* Shared explicit field order. These are nested owner codec fields, not
 * standalone files. Actor provenance uses the candidate checkpoint history.
 * The producer validates source semantics and reconnects item/model IDs. */
uint32_t qa_persistence_family_tag(qa_game_family);
qa_game_family qa_persistence_family_from_tag(uint32_t);
bool qa_persistence_physics(qa_source_save_io *, qa_physics_properties *);
bool qa_persistence_collision(qa_source_save_io *, qa_actor_collision *);
/* Compact word fields keep their existing layout. A false result requires
 * the optional canonical extension to retain every bit and opaque token. */
qa_collision_terminal qa_persistence_contents_import(int32_t, qa_game_family);
bool qa_persistence_contents_compact(qa_collision_bits, qa_game_family, int32_t opaque_token, int32_t *word);
bool qa_persistence_surface_compact(qa_collision_bits, qa_game_family, int32_t *word);
bool qa_persistence_attack(qa_source_save_io *, qa_attack *);
bool qa_persistence_bounds(qa_source_save_io *, qa_bounds *);
bool qa_persistence_body(qa_source_save_io *, qa_body_state *);
bool qa_persistence_ground(qa_source_save_io *, qa_movement_ground *);
bool qa_persistence_movement(qa_source_save_io *, qa_movement_state *);
bool qa_persistence_movement_profile(qa_source_save_io *, qa_movement_profile *);
bool qa_persistence_movement_result(qa_source_save_io *, qa_movement_result *);

#endif
