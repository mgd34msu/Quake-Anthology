#include "guest_q3_mod_private.h"
#include "guest_mod_item_definition.h"
#include "qa/vfs.h"

static const char *const input_names[Q3_MOD_VALUE_COUNT] = {
    "view-angles", "attack", "jump", "impulse", "forward-move", "side-move", "up-move",
    "self", "other", "activator", "attacker", "inflictor", "amount", "damage-flags",
    "regular-protection-scale", "knockback", "point", "direction", "normal", "item",
    "time", "elapsed", "result", "pickup-count", "pickup-has-count", "pickup-dropped"
};
static bool number(const qa_json_document *d, qa_json_id id, double *v, qa_error *e)
{ return qa_json_number(d,id,v,e) && (isfinite(*v) || q3mod_fail(e,QA_ERROR_FORMAT,"Nonfinite source declaration value")); }
static bool word(const qa_json_document *d, qa_json_id id, uint32_t *v, qa_error *e)
{
    uint64_t n;
    if (!qa_json_u64(d,id,&n,e)) return false;
    if (n>UINT32_MAX) {
        q3mod_fail(e,QA_ERROR_FORMAT,"Source declaration exceeds its address word");
        return false;
    }
    *v=(uint32_t)n; return true;
}
static bool text(const qa_json_document *d, qa_json_id id, char **v, qa_error *e)
{
    qa_buffer b={0};
    if (!qa_json_string(d,id,&b,e)) return false;
    if (memchr(b.data,0,b.size)) { qa_buffer_free(&b); return q3mod_fail(e,QA_ERROR_FORMAT,"Source declaration contains embedded NUL"); }
    *v=(char *)b.data; return true;
}
static bool array(const qa_json_document *d, qa_json_id id, size_t element,
    void **out, size_t *count, bool optional, qa_error *e)
{
    if (optional && id==QA_JSON_NONE) { *out=NULL; *count=0; return true; }
    if (qa_json_type(d,id)!=QA_JSON_ARRAY) return q3mod_fail(e,QA_ERROR_FORMAT,"Source declaration requires an array");
    size_t n=qa_json_size(d,id);
    if (n>SIZE_MAX/element) return q3mod_fail(e,QA_ERROR_MEMORY,"Source declaration array overflows");
    void *p=n?calloc(n,element):NULL;
    if (n && !p) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning source declaration array");
    *out=p; *count=n; return true;
}
static bool intern(const qa_json_document *d, qa_json_id id, qa_strings *s, qa_string_id *v, bool nullable, qa_error *e)
{
    if (nullable && qa_json_type(d,id)==QA_JSON_NULL) { *v=0; return true; }
    char *p=NULL; if (!text(d,id,&p,e)) return false;
    bool ok=strchr(p,':') && p[0] && strchr(p,':')[1];
    if (ok) ok=qa_strings_intern_cstr(s,p,v,e);
    else q3mod_fail(e,QA_ERROR_FORMAT,"Source declaration identity has no namespace");
    free(p); return ok;
}
static bool encoding(const qa_json_document *d, qa_json_id id, bool allow_void, mod_scalar *v, qa_error *e)
{
    if (qa_json_string_equal(d,id,"int32")) *v=MOD_INT32;
    else if (qa_json_string_equal(d,id,"float32")) *v=MOD_FLOAT32;
    else if (allow_void && qa_json_string_equal(d,id,"void")) *v=MOD_VOID;
    else return q3mod_fail(e,QA_ERROR_FORMAT,"Unknown source scalar encoding");
    return true;
}
application_q3_mod_input application_q3_mod_input_find(const qa_json_document *d, qa_json_id id)
{
    for(size_t i=0;i<Q3_MOD_VALUE_COUNT;++i)
        if(qa_json_string_equal(d,id,input_names[i])) return (application_q3_mod_input)i;
    return Q3_MOD_VALUE_COUNT;
}
static bool input(const qa_json_document *d, qa_json_id id, application_q3_mod_input *v, qa_error *e)
{
    *v=application_q3_mod_input_find(d,id);
    return *v!=Q3_MOD_VALUE_COUNT||q3mod_fail(e,QA_ERROR_FORMAT,"Unknown source callback input");
}
static bool vector(const qa_json_document *d, qa_json_id id, qa_vec3 *v, qa_error *e)
{
    double x,y,z;
    if (!number(d,qa_json_get(d,id,"x"),&x,e) || !number(d,qa_json_get(d,id,"y"),&y,e) ||
        !number(d,qa_json_get(d,id,"z"),&z,e)) return false;
    *v=(qa_vec3){(float)x,(float)y,(float)z};
    return qa_vec_finite(*v) || q3mod_fail(e,QA_ERROR_FORMAT,"Source vector exceeds binary32 storage");
}
static bool entry(const qa_json_document *d, qa_json_id id, qa_qvm_image *image, uint32_t *v, qa_error *e)
{
    size_t n; const qa_qvm_instruction *code=qa_qvm_image_instructions(image,&n);
    return word(d,id,v,e) && ((*v<n && code[*v].opcode==QA_QVM_ENTER) ||
        q3mod_fail(e,QA_ERROR_FORMAT,"Source callback does not name OP_ENTER"));
}
static const mod_record *record(const application_q3_mod_profile *p, size_t index)
{ return index < p->record_count ? p->records + index : NULL; }
static bool record_index(const qa_json_document *d, qa_json_id node,
    const application_q3_mod_profile *p, size_t *out, qa_error *e)
{
    char *name = NULL;
    if (!text(d, node, &name, e)) return false;
    size_t index = 0;
    while (index < p->record_count && strcmp(p->records[index].id, name)) ++index;
    free(name);
    *out = index;
    return index < p->record_count ||
        q3mod_fail(e, QA_ERROR_FORMAT, "Source declaration names undeclared actor storage");
}
static bool argument(const qa_json_document *d, qa_json_id id, application_q3_mod_profile *p,
    mod_argument *v, qa_error *e)
{
    qa_json_id kind=qa_json_get(d,id,"kind");
    if (qa_json_string_equal(d,kind,"actor") || qa_json_string_equal(d,kind,"client")) {
        v->kind=qa_json_string_equal(d,kind,"actor")?MOD_ACTOR:MOD_CLIENT;
        if (!input(d,qa_json_get(d,id,"input"),&v->name,e) || v->name<Q3_MOD_SELF || v->name>Q3_MOD_INFLICTOR)
            return q3mod_fail(e,QA_ERROR_FORMAT,"Source actor argument requires an actor input");
        if (v->kind==MOD_ACTOR && (!record_index(d,qa_json_get(d,id,"record"),p,&v->record,e) || !record(p,v->record)))
            return q3mod_fail(e,QA_ERROR_FORMAT,"Source argument names undeclared actor storage");
        return true;
    }
    if (qa_json_string_equal(d,kind,"address")) {
        v->kind=MOD_ADDRESS;
        return word(d,qa_json_get(d,id,"value"),&v->address,e) &&
            (!v->address || qa_qvm_qualify_source_span(p->image,v->address,1,e));
    }
    if (qa_json_string_equal(d,kind,"time")) {
        v->kind=MOD_TIME;
        if (!input(d,qa_json_get(d,id,"input"),&v->name,e) || (v->name!=Q3_MOD_TIME && v->name!=Q3_MOD_ELAPSED) ||
            !encoding(d,qa_json_get(d,id,"encoding"),false,&v->encoding,e)) return false;
        qa_json_id units=qa_json_get(d,id,"units");
        v->milliseconds=qa_json_string_equal(d,units,"milliseconds");
        return v->milliseconds || qa_json_string_equal(d,units,"seconds") || q3mod_fail(e,QA_ERROR_FORMAT,"Unknown source time units");
    }
    if (qa_json_string_equal(d,kind,"vector")) v->kind=MOD_VECTOR;
    else if (qa_json_string_equal(d,kind,"string")) v->kind=MOD_STRING;
    else { v->kind=MOD_SCALAR; if (!encoding(d,kind,false,&v->encoding,e)) return false; }
    qa_json_id value=qa_json_get(d,id,"value"), source=qa_json_get(d,value,"kind");
    if (qa_json_string_equal(d,source,"input")) {
        v->input=true;
        if (!input(d,qa_json_get(d,value,"name"),&v->name,e)) return false;
        bool vec=v->name==Q3_MOD_VIEW_ANGLES || v->name==Q3_MOD_POINT || v->name==Q3_MOD_DIRECTION || v->name==Q3_MOD_NORMAL;
        if ((v->kind==MOD_VECTOR)!=vec || (v->kind==MOD_STRING)!=(v->name==Q3_MOD_ITEM) ||
            (v->name>=Q3_MOD_SELF && v->name<=Q3_MOD_INFLICTOR))
            return q3mod_fail(e,QA_ERROR_FORMAT,"Source value input has incompatible representation");
        return true;
    }
    qa_json_id raw=qa_json_get(d,value,"value");
    if (v->kind==MOD_VECTOR && qa_json_string_equal(d,source,"vector")) {
        v->literal.kind=Q3_MOD_VALUE_VECTOR; return vector(d,raw,&v->literal.as.vector,e);
    }
    if (v->kind==MOD_STRING && qa_json_string_equal(d,source,"string")) {
        v->literal.kind=Q3_MOD_VALUE_STRING; return text(d,raw,(char **)&v->literal.as.string,e);
    }
    if (v->kind==MOD_SCALAR && qa_json_string_equal(d,source,"float")) {
        int32_t bits; v->literal.kind=Q3_MOD_VALUE_SCALAR;
        return number(d,raw,&v->literal.as.scalar,e) && q3mod_scalar_word(v->literal.as.scalar,v->encoding,&bits,e);
    }
    return q3mod_fail(e,QA_ERROR_FORMAT,"Source callback literal has incompatible representation");
}
static bool call(const qa_json_document *d, qa_json_id id, application_q3_mod_profile *p, mod_call *v, qa_error *e)
{
    v->profile=p;
    qa_json_id args=qa_json_get(d,id,"arguments"), globals=qa_json_get(d,id,"globals");
    if (!entry(d,qa_json_get(d,id,"entry"),p->image,&v->entry,e) ||
        !encoding(d,qa_json_get(d,id,"returns"),true,&v->returns,e) ||
        !array(d,args,sizeof(*v->arguments),(void **)&v->arguments,&v->argument_count,false,e) ||
        !array(d,globals,sizeof(*v->globals),(void **)&v->globals,&v->global_count,false,e)) return false;
    if (v->argument_count>62) return q3mod_fail(e,QA_ERROR_FORMAT,"Source call exceeds OP_ARG capacity");
    for (size_t i=0;i<v->argument_count;++i) if (!argument(d,qa_json_at(d,args,i),p,v->arguments+i,e)) return false;
    for (size_t i=0;i<v->global_count;++i) {
        mod_global *g=v->globals+i; qa_json_id at=qa_json_at(d,globals,i);
        if (!word(d,qa_json_get(d,at,"address"),&g->address,e) || g->address%4 ||
            !argument(d,qa_json_get(d,at,"value"),p,&g->value,e)) return false;
        size_t length=g->value.kind==MOD_VECTOR?12:4;
        if (!qa_qvm_qualify_source_span(p->image,g->address,length,e)) return false;
        for (size_t j=0;j<i;++j) {
            size_t prior=v->globals[j].value.kind==MOD_VECTOR?12:4;
            if ((uint64_t)g->address<(uint64_t)v->globals[j].address+prior &&
                (uint64_t)v->globals[j].address<(uint64_t)g->address+length)
                return q3mod_fail(e,QA_ERROR_FORMAT,"Source callback globals overlap");
        }
    }
    return true;
}
static void argument_free(mod_argument *v)
{ if (v->literal.kind==Q3_MOD_VALUE_STRING) free((char *)v->literal.as.string); }
static void call_free(mod_call *v)
{
    for (size_t i=0;i<v->argument_count;++i) argument_free(v->arguments+i);
    for (size_t i=0;i<v->global_count;++i) argument_free(&v->globals[i].value);
    free(v->arguments); free(v->globals);
}
static bool available(const mod_call *call_value, uint32_t mask, bool clients, qa_error *e)
{
    for (size_t i=0;i<call_value->argument_count+call_value->global_count;++i) {
        const mod_argument *a=i<call_value->argument_count?call_value->arguments+i:
            &call_value->globals[i-call_value->argument_count].value;
        if (a->kind==MOD_CLIENT && !clients) return q3mod_fail(e,QA_ERROR_FORMAT,"Client argument has no declared source client records");
        if ((a->input || a->kind==MOD_ACTOR || a->kind==MOD_CLIENT || a->kind==MOD_TIME) &&
            !(mask&(UINT32_C(1)<<a->name))) return q3mod_fail(e,QA_ERROR_FORMAT,"Source callback input is unavailable at its operation boundary");
    }
    return true;
}
static bool pointer(const qa_json_document *d, qa_json_id id, application_q3_mod_profile *p, mod_pointer *v, qa_error *e)
{
    qa_json_id kind=qa_json_get(d,id,"kind"), links=qa_json_get(d,id,"indirections");
    v->argument=qa_json_string_equal(d,kind,"argument");
    if ((!v->argument && !qa_json_string_equal(d,kind,"global")) ||
        !word(d,qa_json_get(d,id,v->argument?"index":"address"),&v->root,e) ||
        !word(d,qa_json_get(d,id,"offset"),&v->offset,e) ||
        !array(d,links,sizeof(*v->indirections),(void **)&v->indirections,&v->count,false,e)) return false;
    if (v->argument ? v->root>=62 : !qa_qvm_qualify_source_span(p->image,v->root,4,e))
        return q3mod_fail(e,QA_ERROR_FORMAT,"Source input pointer root leaves its frame");
    for (size_t i=0;i<v->count;++i) if (!word(d,qa_json_at(d,links,i),v->indirections+i,e)) return false;
    return true;
}
static bool field(const qa_json_document *d, qa_json_id id, application_q3_mod_profile *p, mod_field *v, qa_error *e)
{
    if (!record_index(d,qa_json_get(d,id,"record"),p,&v->record,e) ||
        !word(d,qa_json_get(d,id,"offset"),&v->offset,e) ||
        !encoding(d,qa_json_get(d,id,"encoding"),false,&v->encoding,e)) return false;
    const mod_record *r=record(p,v->record);
    return (r && r->stride>=4 && v->offset%4==0 && v->offset<=r->stride-4) ||
        q3mod_fail(e,QA_ERROR_FORMAT,"Protection scalar leaves declared actor storage");
}
static bool outputs(const qa_json_document *d, qa_json_id list, application_q3_mod_profile *p, mod_input_binding *b, qa_error *e)
{
    if (!array(d,list,sizeof(*b->outputs),(void **)&b->outputs,&b->output_count,true,e)) return false;
    for (size_t i=0;i<b->output_count;++i) {
        mod_output *v=b->outputs+i; qa_json_id at=qa_json_at(d,list,i), kind=qa_json_get(d,at,"kind");
        if (qa_json_string_equal(d,kind,"field")) {
            v->kind=MOD_FIELD; qa_json_id value=qa_json_get(d,at,"value");
            if (!record_index(d,qa_json_get(d,at,"record"),p,&v->record,e) || !word(d,qa_json_get(d,at,"offset"),&v->offset,e) ||
                !input(d,qa_json_get(d,value,"input"),&v->input,e) || v->input>=Q3_MOD_INPUT_COUNT) return false;
            const mod_record *r=record(p,v->record); size_t bytes=v->input==Q3_MOD_VIEW_ANGLES?12:4;
            if (!r || r->stride<bytes || v->offset>r->stride-bytes) return q3mod_fail(e,QA_ERROR_FORMAT,"Input field leaves declared actor record");
            if (v->input!=Q3_MOD_VIEW_ANGLES && (!encoding(d,qa_json_get(d,value,"encoding"),false,&v->encoding,e) ||
                !number(d,qa_json_get(d,value,"scale"),&v->scale,e) || v->scale<=0)) return false;
            continue;
        }
        v->kind=qa_json_string_equal(d,kind,"command")?MOD_COMMAND:MOD_HANDLER;
        if (v->kind==MOD_HANDLER && !qa_json_string_equal(d,kind,"handler")) return q3mod_fail(e,QA_ERROR_FORMAT,"Unknown input output kind");
        qa_json_id actor=qa_json_get(d,at,"actor"), ins=qa_json_get(d,at,"inputs");
        if (!entry(d,qa_json_get(d,at,"entry"),p->image,&v->entry,e) ||
            !record_index(d,qa_json_get(d,actor,"record"),p,&v->record,e) || !record(p,v->record) ||
            !pointer(d,qa_json_get(d,actor,"pointer"),p,&v->actor,e) || qa_json_type(d,ins)!=QA_JSON_ARRAY) return false;
        if (!array(d,ins,sizeof(*v->ordered_inputs),(void **)&v->ordered_inputs,&v->input_count,false,e)) return false;
        for (size_t j=0;j<qa_json_size(d,ins);++j) {
            application_q3_mod_input n;
            if (!input(d,qa_json_at(d,ins,j),&n,e) || n>=Q3_MOD_INPUT_COUNT ||
                (v->kind==MOD_HANDLER && n==Q3_MOD_VIEW_ANGLES) || (v->kind==MOD_COMMAND && n==Q3_MOD_IMPULSE))
                return q3mod_fail(e,QA_ERROR_FORMAT,"Input output cannot represent its selected input");
            v->inputs|=1u<<n;
            v->ordered_inputs[j]=n;
        }
        if (v->kind==MOD_COMMAND) {
            if (!pointer(d,qa_json_get(d,at,"command"),p,&v->command,e)) return false;
        } else if (qa_json_get(d,at,"returns")!=QA_JSON_NONE) {
            qa_json_id ret=qa_json_get(d,at,"returns"); int32_t bits; v->has_return=true;
            if (!encoding(d,qa_json_get(d,ret,"encoding"),false,&v->encoding,e) ||
                !number(d,qa_json_get(d,ret,"value"),&v->returned,e) || !q3mod_scalar_word(v->returned,v->encoding,&bits,e)) return false;
            if (v->encoding==MOD_INT32 && trunc(v->returned)!=v->returned)
                return q3mod_fail(e,QA_ERROR_FORMAT,"Input handler return must be an exact int32");
            if (v->encoding==MOD_FLOAT32) v->returned=(float)v->returned;
        }
        bool found=false; for (size_t j=0;j<p->entry_count;++j) found|=p->entries[j]==v->entry;
        if (!found) {
            if (p->entry_count==SIZE_MAX/sizeof(*p->entries)) return q3mod_fail(e,QA_ERROR_MEMORY,"Input entry inventory overflows");
            uint32_t *next=realloc(p->entries,(p->entry_count+1)*sizeof(*next));
            if (!next) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning input entry inventory");
            p->entries=next; p->entries[p->entry_count++]=v->entry;
        }
    }
    return true;
}
static bool selection(const qa_json_document *d, qa_json_id id, application_q3_mod_profile *p,
    mod_selection *v, bool powered, qa_strings *s, qa_error *e)
{
    qa_json_id mask=qa_json_get(d,id,"mask"), values=qa_json_get(d,id,"values");
    if (!field(d,qa_json_get(d,id,"field"),p,&v->field,e) ||
        !array(d,values,sizeof(*v->values),(void **)&v->values,&v->count,false,e)) return false;
    v->masked=qa_json_type(d,mask)!=QA_JSON_NULL;
    if ((v->masked && (!word(d,mask,&v->mask,e) || v->mask>INT32_MAX)) || !v->count) return q3mod_fail(e,QA_ERROR_FORMAT,"Invalid protection selection mask or values");
    for (size_t i=0;i<v->count;++i) {
        qa_json_id at=qa_json_at(d,values,i), chosen=qa_json_get(d,at,"selected");
        mod_selection_value *row=v->values+i;
        if (!number(d,qa_json_get(d,at,"value"),&row->value,e)) return false;
        if (v->masked && (row->value<0 || row->value>INT32_MAX || trunc(row->value)!=row->value ||
            (((uint32_t)row->value&v->mask)!=(uint32_t)row->value))) return q3mod_fail(e,QA_ERROR_FORMAT,"Protection selection exceeds its mask");
        for (size_t j=0;j<i;++j) if (v->values[j].value==row->value) return q3mod_fail(e,QA_ERROR_FORMAT,"Duplicate protection selection");
        if (!powered) { if (!intern(d,chosen,s,&row->selected,true,e)) return false; }
        else if (qa_json_string_equal(d,chosen,"none")) row->selected=QA_POWER_NONE;
        else if (qa_json_string_equal(d,chosen,"screen")) row->selected=QA_POWER_SCREEN;
        else if (qa_json_string_equal(d,chosen,"shield")) row->selected=QA_POWER_SHIELD;
        else return q3mod_fail(e,QA_ERROR_FORMAT,"Unknown source powered protection");
    }
    return true;
}
static bool storage(application_q3_mod_profile *p, const mod_field *f, qa_error *e)
{
    const mod_record *r=record(p,f->record);
    for (size_t j=0;j<r->field_count;++j) if (!r->fields[j].private_field &&
        (uint64_t)f->offset<(uint64_t)r->fields[j].offset+r->fields[j].length &&
        r->fields[j].offset<(uint64_t)f->offset+4)
        return q3mod_fail(e,QA_ERROR_FORMAT,"Protection storage overlaps canonical source projection");
    for (size_t i=0;i<p->protection_count;++i) {
        const mod_protection *v=p->protection+i;
        const mod_field *fs[]={&v->count,v->has_selection?&v->selection.field:NULL};
        for (size_t j=0;j<2;++j) if (fs[j] && fs[j]!=f && fs[j]->record < p->record_count &&
            fs[j]->offset==f->offset && fs[j]->record == f->record)
            return q3mod_fail(e,QA_ERROR_FORMAT,"Protection channels overlap source storage");
    }
    return true;
}
static bool pickup_inventory(const qa_json_document *d,qa_json_id root,const qa_pickup_write *write,qa_strings *strings,qa_error *e)
{
    const char *name=qa_strings_cstr(strings,write->resource.item);
    qa_json_id records=qa_json_get(d,root,"actorRecords");
    if(write->fields==QA_PICKUP_COUNT) for(size_t i=0;i<qa_json_size(d,records);++i) {
        qa_json_id fields=qa_json_get(d,qa_json_at(d,records,i),"fields");
        for(size_t j=0;j<qa_json_size(d,fields);++j) {
            qa_json_id f=qa_json_at(d,fields,j);
            if(qa_json_string_equal(d,qa_json_get(d,f,"binding"),"inventory")&&qa_json_string_equal(d,qa_json_get(d,f,"item"),name)) return true;
        }
    }
    qa_json_id rows=qa_json_get(d,qa_json_get(d,root,"items"),"storage");
    for(size_t i=0;i<qa_json_size(d,rows);++i) {
        qa_json_id row=qa_json_at(d,rows,i);
        if(qa_json_string_equal(d,qa_json_get(d,row,"kind"),"bits")) {
            qa_json_id items=qa_json_get(d,row,"items");
            for(size_t j=0;write->fields==QA_PICKUP_COUNT&&j<qa_json_size(d,items);++j)
                if(qa_json_string_equal(d,qa_json_get(d,qa_json_at(d,items,j),"item"),name)) return true;
        } else if(qa_json_string_equal(d,qa_json_get(d,row,"item"),name)&&
            (write->fields==QA_PICKUP_COUNT||qa_json_string_equal(d,qa_json_get(d,qa_json_get(d,row,"capacity"),"kind"),"field"))) return true;
    }
    return q3mod_fail(e,QA_ERROR_FORMAT,"Original pickup write has no declared count or capacity storage");
}
static bool pickups(const qa_json_document *d,qa_json_id root,application_q3_mod_profile *p,qa_strings *strings,qa_error *e)
{
    qa_json_id rows=qa_json_get(d,root,"pickups");
    if(!array(d,rows,sizeof(*p->pickups),(void **)&p->pickups,&p->pickup_count,true,e)) return false;
    if(p->pickup_count&&!p->clients) return q3mod_fail(e,QA_ERROR_FORMAT,"Original pickups require admitted source clients");
    uint32_t mask=(UINT32_C(1)<<Q3_MOD_SELF)|(UINT32_C(1)<<Q3_MOD_OTHER)|(UINT32_C(1)<<Q3_MOD_ITEM)|
        (UINT32_C(1)<<Q3_MOD_TIME)|(UINT32_C(1)<<Q3_MOD_PICKUP_COUNT)|(UINT32_C(1)<<Q3_MOD_PICKUP_HAS_COUNT)|(UINT32_C(1)<<Q3_MOD_PICKUP_DROPPED);
    for(size_t i=0;i<p->pickup_count;++i) {
        mod_pickup *v=p->pickups+i; qa_json_id at=qa_json_at(d,rows,i),operation=qa_json_get(d,at,"operation");
        if(!application_mod_pickup_definition(d,at,strings,&v->id,&v->offered,&v->offered_count,
            &v->writes,&v->write_count,e))return false;
        for(size_t j=0;j<i;++j)if(p->pickups[j].id==v->id)return q3mod_fail(e,QA_ERROR_FORMAT,"Duplicate original pickup rule");
        for(size_t j=0;j<v->offered_count;++j)
            for(size_t k=0;k<=i;++k)for(size_t l=0;l<(k==i?j:p->pickups[k].offered_count);++l)
                if(p->pickups[k].offered[l]==v->offered[j])return q3mod_fail(e,QA_ERROR_FORMAT,"Ambiguous original pickup item");
        for(size_t j=0;j<v->write_count;++j){const qa_pickup_write *w=v->writes+j;
            if(w->resource.kind==QA_PICKUP_PROTECTION){
                bool found=false;for(size_t k=0;k<p->protection_count;++k)found|=p->protection[k].channel==w->resource.channel;
                if(!found)return q3mod_fail(e,QA_ERROR_FORMAT,"Original pickup has no declared protection owner");
            }else if(!pickup_inventory(d,root,w,strings,e))return false;
        }
        v->gated=qa_json_string_equal(d,qa_json_get(d,operation,"kind"),"gate-then-grant");
        if(!v->gated&&!qa_json_string_equal(d,qa_json_get(d,operation,"kind"),"boolean-grant")) return false;
        if(!call(d,qa_json_get(d,operation,"grant"),p,&v->grant,e)||!available(&v->grant,mask,p->clients,e)) return false;
        if(v->gated) {
            v->always=qa_json_string_equal(d,qa_json_get(d,operation,"grantAccepts"),"always");
            if((!v->always&&!qa_json_string_equal(d,qa_json_get(d,operation,"grantAccepts"),"nonzero"))||
                !call(d,qa_json_get(d,operation,"gate"),p,&v->gate,e)||!available(&v->gate,mask,p->clients,e)||v->gate.returns==MOD_VOID) return false;
        }
        if(!v->always&&v->grant.returns==MOD_VOID) return q3mod_fail(e,QA_ERROR_FORMAT,"Original pickup lacks its declared source decision");
        qa_json_id contexts=qa_json_get(d,at,"context");
        if(!array(d,contexts,sizeof(*v->context),(void **)&v->context,&v->context_count,false,e)) return false;
        for(size_t j=0;j<v->context_count;++j) {
            mod_pickup_context *f=v->context+j; qa_json_id row=qa_json_at(d,contexts,j);
            if(!record_index(d,qa_json_get(d,row,"record"),p,&f->record,e)||!word(d,qa_json_get(d,row,"offset"),&f->offset,e)||
                !argument(d,qa_json_get(d,row,"value"),p,&f->value,e)) return false;
            const mod_record *r=record(p,f->record);
            if(!r||r->client||f->offset%4||f->offset>r->stride-4) return q3mod_fail(e,QA_ERROR_FORMAT,"Original pickup context leaves its nonclient record");
            mod_call check={.arguments=&f->value,.argument_count=1}; if(!available(&check,mask,p->clients,e)) return false;
            for(size_t k=0;k<r->field_count;++k) if(!r->fields[k].private_field&&f->offset<r->fields[k].offset+r->fields[k].length&&r->fields[k].offset<f->offset+4)
                return q3mod_fail(e,QA_ERROR_FORMAT,"Original pickup context overlaps canonical projection");
            for(size_t k=0;k<j;++k) if(v->context[k].offset==f->offset&&v->context[k].record == f->record) return false;
            qa_json_id source=qa_json_get(d,root,"sourceActors");
            if(p->entity_record!=SIZE_MAX&&r==p->records+p->entity_record&&source!=QA_JSON_NONE) {
                uint32_t inuse; if(!word(d,qa_json_get(d,source,"inuse"),&inuse,e)||inuse==f->offset) return false;
                qa_json_id callbacks=qa_json_get(d,source,"callbacks"); const char *names[]={"think","touch","use","pain","die"};
                for(size_t k=0;k<5;++k) { qa_json_id field_id=qa_json_get(d,callbacks,names[k]); uint32_t offset;
                    if(field_id!=QA_JSON_NONE&&qa_json_type(d,field_id)!=QA_JSON_NULL&&(!word(d,field_id,&offset,e)||offset==f->offset)) return false;
                }
            }
        }
    }
    return true;
}
static bool parse(const qa_json_document *d, qa_json_id root, application_q3_mod_profile *p, qa_strings *s, qa_error *e)
{
    qa_json_id records=qa_json_get(d,root,"actorRecords"), clients=qa_json_get(d,root,"clients"),
        protection=qa_json_get(d,root,"protection"), callbacks=qa_json_get(d,root,"callbacks");
    if (!array(d,records,sizeof(*p->records),(void **)&p->records,&p->record_count,false,e)) return false;
    for (size_t i=0;i<p->record_count;++i) {
        mod_record *r=p->records+i; qa_json_id at=qa_json_at(d,records,i), fields=qa_json_get(d,at,"fields");
        if (!text(d,qa_json_get(d,at,"id"),&r->id,e) || !r->id[0] ||
            !word(d,qa_json_get(d,at,"address"),&r->address,e) || !r->address || r->address%4 ||
            !word(d,qa_json_get(d,at,"stride"),&r->stride,e) || r->stride<4 || r->stride%4 ||
            !word(d,qa_json_get(d,at,"capacity"),&r->capacity,e) || !r->capacity || r->capacity>1024 ||
            (uint64_t)r->stride*r->capacity>SIZE_MAX ||
            !qa_qvm_qualify_source_span(p->image,r->address,(size_t)r->stride*r->capacity,e) ||
            !array(d,fields,sizeof(*r->fields),(void **)&r->fields,&r->field_count,false,e)) return false;
        for (size_t j=0;j<i;++j) {
            const mod_record *prior=p->records+j;
            if (!strcmp(prior->id,r->id) || ((uint64_t)r->address<(uint64_t)prior->address+(uint64_t)prior->stride*prior->capacity &&
                (uint64_t)prior->address<(uint64_t)r->address+(uint64_t)r->stride*r->capacity))
                return q3mod_fail(e,QA_ERROR_FORMAT,"Source actor arrays duplicate identities or overlap");
        }
        for (size_t j=0;j<r->field_count;++j) {
            mod_record_field *f=r->fields+j; qa_json_id atf=qa_json_at(d,fields,j), binding=qa_json_get(d,atf,"binding");
            if (!word(d,qa_json_get(d,atf,"offset"),&f->offset,e)) return false;
            f->private_field=qa_json_string_equal(d,binding,"private") || qa_json_string_equal(d,binding,"constant");
            f->length=4;
            const char *vectors[]={"origin","velocity","angles","bounds-min","bounds-max","constant-vector"};
            for (size_t k=0;k<6;++k) if (qa_json_string_equal(d,binding,vectors[k])) f->length=12;
            if (qa_json_string_equal(d,binding,"private") && (!word(d,qa_json_get(d,atf,"byteLength"),&f->length,e) || !f->length)) return false;
            if (f->offset%4 || f->length>r->stride || f->offset>r->stride-f->length) return q3mod_fail(e,QA_ERROR_FORMAT,"Actor field exceeds its source record");
            for (size_t k=0;k<j;++k) if ((uint64_t)f->offset<(uint64_t)r->fields[k].offset+r->fields[k].length &&
                r->fields[k].offset<(uint64_t)f->offset+f->length) return q3mod_fail(e,QA_ERROR_FORMAT,"Source actor fields overlap");
        }
    }
    p->entity_record=SIZE_MAX; p->player_record=SIZE_MAX;
    qa_json_id entity=qa_json_get(d,root,"entityRecord");
    if (qa_json_type(d,entity)!=QA_JSON_NULL) {
        char *id=NULL; if (!text(d,entity,&id,e)) return false;
        for (size_t i=0;i<p->record_count;++i) if (!strcmp(p->records[i].id,id)) p->entity_record=i;
        free(id);
        if (p->entity_record==SIZE_MAX) return q3mod_fail(e,QA_ERROR_FORMAT,"Source engine entity record is undeclared");
    }
    if (clients!=QA_JSON_NONE) {
        p->clients=true; qa_json_id selected=qa_json_get(d,clients,"records"); char *state=NULL;
        if (!word(d,qa_json_get(d,clients,"maximum"),&p->maximum,e) || p->maximum<1 || p->maximum>64 ||
            p->entity_record==SIZE_MAX || qa_json_type(d,selected)!=QA_JSON_ARRAY ||
            !text(d,qa_json_get(d,clients,"playerStateRecord"),&state,e)) { free(state); return false; }
        for (size_t i=0;i<qa_json_size(d,selected);++i) {
            char *id=NULL;
            if (!text(d,qa_json_at(d,selected,i),&id,e)) { free(state); return false; }
            size_t index=0; while (index<p->record_count && strcmp(p->records[index].id,id)) ++index;
            free(id);
            if (index==p->record_count || index==p->entity_record || p->records[index].client) {
                free(state); return q3mod_fail(e,QA_ERROR_FORMAT,"Source client records duplicate rows or include the engine entity array");
            }
            p->records[index].client=true;
        }
        for (size_t i=0;i<p->record_count;++i) {
            if (!strcmp(p->records[i].id,state)) p->player_record=i;
            if (p->records[i].capacity<p->maximum) { free(state); return q3mod_fail(e,QA_ERROR_FORMAT,"Source actor array cannot hold its actual reserved clients"); }
        }
        free(state);
        if (p->player_record==SIZE_MAX || !p->records[p->player_record].client ||
            p->records[p->player_record].stride<qa_qvm_player_bytes(p->abi))
            return q3mod_fail(e,QA_ERROR_FORMAT,"Source clients require their full declared player-state record");
    }
    static const char *stage_names[]={"initialize","admit","userinfo","disconnect","frame"};
    for (size_t i=0;i<5;++i) {
        qa_json_id list=qa_json_get(d,i==0?root:clients,stage_names[i]); mod_call_group *group=p->stages+i;
        if (i>0 && clients==QA_JSON_NONE) continue;
        if (!array(d,list,sizeof(*group->calls),(void **)&group->calls,&group->count,i==4,e)) return false;
        uint32_t mask=UINT32_C(1)<<Q3_MOD_TIME;
        if (i) mask|=UINT32_C(1)<<Q3_MOD_SELF;
        if (i==4) mask|=UINT32_C(1)<<Q3_MOD_ELAPSED;
        for (size_t j=0;j<group->count;++j) if (!call(d,qa_json_at(d,list,j),p,group->calls+j,e) ||
            !available(group->calls+j,mask,p->clients,e)) return false;
    }
    qa_json_id source=qa_json_get(d,root,"sourceActors");
    if (source!=QA_JSON_NONE) {
        qa_json_id update=qa_json_get(d,source,"update"), frame=qa_json_get(d,source,"frame");
        if (qa_json_type(d,update)!=QA_JSON_NULL) {
            mod_call_group *group=p->stages+Q3_MOD_SOURCE_UPDATE;
            group->calls=calloc(1,sizeof(*group->calls)); group->count=group->calls?1:0;
            if (!group->calls) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning original source update call");
            if (!call(d,update,p,group->calls,e) || !available(group->calls,
                (UINT32_C(1)<<Q3_MOD_SELF)|(UINT32_C(1)<<Q3_MOD_TIME)|(UINT32_C(1)<<Q3_MOD_ELAPSED),p->clients,e)) return false;
        }
        if (frame!=QA_JSON_NONE) {
            if (qa_json_type(d,update)!=QA_JSON_NULL) return q3mod_fail(e,QA_ERROR_FORMAT,"Source frame and per-actor update cannot both own lifecycle");
            mod_call_group *group=p->stages+Q3_MOD_SOURCE_FRAME;
            group->calls=calloc(1,sizeof(*group->calls)); group->count=group->calls?1:0;
            if (!group->calls) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning original source frame call");
            if (!call(d,qa_json_get(d,frame,"call"),p,group->calls,e) || !available(group->calls,
                (UINT32_C(1)<<Q3_MOD_TIME)|(UINT32_C(1)<<Q3_MOD_ELAPSED),p->clients,e)) return false;
        }
    }
    qa_json_id inputs=qa_json_get(d,clients,"input");
    if (!array(d,inputs,sizeof(*p->inputs),(void **)&p->inputs,&p->input_count,true,e)) return false;
    for (size_t i=0;i<p->input_count;++i) {
        mod_input_binding *v=p->inputs+i; qa_json_id at=qa_json_at(d,inputs,i), scope=qa_json_get(d,at,"scope"), phase=qa_json_get(d,at,"phase"), calls=qa_json_get(d,at,"calls");
        v->slice=qa_json_string_equal(d,scope,"movement-slice"); v->before=qa_json_string_equal(d,phase,"before");
        if ((!v->slice && !qa_json_string_equal(d,scope,"client-command")) ||
            (!v->before && !qa_json_string_equal(d,phase,"after")) ||
            !array(d,calls,sizeof(*v->calls),(void **)&v->calls,&v->call_count,false,e)) return q3mod_fail(e,QA_ERROR_FORMAT,"Unknown input binding phase or scope");
        uint32_t input_mask=((UINT32_C(1)<<Q3_MOD_INPUT_COUNT)-1)|(UINT32_C(1)<<Q3_MOD_SELF)|
            (UINT32_C(1)<<Q3_MOD_TIME)|(UINT32_C(1)<<Q3_MOD_ELAPSED);
        for (size_t j=0;j<v->call_count;++j) if (!call(d,qa_json_at(d,calls,j),p,v->calls+j,e) ||
            !available(v->calls+j,input_mask,true,e)) return false;
        if (v->before && !outputs(d,qa_json_get(d,at,"outputs"),p,v,e)) return false;
    }
    if (!array(d,protection,sizeof(*p->protection),(void **)&p->protection,&p->protection_count,true,e)) return false;
    if (p->protection_count && clients==QA_JSON_NONE) return q3mod_fail(e,QA_ERROR_FORMAT,"Protection requires actual source client admission");
    for (size_t i=0;i<p->protection_count;++i) {
        mod_protection *v=p->protection+i; qa_json_id at=qa_json_at(d,protection,i), channel=qa_json_get(d,at,"channel"), admission=qa_json_get(d,at,"admission"), flags=qa_json_get(d,at,"flags"), st=qa_json_get(d,at,"storage");
        v->channel=qa_json_string_equal(d,channel,"regular")?QA_PROTECTION_REGULAR:QA_PROTECTION_POWERED;
        if ((v->channel==QA_PROTECTION_POWERED && !qa_json_string_equal(d,channel,"powered")) ||
            !intern(d,qa_json_get(d,at,"id"),s,&v->claim.rule,false,e) || !call(d,qa_json_get(d,at,"absorb"),p,&v->absorb,e) ||
            v->absorb.returns==MOD_VOID) return q3mod_fail(e,QA_ERROR_FORMAT,"Protection requires source savings and declared channel");
        uint32_t protection_mask=(UINT32_C(1)<<Q3_MOD_SELF)|(UINT32_C(1)<<Q3_MOD_ATTACKER)|
            (UINT32_C(1)<<Q3_MOD_INFLICTOR)|(UINT32_C(1)<<Q3_MOD_AMOUNT)|(UINT32_C(1)<<Q3_MOD_KNOCKBACK)|
            (UINT32_C(1)<<Q3_MOD_DAMAGE_FLAGS)|(UINT32_C(1)<<Q3_MOD_PROTECTION_SCALE)|
            (UINT32_C(1)<<Q3_MOD_POINT)|(UINT32_C(1)<<Q3_MOD_DIRECTION)|(UINT32_C(1)<<Q3_MOD_NORMAL)|(UINT32_C(1)<<Q3_MOD_TIME);
        if (!available(&v->absorb,protection_mask,true,e)) return false;
        qa_json_id kind=qa_json_get(d,admission,"kind");
        if (qa_json_string_equal(d,kind,"claim")) v->claim.admission=QA_PROTECTION_CLAIM;
        else if (qa_json_string_equal(d,kind,"replace-current-primary")) v->claim.admission=QA_PROTECTION_REPLACE_CURRENT;
        else if (qa_json_string_equal(d,kind,"replace-primary")) {
            v->claim.admission=QA_PROTECTION_REPLACE_PRIMARY;
            if (!intern(d,qa_json_get(d,admission,"owner"),s,&v->claim.expected_owner,false,e)) return false;
        } else return q3mod_fail(e,QA_ERROR_FORMAT,"Unknown protection admission");
        const char *names[]={"noArmor","noPowerArmor","noRegularArmor","energy","radius"};
        uint32_t *masks[]={&v->no_armor,&v->no_power,&v->no_regular,&v->energy,&v->radius};
        for (size_t j=0;j<5;++j) {
            qa_json_id f=qa_json_get(d,flags,names[j]);
            if (j==4 && f==QA_JSON_NONE) continue;
            if (!word(d,f,masks[j],e) || *masks[j]>INT32_MAX) return q3mod_fail(e,QA_ERROR_FORMAT,"Protection mask exceeds signed source flags");
        }
        if (!field(d,qa_json_get(d,st,v->channel==QA_PROTECTION_REGULAR?"points":"cells"),p,&v->count,e)) return false;
        v->has_selection=qa_json_get(d,st,"selection")!=QA_JSON_NONE;
        if (v->channel==QA_PROTECTION_POWERED && !v->has_selection) return q3mod_fail(e,QA_ERROR_FORMAT,"Powered protection requires its source kind selection");
        if (v->has_selection && !selection(d,qa_json_get(d,st,"selection"),p,&v->selection,v->channel==QA_PROTECTION_POWERED,s,e)) return false;
        if (v->channel==QA_PROTECTION_REGULAR && !intern(d,qa_json_get(d,st,"item"),s,&v->item,true,e)) return false;
        for (size_t j=0;j<i;++j) if (p->protection[j].channel==v->channel || p->protection[j].claim.rule==v->claim.rule)
            return q3mod_fail(e,QA_ERROR_FORMAT,"Protection channels or rules are duplicated");
    }
    for (size_t i=0;i<p->protection_count;++i) if (!storage(p,&p->protection[i].count,e) ||
        (p->protection[i].has_selection && !storage(p,&p->protection[i].selection.field,e))) return false;
    if (!array(d,callbacks,sizeof(*p->callbacks),(void **)&p->callbacks,&p->callback_count,false,e)) return false;
    static const char *const operations[]={"damage","inventory.give","inventory.consume","actor.think","actor.touch","actor.use","actor.pain","actor.die"};
    for (size_t i=0;i<p->callback_count;++i) {
        mod_callback *v=p->callbacks+i; qa_json_id at=qa_json_at(d,callbacks,i), op=qa_json_get(d,at,"operation"), stage=qa_json_get(d,at,"stage");
        if (!intern(d,qa_json_get(d,at,"id"),s,&v->id,false,e) || !call(d,at,p,&v->call,e)) return false;
        size_t j=0; while (j<Q3_MOD_OPERATION_COUNT && !qa_json_string_equal(d,op,operations[j])) ++j;
        if (j==Q3_MOD_OPERATION_COUNT) return q3mod_fail(e,QA_ERROR_FORMAT,"Unknown generic callback operation");
        v->operation=(application_q3_mod_operation)j;
        if (qa_json_string_equal(d,stage,"observe")) v->stage=QA_OPERATION_OBSERVE;
        else if (qa_json_string_equal(d,stage,"transform") && j<=Q3_MOD_CONSUME) {
            v->stage=QA_OPERATION_TRANSFORM; qa_json_id result=qa_json_get(d,at,"result");
            v->knockback=j==Q3_MOD_DAMAGE && qa_json_string_equal(d,result,"knockback");
            if (!v->knockback && !qa_json_string_equal(d,result,"amount")) return q3mod_fail(e,QA_ERROR_FORMAT,"Unknown callback transformation");
        } else if (qa_json_string_equal(d,stage,"replace") && j>=Q3_MOD_THINK && qa_json_string_equal(d,qa_json_get(d,at,"result"),"boolean")) v->stage=QA_OPERATION_REPLACE;
        else return q3mod_fail(e,QA_ERROR_FORMAT,"Callback stage is invalid for its operation");
        if (v->stage!=QA_OPERATION_OBSERVE && v->call.returns==MOD_VOID)
            return q3mod_fail(e,QA_ERROR_FORMAT,"QVM callback transformation or replacement requires its original return value");
        uint32_t mask=(UINT32_C(1)<<Q3_MOD_SELF)|(UINT32_C(1)<<Q3_MOD_TIME);
        if (v->stage==QA_OPERATION_OBSERVE) mask|=UINT32_C(1)<<Q3_MOD_RESULT;
        if (j==Q3_MOD_DAMAGE) mask|=(UINT32_C(1)<<Q3_MOD_ATTACKER)|(UINT32_C(1)<<Q3_MOD_INFLICTOR)|
            (UINT32_C(1)<<Q3_MOD_AMOUNT)|(UINT32_C(1)<<Q3_MOD_KNOCKBACK)|(UINT32_C(1)<<Q3_MOD_DIRECTION)|
            (UINT32_C(1)<<Q3_MOD_POINT)|(UINT32_C(1)<<Q3_MOD_NORMAL);
        else if (j==Q3_MOD_GIVE || j==Q3_MOD_CONSUME) mask|=(UINT32_C(1)<<Q3_MOD_ITEM)|(UINT32_C(1)<<Q3_MOD_AMOUNT);
        else if (j==Q3_MOD_THINK) mask|=UINT32_C(1)<<Q3_MOD_ELAPSED;
        else if (j==Q3_MOD_TOUCH || j==Q3_MOD_USE) { mask|=UINT32_C(1)<<Q3_MOD_OTHER; if (j==Q3_MOD_USE) mask|=UINT32_C(1)<<Q3_MOD_ACTIVATOR; }
        else { mask|=(UINT32_C(1)<<Q3_MOD_ATTACKER)|(UINT32_C(1)<<Q3_MOD_AMOUNT)|(UINT32_C(1)<<Q3_MOD_KNOCKBACK);
            if (j==Q3_MOD_DIE) mask|=(UINT32_C(1)<<Q3_MOD_INFLICTOR)|(UINT32_C(1)<<Q3_MOD_POINT); }
        if (!available(&v->call,mask,clients!=QA_JSON_NONE,e)) return false;
        for (size_t k=0;k<i;++k) if (p->callbacks[k].id==v->id) return q3mod_fail(e,QA_ERROR_FORMAT,"Duplicate generic callback identity");
    }
    return pickups(d,root,p,s,e);
}
void application_q3_mod_profile_destroy(application_q3_mod_profile *p)
{
    if (!p) return;
    for (size_t i=0;i<p->record_count;++i) { free(p->records[i].id); free(p->records[i].fields); }
    for (size_t i=0;i<p->input_count;++i) {
        mod_input_binding *b=p->inputs+i;
        for (size_t j=0;j<b->call_count;++j) call_free(b->calls+j);
        for (size_t j=0;j<b->output_count;++j) {
            free(b->outputs[j].ordered_inputs);
            free(b->outputs[j].actor.indirections); free(b->outputs[j].command.indirections);
        }
        free(b->calls); free(b->outputs);
    }
    for (size_t i=0;i<p->protection_count;++i) {
        mod_protection *v=p->protection+i; call_free(&v->absorb);
        free(v->selection.values);
    }
    for (size_t i=0;i<p->callback_count;++i) call_free(&p->callbacks[i].call);
    for (size_t i=0;i<Q3_MOD_STAGE_COUNT;++i) {
        for (size_t j=0;j<p->stages[i].count;++j) call_free(p->stages[i].calls+j);
        free(p->stages[i].calls);
    }
    for(size_t i=0;i<p->pickup_count;++i) {
        mod_pickup *v=p->pickups+i; call_free(&v->gate); call_free(&v->grant);
        for(size_t j=0;j<v->context_count;++j) { argument_free(&v->context[j].value); }
        free(v->context); free(v->writes); free(v->offered);
    }
    free(p->pickups);
    free(p->records); free(p->inputs); free(p->protection); free(p->callbacks); free(p->entries);
    free(p->declaration.data); qa_qvm_image_release(p->image); free(p);
}
bool application_q3_mod_profile_create(qa_qvm_image *image, qa_qvm_abi abi, const char *path,
    qa_bytes declaration, qa_strings *strings, application_q3_mod_profile **out, qa_error *e)
{
    if (!image || (abi!=QA_QVM_Q3_MODERN && abi!=QA_QVM_Q3_116N) || !path || !strings || !out || *out || !declaration.data || !declaration.size)
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Generic source profile requires its held artifact and authentic declaration");
    qa_json_document *d=NULL; application_q3_mod_profile *p=calloc(1,sizeof(*p));
    if (!p) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning generic QVM profile");
    p->image=image; qa_qvm_image_retain(image); p->abi=abi;
    p->declaration.data=malloc(declaration.size);
    if (!p->declaration.data) { application_q3_mod_profile_destroy(p); return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining authentic generic source declaration"); }
    memcpy(p->declaration.data,declaration.data,declaration.size); p->declaration.size=declaration.size;
    bool ok=qa_json_parse(declaration,&d,e);
    if (ok) {
        qa_json_id root=qa_json_root(d), program=qa_json_get(d,root,"program"); uint32_t version;
        char *declared_path=NULL, *normalized_path=NULL;
        ok=text(d,qa_json_get(d,program,"path"),&declared_path,e);
        if (ok) { normalized_path=qa_vfs_normalize_path(declared_path,e); ok=normalized_path!=NULL; }
        ok=ok && word(d,qa_json_get(d,root,"version"),&version,e) && version==1 &&
            qa_json_string_equal(d,qa_json_get(d,root,"runtime"),"qvm") &&
            !strcmp(normalized_path,path) &&
            qa_json_string_equal(d,qa_json_get(d,root,"abiProfile"),abi==QA_QVM_Q3_MODERN?"q3-modern":"q3-1.16n-base");
        free(declared_path); free(normalized_path);
        if (!ok) q3mod_fail(e,QA_ERROR_FORMAT,"Generic declaration does not qualify this actual QVM artifact");
        else ok=parse(d,root,p,strings,e);
    }
    qa_json_destroy(d);
    if (!ok) { application_q3_mod_profile_destroy(p); return false; }
    *out=p; return true;
}
qa_bytes application_q3_mod_declaration(const application_q3_mod_profile *p)
{ return p?(qa_bytes){p->declaration.data,p->declaration.size}:(qa_bytes){0}; }
size_t application_q3_mod_record_count(const application_q3_mod_profile *p) { return p?p->record_count:0; }
bool application_q3_mod_record(const application_q3_mod_profile *p, size_t i,
    const char **id, uint32_t *address, uint32_t *stride, uint32_t *capacity)
{
    if (!p || i>=p->record_count || !id || !address || !stride || !capacity) return false;
    const mod_record *r=p->records+i; *id=r->id; *address=r->address; *stride=r->stride; *capacity=r->capacity; return true;
}
bool application_q3_mod_clients(const application_q3_mod_profile *p, uint32_t *maximum,
    const char **entity_record, const char **player_record)
{
    if (!p || !p->clients || !maximum || !entity_record || !player_record) return false;
    *maximum=p->maximum; *entity_record=p->records[p->entity_record].id;
    *player_record=p->records[p->player_record].id; return true;
}
bool application_q3_mod_record_is_client(const application_q3_mod_profile *p, size_t i)
{ return p && i<p->record_count && p->records[i].client; }
bool application_q3_mod_call_create(application_q3_mod_profile *p, qa_bytes bytes,
    uint32_t mask, application_q3_mod_call **out, qa_error *e)
{
    if (!p || !out || *out) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Declared call requires its retained source profile");
    qa_json_document *document=NULL; mod_call *value=calloc(1,sizeof(*value));
    if (!value) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning qualified lifecycle call");
    bool ok=qa_json_parse(bytes,&document,e) && call(document,qa_json_root(document),p,value,e) && available(value,mask,p->clients,e);
    qa_json_destroy(document);
    if (!ok) { call_free(value); free(value); return false; }
    *out=value; return true;
}
void application_q3_mod_call_destroy(application_q3_mod_call *call_value)
{ if (call_value) { call_free(call_value); free(call_value); } }
uint32_t application_q3_mod_call_instruction(const application_q3_mod_call *call_value)
{ return call_value?call_value->entry:0; }
