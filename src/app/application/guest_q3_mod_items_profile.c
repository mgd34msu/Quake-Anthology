#include "guest_q3_mod_items_private.h"
#include "guest_mod_item_definition.h"

static bool word(const qa_json_document *d,qa_json_id id,uint32_t *out,qa_error *e)
{
    uint64_t n;
    if(!qa_json_u64(d,id,&n,e)||n>UINT32_MAX) {
        q3mod_fail(e,QA_ERROR_FORMAT,"Item address exceeds its source word");
        return false;
    }
    *out=(uint32_t)n; return true;
}
static bool integer(const qa_json_document *d,qa_json_id id,int32_t *out,qa_error *e)
{ int64_t n; if(!qa_json_i64(d,id,&n,e)||n<INT32_MIN||n>INT32_MAX) return q3mod_fail(e,QA_ERROR_FORMAT,"Item value exceeds signed source storage"); *out=(int32_t)n; return true; }
static bool list(const qa_json_document *d,qa_json_id id,size_t stride,void **out,size_t *count,qa_error *e)
{
    if(qa_json_type(d,id)!=QA_JSON_ARRAY) return q3mod_fail(e,QA_ERROR_FORMAT,"Item declaration requires an array");
    *count=qa_json_size(d,id);
    if(*count>SIZE_MAX/stride) return q3mod_fail(e,QA_ERROR_MEMORY,"Item declaration size overflows");
    *out=*count?calloc(*count,stride):NULL;
    return !*count||*out||q3mod_fail(e,QA_ERROR_MEMORY,"Owning item declaration rows");
}
bool q3items_call_parse(application_q3_mod_items_profile *p,const qa_json_document *d,qa_json_id id,
    application_q3_mod_call **out,qa_error *e)
{ return application_q3_mod_call_create(p->source,qa_json_source(d,id),
    (UINT32_C(1)<<Q3_MOD_SELF)|(UINT32_C(1)<<Q3_MOD_TIME),out,e); }
bool q3items_field_parse(application_q3_mod_items_profile *p,const qa_json_document *d,qa_json_id id,
    item_field *out,bool storage,bool capacity,qa_error *e)
{
    (void)storage; (void)capacity;
    char *name=NULL; uint32_t offset;
    if(!application_mod_item_text(d,qa_json_get(d,id,"record"),&name,e)||!word(d,qa_json_get(d,id,"offset"),&offset,e)) {free(name);return false;}
    size_t index=0; while(index<p->source->record_count&&strcmp(name,p->source->records[index].id)) ++index;
    free(name);
    if(index==p->source->record_count||!p->source->records[index].client||offset%4||
        p->source->records[index].stride<4||offset>p->source->records[index].stride-4)
        return q3mod_fail(e,QA_ERROR_FORMAT,"Item field leaves admitted client storage");
    const mod_record *r=p->source->records+index;
    for(size_t i=0;i<r->field_count;++i) if(!r->fields[i].private_field&&
        offset<(uint64_t)r->fields[i].offset+r->fields[i].length&&r->fields[i].offset<(uint64_t)offset+4)
        return q3mod_fail(e,QA_ERROR_FORMAT,"Item storage overlaps a canonical source projection");
    *out=(item_field){index,offset}; return true;
}
static bool source_constant(application_q3_mod_items_profile *p,uint32_t instruction,qa_error *e)
{
    size_t count; const qa_qvm_instruction *code=qa_qvm_image_instructions(p->source->image,&count);
    return (instruction<count&&code[instruction].opcode==QA_QVM_CONST&&code[instruction].operand>=0)||
        q3mod_fail(e,QA_ERROR_FORMAT,"Item capacity lacks its original nonnegative constant");
}
static bool capacity_parse(application_q3_mod_items_profile *p,const qa_json_document *d,qa_json_id id,item_capacity *c,qa_error *e)
{
    qa_json_id kind=qa_json_get(d,id,"kind");
    if(qa_json_string_equal(d,kind,"constant")) {
        c->kind=ITEM_CAPACITY_CONSTANT;
        return integer(d,qa_json_get(d,id,"value"),&c->constant,e)&&(c->constant>=0||q3mod_fail(e,QA_ERROR_FORMAT,"Negative item capacity"));
    }
    if(qa_json_string_equal(d,kind,"field")) { c->kind=ITEM_CAPACITY_FIELD; return q3items_field_parse(p,d,qa_json_get(d,id,"field"),&c->field,false,true,e); }
    if(!qa_json_string_equal(d,kind,"source")) return q3mod_fail(e,QA_ERROR_FORMAT,"Unknown item capacity source");
    c->kind=ITEM_CAPACITY_SOURCE; qa_json_id rows=qa_json_get(d,id,"overrides");
    if(!word(d,qa_json_get(d,id,"instruction"),&c->instruction,e)||!source_constant(p,c->instruction,e)||
        !list(d,rows,sizeof(*c->overrides),(void **)&c->overrides,&c->count,e)) return false;
    for(size_t i=0;i<c->count;++i) {
        item_override *v=c->overrides+i; qa_json_id at=qa_json_at(d,rows,i),comparison=qa_json_get(d,at,"comparison");
        v->unequal=qa_json_string_equal(d,comparison,"not-equals");
        if((!v->unequal&&!qa_json_string_equal(d,comparison,"equals"))||
            !word(d,qa_json_get(d,at,"address"),&v->address,e)||v->address%4||
            !qa_qvm_qualify_global_word(p->source->image,v->address,e)||
            !integer(d,qa_json_get(d,at,"value"),&v->value,e)||
            !word(d,qa_json_get(d,at,"instruction"),&v->instruction,e)||!source_constant(p,v->instruction,e)) return false;
    }
    return true;
}
static bool same_field(item_field a,item_field b) { return a.record==b.record&&a.offset==b.offset; }
static bool parse(application_q3_mod_items_profile *p,const qa_json_document *d,qa_json_id root,qa_strings *strings,qa_error *e)
{
    qa_json_id definitions=qa_json_get(d,root,"definitions"),storage=qa_json_get(d,root,"storage");
    if(!p->source->clients||!list(d,definitions,sizeof(*p->definitions),(void **)&p->definitions,&p->definition_count,e)||
        !p->definition_count||!list(d,storage,sizeof(*p->storage),(void **)&p->storage,&p->storage_count,e)) return false;
    size_t weapon_count=0;
    for(size_t i=0;i<p->definition_count;++i) {
        item_definition *v=p->definitions+i; qa_item_definition *definition=&v->admission.definition;
        qa_json_id at=qa_json_at(d,definitions,i),actions[2];
        if(!application_mod_item_definition(d,at,strings,&v->admission,&v->icon,&v->held,actions,e)) return false;
        if(definition->weapon) ++weapon_count;
        for(size_t j=0;j<i;++j) if(p->definitions[j].admission.definition.item==definition->item)
            return q3mod_fail(e,QA_ERROR_FORMAT,"Duplicate source item definition");
        for(size_t j=0;j<2;++j) if(actions[j]!=QA_JSON_NONE&&
            !q3items_call_parse(p,d,actions[j],&v->actions[j],e)) return false;
    }
    qa_item_id *bound=calloc(p->definition_count,sizeof(*bound)); size_t bound_count=0;
    if(!bound) return q3mod_fail(e,QA_ERROR_MEMORY,"Qualifying item storage identities");
    bool ok=true;
    for(size_t i=0;ok&&i<p->storage_count;++i) {
        item_storage *v=p->storage+i; qa_json_id at=qa_json_at(d,storage,i),kind=qa_json_get(d,at,"kind");
        v->bits=qa_json_string_equal(d,kind,"bits");
        ok=(v->bits||qa_json_string_equal(d,kind,"counter"))&&q3items_field_parse(p,d,qa_json_get(d,at,"field"),&v->field,true,false,e);
        if(!ok) break;
        if(v->bits) {
            qa_json_id bits=qa_json_get(d,at,"items");
            ok=word(d,qa_json_get(d,at,"privateMask"),&v->private_mask,e)&&list(d,bits,sizeof(*v->items),(void **)&v->items,&v->count,e)&&v->count;
            uint32_t mask=v->private_mask;
            for(size_t j=0;ok&&j<v->count;++j) {
                qa_json_id bit=qa_json_at(d,bits,j); item_bit *b=v->items+j;
                ok=application_mod_item_identity(d,qa_json_get(d,bit,"item"),strings,&b->item,e)&&word(d,qa_json_get(d,bit,"mask"),&b->mask,e)&&
                    b->mask&&b->mask<=UINT32_C(0x80000000)&&!(b->mask&(b->mask-1))&&!(mask&b->mask);
                mask|=b->mask;
            }
        } else ok=application_mod_item_identity(d,qa_json_get(d,at,"item"),strings,&v->item,e)&&capacity_parse(p,d,qa_json_get(d,at,"capacity"),&v->capacity,e);
        for(size_t j=0;ok&&j<i;++j) {
            const item_storage *prior=p->storage+j;
            if(same_field(prior->field,v->field)||(!prior->bits&&prior->capacity.kind==ITEM_CAPACITY_FIELD&&same_field(prior->capacity.field,v->field))||
                (!v->bits&&v->capacity.kind==ITEM_CAPACITY_FIELD&&same_field(v->capacity.field,prior->field))) ok=false;
        }
        if(!v->bits&&v->capacity.kind==ITEM_CAPACITY_FIELD&&same_field(v->field,v->capacity.field)) ok=false;
        for(size_t j=0;ok&&j<(v->bits?v->count:1);++j) {
            qa_item_id item=v->bits?v->items[j].item:v->item; bool found=false;
            for(size_t k=0;k<p->definition_count;++k) found|=p->definitions[k].admission.definition.item==item;
            for(size_t k=0;k<bound_count;++k) if(bound[k]==item) found=false;
            if(!found||bound_count==p->definition_count) ok=false; else bound[bound_count++]=item;
        }
    }
    ok=ok&&bound_count==p->definition_count;
    for(size_t i=0;ok&&i<p->definition_count;++i) {
        qa_item_id ammo=p->definitions[i].admission.definition.ammo;
        if(ammo) { bool found=false; for(size_t j=0;j<bound_count;++j) found|=bound[j]==ammo; ok=found; }
    }
    free(bound);
    if(!ok) return q3mod_fail(e,QA_ERROR_FORMAT,"Source items lack distinct complete storage");
    qa_json_id weapons=qa_json_get(d,root,"weapons");
    if(!!weapon_count!=(weapons!=QA_JSON_NONE)) return q3mod_fail(e,QA_ERROR_FORMAT,"Weapon items require their actual source stage");
    return weapons==QA_JSON_NONE||q3items_stage_parse(p,d,weapons,strings,e);
}
bool application_q3_mod_items_profile_create(application_q3_mod_profile *source,qa_strings *strings,
    application_q3_mod_items_profile **out,qa_error *e)
{
    if(!source||!strings||!out||*out) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Items require the actual retained generic profile");
    qa_json_document *d=NULL;
    if(!qa_json_parse(application_q3_mod_declaration(source),&d,e)) return false;
    qa_json_id items=qa_json_get(d,qa_json_root(d),"items");
    if(items==QA_JSON_NONE) {qa_json_destroy(d);return true;}
    application_q3_mod_items_profile *p=calloc(1,sizeof(*p));
    if(!p) {qa_json_destroy(d);return q3mod_fail(e,QA_ERROR_MEMORY,"Owning generic source item profile");}
    p->source=source; bool ok=parse(p,d,items,strings,e); qa_json_destroy(d);
    if(!ok) {application_q3_mod_items_profile_destroy(p);return false;}
    *out=p; return true;
}
void application_q3_mod_items_profile_destroy(application_q3_mod_items_profile *p)
{
    if(!p) return;
    for(size_t i=0;p->definitions&&i<p->definition_count;++i) { item_definition *d=p->definitions+i;
        free((void *)d->admission.definition.label); for(size_t j=0;j<2;++j) application_q3_mod_call_destroy(d->actions[j]);
        qa_buffer_free(&d->icon); qa_buffer_free(&d->held);
    }
    for(size_t i=0;p->storage&&i<p->storage_count;++i) {free(p->storage[i].items);free(p->storage[i].capacity.overrides);}
    if(p->stage) { item_stage *s=p->stage;
        free(s->dispatcher.pointer.indirections);free(s->continuation.pointer.indirections);free(s->movement.indirections);
        free(s->values);free(s->settled);free(s->accepted);free(s->when);free(s->predicates);free(s->continue_predicates);
        for(size_t i=0;s->calls&&i<s->call_count;++i) application_q3_mod_call_destroy(s->calls[i].call);
        free(s->calls);free(s);
    }
    free(p->definitions);free(p->storage);free(p);
}
