/* Port of src/compat/q2/native-mod-entries.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_ENTRIES_H
#define QA_COMPAT_Q2_NATIVE_MOD_ENTRIES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary_reader.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"

/* TS NativeModHost (app/bootstrap, unlanded) mirrored as fn+ctx.
 * TS owner: app/bootstrap/simulation/native-mod-host.ts. */
typedef struct qa_compat_q2_native_mod_entry_host {
  void *ctx; int64_t pointer_bytes;
  uint64_t (*read_rsp)(void *ctx);
  qa_guest_call_result (*invoke)(void *ctx, qa_guest_address a, const qa_compat_q2_guest_call_signature *sig, const qa_guest_call_value *v, size_t n);
  void (*observe_entry)(void *ctx, qa_guest_address a, void *cb_ctx, void (*cb)(void *));
  void (*unobserve)(void *ctx);
  void (*bind_entry)(void *ctx, qa_guest_address a, qa_callback_id id, const qa_compat_q2_guest_call_signature *sig, void *cb_ctx, qa_guest_call_result (*cb)(void *, const qa_guest_call_value *, size_t));
  void (*unbind)(void *ctx);
} qa_compat_q2_native_mod_entry_host;
typedef struct qa_compat_q2_native_mod_entry_binding {
  qa_compat_q2_native_mod_entry_host host; qa_guest_address address; qa_compat_q2_guest_call_signature signature;
  bool has_frame; uint64_t frame_stack; bool closed;
} qa_compat_q2_native_mod_entry_binding;
void qa_compat_q2_bind_native_mod_entry(qa_compat_q2_native_mod_entry_binding *out, qa_compat_q2_native_mod_entry_host host, qa_guest_address address, qa_callback_id id, qa_compat_q2_guest_call_signature signature);
qa_guest_call_result qa_compat_q2_native_mod_entry_original(qa_compat_q2_native_mod_entry_binding *b, const qa_guest_call_value *v, size_t n);
void qa_compat_q2_native_mod_entry_close(qa_compat_q2_native_mod_entry_binding *b);
#endif
