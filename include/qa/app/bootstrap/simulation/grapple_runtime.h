/* Port of src/app/bootstrap/simulation/grapple-runtime.ts. GPL-2.0-or-later.
 * Input and tether state reference the same session actors and body
 * table as the primary weapon. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_GRAPPLE_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_GRAPPLE_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/qvm_grapple_source.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/app/bootstrap/simulation/weapon_slot.h"
#include "qa/content/q1/equipment/threewave_grapple.h"
#include "qa/content/q1/equipment/threewave_weapon.h"
#include "qa/content/q1/foundation/checkpoint.h"
#include "qa/content/q2/equipment/ctf_grapple.h"
#include "qa/content/q2/equipment/grapple_services.h"
#include "qa/content/q2/equipment/grapple_weapon.h"
#include "qa/content/q2/equipment/lmctf_grapple.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/entity_services.h"
#include "qa/contracts/content.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/q1_foundation.h"
/** TS GrappleSource union on kind ("q1-threewave" | "q2-ctf" |
 * "q2-lmctf" | "q3-qvm"); only the members of `kind` are set. */
typedef struct qa_app_sim_grapple_source {
  const char *kind;
  qa_q1_services *q1_game; qa_q1_threewave_grapple *q1_core;
  qa_q2_entity_services *q2_game; qa_q2_ctf_grapple_equipment *ctf_core; qa_lmctf_grapple_equipment *lmctf_core;
  qa_app_sim_qvm_grapple_source *q3_game; qa_qvm_grapple_provider *q3_core;
} qa_app_sim_grapple_source;
/** TS GrappleControl. teleportBit null is has_teleport_bit false. */
typedef struct qa_app_sim_grapple_control {
  bool has_teleport_bit; double teleport_bit;
  bool jump, held, pressed, released;
  qa_vec3 previous_velocity;
  bool prediction_suppressed;
  /* C-only key order: false is the admit() literal { teleportBit, jump, held, pressed, released, previousVelocity,
   * predictionSuppressed }; true is readGrappleRuntimeCheckpoint's { held, jump, teleportBit, ... }, which a restored
   * control keeps through its { ...state, field } updates. */
  bool read_order;
} qa_app_sim_grapple_control;
/** TS GrappleSlotHost. presentation's kick member is ignored (TS
 * Omit<GrappleWeaponPresentation, "kick">); its other members run with
 * the returned presentation ctx. */
typedef struct qa_app_sim_grapple_slot_host {
  void *ctx;
  bool (*selected)(void *ctx, qa_actor_id actor);
  bool (*available)(void *ctx, qa_actor_id actor);
  void (*frame)(void *ctx, qa_actor_id actor, double frame);
  qa_grapple_weapon_presentation (*presentation)(void *ctx, qa_actor_id actor);
} qa_app_sim_grapple_slot_host;
/** TS GrappleWeaponAnimation. */
typedef struct qa_app_sim_grapple_weapon_animation {
  qa_grapple_weapon_state state;
  double next_frame_at;
  qa_vec3 kick_origin;
  double kick_pitch;
  /* C-only key order of state: false is createGrappleWeaponState()'s { animation, handoff }; true is
   * readGrappleWeaponState's { handoff, animation } (the weapon mutates it in place, so it keeps that order). */
  bool state_read_order;
} qa_app_sim_grapple_weapon_animation;
typedef struct qa_app_sim_grapple_saved_animation { qa_saved_actor_id actor; qa_app_sim_grapple_weapon_animation animation; } qa_app_sim_grapple_saved_animation;
typedef struct qa_app_sim_grapple_saved_control { qa_saved_actor_id actor; qa_app_sim_grapple_control control; } qa_app_sim_grapple_saved_control;
typedef struct qa_app_sim_grapple_saved_ctf { qa_saved_actor_id actor; qa_ctf_grapple_checkpoint state; } qa_app_sim_grapple_saved_ctf;
typedef struct qa_app_sim_grapple_saved_lmctf { qa_saved_actor_id actor; qa_lmctf_grapple_checkpoint state; } qa_app_sim_grapple_saved_lmctf;
/** TS GrappleRuntimeCheckpoint["source"] on kind. q1-threewave carries
 * the content checkpoint when captured (q1_entities, freed by
 * qa_q1_checkpoint_free) or the persistence form when read
 * (q1_saved: has_q1_saved). Q2 entities borrow from q2_owner when
 * read, or from the source game's capture storage when captured. */
typedef struct qa_app_sim_grapple_source_checkpoint {
  const char *kind;
  qa_q1_foundation_checkpoint q1_entities;
  bool has_q1_saved; qa_persist_q1_foundation_checkpoint q1_saved;
  qa_q2_foundation_checkpoint q2_entities;
  qa_persist_decoded q2_owner;
  qa_app_sim_grapple_saved_ctf *ctf_states; size_t ctf_state_count;
  qa_app_sim_grapple_saved_lmctf *lmctf_states; size_t lmctf_state_count;
  qa_app_sim_qvm_grapple_source_checkpoint component;
  qa_saved_actor_id *holstered; size_t holstered_count;
} qa_app_sim_grapple_source_checkpoint;
/** TS GrappleRuntimeCheckpoint. Arrays are owned. */
typedef struct qa_app_sim_grapple_runtime_checkpoint {
  double version;
  qa_app_sim_grapple_saved_animation *weapon_animations; size_t weapon_animation_count;
  qa_app_sim_grapple_saved_control *controls; size_t control_count;
  qa_app_sim_source_random_state random;
  qa_app_sim_grapple_source_checkpoint source;
} qa_app_sim_grapple_runtime_checkpoint;
void qa_app_sim_grapple_runtime_checkpoint_destroy(qa_app_sim_grapple_runtime_checkpoint *checkpoint);
/** TS GrappleRuntime. selection must be enabled; the source, random and
 * slot host (NULL is the TS null) are borrowed. Handoff contexts and
 * retired Q2 weapon animations stay valid until destroy, as the TS
 * closures keep them alive. */
typedef struct qa_app_sim_grapple_runtime qa_app_sim_grapple_runtime;
qa_app_sim_grapple_runtime *qa_app_sim_grapple_runtime_create(qa_grapple_selection selection,
    qa_app_sim_grapple_source source, qa_app_sim_source_random *random, const qa_app_sim_grapple_slot_host *slot_host);
void qa_app_sim_grapple_runtime_destroy(qa_app_sim_grapple_runtime *runtime);
const qa_grapple_selection *qa_app_sim_grapple_runtime_selection(const qa_app_sim_grapple_runtime *runtime);
const qa_app_sim_grapple_source *qa_app_sim_grapple_runtime_source(const qa_app_sim_grapple_runtime *runtime);
void qa_app_sim_grapple_runtime_admit(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor);
qa_weapon_reference qa_app_sim_grapple_runtime_weapon(const qa_app_sim_grapple_runtime *runtime);
qa_app_sim_equipment_weapon_handoff qa_app_sim_grapple_runtime_handoff(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor);
/* False is the TS null. */
bool qa_app_sim_grapple_runtime_weapon_view(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor,
                                            qa_app_sim_grapple_weapon_view *out);
void qa_app_sim_grapple_runtime_released(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor);
void qa_app_sim_grapple_runtime_observe_teleport(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor, double bit);
bool qa_app_sim_grapple_runtime_jump(const qa_app_sim_grapple_runtime *runtime, qa_actor_id actor);
void qa_app_sim_grapple_runtime_set_jump(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor, bool jump);
bool qa_app_sim_grapple_runtime_held(const qa_app_sim_grapple_runtime *runtime, qa_actor_id actor);
void qa_app_sim_grapple_runtime_input(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor, bool held);
qa_vec3 qa_app_sim_grapple_runtime_previous_velocity(const qa_app_sim_grapple_runtime *runtime, qa_actor_id actor);
void qa_app_sim_grapple_runtime_set_previous_velocity(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor, qa_vec3 velocity);
bool qa_app_sim_grapple_runtime_prediction(const qa_app_sim_grapple_runtime *runtime, qa_actor_id actor);
void qa_app_sim_grapple_runtime_set_prediction(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor, bool suppressed);
/* False is the TS null. */
bool qa_app_sim_grapple_runtime_hook(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor, qa_actor_id *out);
bool qa_app_sim_grapple_runtime_pulling(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor);
double qa_app_sim_grapple_runtime_gravity_scale(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor);
void qa_app_sim_grapple_runtime_release(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor);
void qa_app_sim_grapple_runtime_step(qa_app_sim_grapple_runtime *runtime, qa_actor_id actor, bool enabled);
qa_app_sim_grapple_runtime_checkpoint qa_app_sim_grapple_runtime_capture(qa_app_sim_grapple_runtime *runtime);
void qa_app_sim_grapple_runtime_restore(qa_app_sim_grapple_runtime *runtime,
                                        const qa_app_sim_grapple_runtime_checkpoint *checkpoint);
#endif
