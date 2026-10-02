#include "guest_q3_mod_items_private.h"
static bool u32(const qa_json_document *d,qa_json_id id,uint32_t *out,qa_error *e)
{uint64_t n;if(!qa_json_u64(d,id,&n,e)||n>UINT32_MAX)return q3mod_fail(e,QA_ERROR_FORMAT,"Weapon declaration exceeds source word");*out=(uint32_t)n;return true;}
static bool i32(const qa_json_document *d,qa_json_id id,int32_t *out,qa_error *e)
{int64_t n;if(!qa_json_i64(d,id,&n,e)||n<INT32_MIN||n>INT32_MAX)return q3mod_fail(e,QA_ERROR_FORMAT,"Weapon test exceeds signed word");*out=(int32_t)n;return true;}
static bool array(const qa_json_document *d,qa_json_id id,size_t width,void **out,size_t *count,qa_error *e)
{if(qa_json_type(d,id)!=QA_JSON_ARRAY)return q3mod_fail(e,QA_ERROR_FORMAT,"Weapon stage requires an array");*count=qa_json_size(d,id);if(*count>SIZE_MAX/width)return false;*out=*count?calloc(*count,width):NULL;return !*count||*out||q3mod_fail(e,QA_ERROR_MEMORY,"Owning original weapon stage");}
static bool entry(application_q3_mod_items_profile *p,const qa_json_document *d,qa_json_id id,uint32_t *out,qa_error *e)
{size_t n;const qa_qvm_instruction *code=qa_qvm_image_instructions(p->source->image,&n);return u32(d,id,out,e)&&((*out<n&&code[*out].opcode==QA_QVM_ENTER)||q3mod_fail(e,QA_ERROR_FORMAT,"Weapon entry is not original OP_ENTER"));}
static bool pointer(application_q3_mod_items_profile *p,const qa_json_document *d,qa_json_id id,mod_pointer *out,qa_error *e)
{
    qa_json_id kind=qa_json_get(d,id,"kind"),links=qa_json_get(d,id,"indirections");out->argument=qa_json_string_equal(d,kind,"argument");
    if((!out->argument&&!qa_json_string_equal(d,kind,"global"))||!u32(d,qa_json_get(d,id,out->argument?"index":"address"),&out->root,e)||
        !u32(d,qa_json_get(d,id,"offset"),&out->offset,e)||out->offset%4||
        !array(d,links,sizeof(*out->indirections),(void **)&out->indirections,&out->count,e)||
        (out->argument?out->root>=62:out->root%4||!qa_qvm_qualify_global_word(p->source->image,out->root,e)))return false;
    for(size_t i=0;i<out->count;++i)if(!u32(d,qa_json_at(d,links,i),out->indirections+i,e)||out->indirections[i]%4)return false;
    return true;
}
static bool actor(application_q3_mod_items_profile *p,const qa_json_document *d,qa_json_id id,item_actor_pointer *out,qa_error *e)
{
    qa_buffer name={0};if(!qa_json_string(d,qa_json_get(d,id,"record"),&name,e))return false;
    if(memchr(name.data,0,name.size)){qa_buffer_free(&name);return q3mod_fail(e,QA_ERROR_FORMAT,"Weapon actor record contains NUL");}
    size_t i=0;while(i<p->source->record_count&&strcmp((char *)name.data,p->source->records[i].id))++i;qa_buffer_free(&name);
    if(i==p->source->record_count||!p->source->records[i].client)return q3mod_fail(e,QA_ERROR_FORMAT,"Weapon actor pointer lacks admitted client record");
    out->record=i;return pointer(p,d,qa_json_get(d,id,"pointer"),&out->pointer,e);
}
static bool tests(application_q3_mod_items_profile *p,const qa_json_document *d,qa_json_id rows,item_test **out,size_t *count,qa_error *e)
{
    if(!array(d,rows,sizeof(**out),(void **)out,count,e)||!*count)return q3mod_fail(e,QA_ERROR_FORMAT,"Weapon stage lacks source conditions");
    for(size_t i=0;i<*count;++i){item_test *v=*out+i;qa_json_id at=qa_json_at(d,rows,i),mask=qa_json_get(d,at,"mask"),cmp=qa_json_get(d,at,"comparison");
        v->masked=qa_json_type(d,mask)!=QA_JSON_NULL;v->at_most=qa_json_string_equal(d,cmp,"at-most");
        if((!v->at_most&&!qa_json_string_equal(d,cmp,"equals"))||!q3items_field_parse(p,d,qa_json_get(d,at,"field"),&v->field,false,false,e)||
            !i32(d,qa_json_get(d,at,"value"),&v->value,e)||(v->masked&&(!u32(d,mask,&v->mask,e)||v->mask>INT32_MAX)))return false;
    }return true;
}
static uint32_t end_of(const qa_qvm_instruction *code,size_t n,uint32_t entry_index)
{uint32_t end=entry_index+1;while(end<n&&code[end].opcode!=QA_QVM_ENTER)++end;return end;}
static bool predicates(application_q3_mod_items_profile *p,const qa_json_document *d,qa_json_id rows,
    uint32_t owner,item_predicate **out,size_t *count,qa_error *e)
{
    size_t n;const qa_qvm_instruction *code=qa_qvm_image_instructions(p->source->image,&n);uint32_t end=end_of(code,n,owner);
    if(!array(d,rows,sizeof(**out),(void **)out,count,e))return false;
    for(size_t i=0;i<*count;++i){item_predicate *v=*out+i;qa_json_id at=qa_json_at(d,rows,i);
        if(!u32(d,qa_json_get(d,at,"instruction"),&v->instruction,e)||v->instruction<=owner||v->instruction>=end||
            code[v->instruction].opcode<QA_QVM_EQ||code[v->instruction].opcode>QA_QVM_GEF||
            !qa_json_bool(d,qa_json_get(d,at,"unselected"),&v->unselected,e))return q3mod_fail(e,QA_ERROR_FORMAT,"Weapon predicate lacks distinct original conditional");
        for(size_t j=0;j<i;++j)if((*out)[j].instruction==v->instruction)return false;
    }return true;
}
bool q3items_stage_parse(application_q3_mod_items_profile *p,const qa_json_document *d,qa_json_id root,qa_strings *strings,qa_error *e)
{
    item_stage *s=calloc(1,sizeof(*s));if(!s)return q3mod_fail(e,QA_ERROR_MEMORY,"Owning original weapon stage");p->stage=s;
    qa_json_id input=qa_json_get(d,root,"input"),stage=qa_json_get(d,root,"stage"),dispatch=qa_json_get(d,stage,"dispatcher"),
        selection=qa_json_get(d,stage,"selection"),request=qa_json_get(d,stage,"request"),continuation=qa_json_get(d,stage,"continuation"),projection=qa_json_get(d,continuation,"projection");
    if(!entry(p,d,qa_json_get(d,input,"entry"),&s->input_entry,e)||!q3items_field_parse(p,d,qa_json_get(d,input,"clock"),&s->clock,false,false,e)||
        !entry(p,d,qa_json_get(d,dispatch,"entry"),&s->dispatch_entry,e)||!actor(p,d,qa_json_get(d,dispatch,"actor"),&s->dispatcher,e)||
        !entry(p,d,qa_json_get(d,request,"entry"),&s->request_entry,e)||!u32(d,qa_json_get(d,request,"argument"),&s->request_argument,e)||s->request_argument>=62||
        !tests(p,d,qa_json_get(d,stage,"settled"),&s->settled,&s->settled_count,e)||
        !tests(p,d,qa_json_get(d,request,"accepted"),&s->accepted,&s->accepted_count,e)||
        !q3items_field_parse(p,d,qa_json_get(d,selection,"field"),&s->selection,false,false,e)||
        !predicates(p,d,qa_json_get(d,stage,"predicates"),s->dispatch_entry,&s->predicates,&s->predicate_count,e)||!s->predicate_count||
        !entry(p,d,qa_json_get(d,continuation,"entry"),&s->continue_entry,e)||!actor(p,d,qa_json_get(d,continuation,"actor"),&s->continuation,e)||
        !pointer(p,d,qa_json_get(d,projection,"movement"),&s->movement,e)||!u32(d,qa_json_get(d,projection,"byteLength"),&s->movement_length,e)||
        !u32(d,qa_json_get(d,projection,"minimum"),&s->minimum,e)||!u32(d,qa_json_get(d,projection,"maximum"),&s->maximum,e)||
        s->movement_length<12||s->movement_length%4||s->minimum%4||s->maximum%4||s->minimum>s->movement_length-12||s->maximum>s->movement_length-12||
        (s->minimum<s->maximum?s->maximum-s->minimum:s->minimum-s->maximum)<12||
        !q3items_field_parse(p,d,qa_json_get(d,projection,"viewHeight"),&s->view_height,false,false,e)||
        !q3items_field_parse(p,d,qa_json_get(d,projection,"ground"),&s->ground,false,false,e)||
        !tests(p,d,qa_json_get(d,continuation,"when"),&s->when,&s->when_count,e)||
        !predicates(p,d,qa_json_get(d,continuation,"predicates"),s->continue_entry,&s->continue_predicates,&s->continue_predicate_count,e)||
        !u32(d,qa_json_get(d,continuation,"instruction"),&s->continue_branch,e)||!qa_json_bool(d,qa_json_get(d,continuation,"originalTaken"),&s->original_taken,e))return false;
    size_t matched=0;for(size_t i=0;i<p->source->input_count;++i)for(size_t j=0;j<p->source->inputs[i].call_count;++j)
        if(p->source->inputs[i].calls[j].entry==s->input_entry){if(!p->source->inputs[i].slice||p->source->inputs[i].before)return false;++matched;}
    if(matched!=1)return q3mod_fail(e,QA_ERROR_FORMAT,"Weapon input requires exactly one movement-slice after callback");
    size_t n;const qa_qvm_instruction *code=qa_qvm_image_instructions(p->source->image,&n);uint32_t end=end_of(code,n,s->continue_entry);
    if(s->continue_branch<=s->continue_entry||s->continue_branch>=end||code[s->continue_branch].opcode<QA_QVM_EQ||
        code[s->continue_branch].opcode>QA_QVM_GEF||code[s->continue_branch].operand_width!=4)return false;
    for(size_t i=0;i<s->continue_predicate_count;++i)if(s->continue_predicates[i].instruction==s->continue_branch)return false;
    uint32_t pc=s->original_taken?s->continue_branch+1:(uint32_t)code[s->continue_branch].operand;
    bool *seen=calloc(n,sizeof(*seen));if(!seen)return false;bool returned=false;
    while(pc>s->continue_entry&&pc<end&&!seen[pc]){seen[pc]=true;
        if(code[pc].opcode==QA_QVM_LEAVE){returned=true;break;}
        if(code[pc].opcode==QA_QVM_PUSH){++pc;continue;}
        if(code[pc].opcode==QA_QVM_CONST&&pc+1<end&&code[pc+1].opcode==QA_QVM_JUMP){pc=(uint32_t)code[pc].operand;continue;}break;
    }free(seen);if(!returned)return q3mod_fail(e,QA_ERROR_FORMAT,"Weapon continuation original return edge has side effects");
    qa_json_id values=qa_json_get(d,selection,"values");if(!array(d,values,sizeof(*s->values),(void **)&s->values,&s->value_count,e))return false;
    size_t weapon_count=0;for(size_t i=0;i<p->definition_count;++i)weapon_count+=p->definitions[i].admission.definition.weapon?1u:0u;
    if(s->value_count!=weapon_count)return false;
    for(size_t i=0;i<s->value_count;++i){qa_json_id at=qa_json_at(d,values,i);qa_buffer item={0};
        if(!i32(d,qa_json_get(d,at,"value"),&s->values[i].value,e)||s->values[i].value<1||!qa_json_string(d,qa_json_get(d,at,"item"),&item,e))return false;
        bool ok=!memchr(item.data,0,item.size)&&qa_strings_intern(strings,(qa_bytes){item.data,item.size},&s->values[i].item,e);qa_buffer_free(&item);
        bool declared=false;for(size_t j=0;j<p->definition_count;++j)declared|=p->definitions[j].admission.definition.weapon&&p->definitions[j].admission.definition.item==s->values[i].item;
        if(!ok||!declared)return false;for(size_t j=0;j<i;++j)if(s->values[j].item==s->values[i].item||s->values[j].value==s->values[i].value)return false;
    }
    qa_json_id calls=qa_json_get(d,continuation,"calls");if(!array(d,calls,sizeof(*s->calls),(void **)&s->calls,&s->call_count,e))return false;
    uint32_t previous=s->continue_branch;bool dispatcher=false;
    for(size_t i=0;i<s->call_count;++i){item_continued_call *v=s->calls+i;qa_json_id at=qa_json_at(d,calls,i);
        if(!u32(d,qa_json_get(d,at,"instruction"),&v->instruction,e)||v->instruction<=previous||v->instruction>=end||
            code[v->instruction].opcode!=QA_QVM_CALL||code[v->instruction-1].opcode!=QA_QVM_CONST||
            !q3items_call_parse(p,d,qa_json_get(d,at,"call"),&v->call,e)||v->call->argument_count||v->call->global_count||v->call->returns!=MOD_VOID||
            code[v->instruction-1].operand!=(int32_t)v->call->entry)return false;
        dispatcher|=v->call->entry==s->dispatch_entry;previous=v->instruction;
    }
    return dispatcher||q3mod_fail(e,QA_ERROR_FORMAT,"Weapon continuation omits its actual dispatcher");
}
