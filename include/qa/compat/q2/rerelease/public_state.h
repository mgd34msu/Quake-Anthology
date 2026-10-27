/* Port of src/compat/q2/rerelease/public-state.ts. GPL-2.0-or-later.
 * Snapshot buffers are process-retained scratch keyed by byte
 * length (the TS WeakMap cache); body retention returns the
 * previous vector values when bit-identical. */
#ifndef QA_COMPAT_Q2_RERELEASE_PUBLIC_STATE_H
#define QA_COMPAT_Q2_RERELEASE_PUBLIC_STATE_H
#include <stdbool.h>
#include "qa/compat/q2/rerelease/player_state.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/world.h"

typedef struct qa_compat_q2_rerelease_public_edict qa_compat_q2_rerelease_public_edict;
/* NULL only on OOM. */
qa_compat_q2_rerelease_public_edict *qa_compat_q2_rerelease_public_edict_create(qa_compat_q2_memory *memory, qa_raw_entity_view record);
void qa_compat_q2_rerelease_public_edict_destroy(qa_compat_q2_rerelease_public_edict *e);
qa_compat_q2_memory *qa_compat_q2_rerelease_public_edict_get_memory(const qa_compat_q2_rerelease_public_edict *e);
qa_raw_entity_view qa_compat_q2_rerelease_public_edict_get_record(const qa_compat_q2_rerelease_public_edict *e);
qa_guest_address qa_compat_q2_rerelease_public_edict_address(const qa_compat_q2_rerelease_public_edict *e, const char *name);
double qa_compat_q2_rerelease_public_edict_int(const qa_compat_q2_rerelease_public_edict *e, const char *name);
double qa_compat_q2_rerelease_public_edict_uint(const qa_compat_q2_rerelease_public_edict *e, const char *name);
double qa_compat_q2_rerelease_public_edict_byte(const qa_compat_q2_rerelease_public_edict *e, const char *name);
double qa_compat_q2_rerelease_public_edict_float(const qa_compat_q2_rerelease_public_edict *e, const char *name);
/* False is the TS null. */
bool qa_compat_q2_rerelease_public_edict_pointer(const qa_compat_q2_rerelease_public_edict *e, const char *name, qa_guest_address *out);
qa_vec3 qa_compat_q2_rerelease_public_edict_vector(const qa_compat_q2_rerelease_public_edict *e, const char *name);
/* velocity/origin NULL is TS undefined. */
qa_body_state qa_compat_q2_rerelease_public_edict_body(qa_compat_q2_rerelease_public_edict *e, const qa_vec3 *velocity, const qa_vec3 *origin);
void qa_compat_q2_rerelease_public_edict_set_vector(const qa_compat_q2_rerelease_public_edict *e, const char *name, qa_vec3 value);
qa_guest_address qa_compat_q2_rerelease_public_edict_client(const qa_compat_q2_rerelease_public_edict *e);
qa_q2_rerelease_player_state qa_compat_q2_rerelease_public_edict_player_state(const qa_compat_q2_rerelease_public_edict *e);
qa_vec3 qa_compat_q2_rerelease_public_edict_player_velocity(const qa_compat_q2_rerelease_public_edict *e);
double qa_compat_q2_rerelease_public_edict_player_movement_flags(const qa_compat_q2_rerelease_public_edict *e);
typedef struct qa_compat_q2_rerelease_player_view { qa_vec3 view_offset; double view_height, movement_flags; } qa_compat_q2_rerelease_player_view;
qa_compat_q2_rerelease_player_view qa_compat_q2_rerelease_public_edict_player_view(const qa_compat_q2_rerelease_public_edict *e);
typedef struct qa_compat_q2_rerelease_model_state { double model_indexes[4], skin; } qa_compat_q2_rerelease_model_state;
qa_compat_q2_rerelease_model_state qa_compat_q2_rerelease_public_edict_model_state(const qa_compat_q2_rerelease_public_edict *e);
double qa_compat_q2_rerelease_public_edict_ping(const qa_compat_q2_rerelease_public_edict *e);
void qa_compat_q2_rerelease_public_edict_set_ping(const qa_compat_q2_rerelease_public_edict *e, double value);
qa_q2_rerelease_entity_state qa_compat_q2_rerelease_public_edict_state(const qa_compat_q2_rerelease_public_edict *e);
#endif
