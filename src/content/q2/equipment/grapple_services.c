/* Port of src/content/q2/equipment/grapple-services.ts. GPL-2.0-or-later. */
#include "qa/content/q2/equipment/grapple_services.h"
#include "qa/foundation/support.h"

qa_body_state qa_grapple_body(qa_actor_id a, qa_q2_game_services *g) {
  qa_body_state_result r = qa_shared_body_table_read(g->host->bodies, a);
  if (!r.has_value) qa_range_error("Grapple owner requires an existing shared body");
  return r.value;
}
void qa_grapple_velocity(qa_actor_id a, qa_q2_game_services *g, qa_vec3 v) {
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(g->host->actors, a, &owned)) return;
  qa_body_state body = qa_grapple_body(a, g);
  body.velocity = v;
  qa_shared_body_table_write(g->host->bodies, owned, body);
  qa_shared_body_table_link(g->host->bodies, owned, NULL);
}
void qa_ctf_grapple_state_init(qa_ctf_grapple_state *s) {
  *s = (qa_ctf_grapple_state){.grapple_state = "fly"};
}
void qa_lmctf_grapple_state_init(qa_lmctf_grapple_state *s) { *s = (qa_lmctf_grapple_state){0}; }

qa_ctf_grapple_checkpoint qa_capture_ctf_grapple(qa_ctf_grapple_state s) {
  qa_ctf_grapple_checkpoint out = {0};
  out.has_grapple = s.has_grapple;
  if (s.has_grapple) out.grapple = (qa_saved_actor_id){(double)s.grapple.slot, (double)s.grapple.generation};
  out.grapple_state = s.grapple_state;
  out.grapple_release_time = s.grapple_release_time;
  out.has_no_knockback = s.has_no_knockback;
  out.grapple_no_knockback = s.grapple_no_knockback;
  return out;
}
qa_lmctf_grapple_checkpoint qa_capture_lmctf_grapple(qa_lmctf_grapple_state s) {
  qa_lmctf_grapple_checkpoint out = {0};
  out.has_hook = s.has_hook;
  if (s.has_hook) out.hook = (qa_saved_actor_id){(double)s.hook.slot, (double)s.hook.generation};
  out.hook_state = s.hook_state;
  out.hook_length = s.hook_length;
  out.hook_held = s.hook_held;
  return out;
}
qa_ctf_grapple_state qa_restore_ctf_grapple(qa_ctf_grapple_checkpoint s, qa_q2_game_services *g) {
  qa_ctf_grapple_state out = {0};
  out.has_grapple = s.has_grapple;
  if (s.has_grapple) out.grapple = qa_session_actor_registry_reference_saved(g->host->actors, s.grapple, "checkpoint");
  out.grapple_state = s.grapple_state;
  out.grapple_release_time = s.grapple_release_time;
  out.has_no_knockback = s.has_no_knockback;
  out.grapple_no_knockback = s.grapple_no_knockback;
  return out;
}
qa_lmctf_grapple_state qa_restore_lmctf_grapple(qa_lmctf_grapple_checkpoint s, qa_q2_game_services *g) {
  qa_lmctf_grapple_state out = {0};
  out.has_hook = s.has_hook;
  if (s.has_hook) out.hook = qa_session_actor_registry_reference_saved(g->host->actors, s.hook, "checkpoint");
  out.hook_state = s.hook_state;
  out.hook_length = s.hook_length;
  out.hook_held = s.hook_held;
  return out;
}
