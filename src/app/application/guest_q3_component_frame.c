#include "guest_q3_component_private.h"

static bool word(const qa_json_document *d,qa_json_id id,uint32_t *out,qa_error *e)
{ uint64_t n; if(!qa_json_u64(d,id,&n,e)) return false; if(n>UINT32_MAX) return q3records_fail(e,QA_ERROR_FORMAT,"Component frame address exceeds source word"); *out=(uint32_t)n; return true; }
bool q3component_bootstrap_profile(application_q3_component *c,qa_error *e)
{
    qa_json_document *d=NULL;
    if(!qa_json_parse(application_q3_mod_declaration(c->profile),&d,e)) return false;
    qa_json_id stores=qa_json_get(d,qa_json_get(d,qa_json_root(d),"sourceActors"),"initialStores");
    c->initial_store_count=qa_json_size(d,stores);
    c->initial_stores=c->initial_store_count?calloc(c->initial_store_count,sizeof(*c->initial_stores)):NULL;
    bool ok=!c->initial_store_count||c->initial_stores;
    if(!ok) q3records_fail(e,QA_ERROR_MEMORY,"Retaining original component bootstrap stores");
    size_t count; const qa_qvm_instruction *code=qa_qvm_image_instructions(c->options.image,&count);
    qa_bytes artifact=qa_resource_bytes(c->options.program),initialized=qa_qvm_image_initialized_data(c->options.image);
    uint32_t data_length=artifact.size>=32?qa_load_u32le(artifact.data+20):0;
    for(size_t i=0;ok&&i<c->initial_store_count;++i) {
        uint32_t pc;
        ok=word(d,qa_json_at(d,stores,i),&pc,e)&&pc>=2&&pc<count&&code[pc].opcode==QA_QVM_STORE4&&
            code[pc-2].opcode==QA_QVM_CONST&&code[pc-1].opcode==QA_QVM_CONST&&code[pc-2].operand>=0;
        uint32_t address=ok?(uint32_t)code[pc-2].operand:0;
        if(ok) ok=!(address&3)&&qa_qvm_qualify_source_span(c->options.image,address,4,e)&&
            (address>=initialized.size||(uint64_t)address+4<=data_length);
        for(size_t j=0;ok&&j<i;++j) ok=c->initial_stores[j].address!=address;
        for(size_t j=0;ok&&j<c->records->record_count;++j) {
            component_record *record=c->records->records+j;
            ok=(uint64_t)address+4<=record->address||(uint64_t)address>=(uint64_t)record->address+(uint64_t)record->stride*record->capacity;
        }
        if(ok) c->initial_stores[i]=(component_initial_store){address,code[pc-1].operand};
        else if(e&&e->code==QA_OK) q3records_fail(e,QA_ERROR_FORMAT,"Component bootstrap differs from original constant stores or overlaps projected/literal storage");
    }
    qa_json_destroy(d); return ok;
}
bool q3component_frame_profile(application_q3_component *c,qa_error *e)
{
    qa_json_document *d=NULL; if(!qa_json_parse(application_q3_mod_declaration(c->profile),&d,e)) return false;
    qa_json_id source=qa_json_get(d,qa_json_root(d),"sourceActors"),frame=qa_json_get(d,source,"frame"); bool ok=true;
    c->has_actor_frame=frame!=QA_JSON_NONE;
    if(c->has_actor_frame) {
        qa_json_id call=qa_json_get(d,frame,"call"),end=qa_json_get(d,frame,"end"),clock=qa_json_get(d,frame,"clock"),owned=qa_json_get(d,frame,"owned");
        uint32_t clock_address,clock_store,clock_argument;
        ok=word(d,qa_json_get(d,call,"entry"),&c->frame_entry,e)&&word(d,qa_json_get(d,end,"instruction"),&c->frame_end,e)&&
            qa_json_bool(d,qa_json_get(d,end,"completedTaken"),&c->frame_taken,e)&&word(d,qa_json_get(d,clock,"address"),&clock_address,e)&&
            word(d,qa_json_get(d,clock,"store"),&clock_store,e)&&word(d,qa_json_get(d,clock,"argument"),&clock_argument,e);
        size_t count; const qa_qvm_instruction *code=qa_qvm_image_instructions(c->options.image,&count);
        size_t stop=ok&&c->frame_entry<count?(size_t)c->frame_entry+1:count;
        while(stop<count&&code[stop].opcode!=QA_QVM_ENTER) ++stop;
        c->frame_branch_count=qa_json_size(d,owned);
        if(ok) ok=c->frame_entry<count&&code[c->frame_entry].opcode==QA_QVM_ENTER&&qa_json_string_equal(d,qa_json_get(d,call,"returns"),"void")&&
            c->frame_branch_count&&c->frame_end>c->frame_entry&&c->frame_end<stop&&code[c->frame_end].opcode>=QA_QVM_EQ&&code[c->frame_end].opcode<=QA_QVM_GEF&&
            (uint64_t)clock_store>(uint64_t)c->frame_entry+3&&clock_store<stop&&clock_argument<62&&qa_qvm_qualify_global_word(c->options.image,clock_address,e);
        if(ok) {
            qa_json_id argument=qa_json_at(d,qa_json_get(d,call,"arguments"),clock_argument);
            ok=qa_json_string_equal(d,qa_json_get(d,argument,"kind"),"time")&&qa_json_string_equal(d,qa_json_get(d,argument,"input"),"time")&&
                qa_json_string_equal(d,qa_json_get(d,argument,"units"),"milliseconds")&&qa_json_string_equal(d,qa_json_get(d,argument,"encoding"),"int32")&&
                code[clock_store-3].opcode==QA_QVM_CONST&&(uint32_t)code[clock_store-3].operand==clock_address&&code[clock_store-2].opcode==QA_QVM_LOCAL&&
                code[clock_store-2].operand==code[c->frame_entry].operand+8+(int32_t)clock_argument*4&&code[clock_store-1].opcode==QA_QVM_LOAD4&&code[clock_store].opcode==QA_QVM_STORE4;
            qa_json_id globals=qa_json_get(d,call,"globals");
            for(size_t i=0;ok&&i<qa_json_size(d,globals);++i) { uint32_t at; ok=word(d,qa_json_get(d,qa_json_at(d,globals,i),"address"),&at,e)&&at!=clock_address; }
        }
        if(ok) {
            c->frame_branches=calloc(c->frame_branch_count,sizeof(*c->frame_branches)); c->frame_locals=calloc(c->frame_branch_count,sizeof(*c->frame_locals));
            if(!c->frame_branches||!c->frame_locals) ok=q3records_fail(e,QA_ERROR_MEMORY,"Retaining true original component entity loop");
        }
        for(size_t i=0;ok&&i<c->frame_branch_count;++i) {
            uint32_t branch,local; qa_json_id filter=qa_json_at(d,owned,i);
            ok=word(d,qa_json_get(d,filter,"instruction"),&branch,e)&&word(d,qa_json_get(d,filter,"localInstruction"),&local,e);
            if(ok) ok=(uint64_t)branch>(uint64_t)c->frame_entry+6&&branch<stop&&local==branch-6&&code[local].opcode==QA_QVM_LOCAL&&
                code[local].operand>=8&&code[local].operand<=code[c->frame_entry].operand-4&&code[local+1].opcode==QA_QVM_LOAD4&&
                code[local+2].opcode==QA_QVM_CONST&&(uint32_t)code[local+2].operand==c->inuse&&code[local+3].opcode==QA_QVM_ADD&&
                code[local+4].opcode==QA_QVM_LOAD4&&code[local+5].opcode==QA_QVM_CONST&&!code[local+5].operand&&code[branch].opcode==QA_QVM_NE&&branch!=c->frame_end;
            for(size_t j=0;ok&&j<i;++j) ok=c->frame_branches[j]!=branch;
            if(ok) { c->frame_branches[i]=branch; c->frame_locals[i]=(uint32_t)code[local].operand; }
        }
    }
    qa_json_destroy(d); return ok||q3records_fail(e,QA_ERROR_FORMAT,"Component actor frame differs from its original clock and entity predicates");
}
typedef struct frame_branch_scope {
    application_q3_component *owner;
    qa_qvm_call function;
    qa_qvm_source_frame locals;
} frame_branch_scope;
static bool branch(void *context,const qa_qvm_call *call,bool original,bool *taken,qa_error *e)
{
    frame_branch_scope *scope=context; application_q3_component *c=scope->owner;
    if(!c->actor_frame_active||!q3component_current(c,e)) return false;
    /* Branch callbacks identify the deciding instruction through their live
     * source program counter in the binding's separate context below. */
    (void)call; *taken=original; return true;
}
typedef struct frame_decision { frame_branch_scope scope; size_t index; bool end; } frame_decision;
static bool decide(void *context,const qa_qvm_call *call,bool original,bool *taken,qa_error *e)
{
    frame_decision *decision=context; frame_branch_scope *scope=&decision->scope; application_q3_component *c=scope->owner;
    if(!branch(scope,call,original,taken,e)) return false;
    if(decision->end) {
        if(original==c->frame_taken) { c->actor_frame_completed=true; return qa_qvm_cancel(&scope->function,e); }
        return true;
    }
    if(!original) return true;
    uint32_t at=scope->locals.start+c->frame_locals[decision->index]; uint8_t bytes[4];
    if(at>scope->locals.end-4||!qa_qvm_read(c->vm,at,bytes,4,e)) return false;
    uint32_t pointer=qa_load_u32le(bytes); component_record *record=c->records->records+c->entity_record; *taken=false;
    if(pointer<record->address||(uint64_t)pointer>=(uint64_t)record->address+(uint64_t)record->stride*record->capacity||(pointer-record->address)%record->stride) return true;
    uint32_t slot=(pointer-record->address)/record->stride;
    for(size_t i=0;i<c->records->actor_count;++i) { component_actor row=c->records->actors[i]; if(row.slot==slot&&row.owned&&!row.retired&&q3records_live(c->records,row.actor)) { *taken=true; break; } }
    return true;
}
/* Decisions live only through this genuine synchronous proceed. Branch
 * bindings borrow them and disappear with the original source function. */
bool q3component_frame_proceed(application_q3_component *c,const qa_qvm_call *call,int32_t *result,qa_error *e)
{
    if(!c->actor_frame_active) return qa_qvm_proceed(call,result,e);
    qa_qvm_source_frame locals;
    if(!qa_qvm_call_source_frame(call,c->options.image,&locals,e)) return false;
    size_t count=c->frame_branch_count+1;
    frame_decision *contexts=calloc(count,sizeof(*contexts)); qa_qvm_branch_binding *bindings=calloc(count,sizeof(*bindings));
    if(!contexts||!bindings) { free(contexts); free(bindings); return q3records_fail(e,QA_ERROR_MEMORY,"Entering original component frame branch scope"); }
    for(size_t i=0;i<count;++i) {
        contexts[i]=(frame_decision){.scope={c,*call,locals},.index=i,.end=i==count-1};
        bindings[i]=(qa_qvm_branch_binding){.instruction=i==count-1?c->frame_end:c->frame_branches[i],.decide=decide,.context=contexts+i};
    }
    bool ok=qa_qvm_bind_branches(call,bindings,count,e)&&qa_qvm_proceed(call,result,e);
    free(contexts); free(bindings); return ok;
}
