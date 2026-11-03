/* Port of src/compat/qc/quakeworld-presentation.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/quakeworld_presentation.h"
#include <string.h>
#include "qa/compat/qc/message_effects.h"
#include "qa/foundation/support.h"
static qa_netquake_message to_nq(qa_quakeworld_message m) {
  qa_netquake_message r; memset(&r, 0, sizeof r);
  r.kind = m.kind; r.text = m.text; r.entity = m.entity; r.slot = m.slot; r.track = m.track;
  r.angles = m.angles; r.origin = m.origin; r.index = m.index; r.value = m.value;
  r.channel = m.channel; r.volume = m.volume; r.attenuation = m.attenuation; r.armor = m.armor; r.blood = m.blood;
  r.effect = m.effect; r.paused = m.paused; r.count = m.count;
  r.source = m.source; r.str_value = m.str_value; r.state = m.state;
  return r;
}
void qa_compat_qc_present_quakeworld_message(qa_compat_qc_routed_message entry, const qa_actor_id *target, qa_compat_qc_local_messages state, qa_compat_qc_local_message_host host) {
  qa_quakeworld_message *message = &entry.message;
  const char *k = message->kind;
  if (strcmp(k, "print") == 0) {
    qa_network_event e; memset(&e, 0, sizeof e); e.kind = QA_NETWORK_EVENT_PRINT; e.level = message->level; e.text = message->text;
    host.message(host.ctx, e, target); return;
  }
  if (strcmp(k, "kick") == 0) return;
  if (strcmp(k, "temporary-entity") == 0) {
    host.emit(host.ctx, qa_compat_qc_quake_temporary_event(message->effect, entry.has_actor ? &entry.actor : NULL, true), NULL); return;
  }
  if (strcmp(k, "intermission") == 0) {
    if (target) {
      qa_q1_event e; memset(&e,0,sizeof e); e.kind="intermission"; e.has_origin=true; e.origin=message->origin; e.angles=message->angles; e.map=host.map; e.exit_after=host.seconds+5; e.track=0;
      host.emit(host.ctx, e, target);
    } else for (size_t i = 0; i < host.recipient_count; i++) {
      qa_q1_event e; memset(&e,0,sizeof e); e.kind="intermission"; e.has_origin=true; e.origin=message->origin; e.angles=message->angles; e.map=host.map; e.exit_after=host.seconds+5; e.track=0;
      host.emit(host.ctx, e, &host.recipients[i]);
    }
    qa_netquake_message im; memset(&im,0,sizeof im); im.kind="intermission";
    state.receive(state.ctx, &im, 1, target, NULL); return;
  }
  if (strcmp(k, "cd-track") == 0) {
    qa_netquake_message track; memset(&track,0,sizeof track); track.kind="cd-track"; track.track=message->track; track.loop_track=message->track;
    state.receive(state.ctx, &track, 1, target, NULL);
    host.present_local(host.ctx, track, target, state, NULL); return;
  }
  if (strcmp(k, "sound") == 0 || strcmp(k, "stop-sound") == 0) {
    if (!entry.has_actor) qa_range_error("QW %s had no owned actor when written", k);
    host.present_local(host.ctx, to_nq(*message), target, state, &entry.actor); return;
  }
  if (strcmp(k, "muzzle-flash") == 0) {
    if (!entry.has_actor) qa_range_error("QW muzzle flash had no owned actor when written");
    qa_vec3 mo, ma;
    if (host.muzzle(host.ctx, entry.actor, &mo, &ma)) {
      qa_q1_event e; memset(&e,0,sizeof e); e.kind="effect"; e.effect="muzzleflash"; e.has_actor=true; e.actor=entry.actor;
      e.has_origin=true; e.origin=mo; e.amount=1; e.has_muzzle=true; e.muzzle_origin=mo; e.muzzle_angles=ma;
      host.emit(host.ctx, e, NULL);
    }
    return;
  }
  if (strcmp(k, "set-view") == 0) {
    qa_netquake_message sv = to_nq(*message);
    state.receive(state.ctx, &sv, 1, target, entry.has_actor ? &entry.actor : NULL); return;
  }
  static const char *const passthrough[] = {"nop","stat","stufftext","center-print","finale","set-angle","light-style","static","static-sound","damage","pause","killed-monster","found-secret","sell-screen"};
  for (size_t i = 0; i < sizeof passthrough / sizeof *passthrough; i++) if (strcmp(k, passthrough[i]) == 0) {
    qa_netquake_message msg = to_nq(*message);
    state.receive(state.ctx, &msg, 1, target, NULL);
    host.present_local(host.ctx, msg, target, state, NULL); return;
  }
  qa_range_error("QW component service %s requires a destination owner binding", k);
}
