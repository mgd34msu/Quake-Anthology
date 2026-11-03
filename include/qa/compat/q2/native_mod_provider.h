/* Port of src/compat/q2/native-mod-provider.ts. Typed component callbacks run in
 * the original native module and borrow canonical actors. TS async lifecycle
 * calls are synchronous. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_PROVIDER_H
#define QA_COMPAT_Q2_NATIVE_MOD_PROVIDER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_mod_host_api.h"
#include "qa/compat/q2/native_mod_pickups.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"
#include "qa/world/session/mods.h"

void qa_compat_q2_validate_native_mod_declaration(const qa_native_mod_declaration *declaration);
/* TS validateNativeModCheckpoint. */
void qa_compat_q2_validate_native_mod_checkpoint(const qa_provider_checkpoint *record, qa_module_identity module, const qa_native_mod_declaration *declaration);

typedef struct qa_compat_q2_native_mod_provider qa_compat_q2_native_mod_provider;
/* assert_current is the TS assertCurrent closure. */
qa_compat_q2_native_mod_provider *qa_compat_q2_native_mod_provider_create(const qa_native_mod_declaration *declaration, const qa_mod_host_services *services, qa_provider_id instance, const char *map, void *assert_ctx, void (*assert_current)(void *));
void qa_compat_q2_native_mod_provider_attach(qa_compat_q2_native_mod_provider *s, const qa_compat_q2_native_mod_host *host);
void qa_compat_q2_native_mod_provider_reserve_protection(qa_compat_q2_native_mod_provider *s);
void qa_compat_q2_native_mod_provider_activate_protection(qa_compat_q2_native_mod_provider *s);
bool qa_compat_q2_native_mod_provider_client_presentation(qa_compat_q2_native_mod_provider *s, qa_mod_client_presentation_source *out);
/* NativeModProjection. False is TS null. */
bool qa_compat_q2_native_mod_provider_slot_of(qa_compat_q2_native_mod_provider *s, qa_actor_id actor, double *out);
bool qa_compat_q2_native_mod_provider_actor_at(qa_compat_q2_native_mod_provider *s, double slot, qa_actor_id *out);
bool qa_compat_q2_native_mod_provider_accepts_client(qa_compat_q2_native_mod_provider *s, double slot);
bool qa_compat_q2_native_mod_provider_project(qa_compat_q2_native_mod_provider *s, qa_raw_entity_view record, qa_owned_actor *out);
qa_guest_address qa_compat_q2_native_mod_provider_address(qa_compat_q2_native_mod_provider *s, qa_actor_id actor);
/* TS importBoundary(name, values, invoke). invoke_ctx must outlive the provider's
 * restore: a link captured during restore re-runs invoke with its saved values. */
typedef qa_guest_call_result (*qa_compat_q2_native_import_fn)(void *ctx, const qa_guest_call_value *values, size_t count);
qa_guest_call_result qa_compat_q2_native_mod_provider_import_boundary(qa_compat_q2_native_mod_provider *s, const char *name, const qa_guest_call_value *values, size_t count, void *invoke_ctx, qa_compat_q2_native_import_fn invoke);
/* Scratch arrays valid until the next call on this provider. */
const qa_app_simulation_presentation *qa_compat_q2_native_mod_provider_appearance_overrides(qa_compat_q2_native_mod_provider *s, size_t *out_count);
const qa_app_simulation_presentation *qa_compat_q2_native_mod_provider_presentations(qa_compat_q2_native_mod_provider *s, size_t *out_count);
bool qa_compat_q2_native_mod_provider_invoke_command(qa_compat_q2_native_mod_provider *s, qa_command_invocation *command);
void qa_compat_q2_native_mod_provider_advance(qa_compat_q2_native_mod_provider *s, qa_frame_context frame);
void qa_compat_q2_native_mod_provider_initialize(qa_compat_q2_native_mod_provider *s, bool restoring);
/* TS invoke(call, inputs). False is TS null. */
bool qa_compat_q2_native_mod_provider_invoke(qa_compat_q2_native_mod_provider *s, const qa_native_mod_source_call *call, qa_compat_q2_native_pickup_inputs inputs, double *out);
/* bytes is malloc'd; caller frees. */
qa_provider_checkpoint qa_compat_q2_native_mod_provider_checkpoint(qa_compat_q2_native_mod_provider *s);
void qa_compat_q2_native_mod_provider_restore(qa_compat_q2_native_mod_provider *s, const qa_provider_checkpoint *record);
void qa_compat_q2_native_mod_provider_close(qa_compat_q2_native_mod_provider *s);
void qa_compat_q2_native_mod_provider_destroy(qa_compat_q2_native_mod_provider *s);
#endif
