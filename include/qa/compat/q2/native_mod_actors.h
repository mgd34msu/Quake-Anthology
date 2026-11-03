/* Port of src/compat/q2/native-mod-actors.ts. Source edicts retain their
 * private words, function pointers, allocator and update loop. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_ACTORS_H
#define QA_COMPAT_Q2_NATIVE_MOD_ACTORS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_mod_combat.h"
#include "qa/compat/q2/native_mod_deferred.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"
#include "qa/world/session/mods.h"

typedef struct qa_compat_q2_saved_native_actor { qa_saved_actor_id actor; double slot; bool linked; } qa_compat_q2_saved_native_actor;
/* TS SavedNativeActors. Arrays are malloc'd by checkpoint and by the provider's reader. */
typedef struct qa_compat_q2_saved_native_actors {
  double next_frame, frame;
  qa_compat_q2_saved_native_deferred_damage *deferred; size_t deferred_count;
  qa_compat_q2_saved_native_actor *actors; size_t actor_count;
} qa_compat_q2_saved_native_actors;
/* TS validateSaved's row. */
typedef struct qa_compat_q2_native_owned_saved { qa_owned_actor actor; double slot; bool linked; } qa_compat_q2_native_owned_saved;
/* TS SourceCalls. invoke's false is TS null; returns is a NativeModScalar or "void";
 * actor NULL is TS null. combat is the Pick of NativeModCombatCalls: eligible,
 * transfer, source_execution, scalar and synchronize, with its own ctx. */
typedef struct qa_compat_q2_native_mod_source_calls {
  void *ctx;
  qa_guest_address (*resolve)(void *ctx, qa_native_mod_address address);
  void (*scalar)(void *ctx, qa_guest_address address, double value, qa_native_mod_scalar encoding);
  bool (*invoke)(void *ctx, qa_guest_address entry, const qa_guest_call_value *values, size_t count, const char *returns, const qa_actor_id *actor, qa_guest_call_result *out);
  qa_guest_address (*address)(void *ctx, qa_actor_id actor);
  bool (*actor_at)(void *ctx, double slot, qa_actor_id *out);
  bool (*client_frame)(void *ctx, double slot);
  void (*synchronize_frame)(void *ctx, double seconds, double frame);
  void (*begin_frame)(void *ctx);
  void (*end_frame)(void *ctx);
  qa_compat_q2_native_mod_combat_calls combat;
} qa_compat_q2_native_mod_source_calls;
typedef struct qa_compat_q2_native_mod_actors qa_compat_q2_native_mod_actors;
qa_compat_q2_native_mod_actors *qa_compat_q2_native_mod_actors_create(const qa_native_mod_source_actors *definition, const qa_native_mod_declaration *declaration,
    const qa_compat_q2_native_mod_host *host, const qa_mod_host_services *services, qa_provider_id instance, qa_compat_q2_native_mod_source_calls calls);
bool qa_compat_q2_native_mod_actors_advancing(const qa_compat_q2_native_mod_actors *s);
double qa_compat_q2_native_mod_actors_source_frame(const qa_compat_q2_native_mod_actors *s);
double qa_compat_q2_native_mod_actors_source_time(const qa_compat_q2_native_mod_actors *s);
/* False is TS null. */
bool qa_compat_q2_native_mod_actors_actor_at(const qa_compat_q2_native_mod_actors *s, double slot, qa_owned_actor *out);
bool qa_compat_q2_native_mod_actors_slot_of(const qa_compat_q2_native_mod_actors *s, qa_actor_id actor, double *out);
/* Malloc'd rows of the live owned actors. */
qa_compat_q2_native_mod_actor_slot *qa_compat_q2_native_mod_actors_entries(const qa_compat_q2_native_mod_actors *s, size_t *out_count);
void qa_compat_q2_native_mod_actors_synchronize_clock(qa_compat_q2_native_mod_actors *s);
void qa_compat_q2_native_mod_actors_validate(qa_compat_q2_native_mod_actors *s);
void qa_compat_q2_native_mod_actors_drain_releases(qa_compat_q2_native_mod_actors *s);
void qa_compat_q2_native_mod_actors_advance(qa_compat_q2_native_mod_actors *s, qa_frame_context frame);
void qa_compat_q2_native_mod_actors_suspend(qa_compat_q2_native_mod_actors *s, bool value);
qa_compat_q2_saved_native_actors qa_compat_q2_native_mod_actors_checkpoint(qa_compat_q2_native_mod_actors *s);
void qa_compat_q2_saved_native_actors_free(qa_compat_q2_saved_native_actors *saved);
/* Malloc'd rows; caller frees. */
qa_compat_q2_native_owned_saved *qa_compat_q2_native_mod_actors_validate_saved(qa_compat_q2_native_mod_actors *s, const qa_compat_q2_saved_native_actors *saved, size_t *out_count);
void qa_compat_q2_native_mod_actors_restore(qa_compat_q2_native_mod_actors *s, const qa_compat_q2_saved_native_actors *saved, const qa_compat_q2_native_owned_saved *actors, size_t count);
void qa_compat_q2_native_mod_actors_close(qa_compat_q2_native_mod_actors *s);
void qa_compat_q2_native_mod_actors_destroy(qa_compat_q2_native_mod_actors *s);
#endif
