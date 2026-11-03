/* Port of src/compat/qvm/grapple-provider.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_GRAPPLE_PROVIDER_H
#define QA_COMPAT_QVM_GRAPPLE_PROVIDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/grapple_profile.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"

/* TS QvmGrappleProjection. target/mover are TS ActorId | null. */
typedef struct qa_qvm_grapple_projection {
  double hook;
  qa_vec3 origin;
  qa_vec3 velocity;
  bool has_target;
  qa_actor_id target;
  bool has_mover;
  qa_actor_id mover;
  bool pulling;
  qa_vec3 point;
  qa_vec3 owner_velocity;
} qa_qvm_grapple_projection;

/* TS QvmGrappleBridge. entity/actor false returns are the TS null. */
typedef struct qa_qvm_grapple_bridge {
  void *ctx;
  void (*synchronize)(void *ctx);
  bool (*entity)(void *ctx, qa_actor_id actor, double *out);
  bool (*actor)(void *ctx, double pointer, qa_actor_id *out);
  qa_actor_id (*restore_actor)(void *ctx, qa_saved_actor_id saved);
  /* projection NULL retires the tether (TS publish(owner, null)). */
  void (*publish)(void *ctx, qa_actor_id owner, const qa_qvm_grapple_projection *projection);
  void (*velocity)(void *ctx, qa_actor_id owner, qa_vec3 velocity);
  double scratch_word;
  double scratch_byte_length;
} qa_qvm_grapple_bridge;

/* TS QvmGrappleCheckpoint. `profile` is borrowed from the provider
 * that captured it; `owners` is malloc'd by capture and freed with
 * qa_qvm_grapple_checkpoint_destroy (which also releases `module`). */
typedef struct qa_qvm_grapple_checkpoint {
  double version; /* always 1 */
  const char *profile;
  qa_guest_checkpoint module;
  qa_saved_actor_id *owners;
  size_t owner_count;
} qa_qvm_grapple_checkpoint;

typedef struct qa_qvm_grapple_provider qa_qvm_grapple_provider;

/*
 * NULL only on OOM. `game`/`artifact` are borrowed. As in TS, the
 * constructor re-reads the profile through its declaration, aborting on
 * a declaration the artifact rejects. The caller's profile is copied by
 * value (its list arrays stay borrowed, as with qa_qvm_grapple_profile).
 */
qa_qvm_grapple_provider *qa_qvm_grapple_provider_create(qa_qvm_game *game,
                                                        const qa_resolved_qvm_artifact *artifact,
                                                        qa_qvm_grapple_profile profile,
                                                        qa_qvm_grapple_bridge bridge);
void qa_qvm_grapple_provider_destroy(qa_qvm_grapple_provider *provider);
const qa_qvm_grapple_profile *qa_qvm_grapple_provider_get_profile(
    const qa_qvm_grapple_provider *provider);

void qa_qvm_grapple_provider_begin_frame(qa_qvm_grapple_provider *provider,
                                         double time_milliseconds, double frame);
void qa_qvm_grapple_provider_fire(qa_qvm_grapple_provider *provider, qa_actor_id actor);
void qa_qvm_grapple_provider_release(qa_qvm_grapple_provider *provider, qa_actor_id actor,
                                     bool force);
void qa_qvm_grapple_provider_step(qa_qvm_grapple_provider *provider);
/* False (with *out unset) is the TS null. */
bool qa_qvm_grapple_provider_pull(qa_qvm_grapple_provider *provider, qa_actor_id actor,
                                  qa_vec3 forward, qa_vec3 *out);
void qa_qvm_grapple_provider_mover_moved(qa_qvm_grapple_provider *provider, qa_actor_id actor,
                                         qa_vec3 translation);
void qa_qvm_grapple_provider_actor_released(qa_qvm_grapple_provider *provider,
                                            qa_actor_id actor);
void qa_qvm_grapple_provider_capture(qa_qvm_grapple_provider *provider,
                                     qa_qvm_grapple_checkpoint *out);
void qa_qvm_grapple_checkpoint_destroy(qa_qvm_grapple_checkpoint *checkpoint);
void qa_qvm_grapple_provider_restore(qa_qvm_grapple_provider *provider,
                                     const qa_qvm_grapple_checkpoint *checkpoint);
void qa_qvm_grapple_provider_close(qa_qvm_grapple_provider *provider);

#endif
