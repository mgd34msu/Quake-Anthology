#include "remote_unified_components_private.h"
#include "../application/guest_q3_components.h"
#include "qa/binary.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool q3remote_component_fail(qa_error *e,qa_status status,const char *text)
{ qa_error_set(e,status,0,"%s",text); return false; }
bool q3remote_component_player_finite(const qa_q3_player *p)
{
    const float *vectors[]={p->origin,p->velocity,p->grapplePoint,p->viewangles};
    for(size_t i=0;i<4;++i) for(size_t k=0;k<3;++k) if(!isfinite(vectors[i][k])) return false;
    return true;
}
static qa_json_id field(const qa_unified_document *d,qa_json_id id,const char *name)
{ return qa_json_get(qa_unified_document_json(d),id,name); }
static bool integer(const qa_unified_document *d,qa_json_id id,uint64_t maximum,uint64_t *out,qa_error *e)
{
    double n;
    if(!qa_unified_document_number(d,id,&n,e)) return false;
    if(!isfinite(n)||n<0||n>(double)maximum||trunc(n)!=n) return q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote component integer exceeds its source domain");
    *out=(uint64_t)n; return true;
}
static bool text(const qa_unified_document *d,qa_json_id id,char **out,qa_error *e)
{
    qa_buffer b={0}; if(!qa_json_string(qa_unified_document_json(d),id,&b,e)) return false;
    if(memchr(b.data,0,b.size)) { qa_buffer_free(&b); return q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote component string contains NUL"); }
    *out=(char *)b.data; return true;
}
static char *copy_text(const char *text)
{
    size_t size=strlen(text)+1; char *copy=malloc(size);
    if(copy) memcpy(copy,text,size);
    return copy;
}
static bool document_equal(const qa_unified_document *a,const qa_unified_document *b,qa_error *e)
{
    qa_buffer left={0},right={0};
    bool ok=qa_unified_value_canonical(qa_json_source(qa_unified_document_json(a),qa_unified_document_root(a)),&left,e)&&
        qa_unified_value_canonical(qa_json_source(qa_unified_document_json(b),qa_unified_document_root(b)),&right,e);
    if(ok&&(left.size!=right.size||memcmp(left.data,right.data,left.size))) ok=q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote component activation changed its qualified identity");
    qa_buffer_free(&left); qa_buffer_free(&right); return ok;
}
static bool runtime_qualify(remote_component_state *s,qa_error *e)
{
    qa_json_document *d=NULL;
    if(!qa_json_parse(s->mod->declaration,&d,e)) return false;
    qa_json_id runtime=qa_json_get(d,qa_json_get(d,qa_json_root(d),"presentation"),"runtime");
    bool ok=qa_json_string_equal(d,runtime,s->player_events?"qvm-player-events":"qvm-scene");
    qa_json_destroy(d);
    return ok||q3remote_component_fail(e,QA_ERROR_FORMAT,"Component runtime differs from its actual held declaration");
}
bool q3remote_component_state_qualify(frontend_unified_components *owner,remote_component_state *s,qa_error *e)
{
    if(!owner||!s->identity||!s->presentation_owner||!s->provider||
        (s->abi!=QA_QVM_Q3_MODERN&&s->abi!=QA_QVM_Q3_116N))
        return q3remote_component_fail(e,QA_ERROR_FORMAT,"Saved component lacks its actual identity and ABI");
    const qa_json_document *j=qa_unified_document_json(s->identity);
    qa_json_id root=qa_unified_document_root(s->identity),source=qa_json_get(j,root,"source"),selection=qa_json_get(j,root,"selection");
    const qa_json_document *p=qa_unified_document_json(s->presentation_owner); uint64_t generation=0;
    qa_json_id presentation=qa_unified_document_root(s->presentation_owner);
    if(!qa_json_string_equal(p,qa_json_get(p,presentation,"provider"),s->provider)||
        !qa_json_u64(p,qa_json_get(p,presentation,"generation"),&generation,e)||generation!=s->owner_generation||!generation)
        return q3remote_component_fail(e,QA_ERROR_FORMAT,"Saved component owner changed its actual activation");
    const qa_recipe_choices *choices=qa_executable_recipe_choices(owner->recipe);
    for(size_t i=0;i<choices->mod_count;++i) {
        const qa_launch_mod_selection *selected=choices->mods+i;
        if(!selected->enabled||!qa_json_string_equal(j,qa_json_get(j,source,"provider"),selected->instance)) continue;
        for(size_t k=0;k<qa_executable_recipe_provider_count(owner->recipe);++k) {
            const qa_recipe_provider *provider=qa_executable_recipe_provider(owner->recipe,k);
            if(!provider||strcmp(provider->selection.instance,selected->instance)) continue;
            const qa_catalog_mod *mod=qa_catalog_mod_find(qa_executable_recipe_catalog(owner->recipe),selected->component);
            const qa_product *product=mod?qa_catalog_product(qa_executable_recipe_catalog(owner->recipe),mod->product):NULL;
            if(!mod||mod->unavailable||!product||!qa_json_string_equal(j,qa_json_get(j,selection,"product"),product->key)||
                !qa_json_string_equal(j,qa_json_get(j,selection,"id"),mod->id)) continue;
            qa_unified_document *expected=NULL;
            bool ok=application_q3_component_identity_create(mod,product,selected->instance,&expected,e)&&document_equal(s->identity,expected,e);
            qa_unified_document_destroy(expected);
            if(!ok) return false;
            s->mod=mod; s->provider_row=provider; return runtime_qualify(s,e);
        }
    }
    return q3remote_component_fail(e,QA_ERROR_FORMAT,"Saved component has no genuine admitted recipe identity");
}
static bool gamestate(const qa_unified_document *d,qa_json_id id,qa_q3_gamestate *out,qa_error *e)
{
    uint64_t count; qa_buffer data={0}; const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id offsets=field(d,id,"stringOffsets");
    bool ok=integer(d,field(d,id,"dataCount"),16000,&count,e)&&qa_json_size(j,offsets)==1024&&
        qa_unified_document_bytes(d,field(d,id,"stringData"),&data,e)&&data.size==16000&&data.data[0]==0;
    for(size_t i=0;ok&&i<1024;++i) {
        uint64_t at;
        ok=integer(d,qa_json_at(j,offsets,i),count?count-1:0,&at,e);
        if(ok&&at&&!memchr(data.data+at,0,(size_t)count-(size_t)at)) ok=false;
        if(ok) out->config_offsets[i]=(uint16_t)at;
    }
    if(ok) { out->string_bytes=(uint32_t)count; memcpy(out->strings,data.data,16000); }
    qa_buffer_free(&data); return ok||(e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote component gamestate has no exact Source strings"));
}
void q3remote_component_state_free(remote_component_state *s)
{
    for(size_t i=0;i<s->command_count;++i) {
        free((void *)s->commands[i].text);
        qa_command_tokens_free(s->arguments+i);
    }
    free(s->provider); qa_unified_document_destroy(s->identity); qa_unified_document_destroy(s->presentation_owner); memset(s,0,sizeof(*s));
}
static bool arguments_copy(const qa_command_tokens *prior,qa_command_tokens *copy,qa_error *e)
{
    size_t size=strlen(prior->args_text)+1;
    for(size_t k=0;k<prior->count;++k) size+=strlen(prior->values[k])+1;
    copy->values=prior->count?calloc(prior->count,sizeof(*copy->values)):NULL;
    copy->storage=malloc(size); copy->count=prior->count;
    if(!copy->storage||(prior->count&&!copy->values)) return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining literal reached component argv");
    char *at=copy->storage;
    for(size_t k=0;k<prior->count;++k) { size_t n=strlen(prior->values[k])+1; copy->values[k]=at; memcpy(at,prior->values[k],n); at+=n; }
    copy->args_text=at; strcpy(at,prior->args_text); return true;
}
static bool frame_source_copy(const remote_component_state *source,remote_component_state *out,qa_error *e)
{
    out->game_state=source->game_state;
    out->game_state_revision=source->game_state_revision;
    out->command_sequence=source->command_sequence;
    for(size_t i=0;i<source->command_count;++i) {
        out->commands[i]=source->commands[i]; out->commands[i].text=copy_text(source->commands[i].text); ++out->command_count;
        if(!out->commands[i].text||!arguments_copy(source->arguments+i,out->arguments+i,e))
            return e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining received frame command history");
        out->commands[i].arguments=out->arguments+i;
    }
    return true;
}
static bool command_text(const qa_unified_document *d,qa_json_id row,char **out,qa_command_tokens *tokens,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id args=field(d,row,"arguments");
    size_t count=qa_json_size(j,args),size=1;
    if(qa_json_type(j,args)!=QA_JSON_ARRAY||count>128) return q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote component command has no valid argument list");
    char **values=count?calloc(count,sizeof(*values)):NULL;
    if(count&&!values) return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining remote component argv");
    bool ok=true;
    for(size_t i=0;ok&&i<count;++i) {
        ok=text(d,qa_json_at(j,args,i),values+i,e);
        if(ok&&strlen(values[i])>8192) ok=q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote component command argument exceeds the declared extent");
        if(ok) size+=strlen(values[i])+3;
    }
    char *joined=ok?malloc(size):NULL;
    if(ok&&!joined) ok=q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining remote component command text");
    if(ok) {
        char *p=joined;
        for(size_t i=0;i<count;++i) { if(i) *p++=' '; *p++='"'; size_t n=strlen(values[i]); memcpy(p,values[i],n); p+=n; *p++='"'; }
        *p=0;
        size_t storage_size=size*2; char *storage=malloc(storage_size);
        if(!storage) { free(joined); ok=q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining exact received argument storage"); }
        else {
            char *at=storage;
            for(size_t i=0;i<count;++i) { size_t n=strlen(values[i])+1; memcpy(at,values[i],n); free(values[i]); values[i]=at; at+=n; }
            char *args_text=at;
            for(size_t i=1;i<count;++i) { if(i>1) *at++=' '; size_t n=strlen(values[i]); memcpy(at,values[i],n); at+=n; }
            *at=0;
            *tokens=(qa_command_tokens){.count=count,.values=values,.storage=storage,.args_text=args_text};
            values=NULL; *out=joined;
        }
    }
    if(values) for(size_t i=0;i<count;++i) free(values[i]);
    free(values); return ok;
}
bool q3remote_component_state_read(frontend_unified_components *owner,const qa_unified_document *d,qa_json_id id,
    const remote_component *previous,remote_component_state *out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id presentation=field(d,id,"owner"),identity=field(d,id,"identity");
    remote_component_state s={0};
    bool ok=text(d,field(d,presentation,"provider"),&s.provider,e)&&
        integer(d,field(d,presentation,"generation"),QA_UNIFIED_SAFE_INTEGER,&s.owner_generation,e)&&s.owner_generation&&
        integer(d,field(d,id,"generation"),QA_UNIFIED_SAFE_INTEGER,&s.generation,e)&&
        integer(d,field(d,id,"gameStateRevision"),QA_UNIFIED_SAFE_INTEGER,&s.game_state_revision,e)&&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT,qa_json_source(j,identity),&s.identity,e)&&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT,qa_json_source(j,presentation),&s.presentation_owner,e);
    if(ok) {
        if(qa_json_string_equal(j,field(d,id,"abi"),"q3-modern")) s.abi=QA_QVM_Q3_MODERN;
        else if(qa_json_string_equal(j,field(d,id,"abi"),"q3-1.16n-base")) s.abi=QA_QVM_Q3_116N;
        else ok=false;
        s.player_events=qa_json_string_equal(j,field(d,id,"runtime"),"qvm-player-events");
        if(!s.player_events&&!qa_json_string_equal(j,field(d,id,"runtime"),"qvm-scene")) ok=q3remote_component_fail(e,QA_ERROR_UNSUPPORTED,"Remote component requires its actual admitted presentation runtime");
    }
    if(ok&&previous) {
        ok=s.owner_generation==previous->state.owner_generation&&s.generation==previous->state.generation&&s.abi==previous->state.abi&&s.player_events==previous->state.player_events&&document_equal(s.identity,previous->state.identity,e);
        if(ok) { s.mod=previous->state.mod; s.provider_row=previous->state.provider_row; }
    } else if(ok) {
        qa_json_id source=field(d,identity,"source"),selection=field(d,identity,"selection");
        const qa_recipe_choices *choices=qa_executable_recipe_choices(owner->recipe);
        for(size_t i=0;!s.mod&&i<choices->mod_count;++i) {
            const qa_launch_mod_selection *selected=choices->mods+i;
            if(!selected->enabled||!qa_json_string_equal(j,field(d,source,"provider"),selected->instance)) continue;
            for(size_t k=0;k<qa_executable_recipe_provider_count(owner->recipe);++k) {
                const qa_recipe_provider *p=qa_executable_recipe_provider(owner->recipe,k);
                if(strcmp(p->selection.instance,selected->instance)) continue;
                const qa_catalog_mod *mod=qa_catalog_mod_find(qa_executable_recipe_catalog(owner->recipe),selected->component);
                const qa_product *product=mod?qa_catalog_product(qa_executable_recipe_catalog(owner->recipe),mod->product):NULL;
                if(!mod||mod->unavailable||!product||!qa_json_string_equal(j,field(d,selection,"product"),product->key)||
                    !qa_json_string_equal(j,field(d,selection,"id"),mod->id)) continue;
                qa_unified_document *expected=NULL;
                ok=application_q3_component_identity_create(mod,product,selected->instance,&expected,e)&&document_equal(s.identity,expected,e);
                qa_unified_document_destroy(expected);
                if(ok) { s.mod=mod; s.provider_row=p; }
                break;
            }
        }
        if(ok&&!s.mod) ok=q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote component is absent from the genuinely admitted recipe");
    }
    if(ok) ok=runtime_qualify(&s,e);
    qa_json_id gs=field(d,id,"gameState"),commands=field(d,id,"commands"); uint64_t base=0;
    if(ok) ok=integer(d,field(d,id,"commandBase"),INT32_MAX,&base,e)&&(!previous||base==(uint64_t)previous->state.command_sequence);
    if(ok&&qa_json_type(j,gs)==QA_JSON_NULL) {
        if(!previous||s.game_state_revision!=previous->state.game_state_revision) ok=false;
        else s.game_state=previous->state.game_state;
    } else if(ok) ok=gamestate(d,gs,&s.game_state,e)&&(!previous||s.game_state_revision>=previous->state.game_state_revision);
    size_t count=qa_json_size(j,commands);
    if(ok) ok=qa_json_type(j,commands)==QA_JSON_ARRAY&&count<=64&&base+count<=INT32_MAX;
    size_t keep=previous?previous->state.command_count:0;
    if(keep>64-count) keep=64-count;
    for(size_t i=0;ok&&i<keep;++i) {
        const application_q3_scene_command *old=previous->state.commands+previous->state.command_count-keep+i;
        s.commands[i]=*old; s.commands[i].text=copy_text(old->text); ++s.command_count;
        if(!s.commands[i].text) ok=q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining reached remote command history");
        if(ok) {
            const qa_command_tokens *prior=previous->state.arguments+previous->state.command_count-keep+i;
            ok=arguments_copy(prior,s.arguments+i,e);
        }
    }
    for(size_t i=0;ok&&i<count;++i) {
        qa_json_id row=qa_json_at(j,commands,i); uint64_t sequence;
        ok=integer(d,field(d,row,"sequence"),INT32_MAX,&sequence,e)&&sequence==base+i+1;
        application_q3_scene_command *command=s.commands+s.command_count;
        if(ok) ok=command_text(d,row,(char **)&command->text,s.arguments+s.command_count,e);
        if(ok) { command->sequence=(int32_t)sequence; command->addressed=true; ++s.command_count; }
    }
    s.command_sequence=(int32_t)(base+count);
    if(!ok) { q3remote_component_state_free(&s); return e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote reliable component history has a gap"); }
    *out=s;
    for(size_t i=0;i<out->command_count;++i) out->commands[i].arguments=out->arguments+i;
    return true;
}
void q3remote_component_frame_free(remote_component_frame *f)
{ if(!f) return; free((void *)f->snapshot.entities); free(f->actors); q3remote_component_state_free(&f->source); free(f); }
static bool actor(const qa_unified_document *d,qa_json_id id,frontend_remote_unified *replica,qa_actor_id *out,qa_error *e)
{
    uint64_t slot,generation;
    return integer(d,field(d,id,"slot"),UINT32_MAX,&slot,e)&&integer(d,field(d,id,"generation"),QA_UNIFIED_SAFE_INTEGER,&generation,e)&&
        frontend_remote_unified_actor(replica,(uint32_t)slot,generation,out,e);
}
static bool signed_word(const qa_unified_document *d,qa_json_id id,int32_t *out,qa_error *e)
{
    double n;
    if(!qa_unified_document_number(d,id,&n,e)) return false;
    if(!isfinite(n)||n<INT32_MIN||n>INT32_MAX||trunc(n)!=n)
        return q3remote_component_fail(e,QA_ERROR_FORMAT,"Component event exceeds its original signed word");
    *out=(int32_t)n; return true;
}
static bool event_module_matches(const qa_unified_document *d,qa_json_id module,const remote_component *r,
    bool *matches,qa_error *e)
{
    const qa_json_document *identity=qa_unified_document_json(r->state.identity);
    qa_json_id expected=qa_json_at(identity,qa_json_get(identity,qa_unified_document_root(r->state.identity),"modules"),0);
    qa_buffer actual_bytes={0},expected_bytes={0};
    bool ok=expected!=QA_JSON_NONE&&qa_unified_value_canonical(qa_json_source(qa_unified_document_json(d),module),&actual_bytes,e)&&
        qa_unified_value_canonical(qa_json_source(identity,expected),&expected_bytes,e);
    if(ok) *matches=actual_bytes.size==expected_bytes.size&&!memcmp(actual_bytes.data,expected_bytes.data,actual_bytes.size);
    qa_buffer_free(&actual_bytes); qa_buffer_free(&expected_bytes); return ok;
}
static bool player_event_read(frontend_unified_components *o,const qa_unified_document *d,qa_json_id row,
    remote_component **target,remote_component_event *out,bool *handled,qa_error *e)
{
    *handled=false; *target=NULL;
    if(!o||!d||!frontend_unified_components_current(o)||o->busy)
        return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component event lost its actual replica roster");
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id event=field(d,row,"event"),source=field(d,event,"source"),stamp=field(d,row,"source");
    if(!qa_json_string_equal(j,field(d,row,"kind"),"q3-source")||
        !qa_json_string_equal(j,field(d,event,"kind"),"player-event")) return true;
    remote_component *r=NULL; bool known=false;
    for(size_t i=0;!r&&i<q3remote_component_physical_count(o);++i) {
        remote_component *candidate=q3remote_component_physical_at(o,i);
        if(!candidate||!candidate->state.player_events||
            !qa_json_string_equal(j,field(d,stamp,"provider"),candidate->state.provider)) continue;
        known=true; bool matches=false;
        if(!event_module_matches(d,field(d,source,"module"),candidate,&matches,e)) return false;
        const qa_product *content=qa_catalog_product(qa_executable_recipe_catalog(o->recipe),candidate->state.mod->product);
        if(matches&&content&&qa_json_string_equal(j,field(d,row,"content"),content->identity)&&
            qa_json_string_equal(j,field(d,source,"abiProfile"),candidate->state.abi==QA_QVM_Q3_116N?"q3-1.16n-base":"q3-modern")) r=candidate;
    }
    if(!r) return !known||q3remote_component_fail(e,QA_ERROR_FORMAT,"Component event has no actual admitted gameplay identity");
    *handled=true;
    const qa_product *product=qa_catalog_product(qa_executable_recipe_catalog(o->recipe),r->state.mod->product);
    if(!product||!qa_json_string_equal(j,field(d,row,"content"),product->identity)||
        !qa_json_string_equal(j,field(d,source,"abiProfile"),r->state.abi==QA_QVM_Q3_116N?"q3-1.16n-base":"q3-modern"))
        return q3remote_component_fail(e,QA_ERROR_FORMAT,"Component player event changed its admitted source content or ABI");
    qa_json_id owner=field(d,row,"owner");
    if(qa_json_type(j,owner)!=QA_JSON_NULL&&owner!=QA_JSON_NONE) {
        uint64_t generation=0;
        if(!qa_json_string_equal(j,field(d,owner,"provider"),r->state.provider)||
            !integer(d,field(d,owner,"generation"),QA_UNIFIED_SAFE_INTEGER,&generation,e)||!generation)
            return q3remote_component_fail(e,QA_ERROR_FORMAT,"Component event lost its genuine presentation token domain");
    }
    qa_json_id recipient=field(d,row,"recipient");
    if(recipient!=QA_JSON_NONE&&qa_json_type(j,recipient)!=QA_JSON_NULL) {
        qa_actor_id receiver,viewer; uint32_t slot;
        if(!actor(d,recipient,o->replica,&receiver,e)||!frontend_remote_unified_player(o->replica,&viewer,&slot)) return false;
        if(!qa_actor_id_equal(receiver,viewer)) return true;
    }
    application_q3_scene_player_event *v=&out->value;
    uint8_t bytes[468]={0}; qa_json_id state=field(d,event,"playerState");
    bool ok=actor(d,field(d,event,"actor"),o->replica,&v->actor,e)&&
        integer(d,field(d,row,"sequence"),QA_UNIFIED_SAFE_INTEGER,&out->sequence,e)&&
        signed_word(d,field(d,event,"event"),&v->event,e)&&signed_word(d,field(d,event,"parameter"),&v->parameter,e)&&
        signed_word(d,field(d,event,"time"),&v->time_ms,e)&&v->time_ms>=0&&
        qa_json_type(j,state)==QA_JSON_ARRAY&&qa_json_size(j,state)==qa_qvm_player_bytes(QA_QVM_Q3_MODERN);
    for(size_t i=0;ok&&i<sizeof(bytes);++i) {
        uint64_t value=0; ok=integer(d,qa_json_at(j,state,i),UINT8_MAX,&value,e);
        if(ok) bytes[i]=(uint8_t)value;
    }
    if(ok) {
        qa_q3_abi_record record={.abi=QA_QVM_Q3_MODERN,.bytes={bytes,sizeof(bytes)}};
        ok=qa_q3_abi_read_player(&record,0,true,&v->player,e)&&q3remote_component_player_finite(&v->player);
    }
    qa_json_id sequence=field(d,event,"sequence");
    v->external=qa_json_string_equal(j,field(d,sequence,"kind"),"external");
    if(ok) ok=(v->external||qa_json_string_equal(j,field(d,sequence,"kind"),"predictable"))&&
        signed_word(d,field(d,sequence,v->external?"time":"sequence"),&v->source_sequence,e);
    float *coordinates[]={&v->origin.x,&v->origin.y,&v->origin.z}; const char *names[]={"x","y","z"};
    for(size_t i=0;ok&&i<3;++i) {
        double n;
        ok=qa_unified_document_number(d,field(d,field(d,event,"origin"),names[i]),&n,e)&&isfinite(n)&&isfinite((float)n);
        if(ok) *coordinates[i]=(float)n;
    }
    if(!ok) return e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_FORMAT,"Component player event has no exact original record");
    *target=r->retired?NULL:r; return true;
}
bool frontend_unified_components_player_event_validate(frontend_unified_components *o,const qa_unified_document *d,
    qa_json_id row,bool *handled,qa_error *e)
{
    if(!handled) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component event requires a reached-handler result");
    remote_component *target=NULL; remote_component_event event={0};
    return player_event_read(o,d,row,&target,&event,handled,e);
}
bool frontend_unified_components_player_event(frontend_unified_components *o,const qa_unified_document *d,
    qa_json_id row,bool *handled,qa_error *e)
{
    if(!handled) return q3remote_component_fail(e,QA_ERROR_ARGUMENT,"Component event requires a reached-handler result");
    remote_component *target=NULL; remote_component_event event={0};
    if(!player_event_read(o,d,row,&target,&event,handled,e)) return false;
    if(!target||(target->event_present&&event.sequence<=target->event_sequence)) return true;
    remote_component_event *owned=malloc(sizeof(*owned));
    if(!owned) return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining reached original component player event");
    *owned=event; remote_component_event **tail=&target->events;
    while(*tail) tail=&(*tail)->next;
    *tail=owned; target->event_sequence=event.sequence; target->event_present=true; return true;
}
bool q3remote_component_frame_read(frontend_unified_components *o,remote_component *row,const qa_unified_document *d,qa_json_id id,
    remote_component_frame **out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); uint64_t owner_generation=0,generation=0,revision=0,client=0,time=0;
    qa_json_id identity=field(d,id,"owner"),snapshot=field(d,id,"snapshot"),bindings=field(d,id,"bindings"),scene=field(d,id,"scene");
    remote_component_frame *f=calloc(1,sizeof(*f)); if(!f) return q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining actual remote component frame");
    qa_actor_id viewer={0}; uint32_t viewer_slot=0;
    bool ok=qa_json_string_equal(j,field(d,identity,"provider"),row->state.provider)&&
        integer(d,field(d,identity,"generation"),QA_UNIFIED_SAFE_INTEGER,&owner_generation,e)&&owner_generation==row->state.owner_generation&&
        integer(d,field(d,id,"generation"),QA_UNIFIED_SAFE_INTEGER,&generation,e)&&generation==row->state.generation&&
        integer(d,field(d,id,"gameStateRevision"),QA_UNIFIED_SAFE_INTEGER,&revision,e)&&revision==row->state.game_state_revision&&
        qa_json_string_equal(j,field(d,id,"abi"),row->state.abi==QA_QVM_Q3_116N?"q3-1.16n-base":"q3-modern")&&
        actor(d,field(d,id,"viewer"),o->replica,&f->viewer,e)&&frontend_remote_unified_player(o->replica,&viewer,&viewer_slot)&&qa_actor_id_equal(viewer,f->viewer)&&
        integer(d,field(d,id,"clientNumber"),1023,&client,e)&&integer(d,field(d,snapshot,"serverTime"),INT32_MAX,&time,e);
    qa_buffer ps={0};
    if(ok) ok=qa_unified_document_bytes(d,field(d,snapshot,"playerState"),&ps,e)&&ps.size==qa_qvm_player_bytes(row->state.abi);
    if(ok) { qa_q3_abi_record record={.abi=row->state.abi,.bytes={ps.data,ps.size}}; ok=qa_q3_abi_read_player(&record,0,true,&f->snapshot.player,e); }
    qa_buffer_free(&ps);
    size_t count=qa_json_size(j,bindings); bool has_viewer=false;
    if(ok) ok=qa_json_type(j,bindings)==QA_JSON_ARRAY&&count<=1024;
    if(ok&&count) { f->actors=calloc(count,sizeof(*f->actors)); if(!f->actors) ok=q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining real replica actor bindings"); }
    for(size_t i=0;ok&&i<count;++i) {
        qa_json_id binding=qa_json_at(j,bindings,i); uint64_t slot=0;
        ok=integer(d,field(d,binding,"slot"),1023,&slot,e)&&actor(d,field(d,binding,"actor"),o->replica,&f->actors[i].actor,e)&&
            qa_json_bool(j,field(d,binding,"owned"),&f->actors[i].owned,e);
        f->actors[i].slot=(uint32_t)slot;
        for(size_t k=0;ok&&k<i;++k) if(f->actors[k].slot==slot) ok=false;
        has_viewer|=ok&&slot==client&&!f->actors[i].owned&&qa_actor_id_equal(f->actors[i].actor,viewer);
    }
    f->has_scene=qa_json_type(j,scene)!=QA_JSON_NULL;
    if(ok) ok=has_viewer&&f->has_scene!=row->state.player_events;
    uint64_t scene_revision=0,scene_time=0,flags=0,sequence=(uint64_t)row->state.command_sequence;
    qa_json_id scene_snapshot=field(d,scene,"snapshot");
    if(ok&&f->has_scene) ok=integer(d,field(d,scene,"revision"),QA_UNIFIED_SAFE_INTEGER,&scene_revision,e)&&
        integer(d,field(d,scene_snapshot,"serverTime"),INT32_MAX,&scene_time,e)&&scene_time==time&&
        integer(d,field(d,scene_snapshot,"flags"),255,&flags,e)&&
        integer(d,field(d,scene_snapshot,"serverCommandSequence"),INT32_MAX,&sequence,e)&&sequence==(uint64_t)row->state.command_sequence;
    qa_buffer mask={0};
    if(ok&&f->has_scene) ok=qa_unified_document_bytes(d,field(d,scene_snapshot,"areaMask"),&mask,e)&&mask.size==32;
    if(ok&&f->has_scene) memcpy(f->snapshot.area_mask,mask.data,32);
    qa_buffer_free(&mask);
    qa_json_id entities=field(d,scene_snapshot,"entities"); size_t n=qa_json_size(j,entities);
    if(ok&&f->has_scene) ok=qa_json_type(j,entities)==QA_JSON_ARRAY&&n<=256;
    qa_q3_entity *decoded=ok&&n?calloc(n,sizeof(*decoded)):NULL;
    f->snapshot.entities=decoded;
    if(ok&&n&&!decoded) ok=q3remote_component_fail(e,QA_ERROR_MEMORY,"Retaining received Source entity records");
    for(size_t i=0;ok&&i<n;++i) {
        qa_buffer bytes={0}; ok=qa_unified_document_bytes(d,qa_json_at(j,entities,i),&bytes,e)&&bytes.size==qa_qvm_entity_bytes(row->state.abi);
        if(ok) { qa_q3_abi_record record={.abi=row->state.abi,.bytes={bytes.data,bytes.size}}; ok=qa_q3_abi_read_entity(&record,0,true,decoded+i,e); }
        qa_buffer_free(&bytes);
        for(size_t k=0;ok&&k<i;++k) if(f->snapshot.entities[k].number==f->snapshot.entities[i].number) ok=false;
    }
    if(ok) {
        f->snapshot.valid=true; f->snapshot.server_time=(int32_t)time; f->snapshot.flags=(uint8_t)flags;
        f->snapshot.area_bytes=32; f->snapshot.server_command_number=(int32_t)sequence; f->snapshot.entity_count=n;
        f->context=(application_q3_scene_context){.generation=generation,.revision=(int64_t)scene_revision,.game_state_revision=(int64_t)revision,
            .time_ms=(int32_t)time,.client_number=(int32_t)client,.snapshot=&f->snapshot,.actors=f->actors,.actor_count=count,.has_weapon_presented=true};
        ok=qa_json_bool(j,field(d,id,"weaponPresented"),&f->context.weapon_presented,e)&&frame_source_copy(&row->state,&f->source,e);
        if(ok) {
            f->context.game_state=&f->source.game_state; f->context.commands=f->source.commands; f->context.command_count=f->source.command_count;
            if(row->frame&&time>=(uint64_t)row->frame->context.time_ms) f->context.frame_ms=(int32_t)(time-(uint64_t)row->frame->context.time_ms);
        }
    }
    if(!ok) { q3remote_component_frame_free(f); return e&&e->code!=QA_OK?false:q3remote_component_fail(e,QA_ERROR_FORMAT,"Remote component frame differs from reliable admission"); }
    *out=f; return true;
}
