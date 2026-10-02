#include "remote_q2_effects_private.h"
#include <stdlib.h>

struct frontend_remote_q2_effects_policy {
    frontend_remote_q2_effects *owner;
    qa_scene_resource_policy *bank;
    frontend_remote_q2_effects_source source;
    const qa_scene_image *particle, *white, *prepared_particle, *prepared_white;
    bool sealed, published;
};
static bool current(const frontend_remote_q2_effects_policy *t, bool parent)
{
    if (!t || !t->owner || t->owner->pending!=t || t->owner->busy ||
        t->owner->source.images!=t->source.images || t->owner->source.identity!=t->source.identity ||
        t->owner->source.content_generation!=t->source.content_generation ||
        t->owner->source.profile!=t->source.profile ||
        t->owner->source.map!=t->source.map || t->owner->source.files!=t->source.files ||
        t->owner->source.materials!=t->source.materials || t->owner->source.world!=t->source.world || t->owner->source.context!=t->source.context ||
        t->owner->source.session!=t->source.session ||
        t->owner->source.protocol.kind!=t->source.protocol.kind || t->owner->source.protocol.revision!=t->source.protocol.revision ||
        t->owner->source.protocol.flags!=t->source.protocol.flags || t->owner->source.current!=t->source.current ||
        t->owner->source.actor!=t->source.actor || t->owner->source.actor_pose!=t->source.actor_pose ||
        t->owner->source.viewer!=t->source.viewer ||
        t->owner->source.model!=t->source.model || t->owner->source.sound!=t->source.sound ||
        t->owner->source.hit_marker!=t->source.hit_marker || t->owner->source.controls!=t->source.controls ||
        t->owner->source.frame_milliseconds!=t->source.frame_milliseconds ||
        t->owner->source.render_clock!=t->source.render_clock ||
        t->owner->source.footstep!=t->source.footstep ||
        t->owner->source.trace!=t->source.trace ||
        t->owner->source.video_frame!=t->source.video_frame || t->owner->source.video_context!=t->source.video_context ||
        t->owner->source.white!=(t->published?t->prepared_white:t->white) ||
        t->owner->particle_image!=(t->published?t->prepared_particle:t->particle)) return false;
    qa_error e={0}; return !parent || q2fx_source_current(t->owner,&e);
}
bool frontend_remote_q2_effects_policy_prepare(frontend_remote_q2_effects *o, qa_scene_resource_policy *bank,
    frontend_remote_q2_effects_policy **out, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !out || *out || !bank ||
        qa_scene_resource_policy_source(bank)!=o->source.images || !q2fx_source_current(o,e))
        return q2fx_fail(e,QA_ERROR_ARGUMENT,"Q2 effects policy requires its actual source bank ticket");
    frontend_remote_q2_effects_policy *t=calloc(1,sizeof(*t));
    if (!t) return q2fx_fail(e,QA_ERROR_MEMORY,"Retaining Q2 effects image policy");
    t->owner=o; t->bank=bank; t->source=o->source; t->particle=o->particle_image; t->white=o->source.white;
    o->pending=t; *out=t;
    qa_scene_image *particle=NULL,*white=NULL;
    bool ok=qa_scene_resource_policy_image(bank,t->particle,&particle,e);
    t->prepared_particle=particle;
    if (ok) { ok=qa_scene_resource_policy_image(bank,t->white,&white,e); t->prepared_white=white; }
    if (!ok) {
        qa_error cleanup={0}; (void)frontend_remote_q2_effects_policy_abort(out,&cleanup); return false;
    }
    return current(t,true);
}
bool frontend_remote_q2_effects_policy_ready(frontend_remote_q2_effects_policy *t, qa_error *e)
{
    if (!current(t,true) || t->sealed || t->published || !t->prepared_particle || !t->prepared_white)
        return q2fx_fail(e,QA_ERROR_ARGUMENT,"Prepared Q2 effect images lost their actual owner");
    t->sealed=true; return true;
}
bool frontend_remote_q2_effects_policy_ready_is(const frontend_remote_q2_effects_policy *t)
{ return current(t,true) && t->sealed && !t->published && qa_scene_resource_policy_ready_is(t->bank); }
void frontend_remote_q2_effects_policy_publish(frontend_remote_q2_effects_policy *t)
{
    /* The enclosing bank/parent policy already consumed readiness. Parent's
     * white pointer may publish immediately before or after this no-fail splice. */
    if (!current(t,false) || !t->sealed || t->published) return;
    t->owner->particle_image=t->prepared_particle; t->owner->source.white=t->prepared_white; t->published=true;
}
static bool end(frontend_remote_q2_effects_policy **out, bool published, qa_error *e)
{
    if (!out || !*out) return true;
    frontend_remote_q2_effects_policy *t=*out;
    if (!current(t,true) || t->published!=published)
        return q2fx_fail(e,QA_ERROR_ARGUMENT,"Q2 effects policy still owns a nonterminal parent binding");
    qa_scene_image_release(published?t->particle:t->prepared_particle);
    qa_scene_image_release(t->prepared_white); t->owner->pending=NULL; free(t); *out=NULL; return true;
}
bool frontend_remote_q2_effects_policy_finish(frontend_remote_q2_effects_policy **out, qa_error *e)
{ return end(out,true,e); }
bool frontend_remote_q2_effects_policy_abort(frontend_remote_q2_effects_policy **out, qa_error *e)
{ return end(out,false,e); }
