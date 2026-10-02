#include "native_q2_records_private.h"

bool nqr_applies(application_native_q2_records *o,const nqr_actor *actor,const nqr_record *record,const nqr_field *field)
{
    return !actor->retired&&(!record->client||actor->client)&&field->kind<=NQR_MAX&&
        !(actor->client&&field->body_output&&o->options.client_admitted(o->options.context,actor->actor));
}
static bool observations(application_native_q2_records *o,nqr_observation **out,size_t *count,qa_error *e)
{
    nqr_observation *rows=NULL; size_t used=0;
    for(nqr_actor *actor=o->actors;actor;actor=actor->next) {
        if(actor->retired) continue;
        if(!nqr_actor_current(o,actor,e)) { free(rows); return false; }
        for(size_t i=0;i<o->record_count;++i) {
            nqr_record *record=o->records+i;
            if(record->client&&!actor->client) continue;
            qa_native_address base;
            if(!nqr_address(o,actor,record,&base,e)) { free(rows); return false; }
            for(size_t j=0;j<record->field_count;++j) {
                nqr_field *field=record->fields+j;
                if(!nqr_applies(o,actor,record,field)||!field->writable) continue;
                if(used==SIZE_MAX/sizeof(*rows)) { free(rows); return nqr_fail(e,QA_ERROR_MEMORY,"Native observation extent overflows"); }
                nqr_observation *next=realloc(rows,(used+1)*sizeof(*rows));
                if(!next) { free(rows); return nqr_fail(e,QA_ERROR_MEMORY,"Retaining native source write observations"); }
                rows=next; nqr_observation *row=rows+used++;
                *row=(nqr_observation){.actor=actor->actor,.field=field,.address=base+field->offset};
                if(!qa_native_read(o->options.instance,row->address,row->bytes,field->length,e)) { free(rows); return false; }
            }
        }
    }
    *out=rows; *count=used; return true;
}
bool nqr_rebase(application_native_q2_records *o,qa_error *e)
{
    typedef struct baseline { struct baseline *next; application_native_q2_record_scope *frame; nqr_observation *rows; size_t count; } baseline;
    baseline *pending=NULL; bool ok=true;
    for(application_native_q2_record_scope *frame=o->frame;ok&&frame;frame=frame->outer) {
        baseline *row=calloc(1,sizeof(*row)); if(!row) { ok=nqr_fail(e,QA_ERROR_MEMORY,"Retaining nested native observation baselines"); break; }
        row->next=pending; pending=row; row->frame=frame;
        ok=observations(o,&row->rows,&row->count,e);
    }
    while(pending) {
        baseline *row=pending; pending=row->next;
        if(ok) { free(row->frame->observations); row->frame->observations=row->rows; row->frame->observation_count=row->count; }
        else free(row->rows);
        free(row);
    }
    return ok;
}
static bool inventory_read(application_native_q2_records *o,qa_actor_id actor,qa_item_id item,double *count,double *capacity,qa_error *e)
{
    size_t total=0;
    if(!qa_inventory_entries(o->options.inventory,actor,NULL,0,&total,e)) return false;
    if(total&&SIZE_MAX/total<sizeof(qa_inventory_entry)) return nqr_fail(e,QA_ERROR_MEMORY,"Native inventory read extent overflows");
    qa_inventory_entry *entries=total?malloc(total*sizeof(*entries)):NULL;
    if(total&&!entries) return nqr_fail(e,QA_ERROR_MEMORY,"Reading actual canonical inventory entries");
    size_t reached=0; bool ok=qa_inventory_entries(o->options.inventory,actor,entries,total,&reached,e);
    *count=*capacity=0;
    for(size_t i=0;ok&&i<reached;++i) if(entries[i].item==item) { *count=entries[i].count; *capacity=entries[i].capacity; break; }
    free(entries); return ok&&nqr_live(o,actor);
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
    qa_string_id team=0; double score=0; bool found=false;
    if(!o->options.match_read||!o->options.match_read(o->options.context,actor,&team,&score,&found,e)) return false;
    if(!found) { team=0; score=0; }
    if(field->kind==NQR_SCORE) { *value=score; return true; }
    for(size_t i=0;i<field->team_count;++i) if(field->teams[i].team==team) { *value=field->teams[i].value; return true; }
    return nqr_fail(e,QA_ERROR_FORMAT,"Canonical team has no declared native source alias");
}
static qa_vec3 body_vector(const qa_body_state *body,nqr_kind kind)
{ return kind==NQR_ORIGIN?body->origin:kind==NQR_VELOCITY?body->velocity:kind==NQR_ANGLES?body->angles:kind==NQR_MIN?body->bounds.mins:body->bounds.maxs; }
static bool pose(application_native_q2_records *o,nqr_actor *actor,qa_error *e)
{
    if(!o->has_pose||!actor->client||!o->options.client_admitted(o->options.context,actor->actor)) return true;
    double height; bool crouched;
    if(!o->options.pose(o->options.context,actor->actor,&height,&crouched,e)) return false;
    qa_native_address height_base,crouch_base;
    if(!nqr_address(o,actor,o->records+o->view_height.record,&height_base,e)||
        !nqr_address(o,actor,o->records+o->crouch.record,&crouch_base,e)) return false;
    uint8_t raw[8]; double flags;
    size_t flag_bytes=nqr_scalar_size(o->crouch.encoding),height_bytes=nqr_scalar_size(o->view_height.encoding);
    if(!qa_native_read(o->options.instance,crouch_base+o->crouch.offset,raw,flag_bytes,e)||
        !nqr_scalar_decode(raw,o->crouch.encoding,&flags,e)) return false;
    double wrapped=fmod(trunc(flags),4294967296.0); if(wrapped<0) wrapped+=4294967296.0;
    uint32_t bits=(uint32_t)wrapped;
    if(crouched) bits|=o->crouch_mask; else bits&=~o->crouch_mask;
    int32_t signed_bits; memcpy(&signed_bits,&bits,4);
    if(o->view_height.encoding!=QA_NATIVE_F32&&o->view_height.encoding!=QA_NATIVE_F64) height=trunc(height);
    if(!nqr_scalar_encode(height,o->view_height.encoding,raw,e)||
        !qa_native_write(o->options.instance,height_base+o->view_height.offset,(qa_bytes){raw,height_bytes},e)||
        !nqr_scalar_encode(signed_bits,o->crouch.encoding,raw,e)||
        !qa_native_write(o->options.instance,crouch_base+o->crouch.offset,(qa_bytes){raw,flag_bytes},e)) return false;
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
                        if(ok) vector=body_vector(&body,field->kind);
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
static bool capture(application_native_q2_records *o,qa_error *e)
{
    application_native_q2_record_scope *frame=o->frame;
    if(!frame||o->projection_depth) return true;
    nqr_observation *changed=NULL; size_t count=0;
    for(size_t i=0;i<frame->observation_count;++i) {
        nqr_observation row=frame->observations[i]; uint8_t raw[12];
        if(!qa_native_read(o->options.instance,row.address,raw,row.field->length,e)) { free(changed); return false; }
        if(!memcmp(raw,row.bytes,row.field->length)) continue;
        if(count==SIZE_MAX/sizeof(*changed)) { free(changed); return nqr_fail(e,QA_ERROR_MEMORY,"Native changed field extent overflows"); }
        nqr_observation *next=realloc(changed,(count+1)*sizeof(*next));
        if(!next) { free(changed); return nqr_fail(e,QA_ERROR_MEMORY,"Retaining reached native canonical writes"); }
        changed=next; memcpy(row.bytes,raw,row.field->length); changed[count++]=row;
    }
    size_t used=0;
    for(size_t i=0;i<count;++i) {
        nqr_observation row=changed[i];
        if(row.field->kind==NQR_COUNT||row.field->kind==NQR_CAPACITY) {
            size_t at=0; for(;at<used;++at) if(changed[at].field->item==row.field->item&&
                (changed[at].field->kind==NQR_COUNT||changed[at].field->kind==NQR_CAPACITY)&&qa_actor_id_equal(changed[at].actor,row.actor)) break;
            double value;
            if(!nqr_scalar_decode(row.bytes,row.field->encoding,&value,e)) { free(changed); return false; }
            if(at==used) changed[used++]=row;
            if(row.field->kind==NQR_COUNT) { changed[at].count_present=true; changed[at].count=value; }
            else { changed[at].capacity_present=true; changed[at].capacity=value; }
        } else changed[used++]=row;
    }
    if(used>SIZE_MAX/sizeof(*changed)-frame->pending_count) { free(changed); return nqr_fail(e,QA_ERROR_MEMORY,"Native pending write queue overflows"); }
    nqr_observation *pending=NULL; size_t pending_count=frame->pending_count+used;
    if(pending_count) {
        pending=malloc(pending_count*sizeof(*pending));
        if(!pending) { free(changed); return nqr_fail(e,QA_ERROR_MEMORY,"Owning ordered native canonical commits"); }
        if(frame->pending_count) memcpy(pending,frame->pending,frame->pending_count*sizeof(*pending));
        if(used) memcpy(pending+frame->pending_count,changed,used*sizeof(*pending));
    }
    free(changed);
    if(!nqr_rebase(o,e)) { free(pending); return false; }
    free(frame->pending); frame->pending=pending; frame->pending_count=pending_count; return true;
}
static bool commit(application_native_q2_records *o,const nqr_observation *row,qa_error *e)
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
        return qa_inventory_configure(o->options.inventory,row->actor,&entry,NULL,NULL,e);
    }
    if(f->kind<=NQR_SCORE) {
        double value; if(!nqr_scalar_decode(row->bytes,f->encoding,&value,e)) return false;
        if(f->kind==NQR_HEALTH) {
            float health=(float)value;
            if(!isfinite(health)) return nqr_fail(e,QA_ERROR_FORMAT,"Native health exceeds canonical float storage");
            return qa_combat_set_health(o->options.combat,row->actor,health,e);
        }
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
    if(f->kind==NQR_ORIGIN) body.origin=vector;
    else if(f->kind==NQR_VELOCITY) body.velocity=vector;
    else if(f->kind==NQR_ANGLES) body.angles=vector;
    else if(f->kind==NQR_MIN) body.bounds.mins=vector;
    else body.bounds.maxs=vector;
    return qa_world_body_write(o->options.world,row->actor,&body,e);
}
bool application_native_q2_records_commit(application_native_q2_records *o,qa_error *e)
{
    if(!nqr_current(o,e)||!capture(o,e)) return false;
    application_native_q2_record_scope *frame=o->frame; if(!frame) return true;
    while(frame->cursor<frame->pending_count) {
        nqr_observation row=frame->pending[frame->cursor++];
        if(!commit(o,&row,e)) return false;
    }
    free(frame->pending); frame->pending=NULL; frame->pending_count=frame->cursor=0; return true;
}
bool application_native_q2_records_begin(application_native_q2_records *o,application_native_q2_record_scope **out,qa_error *e)
{
    if(!out||*out||!nqr_current(o,e)||o->closing||o->restoring||o->lifecycle_depth)
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native record transfer requires its actual executable source owner");
    unsigned depth=0; for(application_native_q2_record_scope *f=o->frame;f;f=f->outer) ++depth;
    if(depth>=64) return nqr_fail(e,QA_ERROR_ARGUMENT,"Native record transfer exceeds actual callback depth");
    if(!application_native_q2_records_commit(o,e)||!nqr_releases(o,e)||!application_native_q2_records_refresh(o,e)) return false;
    application_native_q2_record_scope *frame=calloc(1,sizeof(*frame));
    if(!frame) return nqr_fail(e,QA_ERROR_MEMORY,"Retaining entered native projection scope");
    frame->owner=o; frame->outer=o->frame;
    if(!observations(o,&frame->observations,&frame->observation_count,e)) { free(frame); return false; }
    o->frame=frame; *out=frame; return true;
}
bool application_native_q2_records_end(application_native_q2_records *o,application_native_q2_record_scope **scope,bool succeeded,qa_error *e)
{
    application_native_q2_record_scope *frame=scope?*scope:NULL;
    if(!o||!frame||frame->owner!=o||o->frame!=frame) return nqr_fail(e,QA_ERROR_ARGUMENT,"Native cleanup requires its actual innermost record scope");
    if(succeeded&&!application_native_q2_records_commit(o,e)) return false;
    o->frame=frame->outer; free(frame->observations); free(frame->pending); free(frame); *scope=NULL;
    return nqr_releases(o,e);
}
