/* Port of src/compat/qc/message-effects.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/message_effects.h"
#include <string.h>
#include "qa/foundation/support.h"
qa_q1_event qa_compat_qc_quake_temporary_event(qa_temporary_entity e, const qa_actor_id *actor, bool qw) {
  qa_q1_event r; memset(&r, 0, sizeof r);
  if (e.kind == QA_TE_EXPLOSION_COLORS) { r.kind="colored-explosion"; r.has_origin=true; r.origin=e.origin; r.color_start=e.color_start; r.color_length=e.color_length; return r; }
  if (e.kind == QA_TE_BEAM) {
    if (!actor) qa_range_error("QC beam entity %g had no owned actor when written", e.entity);
    const char *style = e.type==5?"lightning1":e.type==6?"lightning2":e.type==9?"lightning3":(e.type==13&&!qw)?"grapple":NULL;
    if (!style) qa_range_error("Unsupported QC beam %g", e.type);
    r.kind="beam"; r.beam_style=style; r.has_actor=true; r.actor=*actor; r.start=e.start; r.end=e.end; return r;
  }
  if (qw && (e.type==2||e.type==12||e.type==13)) {
    r.kind="particles"; r.has_origin=true; r.origin=e.origin; r.direction=(qa_vec3){0,0,0};
    r.color = e.type==2?0:e.type==12?73:225; r.count = e.type==13?50:20*e.count; return r;
  }
  const char *name;
  if(e.type==0)name="spike"; else if(e.type==1)name="superspike"; else if(e.type==2)name="gunshot";
  else if(e.type==3)name="explosion"; else if(e.type==4)name="tar-explosion"; else if(e.type==7)name="wizard-spike";
  else if(e.type==8)name="knight-spike"; else if(e.type==10)name="lava-splash"; else if(e.type==11)name="teleport";
  else qa_range_error("Unsupported QC point effect %g", e.type);
  r.kind="effect"; r.effect=name; r.has_origin=true; r.origin=e.origin; r.amount=e.count; return r;
}
