/*
 * Port of src/compat/qvm/ui-key-syscalls.ts. The TS invokeAsync of
 * UI_HASUNIQUECDKEY becomes the synchronous invoke_async callback on
 * the mirrored syscall; its validate callback re-enters
 * assert_current. False means another service owns the trap.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_UI_KEY_SYSCALLS_H
#define QA_COMPAT_QVM_UI_KEY_SYSCALLS_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/core/q3_cd_key.h"

/* TS QvmUiKeyServices. write_ui replaces the TS keys.writeUi (its
 * refresh semantics live with the caller); read_ui runs over the
 * landed cd-key state through guest-write callbacks. */
typedef struct qa_qvm_ui_key_services {
  const qa_q3_cd_key_state *keys;
  void *ctx;
  void (*write_ui)(void *ctx, int64_t unique, const char *directory, const uint8_t *source, size_t length);
  const char *(*game_directory)(void *ctx);
  void (*assert_current)(void *ctx);
} qa_qvm_ui_key_services;

bool qa_qvm_ui_key_syscall(const qa_qvm_host_call *call, const qa_qvm_ui_key_services *services, int32_t *out);

#endif
