/* Quake pr_edict.c source text field semantics. */
#include "internal.h"
#include "qa/qc_text_save.h"
#include "qa/text.h"
#include <math.h>
#include <stdio.h>

static bool text_idle(const qa_qc_instance *vm,qa_error *error)
{
    return vm && qa_qc_idle(vm) && vm->program->info.api==QA_QC_API_NETQUAKE ? true :
        qc_fail(error,QA_ERROR_ARGUMENT,0,"Original text fields require an idle NetQuake instance");
}
static bool raw_vector(const qa_qc_instance *vm,uint32_t slot,const char *name,
    qa_vec3 *out,qa_error *error)
{
    const qa_qc_definition *field=qa_qc_program_find_field(vm->program,name);
    *out=qa_v3(0,0,0);
    if (!field) return true;
    if (field->type!=QA_QC_VECTOR || (uint32_t)field->offset+3>vm->layout.field_words)
        return qc_fail(error,QA_ERROR_FORMAT,slot,"Saved body field has an invalid source definition");
    const uint8_t *words=qc_entity_words_const(vm,slot);
    *out=qa_v3(qc_load_float(words,field->offset),qc_load_float(words,field->offset+1),
        qc_load_float(words,field->offset+2));
    return qa_vec_finite(*out) || qc_fail(error,QA_ERROR_FORMAT,slot,"Saved body vector is nonfinite");
}
bool qa_qc_text_body_read(const qa_qc_instance *vm,uint32_t slot,qa_body_state *out,qa_error *error)
{
    if (!text_idle(vm,error)) return false;
    if (!out || !slot || slot>=vm->entity_count ||
        (vm->slots[slot].kind!=QA_QC_SLOT_OWNED && vm->slots[slot].kind!=QA_QC_SLOT_BORROWED))
        return qc_fail(error,QA_ERROR_ARGUMENT,slot,"Saved body requires its actual imported actor row");
    qa_body_state body={0};
    if (!raw_vector(vm,slot,"origin",&body.origin,error) ||
        !raw_vector(vm,slot,"angles",&body.angles,error) ||
        !raw_vector(vm,slot,"velocity",&body.velocity,error) ||
        !raw_vector(vm,slot,"mins",&body.bounds.mins,error) ||
        !raw_vector(vm,slot,"maxs",&body.bounds.maxs,error)) return false;
    const qa_qc_definition *ground=qa_qc_program_find_field(vm->program,"groundentity");
    if (ground) {
        if (ground->type!=QA_QC_ENTITY || ground->offset>=vm->layout.field_words)
            return qc_fail(error,QA_ERROR_FORMAT,slot,"Saved ground field has an invalid source definition");
        int32_t reference=qc_load_int(qc_entity_words_const(vm,slot),ground->offset);
        uint32_t target;
        if (!qc_entity_slot(vm,reference,&target,error)) return false;
        body.ground = vm->slots[target].kind == QA_QC_SLOT_BORROWED ?
            qa_actor_reference_lifetime(vm->slots[target].actor) :
            qa_actor_reference_source(vm->options.host.owner, target);
    }
    if (body.bounds.mins.x>body.bounds.maxs.x || body.bounds.mins.y>body.bounds.maxs.y ||
        body.bounds.mins.z>body.bounds.maxs.z)
        return qc_fail(error,QA_ERROR_FORMAT,slot,"Saved body bounds are inverted");
    *out=body; return true;
}
static bool saved_value(const qa_qc_instance *vm,const uint8_t *words,
    const qa_qc_definition *definition,qa_q1_save_record *out,qa_error *error)
{
    qa_q1_save_value value={0}; int32_t raw=qc_load_int(words,definition->offset);
    switch (definition->type) {
    case QA_QC_STRING:
        value.kind=QA_Q1_SAVE_STRING;
        if (!qa_qc_string(vm,raw,&value.value.text,error)) return false;
        break;
    case QA_QC_FLOAT:
        value.kind=QA_Q1_SAVE_FLOAT; value.value.number=qc_load_float(words,definition->offset); break;
    case QA_QC_VECTOR:
        value.kind=QA_Q1_SAVE_VECTOR;
        value.value.vector=qa_v3(qc_load_float(words,definition->offset),
            qc_load_float(words,definition->offset+1),qc_load_float(words,definition->offset+2)); break;
    case QA_QC_ENTITY:
        value.kind=QA_Q1_SAVE_ENTITY;
        if (!qc_entity_slot(vm,raw,&value.value.entity,error)) return false;
        break;
    case QA_QC_FUNCTION: {
        const qa_qc_function *function=raw>=0?qa_qc_program_function(vm->program,(uint32_t)raw):NULL;
        if (!function) return qc_fail(error,QA_ERROR_FORMAT,definition->offset,"Cannot save unknown QC function");
        value.kind=QA_Q1_SAVE_FUNCTION; value.value.text=function->name; break;
    }
    case QA_QC_FIELD: {
        const qa_qc_definition *field=NULL;
        for (uint32_t i=0;i<vm->program->info.field_count;++i)
            if ((int32_t)vm->program->fields[i].offset==raw) { field=vm->program->fields+i; break; }
        if (!field) return qc_fail(error,QA_ERROR_FORMAT,definition->offset,"Cannot save unknown QC field offset");
        value.kind=QA_Q1_SAVE_FIELD; value.value.text=field->name; break;
    }
    case QA_QC_VOID: value.kind=QA_Q1_SAVE_VOID; break;
    default: return qc_fail(error,QA_ERROR_UNSUPPORTED,definition->offset,"QC type requires shared state storage");
    }
    return qa_q1_save_record_value(out,definition->name,&value,error);
}
bool qa_qc_text_capture(const qa_qc_instance *vm,qa_q1_save_record *globals,
    qa_q1_save_record **entities,size_t *count,qa_error *error)
{
    if (!text_idle(vm,error)) return false;
    if (!globals || globals->count || globals->pairs || !entities || *entities || !count || *count)
        return qc_fail(error,QA_ERROR_ARGUMENT,0,"Source field capture requires empty outputs");
    qa_q1_save_record saved_globals={0};
    qa_q1_save_record *saved_entities=calloc(vm->entity_count,sizeof(*saved_entities));
    if (!saved_entities) return qc_fail(error,QA_ERROR_MEMORY,0,"Allocating source entity records");
    bool ok=true;
    for (uint32_t i=0;ok && i<vm->program->info.global_count;++i) {
        const qa_qc_definition *d=vm->program->globals+i;
        if (d->save && (d->type==QA_QC_STRING || d->type==QA_QC_FLOAT || d->type==QA_QC_ENTITY))
            ok=saved_value(vm,vm->globals,d,&saved_globals,error);
    }
    for (uint32_t slot=0;ok && slot<vm->entity_count;++slot) {
        if (qa_load_u32le(vm->entities+(size_t)slot*vm->layout.stride_bytes)) continue;
        const uint8_t *words=qc_entity_words_const(vm,slot);
        for (uint32_t i=1;ok && i<vm->program->info.field_count;++i) {
            const qa_qc_definition *d=vm->program->fields+i; size_t length=strlen(d->name);
            if (length>=2 && d->name[length-2]=='_') continue;
            uint32_t width=d->type==QA_QC_VECTOR?3:1;
            if ((uint32_t)d->offset+width>vm->layout.field_words) {
                ok=qc_fail(error,QA_ERROR_FORMAT,d->offset,"Source field exceeds its actual entity words"); break;
            }
            bool nonzero=false;
            for (uint32_t j=0;j<width;++j) nonzero=nonzero || qc_load_word(words,d->offset+j)!=0;
            if (nonzero) ok=saved_value(vm,words,d,saved_entities+slot,error);
        }
    }
    if (!ok) {
        qa_q1_save_record_destroy(&saved_globals);
        for (uint32_t i=0;i<vm->entity_count;++i) qa_q1_save_record_destroy(saved_entities+i);
        free(saved_entities); return false;
    }
    *globals=saved_globals; *entities=saved_entities; *count=vm->entity_count; return true;
}
static bool parse_value(qa_qc_instance *vm,uint8_t *words,
    const qa_qc_definition *d,const char *text,qa_error *error)
{
    uint32_t offset=d->offset;
    switch (d->type) {
    case QA_QC_STRING: {
        char *decoded=NULL;
        if (!qa_q1_save_string_decode(text,&decoded,error)) return false;
        int32_t id;
        bool ok=qa_qc_string_allocate(vm,decoded,&id,error); free(decoded);
        if (!ok) return false;
        qc_store_word(words,offset,(uint32_t)id); break;
    }
    case QA_QC_FLOAT: {
        double value;
        if (!qa_parse_atof(text,&value,error)) return false;
        qc_store_float(words,offset,(float)value);break;
    }
    case QA_QC_VECTOR: {
        qa_vec3 value;
        if (!qa_q1_save_vector_decode(text,&value,error)) return false;
        qc_store_float(words,offset,value.x);qc_store_float(words,offset+1,value.y);
        qc_store_float(words,offset+2,value.z);break;
    }
    case QA_QC_ENTITY: {
        uint32_t slot;
        if (!qa_q1_save_entity_decode(text,&slot,error)) return false;
        if (slot>=vm->entity_count)
            return qc_fail(error,QA_ERROR_FORMAT,offset,"Saved entity reference exceeds source count");
        qc_store_word(words,offset,(uint32_t)((uint64_t)slot*vm->layout.stride_bytes));break;
    }
    case QA_QC_FUNCTION: {
        uint32_t index;
        if (!qa_qc_program_find_function(vm->program,text,&index))
            return qc_fail(error,QA_ERROR_FORMAT,offset,"Saved QC function is absent from the selected program");
        qc_store_word(words,offset,index); break;
    }
    case QA_QC_FIELD: {
        const qa_qc_definition *field=qa_qc_program_find_field(vm->program,text);
        if (!field) return qc_fail(error,QA_ERROR_FORMAT,offset,"Saved QC field is absent from the selected program");
        if (!qc_global_range(vm,field->offset,1,error)) return false;
        qc_store_word(words,offset,qc_load_word(vm->globals,field->offset)); break;
    }
    case QA_QC_VOID: break;
    default: return qc_fail(error,QA_ERROR_UNSUPPORTED,offset,"QC type requires raw checkpoint storage");
    }
    return true;
}
static const qa_qc_definition *saved_definition(const qa_qc_program *program,
    const char *name,bool entity,bool *angle,qa_error *error)
{
    *angle=entity && !strcmp(name,"angle");
    const char *key=*angle?"angles":entity && !strcmp(name,"light")?"light_lev":name;
    char *normalized=NULL;
    if (entity) {
        size_t size=strlen(key); while (size && key[size-1]==' ') --size;
        normalized=malloc(size+1);
        if (!normalized) { qc_fail(error,QA_ERROR_MEMORY,0,"Normalizing source field name"); return NULL; }
        memcpy(normalized,key,size); normalized[size]=0; key=normalized;
    }
    const qa_qc_definition *definition=entity?(*key=='_'?NULL:qa_qc_program_find_field(program,key)):
        qa_qc_program_find_global(program,key);
    free(normalized);return definition;
}
static bool record_ready(const qa_qc_program *program,const qa_q1_save_record *record,
    bool entity,qa_error *error)
{
    if (!record || (record->count && !record->pairs))
        return qc_fail(error,QA_ERROR_FORMAT,0,"Missing source field pairs");
    for (size_t i=0;i<record->count;++i) {
        const qa_q1_save_pair *pair=record->pairs+i;
        if (!pair->key || !pair->value) return qc_fail(error,QA_ERROR_FORMAT,i,"Missing source field text");
        if (entity && *pair->key=='_') continue;
        bool angle=false; qa_error local={0};
        const qa_qc_definition *d=saved_definition(program,pair->key,entity,&angle,&local);
        if (!d) {
            if (local.code!=QA_OK) { if (error) *error=local;return false; }
            qa_error_set(error,QA_ERROR_FORMAT,i,"Saved %s '%s' is absent from this original program",
                entity?"field":"global",pair->key);return false;
        }
        if (d->type==QA_QC_FUNCTION && !qa_qc_program_find_function(program,pair->value,NULL))
            return qc_fail(error,QA_ERROR_FORMAT,i,"Saved function is absent from this original program");
        if (d->type==QA_QC_FIELD && !qa_qc_program_find_field(program,pair->value))
            return qc_fail(error,QA_ERROR_FORMAT,i,"Saved field reference is absent from this original program");
    }
    return true;
}
bool qa_qc_text_program_ready(const qa_qc_program *program,const qa_q1_save_data *save,qa_error *error)
{
    if (!program || !save || (save->entity_count && !save->entities) ||
        qa_qc_program_describe(program).api!=QA_QC_API_NETQUAKE)
        return qc_fail(error,QA_ERROR_ARGUMENT,0,"Original save requires its actual NetQuake program");
    if (!record_ready(program,&save->globals,false,error)) return false;
    qa_qc_program_info info=qa_qc_program_describe(program);
    for (uint32_t i=0;i<info.global_count;++i) {
        const qa_qc_definition *definition=qa_qc_program_global(program,i);
        if (!definition->save || (definition->type!=QA_QC_STRING && definition->type!=QA_QC_FLOAT &&
            definition->type!=QA_QC_ENTITY)) continue;
        bool present=false;
        for (size_t j=0;j<save->globals.count;++j)
            if (!strcmp(save->globals.pairs[j].key,definition->name)) { present=true;break; }
        if (!present) {
            qa_error_set(error,QA_ERROR_FORMAT,i,"Original save lacks this program's saved global '%s'",definition->name);
            return false;
        }
    }
    for (size_t i=0;i<save->entity_count;++i)
        if (!record_ready(program,save->entities+i,true,error)) return false;
    return true;
}
static bool pairs(qa_qc_instance *vm,uint8_t *words,const qa_q1_save_record *record,bool entity,qa_error *error)
{
    if (record->count && !record->pairs) return qc_fail(error,QA_ERROR_FORMAT,0,"Missing source field pairs");
    for (size_t i=0;i<record->count;++i) {
        const qa_q1_save_pair *pair=record->pairs+i;
        if (!pair->key || !pair->value) return qc_fail(error,QA_ERROR_FORMAT,i,"Missing source field text");
        bool angle=false; qa_error local={0};
        const qa_qc_definition *d=saved_definition(vm->program,pair->key,entity,&angle,&local);
        if (local.code!=QA_OK) { if (error) *error=local;return false; }
        if (!d) continue;
        uint32_t width=d->type==QA_QC_VECTOR?3:1;
        if (entity ? (uint32_t)d->offset+width>vm->layout.field_words : !qc_global_range(vm,d->offset,width,error))
            return qc_fail(error,QA_ERROR_FORMAT,d->offset,"Saved source definition exceeds its actual words");
        const char *value=pair->value; char *angle_text=NULL;
        if (angle) {
            size_t size=strlen(value);
            if (size>SIZE_MAX-5) return qc_fail(error,QA_ERROR_MEMORY,i,"Source angle text overflows");
            angle_text=malloc(size+5);
            if (!angle_text) return qc_fail(error,QA_ERROR_MEMORY,i,"Decoding source angle");
            memcpy(angle_text,"0 ",2); memcpy(angle_text+2,value,size); memcpy(angle_text+2+size," 0",3); value=angle_text;
        }
        bool ok=parse_value(vm,words,d,value,error); free(angle_text);
        if (!ok) return false;
    }
    return true;
}
bool qa_qc_text_import(qa_qc_instance *vm,const qa_q1_save_data *save,qa_error *error)
{
    if (!text_idle(vm,error) || !qa_qc_text_program_ready(vm->program,save,error)) return false;
    if (!save || !save->entities || save->entity_count<vm->options.first_dynamic_slot || save->entity_count>vm->options.entity_capacity ||
        !isfinite(save->time) || save->time<0)
        return qc_fail(error,QA_ERROR_FORMAT,0,"Saved source count/time exceeds candidate admission");
    for (uint32_t slot=0;slot<vm->entity_count || slot<save->entity_count;++slot) {
        bool live=slot<save->entity_count && save->entities[slot].count!=0;
        const qc_slot *binding=vm->slots+slot;
        if (live ? binding->kind==QA_QC_SLOT_FREE : binding->kind!=QA_QC_SLOT_FREE)
            return qc_fail(error,QA_ERROR_ARGUMENT,slot,"Host has not prepared the actual saved actor topology");
    }
    vm->entity_count=(uint32_t)save->entity_count;
    if (!pairs(vm,vm->globals,&save->globals,false,error)) return false;
    for (uint32_t slot=0;slot<vm->entity_count;++slot) {
        uint8_t *words=qc_entity_words(vm,slot);
        memset(words,0,(size_t)vm->layout.field_words*4);
        bool free_row=save->entities[slot].count==0;
        uint8_t *edict=vm->entities+(size_t)slot*vm->layout.stride_bytes;
        qa_store_u32le(edict,free_row?1:0);
        qc_store_float(edict,vm->layout.variables_offset_bytes/4-1,0);
        if (!pairs(vm,words,save->entities+slot,true,error)) return false;
    }
    return true;
}
