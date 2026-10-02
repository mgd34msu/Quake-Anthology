#include "native_q2_records_private.h"
#include <limits.h>

size_t nqr_scalar_size(qa_native_value_type type)
{
    switch(type) {
    case QA_NATIVE_I8:case QA_NATIVE_U8:return 1;
    case QA_NATIVE_I16:case QA_NATIVE_U16:return 2;
    case QA_NATIVE_I32:case QA_NATIVE_U32:case QA_NATIVE_F32:return 4;
    case QA_NATIVE_I64:case QA_NATIVE_U64:case QA_NATIVE_F64:return 8;
    default:return 0;
    }
}
static qa_native_value_type scalar(const qa_json_document *d,qa_json_id id)
{
    const char *names[]={"void","int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
    for(size_t i=1;i<sizeof(names)/sizeof(*names);++i) if(qa_json_string_equal(d,id,names[i])) return (qa_native_value_type)i;
    return QA_NATIVE_VOID;
}
bool nqr_scalar_encode(double value,qa_native_value_type type,uint8_t *out,qa_error *e)
{
    if(!isfinite(value)) return nqr_fail(e,QA_ERROR_FORMAT,"Native projected scalar is not finite");
    double n=trunc(value);
    switch(type) {
    case QA_NATIVE_I8:if(n<INT8_MIN||n>INT8_MAX) break; out[0]=(uint8_t)(int8_t)n; return true;
    case QA_NATIVE_U8:if(n<0||n>UINT8_MAX) break; out[0]=(uint8_t)n; return true;
    case QA_NATIVE_I16:if(n<INT16_MIN||n>INT16_MAX) break; qa_store_u16le(out,(uint16_t)(int16_t)n); return true;
    case QA_NATIVE_U16:if(n<0||n>UINT16_MAX) break; qa_store_u16le(out,(uint16_t)n); return true;
    case QA_NATIVE_I32:if(n<INT32_MIN||n>INT32_MAX) break; qa_store_u32le(out,(uint32_t)(int32_t)n); return true;
    case QA_NATIVE_U32:if(n<0||n>UINT32_MAX) break; qa_store_u32le(out,(uint32_t)n); return true;
    case QA_NATIVE_I64:if(fabs(n)>9007199254740991.0) break; qa_store_u64le(out,(uint64_t)(int64_t)n); return true;
    case QA_NATIVE_U64:if(n<0||n>9007199254740991.0) break; qa_store_u64le(out,(uint64_t)n); return true;
    case QA_NATIVE_F32:{ float f=(float)value; uint32_t bits; if(!isfinite(f)) break; memcpy(&bits,&f,4); qa_store_u32le(out,bits); return true; }
    case QA_NATIVE_F64:{ uint64_t bits; memcpy(&bits,&value,8); qa_store_u64le(out,bits); return true; }
    default:break;
    }
    return nqr_fail(e,QA_ERROR_FORMAT,"Native projected scalar exceeds its declared encoding");
}
bool nqr_scalar_decode(const uint8_t *bytes,qa_native_value_type type,double *out,qa_error *e)
{
    switch(type) {
    case QA_NATIVE_I8:{ int8_t n; memcpy(&n,bytes,1); *out=n; break; }
    case QA_NATIVE_U8:*out=bytes[0]; break;
    case QA_NATIVE_I16:{ uint16_t u=qa_load_u16le(bytes); int16_t n; memcpy(&n,&u,2); *out=n; break; }
    case QA_NATIVE_U16:*out=qa_load_u16le(bytes); break;
    case QA_NATIVE_I32:*out=qa_load_i32le(bytes); break;
    case QA_NATIVE_U32:*out=qa_load_u32le(bytes); break;
    case QA_NATIVE_I64:{ uint64_t u=qa_load_u64le(bytes); int64_t n; memcpy(&n,&u,8); if(n<INT64_C(-9007199254740991)||n>INT64_C(9007199254740991)) return nqr_fail(e,QA_ERROR_FORMAT,"Native int64 exceeds shared Number precision"); *out=(double)n; break; }
    case QA_NATIVE_U64:{ uint64_t n=qa_load_u64le(bytes); if(n>UINT64_C(9007199254740991)) return nqr_fail(e,QA_ERROR_FORMAT,"Native uint64 exceeds shared Number precision"); *out=(double)n; break; }
    case QA_NATIVE_F32:*out=qa_load_f32le(bytes); break;
    case QA_NATIVE_F64:{ uint64_t bits=qa_load_u64le(bytes); memcpy(out,&bits,8); break; }
    default:return nqr_fail(e,QA_ERROR_FORMAT,"Native field has no scalar representation");
    }
    return isfinite(*out)||nqr_fail(e,QA_ERROR_FORMAT,"Native source wrote a nonfinite scalar");
}
static bool word(const qa_json_document *d,qa_json_id row,const char *key,uint32_t *out,qa_error *e)
{
    uint64_t n;
    if(!qa_json_u64(d,qa_json_get(d,row,key),&n,e)) return false;
    if(n>UINT32_MAX) return nqr_fail(e,QA_ERROR_FORMAT,"Native record extent exceeds its declared word");
    *out=(uint32_t)n; return true;
}
static bool identity(application_native_q2_records *o,qa_json_id id,qa_string_id *out,qa_error *e)
{
    qa_buffer text={0}; if(!qa_json_string(o->document,id,&text,e)) return false;
    bool ok=text.size&&!memchr(text.data,0,text.size)&&qa_strings_intern(o->options.strings,(qa_bytes){text.data,text.size},out,e);
    qa_buffer_free(&text); return ok||nqr_fail(e,QA_ERROR_FORMAT,"Native shared identity is empty or contains NUL");
}
static bool find_record(application_native_q2_records *o,qa_json_id id,size_t *out,qa_error *e)
{
    for(size_t i=0;i<o->record_count;++i) if(qa_json_string_equal(o->document,id,o->records[i].id)) { *out=i; return true; }
    return nqr_fail(e,QA_ERROR_FORMAT,"Native record names an undeclared array");
}
static bool fields(application_native_q2_records *o,nqr_record *r,qa_json_id list,qa_error *e)
{
    const qa_json_document *d=o->document;
    if(qa_json_type(d,list)!=QA_JSON_ARRAY) return nqr_fail(e,QA_ERROR_FORMAT,"Native record fields require their authored array");
    r->field_count=qa_json_size(d,list);
    r->fields=r->field_count?calloc(r->field_count,sizeof(*r->fields)):NULL;
    if(r->field_count&&!r->fields) return nqr_fail(e,QA_ERROR_MEMORY,"Owning native record fields");
    const char *names[]={"health","inventory","inventory-capacity","team","score","origin","velocity","angles","bounds-min","bounds-max","record","address","constant","constant-vector","private"};
    uint8_t pointer_bytes=qa_native_module_describe(qa_native_get_module(o->options.instance)).image.target.pointer_bytes;
    for(size_t i=0;i<r->field_count;++i) {
        nqr_field *f=r->fields+i; qa_json_id row=qa_json_at(d,list,i),binding=qa_json_get(d,row,"binding");
        size_t k=0; for(;k<sizeof(names)/sizeof(*names);++k) if(qa_json_string_equal(d,binding,names[k])) break;
        if(k==sizeof(names)/sizeof(*names)||!word(d,row,"offset",&f->offset,e)) return nqr_fail(e,QA_ERROR_FORMAT,"Unknown native record field binding");
        f->kind=(nqr_kind)k; f->writable=k<=NQR_MAX;
        f->length=(k>=NQR_ORIGIN&&k<=NQR_MAX)||k==NQR_VECTOR?12:pointer_bytes;
        if(k<=NQR_SCORE||k==NQR_CONSTANT) { f->encoding=scalar(d,qa_json_get(d,row,"encoding")); f->length=(uint32_t)nqr_scalar_size(f->encoding); }
        if(k==NQR_PRIVATE&&!word(d,row,"byteLength",&f->length,e)) return false;
        if(!f->length||f->offset>r->stride||f->length>r->stride-f->offset) return nqr_fail(e,QA_ERROR_FORMAT,"Native field exceeds its actual declared record");
        for(size_t j=0;j<i;++j) if(f->offset<r->fields[j].offset+r->fields[j].length&&r->fields[j].offset<f->offset+f->length)
            return nqr_fail(e,QA_ERROR_FORMAT,"Native declared field byte extents overlap");
        if((k==NQR_COUNT||k==NQR_CAPACITY)&&!identity(o,qa_json_get(d,row,"item"),&f->item,e)) return false;
        if(k==NQR_LINK&&!find_record(o,qa_json_get(d,row,"record"),&f->target,e)) return false;
        if(k==NQR_ADDRESS) f->address=qa_json_get(d,row,"value");
        if(k==NQR_CONSTANT) { double n; if(!qa_json_number(d,qa_json_get(d,row,"value"),&n,e)||!nqr_scalar_encode(n,f->encoding,f->initial,e)) return false; }
        if(k==NQR_VECTOR) { const char *axes[]={"x","y","z"}; qa_json_id v=qa_json_get(d,row,"value"); for(size_t j=0;j<3;++j) { double n; if(!qa_json_number(d,qa_json_get(d,v,axes[j]),&n,e)||!nqr_scalar_encode(n,QA_NATIVE_F32,f->initial+j*4,e)) return false; } }
        if(k==NQR_TEAM) {
            qa_json_id values=qa_json_get(d,row,"values"); f->team_count=qa_json_size(d,values);
            if(qa_json_type(d,values)!=QA_JSON_ARRAY||!f->team_count) return nqr_fail(e,QA_ERROR_FORMAT,"Native team requires authored source aliases");
            f->teams=calloc(f->team_count,sizeof(*f->teams)); if(!f->teams) return nqr_fail(e,QA_ERROR_MEMORY,"Owning native team aliases");
            for(size_t j=0;j<f->team_count;++j) {
                qa_json_id value=qa_json_at(d,values,j),team=qa_json_get(d,value,"team"); uint8_t encoded[8];
                if(!qa_json_number(d,qa_json_get(d,value,"value"),&f->teams[j].value,e)||!nqr_scalar_encode(f->teams[j].value,f->encoding,encoded,e)||
                    (qa_json_type(d,team)!=QA_JSON_NULL&&!identity(o,team,&f->teams[j].team,e))) return false;
                for(size_t n=0;n<j;++n) if(f->teams[n].value==f->teams[j].value||f->teams[n].team==f->teams[j].team)
                    return nqr_fail(e,QA_ERROR_FORMAT,"Native team aliases duplicate an identity");
            }
        }
    }
    return true;
}
static bool pose_field(application_native_q2_records *o,qa_json_id value,nqr_pose_field *out,qa_error *e)
{
    const qa_json_document *d=o->document;
    if(!find_record(o,qa_json_get(d,value,"record"),&out->record,e)||!word(d,value,"offset",&out->offset,e)) return false;
    out->encoding=scalar(d,qa_json_get(d,value,"encoding")); size_t length=nqr_scalar_size(out->encoding);
    nqr_record *r=o->records+out->record;
    if(!length||(!r->client&&out->record!=o->entity_record)||out->offset>r->stride||length>r->stride-out->offset)
        return nqr_fail(e,QA_ERROR_FORMAT,"Native pose has no exclusive declared client storage");
    for(size_t i=0;i<r->field_count;++i) { nqr_field *f=r->fields+i;
        if(f->kind<=NQR_MAX&&out->offset<f->offset+f->length&&f->offset<out->offset+length)
            return nqr_fail(e,QA_ERROR_FORMAT,"Native pose overlaps a canonical shared field");
    }
    return true;
}
bool nqr_profile(application_native_q2_records *o,qa_error *e)
{
    const qa_json_document *d=o->document; qa_json_id root=qa_json_root(d),list=qa_json_get(d,root,"actorRecords");
    if(qa_json_type(d,list)!=QA_JSON_ARRAY) return nqr_fail(e,QA_ERROR_FORMAT,"Native actor projection requires its acquired record declaration");
    o->record_count=qa_json_size(d,list); o->entity_record=SIZE_MAX;
    o->records=o->record_count?calloc(o->record_count,sizeof(*o->records)):NULL;
    if(o->record_count&&!o->records) return nqr_fail(e,QA_ERROR_MEMORY,"Owning native declared record roster");
    for(size_t i=0;i<o->record_count;++i) {
        nqr_record *r=o->records+i; qa_json_id row=qa_json_at(d,list,i); qa_buffer id={0};
        if(!qa_json_string(d,qa_json_get(d,row,"id"),&id,e)) return false;
        if(!id.size||memchr(id.data,0,id.size)) { qa_buffer_free(&id); return nqr_fail(e,QA_ERROR_FORMAT,"Native record identity is empty"); }
        r->id=(char *)id.data;
        for(size_t j=0;j<i;++j) if(!strcmp(r->id,o->records[j].id)) return nqr_fail(e,QA_ERROR_FORMAT,"Duplicate native record identity");
        if(!word(d,row,"stride",&r->stride,e)||r->stride<4||!word(d,row,"firstSlot",&r->first,e)||
            !word(d,row,"capacity",&r->capacity,e)||!r->capacity||r->capacity>65536) return nqr_fail(e,QA_ERROR_FORMAT,"Invalid native declared record extent");
        qa_json_id base=qa_json_get(d,row,"base"),kind=qa_json_get(d,base,"kind");
        r->entities=qa_json_string_equal(d,kind,"entities");
        if(!r->entities&&!qa_json_string_equal(d,kind,"clients")&&!qa_json_string_equal(d,kind,"address")) return nqr_fail(e,QA_ERROR_FORMAT,"Unknown native record base");
    }
    qa_json_id clients=qa_json_get(d,root,"clients"),client_records=qa_json_get(d,clients,"records");
    if(clients!=QA_JSON_NONE) {
        if(!word(d,clients,"maximum",&o->client_maximum,e)||!o->client_maximum||o->client_maximum>256||
            qa_json_type(d,client_records)!=QA_JSON_ARRAY||!qa_json_size(d,client_records)) return nqr_fail(e,QA_ERROR_FORMAT,"Native clients have no declared record reservation");
        for(size_t i=0;i<qa_json_size(d,client_records);++i) { size_t at; if(!find_record(o,qa_json_at(d,client_records,i),&at,e)) return false; nqr_record *r=o->records+at;
            if(r->client||r->entities) return nqr_fail(e,QA_ERROR_FORMAT,"Native client arrays duplicate or use entity storage");
            r->client=true;
        }
    }
    qa_json_id entity=qa_json_get(d,root,"entityRecord");
    if(qa_json_type(d,entity)!=QA_JSON_NULL&&!find_record(o,entity,&o->entity_record,e)) return false;
    if(o->entity_record!=SIZE_MAX&&!o->records[o->entity_record].entities) return nqr_fail(e,QA_ERROR_FORMAT,"Native entity record is not the genuine public export table");
    if(o->client_maximum&&(o->entity_record==SIZE_MAX||o->records[o->entity_record].first!=1)) return nqr_fail(e,QA_ERROR_FORMAT,"Native client entities require their actual source first slot");
    for(size_t i=0;i<o->record_count;++i) {
        nqr_record *r=o->records+i; qa_json_id row=qa_json_at(d,list,i),base=qa_json_get(d,row,"base");
        if(r->capacity<o->client_maximum||
            (qa_json_string_equal(d,qa_json_get(d,base,"kind"),"clients")&&(!r->client||r->first||r->capacity!=o->client_maximum)))
            return nqr_fail(e,QA_ERROR_FORMAT,"Native declared client capacity differs from its private storage");
        if(!fields(o,r,qa_json_get(d,row,"fields"),e)) return false;
        for(size_t j=0;j<r->field_count;++j) { nqr_field *f=r->fields+j; if(f->kind>NQR_MAX) continue;
            for(size_t n=0;n<=i;++n) for(size_t m=0;m<o->records[n].field_count;++m) { nqr_field *p=o->records[n].fields+m;
                if(p==f) break;
                if(p->kind==f->kind&&((f->kind!=NQR_COUNT&&f->kind!=NQR_CAPACITY)||p->item==f->item)) return nqr_fail(e,QA_ERROR_FORMAT,"Native canonical field has multiple private authorities");
            }
        }
    }
    qa_json_id outputs=qa_json_get(d,clients,"outputs");
    for(size_t i=0;i<qa_json_size(d,outputs);++i) {
        qa_json_id output=qa_json_at(d,outputs,i); if(!qa_json_string_equal(d,qa_json_get(d,output,"kind"),"body-shape")) continue;
        const char *ends[]={"min","max"};
        for(size_t j=0;j<2;++j) { qa_json_id endpoint=qa_json_get(d,output,ends[j]); size_t at; uint32_t offset;
            if(!find_record(o,qa_json_get(d,endpoint,"record"),&at,e)||!word(d,endpoint,"offset",&offset,e)) return false;
            bool found=false; nqr_record *r=o->records+at;
            for(size_t k=0;k<r->field_count;++k) if(r->fields[k].offset==offset&&r->fields[k].kind==(j?NQR_MAX:NQR_MIN)) { r->fields[k].body_output=true; found=true; }
            if(!found) return nqr_fail(e,QA_ERROR_FORMAT,"Native client body output has no authored bounds field");
        }
    }
    if(o->record_count&&(!qa_json_size(d,qa_json_get(d,root,"project"))||!qa_json_size(d,qa_json_get(d,root,"release"))))
        return nqr_fail(e,QA_ERROR_FORMAT,"Native projection requires authored PROJECT and RELEASE calls");
    qa_json_id pose=qa_json_get(d,clients,"pose");
    if(pose!=QA_JSON_NONE) {
        qa_json_id crouched=qa_json_get(d,pose,"crouched");
        if(!o->options.pose||!pose_field(o,qa_json_get(d,pose,"viewHeight"),&o->view_height,e)||
            !pose_field(o,qa_json_get(d,crouched,"field"),&o->crouch,e)||!word(d,crouched,"mask",&o->crouch_mask,e)||
            !o->crouch_mask||o->crouch_mask>INT32_MAX) return nqr_fail(e,QA_ERROR_FORMAT,"Native pose requires its real admitted control view and source mask");
        o->has_pose=true;
    }
    return true;
}
