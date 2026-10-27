/* Port of src/compat/q2/native-mod-entries.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_entries.h"
static void observe_cb(void *ctx) { qa_compat_q2_native_mod_entry_binding *b=ctx; if (b->has_frame) b->frame_stack=b->host.read_rsp(b->host.ctx); }
void qa_compat_q2_bind_native_mod_entry(qa_compat_q2_native_mod_entry_binding *out, qa_compat_q2_native_mod_entry_host host, qa_guest_address address, qa_callback_id id, qa_compat_q2_guest_call_signature signature) {
  out->host=host; out->address=address; out->signature=signature; out->has_frame=false; out->closed=false;
  if (host.observe_entry) host.observe_entry(host.ctx, address, out, observe_cb);
  (void)id;
}
qa_guest_call_result qa_compat_q2_native_mod_entry_original(qa_compat_q2_native_mod_entry_binding *b, const qa_guest_call_value *v, size_t n) {
  b->has_frame=true; qa_guest_call_result r=b->host.invoke(b->host.ctx,b->address,&b->signature,v,n); b->has_frame=false; return r;
}
void qa_compat_q2_native_mod_entry_close(qa_compat_q2_native_mod_entry_binding *b) {
  if (b->closed) return;
  b->closed=true; if (b->host.unbind) b->host.unbind(b->host.ctx); if (b->host.unobserve) b->host.unobserve(b->host.ctx);
}
