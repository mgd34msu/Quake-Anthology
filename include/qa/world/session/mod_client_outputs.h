/*
 * Port of src/world/session/mod-client-outputs.ts: leased component
 * client output channels. TS plain Error throws abort identically;
 * RangeError throws use qa_range_error. The generic <Scalar, Vector>
 * field references stay opaque pointers (contracts header).
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_CLIENT_OUTPUTS_H
#define QA_WORLD_SESSION_MOD_CLIENT_OUTPUTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/mod_client_outputs.h"

/** TS ModClientOutputLease. close() frees the lease state; the
 * returned struct must not be used afterwards. */
typedef struct qa_mod_client_output_lease {
  void *ctx;
  void (*publish)(void *ctx, qa_actor_id actor,
                  const qa_mod_client_output *outputs, size_t count);
  void (*release)(void *ctx, qa_actor_id actor);
  void (*close)(void *ctx);
} qa_mod_client_output_lease;

typedef bool (*qa_mod_client_live_fn)(void *ctx, qa_actor_id actor);

typedef struct qa_mod_client_outputs qa_mod_client_outputs;

/* NULL only on allocation failure. */
qa_mod_client_outputs *qa_mod_client_outputs_create(
    qa_mod_client_live_fn live, void *live_ctx);
void qa_mod_client_outputs_destroy(qa_mod_client_outputs *outputs);
qa_mod_client_output_lease qa_mod_client_outputs_claim(
    qa_mod_client_outputs *outputs, qa_provider_id owner,
    const qa_mod_client_output_channel *channels, size_t channel_count);
/* False is the TS null. */
bool qa_mod_client_outputs_read(qa_mod_client_outputs *outputs,
                                qa_actor_id actor,
                                qa_mod_client_movement_outputs *out);
void qa_mod_client_outputs_release(qa_mod_client_outputs *outputs,
                                   qa_actor_id actor);
void qa_mod_client_outputs_close(qa_mod_client_outputs *outputs);

/* read/check adapters over a source's opaque field references. */
typedef double (*qa_client_output_scalar_fn)(void *ctx, const void *field);
typedef qa_vec3 (*qa_client_output_vector_fn)(void *ctx, const void *field);
typedef void (*qa_client_output_check_fn)(void *ctx, const void *field);

/* Fills out[0..count); out must hold count entries. */
void qa_read_mod_client_outputs(
    const qa_mod_client_output_declaration *declarations, size_t count,
    qa_client_output_scalar_fn scalar, qa_client_output_vector_fn vector,
    void *read_ctx, qa_mod_client_output *out);
void qa_validate_mod_client_outputs(
    const qa_mod_client_output_declaration *declarations, size_t count,
    qa_client_output_check_fn scalar, qa_client_output_check_fn vector,
    void *check_ctx);

typedef qa_mod_client_output_lease (*qa_client_output_claim_fn)(
    void *ctx, qa_provider_id owner,
    const qa_mod_client_output_channel *channels, size_t channel_count);
typedef double (*qa_source_output_scalar_fn)(void *ctx, qa_actor_id actor,
                                             const void *field);
typedef qa_vec3 (*qa_source_output_vector_fn)(void *ctx, qa_actor_id actor,
                                              const void *field);

/** TS SourceModClientOutputs<S, V>. Declarations are borrowed. */
typedef struct qa_source_mod_client_outputs qa_source_mod_client_outputs;
/* has_claim false is the TS undefined claim. NULL only on OOM. */
qa_source_mod_client_outputs *qa_source_mod_client_outputs_create(
    qa_provider_id owner,
    const qa_mod_client_output_declaration *declarations, size_t count,
    bool has_claim, qa_client_output_claim_fn claim, void *claim_ctx,
    qa_source_output_scalar_fn scalar, qa_source_output_vector_fn vector,
    void *read_ctx);
void qa_source_mod_client_outputs_destroy(
    qa_source_mod_client_outputs *outputs);
bool qa_source_mod_client_outputs_enabled(
    const qa_source_mod_client_outputs *outputs);
bool qa_source_mod_client_outputs_has(
    const qa_source_mod_client_outputs *outputs, qa_actor_id actor);
void qa_source_mod_client_outputs_publish(
    qa_source_mod_client_outputs *outputs, qa_actor_id actor);
void qa_source_mod_client_outputs_release(
    qa_source_mod_client_outputs *outputs, qa_actor_id actor);
void qa_source_mod_client_outputs_clear(
    qa_source_mod_client_outputs *outputs);
void qa_source_mod_client_outputs_close(
    qa_source_mod_client_outputs *outputs);

#endif
