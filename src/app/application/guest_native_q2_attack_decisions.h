#ifndef QA_APPLICATION_NATIVE_Q2_ATTACK_DECISIONS_H
#define QA_APPLICATION_NATIVE_Q2_ATTACK_DECISIONS_H

static bool decision_fail(qa_error *error,qa_status status,const char *message)
{ application_fail(error,status,message); return false; }

static void *decision_array(struct application_native_q2_attack *p,size_t count,size_t bytes,
    size_t alignment,qa_error *error)
{
    if(count>SIZE_MAX/bytes) {
        decision_fail(error,QA_ERROR_MEMORY,"Native weapon descriptor size overflows"); return NULL;
    }
    void *out=qa_arena_alloc(&p->decision_storage,count*bytes,alignment,error);
    if(out) memset(out,0,count*bytes);
    return out;
}
static bool decision_field_prepare(struct application_native_q2_attack *p,qa_json_id node,
    uint8_t bytes,qa_native_value_type encoding,decision_field *out,qa_error *error)
{
    decision_field field={.bytes=bytes,.encoding=encoding};
    if(!word(p->document,node,"offset",&field.offset,error)) return false;
    qa_json_id record=qa_json_get(p->document,node,"record");
    if(qa_json_string_equal(p->document,record,"image")) field.record=DECISION_IMAGE;
    else if(qa_json_string_equal(p->document,record,"entity")) field.record=DECISION_ENTITY;
    else if(qa_json_string_equal(p->document,record,"client")) {
        field.record=DECISION_CLIENT;
        if(field.offset>p->client_bytes || bytes>p->client_bytes-field.offset)
            return decision_fail(error,QA_ERROR_FORMAT,"Native weapon field exceeds its Source client record");
    } else return decision_fail(error,QA_ERROR_FORMAT,"Native weapon decision names another Source record");
    *out=field; return true;
}
static bool decision_scalar_type(struct application_native_q2_attack *p, qa_json_id encoding,
    decision_field *out, qa_error *error)
{
    static const struct { const char *name; qa_native_value_type encoding; uint8_t bytes; } types[]={
        {"int8",QA_NATIVE_I8,1},{"uint8",QA_NATIVE_U8,1},{"int16",QA_NATIVE_I16,2},
        {"uint16",QA_NATIVE_U16,2},{"int32",QA_NATIVE_I32,4},{"uint32",QA_NATIVE_U32,4},
        {"int64",QA_NATIVE_I64,8},{"uint64",QA_NATIVE_U64,8},{"float32",QA_NATIVE_F32,4},
        {"float64",QA_NATIVE_F64,8}};
    for(size_t i=0;i<sizeof(types)/sizeof(types[0]);++i)
        if(qa_json_string_equal(p->document,encoding,types[i].name)) {
            out->bytes = types[i].bytes; out->encoding = types[i].encoding; return true;
        }
    return decision_fail(error,QA_ERROR_FORMAT,"Native weapon scalar has no Source encoding");
}
static bool decision_scalar_prepare(struct application_native_q2_attack *p,qa_json_id node,
    decision_field *out,qa_error *error)
{
    decision_field type = {0};
    return decision_scalar_type(p, qa_json_get(p->document,node,"encoding"), &type, error) &&
        decision_field_prepare(p,node,type.bytes,type.encoding,out,error);
}
static bool decision_test_prepare(struct application_native_q2_attack *p,qa_json_id node,
    decision_test_descriptor *out,qa_error *error)
{
    qa_json_id field=qa_json_get(p->document,node,"field"),kind=qa_json_get(p->document,node,"kind");
    if(qa_json_string_equal(p->document,kind,"pointer")) {
        out->pointer=true;
        if(!decision_field_prepare(p,field,p->pointer_bytes,QA_NATIVE_ADDRESS,&out->field,error)) return false;
        qa_json_id value=qa_json_get(p->document,node,"value");
        out->has_pointer_value=qa_json_type(p->document,value)!=QA_JSON_NULL;
        if(!out->has_pointer_value) return true;
        if(!word(p->document,value,"rva",&out->rva,error)) return false;
        qa_json_id offsets=qa_json_get(p->document,value,"indirections");
        if(qa_json_type(p->document,offsets)!=QA_JSON_ARRAY)
            return decision_fail(error,QA_ERROR_FORMAT,"Native weapon pointer lacks its indirection roster");
        out->indirection_count=qa_json_size(p->document,offsets);
        if(out->indirection_count) {
            out->indirections=decision_array(p,out->indirection_count,sizeof(*out->indirections),
                _Alignof(uint32_t),error);
            if(!out->indirections) return false;
        }
        for(size_t i=0;i<out->indirection_count;++i) {
            uint64_t offset;
            if(!qa_json_u64(p->document,qa_json_at(p->document,offsets,i),&offset,error)) return false;
            if(offset>UINT32_MAX) return decision_fail(error,QA_ERROR_FORMAT,"Native weapon pointer indirection overflows");
            out->indirections[i]=(uint32_t)offset;
        }
        return true;
    }
    if(!qa_json_string_equal(p->document,kind,"scalar"))
        return decision_fail(error,QA_ERROR_FORMAT,"Native weapon commitment names another test kind");
    if(!decision_scalar_prepare(p,field,&out->field,error) ||
        !qa_json_number(p->document,qa_json_get(p->document,node,"value"),&out->expected,error)) return false;
    qa_json_id mask=qa_json_get(p->document,node,"mask");
    out->masked=qa_json_type(p->document,mask)!=QA_JSON_NULL;
    if(out->masked) {
        uint64_t bits;
        if(!qa_json_u64(p->document,mask,&bits,error)) return false;
        if(bits>UINT32_MAX) return decision_fail(error,QA_ERROR_FORMAT,"Native weapon commitment mask exceeds its Source word");
        out->mask=(uint32_t)bits;
    }
    qa_json_id comparison=qa_json_get(p->document,node,"comparison");
    if(qa_json_string_equal(p->document,comparison,"equals")) out->at_most=false;
    else if(qa_json_string_equal(p->document,comparison,"at-most")) out->at_most=true;
    else return decision_fail(error,QA_ERROR_FORMAT,"Native weapon commitment has another Source comparison");
    return true;
}
static bool decision_group_prepare(struct application_native_q2_attack *p, qa_json_id row,
    decision_group *group, qa_error *error)
{
    if (qa_json_type(p->document,row) != QA_JSON_ARRAY)
        return decision_fail(error,QA_ERROR_FORMAT,"Native weapon tests require their Source conjunction");
    group->count=qa_json_size(p->document,row);
    if (group->count) {
        group->tests=decision_array(p,group->count,sizeof(*group->tests),_Alignof(decision_test_descriptor),error);
        if(!group->tests) return false;
    }
    for(size_t j=0;j<group->count;++j)
        if(!decision_test_prepare(p,qa_json_at(p->document,row,j),group->tests+j,error)) return false;
    return true;
}
static bool decision_groups_prepare(struct application_native_q2_attack *p, qa_json_id rows,
    decision_group **out, size_t *count, qa_error *error)
{
    if (qa_json_type(p->document,rows) != QA_JSON_ARRAY)
        return decision_fail(error,QA_ERROR_FORMAT,"Native weapon alternatives require their Source test roster");
    *count=qa_json_size(p->document,rows);
    if (*count) {
        *out=decision_array(p,*count,sizeof(**out),_Alignof(decision_group),error);
        if (!*out) return false;
    }
    for(size_t i=0;i<*count;++i)
        if (!decision_group_prepare(p,qa_json_at(p->document,rows,i),*out+i,error)) return false;
    return true;
}
static bool decision_descriptors_prepare(struct application_native_q2_attack *p,qa_json_id weapons,
    qa_json_id committed,qa_json_id decisions,qa_error *error)
{
    if (!decision_groups_prepare(p,committed,&p->committed,&p->committed_count,error)) return false;
    qa_json_id active=qa_json_get(p->document,weapons,"active");
    if (active != QA_JSON_NONE && !decision_group_prepare(p,active,&p->available,error)) return false;
    qa_json_id continuations=qa_json_get(p->document,weapons,"continuations");
    p->continuations_declared=continuations != QA_JSON_NONE;
    if (p->continuations_declared &&
        !decision_groups_prepare(p,continuations,&p->continuations,&p->continuation_count,error)) return false;
    qa_json_id clock=qa_json_get(p->document,weapons,"time");
    p->clock_declared=clock != QA_JSON_NONE;
    if (p->clock_declared) {
        p->clock.record=DECISION_IMAGE;
        if (!word(p->document,clock,"address",&p->clock.offset,error) ||
            !decision_scalar_type(p,qa_json_get(p->document,clock,"encoding"),&p->clock,error) ||
            !qa_json_number(p->document,qa_json_get(p->document,clock,"milliseconds"),&p->clock_milliseconds,error)) return false;
        if (!isfinite(p->clock_milliseconds) || p->clock_milliseconds <= 0)
            return decision_fail(error,QA_ERROR_FORMAT,"Native level time requires its positive Source scale");
    }
    for(size_t i=0;i<p->decision_count;++i) {
        decision_region *region=p->decisions+i;
        qa_json_id fields=qa_json_get(p->document,qa_json_at(p->document,decisions,i),"fields");
        region->fields=decision_array(p,region->field_count,sizeof(*region->fields),_Alignof(decision_input),error);
        if(!region->fields) return false;
        for(size_t j=0;j<region->field_count;++j) {
            qa_json_id row=qa_json_at(p->document,fields,j);
            decision_input *input=region->fields+j;
            if(!decision_scalar_prepare(p,qa_json_get(p->document,row,"field"),&input->field,error)) return false;
            if(input->field.bytes>4 || input->field.encoding==QA_NATIVE_F32)
                return decision_fail(error,QA_ERROR_FORMAT,"Native weapon input mask requires an integer Source word");
            if(!word(p->document,row,"clearMask",&input->mask,error) || !input->mask) return false;
            if(input->field.bytes<4 && input->mask>=(UINT32_C(1)<<(input->field.bytes*8)))
                return decision_fail(error,QA_ERROR_FORMAT,"Native weapon input mask exceeds its actual Source storage width");
        }
    }
    qa_json_id entity=qa_json_get(p->document,weapons,"entity"),root=qa_json_root(p->document);
    qa_json_id spawn=qa_json_get(p->document,weapons,"spawn");
    p->client_spawn_declared=spawn != QA_JSON_NONE;
    if (p->client_spawn_declared) {
        qa_json_id accepted=qa_json_get(p->document,spawn,"accepted");
        if (!word(p->document,spawn,"entry",&p->client_spawn,error) ||
            (accepted != QA_JSON_NONE && !decision_group_prepare(p,accepted,&p->spawn_accepted,error)) ||
            !decision_scalar_prepare(p,qa_json_get(p->document,entity,"maxHealth"),&p->max_health,error)) return false;
    }
    if(!decision_scalar_prepare(p,qa_json_get(p->document,entity,"waterLevel"),&p->water_level,error) ||
        !word(p->document,qa_json_get(p->document,root,"player"),"commandAngles",&p->command_angles,error)) return false;
    if(p->command_angles>p->client_bytes || p->client_bytes-p->command_angles<12)
        return decision_fail(error,QA_ERROR_FORMAT,"Native Source command angles exceed its qualified client record");
    qa_arena_seal(&p->decision_storage); return true;
}
static bool decision_field_address(struct application_native_q2_attack *p,qa_native_address entity,
    const decision_field *field,qa_native_address *out,qa_error *error)
{
    if(field->record==DECISION_IMAGE) return qa_native_rva(instance(p),field->offset,field->bytes,out,error);
    qa_native_address base=entity;
    if(field->record==DECISION_CLIENT) {
        if(!pointer_read(p,entity+p->client_pointer,&base,error)) return false;
        if(!base) return decision_fail(error,QA_ERROR_NOT_FOUND,"Native weapon decision lost its Source client record");
    }
    if(base>UINT64_MAX-field->offset) return decision_fail(error,QA_ERROR_FORMAT,"Native weapon field address overflows");
    *out=base+field->offset; return true;
}
static bool decision_scalar(struct application_native_q2_attack *p,qa_native_address entity,
    const decision_field *field,double *out,qa_error *error)
{
    size_t count=field->bytes; uint8_t bytes[8]; qa_native_address address;
    if(!decision_field_address(p,entity,field,&address,error)||!qa_native_read(instance(p),address,bytes,count,error)) return false;
    if(field->encoding==QA_NATIVE_F32) *out=qa_load_f32le(bytes);
    else if(field->encoding==QA_NATIVE_F64) { uint64_t bits=qa_load_u64le(bytes); memcpy(out,&bits,sizeof(bits)); }
    else {
        uint64_t bits=count==1?bytes[0]:count==2?qa_load_u16le(bytes):count==4?qa_load_u32le(bytes):qa_load_u64le(bytes);
        bool signed_value=field->encoding==QA_NATIVE_I8||field->encoding==QA_NATIVE_I16||
            field->encoding==QA_NATIVE_I32||field->encoding==QA_NATIVE_I64;
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
    const decision_test_descriptor *test,bool *out,qa_error *error)
{
    if(test->pointer) {
        qa_native_address address,actual,expected=0;
        if(!decision_field_address(p,entity,&test->field,&address,error)||!pointer_read(p,address,&actual,error)) return false;
        if(test->has_pointer_value) {
            if(!qa_native_rva(instance(p),test->rva,1,&expected,error)) return false;
            for(size_t i=0;i<test->indirection_count;++i) {
                uint32_t offset=test->indirections[i];
                if(expected>UINT64_MAX-offset) return decision_fail(error,QA_ERROR_FORMAT,"Native weapon pointer indirection overflows");
                if(!pointer_read(p,expected+offset,&expected,error)) return false;
            }
        }
        *out=actual==expected; return true;
    }
    double actual;
    if(!decision_scalar(p,entity,&test->field,&actual,error)) return false;
    if(test->masked) {
        if(actual<INT32_MIN || actual>UINT32_MAX || trunc(actual)!=actual)
            return decision_fail(error,QA_ERROR_FORMAT,"Native weapon commitment mask exceeds its Source word");
        actual=(double)((uint32_t)(int64_t)actual&test->mask);
    }
    *out=test->at_most?actual<=test->expected:actual==test->expected; return true;
}
static bool decision_matches(struct application_native_q2_attack *p, qa_native_address entity,
    const decision_group *group, bool *out, qa_error *error)
{
    *out=true;
    for(size_t j=0;j<group->count&&*out;++j)
        if(!decision_test(p,entity,group->tests+j,out,error)) return false;
    return true;
}
static bool decision_any(struct application_native_q2_attack *p, qa_native_address entity,
    const decision_group *groups, size_t count, bool *out, qa_error *error)
{
    *out=false;
    for(size_t i=0;i<count;++i) {
        bool matches;
        if (!decision_matches(p,entity,groups+i,&matches,error)) return false;
        if(matches) { *out=true; return true; }
    }
    return true;
}
static bool decision_committed(struct application_native_q2_attack *p,qa_native_address entity,bool *out,qa_error *error)
{ return decision_any(p,entity,p->committed,p->committed_count,out,error); }
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
    qa_unified_frame_lease *storage=projection->storage;
    p->projections=projection->next; qa_unified_frame_lease_release(storage); return true;
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
    qa_unified_frame_lease *storage=application_control_storage_acquire(p->engine->provider->application,error);
    if(!storage) return false;
    decision_projection *projection=qa_unified_frame_lease_alloc(storage,1,sizeof(*projection),
        _Alignof(decision_projection),error);
    size_t count=region->field_count;
    if(projection) projection->fields=qa_unified_frame_lease_alloc(storage,count,sizeof(*projection->fields),
        _Alignof(decision_saved),error);
    if(!projection||!projection->fields) { qa_unified_frame_lease_release(storage); return false; }
    projection->storage=storage;
    projection->region=region; projection->actor=frame->actor; projection->entity=frame->entity;
    projection->next=p->projections; p->projections=projection;
    for(size_t i=0;i<count;++i) {
        const decision_input *input=region->fields+i;
        decision_saved *saved=projection->fields+i; uint8_t bytes[4]={0};
        saved->bytes=input->field.bytes; saved->mask=input->mask;
        if(!decision_field_address(p,frame->entity,&input->field,&saved->address,error) ||
            !qa_native_read(native,saved->address,bytes,saved->bytes,error)) return false;
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
    if(ok&&stage&&qa_actor_id_equal(stage->actor,actor)&&
        application_provider_for(app,actor,QA_ROLE_MOVEMENT,NULL)==p->engine->provider&&
        application_provider_for(app,actor,QA_ROLE_ARSENAL,NULL)!=p->engine->provider) {
        if(!stage->current(stage->context,actor)||!stage->arsenal||!p->engine->input_command)
            return decision_fail(error,QA_ERROR_NOT_FOUND,"Native weapon decision lost its actual selected arsenal stage");
        bool previous=p->engine->input_arsenal;
        bool committed=p->engine->input_arsenal_committed;
        bool reached=p->engine->input_arsenal_reached;
        p->engine->input_arsenal=true;
        p->engine->input_arsenal_committed=frame.committed;
        p->engine->input_arsenal_reached=frame.reached;
        ok=stage->arsenal(stage->context,actor,p->engine->input_command,stage->time_ns,error);
        p->engine->input_arsenal=previous;
        p->engine->input_arsenal_committed=committed;
        p->engine->input_arsenal_reached=reached;
    }
    return ok;
}
#endif
