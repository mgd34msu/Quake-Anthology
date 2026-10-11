#include "guest_qc_item_weapons.h"
#include "guest_qc_items.h"
#include "guest_mod_item_definition.h"
#include "qa/qc_observation.h"
#include <float.h>

typedef struct weapon_value { float value; qa_item_id item; } weapon_value;
struct application_qc_item_weapons {
    uint32_t dispatcher, *continuations;
    size_t continuation_count, repeat_count, weapon_count;
    qa_qc_inline_region *regions;
    uint32_t *released;
    float *release_values;
    const qa_qc_definition *selected, *select, *model, *frame, *think;
    weapon_value *selected_values, *select_values;
    application_qc_call select_call;
    application_qc_calls resume;
};
struct application_qc_item_weapon_actor {
    struct application_qc_state *engine;
    qa_actor_id actor;
    int32_t reference;
    unsigned calls;
    bool bound, retired;
    struct application_qc_item_weapon_actor *next;
};
static struct application_qc_item_weapons *profile(const application_provider *p)
{
    return p && p->state.qc.qualified && p->state.qc.qualified->items ?
        p->state.qc.qualified->items->weapons : NULL;
}
static bool array(const qa_json_document *d, qa_json_id id, size_t stride,
    void **out, size_t *count, qa_error *e)
{
    if (qa_json_type(d,id)!=QA_JSON_ARRAY)
        return application_fail(e,QA_ERROR_FORMAT,"QC weapon declaration requires an array");
    *count=qa_json_size(d,id);
    if (*count>SIZE_MAX/stride)
        return application_fail(e,QA_ERROR_MEMORY,"QC weapon declaration is too large");
    *out=*count?calloc(*count,stride):NULL;
    return !*count || *out || application_fail(e,QA_ERROR_MEMORY,"Owning QC weapon declaration");
}
static bool number(const qa_json_document *d, qa_json_id id, uint32_t maximum,
    uint32_t *out, qa_error *e)
{
    uint64_t value;
    if (!qa_json_u64(d,id,&value,e)) return false;
    if (value>maximum) {
        application_fail(e,QA_ERROR_FORMAT,"QC weapon instruction exceeds its Source extent");
        return false;
    }
    *out=(uint32_t)value; return true;
}
static const qa_qc_function *source_function(const qa_qc_program *program,uint32_t index,qa_error *e)
{
    const qa_qc_function *fn=qa_qc_program_function(program,index);
    if(!index || !fn || fn->first_statement<=0 || fn->named_builtin || fn->parameter_count) {
        application_fail(e,QA_ERROR_FORMAT,"QC weapon stage requires an original parameterless Source function"); return NULL;
    }
    for(uint32_t i=(uint32_t)fn->first_statement,end=qa_qc_program_function_end(program,fn);i<end;++i) {
        const qa_qc_statement *s=qa_qc_program_statement(program,i);
        if((s->opcode==QA_QC_RETURN || s->opcode==QA_QC_DONE) && s->a) {
            application_fail(e,QA_ERROR_FORMAT,"QC weapon stage must return void"); return NULL;
        }
    }
    return fn;
}
static const qa_qc_function *named_function(const application_provider *p,const qa_json_document *d,
    qa_json_id id,uint32_t *index,qa_error *e)
{
    char *name=application_qc_declaration_string(d,id,e);
    const qa_qc_function *fn=name?qa_qc_program_find_function(p->state.qc.program,name,index):NULL;
    free(name);
    if(!fn) { application_fail(e,QA_ERROR_FORMAT,"QC weapon stage function is absent from its program"); return NULL; }
    return source_function(p->state.qc.program,*index,e);
}
static bool temporary(const qa_qc_program *program,const qa_qc_function *fn,uint32_t word)
{
    qa_qc_program_info info=qa_qc_program_describe(program);
    if(word<28 || word>=info.global_words) return false;
    if(word>=fn->parameter_start && word-fn->parameter_start<fn->local_words) return true;
    for(uint32_t i=0;i<info.global_count;++i) {
        const qa_qc_definition *g=qa_qc_program_global(program,i);
        if(!*g->name || !strcmp(g->name,"IMMEDIATE")) continue;
        uint32_t count=g->type==QA_QC_VECTOR?3u:1u;
        if(word>=g->offset && word-(uint32_t)g->offset<count) return false;
    }
    return true;
}
static bool pure(qa_qc_opcode opcode)
{
    switch(opcode) {
    case QA_QC_LOAD_F:case QA_QC_NOT_F:case QA_QC_EQ_F:case QA_QC_NE_F:
    case QA_QC_LE:case QA_QC_GE:case QA_QC_LT:case QA_QC_GT:case QA_QC_AND:case QA_QC_OR:
    case QA_QC_BITAND:case QA_QC_BITOR:case QA_QC_ADD_F:case QA_QC_SUB_F:case QA_QC_MUL_F:case QA_QC_DIV_F:return true;
    default:return false;
    }
}
static const qa_qc_definition *field(application_provider *p,const qa_json_document *d,
    qa_json_id id,qa_qc_value_type type,bool input,qa_error *e)
{
    char *name=application_qc_declaration_string(d,id,e);
    const qa_qc_definition *f=name?qa_qc_program_find_field(p->state.qc.program,name):NULL;
    free(name);
    if(f && f->type==type) for(size_t i=0;i<p->state.qc.qualified->field_count;++i) {
        const application_qc_bound_field *binding=p->state.qc.qualified->fields+i;
        if(binding->definition==f && (binding->kind==QC_FIELD_PRIVATE || (input && binding->kind==QC_FIELD_INPUT))) return f;
    }
    application_fail(e,QA_ERROR_FORMAT,"QC weapon field lacks its declared private Source storage"); return NULL;
}
static bool mapping(application_provider *p,const qa_json_document *d,qa_json_id node,
    const qa_qc_definition **f,weapon_value **values,size_t weapons,bool input,qa_error *e)
{
    *f=field(p,d,qa_json_get(d,node,"field"),QA_QC_FLOAT,input,e);
    size_t count;
    if(!*f || !array(d,qa_json_get(d,node,"values"),sizeof(**values),(void **)values,&count,e))return false;
    if(count!=weapons)return application_fail(e,QA_ERROR_FORMAT,"QC weapon selector differs from its declared weapon items");
    const struct application_qc_items *items=p->state.qc.qualified->items;
    qa_json_id rows=qa_json_get(d,node,"values");
    for(size_t i=0;i<count;++i) {
        qa_json_id row=qa_json_at(d,rows,i);double value;
        if(!qa_json_number(d,qa_json_get(d,row,"value"),&value,e) ||
            !application_mod_item_identity(d,qa_json_get(d,row,"item"),qa_session_strings(p->application->session),&(*values)[i].item,e))return false;
        if(!isfinite(value) || value==0 || fabs(value)>FLT_MAX || (double)(float)value!=value)
            return application_fail(e,QA_ERROR_FORMAT,"QC weapon selector must be a nonzero exact Source float");
        (*values)[i].value=(float)value;bool declared=false;
        for(size_t j=0;j<items->definition_count;++j)
            declared|=items->admissions[j].definition.item==(*values)[i].item && items->admissions[j].definition.weapon;
        if(!declared)return application_fail(e,QA_ERROR_FORMAT,"QC weapon selector names an undeclared weapon");
        for(size_t j=0;j<i;++j) if((*values)[j].value==(*values)[i].value || (*values)[j].item==(*values)[i].item)
            return application_fail(e,QA_ERROR_FORMAT,"QC weapon selector values overlap");
    }
    return true;
}
static bool stage(application_provider *provider,const qa_json_document *d,qa_json_id node,
    struct application_qc_item_weapons *p,qa_error *e)
{
    const qa_qc_program *program=provider->state.qc.program;
    if(!named_function(provider,d,qa_json_get(d,node,"dispatcher"),&p->dispatcher,e))return false;
    qa_json_id continuations=qa_json_get(d,node,"continuations"),repeats=qa_json_get(d,node,"repeats");
    if(!array(d,continuations,sizeof(*p->continuations),(void **)&p->continuations,&p->continuation_count,e) ||
        !p->continuation_count || !array(d,repeats,sizeof(*p->regions),(void **)&p->regions,&p->repeat_count,e))return false;
    p->released=p->repeat_count?calloc(p->repeat_count,sizeof(*p->released)):NULL;
    p->release_values=p->repeat_count?calloc(p->repeat_count,sizeof(*p->release_values)):NULL;
    if(p->repeat_count && (!p->released || !p->release_values))return application_fail(e,QA_ERROR_MEMORY,"Owning QC repeat predicates");
    for(size_t i=0;i<p->continuation_count;++i) {
        if(!named_function(provider,d,qa_json_at(d,continuations,i),p->continuations+i,e))return false;
        if(p->continuations[i]==p->dispatcher)return application_fail(e,QA_ERROR_FORMAT,"QC weapon dispatcher overlaps its continuation");
        for(size_t j=0;j<i;++j)if(p->continuations[j]==p->continuations[i])return application_fail(e,QA_ERROR_FORMAT,"QC weapon continuations overlap");
    }
    for(size_t i=0;i<p->repeat_count;++i) {
        qa_json_id row=qa_json_at(d,repeats,i),result=qa_json_get(d,row,"result");
        qa_qc_inline_region *r=p->regions+i;uint32_t value;
        const qa_qc_function *fn=named_function(provider,d,qa_json_get(d,row,"function"),&r->function,e);
        if(!fn || !number(d,qa_json_get(d,row,"entry"),UINT32_MAX,&r->entry,e) ||
            !number(d,qa_json_get(d,row,"exit"),UINT32_MAX,&r->exit,e) ||
            !number(d,qa_json_get(d,result,"word"),UINT32_MAX,p->released+i,e) ||
            !number(d,qa_json_get(d,result,"value"),1,&value,e))return false;
        bool continuation=false;
        for(size_t j=0;j<p->continuation_count;++j)continuation|=p->continuations[j]==r->function;
        uint32_t end=qa_qc_program_function_end(program,fn);
        if(!continuation || r->entry<(uint32_t)fn->first_statement || r->exit<=r->entry ||
            r->exit>=end || end-r->exit<=2 || !temporary(program,fn,p->released[i]))
            return application_fail(e,QA_ERROR_FORMAT,"QC weapon repeat is outside its original continuation");
        for(size_t j=0;j<i;++j)if(r->entry<=p->regions[j].exit && r->exit>=p->regions[j].entry)
            return application_fail(e,QA_ERROR_FORMAT,"QC weapon repeat regions overlap");
        qa_json_id statements=qa_json_get(d,row,"statements");
        if(!application_qc_statements_validate(d,statements,program,r->entry,r->exit,e))return false;
        bool written=false;
        for(uint32_t at=r->entry;at<=r->exit;++at) {
            const qa_qc_statement *s=qa_qc_program_statement(program,at);
            if(at==r->exit)continue;
            if(!pure(s->opcode) || !temporary(program,fn,s->c))
                return application_fail(e,QA_ERROR_FORMAT,"QC repeat predicate must write only its actual Source temporaries");
            written|=s->c==p->released[i];
        }
        const qa_qc_statement *join=qa_qc_program_statement(program,r->exit),
            *release=qa_qc_program_statement(program,r->exit+1),*returned=qa_qc_program_statement(program,r->exit+2);
        if(!written || join->a!=p->released[i] || join->opcode!=(value?QA_QC_IFNOT:QA_QC_IF) ||
            join->b!=3 || release->opcode!=QA_QC_CALL0 || returned->opcode!=QA_QC_RETURN || returned->a)
            return application_fail(e,QA_ERROR_FORMAT,"QC repeat does not join its original release-call and return");
        int32_t callee;
        if(!qa_qc_program_initial_int(program,release->a,&callee,e) || callee<=0 ||
            !source_function(program,(uint32_t)callee,e))return false;
        r->replaceable=true;p->release_values[i]=(float)value;
    }
    return true;
}
bool application_qc_item_weapons_qualify(application_provider *provider,const qa_json_document *d,qa_json_id node,qa_error *e)
{
    struct application_qc_items *items=provider->state.qc.qualified->items;
    struct application_qc_item_weapons *p=calloc(1,sizeof(*p));
    if(!p)return application_fail(e,QA_ERROR_MEMORY,"Owning declared QC weapon stages");
    items->weapons=p;
    for(size_t i=0;i<items->definition_count;++i) {
        const qa_item_definition *item=&items->admissions[i].definition;
        if(!item->weapon)continue;
        ++p->weapon_count;
        if(!item->ammo)continue;
        bool declared=false;
        for(size_t j=0;j<items->definition_count;++j)declared|=items->admissions[j].definition.item==item->ammo;
        for(size_t j=0;j<provider->state.qc.qualified->field_count;++j) {
            const application_qc_bound_field *f=provider->state.qc.qualified->fields+j;
            declared|=f->kind==QC_FIELD_INVENTORY && f->item==item->ammo;
        }
        if(!declared)return application_fail(e,QA_ERROR_FORMAT,"QC weapon ammunition has no actual declared Source storage");
    }
    bool think=false,nextthink=false;
    for(size_t i=0;i<provider->state.qc.qualified->field_count;++i) {
        const application_qc_bound_field *f=provider->state.qc.qualified->fields+i;
        think|=f->kind==QC_FIELD_THINK;nextthink|=f->kind==QC_FIELD_NEXTTHINK;
    }
    if(!think || !nextthink)return application_fail(e,QA_ERROR_FORMAT,"QC weapons require continuing Source think ownership");
    p->think=provider->state.qc.engine->field_bindings->think;
    if(!p->think || p->think->type!=QA_QC_FUNCTION)return application_fail(e,QA_ERROR_FORMAT,"QC weapons require their actual think field");
    qa_json_id selected=qa_json_get(d,node,"selected"),select=qa_json_get(d,node,"select"),model=qa_json_get(d,node,"model");
    if(!stage(provider,d,qa_json_get(d,node,"stage"),p,e) ||
        !mapping(provider,d,selected,&p->selected,&p->selected_values,p->weapon_count,false,e) ||
        !mapping(provider,d,select,&p->select,&p->select_values,p->weapon_count,true,e))return false;
    p->model=field(provider,d,qa_json_get(d,model,"field"),QA_QC_STRING,false,e);
    p->frame=field(provider,d,qa_json_get(d,model,"frame"),QA_QC_FLOAT,false,e);
    uint64_t inputs=(UINT64_C(1)<<QC_INPUT_SELF)|(UINT64_C(1)<<QC_INPUT_TIME);
    if(!p->model || !p->frame || !application_qc_call_parse(d,qa_json_get(d,select,"call"),provider->state.qc.program,inputs,false,&p->select_call,e))return false;
    qa_json_id resume=qa_json_get(d,node,"resume");
    if(!array(d,resume,sizeof(*p->resume.values),(void **)&p->resume.values,&p->resume.count,e))return false;
    for(size_t i=0;i<p->resume.count;++i)if(!application_qc_call_parse(d,qa_json_at(d,resume,i),provider->state.qc.program,inputs,false,p->resume.values+i,e))return false;
    return true;
}
void application_qc_item_weapons_profile_free(struct application_qc_item_weapons *p)
{
    if(!p)return;
    free(p->continuations);free(p->regions);free(p->released);free(p->release_values);
    free(p->selected_values);free(p->select_values);application_qc_call_free(&p->select_call);
    for(size_t i=0;i<p->resume.count;++i)application_qc_call_free(p->resume.values+i);
    free(p->resume.values);free(p);
}
static struct application_qc_item_weapon_actor *actor_for(struct application_qc_state *engine,qa_actor_id actor)
{
    for(struct application_qc_item_weapon_actor *a=engine->item_weapon_actors;a;a=a->next)
        if(!a->retired && qa_actor_id_equal(a->actor,actor))return a;
    return NULL;
}
static bool current(struct application_qc_item_weapon_actor *a,qa_error *e)
{
    int32_t reference;
    return a && actor_for(a->engine,a->actor)==a &&
        application_qc_items_actor_current(a->engine,a->actor,e) &&
        application_qc_reference(a->engine,a->actor,&reference,e) && reference==a->reference;
}
static bool binding_current(void *context,qa_actor_id actor)
{
    struct application_qc_item_weapon_actor *a=context;
    return qa_actor_id_equal(a->actor,actor) && current(a,NULL);
}
bool application_qc_item_weapons_selected_read(struct application_qc_state *engine,qa_actor_id actor,qa_item_id *out,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=actor_for(engine,actor);
    const struct application_qc_item_weapons *p=profile(engine->provider);float value;
    if(!out || !p || !current(a,e))return application_fail(e,QA_ERROR_ARGUMENT,"QC weapon selection lost its actual admitted Source owner");
    if(!qa_qc_entity_float(engine->provider->state.qc.instance,a->reference,p->selected->offset,&value,e))return false;
    for(size_t i=0;i<p->weapon_count;++i)if(p->selected_values[i].value==value) { *out=p->selected_values[i].item;return current(a,e); }
    if(value==0) { *out=0;return current(a,e); }
    return application_fail(e,QA_ERROR_FORMAT,"Original QC selected an undeclared weapon value");
}
static bool declares(void *context,qa_actor_id actor,qa_item_id item,bool *out,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=context;
    if(!qa_actor_id_equal(a->actor,actor) || !current(a,e))return false;
    const struct application_qc_item_weapons *p=profile(a->engine->provider);*out=false;
    for(size_t i=0;i<p->weapon_count;++i)if(p->selected_values[i].item==item) { *out=true;break; }
    return true;
}
static bool accepts(void *context,qa_actor_id actor,qa_item_id item,bool *out,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=context;double owned;
    if(!declares(context,actor,item,out,e))return false;
    if(!*out)return true;
    if(!qa_inventory_count_read(a->engine->provider->application->inventory,actor,item,&owned,e))return false;
    *out=owned>0;return current(a,e);
}
bool application_qc_item_weapons_accepts(struct application_qc_state *engine,qa_actor_id actor,
    qa_item_id item,bool *out,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=actor_for(engine,actor);
    if(!out || !a)return application_fail(e,QA_ERROR_ARGUMENT,"QC weapon acceptance requires its actual item binding");
    return accepts(a,actor,item,out,e);
}
static bool invoke(struct application_qc_item_weapon_actor *a,const application_qc_call *call,qa_error *e)
{
    if(!current(a,e))return false;
    application_qc_inputs inputs={.self=a->actor,.time_ns=a->engine->source_time_ns};
    ++a->calls;bool ok=application_qc_run_call(a->engine,call,&inputs,NULL,e);--a->calls;
    return ok;
}
bool application_qc_item_weapons_select(struct application_qc_state *engine,qa_actor_id actor,
    qa_item_id item,bool *out,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=actor_for(engine,actor);
    if(!out || !a)return application_fail(e,QA_ERROR_ARGUMENT,"QC weapon select requires its actual item binding");
    if(!accepts(a,actor,item,out,e))return false;
    if(!*out)return true;
    const struct application_qc_item_weapons *p=profile(engine->provider);
    for(size_t i=0;i<p->weapon_count;++i)if(p->select_values[i].item==item) {
        if(!qa_qc_set_entity_float(engine->provider->state.qc.instance,a->reference,p->select->offset,p->select_values[i].value,e) ||
            !invoke(a,&p->select_call,e))return false;
        if(a->retired) { *out=false;return true; }
        qa_item_id selected;
        if(!application_qc_item_weapons_selected_read(engine,actor,&selected,e))return false;
        *out=selected==item;return true;
    }
    *out=false;return true;
}
static bool select_weapon(void *context,qa_actor_id actor,qa_item_id item,bool *out,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=context;
    if(!qa_actor_id_equal(a->actor,actor))return false;
    return application_qc_item_weapons_select(a->engine,actor,item,out,e);
}
bool application_qc_item_weapons_settled(struct application_qc_state *engine,qa_actor_id actor,bool *out,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=actor_for(engine,actor);
    const struct application_qc_item_weapons *p=profile(engine->provider);int32_t think;
    if(!out || !p || !current(a,e))return application_fail(e,QA_ERROR_ARGUMENT,"QC weapon continuation lost its admitted Source actor");
    if(!qa_qc_entity_int(engine->provider->state.qc.instance,a->reference,p->think->offset,&think,e))return false;
    *out=true;
    for(size_t i=0;i<p->continuation_count;++i)if(think>=0 && (uint32_t)think==p->continuations[i]) { *out=false;break; }
    return current(a,e);
}
static bool holster(void *context,qa_actor_id actor,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=context;
    return qa_actor_id_equal(a->actor,actor) && current(a,e);
}
static bool holstered(void *context,qa_actor_id actor,bool *out,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=context;
    if(!qa_actor_id_equal(a->actor,actor))return false;
    return application_qc_item_weapons_settled(a->engine,actor,out,e);
}
static bool resume(void *context,qa_actor_id actor,qa_item_id item,bool *out,uint64_t *request,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=context;
    *out=false;*request=0;
    if(!qa_actor_id_equal(a->actor,actor) || !current(a,e))return false;
    if(item)return select_weapon(context,actor,item,out,e);
    const struct application_qc_item_weapons *p=profile(a->engine->provider);
    for(size_t i=0;i<p->resume.count;++i) {
        if(a->retired)return true;
        if(!invoke(a,p->resume.values+i,e))return false;
    }
    if(a->retired)return true;
    *out=true;return current(a,e);
}
bool application_qc_item_weapons_resume(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=actor_for(engine,actor);
    if(!a)return application_fail(e,QA_ERROR_ARGUMENT,"QC weapon resume requires its actual item binding");
    bool accepted;uint64_t request;
    return resume(a,actor,0,&accepted,&request,e);
}
bool application_qc_item_weapons_model_read(struct application_qc_state *engine,qa_actor_id actor,
    const application_qc_resource **out,float *frame,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=actor_for(engine,actor);
    const struct application_qc_item_weapons *p=profile(engine->provider);int32_t model;const char *path;
    if(!out || !frame || !p || !current(a,e))return application_fail(e,QA_ERROR_ARGUMENT,"QC weapon presentation has no admitted Source owner");
    qa_qc_instance *vm=engine->provider->state.qc.instance;
    if(!qa_qc_entity_int(vm,a->reference,p->model->offset,&model,e) || !qa_qc_string(vm,model,&path,e) ||
        !qa_qc_entity_float(vm,a->reference,p->frame->offset,frame,e))return false;
    *out=NULL;
    for(size_t i=0;*path && i<engine->resource_count;++i)if(engine->resources[i].kind==QA_QC_RESOURCE_MODEL && !strcmp(path,qa_strings_cstr(qa_session_strings(engine->services.session), engine->resources[i].name))) {
        if(!engine->resources[i].source)return application_fail(e,QA_ERROR_NOT_FOUND,"QC weapon model lost its actual prepared resource");
        *out=engine->resources+i;break;
    }
    if(*path && !*out)return application_fail(e,QA_ERROR_NOT_FOUND,"Original QC weapon model was not prepared");
    return current(a,e);
}
static bool presentation(void *context,qa_actor_id actor,qa_weapon_presentation *out,qa_error *e)
{
    struct application_qc_item_weapon_actor *a=context;
    qa_item_id active;const application_qc_resource *resource;float frame;
    if(!qa_actor_id_equal(a->actor,actor) || !application_qc_item_weapons_selected_read(a->engine,actor,&active,e) ||
        !application_qc_item_weapons_model_read(a->engine,actor,&resource,&frame,e))return false;
    *out=(qa_weapon_presentation){.provider=a->engine->provider->owner,.active=active,.context=a};return true;
}
bool application_qc_item_weapons_reserve(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    if(!profile(engine->provider))return true;
    struct application_qc_item_weapon_actor *a=actor_for(engine,actor);
    if(a)return current(a,e);
    if(!application_qc_items_actor_current(engine,actor,e))return false;
    a=calloc(1,sizeof(*a));if(!a)return application_fail(e,QA_ERROR_MEMORY,"Retaining QC weapon actor binding");
    a->engine=engine;a->actor=actor;
    if(!application_qc_reference(engine,actor,&a->reference,e)) { free(a);return false; }
    a->next=engine->item_weapon_actors;engine->item_weapon_actors=a;
    return current(a,e);
}
bool application_qc_item_weapons_admit(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    if(!profile(engine->provider))return true;
    if(!application_qc_item_weapons_reserve(engine,actor,e))return false;
    struct application_qc_item_weapon_actor *a=actor_for(engine,actor);
    if(a->bound)return qa_equipment_weapon_binding_is(engine->provider->application->equipment,actor,engine->provider->owner,a);
    qa_equipment_state state;
    if(!engine->provider->application->equipment ||
        !qa_equipment_read(engine->provider->application->equipment,actor,&state))
        return application_fail(e,QA_ERROR_ARGUMENT,"QC weapon items require their real destination weapon slot");
    if(application_provider_for(engine->provider->application,actor,QA_ROLE_ARSENAL,"")==engine->provider)
        return true;
    qa_equipment_weapon_binding binding={.provider=engine->provider->owner,.context=a,
        .current=binding_current,.read=presentation,.declares=declares,.accepts=accepts,.select=select_weapon,
        .holster=holster,.holstered=holstered,.resume=resume};
    if(!qa_equipment_weapon_bind(engine->provider->application->equipment,actor,&binding,e))return false;
    a->bound=true;return true;
}
bool application_qc_item_weapons_finish(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    if(!actor_for(engine,actor))return true;
    return application_qc_item_weapons_admit(engine,actor,e);
}
bool application_qc_item_weapons_release(struct application_qc_state *engine,qa_actor_id actor,qa_error *e)
{
    struct application_qc_item_weapon_actor **at=&engine->item_weapon_actors;
    while(*at && ((*at)->retired || !qa_actor_id_equal((*at)->actor,actor)))at=&(*at)->next;
    if(!*at)return true;
    struct application_qc_item_weapon_actor *a=*at;
    if(a->bound && !qa_equipment_weapon_unbind(engine->provider->application->equipment,actor,engine->provider->owner,a,e))return false;
    a->bound=false;a->retired=true;return true;
}
bool application_qc_item_weapons_close(struct application_qc_state *engine,qa_error *e)
{
    for(struct application_qc_item_weapon_actor *a=engine->item_weapon_actors;a;a=a->next)
        if(a->calls)return application_fail(e,QA_ERROR_ARGUMENT,"QC weapons retain an entered Source call");
    while(engine->item_weapon_actors) {
        struct application_qc_item_weapon_actor *a=engine->item_weapon_actors;
        if(!a->retired && !application_qc_item_weapons_release(engine,a->actor,e))return false;
        engine->item_weapon_actors=a->next;free(a);
    }
    return true;
}
const qa_qc_inline_region *application_qc_item_weapons_regions(const application_provider *provider,size_t *count)
{
    const struct application_qc_item_weapons *p=profile(provider);
    *count=p?p->repeat_count:0;return p?p->regions:NULL;
}
static bool source_selected(struct application_qc_state *engine,qa_qc_instance *vm,bool *out,qa_error *e)
{
    const qa_qc_definition *self=engine->global_bindings->self;int32_t reference;qa_actor_id actor;
    if(vm!=engine->provider->state.qc.instance || !self || self->type!=QA_QC_ENTITY)
        return application_fail(e,QA_ERROR_ARGUMENT,"QC weapon stage lost its actual Source VM self");
    if(!qa_qc_global_int(vm,self->offset,&reference,e) || !qa_qc_reference_actor(vm,reference,&actor,e))return false;
    *out=true;
    for(uint32_t i=1;engine->clients && i<=engine->max_clients;++i)if(engine->clients[i].connected && qa_actor_id_equal(engine->clients[i].actor,actor)) {
        qa_application *app=engine->provider->application;
        *out=application_provider_for(app,actor,QA_ROLE_ARSENAL,"")==engine->provider ?
            qa_equipment_primary_selected(app->equipment,actor) :
            qa_equipment_weapon_selected(app->equipment,actor,engine->provider->owner);
        break;
    }
    return true;
}
bool application_qc_item_weapons_replace(struct application_qc_state *engine,qa_qc_instance *vm,
    const qa_qc_call_event *event,qa_qc_call_next next,bool *handled,qa_error *e)
{
    const struct application_qc_item_weapons *p=profile(engine->provider);*handled=false;
    if(!p || event->function!=p->dispatcher)return true;
    *handled=true;bool selected;
    if(!source_selected(engine,vm,&selected,e))return false;
    if(selected)return qa_qc_call_continue(next,e);
    const uint32_t result[3]={0};return qa_qc_call_skip(next,result,e);
}
bool application_qc_item_weapons_inline(struct application_qc_state *engine,qa_qc_instance *vm,
    const qa_qc_inline_event *event,qa_qc_inline_next next,bool *handled,qa_error *e)
{
    const struct application_qc_item_weapons *p=profile(engine->provider);*handled=false;
    for(size_t i=0;p && i<p->repeat_count;++i)if(p->regions[i].entry==event->region.entry) {
        *handled=true;bool selected;
        if(!source_selected(engine,vm,&selected,e) || !qa_qc_inline_continue(next,e))return false;
        return selected || qa_qc_set_global_float(vm,p->released[i],p->release_values[i],e);
    }
    return true;
}
