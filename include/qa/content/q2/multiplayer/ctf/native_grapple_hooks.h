/* Port of src/content/q2/multiplayer/ctf/native-grapple-hooks.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_CTF_NATIVE_GRAPPLE_HOOKS_H
#define QA_CONTENT_Q2_MULTIPLAYER_CTF_NATIVE_GRAPPLE_HOOKS_H
#include "qa/content/q2/equipment/grapple_services.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
typedef struct qa_q2_ctf_native_grapple_ctx {
  const qa_q2_ctf_hooks *hooks;
  void (*emit_cable)(void *emit_ctx, qa_grapple_cable_event event);
  void *emit_ctx;
  bool weapon_aim;
} qa_q2_ctf_native_grapple_ctx;
/* emit_cable receives the TS grapple-cable event; the CTF/LMCTF
 * grapple wrappers pass one that forwards to their hooks.emit. */
qa_grapple_hooks qa_native_grapple_hooks(qa_q2_ctf_native_grapple_ctx *ctx);
#endif
