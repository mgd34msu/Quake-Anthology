#include "guest_q3_mod_actors_private.h"

static bool same_vector(qa_vec3 a,qa_vec3 b)
{ return a.x==b.x&&a.y==b.y&&a.z==b.z; }
static bool velocity(application_q3_mod_actors *o,qa_actor_id actor,qa_vec3 *out,bool *found,qa_error *e)
{
    *found=qa_world_body_storage_serial(o->options.world,actor)!=0;
    if(!*found) { *out=(qa_vec3){0}; return true; }
    qa_body_state body; if(!qa_world_body_read(o->options.world,actor,&body,e)) return false;
    *out=body.velocity; return true;
}
bool q3mod_actors_flush(application_q3_mod_actors *o,mod_actor_damage_frame *frame,qa_error *e)
{
    if(frame->finished) return true;
    mod_actor_row *row=q3mod_actors_find(o,frame->request.target); qa_combat_state after; qa_vec3 current; bool has_velocity;
    if(!row||!q3mod_actors_state(row,&after,e)||!velocity(o,row->actor,&current,&has_velocity,e)) return false;
    qa_combat_state before=frame->before; qa_vec3 previous=frame->velocity; bool had_velocity=frame->has_velocity;
    for(mod_actor_damage_frame *active=o->frames;active;active=active->previous) if(qa_actor_id_equal(active->request.target,row->actor)) {
        active->before=after; active->velocity=current; active->has_velocity=has_velocity;
    }
    qa_damage_mutation mutation={0};
    if(before.armor.regular.kind==QA_ARMOR_Q3&&after.armor.regular.kind==QA_ARMOR_Q3&&before.armor.regular.points!=after.armor.regular.points) {
        mutation=(qa_damage_mutation){.kind=QA_MUTATION_ARMOR,.value.armor={before.armor,after.armor}};
        if(!qa_damage_observe(frame->observer,&mutation,e)) return false;
    }
    if(before.health!=after.health) {
        mutation=(qa_damage_mutation){.kind=QA_MUTATION_HEALTH,.value.health={before.health,after.health}};
        if(!qa_damage_observe(frame->observer,&mutation,e)) return false;
    }
    if(had_velocity&&has_velocity&&!same_vector(previous,current)) {
        mutation=(qa_damage_mutation){.kind=QA_MUTATION_SOURCE_VELOCITY,.value.velocity={previous,current,frame->request.attack.movement_provider}};
        if(!qa_damage_observe(frame->observer,&mutation,e)) return false;
    }
    frame->result.applied_damage+=before.health-after.health;
    frame->result.reaction=QA_REACTION_NONE;
    return true;
}
bool q3mod_actors_reaction(application_q3_mod_actors *o,qa_actor_id actor,size_t kind,float amount,qa_error *e)
{
    mod_actor_damage_frame *frame=o->frames;
    while(frame&&!qa_actor_id_equal(frame->request.target,actor)) frame=frame->previous;
    if(!frame||frame->finished) return true;
    if(!q3mod_actors_flush(o,frame,e)) return false;
    frame->finished=true; frame->result=(qa_damage_result){.applied_damage=amount,
        .reaction=kind==MOD_ACTOR_DIE?QA_REACTION_DEATH:QA_REACTION_PAIN};
    return qa_damage_before_reaction(frame->observer,&frame->result,e);
}
static bool vector_read(application_q3_mod_actors *o,int32_t pointer,qa_vec3 *out,qa_error *e)
{
    *out=(qa_vec3){0}; if(!pointer) return true;
    uint8_t bytes[12]; if(pointer<0||!qa_qvm_read(o->options.vm,(uint32_t)pointer,bytes,12,e)) return false;
    *out=(qa_vec3){qa_load_f32le(bytes),qa_load_f32le(bytes+4),qa_load_f32le(bytes+8)};
    return qa_vec_finite(*out)||q3mod_fail(e,QA_ERROR_FORMAT,"Source damage geometry is nonfinite");
}
static uint32_t canonical_flags(application_q3_mod_actors *o,uint32_t flags)
{
    uint32_t value=0; for(size_t i=0;i<5;++i) if(flags&o->damage_flags[i]) value|=UINT32_C(1)<<i;
    return value;
}
static uint32_t source_flags(application_q3_mod_actors *o,const qa_damage_request *request,uint32_t original)
{
    qa_damage_flags flags=qa_attack_flags(&request->attack);
    bool enabled[]={request->radius,flags.no_armor,flags.no_knockback,flags.no_protection,flags.no_team_protection};
    for(size_t i=0;i<5;++i) { original&=~o->damage_flags[i]; if(enabled[i]) original|=o->damage_flags[i]; }
    return original;
}
typedef struct damage_run {
    application_q3_mod_actors *owner;
    const qa_qvm_call *call;
    const qa_damage_request *effective;
    qa_damage_request original;
    uint32_t flags;
    bool incoming;
    mod_actor_incoming *receipt;
} damage_run;
static bool lower_scratch(void *context,qa_qvm *vm,uint32_t address,qa_error *e)
{
    damage_run *r=context; application_q3_mod_actors *o=r->owner; const qa_damage_request *request=r->effective;
    const mod_actor_call *layout=o->calls+MOD_ACTOR_DAMAGE; int32_t words[62]; memcpy(words,layout->words,layout->count*sizeof(*words));
    uint32_t actors[3]={0}; qa_actor_id ids[]={request->target,request->attack.inflictor,request->attack.attacker};
    for(size_t i=0;i<3;++i) if(ids[i].registry&&!o->options.pointer(o->options.context,ids[i],actors+i,e)) return false;
    uint8_t bytes[24]; float values[]={request->direction.x,request->direction.y,request->direction.z,request->point.x,request->point.y,request->point.z};
    for(size_t i=0;i<6;++i) { uint32_t bits; memcpy(&bits,values+i,4); qa_store_u32le(bytes+i*4,bits); }
    if(!qa_qvm_write(vm,address,(qa_bytes){bytes,24},e)) return false;
    for(size_t i=0;i<3;++i) memcpy(words+layout->roles[i],actors+i,4);
    words[layout->roles[3]]=(int32_t)address; words[layout->roles[4]]=(int32_t)address+12;
    if(!q3mod_scalar_word(request->amount,MOD_INT32,words+layout->roles[5],e)) return false;
    uint32_t flags=source_flags(o,request,r->flags); memcpy(words+layout->roles[6],&flags,4);
    words[layout->roles[7]]=request->attack.cause.kind==QA_CAUSE_Q3?request->attack.cause.source.q3.means_of_death:0;
    if(r->incoming) {
        r->receipt->previous=o->incoming; o->incoming=r->receipt;
        bool ok=q3mod_actors_invoke_started(o,o->damage_entry,words,layout->count,NULL,&r->receipt->started,e);
        o->incoming=r->receipt->previous;
        return ok&&(r->receipt->entered||q3mod_fail(e,QA_ERROR_FORMAT,"Source damage did not reach its admitted physical entry"));
    }
    int32_t saved[8]; size_t changed=0; bool ok=true;
    for(size_t i=0;ok&&i<8;++i) {
        uint32_t at=layout->roles[i]; ok=qa_qvm_call_argument(r->call,at,saved+i,e);
        bool preserve=(i==3&&same_vector(request->direction,r->original.direction))||(i==4&&same_vector(request->point,r->original.point))||
            (i==1&&qa_actor_id_equal(request->attack.inflictor,r->original.attack.inflictor))||(i==2&&qa_actor_id_equal(request->attack.attacker,r->original.attack.attacker));
        if(ok) { ++changed; if(!preserve) ok=qa_qvm_call_set_argument(r->call,at,words[at],e); }
    }
    int32_t raw; if(ok) ok=qa_qvm_proceed(r->call,&raw,e);
    qa_error first=e?*e:(qa_error){0};
    for(size_t i=0;i<changed;++i) { qa_error cleanup={0}; if(!qa_qvm_call_set_argument(r->call,layout->roles[i],saved[i],&cleanup)) { if(ok) first=cleanup; ok=false; } }
    if(!ok&&e) *e=first;
    return ok;
}
static bool frame_run(application_q3_mod_actors *o,const qa_damage_request *request,qa_damage_observer *observer,
    damage_run *run,qa_damage_result *result,qa_error *e)
{
    for(mod_actor_damage_frame *active=o->frames;active;active=active->previous)
        if(qa_actor_id_equal(active->request.target,request->target)&&!q3mod_actors_flush(o,active,e)) return false;
    mod_actor_row *row=q3mod_actors_find(o,request->target);
    mod_actor_damage_frame frame={.previous=o->frames,.request=*request,.observer=observer};
    if(!row||!q3mod_actors_state(row,&frame.before,e)||!velocity(o,request->target,&frame.velocity,&frame.has_velocity,e)) return false;
    o->frames=&frame; ++o->depth; run->effective=request;
    bool ok;
    if(run->incoming) { int32_t raw; ok=qa_qvm_proceed(run->call,&raw,e); }
    else ok=qa_qvm_source_scratch_run(o->options.vm,o->options.profile->image,24,lower_scratch,run,e);
    if(ok) ok=q3mod_actors_flush(o,&frame,e);
    --o->depth; o->frames=frame.previous;
    if(ok) *result=frame.result;
    return ok;
}
bool q3mod_actors_source_damage(void *context,qa_combat *combat,const qa_damage_request *request,
    qa_damage_observer *observer,qa_damage_result *result,qa_error *e)
{
    mod_actor_row *row=context; application_q3_mod_actors *o=row->owner; qa_combat_state state;
    if(combat!=o->options.combat||!request||!result||!observer||!q3mod_actors_current(o,e)||
        !qa_actor_id_equal(request->target,row->actor)||!q3mod_actors_state(row,&state,e)) return false;
    *result=(qa_damage_result){0}; if(!state.can_take_damage) return true;
    mod_actor_incoming incoming={.row=row,.request=request,.observer=observer,.result=result};
    damage_run run={.owner=o,.effective=request,.incoming=true,.receipt=&incoming}; ++o->depth;
    bool ok=qa_qvm_source_scratch_run(o->options.vm,o->options.profile->image,24,lower_scratch,&run,e);
    --o->depth; return ok;
}
static bool entered(void *context,qa_combat *combat,const qa_damage_request *request,qa_damage_observer *observer,qa_damage_result *result,qa_error *e)
{
    damage_run *run=context;
    if(combat!=run->owner->options.combat||!qa_actor_id_equal(request->target,run->original.target))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source damage continuation changed its genuine target");
    return frame_run(run->owner,request,observer,run,result,e);
}
bool application_q3_mod_actors_entry_begin(application_q3_mod_actors *o,const qa_qvm_call *call,qa_error *e)
{
    if(!o||!call||call->vm!=o->options.vm||!q3mod_actors_current(o,e)) return false;
    mod_actor_incoming *incoming=o->incoming;
    if(incoming&&incoming->started&&!incoming->entered&&!incoming->call&&
        call->caller_instruction==UINT32_MAX&&o->combat&&call->instruction==o->damage_entry)
        incoming->call=call;
    return true;
}
void application_q3_mod_actors_entry_end(application_q3_mod_actors *o,const qa_qvm_call *call)
{
    for(mod_actor_incoming *incoming=o?o->incoming:NULL;incoming;incoming=incoming->previous)
        if(incoming->call==call) incoming->call=NULL;
}
bool q3mod_actors_damage_hook(application_q3_mod_actors *o,const qa_qvm_call *call,int32_t *result,qa_error *e)
{
    mod_actor_incoming *incoming=o->incoming;
    if(incoming&&incoming->started&&!incoming->entered&&incoming->call==call) {
        incoming->entered=true;
        damage_run run={.owner=o,.call=call,.incoming=true};
        bool ok=frame_run(o,incoming->request,incoming->observer,&run,incoming->result,e);
        if(ok) *result=0;
        return ok;
    }
    const mod_actor_call *layout=o->calls+MOD_ACTOR_DAMAGE;
    if(call->argument_count<layout->count) return q3mod_fail(e,QA_ERROR_FORMAT,"Source damage exceeds its reached argument frame");
    int32_t words[8]; for(size_t i=0;i<8;++i) if(!qa_qvm_call_argument(call,layout->roles[i],words+i,e)) return false;
    qa_damage_request request={.amount=(float)words[5],.radius=((uint32_t)words[6]&o->damage_flags[0])!=0};
    if((double)request.amount!=words[5]) return q3mod_fail(e,QA_ERROR_UNSUPPORTED,"Source damage amount exceeds exact canonical representation");
    if(!o->options.actor(o->options.context,words[0],&request.target,e)||!request.target.registry)
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source damage target has no genuine canonical actor");
    if(!o->options.actor(o->options.context,words[1],&request.attack.inflictor,e)||!o->options.actor(o->options.context,words[2],&request.attack.attacker,e)||
        !vector_read(o,words[3],&request.direction,e)||!vector_read(o,words[4],&request.point,e)) return false;
    request.knockback=words[3]&&!((uint32_t)words[6]&o->damage_flags[2])?request.amount:0;
    request.attack.cause=(qa_damage_cause){.kind=QA_CAUSE_Q3,.source.q3={words[7],canonical_flags(o,(uint32_t)words[6])}};
    if(!o->options.damage_context(o->options.context,&request,e)||!qa_attack_next(&o->sequence,&request.attack,e)) return false;
    mod_actor_row *row=q3mod_actors_find(o,request.target); qa_damage_outcome outcome={0}; bool ok;
    if(!row||row->retired) ok=qa_combat_apply(o->options.combat,&request,&outcome,e);
    else {
        damage_run run={.owner=o,.call=call,.original=request,.flags=(uint32_t)words[6]};
        ok=qa_combat_run_source(o->options.combat,&request,entered,&run,&outcome,e);
    }
    qa_damage_outcome_free(&outcome); if(ok) *result=0; return ok;
}
