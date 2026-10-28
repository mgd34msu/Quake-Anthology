/* Port of src/app/bootstrap/simulation/weapon-slot-checkpoint.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/weapon_slot_checkpoint.h"
#include <stdlib.h>
#include <string.h>
#include "qa/persistence/save_image.h"
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_save_reader_fail(r, "out of memory"); return o;
}
static const char *fchoice(const qa_save_reader *r, const char *n, const char *const *c, size_t k) {
  qa_save_reader f = fld(r, n); const char *v = qa_save_reader_choice_string(&f, c, k); qa_save_reader_destroy(&f); return v;
}
static const char *fnamespaced(const qa_save_reader *r, const char *n) {
  qa_save_reader f = fld(r, n); const char *v = qa_namespaced(&f); qa_save_reader_destroy(&f); return v;
}
static double fintmin(const qa_save_reader *r, const char *n, double m) {
  qa_save_reader f = fld(r, n); double v = qa_save_reader_integer_min(&f, m); qa_save_reader_destroy(&f); return v;
}
static bool fbool(const qa_save_reader *r, const char *n) {
  qa_save_reader f = fld(r, n); bool v = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f); return v;
}
static double ffinite(const qa_save_reader *r, const char *n) {
  qa_save_reader f = fld(r, n); double v = qa_save_reader_finite(&f); qa_save_reader_destroy(&f); return v;
}
static qa_weapon_reference read_reference(const qa_save_reader *reader) {
  qa_weapon_reference out; out.provider = fnamespaced(reader, "provider"); out.item = fnamespaced(reader, "item"); return out;
}
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
qa_app_sim_weapon_slot_restore_state qa_app_sim_read_weapon_slot_state(const qa_save_reader *reader) {
  static const char *const kinds[] = {"active", "switching", "activating", "primary", "equipment", "holstering-primary", "holstering-equipment"};
  const char *kind = fchoice(reader, "kind", kinds, 7);
  qa_app_sim_weapon_slot_restore_state out; memset(&out, 0, sizeof out); out.kind = kind;
  if (is(kind, "active")) { out.provider = fnamespaced(reader, "provider"); return out; }
  if (is(kind, "switching")) { out.from = fnamespaced(reader, "from"); qa_save_reader n = fld(reader, "next"); out.next = read_reference(&n); qa_save_reader_destroy(&n); return out; }
  if (is(kind, "activating")) { out.from = fnamespaced(reader, "from"); qa_save_reader n = fld(reader, "next"); out.next = read_reference(&n); qa_save_reader_destroy(&n); out.request = fintmin(reader, "request", 1); return out; }
  if (is(kind, "primary") || is(kind, "equipment")) return out;
  qa_save_reader n = fld(reader, "next"); out.next = read_reference(&n); qa_save_reader_destroy(&n); return out;
}
qa_app_sim_saved_weapon_slot *qa_app_sim_read_weapon_slots(const qa_save_reader *reader, size_t *out_count) {
  size_t count = qa_save_reader_list_count(reader);
  qa_app_sim_saved_weapon_slot *out = count ? malloc(count * sizeof *out) : NULL;
  if (count && !out) qa_save_reader_fail(reader, "out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, af, sf;
    if (!qa_save_reader_item(reader, i, &entry)) qa_save_reader_fail(reader, "out of memory");
    af = fld(&entry, "actor"); out[i].actor = qa_read_saved_actor(&af); qa_save_reader_destroy(&af);
    sf = fld(&entry, "state"); out[i].state = qa_app_sim_read_weapon_slot_state(&sf); qa_save_reader_destroy(&sf);
    qa_save_reader_destroy(&entry);
  }
  if (out_count) *out_count = count;
  return out;
}
qa_grapple_weapon_state qa_app_sim_read_grapple_weapon_state(const qa_save_reader *reader) {
  static const char *const handoffs[] = {"active", "holstering", "holstered"};
  static const char *const phases[] = {"activating", "ready", "firing", "dropping"};
  qa_save_reader state = fld(reader, "animation");
  qa_grapple_weapon_state out; memset(&out, 0, sizeof out);
  out.handoff = fchoice(reader, "handoff", handoffs, 3);
  out.animation.phase = fchoice(&state, "phase", phases, 4);
  out.animation.frame = fintmin(&state, "frame", 0);
  out.animation.latched_attack = fbool(&state, "latchedAttack");
  out.animation.source_firing = fbool(&state, "sourceFiring");
  out.animation.think_time = ffinite(&state, "thinkTime");
  out.animation.fire_finished = ffinite(&state, "fireFinished");
  out.animation.fire_buffered = fbool(&state, "fireBuffered");
  out.animation.last_firing_time = ffinite(&state, "lastFiringTime");
  qa_save_reader_destroy(&state);
  return out;
}
