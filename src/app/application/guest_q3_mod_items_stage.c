#include "guest_q3_mod_items_private.h"
static int32_t wrapped(double value)
{double n=fmod(trunc(value),4294967296.0);if(n<0)n+=4294967296.0;uint32_t bits=(uint32_t)n;int32_t out;memcpy(&out,&bits,4);return out;}
static bool word_write(item_actor *a,item_field f,int32_t value,qa_error *e)
{uint32_t address;uint8_t bytes[4];qa_store_u32le(bytes,(uint32_t)value);return q3items_address(a,f,&address,e)&&qa_qvm_write(a->owner->mod->vm,address,(qa_bytes){bytes,4},e);}
static bool pointer(application_q3_mod_items *o,const mod_pointer *p,const qa_qvm_call *call,uint32_t *out,qa_error *e)
{
    int32_t word;uint8_t bytes[4];
    if(p->argument){if(!qa_qvm_call_argument(call,p->root,&word,e))return false;}
    else {if(!qa_qvm_read(o->mod->vm,p->root,bytes,4,e))return false;word=qa_load_i32le(bytes);}
    int64_t address=word;
    for(size_t i=0;i<p->count;++i){int64_t at=address+p->indirections[i];if(at<0||at>UINT32_MAX||!qa_qvm_read(o->mod->vm,(uint32_t)at,bytes,4,e))return false;address=qa_load_i32le(bytes);}
    address+=p->offset;if(address<0||address>UINT32_MAX)return q3mod_fail(e,QA_ERROR_ARGUMENT,"Weapon pointer leaves actual source memory");*out=(uint32_t)address;return true;
}
static bool source_actor(application_q3_mod_items *o,const item_actor_pointer *p,const qa_qvm_call *call,item_actor **out,qa_error *e)
{
    *out=NULL;
    if(!o->application)return true;
    item_actor *a=q3items_actor(o,o->application->actor);if(!a||!q3items_current(a,NULL))return true;
    uint32_t source,wanted;if(!pointer(o,&p->pointer,call,&source,e)||
        !q3mod_address(o->mod,a->actor,o->profile->source->records[p->record].id,0,0,&wanted,e))return false;
    if(source==wanted)*out=a;
    return true;
}
static bool cancel(application_q3_mod_items_entry *entry,const qa_qvm_call *current,qa_error *e)
{
    const qa_qvm_call *scope=&entry->call;
    for(application_q3_mod_items_entry *p=entry;p;p=p->previous)if(p->input&&qa_actor_id_equal(p->actor,entry->actor)){scope=&p->call;break;}
    bool cancelled=false;if(!qa_qvm_call_cancelled(current,&cancelled,e))return false;
    return cancelled||qa_qvm_cancel(scope,e);
}
static bool decision(void *context,const qa_qvm_call *call,bool original,bool *taken,qa_error *e)
{
    application_q3_mod_items_entry *entry=context;application_q3_mod_items *o=entry->owner;item_stage *s=o->profile->stage;
    item_actor *a=q3items_actor(o,entry->actor);if(!a||!q3items_current(a,e)){*taken=original;return cancel(entry,call,e);}
    if(entry->continuation&&call->instruction==s->continue_branch){bool matches;
        if(!q3items_tests(a,s->when,s->when_count,&matches,e))return false;
        *taken=original;if(original==s->original_taken&&matches){entry->continued=true;*taken=!original;}return true;
    }
    const item_predicate *predicates=entry->continuation?s->continue_predicates:s->predicates;
    size_t count=entry->continuation?s->continue_predicate_count:s->predicate_count;
    for(size_t i=0;i<count;++i)if(predicates[i].instruction==call->instruction){*taken=o->services.selected(o->services.context,a->actor)?original:predicates[i].unselected;return true;}
    return q3mod_fail(e,QA_ERROR_ARGUMENT,"Weapon branch is outside its actual declared scope");
}
bool application_q3_mod_items_open(application_q3_mod_items *o,qa_actor_id actor,application_q3_mod_application *source,
    application_q3_mod_items_application **out,qa_error *e)
{
    if(!o||!source||!out||*out||!application_q3_mod_application_current(o->mod,source,actor)||!q3items_current(q3items_actor(o,actor),e))return false;
    application_q3_mod_items_application *a=calloc(1,sizeof(*a));if(!a)return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining source weapon input application");
    a->owner=o;a->source=source;a->actor=actor;a->previous=o->application;o->application=a;*out=a;return true;
}
bool application_q3_mod_items_close(application_q3_mod_items_application **in,qa_error *e)
{
    if(!in)return false;application_q3_mod_items_application *a=*in;if(!a)return true;
    if(a->owner->application!=a||a->owner->entries)return q3mod_fail(e,QA_ERROR_ARGUMENT,"Weapon application retains a nested original call");
    a->owner->application=a->previous;free(a);*in=NULL;return true;
}
bool application_q3_mod_items_apply(application_q3_mod_items_application *a,uint32_t entry,
    const application_q3_mod_inputs *values,qa_error *e)
{
    if(!a||!values)return false;item_stage *s=a->owner->profile->stage;
    if(!s||entry!=s->input_entry)return true;
    if(a->applied||a->owner->application!=a||!application_q3_mod_application_current(a->owner->mod,a->source,a->actor)||!q3items_current(q3items_actor(a->owner,a->actor),e))return q3mod_fail(e,QA_ERROR_ARGUMENT,"Weapon input must consume its real movement slice once");
    a->applied=true;
    const application_q3_mod_value *time=values->values+Q3_MOD_TIME,*elapsed=values->values+Q3_MOD_ELAPSED;
    if(time->kind!=Q3_MOD_VALUE_SCALAR||elapsed->kind!=Q3_MOD_VALUE_SCALAR||!isfinite(time->as.scalar)||
        !isfinite(elapsed->as.scalar)||elapsed->as.scalar<0||!isfinite(time->as.scalar*1000)||!isfinite(elapsed->as.scalar*1000))return false;
    return word_write(q3items_actor(a->owner,a->actor),s->clock,wrapped(trunc(time->as.scalar*1000)-trunc(elapsed->as.scalar*1000)),e);
}
size_t application_q3_mod_items_entry_count(const application_q3_mod_items *o){return o&&o->profile->stage?4:0;}
bool application_q3_mod_items_entry_instruction(const application_q3_mod_items *o,size_t i,uint32_t *out)
{if(!o||!o->profile->stage||!out||i>=4)return false;item_stage *s=o->profile->stage;uint32_t values[]={s->input_entry,s->dispatch_entry,s->request_entry,s->continue_entry};*out=values[i];return true;}
bool application_q3_mod_items_entry_begin(application_q3_mod_items *o,const qa_qvm_call *call,application_q3_mod_items_entry **out,qa_error *e)
{
    if(!o||!call||!out||*out||call->vm!=o->mod->vm||!q3mod_current(o->mod,e))return false;
    item_stage *s=o->profile->stage;if(!s)return true;item_actor *a=NULL;
    bool dispatch=call->instruction==s->dispatch_entry,continuation=call->instruction==s->continue_entry,
        request=call->instruction==s->request_entry,input=call->instruction==s->input_entry;
    if(dispatch){if(!source_actor(o,&s->dispatcher,call,&a,e))return false;}
    else if(continuation){if(!source_actor(o,&s->continuation,call,&a,e))return false;}
    else if(input&&o->application)a=q3items_actor(o,o->application->actor);
    else if(request){for(application_q3_mod_items_entry *p=o->entries;p;p=p->previous)if(p->dispatcher){a=q3items_actor(o,p->actor);break;}}
    if(!a)return true;
    application_q3_mod_items_entry *entry=calloc(1,sizeof(*entry));if(!entry)return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining original source weapon entry");
    entry->owner=o;entry->call=*call;entry->actor=a->actor;entry->dispatcher=dispatch;entry->continuation=continuation;entry->request=request;entry->input=input;
    entry->previous=o->entries;o->entries=entry;*out=entry;
    if(!q3items_current(a,e))return cancel(entry,call,e);
    if(request){if(!q3items_tests(a,s->accepted,s->accepted_count,&entry->accepted,e)||!qa_qvm_call_argument(call,s->request_argument,&entry->requested,e))return false;
        if(!entry->accepted&&a->request.id&&a->status==Q3_ITEM_REQUEST_PENDING){int32_t value;bool present;
            if(!application_q3_mod_items_requested(o,a->actor,&value,&present,e))return false;if(present&&value==entry->requested)a->attempted=true;}
    }
    if(continuation){uint8_t last;
        if(!pointer(o,&s->movement,call,&entry->movement,e)||(uint64_t)entry->movement+s->movement_length>UINT32_MAX||
            !qa_qvm_read(o->mod->vm,entry->movement+s->movement_length-1,&last,1,e))return false;
    }
    size_t count=continuation?s->continue_predicate_count+1:dispatch?s->predicate_count:0;
    if(count){entry->branches=calloc(count,sizeof(*entry->branches));if(!entry->branches)return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining genuine weapon branch decisions");
        for(size_t i=0;i<count;++i){uint32_t instruction=continuation?(i==s->continue_predicate_count?s->continue_branch:s->continue_predicates[i].instruction):s->predicates[i].instruction;
            entry->branches[i]=(qa_qvm_branch_binding){instruction,decision,entry};}
        if(!qa_qvm_bind_branches(call,entry->branches,count,e))return false;
    }return true;
}
static bool vector_write(application_q3_mod_items_entry *entry,uint32_t offset,qa_vec3 value,qa_error *e)
{uint8_t bytes[12];qa_store_f32le(bytes,value.x);qa_store_f32le(bytes+4,value.y);qa_store_f32le(bytes+8,value.z);return qa_vec_finite(value)&&qa_qvm_write(entry->owner->mod->vm,entry->movement+offset,(qa_bytes){bytes,12},e);}
bool application_q3_mod_items_entry_end(application_q3_mod_items_entry **in,bool succeeded,qa_error *e)
{
    if(!in)return false;application_q3_mod_items_entry *entry=*in;if(!entry)return true;application_q3_mod_items *o=entry->owner;
    if(o->entries!=entry)return q3mod_fail(e,QA_ERROR_ARGUMENT,"Weapon entry cleanup is outside its source nesting");
    item_actor *a=q3items_actor(o,entry->actor);item_stage *s=o->profile->stage;bool ok=true;
    if(succeeded&&a&&q3items_current(a,e)){
        if(entry->request&&!entry->accepted){bool accepted;ok=q3items_tests(a,s->accepted,s->accepted_count,&accepted,e);
            if(ok&&accepted&&a->request.id&&a->status==Q3_ITEM_REQUEST_PENDING){int32_t value;bool present;ok=application_q3_mod_items_requested(o,a->actor,&value,&present,e);if(ok&&present&&value==entry->requested)a->status=Q3_ITEM_REQUEST_ACCEPTED;}}
        if(ok&&entry->dispatcher&&a->request.id&&a->status==Q3_ITEM_REQUEST_PENDING){qa_item_id active;ok=q3items_active(a,&active,e);if(ok){if(active==a->request.item)a->status=Q3_ITEM_REQUEST_ACCEPTED;else if(a->attempted)a->status=Q3_ITEM_REQUEST_REFUSED;}}
        if(ok&&entry->continuation&&entry->continued){qa_bounds bounds;double height;int32_t ground;
            ok=o->services.posture(o->services.context,a->actor,&bounds,&height,&ground,e)&&isfinite(height)&&q3items_current(a,e)&&
                vector_write(entry,s->minimum,bounds.mins,e)&&vector_write(entry,s->maximum,bounds.maxs,e)&&
                word_write(a,s->view_height,wrapped(height),e)&&word_write(a,s->ground,ground,e);
            for(size_t i=0;ok&&i<s->call_count;++i){if(!q3items_current(a,e)){ok=cancel(entry,&entry->call,e);break;}
                application_q3_mod_inputs values={0};bool found=false;double result;
                ok=application_q3_mod_input_current(o->mod,a->actor,&values,&found,e)&&found&&application_q3_mod_call_run(o->mod,s->calls[i].call,&values,&result,e);
            }
        }
    }
    o->entries=entry->previous;free(entry->branches);free(entry);*in=NULL;return ok;
}
bool application_q3_mod_items_hook_run(application_q3_mod_items *o,const qa_qvm_call *call,
    application_q3_mod_items_proceed proceed,void *context,int32_t *result,qa_error *e)
{
    if(!proceed)return false;application_q3_mod_items_entry *entry=NULL;
    bool ok=application_q3_mod_items_entry_begin(o,call,&entry,e);
    if(ok)ok=proceed(context,call,result,e);
    qa_error cleanup={0};bool ended=application_q3_mod_items_entry_end(&entry,ok,ok?e:&cleanup);
    return ok&&ended;
}
bool application_q3_mod_items_weapon_read(application_q3_mod_items *o,qa_actor_id actor,application_q3_items_weapon_view *out,qa_error *e)
{
    item_actor *a=q3items_actor(o,actor);if(!out||!a||!o->profile->stage||!q3items_current(a,e))return false;
    qa_item_id active;bool settled;if(!q3items_active(a,&active,e)||!q3items_tests(a,o->profile->stage->settled,o->profile->stage->settled_count,&settled,e))return false;
    qa_item_id pending=a->request.id&&a->status!=Q3_ITEM_REQUEST_REFUSED&&a->request.item!=active?a->request.item:0;
    *out=(application_q3_items_weapon_view){actor,o->mod->owner,active,pending,settled};return true;
}
static bool value_for(application_q3_mod_items *o,qa_item_id item,int32_t *out)
{if(!item)return false;item_stage *s=o->profile->stage;for(size_t i=0;s&&i<s->value_count;++i)if(s->values[i].item==item){*out=s->values[i].value;return true;}return false;}
bool application_q3_mod_items_request(application_q3_mod_items *o,qa_actor_id actor,qa_item_id item,application_q3_item_request *out,qa_error *e)
{
    item_actor *a=q3items_actor(o,actor);if(!a||!out||!o->profile->stage||!q3items_current(a,e)||o->next_request==UINT64_MAX)return false;
    bool accepted=false;int32_t value;
    if(item){double count;if(value_for(o,item,&value)){if(!qa_inventory_count_read(o->inventory,actor,item,&count,e))return false;accepted=count>0;}}
    else {qa_item_id active;bool settled;if(!q3items_active(a,&active,e)||!q3items_tests(a,o->profile->stage->settled,o->profile->stage->settled_count,&settled,e))return false;accepted=active&&settled;}
    a->request=(application_q3_item_request){actor,++o->next_request,item};a->status=accepted?(item?Q3_ITEM_REQUEST_PENDING:Q3_ITEM_REQUEST_ACCEPTED):Q3_ITEM_REQUEST_REFUSED;
    a->attempted=false;*out=a->request;return true;
}
bool application_q3_mod_items_request_status(application_q3_mod_items *o,const application_q3_item_request *r,application_q3_item_request_status *out,qa_error *e)
{if(!o||!r||!out)return false;item_actor *a=q3items_actor(o,r->actor);*out=a&&q3items_current(a,e)&&a->request.id==r->id&&a->request.item==r->item?a->status:Q3_ITEM_REQUEST_REFUSED;return true;}
bool application_q3_mod_items_request_cancel(application_q3_mod_items *o,const application_q3_item_request *r,qa_error *e)
{if(!o||!r)return false;item_actor *a=q3items_actor(o,r->actor);if(a&&q3items_current(a,e)&&a->request.id==r->id&&a->request.item==r->item&&a->status==Q3_ITEM_REQUEST_PENDING){a->status=Q3_ITEM_REQUEST_REFUSED;a->request=(application_q3_item_request){0};}return true;}
bool application_q3_mod_items_requested(application_q3_mod_items *o,qa_actor_id actor,int32_t *out,bool *present,qa_error *e)
{if(!o||!out||!present)return false;item_actor *a=q3items_actor(o,actor);*present=a&&q3items_current(a,e)&&a->request.id&&a->status!=Q3_ITEM_REQUEST_REFUSED&&value_for(o,a->request.item,out);return true;}
size_t application_q3_mod_items_definition_count(const application_q3_mod_items_profile *p){return p?p->definition_count:0;}
bool application_q3_mod_items_definition(const application_q3_mod_items_profile *p,size_t i,qa_item_admission *out,qa_bytes *icon,qa_bytes *held,qa_error *e)
{if(!p||!out||!icon||!held||i>=p->definition_count)return q3mod_fail(e,QA_ERROR_ARGUMENT,"Item metadata index leaves actual declaration");const item_definition *d=p->definitions+i;*out=d->admission;*icon=(qa_bytes){d->icon.data,d->icon.size};*held=(qa_bytes){d->held.data,d->held.size};return true;}
