#include "guest_q3_component_records_private.h"
#include "control_frame.h"

static bool projected(application_q3_component_records *r,qa_actor_id actor)
{ component_actor *row=q3records_actor(r,actor); return row&&row->projected&&!row->retired&&!row->owned&&q3records_live(r,actor); }
static bool applies(const component_actor *actor,const component_record *record,const component_field *f)
{ return actor->projected&&!actor->retired&&!actor->owned&&(!record->client||actor->client)&&f->kind<=COMPONENT_BODY&&!(actor->admitted&&f->body_output); }
static bool reserve(component_frame *frame,component_observation **rows,size_t *capacity,size_t count,qa_error *e)
{
    if(count<=*capacity) return true;
    size_t extent=*capacity?*capacity:16;
    while(extent<count) {
        if(extent>SIZE_MAX/2) { extent=count; break; }
        extent*=2;
    }
    component_observation *next=qa_unified_frame_lease_alloc(frame->storage,extent,sizeof(*next),
        _Alignof(component_observation),e);
    if(!next) return false;
    if(*capacity) memcpy(next,*rows,*capacity*sizeof(*next));
    *rows=next; *capacity=extent; return true;
}
static bool observations(application_q3_component_records *r,component_frame *frame,qa_error *e)
{
    size_t used=0;
    for(size_t i=0;i<r->actor_count;++i) {
        component_actor actor=r->actors[i];
        for(size_t j=0;j<r->record_count;++j) {
            component_record *record=r->records+j;
            for(size_t k=0;k<record->field_count;++k) {
                component_field *f=record->fields+k; if(!f->writable||!applies(&actor,record,f)) continue;
                if(!reserve(frame,&frame->scratch,&frame->scratch_capacity,used+1,e)) return false;
                component_observation *row=frame->scratch+used++;
                *row=(component_observation){.actor=actor.actor,.field=f,.address=record->address+actor.slot*record->stride+f->offset};
                if(!qa_qvm_read(r->options.vm,row->address,row->bytes,f->length,e)) return false;
            }
        }
    }
    component_observation *previous=frame->observations; size_t capacity=frame->observation_capacity;
    frame->observations=frame->scratch; frame->observation_capacity=frame->scratch_capacity;
    frame->scratch=previous; frame->scratch_capacity=capacity; frame->observation_count=used;
    return true;
}
static bool rebase(application_q3_component_records *r,qa_error *e)
{
    for(component_frame *frame=r->frame;frame;frame=frame->outer) {
        if(!observations(r,frame,e)) return false;
    }
    return true;
}
static bool scalar_read(application_q3_component_records *r,qa_actor_id actor,const component_field *f,double *value,qa_error *e)
{
    if(f->kind==COMPONENT_HEALTH) { qa_combat_state state; if(!qa_combat_read(r->options.combat,actor,&state,e)) return false; *value=state.health; return true; }
    if(f->kind==COMPONENT_INVENTORY) return qa_inventory_count_read(r->options.inventory,actor,f->item,value,e);
    qa_string_id team=0; double score=0;
    if(!r->options.match_read||!r->options.match_read(r->options.context,actor,&team,&score,e)) return false;
    if(f->kind==COMPONENT_SCORE) { *value=score; return true; }
    for(size_t i=0;i<f->team_count;++i) if(f->teams[i].team==team) { *value=f->teams[i].value; return true; }
    return q3records_fail(e,QA_ERROR_FORMAT,"Actual shared team has no component source alias");
}
bool application_q3_component_records_refresh(application_q3_component_records *r,qa_error *e)
{
    if(!r||!r->options.storage_current(r->options.context,e)) return false;
    bool previous=r->refreshing; r->refreshing=true; bool ok=true;
    for(size_t i=0;ok&&i<r->actor_count;++i) {
        component_actor actor=r->actors[i];
        for(size_t j=0;ok&&j<r->record_count;++j) {
            component_record *record=r->records+j;
            for(size_t k=0;ok&&k<record->field_count;++k) {
                component_field *f=record->fields+k; if(!applies(&actor,record,f)) continue;
                uint8_t bytes[12];
                if(f->kind<=COMPONENT_SCORE) { double value; ok=scalar_read(r,actor.actor,f,&value,e)&&q3records_scalar(value,f->floating,bytes,e); }
                else {
                    qa_body_state body; ok=qa_world_body_read(r->options.world,actor.actor,&body,e);
                    if(ok) { qa_vec3 v=*qa_body_vector(&body,f->body); double values[]={v.x,v.y,v.z}; for(size_t n=0;ok&&n<3;++n) ok=q3records_scalar(values[n],true,bytes+4*n,e); }
                }
                if(ok&&!projected(r,actor.actor)) ok=q3records_fail(e,QA_ERROR_NOT_FOUND,"Component canonical actor changed during projection read");
                if(ok) ok=q3records_raw(r,record->address+actor.slot*record->stride+f->offset,(qa_bytes){bytes,f->length},e);
            }
        }
    }
    if(ok) ok=rebase(r,e);
    r->refreshing=previous; return ok;
}
static bool capture(application_q3_component_records *r,qa_error *e)
{
    component_frame *frame=r->frame; if(!frame||r->refreshing) return true;
    if(!reserve(frame,&frame->changed,&frame->changed_capacity,frame->observation_count,e)) return false;
    size_t count=0;
    for(size_t i=0;i<frame->observation_count;++i) {
        component_observation row=frame->observations[i]; uint8_t bytes[12];
        if(!qa_qvm_read(r->options.vm,row.address,bytes,row.field->length,e)) return false;
        if(!projected(r,row.actor)||!memcmp(row.bytes,bytes,row.field->length)) continue;
        memcpy(row.bytes,bytes,row.field->length); frame->changed[count++]=row;
    }
    /* Rebase every nested scope before any canonical callback can reenter. */
    if(!rebase(r,e)) return false;
    if(count) {
        if(count>SIZE_MAX/sizeof(*frame->changed)-frame->pending_count) return q3records_fail(e,QA_ERROR_MEMORY,"Component write queue overflows");
        if(!reserve(frame,&frame->pending,&frame->pending_capacity,frame->pending_count+count,e)) return false;
        memcpy(frame->pending+frame->pending_count,frame->changed,count*sizeof(*frame->changed)); frame->pending_count+=count;
    }
    return true;
}
static bool commit(application_q3_component_records *r,const component_observation *row,qa_error *e)
{
    if(!projected(r,row->actor)) return q3records_fail(e,QA_ERROR_NOT_FOUND,"Component source wrote a retired canonical actor");
    component_field *f=row->field;
    if(f->kind<=COMPONENT_SCORE) {
        double value=f->floating?(double)qa_load_f32le(row->bytes):(double)qa_load_i32le(row->bytes); uint8_t checked[4];
        if(!q3records_scalar(value,f->floating,checked,e)) return false;
        if(f->kind==COMPONENT_HEALTH) {
            float health=(float)value; if(!isfinite(health)) return q3records_fail(e,QA_ERROR_FORMAT,"Component health exceeds canonical float");
            return qa_combat_set_health(r->options.combat,row->actor,health,e);
        }
        if(f->kind==COMPONENT_INVENTORY) {
            if(r->options.inventory_write&&!r->options.inventory_write(r->options.context,row->actor,f->item,true,false,e)) return false;
            qa_inventory_entry entry;
            if(!qa_inventory_entry_read(r->options.inventory,row->actor,f->item,&entry,e)||!projected(r,row->actor)) return false;
            entry.count=value; return qa_inventory_configure(r->options.inventory,row->actor,&entry,NULL,NULL,e);
        }
        qa_string_id team=0;
        if(f->kind==COMPONENT_TEAM) {
            bool found=false; for(size_t i=0;i<f->team_count;++i) if(f->teams[i].value==value) { team=f->teams[i].team; found=true; break; }
            if(!found) return q3records_fail(e,QA_ERROR_FORMAT,"Original component team has no shared identity");
        }
        if(!r->options.match_write||!r->options.match_write(r->options.context,row->actor,f->kind==COMPONENT_TEAM,team,value,e)) return false;
        if(!projected(r,row->actor)) return true;
        double reached;
        if(!scalar_read(r,row->actor,f,&reached,e)||!q3records_scalar(reached,f->floating,checked,e)) return false;
        bool previous=r->refreshing; r->refreshing=true;
        bool ok=q3records_raw(r,row->address,(qa_bytes){checked,4},e); r->refreshing=previous; return ok;
    }
    qa_vec3 v={qa_load_f32le(row->bytes),qa_load_f32le(row->bytes+4),qa_load_f32le(row->bytes+8)};
    if(!isfinite(v.x)||!isfinite(v.y)||!isfinite(v.z)) return q3records_fail(e,QA_ERROR_FORMAT,"Component body vector is not finite");
    qa_body_state body;
    if(!qa_world_body_read(r->options.world,row->actor,&body,e)||!projected(r,row->actor)) return false;
    *qa_body_vector(&body,f->body)=v;
    return qa_world_body_write(r->options.world,row->actor,&body,e);
}
bool application_q3_component_records_prepare(void *context,qa_error *e)
{
    application_q3_component_records *r=context;
    if(!r||!r->options.storage_current(r->options.context,e)||!capture(r,e)) return false;
    component_frame *frame=r->frame;
    if(!frame) return true;
    while(frame->cursor<frame->pending_count) {
        component_observation row=frame->pending[frame->cursor++];
        if(!commit(r,&row,e)) return false;
    }
    frame->pending_count=frame->cursor=0; return true;
}
bool application_q3_component_records_enter(void *context,uint32_t entry,const int32_t *words,size_t count,void **out,qa_error *e)
{
    application_q3_component_records *r=context;
    if(!r||!out||*out||count>62||(count&&!words)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component source call requires its actual lowered arguments");
    unsigned depth=0; for(component_frame *f=r->frame;f;f=f->outer) ++depth;
    if(depth>=64) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component source recursion exceeds 64 calls");
    if(r->options.lifecycle_begin&&!r->options.lifecycle_begin(r->options.context,entry,words,count,e)) return false;
    if(!application_q3_component_records_refresh(r,e)) return false;
    qa_unified_frame_lease *storage=application_control_storage_acquire(r->options.application,e);
    if(!storage) return false;
    component_frame *frame=qa_unified_frame_lease_alloc(storage,1,sizeof(*frame),_Alignof(component_frame),e);
    if(!frame) { qa_unified_frame_lease_release(storage); return false; }
    frame->storage=storage;
    frame->owner=r; frame->entry=entry; frame->word_count=count; if(count) memcpy(frame->words,words,count*4);
    if(!observations(r,frame,e)) { qa_unified_frame_lease_release(storage); return false; }
    frame->outer=r->frame; r->frame=frame; *out=frame; return true;
}
bool application_q3_component_records_leave(void *context,void **scope,bool succeeded,int32_t result,qa_error *e)
{
    application_q3_component_records *r=context; component_frame *frame=scope?*scope:NULL;
    if(!r||!frame||frame->owner!=r||r->frame!=frame) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component source cleanup requires the actual innermost frame");
    if(!r->options.storage_current(r->options.context,e)) return false;
    if(succeeded&&!frame->completed) {
        /* Delivery can retain an allocated actor before a later admission
         * fails. Cleanup consumes that reached delivery without replay. */
        frame->completed=true;
        if(!r->options.lifecycle(r->options.context,frame->entry,frame->words,frame->word_count,result,e)) return false;
    }
    if(!application_q3_component_records_prepare(r,e)) return false;
    r->frame=frame->outer; qa_unified_frame_lease_release(frame->storage); *scope=NULL;
    return q3records_finish_retired(r,e);
}
