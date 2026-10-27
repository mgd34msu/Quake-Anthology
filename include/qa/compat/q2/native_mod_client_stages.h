/* Port of src/compat/q2/native-mod-client-stages.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_CLIENT_STAGES_H
#define QA_COMPAT_Q2_NATIVE_MOD_CLIENT_STAGES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/protocol.h"
#include "qa/foundation/support.h"

typedef struct qa_compat_q2_native_mod_client_stages { void *host_ctx; bool active; } qa_compat_q2_native_mod_client_stages;
void qa_compat_q2_native_mod_client_stages_init(qa_compat_q2_native_mod_client_stages *s, void *host_ctx);
/* TS writeNativeUserCommand over a byte view (classic 16B; rerelease
 * delegated to write_rerelease fn+ctx when supplied). */
void qa_compat_q2_write_native_user_command(uint8_t *view, size_t len, qa_user_command command, void *rr_ctx, void (*write_rerelease)(void *, uint8_t *, size_t, qa_q2_rerelease_user_command));
/* TS nativeModUserCommand inputs (modClientInputValues mirrored). */
typedef struct qa_compat_q2_mod_command_inputs { double attack,jump,impulse,forward_move,side_move,up_move; bool has[6]; } qa_compat_q2_mod_command_inputs;
size_t qa_compat_q2_native_mod_user_command(uint8_t *out, bool rerelease, double frame, double elapsed_ms, qa_vec3 aim, qa_compat_q2_mod_command_inputs in, void *rr_ctx, void (*write_rerelease)(void *, uint8_t *, size_t, qa_q2_rerelease_user_command));
#endif
