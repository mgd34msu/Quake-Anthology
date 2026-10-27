/*
 * Port of src/world/session/mod-client-applications.ts: the ordered
 * client input application journal. TS plain Error throws abort
 * identically; RangeError throws use qa_range_error. Listener/output
 * failures abort in C (callbacks cannot throw recoverably), so the
 * TS AggregateError fan-out and its catch-and-rethrow output rule
 * degenerate to the first fatal failure.
 * Applications are heap objects returned by begin(); release them
 * with qa_mod_client_application_dispose after finish() (or after
 * close()). GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_CLIENT_APPLICATIONS_H
#define QA_WORLD_SESSION_MOD_CLIENT_APPLICATIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
#include "qa/world/session/mod_clients.h"

typedef struct qa_mod_client_applications qa_mod_client_applications;

typedef bool (*qa_mod_client_applications_live_fn)(
    void *ctx, qa_mod_client_identity identity);
/* TS encodeAim override; NULL selects qa_mod_client_command_aim. */
typedef qa_user_command (*qa_mod_client_encode_aim_fn)(
    void *ctx, qa_vec3 aim, qa_user_command command);

/* NULL only on allocation failure. */
qa_mod_client_applications *qa_mod_client_applications_create(
    qa_mod_client_applications_live_fn live, void *live_ctx);
void qa_mod_client_applications_destroy(
    qa_mod_client_applications *applications);
bool qa_mod_client_applications_active(
    const qa_mod_client_applications *applications);
/* Returns a subscription id for _unsubscribe. */
int64_t qa_mod_client_applications_subscribe(
    qa_mod_client_applications *applications,
    qa_mod_client_application_listener_fn listener, void *listener_ctx);
void qa_mod_client_applications_unsubscribe(
    qa_mod_client_applications *applications, int64_t subscription);
/* NULL is the TS null (inactive or not live). */
qa_mod_client_application *qa_mod_client_applications_begin(
    qa_mod_client_applications *applications,
    qa_mod_client_application_input input,
    qa_mod_client_encode_aim_fn encode_aim, void *encode_aim_ctx);
void qa_mod_client_applications_finish(
    qa_mod_client_applications *applications,
    qa_mod_client_application *application, bool failed);
void qa_mod_client_applications_release(
    qa_mod_client_applications *applications, qa_actor_id actor);
int64_t qa_mod_client_applications_checkpoint(
    qa_mod_client_applications *applications);
void qa_mod_client_applications_restore(
    qa_mod_client_applications *applications, int64_t ordinal);
void qa_mod_client_applications_close(
    qa_mod_client_applications *applications);
void qa_mod_client_application_dispose(
    qa_mod_client_application *application);

#endif
