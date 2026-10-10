#include "guest_q3_mod_items_private.h"
bool q3items_address(item_actor *a,item_field f,uint32_t *out,qa_error *e)
{
    application_q3_mod *m=a->owner->mod;application_q3_mod_profile *p=m->profile;
    if(f.record>=p->record_count)return false;
    return q3mod_address(m,a->actor,p->records[f.record].id,f.offset,4,out,e);
}
static bool scalar_at(item_actor *a,uint32_t address,const qa_qvm_committed_write *previous,int32_t *out,qa_error *e)
{
    uint8_t bytes[4];if(!qa_qvm_read(a->owner->mod->vm,address,bytes,4,e))return false;
    if(previous)for(size_t i=0;i<previous->count;++i){const qa_qvm_committed_range *r=previous->ranges+i;
        uint64_t start=address>r->offset?address:r->offset,end=(uint64_t)address+4,limit=(uint64_t)r->offset+r->before.size;
        if(end>limit)end=limit;
        for(uint64_t at=start;at<end;++at)bytes[(size_t)(at-address)]=r->before.data[(size_t)(at-r->offset)];
    }
    *out=qa_load_i32le(bytes);return true;
}
bool q3items_scalar(item_actor *a,item_field f,const qa_qvm_committed_write *previous,int32_t *out,qa_error *e)
{uint32_t address;return q3items_address(a,f,&address,e)&&scalar_at(a,address,previous,out,e);}
bool q3items_capacity(item_actor *a,const item_capacity *c,const qa_qvm_committed_write *previous,int32_t *out,qa_error *e)
{
    if(c->kind==ITEM_CAPACITY_CONSTANT){*out=c->constant;return true;}
    if(c->kind==ITEM_CAPACITY_FIELD)return q3items_scalar(a,c->field,previous,out,e);
    uint32_t instruction=c->instruction;
    for(size_t i=0;i<c->count;++i){int32_t word;if(!scalar_at(a,c->overrides[i].address,previous,&word,e))return false;
        if((word==c->overrides[i].value)!=c->overrides[i].unequal){instruction=c->overrides[i].instruction;break;}}
    size_t count;const qa_qvm_instruction *code=qa_qvm_image_instructions(a->owner->profile->source->image,&count);
    if(instruction>=count||code[instruction].opcode!=QA_QVM_CONST||code[instruction].operand<0)return false;
    *out=code[instruction].operand;return true;
}
bool q3items_read(item_actor *a,const item_storage *s,const qa_qvm_committed_write *previous,
    qa_inventory_entry *out,size_t first,size_t count,qa_error *e)
{
    int32_t word;if(!q3items_scalar(a,s->field,previous,&word,e))return false;
    if(!s->bits){int32_t capacity;if(first||count!=1||!q3items_capacity(a,&s->capacity,previous,&capacity,e))return false;
        *out=(qa_inventory_entry){s->item,word,capacity,QA_COUNT_SOURCE_INT32};return true;}
    if(first>s->count||count>s->count-first)return false;
    uint32_t mask=s->private_mask;
    for(size_t i=0;i<s->count;++i)mask|=s->items[i].mask;
    if((uint32_t)word&~mask)return q3mod_fail(e,QA_ERROR_FORMAT,"Original QVM inventory contains undeclared bits");
    for(size_t i=0;i<count;++i)out[i]=(qa_inventory_entry){s->items[first+i].item,((uint32_t)word&s->items[first+i].mask)?1:0,1,QA_COUNT_STACK};
    return true;
}
bool q3items_tests(item_actor *a,const item_test *tests,size_t count,bool *out,qa_error *e)
{
    *out=false;for(size_t i=0;i<count;++i){int32_t word;if(!q3items_scalar(a,tests[i].field,NULL,&word,e))return false;
        if(tests[i].masked){uint32_t bits=(uint32_t)word&tests[i].mask;memcpy(&word,&bits,4);}
        if(tests[i].at_most?word>tests[i].value:word!=tests[i].value)return true;
    }*out=true;return true;
}
bool q3items_active(item_actor *a,qa_item_id *out,qa_error *e)
{
    item_stage *s=a->owner->profile->stage;int32_t word;if(!s||!q3items_scalar(a,s->selection,NULL,&word,e))return false;
    if(!word){*out=0;return true;}for(size_t i=0;i<s->value_count;++i)if(s->values[i].value==word){*out=s->values[i].item;return true;}
    return q3mod_fail(e,QA_ERROR_FORMAT,"Original QVM selected an undeclared source weapon");
}
