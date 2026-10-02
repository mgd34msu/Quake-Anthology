#include "remote_unified_private.h"
#include "remote_unified_q2.h"
#include "qa/hud_q2.h"
#include <float.h>
#include <math.h>

typedef struct q2_model {
    struct q2_model *next;
    char *path;
    qa_scene_model *scene;
} q2_model;
typedef struct q2_alias { uint32_t number; qa_actor_id actor; } q2_alias;
typedef struct q2_bank {
    struct q2_bank *next;
    struct frontend_unified_q2 *owner;
    char *content;
    qa_vfs *files;
    const qa_product *product;
    qa_scene_resources *images;
    qa_material_library *materials;
    frontend_remote_q2_effects *effects;
    q2_model *models;
    q2_alias *aliases;
    size_t alias_count;
} q2_bank;
struct frontend_unified_q2 {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    frontend_unified_events *events;
    q2_bank *banks;
    qa_unified_document *frame, *prepared_frame;
    qa_hud *hud;
    char *help, *layout;
    char **config;
    size_t config_count;
    int32_t inventory[256];
    qa_hud_q2_table table;
    double seconds, prepared_seconds;
    uint64_t frame_number, prepared_number;
    qa_scene_light *lights;
    size_t light_count, light_capacity;
    unsigned busy;
};
static qa_json_id get(const qa_json_document *j,qa_json_id row,const char *name)
{ return qa_json_get(j,row,name); }
static bool number(const qa_unified_document *d,qa_json_id row,double *out,qa_error *e)
{
    return (qa_unified_document_number(d,row,out,e) && isfinite(*out)) ||
        frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified Q2 scalar is not finite");
}
static bool real(const qa_unified_document *d,qa_json_id row,float *out,qa_error *e)
{
    double n; if (!number(d,row,&n,e)) return false;
    if (fabs(n)>FLT_MAX) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified Q2 scalar exceeds float storage");
    *out=(float)n; return true;
}
static bool integer(const qa_unified_document *d,qa_json_id row,int32_t *out,qa_error *e)
{
    double n; if (!number(d,row,&n,e)) return false;
    if (n<INT32_MIN || n>INT32_MAX || trunc(n)!=n)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified Q2 integer exceeds its source word");
    *out=(int32_t)n; return true;
}
static bool vector(const qa_unified_document *d,qa_json_id row,qa_vec3 *out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    return real(d,get(j,row,"x"),&out->x,e) && real(d,get(j,row,"y"),&out->y,e) && real(d,get(j,row,"z"),&out->z,e);
}
static bool string(const qa_unified_document *d,qa_json_id row,qa_buffer *out,qa_error *e)
{
    if (!qa_json_string(qa_unified_document_json(d),row,out,e)) return false;
    if (!memchr(out->data,0,out->size)) return true;
    qa_buffer_free(out); return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified Q2 text contains NUL");
}
static bool actor(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_actor_id *out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    if (qa_json_type(j,row)==QA_JSON_NULL) { *out=(qa_actor_id){0}; return true; }
    uint64_t slot,generation;
    return qa_json_u64(j,get(j,row,"slot"),&slot,e) && slot<=UINT32_MAX &&
        qa_json_u64(j,get(j,row,"generation"),&generation,e) &&
        frontend_remote_unified_actor(o->replica,(uint32_t)slot,generation,out,e);
}
static bool current(frontend_unified_q2 *o,qa_error *e)
{ return o && frontend_unified_media_current(o->media) && frontend_remote_unified_current(o->replica,e); }
static bool model(void *ctx,const char *path,bool acquire,qa_scene_model **out,qa_error *e)
{
    q2_bank *b=ctx;
    for (q2_model *m=b->models;m;m=m->next) if (!strcmp(m->path,path)) { *out=m->scene; return true; }
    if (!acquire) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified Q2 cold model is absent from its actual media cache");
    q2_model *m=calloc(1,sizeof(*m));
    if (!m) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q2 effect model admission");
    m->path=malloc(strlen(path)+1);
    if (!m->path) { free(m); return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q2 model path"); }
    strcpy(m->path,path);
    frontend_unified_model actual;
    qa_scene_image_options options={.family=QA_SCENE_Q2,.usage=QA_IMAGE_USAGE_SKIN,.wrap=QA_SCENE_REPEAT,
        .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=255};
    bool okay=frontend_unified_media_model(b->owner->media,b->content,path,QA_SCENE_Q2,&options,&actual,e);
    if (!okay) { free(m->path); free(m); return false; }
    m->scene=actual.scene; m->next=b->models; b->models=m; *out=m->scene; return true;
}
static bool pose_actor(q2_bank *b,qa_actor_id id,frontend_remote_q2_effects_pose *out,qa_error *e)
{
    frontend_unified_q2 *o=b->owner;
    const qa_unified_document *d=o->frame;
    if (!d) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 effect pose has no committed received frame");
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id root=qa_unified_document_root(d),models=get(j,root,"models");
    for (size_t i=0;i<qa_json_size(j,models);++i) {
        qa_json_id row=qa_json_at(j,models,i); qa_actor_id actual;
        if (!qa_json_string_equal(j,get(j,row,"family"),"q2") || !qa_json_string_equal(j,get(j,row,"content"),b->content)) continue;
        if (!actor(o,d,get(j,row,"actor"),&actual,e)) return false;
        if (!qa_actor_id_equal(actual,id)) continue;
        frontend_remote_q2_effects_pose p={.actor=id};
        if (!vector(d,get(j,row,"origin"),&p.origin,e) || !vector(d,get(j,row,"angles"),&p.angles,e) ||
            !integer(d,get(j,row,"frame"),&p.frame,e)) return false;
        qa_buffer path={0};
        if (!string(d,get(j,row,"path"),&path,e)) return false;
        qa_scene_image_options options={.family=QA_SCENE_Q2,.usage=QA_IMAGE_USAGE_SKIN,.wrap=QA_SCENE_REPEAT,
            .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=255};
        frontend_unified_model m;
        bool okay=frontend_unified_media_model(o->media,b->content,(const char *)path.data,QA_SCENE_Q2,&options,&m,e);
        qa_buffer_free(&path); if (!okay) return false;
        if (m.model) {
            p.bounds=(qa_bounds){.mins=qa_v3(m.model->bounds.min[0],m.model->bounds.min[1],m.model->bounds.min[2]),
                .maxs=qa_v3(m.model->bounds.max[0],m.model->bounds.max[1],m.model->bounds.max[2])};
            p.radius=m.model->radius; p.bounds_present=true;
        }
        *out=p; return true;
    }
    return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 effect actor has no received content-owned model pose");
}
static bool pose(void *ctx,uint32_t number_id,frontend_remote_q2_effects_pose *out,qa_error *e)
{
    q2_bank *b=ctx;
    for (size_t i=0;i<b->alias_count;++i) if (b->aliases[i].number==number_id) {
        if (!pose_actor(b,b->aliases[i].actor,out,e)) return false;
        out->number=number_id; return true;
    }
    return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 effect source number has no received full actor witness");
}
static bool full_pose(void *ctx,qa_actor_id actor_id,frontend_remote_q2_effects_pose *out,qa_error *e)
{ return pose_actor(ctx,actor_id,out,e); }
static bool source_current(void *ctx,const frontend_remote_q2_effects_source *s,qa_error *e)
{
    q2_bank *b=ctx;
    return s && s->context==b && s->identity==frontend_unified_events_audio_owner(b->owner->events) &&
        s->content_generation==frontend_remote_unified_epoch(b->owner->replica) &&
        s->files==b->files && s->images==b->images && s->materials==b->materials &&
        s->world==frontend_unified_media_world(b->owner->media) &&
        s->map==qa_executable_recipe_map(frontend_remote_unified_recipe(b->owner->replica)) && current(b->owner,e);
}
static bool sound(void *ctx,const char *path,qa_vec3 origin,qa_actor_id actor_id,double ms,
    int32_t channel,float volume,float attenuation,double delay,qa_error *e)
{
    q2_bank *b=ctx;
    return frontend_unified_events_sound_path(b->owner->events,b->content,path,actor_id,origin,ms,channel,volume,attenuation,delay,e);
}
static uint64_t nanoseconds(double seconds)
{ long double n=(long double)seconds*1e9L; return n<=0?0:n>=(long double)UINT64_MAX?UINT64_MAX:(uint64_t)n; }
static bool hit(void *ctx,int32_t damage,qa_error *e)
{
    q2_bank *b=ctx;
    if (!current(b->owner,e)) return false;
    qa_hud_hit_marker(b->owner->hud,(float)damage,nanoseconds(b->owner->seconds+.2)); return true;
}
static frontend_remote_q2_effects_source source(q2_bank *b)
{
    return (frontend_remote_q2_effects_source){.identity=frontend_unified_events_audio_owner(b->owner->events),
        .content_generation=frontend_remote_unified_epoch(b->owner->replica),
        .protocol={.kind=b->product->edition==QA_EDITION_RERELEASE?QA_NET_Q2KEX_2023:QA_NET_Q2_34},
        .map=qa_executable_recipe_map(frontend_remote_unified_recipe(b->owner->replica)),.files=b->files,
        .images=b->images,.materials=b->materials,.world=frontend_unified_media_world(b->owner->media),.white=qa_scene_white(b->images),.context=b,
        .current=source_current,.actor=pose,.actor_pose=full_pose,.model=model,.sound=sound,.hit_marker=hit};
}
static bool bank(frontend_unified_q2 *o,const char *content,bool effects,q2_bank **out,qa_error *e)
{
    q2_bank *b=o->banks;
    while (b && strcmp(b->content,content)) b=b->next;
    if (!b) {
        b=calloc(1,sizeof(*b));
        if (!b) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 CLIENT content");
        b->owner=o; b->content=malloc(strlen(content)+1);
        if (!b->content) { free(b); return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q2 content identity"); }
        strcpy(b->content,content);
        qa_font_library *fonts; qa_audio_bank *sounds;
        bool okay=qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),content,&b->files,&b->product,e) &&
            b->product->family==QA_GAME_Q2 && frontend_unified_media_bank(o->media,content,&b->images,&b->materials,&fonts,&sounds,e);
        if (!okay) { free(b->content); free(b); return false; }
        b->next=o->banks; o->banks=b;
    }
    if (effects && !b->effects) {
        frontend_remote_q2_effects_source s=source(b);
        if (!frontend_remote_q2_effects_create(&s,&b->effects,e)) return false;
    }
    *out=b; return true;
}
static bool hud_read(void *ctx,const qa_hud_frame *frame,qa_hud_data *out,qa_error *e)
{
    frontend_unified_q2 *o=ctx;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (!o->busy || frame->seat!=d->physical_seat) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 message HUD lost its CLIENT seat");
    *out=(qa_hud_data){.source_vitals=true,.help_title=o->help}; return true;
}
static qa_hud_options hud_options(frontend_unified_q2 *o)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    return (qa_hud_options){.application=d->application,.ui=o->frontend->seats[d->physical_seat].ui,
        .seat=d->physical_seat,.context=o,.read=hud_read};
}
bool frontend_unified_q2_create(qa_frontend *f,frontend_remote_unified *r,frontend_unified_media *media,
    frontend_unified_events *events,frontend_unified_q2 **out,qa_error *e)
{
    if (!f || !r || !media || !events || !out || *out) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 CLIENT needs actual replica media and event route");
    frontend_unified_q2 *o=calloc(1,sizeof(*o));
    if (!o) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q2 normalized CLIENT state");
    o->frontend=f; o->replica=r; o->media=media; o->events=events;
    qa_hud_options h=hud_options(o);
    if (!current(o,e) || !qa_hud_create(&h,&o->hud,e)) { free(o); return false; }
    *out=o; return true;
}
static bool alias(q2_bank *b,int32_t number_id,qa_actor_id actor_id,qa_error *e)
{
    if (number_id<0 || !actor_id.registry) return true;
    for (size_t i=0;i<b->alias_count;++i) if (b->aliases[i].number==(uint32_t)number_id) {
        b->aliases[i].actor=actor_id; return true;
    }
    if (b->alias_count==SIZE_MAX/sizeof(*b->aliases)) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Q2 received number ledger overflow");
    void *rows=realloc(b->aliases,(b->alias_count+1)*sizeof(*b->aliases));
    if (!rows) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 source number witness");
    b->aliases=rows; b->aliases[b->alias_count++]=(q2_alias){(uint32_t)number_id,actor_id}; return true;
}
static bool temporary(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id event,
    qa_net_protocol_id *protocol,qa_q2_temp_entity *t,qa_actor_id actors[7],qa_error *e)
{
    static const char *const names[]={"entity1","entity2","count","color","time","position1","position2","direction","offset"};
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id p=get(j,event,"profile"),fields=get(j,event,"fields");
    if (qa_json_string_equal(j,p,"q2-34")) *protocol=(qa_net_protocol_id){.kind=QA_NET_Q2_34};
    else if (qa_json_string_equal(j,p,"q2-kex-2023")) *protocol=(qa_net_protocol_id){.kind=QA_NET_Q2KEX_2023};
    else return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 temporary event has no genuine source protocol");
    int32_t type;
    if (!integer(d,get(j,event,"type"),&type,e) || type<0 || type>UINT8_MAX ||
        qa_json_type(j,fields)!=QA_JSON_ARRAY || qa_json_size(j,fields)>7) return false;
    *t=(qa_q2_temp_entity){.type=(uint8_t)type,.field_count=qa_json_size(j,fields)};
    memset(actors,0,7*sizeof(*actors));
    for (size_t i=0;i<t->field_count;++i) {
        qa_json_id row=qa_json_at(j,fields,i); qa_q2_temp_field *f=t->fields+i; size_t n;
        for (n=0;n<9;++n) if (qa_json_string_equal(j,get(j,row,"name"),names[n])) break;
        if (n==9) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unknown Q2 temporary field name");
        f->name=(qa_q2_temp_field_name)n;
        if (qa_json_string_equal(j,get(j,row,"kind"),"integer")) {
            f->kind=QA_Q2_TEMP_INTEGER;
            if (!integer(d,get(j,row,"value"),&f->value.integer,e)) return false;
            if ((n==QA_Q2_TEMP_ENTITY1 || n==QA_Q2_TEMP_ENTITY2) &&
                !actor(o,d,get(j,row,"actor"),actors+i,e)) return false;
        } else if (qa_json_string_equal(j,get(j,row,"kind"),"vector")) {
            f->kind=QA_Q2_TEMP_VECTOR; qa_vec3 v;
            if (!vector(d,get(j,row,"value"),&v,e)) return false;
            f->value.vector[0]=v.x; f->value.vector[1]=v.y; f->value.vector[2]=v.z;
        } else return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unknown Q2 temporary field kind");
    }
    uint8_t bytes[128]; qa_net_writer w; qa_q2_codec codec;
    qa_net_writer_init(&w,bytes,sizeof(bytes),e);
    return qa_q2_codec_init(&codec,*protocol,e) && qa_q2_temp_entity_write(&codec,&w,true,t);
}
static bool effect(const qa_unified_document *d,qa_json_id event,qa_q2_temp_entity *t,qa_error *e)
{
    static const struct { const char *name; uint8_t type,fields; } kinds[]={
        {"gunshot",QA_Q2_TE_GUNSHOT,2},{"blood",QA_Q2_TE_BLOOD,2},{"blaster",QA_Q2_TE_BLASTER,2},
        {"shotgun",QA_Q2_TE_SHOTGUN,2},{"sparks",QA_Q2_TE_SPARKS,2},{"screen-sparks",QA_Q2_TE_SCREEN_SPARKS,2},
        {"shield-sparks",QA_Q2_TE_SHIELD_SPARKS,2},{"bullet-sparks",QA_Q2_TE_BULLET_SPARKS,2},
        {"greenblood",QA_Q2_TE_GREENBLOOD,2},{"blaster2",QA_Q2_TE_BLASTER2,2},{"flechette",QA_Q2_TE_FLECHETTE,2},
        {"moreblood",QA_Q2_TE_MOREBLOOD,2},{"electric-sparks",QA_Q2_TE_ELECTRIC_SPARKS,2},
        {"splash",QA_Q2_TE_SPLASH,4},{"laser-sparks",QA_Q2_TE_LASER_SPARKS,4},
        {"welding-sparks",QA_Q2_TE_WELDING_SPARKS,4},{"tunnel-sparks",QA_Q2_TE_TUNNEL_SPARKS,4},
        {"explosion1",QA_Q2_TE_EXPLOSION1,1},{"explosion2",QA_Q2_TE_EXPLOSION2,1},
        {"rocket-explosion",QA_Q2_TE_ROCKET_EXPLOSION,1},{"grenade-explosion",QA_Q2_TE_GRENADE_EXPLOSION,1},
        {"rocket-explosion-water",QA_Q2_TE_ROCKET_EXPLOSION_WATER,1},{"grenade-explosion-water",QA_Q2_TE_GRENADE_EXPLOSION_WATER,1},
        {"bfg-explosion",QA_Q2_TE_BFG_EXPLOSION,1},{"bfg-bigexplosion",QA_Q2_TE_BFG_BIGEXPLOSION,1},
        {"boss-teleport",QA_Q2_TE_BOSSTPORT,1},{"other-teleport",QA_Q2_TE_TELEPORT_EFFECT,1}
    };
    const qa_json_document *j=qa_unified_document_json(d); size_t n;
    for (n=0;n<sizeof(kinds)/sizeof(*kinds);++n)
        if (qa_json_string_equal(j,get(j,event,"effect"),kinds[n].name)) break;
    if (n==sizeof(kinds)/sizeof(*kinds)) return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 effect has no genuine Source recipe");
    qa_vec3 origin,direction; int32_t count,color;
    if (!vector(d,get(j,event,"origin"),&origin,e) || !vector(d,get(j,event,"direction"),&direction,e) ||
        !integer(d,get(j,event,"count"),&count,e) || !integer(d,get(j,event,"color"),&color,e)) return false;
    *t=(qa_q2_temp_entity){.type=kinds[n].type,.field_count=kinds[n].fields};
    size_t at=0;
    if (t->field_count==4) t->fields[at++]=(qa_q2_temp_field){.name=QA_Q2_TEMP_COUNT,.kind=QA_Q2_TEMP_INTEGER,.value.integer=count};
    t->fields[at++]=(qa_q2_temp_field){.name=QA_Q2_TEMP_POSITION1,.kind=QA_Q2_TEMP_VECTOR,.value.vector={origin.x,origin.y,origin.z}};
    if (t->field_count>=2) t->fields[at++]=(qa_q2_temp_field){.name=QA_Q2_TEMP_DIRECTION,.kind=QA_Q2_TEMP_VECTOR,.value.vector={direction.x,direction.y,direction.z}};
    if (t->field_count==4) t->fields[at++]=(qa_q2_temp_field){.name=QA_Q2_TEMP_COLOR,.kind=QA_Q2_TEMP_INTEGER,.value.integer=color};
    return true;
}
static bool received_text(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id event,
    double seconds,bool center,bool console,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_buffer text={0};
    if (!string(d,get(j,event,"text"),&text,e)) return false;
    if (console) {
        if (text.size>SIZE_MAX-2) { qa_buffer_free(&text); return false; }
        void *p=realloc(text.data,text.size+2); if (!p) { qa_buffer_free(&text); return false; }
        text.data=p;
    }
    bool instant=true; double duration=3;
    qa_json_id value=get(j,event,"instant");
    if (center && value!=QA_JSON_NONE && !qa_json_bool(j,value,&instant,e)) { qa_buffer_free(&text); return false; }
    value=get(j,event,"durationSeconds");
    if (center && value!=QA_JSON_NONE && (!number(d,value,&duration,e) || duration<0)) { qa_buffer_free(&text); return false; }
    bool okay=center?qa_hud_center_print(o->hud,(const char *)text.data,nanoseconds(seconds),nanoseconds(duration),instant,UINT64_C(50000000),e):
        qa_hud_notify(o->hud,(const char *)text.data,qa_json_string_equal(j,get(j,event,"level"),"chat"),nanoseconds(seconds),UINT64_C(3000000000),e);
    if (okay && console) {
        text.data[text.size]='\n'; text.data[text.size+1]=0;
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        qa_console_emit(domain->console,&domain->command_context,(const char *)text.data);
    }
    qa_buffer_free(&text); return okay;
}
static bool message_valid(const qa_unified_document *d,qa_json_id event,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id kind=get(j,event,"kind");
    qa_buffer text={0}; bool okay=false;
    if (qa_json_string_equal(j,kind,"config-string")) {
        int32_t i; okay=integer(d,get(j,event,"index"),&i,e) && i>=0 && i<65536 && string(d,get(j,event,"value"),&text,e);
    } else if (qa_json_string_equal(j,kind,"q2-layout")) okay=string(d,get(j,event,"program"),&text,e);
    else if (qa_json_string_equal(j,kind,"q2-inventory")) {
        qa_json_id counts=get(j,event,"counts"); okay=qa_json_type(j,counts)==QA_JSON_ARRAY && qa_json_size(j,counts)==256;
        for (size_t i=0;okay && i<256;++i) { int32_t v; okay=integer(d,qa_json_at(j,counts,i),&v,e); }
    }
    qa_buffer_free(&text); return okay || frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 simulation message has no installed state transition");
}
bool frontend_unified_q2_validate(frontend_unified_q2 *o,bool simulation,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    if (!o || !d) return false;
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id event=get(j,row,"event"),kind=get(j,event,"kind");
    if (simulation) {
        qa_json_id payload=get(j,row,"payload");
        if (qa_json_string_equal(j,get(j,payload,"kind"),"message")) return message_valid(d,get(j,payload,"event"),e);
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2-temp-entity")) {
        qa_q2_temp_entity t; qa_net_protocol_id protocol; qa_actor_id actors[7];
        return temporary(o,d,get(j,row,"event"),&protocol,&t,actors,e);
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"effect")) {
        qa_q2_temp_entity t; return effect(d,event,&t,e);
    }
    if (!simulation && (qa_json_string_equal(j,kind,"centerprint") || qa_json_string_equal(j,kind,"print") || qa_json_string_equal(j,kind,"help"))) {
        qa_buffer text={0}; bool okay=string(d,get(j,event,"text"),&text,e);
        qa_buffer_free(&text); return okay;
    }
    return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 normalized event variant has no installed CLIENT handler");
}
bool frontend_unified_q2_presentation(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,bool *mirrored,qa_error *e)
{
    if (!o || !d || !mirrored || !current(o,e)) return false;
    *mirrored=false;
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id event=get(j,row,"event"),kind=get(j,event,"kind");
    double seconds;
    if (!number(d,get(j,row,"seconds"),&seconds,e)) return false;
    if (qa_json_string_equal(j,kind,"centerprint") || qa_json_string_equal(j,kind,"print")) {
        qa_json_id target=get(j,event,qa_json_string_equal(j,kind,"print")?"target":"actor");
        if (target==QA_JSON_NONE) target=get(j,event,"actor");
        qa_actor_id actual,viewer; uint32_t n;
        if (!actor(o,d,target,&actual,e) || !frontend_remote_unified_player(o->replica,&viewer,&n)) return false;
        if (actual.registry && !qa_actor_id_equal(actual,viewer)) return true;
        bool center=qa_json_string_equal(j,kind,"centerprint");
        /* Source print has an independent simulation console record. */
        if (!received_text(o,d,event,seconds,center,false,e)) return false;
        *mirrored=center; return true;
    }
    if (qa_json_string_equal(j,kind,"help")) {
        qa_buffer text={0};
        if (!string(d,get(j,event,"text"),&text,e)) return false;
        free(o->help); o->help=(char *)text.data; *mirrored=true; return true;
    }
    bool residual=qa_json_string_equal(j,get(j,row,"kind"),"q2-temp-entity");
    bool normalized=qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"effect");
    if (!residual && !normalized) return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 normalized presentation variant has no CLIENT handler");
    qa_q2_temp_entity t; qa_net_protocol_id protocol={0}; qa_actor_id actors[7]={0}; qa_buffer content={0};
    bool okay=(residual?temporary(o,d,event,&protocol,&t,actors,e):effect(d,event,&t,e)) &&
        string(d,get(j,row,"content"),&content,e) && number(d,get(j,row,"seconds"),&seconds,e);
    q2_bank *b=NULL;
    if (okay) okay=bank(o,(const char *)content.data,true,&b,e);
    qa_buffer_free(&content);
    if (okay && residual && source(b).protocol.kind!=protocol.kind)
        okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 temporary protocol differs from its declared content program");
    for (size_t i=0;okay && i<t.field_count;++i)
        if (t.fields[i].name==QA_Q2_TEMP_ENTITY1 || t.fields[i].name==QA_Q2_TEMP_ENTITY2)
            okay=alias(b,t.fields[i].value.integer,actors[i],e);
    if (okay) { ++o->busy;
        okay=frontend_remote_q2_effects_temporary(b->effects,&t,actors,seconds*1000,seconds*1000,e);
        --o->busy; }
    return okay;
}
bool frontend_unified_q2_simulation(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    if (!o || !d || o->busy || !current(o,e)) return false;
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id payload=get(j,row,"payload"),event=get(j,payload,"event"),kind=get(j,event,"kind");
    if (!qa_json_string_equal(j,get(j,payload,"kind"),"message") || !message_valid(d,event,e)) return false;
    if (qa_json_string_equal(j,kind,"q2-inventory")) {
        int32_t values[256]; qa_json_id counts=get(j,event,"counts");
        for (size_t i=0;i<256;++i) if (!integer(d,qa_json_at(j,counts,i),values+i,e)) return false;
        memcpy(o->inventory,values,sizeof(values)); return true;
    }
    qa_buffer text={0}; bool layout=qa_json_string_equal(j,kind,"q2-layout"); int32_t index=0;
    if (!string(d,get(j,event,layout?"program":"value"),&text,e) ||
        (!layout && !integer(d,get(j,event,"index"),&index,e))) { qa_buffer_free(&text); return false; }
    if (layout) { free(o->layout); o->layout=(char *)text.data; return true; }
    if ((size_t)index>=o->config_count) {
        size_t n=(size_t)index+1; void *p=realloc(o->config,n*sizeof(*o->config));
        if (!p) { qa_buffer_free(&text); return false; }
        o->config=p; memset(o->config+o->config_count,0,(n-o->config_count)*sizeof(*o->config)); o->config_count=n;
    }
    free(o->config[index]); o->config[index]=(char *)text.data; return true;
}
bool frontend_unified_q2_frame_prepare(frontend_unified_q2 *o,const qa_unified_document *d,qa_error *e)
{
    if (!o || o->busy || o->prepared_frame || !d || !current(o,e)) return false;
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id root=qa_unified_document_root(d);
    qa_json_id f=get(j,get(j,get(j,root,"output"),"snapshot"),"frame"),time=get(j,f,"time");
    double seconds; uint64_t n;
    if (!qa_json_u64(j,get(j,f,"frame"),&n,e) || !number(d,get(j,time,"value"),&seconds,e)) return false;
    if (qa_json_string_equal(j,get(j,time,"kind"),"milliseconds")) seconds/=1000;
    if (!frontend_unified_clone(d,&o->prepared_frame,e)) return false;
    o->prepared_seconds=seconds; o->prepared_number=n; return true;
}
void frontend_unified_q2_frame_commit(frontend_unified_q2 *o)
{
    if (!o || !o->prepared_frame || o->busy) return;
    qa_unified_document_destroy(o->frame); o->frame=o->prepared_frame; o->prepared_frame=NULL;
    o->seconds=o->prepared_seconds; o->frame_number=o->prepared_number;
}
bool frontend_unified_q2_frame_ready(frontend_unified_q2 *o,const qa_unified_document *d,qa_error *e)
{
    if (!o || o->busy || !o->prepared_frame || !d ||
        qa_unified_document_type(d)!=QA_UNIFIED_FRAME_DOCUMENT ||
        (o->hud && !qa_hud_idle(o->hud)) || !current(o,e))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 CLIENT has no returned prepared frame");
    for (q2_bank *b=o->banks;b;b=b->next) if (!frontend_remote_q2_effects_idle(b->effects))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 frame retains an effects callback");
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id root=qa_unified_document_root(d),f=get(j,get(j,get(j,root,"output"),"snapshot"),"frame"),t=get(j,f,"time");
    uint64_t epoch,n; double seconds;
    if (!qa_json_u64(j,get(j,root,"epoch"),&epoch,e) || !qa_json_u64(j,get(j,f,"frame"),&n,e) ||
        !number(d,get(j,t,"value"),&seconds,e)) return false;
    if (qa_json_string_equal(j,get(j,t,"kind"),"milliseconds")) seconds/=1000;
    else if (!qa_json_string_equal(j,get(j,t,"kind"),"seconds")) return false;
    return (epoch==frontend_remote_unified_epoch(o->replica) && n==o->prepared_number && seconds==o->prepared_seconds) ||
        frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 prepared frame differs from publication");
}
void frontend_unified_q2_frame_abort(frontend_unified_q2 *o)
{ if (o && !o->busy) { qa_unified_document_destroy(o->prepared_frame); o->prepared_frame=NULL; } }
bool frontend_unified_q2_world(frontend_unified_q2 *o,const qa_scene_view *view,const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *e)
{
    if (!o || !view || !frame || o->busy || !current(o,e)) return false;
    qa_actor_id viewer; uint32_t source_number;
    if (!frontend_remote_unified_player(o->replica,&viewer,&source_number)) return false;
    frontend_remote_q2_effects_sample sample={.milliseconds=o->seconds*1000,.server_milliseconds=o->seconds*1000,
        .fraction=1,.frame_sequence=o->frame_number,.view=*view,.viewer=viewer,.hardware=o->frontend->gl!=NULL,.world_input=world};
    ++o->busy; bool okay=true;
    for (q2_bank *b=o->banks;okay && b;b=b->next) if (b->effects) {
        okay=frontend_remote_q2_effects_draw(b->effects,&sample,true,true,frame,e);
    }
    --o->busy; return okay;
}
bool frontend_unified_q2_lights(frontend_unified_q2 *o,const qa_scene_view *view,const qa_scene_world_input *world,
    const qa_scene_light **out,size_t *count,qa_error *e)
{
    if (!o || !view || !out || !count || o->busy || !current(o,e)) return false;
    qa_actor_id viewer; uint32_t number_id;
    if (!frontend_remote_unified_player(o->replica,&viewer,&number_id)) return false;
    frontend_remote_q2_effects_sample sample={.milliseconds=o->seconds*1000,.server_milliseconds=o->seconds*1000,
        .fraction=1,.frame_sequence=o->frame_number,.view=*view,.viewer=viewer,.hardware=o->frontend->gl!=NULL,.world_input=world};
    o->light_count=0; ++o->busy; bool okay=true;
    for (q2_bank *b=o->banks;okay && b;b=b->next) if (b->effects) {
        const qa_scene_light *lights; size_t n;
        okay=frontend_remote_q2_effects_prepare(b->effects,&sample,&lights,&n,e);
        if (!okay) break;
        if (n>SIZE_MAX-o->light_count || o->light_count+n>SIZE_MAX/sizeof(*o->lights)) {
            okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Unified Q2 light span overflow"); break;
        }
        size_t needed=o->light_count+n;
        if (needed>o->light_capacity) {
            void *rows=realloc(o->lights,needed*sizeof(*o->lights));
            if (!rows) { okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 CLIENT light span"); break; }
            o->lights=rows; o->light_capacity=needed;
        }
        if (n) memcpy(o->lights+o->light_count,lights,n*sizeof(*lights));
        o->light_count=needed;
    }
    --o->busy; if (okay) { *out=o->lights; *count=o->light_count; } return okay;
}
bool frontend_unified_q2_hud(frontend_unified_q2 *o,qa_ui *ui,qa_scene_rect viewport,qa_scene_frame *frame,qa_error *e)
{
    (void)ui;
    if (!o || o->busy || !current(o,e)) return false;
    qa_actor_id player; uint32_t n;
    if (!frontend_remote_unified_player(o->replica,&player,&n)) return false;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    ++o->busy;
    bool okay=qa_hud_draw(o->hud,&(qa_hud_frame){.seat=d->physical_seat,.actor=player,.time_ns=nanoseconds(o->seconds),
        .viewport=viewport,.safe_area=viewport,.scale=1,.visible=true},frame,e);
    --o->busy; return okay;
}
bool frontend_unified_q2_idle(const frontend_unified_q2 *o)
{
    if (!o) return true;
    if (o->busy || o->prepared_frame || (o->hud && !qa_hud_idle(o->hud))) return false;
    for (q2_bank *b=o->banks;b;b=b->next) if (!frontend_remote_q2_effects_idle(b->effects)) return false;
    return true;
}
bool frontend_unified_q2_destroy(frontend_unified_q2 **slot,qa_error *e)
{
    if (!slot || !*slot) return true;
    frontend_unified_q2 *o=*slot;
    if (!frontend_unified_q2_idle(o)) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 CLIENT still has callback custody");
    while (o->banks) {
        q2_bank *b=o->banks;
        if (!frontend_remote_q2_effects_destroy(&b->effects,e)) return false;
        while (b->models) { q2_model *m=b->models; b->models=m->next; free(m->path); free(m); }
        o->banks=b->next; free(b->aliases); free(b->content); free(b);
    }
    if (o->hud && !qa_hud_destroy(o->hud,e)) return false;
    qa_unified_document_destroy(o->frame); qa_unified_document_destroy(o->prepared_frame);
    for (size_t i=0;i<o->config_count;++i) free(o->config[i]);
    free(o->config); free(o->help); free(o->layout); free(o->lights); free(o); *slot=NULL; return true;
}
static bool capsule(qa_source_save_io *io,qa_buffer *b)
{
    size_t n=b->size;
    if (!qa_source_save_count(io,&n,SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (n>io->input.size-io->offset) return frontend_unified_fail(io->error,QA_ERROR_FORMAT,"Truncated Q2 CLIENT child capsule");
        b->data=n?malloc(n):NULL; b->size=n;
        if (n && !b->data) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Restoring Q2 CLIENT child capsule");
    }
    return qa_source_save_bytes(io,b->data,n);
}
static bool saved_text(qa_source_save_io *io,char **p)
{
    bool read=io->direction==QA_SOURCE_SAVE_READ,present=*p!=NULL;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) return true;
    size_t n=read?0:strlen(*p);
    if (!qa_source_save_count(io,&n,SIZE_MAX-1)) return false;
    if (read) {
        if (n>io->input.size-io->offset) return frontend_unified_fail(io->error,QA_ERROR_FORMAT,"Truncated Q2 CLIENT text");
        *p=malloc(n+1);
        if (!*p) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Restoring Q2 CLIENT text");
        (*p)[n]=0;
    }
    return qa_source_save_bytes(io,*p,n) && !memchr(*p,0,n);
}
static bool saved_actor(qa_source_save_io *io,const frontend_unified_q2_refs *refs,qa_actor_id *a)
{
    qa_saved_actor_id s={0}; bool read=io->direction==QA_SOURCE_SAVE_READ;
    if (!read && !refs->effects.actor_encode(refs->effects.context,*a,&s,io->error)) return false;
    if (!qa_source_save_bool(io,&s.present) || !qa_source_save_u32(io,&s.slot) || !qa_source_save_u64(io,&s.generation)) return false;
    return !read || refs->effects.actor_decode(refs->effects.context,s,a,io->error);
}
static bool q2_fields(qa_source_save_io *io,frontend_unified_q2 *o,const frontend_unified_q2_refs *refs)
{
    bool read=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[5]={'Q','U','Q','2','1'};
    uint32_t epoch=frontend_remote_unified_epoch(o->replica);
    uint32_t physical=frontend_remote_unified_domain_read(o->replica)->physical_seat;
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QUQ21",sizeof(magic)) ||
        !qa_source_save_u32(io,&epoch) || epoch!=frontend_remote_unified_epoch(o->replica) ||
        !qa_source_save_u32(io,&physical) || physical!=frontend_remote_unified_domain_read(o->replica)->physical_seat ||
        !qa_source_save_f64(io,&o->seconds) || !isfinite(o->seconds) || !qa_source_save_u64(io,&o->frame_number)) return false;
    bool frame=o->frame!=NULL;
    if (!qa_source_save_bool(io,&frame)) return false;
    if (frame) {
        qa_buffer bytes={0};
        bool okay=read?capsule(io,&bytes):qa_unified_document_encode(o->frame,&bytes,io->error) && capsule(io,&bytes);
        if (okay && read) okay=qa_unified_document_decode(QA_UNIFIED_FRAME_DOCUMENT,(qa_bytes){bytes.data,bytes.size},&o->frame,io->error);
        qa_buffer_free(&bytes); if (!okay) return false;
    } else if (o->seconds || o->frame_number) return false;
    if (!saved_text(io,&o->help) || !saved_text(io,&o->layout) ||
        !qa_source_save_count(io,&o->config_count,65536)) return false;
    if (read && o->config_count) {
        if (o->config_count>io->input.size-io->offset) return false;
        o->config=calloc(o->config_count,sizeof(*o->config));
        if (!o->config) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Restoring Q2 CLIENT configstrings");
    }
    for (size_t i=0;i<o->config_count;++i) if (!saved_text(io,o->config+i)) return false;
    for (size_t i=0;i<256;++i) if (!qa_source_save_i32(io,o->inventory+i)) return false;
    if (!qa_source_save_count(io,&o->table.row_count,11) || !qa_source_save_count(io,&o->table.column_count,6) ||
        !qa_source_save_bytes(io,o->table.cells,sizeof(o->table.cells))) return false;
    for (size_t i=0;i<11;++i) for (size_t k=0;k<6;++k)
        if (!memchr(o->table.cells[i][k],0,sizeof(o->table.cells[i][k]))) return false;
    for (size_t i=0;i<5;++i) if (!qa_source_save_f32(io,o->table.columns+i) || !isfinite(o->table.columns[i])) return false;
    size_t count=0; for (q2_bank *b=o->banks;b;b=b->next) ++count;
    if (!qa_source_save_count(io,&count,65536)) return false;
    q2_bank *b=o->banks;
    for (size_t i=0;i<count;++i) {
        char *content=read?NULL:b->content;
        if (!saved_text(io,&content) || !content || !*content) { if (read) free(content); return false; }
        if (read) {
            for (q2_bank *previous=o->banks;previous;previous=previous->next)
                if (!strcmp(previous->content,content)) { free(content); return false; }
            bool okay=bank(o,content,false,&b,io->error); free(content); if (!okay) return false;
        }
        size_t models=0; for (q2_model *m=b->models;m;m=m->next) ++models;
        if (!qa_source_save_count(io,&models,65536)) return false;
        q2_model *m=b->models,**tail=&b->models;
        for (size_t k=0;k<models;++k) {
            if (read) { m=calloc(1,sizeof(*m)); if (!m) return false; *tail=m; tail=&m->next; }
            uint64_t id=0; bool present=m->scene!=NULL;
            if (!saved_text(io,&m->path) || !m->path || !qa_source_save_bool(io,&present)) return false;
            if (present) {
                if ((!read && !refs->model_encode(refs->effects.context,m->scene,&id,io->error)) ||
                    !qa_source_save_u64(io,&id) || !id ||
                    (read && (!refs->model_decode(refs->effects.context,id,&m->scene,io->error) || !m->scene))) return false;
            }
            if (!read) m=m->next;
        }
        if (!qa_source_save_count(io,&b->alias_count,65536)) return false;
        if (read && b->alias_count) {
            if (b->alias_count>(io->input.size-io->offset)/17) return false;
            b->aliases=calloc(b->alias_count,sizeof(*b->aliases)); if (!b->aliases) return false;
        }
        for (size_t k=0;k<b->alias_count;++k) {
            if (!qa_source_save_u32(io,&b->aliases[k].number) || !saved_actor(io,refs,&b->aliases[k].actor) ||
                !b->aliases[k].actor.registry) return false;
            for (size_t p=0;p<k;++p) if (b->aliases[p].number==b->aliases[k].number) return false;
        }
        bool effects=b->effects!=NULL;
        if (!qa_source_save_bool(io,&effects)) return false;
        if (effects) {
            qa_buffer bytes={0}; frontend_remote_q2_effects_source s=source(b);
            bool okay=read?capsule(io,&bytes):frontend_remote_q2_effects_checkpoint(b->effects,&refs->effects,&bytes,io->error) && capsule(io,&bytes);
            if (okay && read) okay=frontend_remote_q2_effects_restore(&s,&refs->effects,(qa_bytes){bytes.data,bytes.size},&b->effects,io->error);
            qa_buffer_free(&bytes); if (!okay) return false;
        }
        if (!read) b=b->next;
    }
    qa_buffer bytes={0};
    bool okay=read?capsule(io,&bytes):qa_hud_checkpoint(o->hud,NULL,&bytes,io->error) && capsule(io,&bytes);
    if (okay && read) { qa_hud_options h=hud_options(o); okay=qa_hud_restore((qa_bytes){bytes.data,bytes.size},&h,NULL,&o->hud,io->error); }
    qa_buffer_free(&bytes); return okay;
}
bool frontend_unified_q2_checkpoint(frontend_unified_q2 *o,const frontend_unified_q2_refs *refs,qa_buffer *out,qa_error *e)
{
    if (!o || !refs || !refs->model_encode || !refs->effects.actor_encode || !out || out->data || out->size ||
        !frontend_unified_q2_idle(o) || !current(o,e)) return false;
    qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,NULL,e) && q2_fields(&io,o,refs) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_unified_q2_restore(qa_frontend *f,frontend_remote_unified *replica,frontend_unified_media *media,
    frontend_unified_events *events,const frontend_unified_q2_refs *refs,qa_bytes bytes,frontend_unified_q2 **out,qa_error *e)
{
    if (!f || !replica || !media || !events || !refs || !refs->model_decode || !refs->effects.actor_decode || !out || *out) return false;
    frontend_unified_q2 *o=calloc(1,sizeof(*o)); if (!o) return false;
    o->frontend=f; o->replica=replica; o->media=media; o->events=events;
    qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && q2_fields(&io,o,refs) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!okay) { qa_error ignored={0}; frontend_unified_q2_destroy(&o,&ignored);
        if (e && e->code==QA_OK) frontend_unified_fail(e,QA_ERROR_FORMAT,"Invalid Q2 private CLIENT state"); return false; }
    *out=o; return true;
}
