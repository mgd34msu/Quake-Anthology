/* Port of src/compat/q2/classic/records.ts. GPL-2.0-or-later.
 * Strings are malloc'd byte strings (TS latin1 via charCode). */
#ifndef QA_COMPAT_Q2_CLASSIC_RECORDS_H
#define QA_COMPAT_Q2_CLASSIC_RECORDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/classic/layout.h"
#include "qa/world/actors/registry.h"

/* Malloc'd; caller frees. has=false address is the TS null (""). */
char *qa_compat_q2_read_classic_string(const qa_compat_q2_memory *memory, bool has, qa_guest_address address, double maximum);
void qa_compat_q2_write_classic_string(const qa_compat_q2_memory *memory, qa_guest_address address, const char *text, double capacity);
qa_guest_address qa_compat_q2_allocate_classic_string(const qa_compat_q2_memory *memory, const char *text);
double qa_compat_q2_classic_string_allocation_bytes(const char *text);
qa_vec3 qa_compat_q2_read_classic_vector(const qa_compat_q2_memory *memory, qa_guest_address address);
void qa_compat_q2_write_classic_vector(const qa_compat_q2_memory *memory, qa_guest_address address, qa_vec3 vector);

typedef struct qa_compat_q2_classic_edict_descriptor {
  qa_guest_address base; double stride, count, capacity;
} qa_compat_q2_classic_edict_descriptor;
/** TS ClassicQ2ActorProjection: project false is TS null. */
typedef struct qa_compat_q2_classic_actor_projection {
  void *ctx;
  bool (*project)(void *ctx, qa_raw_entity_view record, qa_owned_actor *out);
  qa_guest_address (*address)(void *ctx, qa_actor_id actor);
} qa_compat_q2_classic_actor_projection;

typedef struct qa_compat_q2_classic_edicts qa_compat_q2_classic_edicts;
/* bind is services.bindEntity. projection may be NULL. NULL only on OOM. */
qa_compat_q2_classic_edicts *qa_compat_q2_classic_edicts_create(
    qa_compat_q2_memory *memory, qa_guest_address exports, qa_session_actor_registry *actors,
    qa_provider_id provider, void *bind_ctx, void (*bind)(void *ctx, qa_raw_entity_view record, qa_owned_actor actor),
    const qa_compat_q2_classic_actor_projection *projection);
void qa_compat_q2_classic_edicts_destroy(qa_compat_q2_classic_edicts *e);
void qa_compat_q2_classic_edicts_retire_input_client(qa_compat_q2_classic_edicts *e, double slot);
void qa_compat_q2_classic_edicts_finish_input_retirement(qa_compat_q2_classic_edicts *e, double slot);
qa_compat_q2_classic_edict_descriptor qa_compat_q2_classic_edicts_descriptor(qa_compat_q2_classic_edicts *e);
qa_raw_entity_view qa_compat_q2_classic_edicts_at(qa_compat_q2_classic_edicts *e, double slot);
qa_raw_entity_view qa_compat_q2_classic_edicts_from_pointer(qa_compat_q2_classic_edicts *e, qa_guest_address address);
/* False return is the TS null OwnedActor. */
bool qa_compat_q2_classic_edicts_current(qa_compat_q2_classic_edicts *e, qa_raw_entity_view record, qa_owned_actor *out);
bool qa_compat_q2_classic_edicts_resolves(qa_compat_q2_classic_edicts *e, qa_actor_id actor);
qa_guest_address qa_compat_q2_classic_edicts_pointer(qa_compat_q2_classic_edicts *e, qa_actor_id actor);
bool qa_compat_q2_classic_edicts_observe(qa_compat_q2_classic_edicts *e, qa_guest_address address, qa_owned_actor *out);
qa_owned_actor qa_compat_q2_classic_edicts_retain_client(qa_compat_q2_classic_edicts *e, double slot);
void qa_compat_q2_classic_edicts_release_client(qa_compat_q2_classic_edicts *e, double slot);
void qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_edicts *e);
/* Borrowed live view (TS DataView); NULL when there is no client. */
uint8_t *qa_compat_q2_classic_edicts_client_prefix(qa_compat_q2_classic_edicts *e, double slot);
void qa_compat_q2_classic_edicts_set_client_ping(qa_compat_q2_classic_edicts *e, double slot, double ping);
#endif
