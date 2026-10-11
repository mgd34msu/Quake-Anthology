#include "guest_native_q2_private.h"
#include "guest_qc_profile.h"
#include "native_q2_client_outputs.h"
#include "native_q2_client_stages.h"
#include "native_q2_armor.h"
#include <math.h>
#include <float.h>

typedef struct native_output_declaration {
    application_client_output_channel channel;
    application_native_q2_field first,second;
    qa_json_id values;
    uint32_t mask;
    bool height,masked;
} native_output_declaration;
typedef struct native_output_publication {
    qa_actor_id actor;
    application_client_outputs values;
    bool published;
} native_output_publication;
struct application_native_q2_client_outputs {
    struct application_native_q2 *engine;
    native_output_declaration fields[APPLICATION_CLIENT_OUTPUT_COUNT];
    size_t count;
    uint8_t channels,claimed;
    bool ready,restoring;
    native_output_publication clients[257];
};
static bool fail(qa_error *e,const char *text)
{return application_fail(e,QA_ERROR_FORMAT,text);}
static bool field_parse(struct application_native_q2 *n,qa_json_id id,bool vector,
    application_native_q2_field *out,qa_error *e)
{
    if(!vector)return application_native_q2_field_parse(n->callbacks,id,out,e);
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_buffer name={0};uint64_t offset;
    if(!qa_json_string(d,qa_json_get(d,id,"record"),&name,e))return false;
    bool ok=name.size&&!memchr(name.data,0,name.size)&&
        qa_json_u64(d,qa_json_get(d,id,"offset"),&offset,e)&&offset<=UINT32_MAX;
    qa_json_id records=qa_json_get(d,qa_json_root(d),"actorRecords");bool found=false;
    for(size_t i=0;ok&&i<qa_json_size(d,records);++i){qa_json_id r=qa_json_at(d,records,i);uint64_t stride;
        if(!qa_json_string_equal(d,qa_json_get(d,r,"id"),(char *)name.data))continue;
        found=true;ok=qa_json_u64(d,qa_json_get(d,r,"stride"),&stride,e)&&offset<=stride&&12<=stride-offset;break;}
    if(!ok||!found){qa_buffer_free(&name);return fail(e,"Native client vector exceeds its declared record");}
    *out=(application_native_q2_field){application_native_q2_callbacks_record_index(n->callbacks,qa_json_get(d,id,"record")),(uint32_t)offset,QA_NATIVE_F32};qa_buffer_free(&name);return true;
}
static bool exclusive(struct application_native_q2 *n,const application_native_q2_field *f,
    size_t bytes,const char *bounds,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id root=qa_json_root(d),clients=qa_json_get(d,root,"clients"),
        records=qa_json_get(d,root,"actorRecords"),names=qa_json_get(d,clients,"records");
    bool client=application_native_q2_callbacks_record_index(n->callbacks,qa_json_get(d,root,"entityRecord"))==f->record,storage=false;
    for(size_t i=0;i<qa_json_size(d,names);++i)client|=application_native_q2_callbacks_record_index(n->callbacks,qa_json_at(d,names,i))==f->record;
    for(size_t i=0;i<qa_json_size(d,records);++i){qa_json_id r=qa_json_at(d,records,i);
        if(application_native_q2_callbacks_record_index(n->callbacks,qa_json_get(d,r,"id"))!=f->record)continue;
        qa_json_id fields=qa_json_get(d,r,"fields");
        for(size_t j=0;j<qa_json_size(d,fields);++j){qa_json_id field=qa_json_at(d,fields,j);uint64_t offset,length;
            if(!qa_json_u64(d,qa_json_get(d,field,"offset"),&offset,e))return false;
            bool bound=bytes==12&&(qa_json_string_equal(d,qa_json_get(d,field,"binding"),"bounds-min")||
                qa_json_string_equal(d,qa_json_get(d,field,"binding"),"bounds-max"));
            if(bounds){if(qa_json_string_equal(d,qa_json_get(d,field,"binding"),bounds)&&offset==f->offset)storage=true;}
            else if(bound&&offset==f->offset)storage=true;
            else if(qa_json_string_equal(d,qa_json_get(d,field,"binding"),"private")){
                if(!qa_json_u64(d,qa_json_get(d,field,"byteLength"),&length,e))return false;
                if(offset<=f->offset&&bytes<=length&&f->offset-offset<=length-bytes)storage=true;}}
        break;}
    qa_json_id inputs=qa_json_get(d,clients,"inputFields"),pose=qa_json_get(d,clients,"pose");
    for(size_t i=0;i<qa_json_size(d,inputs)+2;++i){qa_json_id field,value;size_t length;
        if(i<qa_json_size(d,inputs)){field=qa_json_at(d,inputs,i);value=qa_json_get(d,field,"value");
            if(qa_json_string_equal(d,qa_json_get(d,value,"kind"),"vector"))length=12;
            else {const char *types[]={"int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
                const size_t sizes[]={1,1,2,2,4,4,8,8,4,8};qa_json_id type=qa_json_get(d,value,"kind");
                if(qa_json_string_equal(d,type,"time"))type=qa_json_get(d,value,"encoding");
                length=0;for(size_t k=0;k<10;++k)if(qa_json_string_equal(d,type,types[k]))length=sizes[k];
                if(!length)return fail(e,"Native input field has no declared scalar extent");}}
        else {field=i==qa_json_size(d,inputs)?qa_json_get(d,pose,"viewHeight"):
                qa_json_get(d,qa_json_get(d,pose,"crouched"),"field");
            if(field==QA_JSON_NONE)continue;
            application_native_q2_field temporary={0};
            if(!application_native_q2_field_parse(n->callbacks,field,&temporary,e))return false;
            length=application_native_q2_field_size(temporary.encoding);application_native_q2_field_dispose(&temporary);}
        if(application_native_q2_callbacks_record_index(n->callbacks,qa_json_get(d,field,"record"))!=f->record)continue;
        uint64_t offset;if(!qa_json_u64(d,qa_json_get(d,field,"offset"),&offset,e))return false;
        if(offset<(uint64_t)f->offset+bytes&&f->offset<offset+length)
            return fail(e,"Native client output overlaps an input or pose field");}
    return (client&&storage)||fail(e,"Native client output requires exclusive declared client storage");
}
bool application_native_q2_client_outputs_create(struct application_native_q2 *n,
    struct application_native_q2_client_outputs **out,qa_error *e)
{
    if(!n||!n->callbacks||!out)return fail(e,"Native client outputs require their actual callback owner");
    if(*out)return (*out)->ready||fail(e,"Native client output declaration preparation is incomplete");
    struct application_native_q2_client_outputs *o=calloc(1,sizeof(*o));
    if(!o)return application_fail(e,QA_ERROR_MEMORY,"Retaining native client output declarations");
    o->engine=n;*out=o;
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id rows=qa_json_get(d,qa_json_get(d,qa_json_root(d),"clients"),"outputs");
    if(rows!=QA_JSON_NONE&&qa_json_type(d,rows)!=QA_JSON_ARRAY)return fail(e,"Native client outputs must be declared as an array");
    const char *names[]={"view-offset","movement-mode","stance","body-shape"};
    for(size_t i=0;i<qa_json_size(d,rows);++i){qa_json_id r=qa_json_at(d,rows,i);size_t kind=0;
        while(kind<APPLICATION_CLIENT_OUTPUT_COUNT&&!qa_json_string_equal(d,qa_json_get(d,r,"kind"),names[kind]))++kind;
        if(kind==APPLICATION_CLIENT_OUTPUT_COUNT||(o->channels&(1u<<kind)))return fail(e,"Native client output channels are unknown or duplicated");
        native_output_declaration *f=&o->fields[o->count++];f->channel=(application_client_output_channel)kind;o->channels=(uint8_t)(o->channels|(1u<<kind));
        f->height=kind==APPLICATION_CLIENT_VIEW_OFFSET&&qa_json_get(d,r,"height")!=QA_JSON_NONE;
        bool vector=kind==APPLICATION_CLIENT_BODY_SHAPE||(kind==APPLICATION_CLIENT_VIEW_OFFSET&&!f->height);
        if(f->height&&qa_json_get(d,r,"field")!=QA_JSON_NONE)return fail(e,"Native view offset names both a height and a vector");
        if(!field_parse(n,qa_json_get(d,r,kind==APPLICATION_CLIENT_BODY_SHAPE?"min":f->height?"height":"field"),vector,&f->first,e)||
            !exclusive(n,&f->first,vector?12:application_native_q2_field_size(f->first.encoding),kind==APPLICATION_CLIENT_BODY_SHAPE?"bounds-min":NULL,e))return false;
        if(kind==APPLICATION_CLIENT_BODY_SHAPE){if(!field_parse(n,qa_json_get(d,r,"max"),true,&f->second,e)||
            !exclusive(n,&f->second,12,"bounds-max",e))return false;
            continue;}
        if(kind==APPLICATION_CLIENT_VIEW_OFFSET)continue;
        f->masked=qa_json_get(d,r,"mask")!=QA_JSON_NONE;
        uint64_t mask;if(f->masked&&(!qa_json_u64(d,qa_json_get(d,r,"mask"),&mask,e)||!mask||mask>UINT32_MAX))return fail(e,"Native client output mask is invalid");
        if(f->masked)f->mask=(uint32_t)mask;
        f->values=qa_json_get(d,r,"values");
        if(qa_json_type(d,f->values)!=QA_JSON_ARRAY||!qa_json_size(d,f->values))return fail(e,"Native client output mapping is empty");
        for(size_t j=0;j<qa_json_size(d,f->values);++j){qa_json_id v=qa_json_at(d,f->values,j);double value;
            if(!qa_json_number(d,qa_json_get(d,v,"value"),&value,e)||!isfinite(value)||
                (f->masked&&(value<0||value>UINT32_MAX||trunc(value)!=value||((uint32_t)value&f->mask)!=(uint32_t)value)))return fail(e,"Native client output mapping escapes its actual mask");
            for(size_t k=0;k<j;++k){double previous;if(!qa_json_number(d,qa_json_get(d,qa_json_at(d,f->values,k),"value"),&previous,e)||previous==value)return fail(e,"Native client output mapping is ambiguous");}
            if(kind==APPLICATION_CLIENT_STANCE){bool crouched;if(!qa_json_bool(d,qa_json_get(d,v,"crouched"),&crouched,e))return false;}
            else if(!qa_json_string_equal(d,qa_json_get(d,v,"mode"),"normal")&&!qa_json_string_equal(d,qa_json_get(d,v,"mode"),"noclip")&&!qa_json_string_equal(d,qa_json_get(d,v,"mode"),"freeze"))return fail(e,"Native client movement mode is unknown");}}
    o->ready=true;return true;
}
void application_native_q2_client_outputs_destroy(struct application_native_q2_client_outputs **out)
{
    if(!out||!*out)return;
    struct application_native_q2_client_outputs *o=*out;
    for(size_t i=0;i<o->count;++i){application_native_q2_field_dispose(&o->fields[i].first);application_native_q2_field_dispose(&o->fields[i].second);}
    free(o);*out=NULL;
}
uint8_t application_native_q2_client_outputs_claimed(const struct application_native_q2_client_outputs *o)
{return o?o->claimed:0;}
bool application_client_output_claim_available(const application_provider *p,uint8_t channels,qa_error *e)
{
    if(!p||!p->application||!channels)return fail(e,"Client output claim needs its actual source");
    for(const application_provider *q=p->application->live_providers;q;q=q->next_live){uint8_t claimed=0;
        if(q==p)continue;
        if(q->kind==APPLICATION_PROVIDER_QC&&q->state.qc.engine)claimed=q->state.qc.engine->output_channels;
        else if(q->kind==APPLICATION_PROVIDER_NATIVE&&q->state.native.q2_engine)
            claimed=application_native_q2_client_outputs_claimed(application_native_q2_stages_outputs(q->state.native.q2_engine));
        if(claimed&channels)return application_fail(e,QA_ERROR_ARGUMENT,"Client output channel already has a retained source lease");}
    return true;
}
static bool vector_read(struct application_native_q2_client_outputs *o,qa_actor_id actor,
    const application_native_q2_field *f,qa_vec3 *out,qa_error *e)
{
    qa_native_address address;uint8_t bytes[12];
    if(!application_native_q2_field_address(o->engine->callbacks,actor,f,&address,e)||
        !qa_native_read(application_native_q2_callbacks_instance(o->engine->callbacks),address,bytes,12,e))return false;
    *out=qa_v3(qa_load_f32le(bytes),qa_load_f32le(bytes+4),qa_load_f32le(bytes+8));
    return qa_vec_finite(*out)||fail(e,"Native client output vector is nonfinite");
}
static bool read_values(struct application_native_q2_client_outputs *o,qa_actor_id actor,
    application_client_outputs *out,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(o->engine->callbacks);
    application_client_outputs v={0};
    for(size_t i=0;i<o->count;++i){native_output_declaration *f=o->fields+i;double value;
        if(f->channel==APPLICATION_CLIENT_BODY_SHAPE){v.has_body_bounds=true;
            if(!vector_read(o,actor,&f->first,&v.body_bounds.mins,e)||!vector_read(o,actor,&f->second,&v.body_bounds.maxs,e))return false;
            if(v.body_bounds.mins.x>v.body_bounds.maxs.x||v.body_bounds.mins.y>v.body_bounds.maxs.y||v.body_bounds.mins.z>v.body_bounds.maxs.z)return fail(e,"Native client body output has backwards bounds");
            continue;}
        if(f->channel==APPLICATION_CLIENT_VIEW_OFFSET){v.has_view_offset=true;
            if(!f->height){if(!vector_read(o,actor,&f->first,&v.view_offset,e))return false;}
            else {if(!application_native_q2_field_read(o->engine->callbacks,actor,&f->first,&value,e)||!isfinite(value)||fabs(value)>FLT_MAX)return fail(e,"Native client view height is nonfinite");v.view_offset=qa_v3(0,0,(float)value);}continue;}
        if(!application_native_q2_field_read(o->engine->callbacks,actor,&f->first,&value,e)||!isfinite(value))return fail(e,"Native client output scalar is nonfinite");
        if(f->masked){if(trunc(value)!=value||value<INT32_MIN||value>UINT32_MAX)return fail(e,"Masked native client output is not an integral source word");
            value=(uint32_t)(value<0?value+4294967296.0:value)&f->mask;}
        qa_json_id selected=QA_JSON_NONE;
        for(size_t j=0;j<qa_json_size(d,f->values);++j){qa_json_id row=qa_json_at(d,f->values,j);double mapped;
            if(!qa_json_number(d,qa_json_get(d,row,"value"),&mapped,e))return false;
            if(mapped==value){selected=row;break;}}
        if(selected==QA_JSON_NONE)return fail(e,"Native client output has no declared source value");
        if(f->channel==APPLICATION_CLIENT_STANCE){v.has_stance=true;if(!qa_json_bool(d,qa_json_get(d,selected,"crouched"),&v.crouched,e))return false;}
        else {v.has_mode=true;qa_json_id mode=qa_json_get(d,selected,"mode");v.mode=qa_json_string_equal(d,mode,"normal")?QA_MOVEMENT_MODE_NORMAL:qa_json_string_equal(d,mode,"noclip")?QA_MOVEMENT_MODE_NOCLIP:QA_MOVEMENT_MODE_FREEZE;}}
    *out=v;return true;
}
static bool publication_slot(struct application_native_q2_client_outputs *o,qa_actor_id actor,uint32_t *out,qa_error *e)
{
    if(!o||!o->ready||!application_native_q2_callbacks_client_current(o->engine->callbacks,actor,e))return false;
    for(uint32_t i=1;i<257;++i){application_native_q2_client *c=o->engine->clients+i;
        if(c->reserved&&c->connected&&!c->denied&&!c->disconnect_started&&qa_actor_id_equal(c->actor,actor)){*out=i;return true;}}
    return fail(e,"Native output publication lost its admitted full client");
}
bool application_native_q2_client_outputs_admit(struct application_native_q2 *n,qa_actor_id actor,qa_error *e)
{
    struct application_native_q2_client_outputs *o=application_native_q2_stages_outputs(n);
    if(!o||!o->ready)return fail(e,"Native client outputs were not prepared");
    if(!o->channels)return true;
    uint32_t slot;application_client_outputs values;
    if(o->restoring)return fail(e,"Native client outputs still retain a cold publication receipt");
    if(!publication_slot(o,actor,&slot,e)||!read_values(o,actor,&values,e)||
        !application_control_output_admit(n->provider->application,actor,o->channels,e)||
        !application_client_output_claim_available(n->provider,o->channels,e))return false;
    o->claimed=o->channels;o->clients[slot]=(native_output_publication){actor,values,true};return true;
}
bool application_native_q2_client_outputs_publish(struct application_native_q2 *n,qa_error *e)
{
    struct application_native_q2_client_outputs *o=application_native_q2_stages_outputs(n);
    if(!o)return true;
    if(!o->ready||o->restoring)return fail(e,"Native output refresh has an unfinished declaration or restore");
    for(uint32_t i=1;i<257;++i){native_output_publication *p=o->clients+i;if(!p->published)continue;
        bool live;if(!application_native_q2_callbacks_client_live_read(n->callbacks,p->actor,&live,e))return false;
        if(!live)continue;
        uint32_t slot;application_client_outputs values;
        if(!publication_slot(o,p->actor,&slot,e)||slot!=i||!read_values(o,p->actor,&values,e))return false;
        p->values=values;}
    return true;
}
void application_native_q2_client_outputs_release(struct application_native_q2 *n,qa_actor_id actor)
{
    struct application_native_q2_client_outputs *o=application_native_q2_stages_outputs(n);if(!o)return;
    for(uint32_t i=1;i<257;++i)if(o->clients[i].published&&qa_actor_id_equal(o->clients[i].actor,actor))o->clients[i]=(native_output_publication){0};
}
bool application_native_q2_client_outputs_capture(struct application_native_q2 *n,qa_buffer *out,qa_error *e)
{
    struct application_native_q2_client_outputs *o=application_native_q2_stages_outputs(n);
    if(!o||!out||!o->ready||o->restoring)return fail(e,"Native output capture needs a returned publication owner");
    uint8_t *data=calloc(1,38);if(!data)return application_fail(e,QA_ERROR_MEMORY,"Retaining native output publication receipt");
    qa_store_u32le(data,UINT32_C(0x31504f4e));data[4]=o->channels;data[5]=o->claimed;
    for(uint32_t i=1;i<257;++i)if(o->clients[i].published){uint32_t slot;
        if(!publication_slot(o,o->clients[i].actor,&slot,e)||slot!=i){free(data);return false;}data[6+(i-1)/8]=(uint8_t)(data[6+(i-1)/8]|(1u<<((i-1)%8)));}
    *out=(qa_buffer){data,38};return true;
}
bool application_native_q2_client_outputs_restore(struct application_native_q2 *n,qa_bytes bytes,qa_error *e)
{
    struct application_native_q2_client_outputs *o=application_native_q2_stages_outputs(n);
    if(!o||!o->ready||bytes.size!=38||qa_load_u32le(bytes.data)!=UINT32_C(0x31504f4e)||
        bytes.data[4]!=o->channels||(bytes.data[5]!=0&&bytes.data[5]!=o->channels))return fail(e,"Saved native output lease differs from its declaration");
    memset(o->clients,0,sizeof(o->clients));o->claimed=bytes.data[5];o->restoring=true;
    if(o->claimed&&!application_client_output_claim_available(n->provider,o->claimed,e))return false;
    for(uint32_t i=1;i<257;++i)if(bytes.data[6+(i-1)/8]&(1u<<((i-1)%8))){if(!o->claimed)return fail(e,"Saved client publication has no actual channel lease");o->clients[i].published=true;}
    return true;
}
bool application_native_q2_client_outputs_finish_restore(struct application_native_q2 *n,qa_error *e)
{
    struct application_native_q2_client_outputs *o=application_native_q2_stages_outputs(n);if(!o||!o->restoring)return true;
    for(uint32_t i=1;i<257;++i)if(o->clients[i].published){uint32_t slot;application_client_outputs values;qa_actor_id actor=n->clients[i].actor;
        if(!publication_slot(o,actor,&slot,e)||slot!=i||!application_control_output_admit(n->provider->application,actor,o->channels,e)||!read_values(o,actor,&values,e))return false;
        o->clients[i].actor=actor;o->clients[i].values=values;}
    o->restoring=false;return true;
}
bool application_native_q2_control_outputs(const qa_application *app,qa_actor_id actor,application_client_outputs *out,qa_error *e)
{
    if(!app||!out)return fail(e,"Native output read requires its actual application and destination");
    *out=(application_client_outputs){0};uint8_t claimed=0;
    for(size_t i=0;i<app->provider_count;++i){application_provider *p=app->providers[i];
        if(!p||!p->attached||!p->constructed||p->close_pending||p->kind!=APPLICATION_PROVIDER_NATIVE||!p->state.native.q2_engine)continue;
        struct application_native_q2_client_outputs *o=application_native_q2_stages_outputs(p->state.native.q2_engine);
        if(!o||!o->claimed)continue;
        if(!o->ready||o->restoring||(claimed&o->claimed))return fail(e,"Native output read found unresolved or competing source leases");
        claimed|=o->claimed;
        for(uint32_t j=1;j<257;++j){native_output_publication *v=o->clients+j;if(!v->published||!qa_actor_id_equal(v->actor,actor))continue;uint32_t slot;
            if(!publication_slot(o,actor,&slot,e)||slot!=j)return false;
            if(v->values.has_view_offset){out->has_view_offset=true;out->view_offset=v->values.view_offset;}
            if(v->values.has_mode){out->has_mode=true;out->mode=v->values.mode;}
            if(v->values.has_stance){out->has_stance=true;out->crouched=v->values.crouched;}
            if(v->values.has_body_bounds){out->has_body_bounds=true;out->body_bounds=v->values.body_bounds;}break;}}
    return true;
}
