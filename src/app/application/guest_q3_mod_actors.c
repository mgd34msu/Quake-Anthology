#include "guest_q3_mod_actors_private.h"

bool q3mod_actors_current(application_q3_mod_actors *o,qa_error *e)
{
    return o&&!o->closing&&!o->restoring&&o->options.current(o->options.context,e)&&q3mod_current(o->options.mod,e) ? true :
        q3mod_fail(e,QA_ERROR_ARGUMENT,"Source actor operation has no current component owner");
}
mod_actor_row *q3mod_actors_find(application_q3_mod_actors *o,qa_actor_id actor)
{ for(mod_actor_row *r=o->actors;r;r=r->next) if(qa_actor_id_equal(r->actor,actor)) return r; return NULL; }
static bool actor_invoke(application_q3_mod_actors *o,uint32_t entry,const int32_t *words,size_t count,int32_t *result,bool *started,qa_error *e)
{
    if(started) *started=false;
    if(count>62||(count&&!words)) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor call leaves its genuine private frame");
    mod_call call=o->globals?*o->globals:(mod_call){.profile=o->options.profile,.returns=MOD_INT32};
    mod_argument arguments[62]; memset(arguments,0,sizeof(arguments));
    for(size_t i=0;i<count;++i) arguments[i]=(mod_argument){.kind=MOD_SCALAR,.encoding=MOD_INT32,.literal={.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=words[i]}};
    call.entry=entry; call.arguments=arguments; call.argument_count=count;
    double seconds,value;
    if(!o->options.mod->services.time(o->options.mod->services.context,&seconds,e)||!isfinite(seconds)) return false;
    application_q3_mod_inputs inputs={0}; inputs.values[Q3_MOD_TIME]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_SCALAR,.as.scalar=seconds};
    bool ok=started?q3mod_invoke_started(o->options.mod,&call,&inputs,&value,started,e):
        q3mod_invoke(o->options.mod,&call,&inputs,&value,e);
    if(ok&&result) *result=(int32_t)value;
    return ok;
}
bool q3mod_actors_invoke(application_q3_mod_actors *o,uint32_t entry,const int32_t *words,size_t count,int32_t *result,qa_error *e)
{ return actor_invoke(o,entry,words,count,result,NULL,e); }
bool q3mod_actors_invoke_started(application_q3_mod_actors *o,uint32_t entry,const int32_t *words,size_t count,int32_t *result,bool *started,qa_error *e)
{
    if(!started) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor source call requires its actual entry receipt");
    return actor_invoke(o,entry,words,count,result,started,e);
}
bool application_q3_mod_actors_create(const application_q3_mod_actors_options *options,bool restoring,
    application_q3_mod_actors **out,qa_error *e)
{
    if(!options||!options->profile||!options->mod||!options->operations||!options->vm||!options->session||!options->world||!options->combat||!options->owner||
        !options->current||!options->owned||!options->pointer||!options->actor||!out||*out||options->mod->profile!=options->profile||options->mod->vm!=options->vm||
        options->mod->session!=options->session||options->mod->owner!=options->owner||options->mod->combat!=options->combat)
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor semantics require the actual component executor and canonical owners");
    application_q3_mod_actors *o=calloc(1,sizeof(*o));
    if(!o) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning declared source actor semantics");
    o->options=*options; o->restoring=restoring;
    bool ok=q3mod_actors_profile(o,e);
    if(ok&&o->combat&&!options->damage_context) ok=q3mod_fail(e,QA_ERROR_ARGUMENT,"Source damage requires actual canonical attack provenance");
    if(!ok) { application_q3_mod_call_destroy(o->globals); free(o->teams); free(o); return false; }
    *out=o; return true;
}
bool application_q3_mod_actors_idle(const application_q3_mod_actors *o)
{ return !o||(!o->depth&&!o->frames&&!o->incoming); }
bool application_q3_mod_actors_binding(application_q3_mod_actors *o,qa_actor_id actor,qa_combat_binding *out,qa_error *e)
{
    mod_actor_row *r=o?q3mod_actors_find(o,actor):NULL;
    if(!r||r->retired||!o->combat||!out) return q3mod_fail(e,QA_ERROR_NOT_FOUND,"Actor combat binding has no retained source row");
    *out=(qa_combat_binding){.context=r,.read=q3mod_actors_state,.write_health=q3mod_actors_health,
        .write_armor=q3mod_actors_armor,.validate_armor=q3mod_actors_armor_valid,.source_damage=q3mod_actors_source_damage,
        .source_armor_stages={true,true},.has_primary_protection={true,false},.primary_protection={o->options.owner,0}};
    return true;
}
bool application_q3_mod_actors_binding_saved(application_q3_mod_actors *o,qa_actor_id actor,
    uint64_t serial,qa_combat_binding *out,qa_error *e)
{
    mod_actor_row *r=o?q3mod_actors_find(o,actor):NULL;
    uint32_t pointer;
    const qa_actor_record *actual=o?qa_actors_get(qa_session_actors(o->options.session),actor):NULL;
    if(!r||!r->bound||!serial||r->serial!=serial||!q3mod_storage_current(o->options.mod,e))
        return q3mod_fail(e,QA_ERROR_FORMAT,"Saved actor primary differs from its retained full source binding");
    if(!actual||actual->owner!=o->options.owner||!o->options.owned(o->options.context,actor)||
        !o->options.pointer(o->options.context,actor,&pointer,e)||pointer!=r->pointer)
        return q3mod_fail(e,QA_ERROR_FORMAT,"Saved actor primary has no exact current source row");
    return application_q3_mod_actors_binding(o,actor,out,e);
}
bool application_q3_mod_actors_admit(application_q3_mod_actors *o,qa_actor_id actor,qa_error *e)
{
    if(!q3mod_actors_current(o,e)) return false;
    if(!o->present) return true;
    uint32_t pointer; const qa_actor_record *actual=qa_actors_get(qa_session_actors(o->options.session),actor);
    if(!actual||actual->owner!=o->options.owner||!o->options.owned(o->options.context,actor)||!o->options.pointer(o->options.context,actor,&pointer,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor semantics admission requires its genuine owned full actor");
    mod_record *record=o->options.profile->records+o->entity_record;
    if(pointer<record->address||(uint64_t)pointer>=(uint64_t)record->address+(uint64_t)record->stride*record->capacity||(pointer-record->address)%record->stride)
        return q3mod_fail(e,QA_ERROR_FORMAT,"Actor semantic pointer leaves its exact source entity array");
    mod_actor_row *r=q3mod_actors_find(o,actor);
    if(r&&(r->retired||r->pointer!=pointer)) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor semantic admission changed its retained source row");
    if(!r) {
        for(mod_actor_row *prior=o->actors;prior;prior=prior->next) if(!prior->retired&&prior->pointer==pointer)
            return q3mod_fail(e,QA_ERROR_FORMAT,"Actor semantic pointer is still owned by another full actor");
        r=calloc(1,sizeof(*r)); if(!r) return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining declared actor callback row");
        *r=(mod_actor_row){.owner=o,.actor=actor,.pointer=pointer}; mod_actor_row **tail=&o->actors; while(*tail) tail=&(*tail)->next; *tail=r;
    }
    if(o->combat&&!r->bound) {
        qa_combat_binding binding;
        if(!application_q3_mod_actors_binding(o,actor,&binding,e)||!qa_combat_bind(o->options.combat,actor,&binding,true,e)) return false;
        r->serial=qa_combat_storage_serial(o->options.combat,actor); r->bound=true;
    }
    return true;
}
bool application_q3_mod_actors_before_release(application_q3_mod_actors *o,qa_actor_id actor,qa_error *e)
{
    for(mod_actor_damage_frame *f=o?o->frames:NULL;f;f=f->previous) if(qa_actor_id_equal(f->request.target,actor)&&!f->finished) {
        if(!q3mod_actors_flush(o,f,e)) return false;
        f->finished=true;
    }
    return true;
}
bool application_q3_mod_actors_release(application_q3_mod_actors *o,qa_actor_id actor,qa_error *e)
{
    if(!o) return true;
    mod_actor_row **link=&o->actors; while(*link&&!qa_actor_id_equal((*link)->actor,actor)) link=&(*link)->next;
    if(!*link) return true;
    mod_actor_row *r=*link;
    if(r->bound&&qa_combat_primary_current(o->options.combat,r->actor,r->serial,r)&&
        !qa_combat_detach_primary(o->options.combat,r->actor,r->serial,r,e)) return false;
    *link=r->next; free(r); return true;
}
bool application_q3_mod_actors_destroy(application_q3_mod_actors **in,qa_error *e)
{
    if(!in||!*in) return true;
    application_q3_mod_actors *o=*in;
    if(!application_q3_mod_actors_idle(o)) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor semantics retain a reached source callback");
    /* Detach snapshots while the real projection is still current. */
    while(o->actors) if(!application_q3_mod_actors_release(o,o->actors->actor,e)) return false;
    o->closing=true;
    application_q3_mod_call_destroy(o->globals); free(o->teams); free(o); *in=NULL; return true;
}
bool application_q3_mod_actors_damage_entry(const application_q3_mod_actors *o,uint32_t *entry)
{ if(!o||!o->combat||!entry) return false; *entry=o->damage_entry; return true; }
