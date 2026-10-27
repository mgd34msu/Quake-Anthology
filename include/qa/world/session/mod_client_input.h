/*
 * Port of src/world/session/mod-client-input.ts: subscribing authored
 * input bindings to ordered client applications. The generic
 * <Call, Output> element lists stay opaque storage + elem size
 * (contracts binding shape). Cleanup/open failures abort in C, so
 * the TS AggregateError combinations degenerate to the first fatal
 * failure. The returned unsubscribe frees the subscription state.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_CLIENT_INPUT_H
#define QA_WORLD_SESSION_MOD_CLIENT_INPUT_H

#include <stddef.h>

#include "qa/contracts/mod_callbacks.h"
#include "qa/world/session/mod_clients.h"

/* TS Operations<Call, Output>. open returns its cleanup through the
 * out params (cleanup may be NULL). output may be NULL; when set it
 * must call run (fn+ctx) as the TS run closure and fill a malloc'd
 * outputs array (caller-owned by the subscription, freed after use)
 * through out_outputs/out_count. */
typedef struct qa_mod_client_input_operations {
  void *ctx;
  void (*open)(void *ctx, qa_mod_client_application *application,
               void (**cleanup)(void *cleanup_ctx), void **cleanup_ctx);
  void (*invoke)(void *ctx, const void *call,
                 qa_mod_client_application *application);
  void (*output)(void *ctx, const void *outputs, size_t output_count,
                 size_t output_elem_size,
                 qa_mod_client_application *application,
                 void (*run)(void *run_ctx), void *run_ctx,
                 qa_mod_client_input_output **out_outputs,
                 size_t *out_count);
} qa_mod_client_input_operations;

qa_mod_client_unsubscribe qa_subscribe_mod_client_input(
    qa_mod_client_services *services,
    const qa_mod_client_input_binding *bindings, size_t binding_count,
    qa_mod_client_input_operations operations);

#endif
