#include "native_q2_armor.h"
#include "qa/native_observe.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct armor_selection {
    application_native_q2_field field;
    bool enumeration;
    double value,none;
} armor_selection;
typedef struct armor_regular {
    qa_item_id item;
    armor_selection selection;
    application_native_q2_field points;
    double normal,energy;
} armor_regular;
typedef struct armor_power {
    qa_item_id item;
    qa_power_kind kind;
    armor_selection selection;
    application_native_q2_field cells,enabled;
    uint64_t mask;
} armor_power;
typedef struct armor_store {
    const application_native_q2_field *field;
    qa_native_address address;
    double value;
} armor_store;
typedef struct armor_watch_field {
    struct application_native_q2_armor_watch *owner;
    const application_native_q2_field *field;
    qa_native_address address;
    qa_native_write_observer *binding;
} armor_watch_field;
struct application_native_q2_armor {
    application_native_q2_armor_options options;
    armor_regular *regular;
    armor_power *power;
    size_t regular_count,power_count,watch_count;
    bool q2;
};
struct application_native_q2_armor_watch {
    application_native_q2_armor *owner;
    qa_actor_id actor;
    application_native_q2_armor_changed_fn changed;
    void *context;
    armor_watch_field *fields;
    size_t count;
    qa_armor previous;
    unsigned entered;
    bool closing;
};
static bool fail(qa_error *e,const char *text)
{ qa_error_set(e,QA_ERROR_FORMAT,0,"%s",text); return false; }
size_t application_native_q2_field_size(qa_native_value_type type)
{
    switch(type) {
    case QA_NATIVE_I8:case QA_NATIVE_U8:return 1;
    case QA_NATIVE_I16:case QA_NATIVE_U16:return 2;
    case QA_NATIVE_I32:case QA_NATIVE_U32:case QA_NATIVE_F32:return 4;
    case QA_NATIVE_I64:case QA_NATIVE_U64:case QA_NATIVE_F64:return 8;
    default:return 0;
    }
}
bool application_native_q2_field_value(qa_native_value_type type,double value,qa_error *e)
{
    size_t bytes=application_native_q2_field_size(type);
    if(!bytes||!isfinite(value)) return fail(e,"Native source field requires a finite declared scalar");
    if(type==QA_NATIVE_F64) return true;
    if(type==QA_NATIVE_F32) return isfinite((float)value)||fail(e,"Native source field exceeds float32");
    bool unsigned_value=type==QA_NATIVE_U8||type==QA_NATIVE_U16||type==QA_NATIVE_U32||type==QA_NATIVE_U64;
    double bound=ldexp(1.0,(int)(bytes*8)-(unsigned_value?0:1));
    return (value==trunc(value)&&fabs(value)<=9007199254740991.0&&
        value>=(unsigned_value?0:-bound)&&value<bound)||fail(e,"Native source counter exceeds its exact integer encoding");
}
void application_native_q2_field_dispose(application_native_q2_field *field)
{ if(field) { *field=(application_native_q2_field){0}; } }
bool application_native_q2_field_parse(application_native_q2_callbacks *callbacks,qa_json_id id,
    application_native_q2_field *out,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(callbacks);
    qa_buffer name={0}; uint64_t offset; application_native_q2_field field={0};
    if(!out||!d||!qa_json_string(d,qa_json_get(d,id,"record"),&name,e)) return false;
    bool ok=name.size&&!memchr(name.data,0,name.size)&&
        qa_json_u64(d,qa_json_get(d,id,"offset"),&offset,e)&&offset<=UINT32_MAX;
    const char *names[]={"void","int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
    for(size_t i=1;i<sizeof(names)/sizeof(*names);++i)
        if(qa_json_string_equal(d,qa_json_get(d,id,"encoding"),names[i])) field.encoding=(qa_native_value_type)i;
    qa_json_id rows=qa_json_get(d,qa_json_root(d),"actorRecords"); bool found=false;
    for(size_t i=0;ok&&i<qa_json_size(d,rows);++i) {
        qa_json_id row=qa_json_at(d,rows,i);
        if(!qa_json_string_equal(d,qa_json_get(d,row,"id"),(char *)name.data)) continue;
        uint64_t stride;
        ok=qa_json_u64(d,qa_json_get(d,row,"stride"),&stride,e)&&offset<=stride&&
            application_native_q2_field_size(field.encoding)&&application_native_q2_field_size(field.encoding)<=stride-offset;
        found=true; break;
    }
    if(!ok||!found) { qa_buffer_free(&name); return fail(e,"Native scalar field exceeds its declared actor record"); }
    field.record=application_native_q2_callbacks_record_index(callbacks,qa_json_get(d,id,"record")); qa_buffer_free(&name); field.offset=(uint32_t)offset; *out=field; return true;
}
bool application_native_q2_field_address(application_native_q2_callbacks *callbacks,qa_actor_id actor,
    const application_native_q2_field *field,qa_native_address *out,qa_error *e)
{
    qa_native_address base;
    if(!field||!field->record||!out||!application_native_q2_callbacks_storage_current(callbacks,e)||
        !application_native_q2_callbacks_record(callbacks,actor,field->record,&base,e)) return false;
    if(!base||base>UINT64_MAX-field->offset) return fail(e,"Native scalar field lost its original record address");
    *out=base+field->offset;
    return qa_native_range_check(application_native_q2_callbacks_instance(callbacks),*out,
        application_native_q2_field_size(field->encoding),QA_NATIVE_MEMORY_READ,e);
}
bool application_native_q2_field_read(application_native_q2_callbacks *callbacks,qa_actor_id actor,
    const application_native_q2_field *field,double *out,qa_error *e)
{
    qa_native_address address;
    return out&&application_native_q2_field_address(callbacks,actor,field,&address,e)&&
        application_native_q2_callbacks_scalar_read(callbacks,address,field->encoding,out,e);
}
bool application_native_q2_field_write(application_native_q2_callbacks *callbacks,qa_actor_id actor,
    const application_native_q2_field *field,double value,qa_error *e)
{
    qa_native_address address;
    return application_native_q2_field_value(field->encoding,value,e)&&
        application_native_q2_field_address(callbacks,actor,field,&address,e)&&
        application_native_q2_callbacks_scalar_write(callbacks,address,field->encoding,value,e);
}
static bool identity(application_native_q2_armor *o,qa_json_id id,qa_item_id *out,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(o->options.callbacks);
    if(qa_json_type(d,id)==QA_JSON_NULL) { *out=0; return true; }
    qa_buffer text={0}; if(!qa_json_string(d,id,&text,e)) return false;
    bool ok=text.size&&!memchr(text.data,0,text.size)&&qa_strings_intern(o->options.strings,
        (qa_bytes){text.data,text.size},out,e); qa_buffer_free(&text);
    return ok||fail(e,"Native armor item identity is invalid");
}
static bool selection_parse(application_native_q2_armor *o,qa_json_id id,armor_selection *out,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(o->options.callbacks);
    if(!application_native_q2_field_parse(o->options.callbacks,qa_json_get(d,id,"field"),&out->field,e)) return false;
    if(qa_json_string_equal(d,qa_json_get(d,id,"kind"),"positive")) return true;
    out->enumeration=true;
    return (qa_json_string_equal(d,qa_json_get(d,id,"kind"),"enum")&&
        qa_json_number(d,qa_json_get(d,id,"value"),&out->value,e)&&
        qa_json_number(d,qa_json_get(d,id,"none"),&out->none,e)&&out->value!=out->none&&
        application_native_q2_field_value(out->field.encoding,out->value,e)&&
        application_native_q2_field_value(out->field.encoding,out->none,e)&&
        (out->field.encoding!=QA_NATIVE_F32||((double)(float)out->value==out->value&&(double)(float)out->none==out->none)))||
        fail(e,"Native armor enumeration cannot be represented exactly");
}
static size_t field_count(const application_native_q2_armor *o)
{
    size_t n=o->regular_count*2+o->power_count*2;
    for(size_t i=0;i<o->power_count;++i) if(o->power[i].enabled.record) ++n;
    return n;
}
static const application_native_q2_field *field_at(const application_native_q2_armor *o,size_t n)
{
    for(size_t i=0;i<o->regular_count;++i) {
        if(!n--) return &o->regular[i].selection.field;
        if(!n--) return &o->regular[i].points;
    }
    for(size_t i=0;i<o->power_count;++i) {
        if(!n--) return &o->power[i].selection.field;
        if(!n--) return &o->power[i].cells;
        if(o->power[i].enabled.record&&!n--) return &o->power[i].enabled;
    }
    return NULL;
}
static bool fields_valid(application_native_q2_armor *o,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(o->options.callbacks);
    qa_json_id root=qa_json_root(d),rows=qa_json_get(d,root,"actorRecords");
    for(size_t i=0;i<field_count(o);++i) {
        const application_native_q2_field *a=field_at(o,i); bool eligible=false;
        for(size_t r=0;r<qa_json_size(d,rows);++r) {
            qa_json_id row=qa_json_at(d,rows,r);
            if(application_native_q2_callbacks_record_index(o->options.callbacks,qa_json_get(d,row,"id"))==a->record)
                eligible=application_native_q2_callbacks_record_index(o->options.callbacks,qa_json_get(d,root,"entityRecord"))==a->record||
                    qa_json_string_equal(d,qa_json_get(d,qa_json_get(d,row,"base"),"kind"),"clients");
        }
        if(!eligible) return fail(e,"Native armor requires its declared entity or client storage");
        for(size_t j=0;j<i;++j) {
            const application_native_q2_field *b=field_at(o,j);
            if(a->record==b->record&&(uint64_t)a->offset<b->offset+application_native_q2_field_size(b->encoding)&&
                (uint64_t)b->offset<a->offset+application_native_q2_field_size(a->encoding)&&
                (a->offset!=b->offset||a->encoding!=b->encoding)) return fail(e,"Native armor fields overlap incompatible encodings");
        }
    }
    return true;
}
static bool create_layout(const application_native_q2_armor_options *options,
    qa_json_id regular,qa_json_id power,bool q2,application_native_q2_armor **out,qa_error *e)
{
    if(!options||!out||*out||!options->callbacks||!options->strings||!options->owner) return fail(e,"Native armor requires its actual callback owner");
    const qa_json_document *d=application_native_q2_callbacks_document(options->callbacks);
    if((regular!=QA_JSON_NONE&&qa_json_type(d,regular)!=QA_JSON_ARRAY)||
        (power!=QA_JSON_NONE&&qa_json_type(d,power)!=QA_JSON_ARRAY)) return fail(e,"Native armor storage requires authored arrays");
    application_native_q2_armor *o=calloc(1,sizeof(*o));
    if(!o) { qa_error_set(e,QA_ERROR_MEMORY,0,"Owning native armor declaration"); return false; }
    o->options=*options; o->q2=q2; *out=o;
    o->regular_count=qa_json_size(d,regular); o->power_count=qa_json_size(d,power);
    o->regular=o->regular_count?calloc(o->regular_count,sizeof(*o->regular)):NULL;
    o->power=o->power_count?calloc(o->power_count,sizeof(*o->power)):NULL;
    if((o->regular_count&&!o->regular)||(o->power_count&&!o->power)) { qa_error_set(e,QA_ERROR_MEMORY,0,"Owning native armor fields"); return false; }
    for(size_t i=0;i<o->regular_count;++i) {
        armor_regular *r=o->regular+i; qa_json_id row=qa_json_at(d,regular,i);
        if(!identity(o,qa_json_get(d,row,"item"),&r->item,e)||
            !selection_parse(o,qa_json_get(d,row,"selection"),&r->selection,e)||
            !application_native_q2_field_parse(options->callbacks,qa_json_get(d,row,"points"),&r->points,e)) return false;
        if(q2) {
            double normal,energy;
            if(!r->item||!qa_json_number(d,qa_json_get(d,row,"normalProtection"),&normal,e)||
                !qa_json_number(d,qa_json_get(d,row,"energyProtection"),&energy,e)||!isfinite(normal)||!isfinite(energy))
                return fail(e,"Native Q2 armor protection is not finite");
            r->normal=normal; r->energy=energy;
        }
        for(size_t j=0;j<i;++j) if(o->regular[j].item==r->item) return fail(e,"Native regular armor selection repeats an item");
    }
    for(size_t i=0;i<o->power_count;++i) {
        armor_power *p=o->power+i; qa_json_id row=qa_json_at(d,power,i),enabled=qa_json_get(d,row,"enabled");
        if(!identity(o,qa_json_get(d,row,"item"),&p->item,e)||!p->item||
            !selection_parse(o,qa_json_get(d,row,"selection"),&p->selection,e)||
            !application_native_q2_field_parse(options->callbacks,qa_json_get(d,row,"cells"),&p->cells,e)) return false;
        if(qa_json_string_equal(d,qa_json_get(d,row,"kind"),"screen")) p->kind=QA_POWER_SCREEN;
        else if(qa_json_string_equal(d,qa_json_get(d,row,"kind"),"shield")) p->kind=QA_POWER_SHIELD;
        else return fail(e,"Native powered armor kind is undeclared");
        if(qa_json_type(d,enabled)!=QA_JSON_NULL) {
            if(!application_native_q2_field_parse(options->callbacks,qa_json_get(d,enabled,"field"),&p->enabled,e)||
                !qa_json_u64(d,qa_json_get(d,enabled,"mask"),&p->mask,e)||!p->mask||
                p->enabled.encoding==QA_NATIVE_F32||p->enabled.encoding==QA_NATIVE_F64||
                !application_native_q2_field_value(p->enabled.encoding,(double)p->mask,e)) return fail(e,"Native powered armor enabled mask exceeds its integer field");
        }
        for(size_t j=0;j<i;++j) if(o->power[j].kind==p->kind) return fail(e,"Native powered armor selection repeats a kind");
    }
    return fields_valid(o,e);
}
bool application_native_q2_armor_create(const application_native_q2_armor_options *options,
    qa_json_id regular,qa_json_id power,bool q2,application_native_q2_armor **out,qa_error *e)
{
    if(!out||*out) return fail(e,"Native armor construction requires an empty destination");
    application_native_q2_armor *o=NULL;
    if(!create_layout(options,regular,power,q2,&o,e)) {
        /* Layout parsing installs no actors or write subscriptions. */
        (void)application_native_q2_armor_destroy(&o,NULL);return false;
    }
    *out=o;return true;
}
static bool raw(application_native_q2_armor *o,qa_actor_id actor,const application_native_q2_field *field,
    const armor_store *stores,size_t count,double *out,qa_error *e)
{
    qa_native_address address;
    if(!application_native_q2_field_address(o->options.callbacks,actor,field,&address,e)) return false;
    for(size_t i=0;i<count;++i) if(stores[i].address==address) { *out=stores[i].value; return true; }
    return application_native_q2_callbacks_scalar_read(o->options.callbacks,address,field->encoding,out,e);
}
static bool selected(application_native_q2_armor *o,qa_actor_id actor,const armor_selection *selection,
    const armor_store *stores,size_t count,bool *out,qa_error *e)
{
    double value; if(!raw(o,actor,&selection->field,stores,count,&value,e)) return false;
    *out=selection->enumeration?value==selection->value:value>0; return true;
}
static bool capture(application_native_q2_armor *o,qa_actor_id actor,const armor_store *stores,size_t count,qa_armor *out,qa_error *e)
{
    qa_armor armor={0};
    for(size_t i=0;i<o->regular_count;++i) {
        armor_regular *r=o->regular+i; bool active; double points;
        if(!selected(o,actor,&r->selection,stores,count,&active,e)) return false;
        if(!active) continue;
        if(!raw(o,actor,&r->points,stores,count,&points,e)) return false;
        armor.regular=(qa_regular_armor){.kind=o->q2?QA_ARMOR_Q2:QA_ARMOR_SOURCE,.item=r->item,.points=points};
        armor.regular.protection.q2.normal=r->normal; armor.regular.protection.q2.energy=r->energy; break;
    }
    for(size_t i=0;i<o->power_count;++i) {
        armor_power *p=o->power+i; bool active; double cells;
        if(!selected(o,actor,&p->selection,stores,count,&active,e)) return false;
        if(active&&p->enabled.record) {
            double enabled;
            if(!raw(o,actor,&p->enabled,stores,count,&enabled,e)||enabled!=trunc(enabled)||fabs(enabled)>9007199254740991.0) return fail(e,"Native powered armor enabled word is not an exact integer");
            uint64_t bits=enabled<0?(uint64_t)(int64_t)enabled:(uint64_t)enabled; active=(bits&p->mask)!=0;
        }
        if(!active) continue;
        if(!raw(o,actor,&p->cells,stores,count,&cells,e)) return false;
        armor.powered=(qa_powered_armor){.kind=p->kind,.cells=cells,.source_owner=o->options.owner,.source_kind=QA_POWER_SOURCE_GENERIC}; break;
    }
    *out=armor; return true;
}
bool application_native_q2_armor_read(application_native_q2_armor *o,qa_actor_id actor,qa_armor *out,qa_error *e)
{ return o&&out&&capture(o,actor,NULL,0,out,e); }
bool application_native_q2_armor_normalize_legacy(application_native_q2_armor *o,qa_actor_id actor,
    const qa_armor *input,qa_armor *out,qa_error *e)
{
    if(!o||!input||!out) return fail(e,"Native armor normalization requires its retained declaration");
    *out=*input;
    const armor_power *power=NULL;
    for(size_t i=0;i<o->power_count;++i) if(o->power[i].kind==input->powered.kind) {power=o->power+i;break;}
    if(!power) return true;
    qa_armor actual;
    if(!application_native_q2_armor_read(o,actor,&actual,e)) return false;
    if(actual.regular.kind==QA_ARMOR_NONE&&input->regular.kind==QA_ARMOR_Q2&&input->regular.item==power->item&&
        input->regular.points==0&&input->regular.protection.q2.normal==0&&input->regular.protection.q2.energy==0&&
        input->powered.kind!=QA_POWER_NONE&&input->powered.kind==actual.powered.kind&&input->powered.cells==actual.powered.cells)
        out->regular=(qa_regular_armor){0};
    return true;
}
static bool store(application_native_q2_armor *o,qa_actor_id actor,const application_native_q2_field *field,
    double value,armor_store *stores,size_t *count,qa_error *e)
{
    qa_native_address address;
    if(!application_native_q2_field_value(field->encoding,value,e)||
        !application_native_q2_field_address(o->options.callbacks,actor,field,&address,e)||
        !qa_native_range_check(application_native_q2_callbacks_instance(o->options.callbacks),address,
            application_native_q2_field_size(field->encoding),QA_NATIVE_MEMORY_WRITE,e)) return false;
    if(field->encoding==QA_NATIVE_F32) value=(float)value;
    for(size_t i=0;i<*count;++i) if(stores[i].address==address) { stores[i]=(armor_store){field,address,value}; return true; }
    stores[(*count)++]=(armor_store){field,address,value}; return true;
}
static bool select_store(application_native_q2_armor *o,qa_actor_id actor,const armor_selection *s,bool active,
    armor_store *stores,size_t *count,qa_error *e)
{
    double value;
    if(s->enumeration) value=active?s->value:s->none;
    else if(!active) value=0;
    else { if(!raw(o,actor,&s->field,NULL,0,&value,e)) return false; value=fmax(1,value); }
    return store(o,actor,&s->field,value,stores,count,e);
}
static bool enable_store(application_native_q2_armor *o,qa_actor_id actor,const armor_power *p,bool active,
    armor_store *stores,size_t *count,qa_error *e)
{
    if(!p->enabled.record) return select_store(o,actor,&p->selection,active,stores,count,e);
    double value; if(!raw(o,actor,&p->enabled,stores,*count,&value,e)||value!=trunc(value)||fabs(value)>9007199254740991.0) return fail(e,"Native powered armor mask requires an exact integer");
    int64_t signed_value=(int64_t)value; uint64_t bits; memcpy(&bits,&signed_value,sizeof(bits));
    bits=active?bits|p->mask:bits&~p->mask; memcpy(&signed_value,&bits,sizeof(bits));
    if(!store(o,actor,&p->enabled,(double)signed_value,stores,count,e)) return false;
    return !active||select_store(o,actor,&p->selection,true,stores,count,e);
}
static bool plan(application_native_q2_armor *o,qa_actor_id actor,const qa_armor *next,armor_store **out,size_t *out_count,qa_error *e)
{
    qa_armor before,after; armor_regular *regular=NULL; armor_power *power=NULL;
    if(!next||!application_native_q2_armor_read(o,actor,&before,e)) return false;
    if(next->regular.kind!=QA_ARMOR_NONE) {
        if(next->regular.kind!=(o->q2?QA_ARMOR_Q2:QA_ARMOR_SOURCE)) return fail(e,"Native armor cannot store another regular armor family");
        for(size_t i=0;i<o->regular_count;++i) if(o->regular[i].item==next->regular.item) regular=o->regular+i;
        if(!regular||(o->q2&&(regular->normal!=next->regular.protection.q2.normal||regular->energy!=next->regular.protection.q2.energy))) return fail(e,"Native armor differs from its authored item or protection");
    }
    if(next->powered.kind!=QA_POWER_NONE) {
        for(size_t i=0;i<o->power_count;++i) if(o->power[i].kind==next->powered.kind) power=o->power+i;
        if(!power||next->powered.source_owner!=o->options.owner||next->powered.source_kind!=QA_POWER_SOURCE_GENERIC) return fail(e,"Native powered armor differs from its actual source owner");
    }
    size_t maximum=field_count(o),count=0; armor_store *stores=maximum?calloc(maximum,sizeof(*stores)):NULL;
    if(maximum&&!stores) { qa_error_set(e,QA_ERROR_MEMORY,0,"Preparing native armor stores"); return false; }
    bool ok=true;
    if(next->regular.kind==QA_ARMOR_NONE) for(size_t i=0;ok&&i<o->regular_count;++i) {
        bool active; ok=selected(o,actor,&o->regular[i].selection,NULL,0,&active,e);
        if(ok&&active) { ok=select_store(o,actor,&o->regular[i].selection,false,stores,&count,e)&&store(o,actor,&o->regular[i].points,0,stores,&count,e); break; }
    }
    if(before.powered.kind!=next->powered.kind) for(size_t i=0;ok&&i<o->power_count;++i) ok=enable_store(o,actor,o->power+i,false,stores,&count,e);
    if(ok&&regular) ok=select_store(o,actor,&regular->selection,true,stores,&count,e)&&store(o,actor,&regular->points,next->regular.points,stores,&count,e);
    if(ok&&power) {
        if(before.powered.kind!=next->powered.kind) ok=enable_store(o,actor,power,true,stores,&count,e);
        if(ok&&(before.powered.kind!=next->powered.kind||before.powered.cells!=next->powered.cells)) ok=store(o,actor,&power->cells,next->powered.cells,stores,&count,e);
    }
    if(ok) ok=capture(o,actor,stores,count,&after,e);
    double requested_points=regular?next->regular.points:0;
    double requested_cells=power?next->powered.cells:0;
    if(regular&&regular->points.encoding==QA_NATIVE_F32) requested_points=(float)requested_points;
    if(power&&power->cells.encoding==QA_NATIVE_F32) requested_cells=(float)requested_cells;
    bool empty=next->regular.kind==QA_ARMOR_NONE||(regular&&requested_points==0&&!regular->selection.enumeration&&
        regular->selection.field.offset==regular->points.offset&&regular->selection.field.record==regular->points.record);
    if(ok) ok=(after.powered.kind==next->powered.kind&&after.powered.cells==requested_cells&&
        (empty?after.regular.kind==QA_ARMOR_NONE:after.regular.kind==next->regular.kind&&after.regular.item==next->regular.item)&&
        after.regular.points==requested_points)||fail(e,"Native source selections cannot represent the requested armor");
    if(!ok) { free(stores); return false; } *out=stores; *out_count=count; return true;
}
bool application_native_q2_armor_validate(application_native_q2_armor *o,qa_actor_id actor,const qa_armor *next,qa_error *e)
{ armor_store *stores=NULL; size_t count; bool ok=o&&plan(o,actor,next,&stores,&count,e); free(stores); return ok; }
bool application_native_q2_armor_write(application_native_q2_armor *o,qa_actor_id actor,const qa_armor *next,qa_error *e)
{
    armor_store *stores=NULL; size_t count=0; if(!o||!plan(o,actor,next,&stores,&count,e)) return false;
    bool ok=true;
    for(size_t i=0;ok&&i<count;++i) {
        qa_native_address address;
        ok=application_native_q2_field_address(o->options.callbacks,actor,stores[i].field,&address,e)&&address==stores[i].address;
        if(ok) ok=application_native_q2_callbacks_scalar_write(o->options.callbacks,address,stores[i].field->encoding,stores[i].value,e);
        else if(!e||e->code==QA_OK) fail(e,"Native armor store changed its original address");
    }
    free(stores); return ok;
}
static bool watched(void *context,qa_native_instance *instance,const qa_native_write_event *event,qa_error *e)
{
    armor_watch_field *field=context; application_native_q2_armor_watch *watch=field->owner; qa_native_address address;
    if(watch->closing) return true;
    if(instance!=application_native_q2_callbacks_instance(watch->owner->options.callbacks)||
        !application_native_q2_field_address(watch->owner->options.callbacks,watch->actor,field->field,&address,e)||address!=field->address)
        return fail(e,"Native armor watch lost its actual source field");
    (void)event;
    qa_armor after; if(!application_native_q2_armor_read(watch->owner,watch->actor,&after,e)) return false;
    qa_armor before=watch->previous; watch->previous=after;
    if(qa_regular_armor_equal(before.regular,after.regular)&&qa_powered_armor_equal(before.powered,after.powered)) return true;
    ++watch->entered; bool ok=watch->changed(watch->context,&before,&after,e); --watch->entered; return ok;
}
bool application_native_q2_armor_observe(application_native_q2_armor *o,qa_actor_id actor,
    application_native_q2_armor_changed_fn changed,void *context,application_native_q2_armor_watch **out,qa_error *e)
{
    if(!o||!changed||!out||*out) return fail(e,"Native armor observation requires an empty owned scope");
    application_native_q2_armor_watch *watch=calloc(1,sizeof(*watch));
    if(!watch) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining native armor write scope"); return false; }
    watch->owner=o; watch->actor=actor; watch->changed=changed; watch->context=context; *out=watch; ++o->watch_count;
    watch->count=field_count(o); watch->fields=watch->count?calloc(watch->count,sizeof(*watch->fields)):NULL;
    if(watch->count&&!watch->fields) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining native armor watched fields"); return false; }
    if(!application_native_q2_armor_read(o,actor,&watch->previous,e)) return false;
    for(size_t i=0;i<watch->count;++i) {
        armor_watch_field *f=watch->fields+i; f->owner=watch; f->field=field_at(o,i);
        if(!application_native_q2_field_address(o->options.callbacks,actor,f->field,&f->address,e)||
            !qa_native_observe_writes(application_native_q2_callbacks_instance(o->options.callbacks),f->address,
                application_native_q2_field_size(f->field->encoding),watched,f,&f->binding,e)) return false;
    }
    return true;
}
bool application_native_q2_armor_observe_end(application_native_q2_armor_watch **owner,qa_error *e)
{
    if(!owner||!*owner) return true;
    application_native_q2_armor_watch *watch=*owner;
    if(watch->entered) return fail(e,"Native armor observation is entered");
    watch->closing=true;
    if(watch->fields) for(size_t i=watch->count;i>0;--i) {
        armor_watch_field *f=watch->fields+i-1;
        if(f->binding) { if(!qa_native_unobserve_writes(f->binding,e)) return false; f->binding=NULL; }
    }
    --watch->owner->watch_count; free(watch->fields); free(watch); *owner=NULL; return true;
}
bool application_native_q2_armor_observe_cancel(application_native_q2_armor_watch *watch,qa_error *e)
{
    if(!watch) return true;
    if(watch->entered) return fail(e,"Native armor observation is entered");
    watch->closing=true;
    return true;
}
bool application_native_q2_armor_destroy(application_native_q2_armor **owner,qa_error *e)
{
    if(!owner||!*owner) return true;
    application_native_q2_armor *o=*owner;
    if(o->watch_count) return fail(e,"Native armor retains source write scopes");
    if(o->regular) for(size_t i=0;i<o->regular_count;++i) {
        application_native_q2_field_dispose(&o->regular[i].selection.field); application_native_q2_field_dispose(&o->regular[i].points);
    }
    if(o->power) for(size_t i=0;i<o->power_count;++i) {
        application_native_q2_field_dispose(&o->power[i].selection.field); application_native_q2_field_dispose(&o->power[i].cells); application_native_q2_field_dispose(&o->power[i].enabled);
    }
    free(o->regular); free(o->power); free(o); *owner=NULL; return true;
}
