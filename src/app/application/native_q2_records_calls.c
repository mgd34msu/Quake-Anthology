#include "native_q2_records_private.h"
#include "qa/text.h"

bool nqr_applies(application_native_q2_records *o,const nqr_actor *actor,const nqr_record *record,const nqr_field *field)
{
    (void)o;
    return !actor->retired&&(!record->client||actor->client)&&field->kind<=NQR_BODY&&
        !(actor->client&&field->body_output);
}
bool nqr_call_storage_prepare(application_native_q2_records *o,qa_error *e)
{
    size_t maximum=0,watches=0;
    size_t actors=o->entity_record==SIZE_MAX?0:o->records[o->entity_record].capacity;
    for(size_t i=0;i<o->record_count;++i) {
        const nqr_record *record=o->records+i;
        size_t count=record->client?o->client_maximum:actors;
        if(count>SIZE_MAX-watches) return nqr_fail(e,QA_ERROR_MEMORY,"Native pickup watch capacity overflows");
        watches+=count;
        for(size_t j=0;j<record->field_count;++j) if(record->fields[j].writable) {
            if(count>SIZE_MAX-maximum) return nqr_fail(e,QA_ERROR_MEMORY,"Native observation capacity overflows");
            maximum+=count;
        }
    }
    qa_arena_init(&o->call_storage,0);
    if(!qa_pool_prepare(&o->scopes,&o->call_storage,1,sizeof(application_native_q2_record_scope),
            _Alignof(application_native_q2_record_scope),e)||
        !qa_pool_prepare(&o->snapshots,&o->call_storage,1,sizeof(nqr_snapshot),_Alignof(nqr_snapshot),e)||
        !qa_pool_prepare(&o->observations,&o->call_storage,maximum,sizeof(nqr_observation),_Alignof(nqr_observation),e)||
        !qa_pool_prepare(&o->pickup_scopes,&o->call_storage,1,sizeof(application_native_q2_pickup_scope),
            _Alignof(application_native_q2_pickup_scope),e)||
        !qa_pool_prepare(&o->pickup_watches,&o->call_storage,watches,sizeof(qa_native_write_observer *),
            _Alignof(qa_native_write_observer *),e)) return false;
    return true;
}
static nqr_observation *observation_run(application_native_q2_records *o,size_t count,
    size_t *slot,qa_error *e)
{
    if(!qa_pool_run_available(&o->observations,count)&&
        !qa_pool_extend(&o->observations,&o->call_storage,count,e))return NULL;
    return qa_pool_take_run(&o->observations,count,slot);
}
static void snapshot_release(application_native_q2_records *o,nqr_snapshot *snapshot)
{
    if(!snapshot||--snapshot->references) return;
    if(snapshot->count) qa_pool_release_run(&o->observations,snapshot->rows_slot,snapshot->count);
    qa_pool_release(&o->snapshots,snapshot->storage_slot);
}
static bool observations(application_native_q2_records *o,nqr_snapshot **out,qa_error *e)
{
    size_t count=0;
    for(nqr_actor *actor=o->actors;actor;actor=actor->next) if(!actor->retired)
        for(size_t i=0;i<o->record_count;++i)
            for(size_t j=0;j<o->records[i].field_count;++j)
                count+=o->records[i].fields[j].writable&&nqr_applies(o,actor,o->records+i,o->records[i].fields+j);
    size_t slot;
    if(o->snapshots.head==SIZE_MAX&&!qa_pool_extend(&o->snapshots,&o->call_storage,
        o->snapshots.capacity,e))return false;
    nqr_snapshot *snapshot=qa_pool_take(&o->snapshots,&slot);
    if(!snapshot) return nqr_fail(e,QA_ERROR_MEMORY,"Native snapshot pool exhausted");
    *snapshot=(nqr_snapshot){.storage_slot=slot,.count=count,.references=1};
    if(count) snapshot->rows=observation_run(o,count,&snapshot->rows_slot,e);
    if(count&&!snapshot->rows) { snapshot->count=0;snapshot_release(o,snapshot);return false; }
    size_t used=0;bool ok=true;
    for(nqr_actor *actor=o->actors;ok&&actor;actor=actor->next) {
        if(actor->retired) continue;
        ok=nqr_actor_current(o,actor,e);
        for(size_t i=0;ok&&i<o->record_count;++i) {
            nqr_record *record=o->records+i;
            if(record->client&&!actor->client) continue;
            qa_native_address base;ok=nqr_address(o,actor,record,&base,e);
            for(size_t j=0;ok&&j<record->field_count;++j) {
                nqr_field *field=record->fields+j;
                if(!nqr_applies(o,actor,record,field)||!field->writable) continue;
                nqr_observation *row=snapshot->rows+used++;
                *row=(nqr_observation){.actor=actor->actor,.field=field,.address=base+field->offset};
                ok=qa_native_read(o->options.instance,row->address,row->bytes,field->length,e);
            }
        }
    }
    if(!ok) { snapshot_release(o,snapshot);return false; }
    *out=snapshot;return true;
}
bool nqr_rebase(application_native_q2_records *o,qa_error *e)
{
    if(!o->frame) return true;
    nqr_snapshot *snapshot=NULL;
    if(!observations(o,&snapshot,e)) return false;
    for(application_native_q2_record_scope *frame=o->frame;frame;frame=frame->outer) {
        snapshot_release(o,frame->snapshot);
        frame->snapshot=snapshot;++snapshot->references;
    }
    snapshot_release(o,snapshot);return true;
}
static bool inventory_read(application_native_q2_records *o,qa_actor_id actor,qa_item_id item,double *count,double *capacity,qa_error *e)
{
    qa_inventory_entry entry={0};bool found;
    if(!qa_inventory_entry_find(o->options.inventory,actor,item,&entry,&found,e)) return false;
    *count=found?entry.count:0;*capacity=found?entry.capacity:0;
    return nqr_live(o,actor);
}
static bool scalar_read(application_native_q2_records *o,qa_actor_id actor,const nqr_field *field,double *value,qa_error *e)
{
    if(field->kind==NQR_HEALTH) {
        if(!qa_combat_has(o->options.combat,actor)) { *value=0; return nqr_live(o,actor); }
        qa_combat_state state; if(!qa_combat_read(o->options.combat,actor,&state,e)) return false; *value=state.health; return true;
    }
    if(field->kind==NQR_COUNT||field->kind==NQR_CAPACITY) {
        double count,capacity;
        if(!inventory_read(o,actor,field->item,&count,&capacity,e)) return false;
        *value=field->kind==NQR_COUNT?count:capacity; return true;
    }
    qa_team_ref team={0}; double score=0; bool found=false;
    if(!o->options.match_read||!o->options.match_read(o->options.context,actor,&team,&score,&found,e)) return false;
    if(!found) { team=(qa_team_ref){0}; score=0; }
    if(field->kind==NQR_SCORE) { *value=score; return true; }
    for(size_t i=0;i<field->team_count;++i) if(qa_team_equal(o->options.strings,qa_team_named(field->teams[i].team),team)) { *value=field->teams[i].value; return true; }
    return nqr_fail(e,QA_ERROR_FORMAT,"Canonical team has no declared native source alias");
}
static bool pose(application_native_q2_records *o,nqr_actor *actor,qa_error *e)
{
    if(!o->has_pose||!actor->client||!o->options.client_admitted(o->options.context,actor->actor)) return true;
    double height; bool crouched;
    if(!o->options.pose(o->options.context,actor->actor,&height,&crouched,e)) return false;
    qa_native_address height_base,crouch_base;
    if(!nqr_address(o,actor,o->records+o->view_height.record,&height_base,e)||
        !nqr_address(o,actor,o->records+o->crouch.record,&crouch_base,e)) return false;
    uint8_t raw[8], flag_raw[8];
    size_t flag_bytes=nqr_scalar_size(o->crouch.encoding),height_bytes=nqr_scalar_size(o->view_height.encoding);
    if(!qa_native_read(o->options.instance,crouch_base+o->crouch.offset,flag_raw,flag_bytes,e)) return false;
    if(o->crouch.encoding==QA_NATIVE_F32||o->crouch.encoding==QA_NATIVE_F64) {
        double flags;
        if(!nqr_scalar_decode(flag_raw,o->crouch.encoding,&flags,e)) return false;
        int32_t integer=o->crouch.encoding==QA_NATIVE_F32?qa_source_to_i32((float)flags):
            flags>=-2147483648.0&&flags<2147483648.0?(int32_t)flags:INT32_MIN;
        uint32_t bits=(uint32_t)integer;
        if(crouched) bits|=o->crouch_mask; else bits&=~o->crouch_mask;
        memcpy(&integer,&bits,sizeof(integer));
        if(!nqr_scalar_encode(integer,o->crouch.encoding,flag_raw,e)) return false;
    } else {
        uint64_t bits=flag_bytes==1?flag_raw[0]:flag_bytes==2?qa_load_u16le(flag_raw):
            flag_bytes==4?qa_load_u32le(flag_raw):qa_load_u64le(flag_raw);
        if(crouched) bits|=o->crouch_mask; else bits&=~(uint64_t)o->crouch_mask;
        if(flag_bytes==1) flag_raw[0]=(uint8_t)bits;
        else if(flag_bytes==2) qa_store_u16le(flag_raw,(uint16_t)bits);
        else if(flag_bytes==4) qa_store_u32le(flag_raw,(uint32_t)bits);
        else qa_store_u64le(flag_raw,bits);
    }
    if(o->view_height.encoding!=QA_NATIVE_F32&&o->view_height.encoding!=QA_NATIVE_F64) height=trunc(height);
    if(!nqr_scalar_encode(height,o->view_height.encoding,raw,e)||
        !qa_native_write(o->options.instance,height_base+o->view_height.offset,(qa_bytes){raw,height_bytes},e)||
        !qa_native_write(o->options.instance,crouch_base+o->crouch.offset,(qa_bytes){flag_raw,flag_bytes},e)) return false;
    if(o->options.pose_publish) {
        uint64_t slot=(uint64_t)o->records[o->entity_record].first+actor->index;
        if(slot>UINT32_MAX||!o->options.pose_publish(o->options.context,(uint32_t)slot,height,e)) return false;
    }
    return nqr_current(o,e)&&nqr_live(o,actor->actor)&&!actor->retired;
}
bool application_native_q2_records_refresh(application_native_q2_records *o,qa_error *e)
{
    if(!nqr_current(o,e)) return false;
    ++o->projection_depth; bool ok=true;
    for(nqr_actor *actor=o->actors;ok&&actor;actor=actor->next) {
        if(actor->retired) continue;
        if(!nqr_actor_current(o,actor,e)||!nqr_capacity(o,actor,e)) { ok=false; break; }
        for(size_t i=0;ok&&i<o->record_count;++i) {
            nqr_record *record=o->records+i;
            if(record->client&&!actor->client) continue;
            qa_native_address base; ok=nqr_address(o,actor,record,&base,e);
            for(size_t j=0;ok&&j<record->field_count;++j) {
                nqr_field *field=record->fields+j; if(!nqr_applies(o,actor,record,field)) continue;
                uint8_t raw[12];
                if(field->kind<=NQR_SCORE) { double value; ok=scalar_read(o,actor->actor,field,&value,e)&&nqr_scalar_encode(value,field->encoding,raw,e); }
                else {
                    qa_vec3 vector={0};
                    if(qa_world_body_storage_serial(o->options.world,actor->actor)) {
                        qa_body_state body; ok=qa_world_body_read(o->options.world,actor->actor,&body,e);
                        if(ok) vector=*qa_body_vector(&body,field->body);
                    }
                    if(ok) ok=nqr_scalar_encode(vector.x,QA_NATIVE_F32,raw,e)&&nqr_scalar_encode(vector.y,QA_NATIVE_F32,raw+4,e)&&nqr_scalar_encode(vector.z,QA_NATIVE_F32,raw+8,e);
                }
                if(ok) ok=nqr_live(o,actor->actor)&&!actor->retired&&qa_native_write(o->options.instance,base+field->offset,(qa_bytes){raw,field->length},e)&&nqr_current(o,e);
            }
        }
        if(ok) ok=pose(o,actor,e);
    }
    if(ok) ok=nqr_rebase(o,e);
    --o->projection_depth; return ok;
}
static application_native_q2_pickup_scope *pickup_current(application_native_q2_records *o)
{ return o->pickup&&o->pickup->frame==o->frame?o->pickup:NULL; }
static bool pickup_ready(application_native_q2_pickup_scope *s,qa_error *e)
{
    if(!s||s->closing||s->owner->pickup!=s||s->owner->frame!=s->frame||
        !qa_pickup_recipient_is(s->execution,s->actor)||!qa_pickup_current(s->execution)||
        !nqr_live(s->owner,s->actor))
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native pickup lost its actual admitted resource execution");
    return nqr_current(s->owner,e);
}
static bool pickup_writes(application_native_q2_records *o,const nqr_observation *rows,size_t count,qa_error *e)
{
    application_native_q2_pickup_scope *s=pickup_current(o);
    if(!s||!count||o->projection_depth) return true;
    if(!pickup_ready(s,e)) return false;
    size_t write_count=0; const qa_pickup_write *writes=qa_pickup_writes(s->execution,&write_count);
    for(size_t i=0;i<count;++i) {
        const nqr_field *f=rows[i].field;
        if(!qa_actor_id_equal(rows[i].actor,s->actor)||(f->kind!=NQR_COUNT&&f->kind!=NQR_CAPACITY))
            return nqr_fail(e,QA_ERROR_ARGUMENT,"Native pickup changed an undeclared recipient resource");
        bool allowed=false;
        for(size_t j=0;!allowed&&j<write_count;++j) {
            const qa_pickup_write *w=writes+j;
            if(w->resource.kind==QA_PICKUP_INVENTORY)
                allowed=w->resource.item==f->item&&(w->fields==QA_PICKUP_COUNT_CAPACITY||
                    (f->kind==NQR_COUNT?w->fields==QA_PICKUP_COUNT:w->fields==QA_PICKUP_CAPACITY));
            else if(w->resource.kind==QA_PICKUP_PROTECTION&&f->kind==NQR_COUNT) {
                if(!s->protection_item||!s->protection_item(s->context,s->actor,w->resource.channel,f->item,&allowed,e))
                    return false;
                if(!pickup_ready(s,e)) return false;
            }
        }
        if(!allowed) return nqr_fail(e,QA_ERROR_ARGUMENT,"Native pickup wrote outside its current resource authority");
    }
    return pickup_ready(s,e);
}
static bool capture(application_native_q2_records *o,qa_error *e)
{
    application_native_q2_record_scope *frame=o->frame;
    if(!frame||o->projection_depth) return true;
    size_t slot=0,count=0,capacity=frame->snapshot->count;
    nqr_observation *changed=capacity?observation_run(o,capacity,&slot,e):NULL;
    if(capacity&&!changed) return false;
    bool ok=true;
    for(size_t i=0;ok&&i<capacity;++i) {
        nqr_observation row=frame->snapshot->rows[i];uint8_t raw[12];
        ok=qa_native_read(o->options.instance,row.address,raw,row.field->length,e);
        if(!ok||!memcmp(raw,row.bytes,row.field->length)) continue;
        memcpy(row.bytes,raw,row.field->length);changed[count++]=row;
    }
    if(ok) ok=pickup_writes(o,changed,count,e);
    size_t used=0;
    for(size_t i=0;ok&&i<count;++i) {
        nqr_observation row=changed[i];
        if(row.field->kind==NQR_COUNT||row.field->kind==NQR_CAPACITY) {
            size_t at=0;for(;at<used;++at) if(changed[at].field->item==row.field->item&&
                (changed[at].field->kind==NQR_COUNT||changed[at].field->kind==NQR_CAPACITY)&&qa_actor_id_equal(changed[at].actor,row.actor)) break;
            double value;ok=nqr_scalar_decode(row.bytes,row.field->encoding,&value,e);
            if(!ok) break;
            if(at==used) changed[used++]=row;
            if(row.field->kind==NQR_COUNT) {changed[at].count_present=true;changed[at].count=value;}
            else {changed[at].capacity_present=true;changed[at].capacity=value;}
        } else changed[used++]=row;
    }
    if(ok&&used>SIZE_MAX-frame->pending_count) ok=nqr_fail(e,QA_ERROR_MEMORY,"Native pending write queue overflows");
    size_t needed=ok?frame->pending_count+used:0;
    if(ok&&needed>frame->pending_capacity) {
        size_t next_slot=frame->pending_slot;
        nqr_observation *next=frame->pending?qa_pool_grow_run(&o->observations,&next_slot,frame->pending_capacity,needed):NULL;
        if(next) memmove(next,frame->pending,frame->pending_count*sizeof(*next));
        else {
            next=observation_run(o,needed,&next_slot,e);
            if(next&&frame->pending_count) memcpy(next,frame->pending,frame->pending_count*sizeof(*next));
            if(next&&frame->pending_capacity) qa_pool_release_run(&o->observations,frame->pending_slot,frame->pending_capacity);
        }
        if(!next) ok=false;
        else {frame->pending=next;frame->pending_slot=next_slot;frame->pending_capacity=needed;}
    }
    if(ok&&used) memcpy(frame->pending+frame->pending_count,changed,used*sizeof(*changed));
    if(capacity) qa_pool_release_run(&o->observations,slot,capacity);
    if(!ok||!nqr_rebase(o,e)) return false;
    frame->pending_count=needed;return true;
}
typedef struct inventory_commit_context {
    application_native_q2_records *owner;
    application_native_q2_pickup_scope *pickup;
    const application_native_q2_inventory_commit *protection;
} inventory_commit_context;
static bool inventory_committed(void *opaque,const qa_inventory_change *change,qa_error *e)
{
    inventory_commit_context *c=opaque; application_native_q2_records *o=c->owner;
    if(!nqr_current(o,e)||(c->pickup&&!pickup_ready(c->pickup,e))) return false;
    if(c->protection) {
        const application_native_q2_inventory_commit *p=c->protection;
        if(!p->current(p->context,e)) return false;
        if(qa_actor_id_equal(p->actor,change->actor))
            for(size_t i=0;i<p->count;++i) if(p->items[i]==change->after.item) {
                if(!p->committed(p->context,change,e)) return false;
                break;
            }
        if(!p->current(p->context,e)||!nqr_current(o,e)) return false;
    }
    nqr_snapshot *snapshot=NULL;
    if(!observations(o,&snapshot,e)) return false;
    nqr_observation *rows=snapshot->rows;size_t count=snapshot->count;
    ++o->projection_depth; bool ok=true;
    for(size_t i=0;ok&&i<count;++i) {
        nqr_observation *r=rows+i; const nqr_field *f=r->field;
        if(!qa_actor_id_equal(r->actor,change->actor)||f->item!=change->after.item||
            (f->kind!=NQR_COUNT&&f->kind!=NQR_CAPACITY)) continue;
        uint8_t raw[8]; double value=f->kind==NQR_COUNT?change->after.count:change->after.capacity;
        ok=nqr_scalar_encode(value,f->encoding,raw,e)&&qa_native_write(o->options.instance,r->address,(qa_bytes){raw,f->length},e)&&nqr_current(o,e);
    }
    --o->projection_depth; snapshot_release(o,snapshot);
    return ok&&nqr_rebase(o,e)&&application_native_q2_records_commit_inventory(o,c->protection,e);
}
static bool commit(application_native_q2_records *o,const nqr_observation *row,
    const application_native_q2_inventory_commit *protection,qa_error *e)
{
    if(o->options.client_rejected&&o->options.client_rejected(o->options.context,row->actor)) return true;
    nqr_actor *actor=nqr_find(o,row->actor);
    if(!actor||!nqr_actor_current(o,actor,e)) return nqr_fail(e,QA_ERROR_NOT_FOUND,"Native source wrote a retired canonical actor");
    const nqr_field *f=row->field;
    if(f->kind==NQR_COUNT||f->kind==NQR_CAPACITY) {
        qa_inventory_entry entry;
        if(!qa_inventory_entry_read(o->options.inventory,row->actor,f->item,&entry,e)) return false;
        if(row->capacity_present&&!qa_inventory_mutable_capacity(o->options.inventory,row->actor,f->item)) return nqr_fail(e,QA_ERROR_ARGUMENT,"Native source wrote immutable inventory capacity");
        if(row->count_present) entry.count=row->count;
        if(row->capacity_present) entry.capacity=row->capacity;
        application_native_q2_pickup_scope *pickup=pickup_current(o);
        if(pickup&&!pickup_ready(pickup,e)) return false;
        inventory_commit_context context={o,pickup,protection};
        if(!qa_inventory_configure(o->options.inventory,row->actor,&entry,
            pickup||protection?inventory_committed:NULL,&context,e)) return false;
        return !pickup||(pickup_ready(pickup,e)&&application_native_q2_records_refresh(o,e));
    }
    if(f->kind<=NQR_SCORE) {
        double value; if(!nqr_scalar_decode(row->bytes,f->encoding,&value,e)) return false;
        if(f->kind==NQR_HEALTH) return qa_combat_set_health(o->options.combat,row->actor,value,e);
        qa_string_id team=0;
        if(f->kind==NQR_TEAM) {
            bool found=false;
            for(size_t i=0;i<f->team_count;++i) if(f->teams[i].value==value) { team=f->teams[i].team; found=true; break; }
            if(!found) return nqr_fail(e,QA_ERROR_FORMAT,"Native source team has no declared canonical identity");
        }
        if(!o->options.match_write||!o->options.match_write(o->options.context,row->actor,f->kind==NQR_TEAM,team,value,e)) return false;
        if(!nqr_live(o,row->actor)||actor->retired) return true;
        uint8_t raw[8]; double reached;
        if(!scalar_read(o,row->actor,f,&reached,e)||!nqr_scalar_encode(reached,f->encoding,raw,e)) return false;
        ++o->projection_depth;
        bool ok=qa_native_write(o->options.instance,row->address,(qa_bytes){raw,f->length},e);
        --o->projection_depth; return ok;
    }
    qa_vec3 vector={qa_load_f32le(row->bytes),qa_load_f32le(row->bytes+4),qa_load_f32le(row->bytes+8)};
    if(!qa_vec_finite(vector)) return nqr_fail(e,QA_ERROR_FORMAT,"Native source wrote a nonfinite body vector");
    qa_body_state body;
    if(!qa_world_body_read(o->options.world,row->actor,&body,e)||!nqr_live(o,row->actor)||actor->retired) return false;
    *qa_body_vector(&body,f->body)=vector;
    return qa_world_body_write(o->options.world,row->actor,&body,e);
}
bool application_native_q2_records_commit(application_native_q2_records *o,qa_error *e)
{ return application_native_q2_records_commit_inventory(o,NULL,e); }
bool application_native_q2_records_commit_inventory(application_native_q2_records *o,
    const application_native_q2_inventory_commit *protection,qa_error *e)
{
    if(protection&&(!protection->actor.registry||!protection->current||!protection->committed||
        (protection->count&&!protection->items)))
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native protection commit requires its actual current reservoir owner");
    if(protection&&!protection->current(protection->context,e)) return false;
    if(!nqr_current(o,e)||!capture(o,e)) return false;
    application_native_q2_record_scope *frame=o->frame; if(!frame) return true;
    ++frame->committing; bool ok=true;
    while(frame->cursor<frame->pending_count) {
        nqr_observation row=frame->pending[frame->cursor++];
        if(!commit(o,&row,protection,e)) { ok=false; break; }
    }
    --frame->committing;
    if(ok&&!frame->committing) {
        if(frame->pending_capacity) qa_pool_release_run(&o->observations,frame->pending_slot,frame->pending_capacity);
        frame->pending=NULL; frame->pending_capacity=frame->pending_count=frame->cursor=0;
    }
    return ok;
}
bool application_native_q2_records_begin(application_native_q2_records *o,application_native_q2_record_scope **out,qa_error *e)
{
    if(!out||*out||!nqr_current(o,e)||o->closing||o->restoring||o->lifecycle_depth)
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native record transfer requires its actual executable source owner");
    if(!application_native_q2_records_commit(o,e)||!nqr_releases(o,e)||!application_native_q2_records_refresh(o,e)) return false;
    if(o->scopes.head==SIZE_MAX&&!qa_pool_extend(&o->scopes,&o->call_storage,
        o->scopes.capacity,e))return false;
    size_t slot;
    application_native_q2_record_scope *frame=qa_pool_take(&o->scopes,&slot);
    if(!frame) return nqr_fail(e,QA_ERROR_MEMORY,"Retaining entered native projection scope");
    *frame=(application_native_q2_record_scope){.storage_slot=slot,.owner=o,.outer=o->frame};
    if(!observations(o,&frame->snapshot,e)) { qa_pool_release(&o->scopes,slot); return false; }
    o->frame=frame; *out=frame; return true;
}
bool application_native_q2_records_end(application_native_q2_records *o,application_native_q2_record_scope **scope,bool succeeded,qa_error *e)
{
    application_native_q2_record_scope *frame=scope?*scope:NULL;
    if(!o||!frame||frame->owner!=o||o->frame!=frame) return nqr_fail(e,QA_ERROR_ARGUMENT,"Native cleanup requires its actual innermost record scope");
    if(frame->committing||(o->pickup&&o->pickup->frame==frame))
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native record cleanup retains its actual commit or pickup observations");
    if(succeeded&&!application_native_q2_records_commit(o,e)) return false;
    o->frame=frame->outer; snapshot_release(o,frame->snapshot);
    if(frame->pending_capacity) qa_pool_release_run(&o->observations,frame->pending_slot,frame->pending_capacity);
    qa_pool_release(&o->scopes,frame->storage_slot); *scope=NULL;
    return nqr_releases(o,e);
}
static bool pickup_stored(void *context,qa_native_instance *instance,
    const qa_native_write_event *event,qa_error *e)
{
    application_native_q2_pickup_scope *s=context;
    (void)event;
    if(!s||instance!=s->owner->options.instance)
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native pickup observer changed its actual module owner");
    application_native_q2_records *o=s->owner;
    if(s->closing||o->pickup!=s||o->frame!=s->frame||o->projection_depth) return true;
    return pickup_ready(s,e)&&application_native_q2_records_commit(o,e);
}
bool application_native_q2_records_pickup_end(application_native_q2_records *o,
    application_native_q2_pickup_scope **scope,qa_error *e)
{
    if(!scope||!*scope) return true;
    application_native_q2_pickup_scope *s=*scope;
    if(!o||s->owner!=o)
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native pickup cleanup requires its returned innermost source scope");
    s->closing=true;
    if(o->pickup!=s||s->frame!=o->frame||s->frame->committing)
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native pickup cleanup retains its actual nested source frame or commit");
    while(s->count) {
        if(!qa_native_unobserve_writes(s->watches[s->count-1],e)) return false;
        s->watches[--s->count]=NULL;
    }
    o->pickup=s->outer;
    if(s->capacity) qa_pool_release_run(&o->pickup_watches,s->watches_slot,s->capacity);
    qa_pool_release(&o->pickup_scopes,s->storage_slot);*scope=NULL;return true;
}
bool application_native_q2_records_pickup_begin(application_native_q2_records *o,qa_actor_id actor,
    qa_pickup_execution *execution,application_native_q2_protection_item_fn protection_item,void *context,
    application_native_q2_pickup_scope **out,qa_error *e)
{
    if(!o||!out||*out||!o->frame||o->closing||o->restoring||o->lifecycle_depth||
        !qa_pickup_recipient_is(execution,actor)||!qa_pickup_current(execution)||
        !nqr_live(o,actor)||!nqr_current(o,e))
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native pickup observation requires its actual admitted source transfer");
    size_t write_count=0; const qa_pickup_write *writes=qa_pickup_writes(execution,&write_count);
    if(!write_count||!writes) return nqr_fail(e,QA_ERROR_ARGUMENT,"Native pickup has no actual admitted resource writes");
    for(size_t i=0;i<write_count;++i)
        if(writes[i].resource.kind==QA_PICKUP_PROTECTION&&!protection_item)
            return nqr_fail(e,QA_ERROR_ARGUMENT,"Native protection pickup needs its actual bound reservoir membership reader");
    size_t capacity=0;
    for(nqr_actor *row=o->actors;row;row=row->next) {
        if(row->retired) continue;
        for(size_t i=0;i<o->record_count;++i) if(!o->records[i].client||row->client) {
            if(capacity==SIZE_MAX/sizeof(qa_native_write_observer *))
                return nqr_fail(e,QA_ERROR_MEMORY,"Native pickup watch roster overflows");
            ++capacity;
        }
    }
    if(o->pickup_scopes.head==SIZE_MAX&&!qa_pool_extend(&o->pickup_scopes,&o->call_storage,
        o->pickup_scopes.capacity,e))return false;
    if(capacity&&!qa_pool_run_available(&o->pickup_watches,capacity)&&
        !qa_pool_extend(&o->pickup_watches,&o->call_storage,capacity,e))return false;
    size_t storage_slot;
    application_native_q2_pickup_scope *s=qa_pool_take(&o->pickup_scopes,&storage_slot);
    if(!s) return nqr_fail(e,QA_ERROR_MEMORY,"Retaining native pickup resource observation scope");
    *s=(application_native_q2_pickup_scope){.storage_slot=storage_slot};
    s->watches=capacity?qa_pool_take_run(&o->pickup_watches,capacity,&s->watches_slot):NULL;
    if(capacity&&!s->watches) { qa_pool_release(&o->pickup_scopes,storage_slot);return nqr_fail(e,QA_ERROR_MEMORY,"Owning actual native pickup write subscriptions"); }
    s->owner=o; s->outer=o->pickup; s->frame=o->frame; s->actor=actor;
    s->execution=execution; s->protection_item=protection_item; s->context=context; s->capacity=capacity;
    o->pickup=s; *out=s; bool ok=true;
    for(nqr_actor *row=o->actors;ok&&row;row=row->next) {
        if(row->retired) continue;
        ok=nqr_actor_current(o,row,e);
        for(size_t i=0;ok&&i<o->record_count;++i) {
            nqr_record *record=o->records+i;
            if(record->client&&!row->client) continue;
            qa_native_address base;
            ok=nqr_address(o,row,record,&base,e)&&qa_native_observe_writes(o->options.instance,
                &(qa_native_write_watch){.address = base, .bytes = record->stride},
                pickup_stored,s,s->watches+s->count,e);
            if(ok) ++s->count;
        }
    }
    if(ok) ok=pickup_ready(s,e);
    if(!ok) {
        qa_error first=e?*e:(qa_error){0},cleanup={0};
        (void)application_native_q2_records_pickup_end(o,out,&cleanup);
        if(e) *e=first;
    }
    return ok;
}
