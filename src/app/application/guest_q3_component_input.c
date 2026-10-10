#include "guest_q3_component_input.h"
#include "guest_q3_mod_items.h"
#include <stdlib.h>

struct application_q3_component_input {
    application_q3_mod *mod;
    application_q3_mod_application *application;
    application_q3_mod_items *items;
    application_q3_mod_items_application *item_application;
    qa_actor_id actor;
    bool slice,finishing,running;
};
static bool fail(qa_error *e,const char *text)
{ qa_error_set(e,QA_ERROR_ARGUMENT,0,"%s",text); return false; }
typedef struct input_preparation {
    application_q3_component_input *scope;
    application_q3_component_input_values values;
    void *context;
} input_preparation;
static bool prepare(void *context,uint32_t entry,qa_error *e)
{
    input_preparation *p=context;
    application_q3_component_input *scope=p->scope;
    application_q3_mod_inputs current={0};
    if(!p->values(p->context,&current,e)||
        !application_q3_mod_input_update(scope->application,&current,e)) return false;
    if(!scope->items||!application_q3_mod_items_applies(scope->items,entry)) return true;
    if(!scope->slice) return fail(e,"Weapon input requires its actual movement slice");
    return application_q3_mod_items_apply(scope->item_application,entry,&current,e);
}
static bool bindings(application_q3_component_input *scope,bool before,
    application_q3_component_input_values values,
    application_q3_component_input_output output,void *context,qa_error *e)
{
    size_t count=application_q3_mod_input_binding_count(scope->mod);
    for(size_t i=0;i<count&&application_q3_mod_client_live(scope->mod,scope->actor);++i) {
        bool slice,phase;
        if(!application_q3_mod_input_binding(scope->mod,i,&slice,&phase))
            return fail(e,"Component input lost its actual declared binding");
        if(slice!=scope->slice||phase!=before) continue;
        application_q3_mod_inputs current={0};
        if(!values||!values(context,&current,e)||
            !application_q3_mod_input_update(scope->application,&current,e)) return false;
        application_q3_mod_output *outputs=NULL; size_t written=0;
        input_preparation preparation={scope,values,context};
        bool ok=application_q3_mod_input_run(scope->mod,i,scope->application,prepare,&preparation,&outputs,&written,e);
        for(size_t j=0;ok&&j<written&&application_q3_mod_client_live(scope->mod,scope->actor);++j)
            ok=output&&output(context,outputs+j,e);
        if(!ok) {
            if(e&&e->code==QA_OK) fail(e,"Component input output has no actual canonical consumer");
            return false;
        }
    }
    return true;
}
bool application_q3_component_input_begin(application_q3_component *component,qa_actor_id actor,
    bool slice,qa_unified_frame_lease *storage,application_q3_component_input_values values,
    application_q3_component_input_output output,void *context,
    application_q3_component_input **out,qa_error *e)
{
    if(!component||!out||*out||!values||!output)
        return fail(e,"Component input requires its genuine caller and empty scope");
    application_q3_mod *mod=application_q3_component_mod(component);
    if(!mod) return fail(e,"Component input has no actual retained generic runtime");
    size_t count=application_q3_mod_input_binding_count(mod); bool selected=false;
    for(size_t i=0;i<count;++i) {
        bool kind,before;
        if(!application_q3_mod_input_binding(mod,i,&kind,&before))
            return fail(e,"Component input lost its actual declared binding");
        selected|=kind==slice;
    }
    if(!selected||!application_q3_mod_client_live(mod,actor)) return true;
    application_q3_mod_inputs current={0};
    current.values[Q3_MOD_SELF]=(application_q3_mod_value){.kind=Q3_MOD_VALUE_ACTOR,.as.actor=actor};
    application_q3_component_input *scope=qa_unified_frame_lease_alloc(storage,1,sizeof(*scope),
        _Alignof(application_q3_component_input),e);
    if(!scope) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining actual component input application"); return false; }
    scope->mod=mod; scope->actor=actor; scope->slice=slice;
    scope->items=application_q3_component_items(component);
    if(!application_q3_mod_open(mod,actor,&current,storage,&scope->application,e)) return false;
    *out=scope;
    if(!application_q3_mod_input_source(scope->application,values,context,e)) return false;
    if(scope->items&&!application_q3_mod_items_open(scope->items,actor,scope->application,storage,
        &scope->item_application,e)) return false;
    scope->running=true;
    bool ok=bindings(scope,true,values,output,context,e);
    scope->running=false; return ok;
}
bool application_q3_component_input_abort(application_q3_component_input **in,qa_error *e)
{
    if(!in) return fail(e,"Component input cleanup requires its retained owner");
    application_q3_component_input *scope=*in;
    if(!scope) return true;
    if(scope->running) return fail(e,"Component input retains an executing boundary callback");
    if(!application_q3_mod_items_close(&scope->item_application,e)) return false;
    if(!application_q3_mod_close(&scope->application,e)) {
        if(!scope->application) *in=NULL;
        return false;
    }
    *in=NULL; return true;
}
bool application_q3_component_input_complete(application_q3_component_input *scope,bool completed,
    application_q3_component_input_values values,void *context,qa_error *e)
{
    if(!scope) return true;
    if(scope->finishing||scope->running) return fail(e,"Component input completion cannot replay authored callbacks");
    scope->finishing=true; scope->running=true;
    bool ok=!completed||bindings(scope,false,values,NULL,context,e);
    scope->running=false;
    return ok;
}
