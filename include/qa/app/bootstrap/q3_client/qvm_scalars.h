/* Port of src/app/bootstrap/q3-client/qvm-scalars.ts. The
 * updateScreen promise is synchronous; REAL_TIME uses localtime.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_QVM_SCALARS_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_QVM_SCALARS_H
#include <stdbool.h>
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/app/bootstrap/q3_client/qvm_display.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/contracts/math.h"
#include "qa/contracts/ui.h"
#include "qa/input/bindings.h"
#include "qa/materials/q3_lighting.h"
/* TS QvmClientInput (BindingCommandSeat + isDown + clearStates). */
typedef struct qa_app_qvm_client_input {
  qa_binding_command_seat seat;
  bool (*is_down)(void *ctx, const qa_physical_input *input);
  void (*clear_states)(void *ctx);
  void *state_ctx;
} qa_app_qvm_client_input;
/* TS qvmClientInputSyscall: false is the TS null. */
bool qa_app_qvm_client_input_syscall(const qa_qvm_host_call *call, qa_app_qvm_client_input input,
                                     qa_qvm_host_result *out);
/* TS clientState() reply. */
typedef struct qa_app_qvm_client_state_info {
  double phase, connect_packet_count, client_number;
  const char *server_name;
  const char *message;
} qa_app_qvm_client_state_info;
/* TS QvmApplicationScalarOptions. */
typedef struct qa_app_qvm_application_scalar_options {
  double backend_stencil_bits;
  const qa_app_gl_driver *driver;
  const qa_app_gl_config *gl_config;
  void *viewport_ctx;
  void (*viewport)(void *ctx, double *out_width, double *out_height);
  /* local.console.field overstrike accessors (LocalInput mirror). */
  bool (*overstrike_get)(void *ctx);
  void (*overstrike_set)(void *ctx, bool value);
  void *local_ctx;
  qa_app_qvm_client_input input;
  qa_app_application_q3_assets *media;
  qa_q3_pres_q3_renderer_resources *resources;
  double (*now)(void *ctx); void *now_ctx;
  double (*key_catcher_get)(void *ctx);
  void (*key_catcher_set)(void *ctx, double value);
  void *key_catcher_ctx;
  qa_app_qvm_client_state_info (*client_state)(void *ctx); void *client_state_ctx;
  qa_lighting_sample (*light_for_point)(void *ctx, qa_vec3 point); void *light_ctx;
  void (*assert_current)(void *ctx); void *assert_ctx;
} qa_app_qvm_application_scalar_options;
/* TS QvmApplicationScalars. */
typedef struct qa_app_qvm_application_scalars {
  qa_app_qvm_application_scalar_options options;
} qa_app_qvm_application_scalars;
void qa_app_qvm_application_scalars_init(qa_app_qvm_application_scalars *scalars,
                                         qa_app_qvm_application_scalar_options options);
/* TS dispatch(call, updateScreen): false is the TS null. */
bool qa_app_qvm_application_scalars_dispatch(qa_app_qvm_application_scalars *scalars,
    const qa_qvm_host_call *call, void (*update_screen)(void *ctx), void *update_screen_ctx,
    qa_qvm_host_result *out);
#endif
