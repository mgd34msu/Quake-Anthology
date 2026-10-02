#ifndef QA_APPLICATION_NATIVE_Q2_ATTACK_DECISIONS_H
#define QA_APPLICATION_NATIVE_Q2_ATTACK_DECISIONS_H

static bool decision_fail(qa_error *error,qa_status status,const char *message)
{ application_fail(error,status,message); return false; }

static size_t decision_field_bytes(struct application_native_q2_attack *p, qa_json_id field)
{
    qa_json_id encoding=qa_json_get(p->document,field,"encoding");
    static const char *const names[]={"int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
    static const uint8_t sizes[]={1,1,2,2,4,4,8,8,4,8};
    for(size_t i=0;i<sizeof(sizes);++i) if(qa_json_string_equal(p->document,encoding,names[i])) return sizes[i];
    return 0;
}
static bool decision_field_address(struct application_native_q2_attack *p, qa_native_address entity,
    qa_json_id field, size_t bytes, qa_native_address *out, qa_error *error)
{
    uint32_t offset;
    if(!word(p->document,field,"offset",&offset,error)) return false;
    qa_json_id record=qa_json_get(p->document,field,"record");
    if(qa_json_string_equal(p->document,record,"image")) return qa_native_rva(instance(p),offset,bytes,out,error);
    qa_native_address base=entity;
    if(qa_json_string_equal(p->document,record,"client")) {
        if(offset>p->client_bytes || bytes>p->client_bytes-offset ||
            !pointer_read(p,entity+p->client_pointer,&base,error)) return false;
        if(!base) return decision_fail(error,QA_ERROR_NOT_FOUND,"Native weapon decision lost its Source client record");
    } else if(!qa_json_string_equal(p->document,record,"entity"))
        return decision_fail(error,QA_ERROR_FORMAT,"Native weapon decision names another Source record");
    if(base>UINT64_MAX-offset) return decision_fail(error,QA_ERROR_FORMAT,"Native weapon field address overflows");
    *out=base+offset; return true;
}
static bool decision_scalar(struct application_native_q2_attack *p,qa_native_address entity,
    qa_json_id field,double *out,qa_error *error)
{
    size_t count=decision_field_bytes(p,field); uint8_t bytes[8]; qa_native_address address;
    if(!count) return decision_fail(error,QA_ERROR_FORMAT,"Native weapon scalar has no Source encoding");
    if(!decision_field_address(p,entity,field,count,&address,error)||!qa_native_read(instance(p),address,bytes,count,error)) return false;
    qa_json_id encoding=qa_json_get(p->document,field,"encoding");
    if(qa_json_string_equal(p->document,encoding,"float32")) *out=qa_load_f32le(bytes);
    else if(qa_json_string_equal(p->document,encoding,"float64")) { uint64_t bits=qa_load_u64le(bytes); memcpy(out,&bits,sizeof(bits)); }
    else {
        uint64_t bits=count==1?bytes[0]:count==2?qa_load_u16le(bytes):count==4?qa_load_u32le(bytes):qa_load_u64le(bytes);
        bool signed_value=qa_json_string_equal(p->document,encoding,"int8")||qa_json_string_equal(p->document,encoding,"int16")||
            qa_json_string_equal(p->document,encoding,"int32")||qa_json_string_equal(p->document,encoding,"int64");
        if(signed_value) {
            int64_t value=count==1?(int8_t)bits:count==2?(int16_t)bits:count==4?(int32_t)bits:(int64_t)bits;
            if(value < -INT64_C(9007199254740991) || value > INT64_C(9007199254740991))
                return decision_fail(error,QA_ERROR_FORMAT,"Native weapon scalar exceeds its exact numeric domain");
            *out=(double)value;
        } else {
            if(bits>UINT64_C(9007199254740991)) return decision_fail(error,QA_ERROR_FORMAT,"Native weapon scalar exceeds its exact numeric domain");
            *out=(double)bits;
        }
    }
    return isfinite(*out)||decision_fail(error,QA_ERROR_FORMAT,"Native weapon decision returned a nonfinite Source scalar");
}
static bool decision_test(struct application_native_q2_attack *p,qa_native_address entity,
    qa_json_id test,bool *out,qa_error *error)
{
    qa_json_id field=qa_json_get(p->document,test,"field"),kind=qa_json_get(p->document,test,"kind");
    if(qa_json_string_equal(p->document,kind,"pointer")) {
        qa_native_address address,actual,expected=0;
        if(!decision_field_address(p,entity,field,p->pointer_bytes,&address,error)||!pointer_read(p,address,&actual,error)) return false;
        qa_json_id value=qa_json_get(p->document,test,"value");
        if(qa_json_type(p->document,value)!=QA_JSON_NULL) {
            uint32_t rva;
            if(!word(p->document,value,"rva",&rva,error)||!qa_native_rva(instance(p),rva,1,&expected,error)) return false;
            qa_json_id offsets=qa_json_get(p->document,value,"indirections");
            if(qa_json_type(p->document,offsets)!=QA_JSON_ARRAY) return decision_fail(error,QA_ERROR_FORMAT,"Native weapon pointer lacks its indirection roster");
            for(size_t i=0;i<qa_json_size(p->document,offsets);++i) {
                uint64_t offset;
                if(!qa_json_u64(p->document,qa_json_at(p->document,offsets,i),&offset,error)) return false;
                if(offset>UINT32_MAX||expected>UINT64_MAX-offset) return decision_fail(error,QA_ERROR_FORMAT,"Native weapon pointer indirection overflows");
                if(!pointer_read(p,expected+offset,&expected,error)) return false;
            }
        }
        *out=actual==expected; return true;
    }
    if(!qa_json_string_equal(p->document,kind,"scalar")) return decision_fail(error,QA_ERROR_FORMAT,"Native weapon commitment names another test kind");
    double actual,expected;
    if(!decision_scalar(p,entity,field,&actual,error)||!qa_json_number(p->document,qa_json_get(p->document,test,"value"),&expected,error)) return false;
    qa_json_id mask=qa_json_get(p->document,test,"mask");
    if(qa_json_type(p->document,mask)!=QA_JSON_NULL) {
        uint64_t bits;
        if(!qa_json_u64(p->document,mask,&bits,error)) return false;
        if(bits>UINT32_MAX || actual<INT32_MIN || actual>UINT32_MAX || trunc(actual)!=actual)
            return decision_fail(error,QA_ERROR_FORMAT,"Native weapon commitment mask exceeds its Source word");
        actual=(double)((uint32_t)(int64_t)actual&(uint32_t)bits);
    }
    qa_json_id comparison=qa_json_get(p->document,test,"comparison");
    if(qa_json_string_equal(p->document,comparison,"equals")) *out=actual==expected;
    else if(qa_json_string_equal(p->document,comparison,"at-most")) *out=actual<=expected;
    else return decision_fail(error,QA_ERROR_FORMAT,"Native weapon commitment has another Source comparison");
    return true;
}
static bool decision_committed(struct application_native_q2_attack *p,qa_native_address entity,bool *out,qa_error *error)
{
    *out=false;
    for(size_t i=0;i<qa_json_size(p->document,p->committed);++i) {
        qa_json_id group=qa_json_at(p->document,p->committed,i); bool matches=true;
        if(qa_json_type(p->document,group)!=QA_JSON_ARRAY||!qa_json_size(p->document,group))
            return decision_fail(error,QA_ERROR_FORMAT,"Native weapon commitment has an empty Source conjunction");
        for(size_t j=0;matches&&j<qa_json_size(p->document,group);++j)
            if(!decision_test(p,entity,qa_json_at(p->document,group,j),&matches,error)) return false;
        if(matches) { *out=true; return true; }
    }
    return true;
}
static bool decision_store(struct application_native_q2_attack *p,const decision_saved *field,uint32_t bits,qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes,bits);
    return qa_native_write(instance(p),field->address,(qa_bytes){bytes,field->bytes},error);
}
static bool decision_restore(struct application_native_q2_attack *p,qa_error *error)
{
    decision_projection *projection=p->projections;
    qa_actor_id actor;
    if(!qa_native_host_source_actor(p->engine->provider->state.native.host,projection->entity,false,&actor,error)) return false;
    if(!qa_actor_id_equal(actor,projection->actor)) return decision_fail(error,QA_ERROR_NOT_FOUND,"Native weapon projection changed its physical Source generation");
    for(size_t i=projection->count;i>0;--i) {
        decision_saved *field=projection->fields+i-1; uint8_t bytes[4]={0};
        if(!field->written) continue;
        if(!qa_native_read(instance(p),field->address,bytes,field->bytes,error)) return false;
        uint32_t current=qa_load_u32le(bytes);
        if(!decision_store(p,field,(current&~field->mask)|(field->bits&field->mask),error)) return false;
        field->written=false;
    }
    p->projections=projection->next; free(projection->fields); free(projection); return true;
}
static bool decision_call(void *context,qa_native_instance *native,const qa_native_region_event *event,
    qa_native_region_decision *out,qa_error *error)
{
    decision_region *region=context; struct application_native_q2_attack *p=region->owner;
    if(native!=instance(p)||event->region.id!=region->id) return decision_fail(error,QA_ERROR_ARGUMENT,"Native weapon decision has another Source region");
    *out=(qa_native_region_decision){.action=QA_NATIVE_REGION_CONTINUE};
    if(event->phase==QA_NATIVE_REGION_JOIN) {
        if(p->projections&&p->projections->region==region) return decision_restore(p,error);
        return true;
    }
    decision_frame *frame=p->dispatch;
    if(frame) frame->reached=true;
    if(!frame || frame->committed || application_provider_for(p->engine->provider->application,frame->actor,QA_ROLE_ARSENAL,NULL)==p->engine->provider) return true;
    qa_actor_id actor;
    if(!source_actor(p,frame->entity,&actor,error)) return false;
    if(!qa_actor_id_equal(actor,frame->actor)) return decision_fail(error,QA_ERROR_NOT_FOUND,"Native weapon decision changed its physical Source actor");
    decision_projection *projection=calloc(1,sizeof(*projection));
    if(!projection) return decision_fail(error,QA_ERROR_MEMORY,"Retaining actual native weapon decision");
    size_t count=qa_json_size(p->document,region->fields);
    projection->fields=calloc(count,sizeof(*projection->fields));
    if(!projection->fields) { free(projection); return decision_fail(error,QA_ERROR_MEMORY,"Retaining original weapon input masks"); }
    projection->region=region; projection->actor=frame->actor; projection->entity=frame->entity;
    projection->next=p->projections; p->projections=projection;
    for(size_t i=0;i<count;++i) {
        qa_json_id row=qa_json_at(p->document,region->fields,i),field=qa_json_get(p->document,row,"field");
        decision_saved *saved=projection->fields+i; uint8_t bytes[4]={0};
        saved->bytes=decision_field_bytes(p,field);
        if(!saved->bytes||saved->bytes>4||qa_json_string_equal(p->document,qa_json_get(p->document,field,"encoding"),"float32"))
            return decision_fail(error,QA_ERROR_FORMAT,"Native weapon input mask requires an integer Source word");
        if(!word(p->document,row,"clearMask",&saved->mask,error)||!saved->mask||
            !decision_field_address(p,frame->entity,field,saved->bytes,&saved->address,error)||
            !qa_native_read(native,saved->address,bytes,saved->bytes,error)) return false;
        if(saved->bytes<4 && saved->mask>=(UINT32_C(1)<<(saved->bytes*8)))
            return decision_fail(error,QA_ERROR_FORMAT,"Native weapon input mask exceeds its actual Source storage width");
        saved->bits=qa_load_u32le(bytes); ++projection->count;
    }
    for(size_t i=0;i<count;++i) {
        decision_saved *saved=projection->fields+i; saved->written=true;
        if(!decision_store(p,saved,saved->bits&~saved->mask,error)) return false;
    }
    return true;
}
static bool dispatcher_call(void *context,qa_native_instance *native,qa_native_entry_observer *binding,
    const qa_native_value *arguments,size_t count,qa_native_value *result,qa_error *error)
{
    struct application_native_q2_attack *p=context;
    if(native!=instance(p)||binding!=p->dispatcher_hook||count!=p->dispatcher_count||arguments[p->dispatcher_argument].type!=QA_NATIVE_ADDRESS)
        return decision_fail(error,QA_ERROR_ARGUMENT,"Native weapon dispatcher differs from its original Source ABI");
    qa_native_address entity=arguments[p->dispatcher_argument].as.address;
    if(!entity) return qa_native_invoke_original(binding,arguments,count,result,error);
    qa_actor_id actor;
    if(!qa_native_host_source_actor(p->engine->provider->state.native.host,entity,false,&actor,error)) return false;
    if(!actor.registry) return qa_native_invoke_original(binding,arguments,count,result,error);
    decision_frame frame={.previous=p->dispatch,.actor=actor,.entity=entity,.base=p->projections};
    if(!decision_committed(p,entity,&frame.committed,error)) return false;
    p->dispatch=&frame; ++p->engine->calls;
    bool ok=qa_native_invoke_original(binding,arguments,count,result,error);
    --p->engine->calls;
    qa_error cleanup={0};
    while(p->projections!=frame.base) if(!decision_restore(p,&cleanup)) { if(ok&&error) *error=cleanup; ok=false; break; }
    p->dispatch=frame.previous;
    const application_native_q2_input_stage *stage=p->engine->input_stage;
    qa_application *app=p->engine->provider->application;
    if(ok&&frame.reached&&stage&&qa_actor_id_equal(stage->actor,actor)&&
        application_provider_for(app,actor,QA_ROLE_MOVEMENT,NULL)==p->engine->provider&&
        application_provider_for(app,actor,QA_ROLE_ARSENAL,NULL)!=p->engine->provider) {
        if(!stage->current(stage->context,actor)||!stage->arsenal||!p->engine->input_command)
            return decision_fail(error,QA_ERROR_NOT_FOUND,"Native weapon decision lost its actual selected arsenal stage");
        bool previous=p->engine->input_arsenal;
        bool committed=p->engine->input_arsenal_committed;
        p->engine->input_arsenal=true;
        p->engine->input_arsenal_committed=frame.committed;
        ok=stage->arsenal(stage->context,actor,p->engine->input_command,stage->time_ns,error);
        p->engine->input_arsenal=previous;
        p->engine->input_arsenal_committed=committed;
    }
    return ok;
}
#endif
