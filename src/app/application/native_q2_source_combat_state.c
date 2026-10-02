#include "guest_native_q2_private.h"
#include "native_q2_source_combat_state.h"
#include "guest_native_q2_combat_state.h"
#include <float.h>
#include <math.h>

typedef struct source_scalar {
    uint32_t offset;
    qa_native_value_type encoding;
} source_scalar;
struct application_native_q2_source_combat_state {
    struct application_native_q2 *engine;
    application_native_q2_armor *armor;
    source_scalar health,mass,damageable,flags;
    uint64_t invulnerable,no_knockback;
    bool rerelease;
    qa_q2_classic_cause_profile causes;
};
static bool scalar_parse(struct application_native_q2 *n,qa_json_id id,source_scalar *out,qa_error *e)
{
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    uint64_t offset;qa_native_entity_table table;
    static const char *const names[]={"int8","uint8","int16","uint16","int32","uint32","int64","uint64","float32","float64"};
    static const qa_native_value_type types[]={QA_NATIVE_I8,QA_NATIVE_U8,QA_NATIVE_I16,QA_NATIVE_U16,
        QA_NATIVE_I32,QA_NATIVE_U32,QA_NATIVE_I64,QA_NATIVE_U64,QA_NATIVE_F32,QA_NATIVE_F64};
    qa_json_id encoding=qa_json_get(d,id,"encoding");size_t index=0;
    while(index<10&&!qa_json_string_equal(d,encoding,names[index]))++index;
    if(index==10||!qa_json_u64(d,qa_json_get(d,id,"offset"),&offset,e)||offset>UINT32_MAX||
        !qa_native_entity_table_get(application_native_q2_callbacks_instance(n->callbacks),&table,e)||
        offset>table.stride||application_native_q2_field_size(types[index])>table.stride-offset)
        return application_fail(e,QA_ERROR_FORMAT,"Declared combat scalar exceeds its actual source edict");
    *out=(source_scalar){(uint32_t)offset,types[index]};return true;
}
static bool actor_address(application_native_q2_source_combat_state *o,qa_actor_id actor,
    qa_native_address *out,qa_error *e)
{
    struct application_native_q2 *n=o->engine;
    const qa_actor_record *r=qa_actors_get(qa_session_actors(n->provider->application->session),actor);
    qa_native_instance *native=application_native_q2_callbacks_instance(n->callbacks);
    qa_native_slot_binding slot;
    if(!r||!r->has_source||r->owner!=n->provider->owner||
        !application_native_q2_callbacks_storage_current(n->callbacks,e)||
        !qa_native_slot(native,r->source_slot,&slot,e))return false;
    if(slot.kind!=QA_NATIVE_SLOT_OWNED||slot.owner!=r->owner||slot.source_slot!=r->source_slot||
        !qa_actor_id_equal(slot.actor,actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared combat lost its full owned SourceActor");
    return qa_native_entity_address(native,r->source_slot,out,e);
}
static bool scalar_read(application_native_q2_source_combat_state *o,qa_native_address base,
    source_scalar field,double *out,qa_error *e)
{
    return base<=UINT64_MAX-field.offset&&application_native_q2_callbacks_scalar_read(
        o->engine->callbacks,base+field.offset,field.encoding,out,e)&&
        (isfinite(*out)||application_fail(e,QA_ERROR_FORMAT,"Declared combat source scalar is not finite"));
}
static bool projection_address(application_native_q2_source_combat_state *o,qa_actor_id actor,
    qa_native_address *out,qa_error *e)
{
    if(!o||!out||!application_native_q2_callbacks_transfer_current(o->engine->callbacks)||
        !application_native_q2_callbacks_storage_current(o->engine->callbacks,e)||
        !qa_actors_get(qa_session_actors(o->engine->provider->application->session),actor))
        return application_fail(e,QA_ERROR_ARGUMENT,"Declared damage observation requires its actual live transfer target");
    qa_native_instance *native=application_native_q2_callbacks_instance(o->engine->callbacks);
    qa_native_entity_table table;if(!qa_native_entity_table_get(native,&table,e))return false;
    for(uint32_t i=1;i<table.count;++i) {
        qa_native_slot_binding slot;if(!qa_native_slot(native,i,&slot,e))return false;
        if((slot.kind==QA_NATIVE_SLOT_OWNED||slot.kind==QA_NATIVE_SLOT_BORROWED)&&qa_actor_id_equal(slot.actor,actor))
            return qa_native_entity_address(native,i,out,e);
    }
    return application_fail(e,QA_ERROR_ARGUMENT,"Declared damage has no retained physical target projection");
}
bool application_native_q2_source_combat_state_create(struct application_native_q2 *n,
    application_native_q2_source_combat_state **out,qa_error *e)
{
    if(!n||!out||*out||!n->callbacks)return application_fail(e,QA_ERROR_ARGUMENT,"Declared combat requires its real callback owner");
    const qa_json_document *d=application_native_q2_callbacks_document(n->callbacks);
    qa_json_id combat=qa_json_get(d,qa_json_get(d,qa_json_root(d),"sourceActors"),"combat");
    if(combat==QA_JSON_NONE)return true;
    application_native_q2_source_combat_state *o=calloc(1,sizeof(*o));
    if(!o)return application_fail(e,QA_ERROR_MEMORY,"Retaining declared SourceActor combat fields");
    o->engine=n;*out=o;
    if(!scalar_parse(n,qa_json_get(d,combat,"health"),&o->health,e)||
        !scalar_parse(n,qa_json_get(d,combat,"mass"),&o->mass,e)||
        !scalar_parse(n,qa_json_get(d,combat,"takedamage"),&o->damageable,e)||
        !scalar_parse(n,qa_json_get(d,combat,"flags"),&o->flags,e))return false;
    qa_json_id flags=qa_json_get(d,combat,"flags"),damage=qa_json_get(d,combat,"damage"),
        causes=qa_json_get(d,combat,"causes"),armor=qa_json_get(d,combat,"armor");
    if(o->flags.encoding==QA_NATIVE_F32||o->flags.encoding==QA_NATIVE_F64||
        !qa_json_u64(d,qa_json_get(d,flags,"invulnerable"),&o->invulnerable,e)||
        !qa_json_u64(d,qa_json_get(d,flags,"noKnockback"),&o->no_knockback,e))
        return application_fail(e,QA_ERROR_FORMAT,"Declared combat traits require integer source masks");
    size_t bits=application_native_q2_field_size(o->flags.encoding)*8;
    if(bits<64&&((o->invulnerable>>bits)||(o->no_knockback>>bits)))
        return application_fail(e,QA_ERROR_FORMAT,"Declared combat masks exceed their actual integer field");
    o->rerelease=qa_json_string_equal(d,qa_json_get(d,damage,"abi"),"q2-rerelease");
    if((!o->rerelease&&!qa_json_string_equal(d,qa_json_get(d,damage,"abi"),"q2-classic"))||
        !qa_json_string_equal(d,qa_json_get(d,causes,"edition"),o->rerelease?"rerelease":"classic"))
        return application_fail(e,QA_ERROR_FORMAT,"Declared combat cause family differs from its damage ABI");
    if(!o->rerelease){qa_json_id game=qa_json_get(d,causes,"game");
        if(qa_json_string_equal(d,game,"base"))o->causes=QA_Q2_NATIVE_BASE;
        else if(qa_json_string_equal(d,game,"xatrix"))o->causes=QA_Q2_NATIVE_XATRIX;
        else if(qa_json_string_equal(d,game,"rogue"))o->causes=QA_Q2_NATIVE_ROGUE;
        else if(qa_json_string_equal(d,game,"ctf"))o->causes=QA_Q2_NATIVE_CTF;
        else return application_fail(e,QA_ERROR_FORMAT,"Declared combat requires its actual classic cause roster");}
    bool none=qa_json_string_equal(d,qa_json_get(d,armor,"kind"),"none"),
        q2=qa_json_string_equal(d,qa_json_get(d,armor,"kind"),"q2");
    if(!none&&!q2&&!qa_json_string_equal(d,qa_json_get(d,armor,"kind"),"source"))
        return application_fail(e,QA_ERROR_FORMAT,"Declared combat armor storage kind is unknown");
    application_native_q2_armor_options options={.callbacks=n->callbacks,
        .strings=qa_session_strings(n->provider->application->session),.owner=n->provider->owner};
    return application_native_q2_armor_create(&options,none?QA_JSON_NONE:qa_json_get(d,armor,"regular"),
        none?QA_JSON_NONE:qa_json_get(d,armor,"power"),q2,&o->armor,e);
}
bool application_native_q2_source_combat_state_destroy(application_native_q2_source_combat_state **out,qa_error *e)
{
    if(!out||!*out)return true;
    if(!application_native_q2_armor_destroy(&(*out)->armor,e))return false;
    free(*out);*out=NULL;return true;
}
static bool state_at(application_native_q2_source_combat_state *o,qa_actor_id actor,
    qa_native_address base,qa_combat_state *out,qa_error *e)
{
    double health,mass,damageable,flags;qa_combat_state state={0};
    if(!scalar_read(o,base,o->health,&health,e)||
        !scalar_read(o,base,o->mass,&mass,e)||!scalar_read(o,base,o->damageable,&damageable,e)||
        !scalar_read(o,base,o->flags,&flags,e)||!application_native_q2_armor_read(o->armor,actor,&state.armor,e))return false;
    if(fabs(health)>FLT_MAX||fabs(mass)>FLT_MAX||(double)(float)health!=health||(double)(float)mass!=mass||
        flags!=trunc(flags)||fabs(flags)>9007199254740991.0)
        return application_fail(e,QA_ERROR_UNSUPPORTED,"Declared combat exceeds exact canonical scalar storage");
    uint64_t value=flags<0?(uint64_t)(int64_t)flags:(uint64_t)flags;
    state.health=(float)health;state.mass=(float)mass;state.can_take_damage=damageable!=0;
    state.invulnerable=(value&o->invulnerable)!=0;state.no_knockback=(value&o->no_knockback)!=0;
    qa_application *app=o->engine->provider->application;
    state.team=app->modes?qa_modes_combat_team(app->modes,app->primary_mode,actor,0):0;
    *out=state;return true;
}
bool application_native_q2_source_combat_state_read(application_native_q2_source_combat_state *o,
    qa_actor_id actor,qa_combat_state *out,qa_error *e)
{
    if(!o||!out)return application_fail(e,QA_ERROR_ARGUMENT,"Declared combat read has no source owner or output");
    qa_native_address base,after;
    return actor_address(o,actor,&base,e)&&state_at(o,actor,base,out,e)&&
        actor_address(o,actor,&after,e)&&base==after;
}
bool application_native_q2_source_combat_projection_read(application_native_q2_source_combat_state *o,
    qa_actor_id actor,qa_combat_state *out,qa_error *e)
{
    if(!out)return false;
    qa_native_address base,after;
    return projection_address(o,actor,&base,e)&&state_at(o,actor,base,out,e)&&
        projection_address(o,actor,&after,e)&&base==after;
}
bool application_native_q2_source_combat_projection_health(application_native_q2_source_combat_state *o,
    qa_actor_id actor,qa_native_address *out,size_t *bytes,qa_error *e)
{
    qa_native_address base;if(!out||!bytes||!projection_address(o,actor,&base,e))return false;
    *out=base+o->health.offset;*bytes=application_native_q2_field_size(o->health.encoding);return true;
}
bool application_native_q2_source_combat_projection_armor_observe(application_native_q2_source_combat_state *o,
    qa_actor_id actor,application_native_q2_armor_changed_fn changed,void *context,
    application_native_q2_armor_watch **out,qa_error *e)
{
    qa_native_address base;return projection_address(o,actor,&base,e)&&
        application_native_q2_armor_observe(o->armor,actor,changed,context,out,e);
}
bool application_native_q2_source_combat_health_write(application_native_q2_source_combat_state *o,
    qa_actor_id actor,float value,qa_error *e)
{
    qa_native_address base;
    return o&&application_native_q2_field_value(o->health.encoding,value,e)&&actor_address(o,actor,&base,e)&&
        application_native_q2_callbacks_scalar_write(o->engine->callbacks,base+o->health.offset,o->health.encoding,value,e);
}
bool application_native_q2_source_combat_armor_validate(application_native_q2_source_combat_state *o,
    qa_actor_id actor,const qa_armor *armor,qa_error *e)
{
    qa_native_address base;return o&&actor_address(o,actor,&base,e)&&application_native_q2_armor_validate(o->armor,actor,armor,e);
}
bool application_native_q2_source_combat_armor_write(application_native_q2_source_combat_state *o,
    qa_actor_id actor,const qa_armor *armor,qa_error *e)
{
    qa_native_address base;return o&&actor_address(o,actor,&base,e)&&application_native_q2_armor_write(o->armor,actor,armor,e);
}
bool application_native_q2_source_combat_armor_normalize(application_native_q2_source_combat_state *o,
    qa_actor_id actor,const qa_armor *input,qa_armor *out,qa_error *e)
{
    qa_native_address base;return o&&actor_address(o,actor,&base,e)&&application_native_q2_armor_normalize_legacy(o->armor,actor,input,out,e);
}
bool application_native_q2_source_combat_cause_read(application_native_q2_source_combat_state *o,
    const qa_native_value *value,uint32_t flags,qa_damage_cause *out,qa_error *e)
{return o&&application_q2_native_cause_read(o->rerelease,o->causes,value,flags,out,e);}
bool application_native_q2_source_combat_cause_lower(application_native_q2_source_combat_state *o,
    const qa_damage_cause *cause,uint8_t mod[3],qa_native_value *out,qa_error *e)
{return o&&application_q2_native_cause_lower(o->rerelease,o->causes,cause,mod,out,e);}
