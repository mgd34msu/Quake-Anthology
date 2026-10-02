#include "guest_q3_mod_actors_private.h"
#include <stdio.h>

static bool word(const qa_json_document *d,qa_json_id id,uint32_t *out,qa_error *e)
{
    uint64_t value;
    if (!qa_json_u64(d,id,&value,e)) return false;
    if (value>UINT32_MAX) return q3mod_fail(e,QA_ERROR_FORMAT,"Actor semantic field exceeds source word");
    *out=(uint32_t)value; return true;
}
static bool field(application_q3_mod_actors *o,const qa_json_document *d,qa_json_id id,size_t record,uint32_t *out,qa_error *e)
{
    uint32_t stride=o->options.profile->records[record].stride;
    return word(d,id,out,e)&&((!(*out&3)&&stride>=4&&*out<=stride-4)||
        q3mod_fail(e,QA_ERROR_FORMAT,"Actor semantic field leaves its genuine declared record"));
}
static bool call(application_q3_mod_actors *o,const qa_json_document *d,qa_json_id id,
    const char *const *names,size_t roles,mod_actor_call *out,qa_error *e)
{
    qa_json_id extras=qa_json_get(d,id,"extras"),declared=qa_json_get(d,id,"roles");
    size_t count=qa_json_size(d,extras);
    if (qa_json_type(d,extras)!=QA_JSON_ARRAY||count>62-roles)
        return q3mod_fail(e,QA_ERROR_FORMAT,"Actor call exceeds original private argument capacity");
    out->role_count=roles; out->count=roles+count; bool occupied[62]={0};
    for (size_t i=0;i<roles;++i) {
        uint32_t at;
        if (!word(d,qa_json_get(d,declared,names[i]),&at,e)) return false;
        if (at>=out->count||occupied[at]) return q3mod_fail(e,QA_ERROR_FORMAT,"Actor call roles overlap or leave original frame");
        occupied[at]=true; out->roles[i]=at;
    }
    for (size_t i=0;i<count;++i) {
        qa_json_id row=qa_json_at(d,extras,i),kind=qa_json_get(d,row,"kind"),value=qa_json_get(d,row,"value"); uint32_t at;
        if (!word(d,qa_json_get(d,row,"index"),&at,e)) return false;
        if (at>=out->count||occupied[at]) return q3mod_fail(e,QA_ERROR_FORMAT,"Actor call extra overlaps a genuine argument");
        occupied[at]=true;
        if (qa_json_string_equal(d,kind,"float32")) {
            double scalar;
            if (!qa_json_number(d,value,&scalar,e)||!q3mod_scalar_word(scalar,MOD_FLOAT32,out->words+at,e)) return false;
        } else if (qa_json_string_equal(d,kind,"address")) {
            uint32_t address;
            if (!word(d,value,&address,e)||!qa_qvm_qualify_source_span(o->options.profile->image,address,1,e)) return false;
            memcpy(out->words+at,&address,4);
        } else if (qa_json_string_equal(d,kind,"int32")) {
            int64_t scalar;
            if (!qa_json_i64(d,value,&scalar,e)) return false;
            if (scalar<INT32_MIN||scalar>INT32_MAX) return q3mod_fail(e,QA_ERROR_FORMAT,"Actor call extra exceeds signed source word");
            out->words[at]=(int32_t)scalar;
        } else return q3mod_fail(e,QA_ERROR_FORMAT,"Actor call has unknown extra encoding");
    }
    return true;
}
static bool globals(application_q3_mod_actors *o,const qa_json_document *d,qa_json_id combat,uint32_t entry,qa_error *e)
{
    qa_bytes source=qa_json_source(d,qa_json_get(d,combat,"globals"));
    if (qa_json_type(d,qa_json_get(d,combat,"globals"))!=QA_JSON_ARRAY)
        return q3mod_fail(e,QA_ERROR_FORMAT,"Actor combat requires its declared scoped globals list");
    char prefix[96]; int n=snprintf(prefix,sizeof(prefix),"{\"entry\":%u,\"arguments\":[],\"returns\":\"int32\",\"globals\":",entry);
    if (n<0||(size_t)n>=sizeof(prefix)||source.size>SIZE_MAX-(size_t)n-1)
        return q3mod_fail(e,QA_ERROR_MEMORY,"Actor scoped globals extent overflows");
    size_t length=(size_t)n+source.size+1; uint8_t *bytes=malloc(length);
    if (!bytes) return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining original actor scoped globals");
    memcpy(bytes,prefix,(size_t)n); memcpy(bytes+n,source.data,source.size); bytes[length-1]='}';
    bool ok=application_q3_mod_call_create(o->options.profile,(qa_bytes){bytes,length},UINT32_C(1)<<Q3_MOD_TIME,&o->globals,e);
    free(bytes); return ok;
}
bool q3mod_actors_profile(application_q3_mod_actors *o,qa_error *e)
{
    qa_json_document *d=NULL;
    if (!qa_json_parse(application_q3_mod_declaration(o->options.profile),&d,e)) return false;
    qa_json_id root=qa_json_root(d),source=qa_json_get(d,root,"sourceActors"),combat=qa_json_get(d,root,"combat"),callbacks=qa_json_get(d,source,"callbacks");
    o->present=callbacks!=QA_JSON_NONE||combat!=QA_JSON_NONE;
    o->combat=combat!=QA_JSON_NONE; o->entity_record=o->options.profile->entity_record;
    bool ok=true;
    if (o->present&&(source==QA_JSON_NONE||o->entity_record>=o->options.profile->record_count))
        ok=q3mod_fail(e,QA_ERROR_FORMAT,"Actor semantics require genuine declared source actors");
    if (ok&&callbacks!=QA_JSON_NONE&&qa_json_type(d,callbacks)!=QA_JSON_OBJECT)
        ok=q3mod_fail(e,QA_ERROR_FORMAT,"Actor callbacks require their declared field object");
    const char *const kinds[]={"touch","use","pain","die","damage"};
    const char *const touch[]={"target","other","trace"},*const use[]={"target","other","activator"},*const pain[]={"target","attacker","amount"};
    const char *const die[]={"target","inflictor","attacker","amount","method"},*const damage[]={"target","inflictor","attacker","direction","point","amount","flags","method"};
    const char *const *roles[]={touch,use,pain,die,damage}; const size_t counts[]={3,3,3,5,8};
    for (size_t i=0;ok&&i<4;++i) {
        qa_json_id id=qa_json_get(d,callbacks,kinds[i]); o->callbacks[i]=id!=QA_JSON_NONE&&qa_json_type(d,id)!=QA_JSON_NULL;
        if (callbacks!=QA_JSON_NONE&&id==QA_JSON_NONE) ok=q3mod_fail(e,QA_ERROR_FORMAT,"Actor callback declaration omits a required nullable field");
        if (o->callbacks[i]) ok=field(o,d,id,o->entity_record,o->callback_fields+i,e);
    }
    o->legacy=!o->combat||qa_json_string_equal(d,qa_json_get(d,combat,"abi"),"q3-g-damage");
    if (ok&&o->combat&&!o->legacy&&!qa_json_string_equal(d,qa_json_get(d,combat,"abi"),"declared"))
        ok=q3mod_fail(e,QA_ERROR_FORMAT,"Unknown declared actor combat ABI");
    for (size_t i=0;ok&&i<MOD_ACTOR_CALLS;++i) {
        if (o->legacy) { o->calls[i].role_count=o->calls[i].count=counts[i]; for(size_t j=0;j<counts[i];++j) o->calls[i].roles[j]=(uint32_t)j; }
        else ok=call(o,d,qa_json_get(d,qa_json_get(d,combat,"calls"),kinds[i]),roles[i],counts[i],o->calls+i,e);
    }
    if (ok&&o->combat) {
        size_t code_count; const qa_qvm_instruction *code=qa_qvm_image_instructions(o->options.profile->image,&code_count);
        ok=callbacks!=QA_JSON_NONE&&word(d,qa_json_get(d,combat,"entry"),&o->damage_entry,e)&&o->damage_entry<code_count&&code[o->damage_entry].opcode==QA_QVM_ENTER&&
            field(o,d,qa_json_get(d,combat,"health"),o->entity_record,&o->health,e)&&field(o,d,qa_json_get(d,combat,"takedamage"),o->entity_record,&o->takedamage,e)&&
            field(o,d,qa_json_get(d,combat,"flags"),o->entity_record,&o->flags,e)&&word(d,qa_json_get(d,combat,"godmode"),&o->godmode,e)&&
            word(d,qa_json_get(d,combat,"noKnockback"),&o->no_knockback,e);
        if (ok&&(!o->godmode||!o->no_knockback||(o->legacy&&(o->godmode>INT32_MAX||o->no_knockback>INT32_MAX))))
            ok=q3mod_fail(e,QA_ERROR_FORMAT,"Actor combat masks are not genuine source bits");
        const char *const masks[]={"radius","noArmor","noKnockback","noProtection","noTeamProtection"}; uint32_t used=0;
        for(size_t i=0;ok&&i<5;++i) {
            if(o->legacy) o->damage_flags[i]=UINT32_C(1)<<i;
            else ok=word(d,qa_json_get(d,qa_json_get(d,combat,"damageFlags"),masks[i]),o->damage_flags+i,e);
            uint32_t mask=o->damage_flags[i];
            if(ok&&(!mask||(mask&(mask-1))||(used&mask))) ok=q3mod_fail(e,QA_ERROR_FORMAT,"Actor damage masks overlap");
            used|=mask;
        }
        o->mass_kind=MOD_ACTOR_MASS_CONSTANT; o->mass=200;
        qa_json_id mass=qa_json_get(d,combat,"mass");
        if(ok&&!o->legacy) {
            if(qa_json_string_equal(d,qa_json_get(d,mass,"kind"),"constant")) {
                ok=qa_json_number(d,qa_json_get(d,mass,"value"),&o->mass,e)&&isfinite(o->mass)&&o->mass>=0&&o->mass<=FLT_MAX;
            } else {
                bool floating=qa_json_string_equal(d,qa_json_get(d,mass,"storage"),"float32");
                ok=qa_json_string_equal(d,qa_json_get(d,mass,"kind"),"entity")&&(floating||qa_json_string_equal(d,qa_json_get(d,mass,"storage"),"int32"))&&
                    field(o,d,qa_json_get(d,mass,"offset"),o->entity_record,&o->mass_field,e);
                o->mass_kind=floating?MOD_ACTOR_MASS_FLOAT32:MOD_ACTOR_MASS_INT32;
            }
        }
        qa_json_id client=qa_json_get(d,combat,"client"); o->has_client=qa_json_type(d,client)!=QA_JSON_NULL;
        if(ok&&o->has_client) {
            qa_json_id record=qa_json_get(d,client,"record"); o->client_record=SIZE_MAX;
            for(size_t i=0;i<o->options.profile->record_count;++i) if(qa_json_string_equal(d,record,o->options.profile->records[i].id)) o->client_record=i;
            ok=o->client_record!=SIZE_MAX&&field(o,d,qa_json_get(d,client,"pointer"),o->entity_record,&o->client_pointer,e)&&
                field(o,d,qa_json_get(d,client,"health"),o->client_record,&o->client_health,e)&&field(o,d,qa_json_get(d,client,"armor"),o->client_record,&o->client_armor,e)&&
                field(o,d,qa_json_get(d,client,"team"),o->client_record,&o->client_team,e)&&qa_json_number(d,qa_json_get(d,client,"protection"),&o->protection,e)&&
                isfinite(o->protection)&&o->protection>=0&&o->protection<=1;
        }
        qa_json_id teams=qa_json_get(d,combat,"teams"); o->team_count=o->legacy?0:qa_json_size(d,teams);
        if(ok&&!o->legacy&&(qa_json_type(d,teams)!=QA_JSON_ARRAY||(!o->has_client&&o->team_count))) ok=false;
        if(ok&&o->team_count) { o->teams=calloc(o->team_count,sizeof(*o->teams)); if(!o->teams) ok=q3mod_fail(e,QA_ERROR_MEMORY,"Owning source actor team mapping"); }
        for(size_t i=0;ok&&i<o->team_count;++i) {
            qa_json_id row=qa_json_at(d,teams,i); int64_t value; qa_buffer name={0};
            ok=qa_json_i64(d,qa_json_get(d,row,"value"),&value,e)&&value>=INT32_MIN&&value<=INT32_MAX&&qa_json_string(d,qa_json_get(d,row,"team"),&name,e);
            if(ok) {
                const uint8_t *colon=memchr(name.data,':',name.size);
                ok=name.size&&!memchr(name.data,0,name.size)&&colon&&colon!=name.data&&colon!=name.data+name.size-1&&
                    qa_strings_intern(qa_session_strings(o->options.session),(qa_bytes){name.data,name.size},&o->teams[i].team,e);
            }
            qa_buffer_free(&name);
            for(size_t j=0;ok&&j<i;++j) if(o->teams[j].value==value) ok=false;
            if(ok) o->teams[i].value=(int32_t)value;
        }
        qa_json_id fields=qa_json_get(d,qa_json_at(d,qa_json_get(d,root,"actorRecords"),o->entity_record),"fields");
        for(size_t i=0;i<qa_json_size(d,fields);++i) if(qa_json_string_equal(d,qa_json_get(d,qa_json_at(d,fields,i),"binding"),"team")) o->has_team_field=true;
        if(ok&&o->has_team_field&&!o->options.team) ok=q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor team binding requires actual canonical match services");
        if(ok) ok=globals(o,d,combat,o->damage_entry,e);
    }
    qa_json_destroy(d);
    return ok||(e&&e->code!=QA_OK?false:q3mod_fail(e,QA_ERROR_FORMAT,"Invalid declared source actor semantics"));
}
