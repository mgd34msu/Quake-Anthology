#include "guest_q3_component_private.h"
#include "qa/q3_abi.h"

static int32_t signed_difference(int32_t next,int32_t previous)
{ uint32_t bits=(uint32_t)next-(uint32_t)previous; int32_t result; memcpy(&result,&bits,4); return result; }
static bool touched(const qa_qvm_committed_write *event,uint32_t at)
{
    for(size_t i=0;i<event->count;++i) if(event->ranges[i].offset<(uint64_t)at+4&&at<(uint64_t)event->ranges[i].offset+event->ranges[i].after.size) return true;
    return false;
}
static bool ordinal(application_q3_component *c,uint64_t *out,qa_error *e)
{
    if(c->player_order==UINT64_MAX) return q3records_fail(e,QA_ERROR_ARGUMENT,"Original player event source order exhausted");
    *out=++c->player_order; return true;
}
static bool read_state(application_q3_component *c,component_player_cursor *cursor,qa_q3_player *out,qa_error *e)
{ return qa_qvm_read_player(c->vm,(int32_t)cursor->address,true,out,e); }
static bool observe(void *context,qa_qvm *vm,const qa_qvm_committed_write *event,qa_error *e)
{
    application_q3_component *c=context; (void)vm;
    if(c->restoring||c->closing) return true;
    for(size_t i=0;i<c->player_cursor_count;++i) {
        component_player_cursor *cursor=c->player_cursors+i; qa_q3_player state;
        bool predictable=touched(event,cursor->address+108),external=touched(event,cursor->address+128);
        if(!predictable&&!external) continue;
        if(!read_state(c,cursor,&state,e)) return false;
        if(predictable) {
            int32_t delta=signed_difference(state.eventSequence,cursor->observed);
            if(delta<0) { cursor->sequence=state.eventSequence; cursor->predictable_order[0]=cursor->predictable_order[1]=0; }
            else if(delta>0) for(int32_t step=delta<2?delta:2;step>0;--step) {
                uint32_t bits=(uint32_t)state.eventSequence-(uint32_t)step; int32_t sequence; memcpy(&sequence,&bits,4);
                uint32_t slot=bits&1; cursor->predictable_sequence[slot]=sequence;
                if(!ordinal(c,cursor->predictable_order+slot,e)) return false;
            }
            cursor->observed=state.eventSequence;
        }
        if(external&&!ordinal(c,&cursor->external_order,e)) return false;
    }
    return true;
}
bool q3component_player_events_create(application_q3_component *c,qa_error *e)
{
    if(c->scene||c->player_record==SIZE_MAX||!c->maximum) return true;
    component_record *record=c->records->records+c->player_record;
    qa_qvm_write_range ranges[64];
    for(uint32_t i=0;i<c->maximum;++i) ranges[i]=(qa_qvm_write_range){record->address+i*record->stride+108,32};
    return qa_qvm_observe_writes(c->vm,ranges,c->maximum,observe,NULL,c,&c->player_watch,e);
}
bool q3component_player_events_track(application_q3_component *c,qa_actor_id actor,qa_error *e)
{
    if(!c->player_watch) return true;
    for(size_t i=0;i<c->player_cursor_count;++i) if(qa_actor_id_equal(c->player_cursors[i].actor,actor)) return true;
    uint32_t address;
    if(!application_q3_component_records_pointer(c->records,actor,c->records->records[c->player_record].id,&address,e)) return false;
    component_player_cursor cursor={.actor=actor,.address=address}; qa_q3_player state;
    if(!read_state(c,&cursor,&state,e)) return false;
    cursor.external=state.externalEvent; cursor.external_time=state.externalEventTime;
    cursor.sequence=cursor.observed=state.eventSequence;
    component_player_cursor *rows=realloc(c->player_cursors,(c->player_cursor_count+1)*sizeof(*rows));
    if(!rows) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining admitted original player event cursor");
    c->player_cursors=rows; rows[c->player_cursor_count++]=cursor; return true;
}
void q3component_player_events_release(application_q3_component *c,qa_actor_id actor)
{
    for(size_t i=0;i<c->player_cursor_count;++i) if(qa_actor_id_equal(c->player_cursors[i].actor,actor)) {
        memmove(c->player_cursors+i,c->player_cursors+i+1,(c->player_cursor_count-i-1)*sizeof(*c->player_cursors)); --c->player_cursor_count; return;
    }
}
typedef struct pending_event { uint64_t order; application_q3_scene_player_event event; } pending_event;
bool q3component_player_events_publish(application_q3_component *c,bool succeeded,qa_error *e)
{
    if(!c->player_watch||c->restoring||c->closing) return true;
    pending_event pending[192]; size_t count=0;
    for(size_t i=0;i<c->player_cursor_count;++i) {
        component_player_cursor *cursor=c->player_cursors+i; qa_q3_player state;
        if(!application_q3_component_records_live_client(c->records,cursor->actor)) continue;
        if(!read_state(c,cursor,&state,e)) return false;
        if(succeeded) {
            qa_body_state body;
            bool external=state.externalEvent&&(state.externalEvent!=cursor->external||state.externalEventTime!=cursor->external_time);
            int32_t delta=signed_difference(state.eventSequence,cursor->sequence),number=delta>0?(delta<2?delta:2):0;
            if((external||number)&&!qa_world_body_read(c->options.host.world,cursor->actor,&body,e)) return false;
            application_q3_scene_player_event base={.actor=cursor->actor,.player=state,.time_ms=c->milliseconds};
            if(external||number) base.origin=body.origin;
            if(external) {
                pending_event *row=pending+count++; row->event=base; row->event.external=true;
                row->event.event=state.externalEvent; row->event.parameter=state.externalEventParm; row->event.source_sequence=state.externalEventTime;
                row->order=cursor->external_order; if(!row->order&&!ordinal(c,&row->order,e)) return false;
            }
            for(int32_t step=number;step>0;--step) {
                uint32_t bits=(uint32_t)state.eventSequence-(uint32_t)step,slot=bits&1; int32_t sequence; memcpy(&sequence,&bits,4);
                if(!state.events[slot]) continue;
                pending_event *row=pending+count++; row->event=base;
                row->event.event=state.events[slot]; row->event.parameter=state.eventParms[slot]; row->event.source_sequence=sequence;
                row->order=cursor->predictable_sequence[slot]==sequence?cursor->predictable_order[slot]:0;
                if(!row->order&&!ordinal(c,&row->order,e)) return false;
            }
        }
        cursor->external=state.externalEvent; cursor->external_time=state.externalEventTime;
        cursor->sequence=cursor->observed=state.eventSequence; cursor->external_order=0; cursor->predictable_order[0]=cursor->predictable_order[1]=0;
    }
    for(size_t i=1;i<count;++i) { pending_event row=pending[i]; size_t j=i; while(j&&pending[j-1].order>row.order) { pending[j]=pending[j-1]; --j; } pending[j]=row; }
    for(size_t i=0;i<count;++i) if(application_q3_component_records_live_client(c->records,pending[i].event.actor)) {
        if(c->player_sequence==UINT64_MAX) return q3records_fail(e,QA_ERROR_ARGUMENT,"Original player event delivery sequence exhausted");
        uint64_t sequence=++c->player_sequence;
        if(c->options.source_player_event&&!c->options.source_player_event(c->options.context,&pending[i].event,sequence,e)) return false;
    }
    return true;
}
bool q3component_player_events_destroy(application_q3_component *c,qa_error *e)
{
    if(c->player_watch&&!qa_qvm_unobserve_writes(c->vm,c->player_watch,e)) return false;
    c->player_watch=0; free(c->player_cursors); c->player_cursors=NULL; c->player_cursor_count=0; return true;
}
bool q3component_player_events_fields(application_q3_component *c,qa_source_save_io *io)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; qa_qvm_binding watch=reading?0:c->player_watch;
    if(!qa_source_save_u64(io,&watch)||((watch!=0)!=(c->player_watch!=0))||
        !qa_source_save_u64(io,&c->player_order)||!qa_source_save_u64(io,&c->player_sequence)||
        !qa_source_save_count(io,&c->player_cursor_count,c->maximum)) return false;
    if(reading) {
        c->restored_player_watch=watch;
        c->player_cursors=c->player_cursor_count?calloc(c->player_cursor_count,sizeof(*c->player_cursors)):NULL;
        if(c->player_cursor_count&&!c->player_cursors) return q3records_fail(io->error,QA_ERROR_MEMORY,"Restoring original player event cursors");
    }
    for(size_t i=0;i<c->player_cursor_count;++i) {
        component_player_cursor *cursor=c->player_cursors+i;
        if(!qa_source_save_actor(io,&cursor->actor)||!cursor->actor.registry||!qa_source_save_u32(io,&cursor->address)||
            !qa_source_save_i32(io,&cursor->external)||!qa_source_save_i32(io,&cursor->external_time)||
            !qa_source_save_i32(io,&cursor->sequence)||!qa_source_save_i32(io,&cursor->observed)||
            !qa_source_save_u64(io,&cursor->external_order)||cursor->external_order>c->player_order) return false;
        for(size_t j=0;j<2;++j) if(!qa_source_save_u64(io,cursor->predictable_order+j)||cursor->predictable_order[j]>c->player_order||
            !qa_source_save_i32(io,cursor->predictable_sequence+j)) return false;
        component_record *record=c->records->records+c->player_record;
        if(cursor->address<record->address||(cursor->address-record->address)%record->stride||
            (cursor->address-record->address)/record->stride>=c->maximum) return false;
        if(!reading) { uint32_t actual; if(!application_q3_component_records_pointer(c->records,cursor->actor,record->id,&actual,io->error)||actual!=cursor->address) return false; }
        for(size_t j=0;j<i;++j) if(c->player_cursors[j].address==cursor->address||qa_actor_id_equal(c->player_cursors[j].actor,cursor->actor)) return false;
    }
    return true;
}
static bool record_write(void *context,size_t offset,qa_bytes bytes,qa_error *e)
{ (void)e; if(bytes.size) memcpy((uint8_t *)context+offset,bytes.data,bytes.size); return true; }
bool q3component_player_event_fields(qa_qvm_abi abi,qa_source_save_io *io,application_q3_scene_player_event *event)
{
    uint8_t bytes[468]={0}; size_t size=qa_qvm_player_bytes(abi);
    qa_q3_abi_record record={.abi=abi,.bytes={bytes,size},.context=bytes,.write=record_write};
    bool ok=io->direction==QA_SOURCE_SAVE_READ||qa_q3_abi_write_player(&record,0,true,false,&event->player,io->error);
    if(ok) ok=qa_source_save_bytes(io,bytes,size);
    if(ok&&io->direction==QA_SOURCE_SAVE_READ) ok=qa_q3_abi_read_player(&record,0,true,&event->player,io->error);
    return ok&&qa_source_save_actor(io,&event->actor)&&event->actor.registry&&qa_source_save_vec3(io,&event->origin)&&qa_vec_finite(event->origin)&&
        qa_source_save_i32(io,&event->event)&&qa_source_save_i32(io,&event->parameter)&&qa_source_save_i32(io,&event->time_ms)&&event->time_ms>=0&&
        qa_source_save_i32(io,&event->source_sequence)&&qa_source_save_bool(io,&event->external);
}
