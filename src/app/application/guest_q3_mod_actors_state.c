#include "guest_q3_mod_actors_private.h"

bool q3mod_actors_word(application_q3_mod_actors *o,uint32_t at,int32_t *value,qa_error *e)
{ uint8_t bytes[4]; if(!qa_qvm_read(o->options.vm,at,bytes,4,e)) return false; *value=qa_load_i32le(bytes); return true; }
bool q3mod_actors_write(application_q3_mod_actors *o,uint32_t at,int32_t value,qa_error *e)
{ uint8_t bytes[4]; qa_store_u32le(bytes,(uint32_t)value); return qa_qvm_write(o->options.vm,at,(qa_bytes){bytes,4},e); }
static bool row_current(mod_actor_row *r,qa_error *e)
{
    uint32_t pointer; application_q3_mod_actors *o=r->owner;
    return !r->retired&&q3mod_storage_current(o->options.mod,e)&&qa_actors_get(qa_session_actors(o->options.session),r->actor)&&o->options.owned(o->options.context,r->actor)&&
        o->options.pointer(o->options.context,r->actor,&pointer,e)&&pointer==r->pointer ? true :
        q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor semantics left its actual full source row");
}
static bool client(mod_actor_row *r,uint32_t *out,qa_error *e)
{
    application_q3_mod_actors *o=r->owner; *out=0;
    if(!o->has_client) return true;
    int32_t raw;
    if(!q3mod_actors_word(o,r->pointer+o->client_pointer,&raw,e)) return false;
    if(!raw) return true;
    mod_record *record=o->options.profile->records+o->client_record;
    uint32_t at=(uint32_t)raw; uint64_t end=(uint64_t)record->address+(uint64_t)record->stride*record->capacity;
    if(at<record->address||at>=end||(at-record->address)%record->stride)
        return q3mod_fail(e,QA_ERROR_FORMAT,"Actor combat client pointer leaves its declared source record");
    *out=at; return true;
}
bool q3mod_actors_state(void *context,qa_combat_state *out,qa_error *e)
{
    mod_actor_row *r=context; application_q3_mod_actors *o=r->owner;
    if(!out||!o->combat||!row_current(r,e)) return false;
    uint32_t c; int32_t health,damageable,flags,armor=0,team=0;
    if(!q3mod_actors_word(o,r->pointer+o->health,&health,e)||!q3mod_actors_word(o,r->pointer+o->takedamage,&damageable,e)||
        !q3mod_actors_word(o,r->pointer+o->flags,&flags,e)||!client(r,&c,e)) return false;
    if(c&&(!q3mod_actors_word(o,c+o->client_armor,&armor,e)||!q3mod_actors_word(o,c+o->client_team,&team,e))) return false;
    qa_combat_state value={.health=(float)health,.mass=(float)o->mass,.can_take_damage=damageable!=0,
        .invulnerable=((uint32_t)flags&o->godmode)!=0,.no_knockback=((uint32_t)flags&o->no_knockback)!=0};
    if((double)value.health!=health) return q3mod_fail(e,QA_ERROR_UNSUPPORTED,"Source health exceeds exact canonical binary32 representation");
    if(c) value.armor.regular=(qa_regular_armor){.kind=QA_ARMOR_Q3,.points=(float)armor,.protection=(float)o->protection};
    if(c&&(double)value.armor.regular.points!=armor) return q3mod_fail(e,QA_ERROR_UNSUPPORTED,"Source armor exceeds exact canonical representation");
    if(o->mass_kind!=MOD_ACTOR_MASS_CONSTANT) {
        int32_t raw; if(!q3mod_actors_word(o,r->pointer+o->mass_field,&raw,e)) return false;
        if(o->mass_kind==MOD_ACTOR_MASS_FLOAT32) memcpy(&value.mass,&raw,4); else value.mass=(float)raw;
        if(!isfinite(value.mass)||value.mass<0) return q3mod_fail(e,QA_ERROR_FORMAT,"Source actor mass is invalid");
    }
    if(o->has_team_field) { if(!o->options.team(o->options.context,r->actor,&value.team,e)) return false; }
    else if(o->legacy&&(team==1||team==2)) {
        const char *name=team==1?"q3:1":"q3:2";
        if(!qa_strings_intern(qa_session_strings(o->options.session),(qa_bytes){(const uint8_t *)name,4},&value.team,e)) return false;
    } else if(!o->legacy) for(size_t i=0;i<o->team_count;++i) if(o->teams[i].value==team) value.team=o->teams[i].team;
    if(!row_current(r,e)) return false;
    *out=value; return true;
}
bool q3mod_actors_health(void *context,float health,qa_error *e)
{
    mod_actor_row *r=context; application_q3_mod_actors *o=r->owner; int32_t value; uint32_t c;
    return q3mod_actors_current(o,e)&&row_current(r,e)&&q3mod_scalar_word(health,MOD_INT32,&value,e)&&client(r,&c,e)&&
        q3mod_actors_write(o,r->pointer+o->health,value,e)&&(!c||q3mod_actors_write(o,c+o->client_health,value,e));
}
bool q3mod_actors_armor_valid(void *context,const qa_armor *armor,qa_error *e)
{
    mod_actor_row *r=context; uint32_t c; int32_t value;
    if(!armor||!row_current(r,e)||!client(r,&c,e)) return false;
    if(armor->powered.kind!=QA_POWER_NONE||(armor->regular.kind!=QA_ARMOR_NONE&&armor->regular.kind!=QA_ARMOR_Q3)||
        (!c&&armor->regular.kind!=QA_ARMOR_NONE)) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Source actor armor requires genuine declared Q3 storage");
    return q3mod_scalar_word(armor->regular.kind==QA_ARMOR_NONE?0:armor->regular.points,MOD_INT32,&value,e);
}
bool q3mod_actors_armor(void *context,const qa_armor *armor,qa_error *e)
{
    mod_actor_row *r=context; uint32_t c; int32_t value;
    return q3mod_actors_current(r->owner,e)&&q3mod_actors_armor_valid(r,armor,e)&&client(r,&c,e)&&(!c||
        (q3mod_scalar_word(armor->regular.kind==QA_ARMOR_NONE?0:armor->regular.points,MOD_INT32,&value,e)&&q3mod_actors_write(r->owner,c+r->owner->client_armor,value,e)));
}
bool q3mod_actors_entry(mod_actor_row *r,size_t kind,uint32_t *out,qa_error *e)
{
    application_q3_mod_actors *o=r->owner; *out=0;
    if(kind>=4||!row_current(r,e)) return false;
    if(!o->callbacks[kind]) return true;
    int32_t value; if(!q3mod_actors_word(o,r->pointer+o->callback_fields[kind],&value,e)) return false;
    if(!value) return true;
    size_t count; const qa_qvm_instruction *code=qa_qvm_image_instructions(o->options.profile->image,&count);
    if(value<0||(uint32_t)value>=count||code[value].opcode!=QA_QVM_ENTER) return q3mod_fail(e,QA_ERROR_FORMAT,"Actor callback field is not a genuine source function");
    *out=(uint32_t)value; return true;
}
