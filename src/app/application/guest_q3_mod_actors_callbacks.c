#include "guest_q3_mod_actors_private.h"

static size_t kind(application_q3_mod_operation operation)
{
    switch(operation) { case Q3_MOD_TOUCH:return MOD_ACTOR_TOUCH; case Q3_MOD_USE:return MOD_ACTOR_USE;
        case Q3_MOD_PAIN:return MOD_ACTOR_PAIN; case Q3_MOD_DIE:return MOD_ACTOR_DIE; default:return SIZE_MAX; }
}
static bool pointer(application_q3_mod_actors *o,qa_actor_id actor,int32_t *word,qa_error *e)
{
    uint32_t value=0;
    if(actor.registry&&!o->options.pointer(o->options.context,actor,&value,e)) return false;
    memcpy(word,&value,4); return true;
}
static bool reaction_words(application_q3_mod_actors *o,size_t k,const application_q3_mod_actor_request *request,int32_t *words,qa_error *e)
{
    const mod_actor_call *call=o->calls+k; memcpy(words,call->words,call->count*sizeof(*words));
    if(!pointer(o,request->self,words+call->roles[0],e)) return false;
    if(k==MOD_ACTOR_USE) return pointer(o,request->source.use.other,words+call->roles[1],e)&&pointer(o,request->source.use.activator,words+call->roles[2],e);
    if(k==MOD_ACTOR_PAIN) return pointer(o,request->source.pain.attacker,words+call->roles[1],e)&&q3mod_scalar_word(request->source.pain.damage,MOD_INT32,words+call->roles[2],e);
    if(k==MOD_ACTOR_DIE) {
        if(!pointer(o,request->source.die.inflictor,words+call->roles[1],e)||!pointer(o,request->source.die.attacker,words+call->roles[2],e)||
            !q3mod_scalar_word(request->source.die.damage,MOD_INT32,words+call->roles[3],e)) return false;
        words[call->roles[4]]=request->has_attack&&request->attack.cause.kind==QA_CAUSE_Q3?
            request->attack.cause.source.q3.means_of_death:0;
        return true;
    }
    return q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor reaction has no declared argument lowering");
}
bool application_q3_mod_actors_callback(application_q3_mod_actors *o,application_q3_mod_operation operation,
    const application_q3_mod_actor_request *request,bool *handled,qa_error *e)
{
    if(!o||!request||!handled||!q3mod_actors_current(o,e)) return false;
    *handled=false; mod_actor_row *row=q3mod_actors_find(o,request->self); size_t k=kind(operation);
    if(!row||row->retired) return true;
    if(k==SIZE_MAX||k==MOD_ACTOR_TOUCH) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor body requires its exact callback channel");
    uint32_t entry; int32_t words[62];
    if(!q3mod_actors_entry(row,k,&entry,e)) return false;
    *handled=true;
    if(!entry) return true;
    ++o->depth;
    bool ok=reaction_words(o,k,request,words,e)&&q3mod_actors_invoke(o,entry,words,o->calls[k].count,NULL,e);
    --o->depth; return ok;
}
typedef struct touch_run { application_q3_mod_actors *owner; mod_actor_row *row; const qa_touch_contact *contact; uint32_t entry; } touch_run;
static bool touch_scratch(void *context,qa_qvm *vm,uint32_t scratch,qa_error *e)
{
    touch_run *r=context; application_q3_mod_actors *o=r->owner; const mod_actor_call *call=o->calls+MOD_ACTOR_TOUCH;
    qa_body_state body={0};
    if(qa_world_body_storage_serial(o->options.world,r->row->actor)&&
        !qa_world_body_read(o->options.world,r->row->actor,&body,e)) return false;
    qa_vec3 normal=r->contact->has_plane?r->contact->plane.normal:(qa_vec3){0};
    qa_trace_result trace={.end=body.origin,.plane={.normal=normal,
        .distance=r->contact->has_plane?r->contact->plane.distance:0,
        .type=normal.x==1?0:normal.y==1?1:normal.z==1?2:3,
        .signbits=(normal.x<0?1:0)|(normal.y<0?2:0)|(normal.z<0?4:0)}};
    int32_t words[62]; memcpy(words,call->words,call->count*sizeof(*words));
    if(!qa_qvm_write_trace(vm,(int32_t)scratch,&trace,1023,e)||!pointer(o,r->row->actor,words+call->roles[0],e)||
        !pointer(o,r->contact->other,words+call->roles[1],e)) return false;
    words[call->roles[2]]=(int32_t)scratch;
    return q3mod_actors_invoke(o,r->entry,words,call->count,NULL,e);
}
bool application_q3_mod_actors_touch(application_q3_mod_actors *o,const qa_touch_contact *contact,bool *handled,qa_error *e)
{
    if(!o||!contact||!handled||!q3mod_actors_current(o,e)) return false;
    *handled=false; mod_actor_row *row=q3mod_actors_find(o,contact->self);
    if(!row||row->retired) return true;
    uint32_t entry; if(!q3mod_actors_entry(row,MOD_ACTOR_TOUCH,&entry,e)) return false;
    *handled=true; if(!entry) return true;
    touch_run run={o,row,contact,entry}; ++o->depth;
    bool ok=qa_qvm_source_scratch_run_reserved(o->options.vm,o->options.profile->image,56,65536,touch_scratch,&run,e);
    --o->depth; return ok;
}
static bool match(application_q3_mod_actors *o,const qa_qvm_call *call,mod_actor_row **actor,size_t *selected,qa_error *e)
{
    *actor=NULL; *selected=SIZE_MAX;
    if(call->caller_instruction==UINT32_MAX) return true;
    for(size_t k=0;k<4;++k) {
        uint32_t target=o->calls[k].roles[0]; if(target>=call->argument_count) continue;
        int32_t pointer_word; if(!qa_qvm_call_argument(call,target,&pointer_word,e)) return false;
        for(mod_actor_row *r=o->actors;r;r=r->next) {
            if(r->retired||r->pointer!=(uint32_t)pointer_word||!qa_actors_get(qa_session_actors(o->options.session),r->actor)) continue;
            uint32_t entry;
            if(!q3mod_actors_entry(r,k,&entry,e)) return false;
            if(!entry||entry!=call->instruction) continue;
            if(call->argument_count<o->calls[k].count) return q3mod_fail(e,QA_ERROR_FORMAT,"Actor callback exceeds the reached original caller frame");
            if(*actor) return q3mod_fail(e,QA_ERROR_FORMAT,"Actor callback entry has ambiguous declared source signatures");
            *actor=r; *selected=k;
        }
    }
    return true;
}
bool application_q3_mod_actors_match(application_q3_mod_actors *o,const qa_qvm_call *call,bool *found,qa_error *e)
{
    if(!call||!found||!q3mod_actors_current(o,e)||call->vm!=o->options.vm) return false;
    *found=false;
    if(o->combat&&call->instruction==o->damage_entry) { *found=true; return true; }
    mod_actor_row *row; size_t k;
    if(!match(o,call,&row,&k,e)) return false;
    *found=row!=NULL; return true;
}
typedef struct source_reaction {
    application_q3_mod_actors *owner;
    const qa_qvm_call *call;
    size_t kind;
    application_q3_mod_actor_request original;
    application_q3_mod_actor_proceed body;
    void *context;
} source_reaction;
static bool proceed(void *context,const application_q3_mod_actor_request *request,bool *result,qa_error *e)
{
    source_reaction *source=context;
    application_q3_mod_actors *o=source->owner;
    const mod_actor_call *layout=o->calls+source->kind;
    if(!qa_actors_get(qa_session_actors(o->options.session),request->self)||
        (source->kind==MOD_ACTOR_TOUCH&&!qa_actors_get(qa_session_actors(o->options.session),request->source.touch.other))) {
        *result=false; return true;
    }
    qa_actor_id original[3]={source->original.self},effective[3]={request->self};
    size_t actors=2,count=2;
    if(source->kind==MOD_ACTOR_USE) {
        original[1]=source->original.source.use.other; original[2]=source->original.source.use.activator;
        effective[1]=request->source.use.other; effective[2]=request->source.use.activator;
        actors=count=3;
    } else if(source->kind==MOD_ACTOR_TOUCH) {
        original[1]=source->original.source.touch.other; effective[1]=request->source.touch.other;
    } else if(source->kind==MOD_ACTOR_PAIN) {
        original[1]=source->original.source.pain.attacker; effective[1]=request->source.pain.attacker; count=3;
    } else {
        original[1]=source->original.source.die.inflictor; original[2]=source->original.source.die.attacker;
        effective[1]=request->source.die.inflictor; effective[2]=request->source.die.attacker;
        actors=3; count=4;
    }
    int32_t saved[4],words[4];
    for(size_t i=0;i<count;++i) if(!qa_qvm_call_argument(source->call,layout->roles[i],saved+i,e)) return false;
    memcpy(words,saved,count*sizeof(*words));
    for(size_t i=0;i<actors;++i) if(!qa_actor_id_equal(original[i],effective[i])&&!pointer(o,effective[i],words+i,e)) return false;
    if(source->kind==MOD_ACTOR_PAIN&&!q3mod_scalar_word(request->source.pain.damage,MOD_INT32,words+2,e)) return false;
    if(source->kind==MOD_ACTOR_DIE&&!q3mod_scalar_word(request->source.die.damage,MOD_INT32,words+3,e)) return false;
    size_t changed=0; bool ok=true;
    for(size_t i=0;ok&&i<count;++i) { ++changed; ok=qa_qvm_call_set_argument(source->call,layout->roles[i],words[i],e); }
    int32_t raw; if(ok) ok=source->body(source->context,source->call,&raw,e);
    qa_error first=e?*e:(qa_error){0};
    for(size_t i=0;i<changed;++i) {
        qa_error cleanup={0};
        if(!qa_qvm_call_set_argument(source->call,layout->roles[i],saved[i],&cleanup)) { if(ok) first=cleanup; ok=false; }
    }
    if(!ok&&e) *e=first;
    if(ok) *result=true;
    return ok;
}
static bool source_actor(application_q3_mod_actors *o,const qa_qvm_call *call,uint32_t position,qa_actor_id *actor,qa_error *e)
{ int32_t word; return qa_qvm_call_argument(call,position,&word,e)&&o->options.actor(o->options.context,word,actor,e); }
bool application_q3_mod_actors_hook_run(application_q3_mod_actors *o,const qa_qvm_call *call,
    application_q3_mod_actor_proceed body,void *context,int32_t *result,qa_error *e)
{
    if(!call||!result||!body||!q3mod_actors_current(o,e)||call->vm!=o->options.vm) return false;
    if(o->combat&&call->instruction==o->damage_entry) return q3mod_actors_damage_hook(o,call,body,context,result,e);
    mod_actor_row *row; size_t k;
    if(!match(o,call,&row,&k,e)) return false;
    if(!row) return body(context,call,result,e);
    const mod_actor_call *layout=o->calls+k;
    application_q3_mod_actor_request request={.self=row->actor}; application_q3_mod_operation operation;
    if(k==MOD_ACTOR_USE) {
        operation=Q3_MOD_USE;
        if(!source_actor(o,call,layout->roles[1],&request.source.use.other,e)||!source_actor(o,call,layout->roles[2],&request.source.use.activator,e)) return false;
    } else if(k==MOD_ACTOR_TOUCH) {
        operation=Q3_MOD_TOUCH;
        if(!source_actor(o,call,layout->roles[1],&request.source.touch.other,e)) return false;
        if(!request.source.touch.other.registry) return body(context,call,result,e);
        int32_t trace; uint8_t plane[16];
        if(!qa_qvm_call_argument(call,layout->roles[2],&trace,e)) return false;
        if(trace<0) return q3mod_fail(e,QA_ERROR_FORMAT,"Source touch trace leaves its actual plane storage");
        if(trace&&!qa_qvm_read(o->options.vm,(uint32_t)trace+24,plane,sizeof(plane),e)) return false;
    } else {
        int32_t amount;
        if(!qa_qvm_call_argument(call,layout->roles[k==MOD_ACTOR_PAIN?2:3],&amount,e)) return false;
        float damage=(float)amount;
        if((double)damage!=amount) return q3mod_fail(e,QA_ERROR_UNSUPPORTED,"Actor source reaction exceeds exact canonical representation");
        if(!q3mod_actors_reaction(o,row->actor,k,damage,e)) return false;
        if(!qa_actors_get(qa_session_actors(o->options.session),request.self)) {
            if(!qa_qvm_cancel(call,e)) return false;
            *result=0; return true;
        }
        const mod_actor_damage_frame *frame=o->frames;
        while(frame&&!qa_actor_id_equal(frame->request.target,request.self)) frame=frame->previous;
        request.has_attack=frame!=NULL;
        if(frame) request.attack=frame->request.attack;
        if(k==MOD_ACTOR_PAIN) {
            operation=Q3_MOD_PAIN; request.source.pain.damage=damage; request.source.pain.kick=frame?frame->request.knockback:0;
            if(!source_actor(o,call,layout->roles[1],&request.source.pain.attacker,e)) return false;
        } else {
            operation=Q3_MOD_DIE; request.source.die.damage=damage; request.source.die.kick=frame?frame->request.knockback:0;
            request.source.die.point=frame?frame->request.point:(qa_vec3){0};
            if(!source_actor(o,call,layout->roles[1],&request.source.die.inflictor,e)||!source_actor(o,call,layout->roles[2],&request.source.die.attacker,e)) return false;
        }
    }
    source_reaction source={.owner=o,.call=call,.kind=k,.original=request,.body=body,.context=context}; bool disposition=false; ++o->depth;
    bool ok=application_q3_mod_actor_dispatch(o->options.operations,operation,&request,proceed,&source,&disposition,e);
    --o->depth;
    if(ok) *result=0;
    return ok;
}
static bool original(void *context,const qa_qvm_call *call,int32_t *result,qa_error *e)
{ (void)context; return qa_qvm_proceed(call,result,e); }
bool application_q3_mod_actors_hook(application_q3_mod_actors *o,const qa_qvm_call *call,int32_t *result,qa_error *e)
{ return application_q3_mod_actors_hook_run(o,call,original,NULL,result,e); }
