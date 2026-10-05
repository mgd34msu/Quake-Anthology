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
static bool add_pair(qa_q1_save_record *record,const char *key,const char *value,qa_error *error)
{
    if (record->count>=SIZE_MAX/sizeof(*record->pairs))
        return qc_fail(error,QA_ERROR_MEMORY,0,"Source field count overflows");
    qa_q1_save_pair pair={qc_strdup(key,error),NULL};
    if (pair.key) pair.value=qc_strdup(value,error);
    if (!pair.key || !pair.value) { free(pair.key); free(pair.value); return false; }
    qa_q1_save_pair *next=realloc(record->pairs,(record->count+1)*sizeof(*next));
    if (!next) { free(pair.key); free(pair.value); return qc_fail(error,QA_ERROR_MEMORY,0,"Allocating source fields"); }
    record->pairs=next; next[record->count++]=pair; return true;
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
        const qa_qc_definition *flags=qa_qc_program_find_field(vm->program,"flags");
        uint32_t bits=0;
        if (flags) {
            if (flags->type!=QA_QC_FLOAT || flags->offset>=vm->layout.field_words)
                return qc_fail(error,QA_ERROR_FORMAT,slot,"Saved flags field has an invalid source definition");
            float value=qc_load_float(qc_entity_words_const(vm,slot),flags->offset);
            if (!isfinite(value)) return qc_fail(error,QA_ERROR_FORMAT,slot,"Saved body flags are nonfinite");
            bits=(uint32_t)qa_source_float_to_i32(value);
        }
        if ((bits&512u)!=0 && target && vm->slots[target].kind!=QA_QC_SLOT_FREE)
            body.ground=vm->slots[target].actor;
    }
    if (body.bounds.mins.x>body.bounds.maxs.x || body.bounds.mins.y>body.bounds.maxs.y ||
        body.bounds.mins.z>body.bounds.maxs.z)
        return qc_fail(error,QA_ERROR_FORMAT,slot,"Saved body bounds are inverted");
    *out=body; return true;
}
static bool saved_value(const qa_qc_instance *vm,const uint8_t *words,
    const qa_qc_definition *definition,qa_q1_save_record *out,qa_error *error)
{
    char buffer[192]; const char *value=buffer; int32_t raw=qc_load_int(words,definition->offset);
    switch (definition->type) {
    case QA_QC_STRING:
        if (!qa_qc_string(vm,raw,&value,error)) return false;
        break;
    case QA_QC_FLOAT:
        if (!isfinite(qc_load_float(words,definition->offset)) ||
            !qa_format_fixed(qc_load_float(words,definition->offset),6,buffer,sizeof(buffer),error))
            return qc_fail(error,QA_ERROR_FORMAT,definition->offset,"Nonfinite source text float");
        break;
    case QA_QC_VECTOR: {
        char component[3][64];
        for (uint32_t i=0;i<3;++i) {
            float f=qc_load_float(words,definition->offset+i);
            if (!isfinite(f) || !qa_format_fixed(f,6,component[i],sizeof(component[i]),error))
                return qc_fail(error,QA_ERROR_FORMAT,definition->offset+i,"Nonfinite source text vector");
        }
        (void)snprintf(buffer,sizeof(buffer),"%s %s %s",component[0],component[1],component[2]); break;
    }
    case QA_QC_ENTITY: {
        uint32_t slot;
        if (!qc_entity_slot(vm,raw,&slot,error)) return false;
        (void)snprintf(buffer,sizeof(buffer),"%u",slot); break;
    }
    case QA_QC_FUNCTION: {
        const qa_qc_function *function=raw>=0?qa_qc_program_function(vm->program,(uint32_t)raw):NULL;
        if (!function) return qc_fail(error,QA_ERROR_FORMAT,definition->offset,"Cannot save unknown QC function");
        value=function->name; break;
    }
    case QA_QC_FIELD: {
        const qa_qc_definition *field=NULL;
        for (uint32_t i=0;i<vm->program->info.field_count;++i)
            if ((int32_t)vm->program->fields[i].offset==raw) { field=vm->program->fields+i; break; }
        if (!field) return qc_fail(error,QA_ERROR_FORMAT,definition->offset,"Cannot save unknown QC field offset");
        value=field->name; break;
    }
    case QA_QC_VOID: value="void"; break;
    default: return qc_fail(error,QA_ERROR_UNSUPPORTED,definition->offset,"QC type requires raw checkpoint storage");
    }
    return add_pair(out,definition->name,value,error);
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
        size_t size=strlen(text); char *decoded=malloc(size+1);
        if (!decoded) return qc_fail(error,QA_ERROR_MEMORY,offset,"Decoding QC source string");
        size_t used=0;
        for (size_t i=0;i<size;++i) {
            if (text[i]=='\\') { ++i; decoded[used++]=i<size && text[i]=='n'?'\n':'\\'; }
            else decoded[used++]=text[i];
        }
        decoded[used]=0; int32_t id;
        bool ok=qa_qc_string_allocate(vm,decoded,&id,error); free(decoded);
        if (!ok) return false;
        qc_store_word(words,offset,(uint32_t)id); break;
    }
    case QA_QC_FLOAT:
        qc_store_float(words,offset,(float)qa_parse_quake_number(text,
            QA_QUAKE_NUMBER_ASCII_UNSIGNED)); break;
    case QA_QC_VECTOR: {
        const char *start=text;
        for (uint32_t i=0;i<3;++i) {
            const char *end=strchr(start,' '); size_t size=end?(size_t)(end-start):strlen(start);
            char *part=malloc(size+1);
            if (!part) return qc_fail(error,QA_ERROR_MEMORY,offset,"Decoding QC source vector");
            memcpy(part,start,size); part[size]=0;
            qc_store_float(words,offset+i,(float)qa_parse_quake_number(part,
                QA_QUAKE_NUMBER_ASCII_UNSIGNED)); free(part);
            start=end?end+1:start+size;
        }
        break;
    }
    case QA_QC_ENTITY: {
        double slot=trunc(qa_parse_quake_number(text, QA_QUAKE_NUMBER_ASCII_UNSIGNED));
        if (!isfinite(slot) || slot<0 || slot>=vm->entity_count)
            return qc_fail(error,QA_ERROR_FORMAT,offset,"Saved entity reference exceeds source count");
        qc_store_word(words,offset,(uint32_t)((uint64_t)slot*vm->layout.stride_bytes)); break;
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
static bool pairs(qa_qc_instance *vm,uint8_t *words,const qa_q1_save_record *record,bool entity,qa_error *error)
{
    if (record->count && !record->pairs) return qc_fail(error,QA_ERROR_FORMAT,0,"Missing source field pairs");
    for (size_t i=0;i<record->count;++i) {
        const qa_q1_save_pair *pair=record->pairs+i;
        if (!pair->key || !pair->value) return qc_fail(error,QA_ERROR_FORMAT,i,"Missing source field text");
        bool angle=entity && !strcmp(pair->key,"angle");
        const char *key=angle?"angles":entity && !strcmp(pair->key,"light")?"light_lev":pair->key;
        char *normalized=NULL;
        if (entity) {
            size_t size=strlen(key); while (size && key[size-1]==' ') --size;
            normalized=malloc(size+1);
            if (!normalized) return qc_fail(error,QA_ERROR_MEMORY,i,"Normalizing source field name");
            memcpy(normalized,key,size); normalized[size]=0; key=normalized;
        }
        const qa_qc_definition *d=entity?(*key=='_'?NULL:qa_qc_program_find_field(vm->program,key)):
            qa_qc_program_find_global(vm->program,key);
        free(normalized);
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
    if (!text_idle(vm,error)) return false;
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
        qc_store_float(edict,vm->layout.variables_offset_bytes/4-1,free_row?(float)save->time:0);
        if (!pairs(vm,words,save->entities+slot,true,error)) return false;
    }
    return true;
}
