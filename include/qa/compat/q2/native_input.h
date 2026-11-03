/* Port of src/compat/q2/native-input.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_INPUT_H
#define QA_COMPAT_Q2_NATIVE_INPUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/classic/host.h"
#include "qa/compat/q2/rerelease/module.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/time.h"
#include "qa/movement/body_shape.h"
#include "qa/world/session/mod_client_applications.h"

/* TS NativeInputTouch: a guest edict slot bumped during the move and the plane of the contact. */
typedef struct qa_compat_q2_native_input_touch { double slot; qa_plane plane; } qa_compat_q2_native_input_touch;
/* TS NativeInputMotionOutputs. has_ground_slot false / has_ground_plane false are TS null. */
typedef struct qa_compat_q2_native_input_motion_outputs {
  bool grounded; bool has_ground_slot; double ground_slot; bool ducked; double water_level, water_type, view_height;
  qa_vec3 view_angles; qa_bounds bounds; const qa_compat_q2_native_input_touch *touches; size_t touch_count;
  bool has_ground_plane; qa_plane ground_plane;
} qa_compat_q2_native_input_motion_outputs;
qa_q2_user_command qa_compat_q2_read_classic_user_command(const uint8_t *view);
/** TS NativeInputMotion: Pmove projection passed to services.movement. */
typedef struct qa_compat_q2_native_input_motion_state { qa_vec3 origin, velocity; } qa_compat_q2_native_input_motion_state;
typedef struct qa_compat_q2_native_input_motion_view { qa_vec3 view_offset; bool crouched; } qa_compat_q2_native_input_motion_view;
typedef struct qa_compat_q2_native_input_motion {
  void *ctx;
  qa_compat_q2_native_input_motion_state (*read)(void *ctx);
  bool (*grounded)(void *ctx);
  qa_compat_q2_native_input_motion_view (*view)(void *ctx);
  void (*write)(void *ctx, qa_compat_q2_native_input_motion_state value);
  void (*write_outputs)(void *ctx, const qa_compat_q2_native_input_motion_outputs *outputs);
} qa_compat_q2_native_input_motion;
typedef void (*qa_compat_q2_native_input_release_listener)(void *ctx, qa_actor_id actor);
/** TS NativeInputServices. body_bounds, client_outputs and original_command may be
 * NULL (TS optional); body_bounds false and client_outputs NULL are the TS null. */
typedef struct qa_compat_q2_native_input_services {
  qa_mod_client_applications *applications;
  const qa_numeric_operations *numeric;
  void *ctx;
  bool (*body_bounds)(void *ctx, qa_actor_id actor, qa_bounds *out);
  const qa_mod_client_movement_outputs *(*client_outputs)(void *ctx, qa_actor_id actor);
  bool (*identity)(void *ctx, double slot, qa_mod_client_identity *out);
  bool (*live)(void *ctx, qa_mod_client_identity identity);
  const qa_mod_client_command_receipt *(*accepted)(void *ctx, qa_actor_id actor);
  qa_frame_context (*frame)(void *ctx);
  uint64_t (*on_release)(void *ctx, qa_compat_q2_native_input_release_listener listener, void *listener_ctx);
  void (*remove_release)(void *ctx, uint64_t handle);
  void (*retired)(void *ctx, qa_mod_client_identity identity);
  qa_user_command (*original_command)(void *ctx, qa_mod_client_identity identity, qa_user_command command);
  qa_guest_call_result (*movement)(void *ctx, qa_mod_client_identity identity, const qa_compat_q2_native_input_motion *projection,
                                   qa_user_command command, qa_vec3 aim, void *run_ctx, qa_guest_call_result (*run)(void *run_ctx));
} qa_compat_q2_native_input_services;

/** TS NativeInputSource: the edition's host surface NativeInputBinding reads. The
 * _classic/_rerelease constructors below fill it from the real guest hosts. */
typedef struct qa_compat_q2_native_input_source {
  bool classic;
  qa_compat_q2_memory *memory;
  qa_compat_q2_runner *runner;
  void *host;
  void *retire_ctx;
  void (*retire)(void *retire_ctx, double slot, qa_mod_client_identity identity);
  /* Address and signature of the live export named name ("ClientThink"). */
  void (*entry)(void *host, const char *name, qa_guest_address *out_address, qa_compat_q2_guest_call_signature *out_signature);
  qa_guest_call_result (*invoke)(void *host, qa_guest_address address, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count);
  qa_raw_entity_view (*from_pointer)(void *host, qa_guest_address address);
  qa_raw_entity_view (*at_slot)(void *host, double slot);
  /* Classic client prefix view; NULL is the TS null. */
  uint8_t *(*client_prefix)(void *host, double slot);
  uint64_t (*bind_input_movement)(void *host, void *ctx, void (*boundary)(void *ctx, qa_guest_address address, void *run_ctx, void (*run)(void *, const qa_movement_body_shape *)),
                                  void *active_ctx, bool (*active)(void *));
  void (*unbind_input_movement)(void *host, uint64_t handle);
} qa_compat_q2_native_input_source;
qa_compat_q2_native_input_source qa_compat_q2_native_input_source_classic(qa_compat_q2_classic_guest_host *host, void *retire_ctx, void (*retire)(void *retire_ctx, double slot, qa_mod_client_identity identity));
qa_compat_q2_native_input_source qa_compat_q2_native_input_source_rerelease(qa_compat_q2_rerelease_guest_module *host, void *retire_ctx, void (*retire)(void *retire_ctx, double slot, qa_mod_client_identity identity));

typedef struct qa_compat_q2_native_input_binding qa_compat_q2_native_input_binding;
qa_compat_q2_native_input_binding *qa_compat_q2_native_input_binding_create(qa_compat_q2_native_input_source source, qa_compat_q2_native_input_services services);
/* TS close(): removes the bindings. */
void qa_compat_q2_native_input_binding_close(qa_compat_q2_native_input_binding *s);
void qa_compat_q2_native_input_binding_destroy(qa_compat_q2_native_input_binding *s);
#endif
