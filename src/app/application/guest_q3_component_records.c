#include "guest_q3_component_records_private.h"
#include <limits.h>

bool q3records_fail(qa_error *e,qa_status code,const char *message)
{ qa_error_set(e,code,0,"%s",message); return false; }
bool q3records_live(const application_q3_component_records *r,qa_actor_id actor)
{ return r&&qa_actors_get(qa_session_actors(r->options.session),actor)!=NULL; }
component_actor *q3records_actor(application_q3_component_records *r,qa_actor_id actor)
{ for(size_t i=0;i<r->actor_count;++i) if(qa_actor_id_equal(r->actors[i].actor,actor)) return r->actors+i; return NULL; }
bool q3records_raw(application_q3_component_records *r,uint32_t address,qa_bytes bytes,qa_error *e)
{ return r->options.storage_current(r->options.context,e)&&qa_qvm_write(r->options.vm,address,bytes,e); }
bool q3records_scalar(double value,bool floating,uint8_t out[4],qa_error *e)
{
    if(!isfinite(value)) return q3records_fail(e,QA_ERROR_FORMAT,"Component field is not finite");
    uint32_t word;
    if(floating) { float f=(float)value; if(!isfinite(f)) return q3records_fail(e,QA_ERROR_FORMAT,"Component field exceeds float32"); memcpy(&word,&f,4); }
    else { double n=trunc(value); if(n<INT32_MIN||n>INT32_MAX) return q3records_fail(e,QA_ERROR_FORMAT,"Component field exceeds int32"); int32_t s=(int32_t)n; memcpy(&word,&s,4); }
    qa_store_u32le(out,word); return true;
}
static bool word(const qa_json_document *d,qa_json_id id,uint32_t *out,qa_error *e)
{ uint64_t n; if(!qa_json_u64(d,id,&n,e)) return false; if(n>UINT32_MAX) return q3records_fail(e,QA_ERROR_FORMAT,"Component layout exceeds its source word"); *out=(uint32_t)n; return true; }
static bool string_id(const qa_json_document *d,qa_json_id id,qa_strings *strings,qa_string_id *out,qa_error *e)
{ qa_buffer b={0}; if(!qa_json_string(d,id,&b,e)) return false; bool ok=b.size&&!memchr(b.data,0,b.size)&&qa_strings_intern(strings,(qa_bytes){b.data,b.size},out,e); qa_buffer_free(&b); return ok||q3records_fail(e,QA_ERROR_FORMAT,"Component binding requires a nonempty identity"); }
static bool record_index(const qa_json_document *d,qa_json_id id,application_q3_component_records *r,size_t *out,qa_error *e)
{ for(size_t i=0;i<r->record_count;++i) if(qa_json_string_equal(d,id,r->records[i].id)) { *out=i; return true; } return q3records_fail(e,QA_ERROR_FORMAT,"Component field names an undeclared record"); }
static bool vector(const qa_json_document *d,qa_json_id id,uint8_t bytes[12],qa_error *e)
{ const char *axes[]={"x","y","z"}; for(size_t i=0;i<3;++i) { double n; if(!qa_json_number(d,qa_json_get(d,id,axes[i]),&n,e)||!q3records_scalar(n,true,bytes+i*4,e)) return false; } return true; }
static bool fields(application_q3_component_records *r,component_record *record,const qa_json_document *d,qa_json_id rows,qa_error *e)
{
    if(qa_json_type(d,rows)!=QA_JSON_ARRAY) return q3records_fail(e,QA_ERROR_FORMAT,"Component actor fields require a list");
    record->field_count=qa_json_size(d,rows);
    record->fields=record->field_count?calloc(record->field_count,sizeof(*record->fields)):NULL;
    if(record->field_count&&!record->fields) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining component actor fields");
    static const struct { const char *name; component_field_kind kind; qa_body_vector_kind body; } bindings[]={
        {"health",COMPONENT_HEALTH,0},{"inventory",COMPONENT_INVENTORY,0},
        {"team",COMPONENT_TEAM,0},{"score",COMPONENT_SCORE,0},
        {"origin",COMPONENT_BODY,QA_BODY_ORIGIN},{"velocity",COMPONENT_BODY,QA_BODY_VELOCITY},
        {"angles",COMPONENT_BODY,QA_BODY_ANGLES},{"bounds-min",COMPONENT_BODY,QA_BODY_MINIMUM},
        {"bounds-max",COMPONENT_BODY,QA_BODY_MAXIMUM},{"record",COMPONENT_RECORD,0},
        {"constant",COMPONENT_CONSTANT,0},{"constant-vector",COMPONENT_VECTOR,0},
        {"private",COMPONENT_PRIVATE,0}
    };
    for(size_t i=0;i<record->field_count;++i) {
        component_field *f=record->fields+i; qa_json_id row=qa_json_at(d,rows,i),binding=qa_json_get(d,row,"binding");
        size_t at=0; for(;at<sizeof(bindings)/sizeof(*bindings);++at) if(qa_json_string_equal(d,binding,bindings[at].name)) break;
        if(at==sizeof(bindings)/sizeof(*bindings)||!word(d,qa_json_get(d,row,"offset"),&f->offset,e)) return q3records_fail(e,QA_ERROR_FORMAT,"Unknown component field binding");
        component_field_kind kind=f->kind=bindings[at].kind;
        if(kind==COMPONENT_BODY) f->body=bindings[at].body;
        f->length=kind==COMPONENT_BODY||kind==COMPONENT_VECTOR?12:4;
        f->writable=kind<=COMPONENT_BODY&&!qa_json_string_equal(d,qa_json_get(d,row,"access"),"read-only");
        if(kind<=COMPONENT_SCORE||kind==COMPONENT_CONSTANT) {
            qa_json_id encoding=qa_json_get(d,row,"encoding"); f->floating=qa_json_string_equal(d,encoding,"float32");
            if(!f->floating&&!qa_json_string_equal(d,encoding,"int32")) return q3records_fail(e,QA_ERROR_FORMAT,"Component field requires its declared scalar encoding");
        }
        if(kind==COMPONENT_PRIVATE&&!word(d,qa_json_get(d,row,"byteLength"),&f->length,e)) return false;
        if(!f->length||f->offset>record->stride||f->length>record->stride-f->offset) return q3records_fail(e,QA_ERROR_FORMAT,"Component field exceeds its actual record");
        if(kind==COMPONENT_INVENTORY&&!string_id(d,qa_json_get(d,row,"item"),r->options.strings,&f->item,e)) return false;
        if(kind==COMPONENT_RECORD&&!record_index(d,qa_json_get(d,row,"record"),r,&f->target,e)) return false;
        if(kind==COMPONENT_CONSTANT) { double n; if(!qa_json_number(d,qa_json_get(d,row,"value"),&n,e)||!q3records_scalar(n,f->floating,f->initial,e)) return false; }
        if(kind==COMPONENT_VECTOR&&!vector(d,qa_json_get(d,row,"value"),f->initial,e)) return false;
        if(kind==COMPONENT_TEAM) {
            qa_json_id values=qa_json_get(d,row,"values"); f->team_count=qa_json_size(d,values);
            if(qa_json_type(d,values)!=QA_JSON_ARRAY||!f->team_count) return q3records_fail(e,QA_ERROR_FORMAT,"Component team requires actual aliases");
            f->teams=calloc(f->team_count,sizeof(*f->teams)); if(!f->teams) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining component teams");
            for(size_t j=0;j<f->team_count;++j) {
                qa_json_id v=qa_json_at(d,values,j),team=qa_json_get(d,v,"team"); uint8_t raw[4];
                if(!qa_json_number(d,qa_json_get(d,v,"value"),&f->teams[j].value,e)||!q3records_scalar(f->teams[j].value,f->floating,raw,e)||
                    (qa_json_type(d,team)!=QA_JSON_NULL&&!string_id(d,team,r->options.strings,&f->teams[j].team,e))) return false;
                for(size_t k=0;k<j;++k) if(f->teams[k].team==f->teams[j].team||f->teams[k].value==f->teams[j].value) return q3records_fail(e,QA_ERROR_FORMAT,"Component team aliases are not distinct");
            }
        }
    }
    return true;
}
bool application_q3_component_records_create(const application_q3_component_records_options *o,
    application_q3_component_records **out,qa_error *e)
{
    if(!o||!out||*out||!o->profile||!o->vm||!o->image||!o->session||!o->world||!o->combat||!o->inventory||!o->strings||!o->storage_current||!o->bound||!o->released||!o->lifecycle)
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component projection requires actual retained source and shared owners");
    application_q3_component_records *r=calloc(1,sizeof(*r)); if(!r) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining component actor projection");
    r->options=*o; r->entity_record=r->player_record=SIZE_MAX; *out=r;
    r->record_count=application_q3_mod_record_count(o->profile); r->records=r->record_count?calloc(r->record_count,sizeof(*r->records)):NULL;
    if(r->record_count&&!r->records) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining actual component record roster");
    for(size_t i=0;i<r->record_count;++i) {
        component_record *row=r->records+i;
        if(!application_q3_mod_record(o->profile,i,&row->id,&row->address,&row->stride,&row->capacity)) return q3records_fail(e,QA_ERROR_FORMAT,"Missing component record metadata");
        row->client=application_q3_mod_record_is_client(o->profile,i);
    }
    qa_json_document *d=NULL; if(!qa_json_parse(application_q3_mod_declaration(o->profile),&d,e)) return false;
    qa_json_id root=qa_json_root(d),rows=qa_json_get(d,root,"actorRecords"); bool ok=qa_json_size(d,rows)==r->record_count;
    for(size_t i=0;ok&&i<r->record_count;++i) ok=fields(r,r->records+i,d,qa_json_get(d,qa_json_at(d,rows,i),"fields"),e);
    qa_json_id entity=qa_json_get(d,root,"entityRecord");
    if(ok&&qa_json_type(d,entity)!=QA_JSON_NULL) ok=record_index(d,entity,r,&r->entity_record,e);
    const char *entity_name=NULL,*player_name=NULL;
    if(ok&&application_q3_mod_clients(o->profile,&r->client_maximum,&entity_name,&player_name)) {
        for(size_t i=0;i<r->record_count;++i) if(!strcmp(r->records[i].id,player_name)) r->player_record=i;
        qa_json_id outputs=qa_json_get(d,qa_json_get(d,root,"clients"),"outputs");
        for(size_t i=0;i<qa_json_size(d,outputs);++i) {
            qa_json_id output=qa_json_at(d,outputs,i);
            if(!qa_json_string_equal(d,qa_json_get(d,output,"kind"),"body-shape")) continue;
            const char *ends[]={"min","max"};
            for(size_t k=0;k<2&&ok;++k) {
                qa_json_id endpoint=qa_json_get(d,output,ends[k]); size_t at; uint32_t offset;
                ok=record_index(d,qa_json_get(d,endpoint,"record"),r,&at,e)&&word(d,qa_json_get(d,endpoint,"offset"),&offset,e);
                if(ok) for(size_t j=0;j<r->records[at].field_count;++j) if(r->records[at].fields[j].offset==offset) r->records[at].fields[j].body_output=true;
            }
        }
    }
    qa_json_id source=qa_json_get(d,root,"sourceActors"); r->has_source=source!=QA_JSON_NONE;
    if(ok&&r->has_source) ok=r->entity_record!=SIZE_MAX&&word(d,qa_json_get(d,source,"inuse"),&r->inuse,e);
    qa_json_destroy(d);
    return ok||q3records_fail(e,QA_ERROR_FORMAT,"Component projection differs from its declared record roster");
}
bool application_q3_component_records_idle(const application_q3_component_records *r)
{ return r&&!r->frame&&!r->refreshing; }
bool application_q3_component_records_destroy(application_q3_component_records **slot,qa_error *e)
{
    if(!slot||!*slot) return true;
    application_q3_component_records *r=*slot;
    if(!application_q3_component_records_idle(r)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component actor projection has a real source call");
    for(size_t i=0;r->records&&i<r->record_count;++i) { component_record *record=r->records+i; for(size_t j=0;record->fields&&j<record->field_count;++j) free(record->fields[j].teams); free(record->fields); qa_buffer_free(&record->defaults); }
    free(r->records); free(r->actors); free(r); *slot=NULL; return true;
}
bool application_q3_component_records_defaults(application_q3_component_records *r,qa_error *e)
{
    if(!r||!application_q3_component_records_idle(r)||r->defaults_ready) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component defaults require reached Initialize before foreign projections");
    for(size_t i=0;i<r->actor_count;++i) if(!r->actors[i].owned) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component defaults already contain a foreign projection");
    for(size_t i=0;i<r->record_count;++i) {
        component_record *record=r->records+i; size_t bytes=(size_t)record->stride*record->capacity;
        if(!record->defaults.data) record->defaults.data=malloc(bytes);
        if(!record->defaults.data) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining reached component record defaults");
        if(!qa_qvm_read(r->options.vm,record->address,record->defaults.data,bytes,e)) return false;
        record->defaults.size=bytes;
    }
    r->defaults_ready=true; return true;
}
static bool reset(application_q3_component_records *r,uint32_t slot,bool client,qa_error *e)
{
    for(size_t i=0;i<r->record_count;++i) {
        component_record *record=r->records+i; if((record->client&&!client)||slot>=record->capacity) continue;
        if(!q3records_raw(r,record->address+slot*record->stride,(qa_bytes){record->defaults.data+(size_t)slot*record->stride,record->stride},e)) return false;
    }
    return true;
}
bool q3records_reserve_actor(application_q3_component_records *r,qa_error *e)
{
    if(r->actor_count<r->actor_capacity) return true;
    if(r->actor_count==SIZE_MAX/sizeof(*r->actors)) return q3records_fail(e,QA_ERROR_MEMORY,"Component actor row extent overflows");
    size_t capacity=r->actor_count+1;
    component_actor *rows=realloc(r->actors,capacity*sizeof(*rows));
    if(!rows) return q3records_fail(e,QA_ERROR_MEMORY,"Retaining actual component actor row");
    r->actors=rows; r->actor_capacity=capacity; return true;
}
bool application_q3_component_records_bind(application_q3_component_records *r,qa_actor_id actor,uint32_t slot,bool owned,bool client,qa_error *e)
{
    if(!r||(!owned&&!r->defaults_ready)||!q3records_live(r,actor)||!r->options.storage_current(r->options.context,e)||slot>=1022||
        (client?slot>=r->client_maximum:slot<r->client_maximum)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component projection requires a genuine available source slot");
    component_actor *existing=q3records_actor(r,actor);
    if(existing&&(existing->retired||existing->slot!=slot||existing->owned!=owned||existing->client!=client)) return q3records_fail(e,QA_ERROR_FORMAT,"Component actor projection changed identity");
    if(existing&&existing->projected) return true;
    for(size_t i=0;i<r->actor_count;++i) if(r->actors+i!=existing&&r->actors[i].slot==slot) return q3records_fail(e,QA_ERROR_FORMAT,"Component projection slot is occupied");
    for(size_t i=0;i<r->record_count;++i) if((!r->records[i].client||client)&&slot>=r->records[i].capacity) return q3records_fail(e,QA_ERROR_FORMAT,"Component auxiliary projection capacity exceeded");
    if(!existing) {
        if(!q3records_reserve_actor(r,e)) return false;
        r->actors[r->actor_count++]=(component_actor){.actor=actor,.slot=slot,.owned=owned,.client=client};
    }
    if(!owned) {
        if(!reset(r,slot,client,e)) return false;
        for(size_t i=0;i<r->record_count;++i) {
            component_record *record=r->records+i; if(record->client&&!client) continue;
            for(size_t j=0;j<record->field_count;++j) {
                component_field *f=record->fields+j; uint8_t pointer[4]; const uint8_t *bytes=f->initial;
                if(f->kind==COMPONENT_RECORD) { component_record *target=r->records+f->target; qa_store_u32le(pointer,target->client&&!client?0:target->address+slot*target->stride); bytes=pointer; }
                else if(f->kind!=COMPONENT_CONSTANT&&f->kind!=COMPONENT_VECTOR) continue;
                if(!q3records_raw(r,record->address+slot*record->stride+f->offset,(qa_bytes){bytes,f->length},e)) return false;
            }
        }
        if(r->has_source) { uint8_t raw[4]; component_record *entity=r->records+r->entity_record; qa_store_u32le(raw,1); if(!q3records_raw(r,entity->address+slot*entity->stride+r->inuse,(qa_bytes){raw,4},e)) return false; qa_store_u32le(raw,slot); if(!q3records_raw(r,entity->address+slot*entity->stride,(qa_bytes){raw,4},e)) return false; }
    }
    if(!r->options.bound(r->options.context,actor,slot,owned,client,e)) return false;
    existing=q3records_actor(r,actor); if(!existing||existing->retired) return q3records_fail(e,QA_ERROR_NOT_FOUND,"Component actor retired during source binding");
    existing->projected=true; return true;
}
bool application_q3_component_records_reserve_client(application_q3_component_records *r,qa_actor_id actor,uint32_t *out,qa_error *e)
{
    if(!r||!out||!r->client_maximum||!q3records_live(r,actor)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component reservation requires an actual canonical client");
    component_actor *row=q3records_actor(r,actor);
    if(row) { if(!row->client||row->retired) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component client already occupies another projection"); *out=row->slot; return true; }
    uint32_t slot=0;
    for(;slot<r->client_maximum;++slot) { bool used=false; for(size_t i=0;i<r->actor_count;++i) if(r->actors[i].slot==slot) used=true; if(!used) break; }
    if(slot==r->client_maximum) return q3records_fail(e,QA_ERROR_FORMAT,"Component source client capacity exceeded");
    if(!q3records_reserve_actor(r,e)) return false;
    r->actors[r->actor_count++]=(component_actor){.actor=actor,.slot=slot,.client=true}; *out=slot; return true;
}
bool q3records_finish_retired(application_q3_component_records *r,qa_error *e)
{
    if(r->frame) return true;
    for(size_t i=0;i<r->actor_count;) {
        component_actor row=r->actors[i]; if(!row.retired) { ++i; continue; }
        if(!row.owned&&!reset(r,row.slot,true,e)) return false;
        memmove(r->actors+i,r->actors+i+1,(r->actor_count-i-1)*sizeof(*r->actors)); --r->actor_count;
    }
    return true;
}
bool application_q3_component_records_release(application_q3_component_records *r,qa_actor_id actor,qa_error *e)
{
    component_actor *row=r?q3records_actor(r,actor):NULL; if(!row) return true;
    row->retired=true; if(!r->options.released(r->options.context,actor,e)) return false;
    return q3records_finish_retired(r,e);
}
bool application_q3_component_records_admitted(application_q3_component_records *r,qa_actor_id actor,bool admitted,qa_error *e)
{ component_actor *row=r?q3records_actor(r,actor):NULL; if(!row||!row->client||row->retired||!q3records_live(r,actor)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component admission lost its actual client row"); row->admitted=admitted; return true; }
bool application_q3_component_records_eligible(void *context,qa_actor_id actor)
{ application_q3_component_records *r=context; component_actor *row=r?q3records_actor(r,actor):NULL; return q3records_live(r,actor)&&(!row||!row->retired); }
bool application_q3_component_records_live_client(void *context,qa_actor_id actor)
{ application_q3_component_records *r=context; component_actor *row=r?q3records_actor(r,actor):NULL; return row&&row->client&&row->admitted&&application_q3_component_records_eligible(r,actor); }
bool application_q3_component_records_client_slot(void *context,qa_actor_id actor,int32_t *out,qa_error *e)
{ application_q3_component_records *r=context; component_actor *row=r?q3records_actor(r,actor):NULL; if(!out||!row||!row->client||row->retired||!q3records_live(r,actor)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component source call requires its genuine client reservation"); *out=(int32_t)row->slot; return true; }
bool application_q3_component_records_pointer(void *context,qa_actor_id actor,const char *name,uint32_t *out,qa_error *e)
{
    application_q3_component_records *r=context;
    if(!r||!name||!out||!r->options.storage_current(r->options.context,e)) return false;
    if(!actor.registry) { *out=0; return true; }
    component_actor *row=q3records_actor(r,actor);
    if(!row) {
        uint32_t slot=r->client_maximum;
        for(;;++slot) { bool used=false; for(size_t i=0;i<r->actor_count;++i) if(r->actors[i].slot==slot) used=true; if(!used) break; }
        if(!application_q3_component_records_bind(r,actor,slot,false,false,e)) return false;
        row=q3records_actor(r,actor);
    }
    if(row->retired||!q3records_live(r,actor)) return q3records_fail(e,QA_ERROR_NOT_FOUND,"Component projection actor is retired");
    if(!row->projected) { uint32_t slot=row->slot; bool client=row->client; if(!application_q3_component_records_bind(r,actor,slot,false,client,e)) return false; row=q3records_actor(r,actor); }
    for(size_t i=0;i<r->record_count;++i) if(!strcmp(r->records[i].id,name)) {
        component_record *record=r->records+i;
        if((record->client&&!row->client)||row->slot>=record->capacity) return q3records_fail(e,QA_ERROR_FORMAT,"Component actor has no auxiliary source record");
        *out=record->address+row->slot*record->stride; return true;
    }
    return q3records_fail(e,QA_ERROR_FORMAT,"Component call names an undeclared actor record");
}
