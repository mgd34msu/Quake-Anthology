#include "native_q2_records_private.h"

bool nqr_current(application_native_q2_records *o,qa_error *e)
{
    if(!o||o->document!=application_native_q2_callbacks_document(o->options.callbacks))
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native projection lost its actual callback document owner");
    if(!o->options.current(o->options.context,e)) return false;
    return !qa_native_terminal(o->options.instance)||nqr_fail(e,QA_ERROR_ARGUMENT,"Native projection source is terminal");
}
bool nqr_live(application_native_q2_records *o,qa_actor_id actor)
{ return qa_actors_get(qa_session_actors(o->options.session),actor)!=NULL; }
nqr_actor *nqr_find(application_native_q2_records *o,qa_actor_id actor)
{ for(nqr_actor *r=o->actors;r;r=r->next) if(qa_actor_id_equal(r->actor,actor)) return r; return NULL; }
bool nqr_actor_current(application_native_q2_records *o,const nqr_actor *actor,qa_error *e)
{
    if(!nqr_live(o,actor->actor)||actor->retired||actor->failed) return nqr_fail(e,QA_ERROR_NOT_FOUND,"Native projection actor retired");
    uint32_t index=0; bool client=false;
    if(!o->options.client_slot(o->options.context,actor->actor,&index,&client,e)) return false;
    if(client!=actor->client||(client&&index!=actor->index)) return nqr_fail(e,QA_ERROR_NOT_FOUND,"Native projection changed its actual client row");
    if(actor->bound&&!o->options.binding_current(o->options.context,actor->actor,o->records[o->entity_record].first+actor->index))
        return nqr_fail(e,QA_ERROR_NOT_FOUND,"Native projection lost its actual host slot binding");
    return nqr_current(o,e);
}
bool nqr_address(application_native_q2_records *o,const nqr_actor *actor,const nqr_record *record,qa_native_address *out,qa_error *e)
{
    if((record->client&&!actor->client)||actor->index>=record->capacity)
        return nqr_fail(e,QA_ERROR_FORMAT,"Native actor has no declared private record row");
    if(!nqr_current(o,e)||!o->options.record_source(o->options.context,(size_t)(record-o->records)+1,actor->index,out,e)) return false;
    return (*out&&*out<=UINT64_MAX-record->stride)||nqr_fail(e,QA_ERROR_FORMAT,"Native record row leaves its actual address extent");
}
bool nqr_capacity(application_native_q2_records *o,const nqr_actor *actor,qa_error *e)
{
    for(size_t i=0;i<o->record_count;++i) {
        nqr_record *r=o->records+i; if(r->client&&!actor->client) continue;
        if(actor->index>=r->capacity) return nqr_fail(e,QA_ERROR_FORMAT,"Native projection exceeds its declared auxiliary record capacity");
        for(size_t j=0;j<r->field_count;++j) if(r->fields[j].kind==NQR_CAPACITY&&
            !qa_inventory_mutable_capacity(o->options.inventory,actor->actor,r->fields[j].item))
            return nqr_fail(e,QA_ERROR_ARGUMENT,"Native declaration requires actual mutable inventory capacity");
    }
    return true;
}
bool nqr_seed(application_native_q2_records *o,nqr_actor *actor,bool constants,qa_error *e)
{
    if(!nqr_actor_current(o,actor,e)) return false;
    ++o->projection_depth; bool ok=true;
    uint8_t pointer_bytes=qa_native_module_describe(qa_native_get_module(o->options.instance)).image.target.pointer_bytes;
    for(size_t i=0;ok&&i<o->record_count;++i) {
        nqr_record *record=o->records+i; if(record->client&&!actor->client) continue;
        qa_native_address base; ok=nqr_address(o,actor,record,&base,e);
        for(size_t j=0;ok&&j<record->field_count;++j) {
            nqr_field *f=record->fields+j; uint8_t raw[12]; const uint8_t *bytes=f->initial;
            if(f->kind==NQR_CONSTANT||f->kind==NQR_VECTOR) { if(!constants) continue; }
            else if(f->kind==NQR_LINK||f->kind==NQR_ADDRESS) {
                qa_native_address pointer=0;
                if(f->kind==NQR_LINK) ok=application_native_q2_records_pointer(o,actor->actor,f->target+1,&pointer,e);
                else if(qa_json_type(o->document,f->address)!=QA_JSON_NULL)
                    ok=application_native_q2_callbacks_address(o->options.callbacks,f->address,&pointer,e);
                if(ok&&pointer_bytes==4&&pointer>UINT32_MAX) ok=nqr_fail(e,QA_ERROR_FORMAT,"Native record link exceeds its real pointer width");
                if(pointer_bytes==4) qa_store_u32le(raw,(uint32_t)pointer); else qa_store_u64le(raw,pointer);
                bytes=raw;
            } else if(constants&&f->kind==NQR_BODY&&(f->body==QA_BODY_MINIMUM||f->body==QA_BODY_MAXIMUM)) {
                if(!qa_world_body_storage_serial(o->options.world,actor->actor)) continue;
                qa_body_state body; ok=qa_world_body_read(o->options.world,actor->actor,&body,e);
                if(ok) {
                    qa_vec3 v=*qa_body_vector(&body,f->body);
                    ok=nqr_scalar_encode(v.x,QA_NATIVE_F32,raw,e)&&nqr_scalar_encode(v.y,QA_NATIVE_F32,raw+4,e)&&nqr_scalar_encode(v.z,QA_NATIVE_F32,raw+8,e);
                }
                bytes=raw;
            } else continue;
            if(ok) ok=qa_native_write(o->options.instance,base+f->offset,(qa_bytes){bytes,f->length},e)&&nqr_current(o,e);
        }
    }
    --o->projection_depth; return ok;
}
static bool lifecycle(application_native_q2_records *o,nqr_actor *actor,const char *section,size_t *cursor,qa_error *e)
{
    qa_json_id calls=qa_json_get(o->document,qa_json_root(o->document),section);
    double time;
    if(!o->options.time(o->options.context,&time,e)||!isfinite(time)) return nqr_fail(e,QA_ERROR_ARGUMENT,"Native projection lost its actual source clock");
    application_native_callback_value values[]={
        {.name="self",.kind=APPLICATION_NATIVE_VALUE_ACTOR,.value.actor=actor->actor},
        {.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=time}};
    application_native_callback_inputs inputs={.values=values,.count=2};
    ++o->lifecycle_depth; bool ok=true;
    while(ok&&*cursor<qa_json_size(o->document,calls)) {
        qa_json_id call=qa_json_at(o->document,calls,*cursor); double ignored; bool entered=false;
        ok=o->options.lifecycle(o->options.context,call,&inputs,&ignored,&entered,e);
        /* A reached call can fail after changing the real private row. */
        if(ok||entered) ++*cursor;
    }
    --o->lifecycle_depth; return ok;
}
bool nqr_releases(application_native_q2_records *o,qa_error *e)
{
    if(o->frame||o->lifecycle_depth||o->projection_depth||o->restoring) return true;
    for(nqr_actor **slot=&o->actors;*slot;) {
        nqr_actor *actor=*slot;
        if(!actor->retired) { slot=&actor->next; continue; }
        if(!actor->client&&!lifecycle(o,actor,"release",&actor->release_cursor,e)) return false;
        if(actor->bound) {
            uint32_t source_slot=o->records[o->entity_record].first+actor->index;
            if(!o->options.released(o->options.context,actor->actor,source_slot,e)) return false;
            actor->bound=false;
        }
        *slot=actor->next; free(actor);
    }
    return true;
}
bool application_native_q2_records_pointer(application_native_q2_records *o,qa_actor_id actor,size_t id,qa_native_address *out,qa_error *e)
{
    if(!id||!out||!nqr_current(o,e)) return false;
    if(!actor.registry) { *out=0; return true; }
    nqr_record *record=id<=o->record_count?o->records+id-1:NULL;
    if(!record) return nqr_fail(e,QA_ERROR_FORMAT,"Native callback names an undeclared actor record");
    nqr_actor *row=nqr_find(o,actor);
    if(row&&row->retired&&o->lifecycle_depth) return nqr_address(o,row,record,out,e);
    if(!nqr_live(o,actor)||(o->restoring&&!o->lifecycle_depth)) return nqr_fail(e,QA_ERROR_NOT_FOUND,"Native record requires a live full actor outside restore");
    bool found=false;
    if(!o->options.owned_record(o->options.context,actor,id,out,&found,e)) return false;
    if(found) return true;
    uint32_t client_slot=0; bool client=false;
    if(!o->options.client_slot(o->options.context,actor,&client_slot,&client,e)) return false;
    if(record->client&&!client) { *out=0; return true; }
    if(row) {
        if(row->retired||row->failed||row->client!=client||(client&&row->index!=client_slot)) return nqr_fail(e,QA_ERROR_NOT_FOUND,"Native projection changed its retained actor/client identity");
        return nqr_address(o,row,record,out,e);
    }
    if(o->closing) return nqr_fail(e,QA_ERROR_ARGUMENT,"Native projection owner is retiring");
    if(!nqr_releases(o,e)) return false;
    uint32_t index=client?client_slot:o->client_maximum;
    for(;;++index) {
        bool used=false; for(nqr_actor *r=o->actors;r;r=r->next) if(r->index==index) { used=true; break; }
        if(!used) break;
        if(client||index==UINT32_MAX) return nqr_fail(e,QA_ERROR_FORMAT,"Native source client projection is occupied");
    }
    row=calloc(1,sizeof(*row)); if(!row) return nqr_fail(e,QA_ERROR_MEMORY,"Retaining native full actor projection");
    *row=(nqr_actor){.actor=actor,.index=index,.client=client};
    if(!nqr_capacity(o,row,e)) { free(row); return false; }
    for(size_t i=0;i<o->record_count;++i) {
        if(o->records[i].client&&!client) continue;
        qa_native_address base;
        if(!nqr_address(o,row,o->records+i,&base,e)) { free(row); return false; }
    }
    nqr_actor **tail=&o->actors; while(*tail) tail=&(*tail)->next; *tail=row;
    bool ok=true;
    if(o->entity_record!=SIZE_MAX) {
        uint64_t source_slot=(uint64_t)o->records[o->entity_record].first+index;
        if(source_slot>UINT32_MAX) ok=nqr_fail(e,QA_ERROR_FORMAT,"Native projected source slot overflows");
        else {
            ok=o->options.bound(o->options.context,actor,(uint32_t)source_slot,e);
            row->bound=ok;
        }
    }
    if(!ok) { *tail=NULL; free(row); return false; }
    if(ok) ok=nqr_seed(o,row,true,e);
    size_t project_cursor=0;
    if(ok&&!client) ok=lifecycle(o,row,"project",&project_cursor,e);
    if(ok) ok=nqr_actor_current(o,row,e)&&nqr_seed(o,row,true,e);
    if(!ok) {
        row->retired=row->failed=true; qa_error first=e?*e:(qa_error){0},cleanup={0};
        (void)nqr_releases(o,&cleanup); if(e) *e=first;
        return false;
    }
    return nqr_address(o,row,record,out,e);
}
bool application_native_q2_records_release(application_native_q2_records *o,qa_actor_id actor,qa_error *e)
{
    if(!o) return true;
    nqr_actor *row=nqr_find(o,actor); if(row) row->retired=true;
    return nqr_releases(o,e);
}
bool application_native_q2_records_create(const application_native_q2_records_options *options,application_native_q2_records **out,qa_error *e)
{
    if(!options||!out||*out||!options->callbacks||!options->instance||!options->session||!options->world||!options->combat||
        !options->inventory||!options->strings||!options->current||!options->owned_record||!options->client_slot||!options->client_admitted||!options->record_source||!options->record_validate||!options->bound||!options->binding_current||!options->released||!options->time||!options->lifecycle)
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native projection requires its actual module and canonical owners");
    application_native_q2_records *o=calloc(1,sizeof(*o)); if(!o) return nqr_fail(e,QA_ERROR_MEMORY,"Owning native declared actor projection");
    o->options=*options; o->document=application_native_q2_callbacks_document(options->callbacks); *out=o;
    return nqr_current(o,e)&&nqr_profile(o,e);
}
bool application_native_q2_records_idle(const application_native_q2_records *o)
{ return !o||(!o->frame&&!o->pickup&&!o->projection_depth&&!o->lifecycle_depth); }
bool application_native_q2_records_writing(const application_native_q2_records *o)
{ return o&&o->projection_depth!=0; }
bool application_native_q2_records_lifecycle(const application_native_q2_records *o)
{ return o&&o->lifecycle_depth!=0; }
bool application_native_q2_records_restoring(const application_native_q2_records *o)
{ return o&&o->restoring; }
bool application_native_q2_records_destroy(application_native_q2_records **owner,qa_error *e)
{
    if(!owner||!*owner) return true;
    application_native_q2_records *o=*owner;
    if(!application_native_q2_records_idle(o)) return nqr_fail(e,QA_ERROR_ARGUMENT,"Native projection retains an entered source scope");
    o->closing=true;
    if(!o->restoring&&!qa_native_terminal(o->options.instance)) {
        for(nqr_actor *r=o->actors;r;r=r->next) r->retired=true;
        if(!nqr_releases(o,e)) return false;
    } else for(nqr_actor *r=o->actors;r;r=r->next) if(r->bound) {
        uint32_t source_slot=o->records[o->entity_record].first+r->index;
        if(!o->options.released(o->options.context,r->actor,source_slot,e)) return false;
        r->bound=false;
    }
    while(o->actors) { nqr_actor *r=o->actors; o->actors=r->next; free(r); }
    for(size_t i=0;o->records&&i<o->record_count;++i) { nqr_record *r=o->records+i; for(size_t j=0;r->fields&&j<r->field_count;++j) free(r->fields[j].teams); free(r->fields); free(r->id); }
    free(o->records); free(o); *owner=NULL; return true;
}
