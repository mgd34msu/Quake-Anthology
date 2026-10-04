#include "remote_unified_q1.h"
#include "remote_unified_private.h"
#include "remote_unified_save.h"
#include "selected_effects_particles.h"
#include "legacy_render_policy.h"
#include "scene_identity.h"
#include "received_music.h"
#include "qa/localization.h"
#include "qa/ui_language.h"
#include "qa/text.h"
#include "qa/q1_text.h"
#include "qa/ui_preferences.h"
#include "qa/player_progress.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { Q1_LIGHTS=64, Q1_BEAMS=32, Q1_POWERS=9 };
typedef enum q1_event_kind {
    Q1_PARTICLES,Q1_EFFECT,Q1_COLORS,Q1_BEAM,Q1_STYLE,Q1_STATIC,Q1_WEAPON,
    Q1_POWER,Q1_MESSAGE,Q1_STOP,Q1_AMBIENT,Q1_SOUND,Q1_CTF_STATUS,Q1_CTF_CAPTURE,
    Q1_PROMPT,Q1_CLEAR_PROMPT,Q1_LOG,Q1_TOTAL,Q1_FOUND,Q1_ACHIEVEMENT,Q1_CLIENT,Q1_SKY,
    Q1_FOG,Q1_FINALE,Q1_ACTION,Q1_MUSIC,Q1_PAUSE,Q1_COMPLETED
} q1_event_kind;
typedef struct q1_event {
    q1_event_kind kind;
    qa_buffer content,text,name,extra,provider;
    qa_saved_actor_id actor;
    qa_vec3 origin,end,angles;
    double seconds,a,b,c,d,sky_factor;
    uint64_t sequence,owner_generation;
    bool actor_present,flag,muzzle;
    const qa_unified_document *document;
    qa_json_id value;
} q1_event;
typedef struct q1_activation {
    struct q1_activation *next;
    char *provider;
    uint64_t generation;
    bool retired;
} q1_activation;
typedef struct q1_light {
    qa_vec3 origin;
    double born,until;
    float radius,decay,minimum;
    uint64_t identity;
} q1_light;
typedef struct q1_beam {
    qa_saved_actor_id actor;
    qa_vec3 start,end;
    double until;
    uint8_t kind;
    uint32_t roll_seed;
} q1_beam;
typedef struct q1_static {
    struct q1_static *next;
    char *path;
    qa_vec3 origin,angles;
    uint32_t frame,skin;
    frontend_unified_model model;
} q1_static;
typedef struct q1_ambient {
    struct q1_ambient *next;
    char *path;
    qa_audio_asset *asset;
    qa_audio_mixer *mixer;
    qa_vec3 origin;
    float volume,attenuation;
    uint64_t identity;
    bool saved_installed;
} q1_ambient;
typedef struct q1_group {
    frontend_unified_q1 *parent;
    frontend_received_music *music;
    struct q1_group *next;
    char *content;
    q1_activation *activation;
    const qa_product *product;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_audio_bank *sounds;
    qa_localization *localization;
    char language[64];
    frontend_fx_particles particles;
    qa_scene_image *particle_image;
    q1_light lights[Q1_LIGHTS];
    q1_beam beams[Q1_BEAMS];
    q1_static *statics;
    q1_ambient *ambient;
    char *styles[256];
    uint64_t style_sequences[256],sky_sequence;
    struct {char *name,*social,*info;double colors,frags,ping;uint64_t sequences[6];bool fields[6],present,has_ping;} clients[256];
    char *sky_name;
    qa_scene_image *sky[6];
    bool sky_found;
    double sampled;
    bool has_sample;
} q1_group;
struct frontend_unified_q1 {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    frontend_unified_events *events;
    frontend_unified_q1_options options;
    qa_builtin_random random;
    qa_localization_pool *localizations;
    qa_hud *hud;
    q1_group *groups;
    q1_activation *activations,*weapon_activation,*prompt_activation,*ctf_activation;
    q1_activation *power_activations[Q1_POWERS];
    qa_unified_document *weapon,*prompt;
    qa_unified_document *finale;
    qa_scene_image *finale_image;
    q1_activation *fog_activation,*finale_activation;
    struct {qa_vec3 previous,target;double previous_density,target_density,start,duration,sky_factor;bool active;} fog;
    bool finale_banner;
    q1_activation *pause_activation;
    bool music_retiring;
    double monsters,total_monsters,secrets,total_secrets;
    const qa_unified_document *preparing_document;
    qa_unified_document *restored_preparing;
    qa_hud_timer timers[Q1_POWERS];
    double powers[Q1_POWERS];
    qa_hud_value ctf[4];
    qa_hud_value display_bars[6];
    char *prompt_title,**prompt_lines;
    size_t prompt_count;
    uint32_t epoch;
    uint64_t frame,prepared_frame;
    double seconds,prepared_seconds,bonus_until,capture_until;
    bool has_frame,prepared,busy,ctf_present,restoring;
    bool monsters_present,secrets_present;
    qa_scene_light *scene_lights;
    size_t scene_capacity;
    float scene_styles[256];
    qa_hud_score *scores;
    size_t score_capacity,score_count;
};
static const char *const powers[Q1_POWERS]={"quad","invulnerability","invisibility","suit",
    "hipnotic:wetsuit","hipnotic:empathy","rogue:shield","rogue:antigrav","mg3:lavasuit"};
static qa_json_id field(const qa_json_document *j,qa_json_id row,const char *key)
{ return qa_json_get(j,row,key); }
static bool fail(qa_error *e,const char *message)
{ return frontend_unified_fail(e,QA_ERROR_FORMAT,message); }
static bool number(const qa_unified_document *d,qa_json_id row,double *out,qa_error *e)
{ return (qa_unified_document_number(d,row,out,e) && isfinite(*out)) || fail(e,"Q1 presentation scalar is not finite"); }
static bool word(const qa_unified_document *d,qa_json_id row,double *out,double maximum,qa_error *e)
{ return number(d,row,out,e) && ((*out>=0 && *out<=maximum && trunc(*out)==*out) || fail(e,"Q1 presentation word exceeds its source domain")); }
static bool vec(const qa_unified_document *d,qa_json_id row,qa_vec3 *out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); double x,y,z;
    if(!number(d,field(j,row,"x"),&x,e) || !number(d,field(j,row,"y"),&y,e) ||
        !number(d,field(j,row,"z"),&z,e) || fabs(x)>FLT_MAX || fabs(y)>FLT_MAX || fabs(z)>FLT_MAX) return fail(e,"Q1 presentation vector exceeds float storage");
    *out=qa_v3((float)x,(float)y,(float)z); return true;
}
static bool string(const qa_unified_document *d,qa_json_id row,qa_buffer *out,qa_error *e)
{ return qa_json_string(qa_unified_document_json(d),row,out,e) &&
    (!memchr(out->data,0,out->size) || fail(e,"Q1 presentation text contains NUL")); }
static bool wire(const qa_unified_document *d,qa_json_id row,qa_saved_actor_id *out,bool *present,bool optional,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    if(optional && qa_json_type(j,row)==QA_JSON_NULL) { *present=false; *out=(qa_saved_actor_id){0}; return true; }
    uint64_t slot,generation;
    if(!qa_json_u64(j,field(j,row,"slot"),&slot,e) || slot>UINT32_MAX ||
        !qa_json_u64(j,field(j,row,"generation"),&generation,e)) return fail(e,"Q1 presentation actor lacks its full wire identity");
    *out=(qa_saved_actor_id){.slot=(uint32_t)slot,.generation=generation}; *present=true; return true;
}
static bool same_wire(qa_saved_actor_id a,qa_saved_actor_id b)
{ return a.slot==b.slot && a.generation==b.generation; }
static uint64_t ns(double value)
{ return value<=0?0:value>=18446744073.709551615?UINT64_MAX:(uint64_t)(value*1e9); }
static void event_free(q1_event *p)
{ qa_buffer_free(&p->content);qa_buffer_free(&p->text);qa_buffer_free(&p->name);qa_buffer_free(&p->extra);qa_buffer_free(&p->provider); }
static bool owner_parse(const qa_unified_document *d,qa_json_id value,q1_event *p,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    if(value==QA_JSON_NONE || qa_json_type(j,value)==QA_JSON_NULL)return true;
    return string(d,field(j,value,"provider"),&p->provider,e) && p->provider.size>0 &&
        qa_json_u64(j,field(j,value,"generation"),&p->owner_generation,e) && p->owner_generation>0;
}
bool frontend_unified_q1_current(const frontend_unified_q1 *o)
{
    return o && frontend_unified_media_current(o->media) &&
        frontend_remote_unified_current(o->replica,NULL) && o->epoch==frontend_remote_unified_epoch(o->replica);
}
static bool checkpoint_current(const frontend_unified_q1 *o,qa_error *e)
{
    return o && (o->frontend->capture || o->frontend->source_restoring) &&
        frontend_unified_media_current(o->media) && frontend_remote_unified_checkpoint_current(o->replica,e) &&
        o->epoch==frontend_remote_unified_epoch(o->replica);
}
static bool mutable(frontend_unified_q1 *o,qa_error *e)
{
    return (frontend_unified_q1_current(o) && !o->restoring && !o->frontend->capture && !o->frontend->resource_inventory &&
        !o->frontend->source_restoring) || frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 CLIENT presentation has a foreign or captured parent");
}
static bool parse(frontend_unified_q1 *o,const qa_unified_document *d,qa_json_id row,q1_event *p,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id kind=field(j,row,"kind"),v=field(j,row,"event"),k=field(j,v,"kind");
    p->document=d;p->value=v;
    if(!number(d,field(j,row,"seconds"),&p->seconds,e) || !qa_json_u64(j,field(j,row,"sequence"),&p->sequence,e) ||
        !string(d,field(j,row,"content"),&p->content,e) || !owner_parse(d,field(j,row,"owner"),p,e)) return false;
    const qa_product *product=qa_catalog_find(qa_executable_recipe_catalog(frontend_remote_unified_recipe(o->replica)),(char *)p->content.data);
    if(!product || product->family!=QA_GAME_Q1) return fail(e,"Q1 event content is not an admitted Q1 provider");
    bool q1=qa_json_string_equal(j,kind,"q1"),composition=qa_json_string_equal(j,kind,"q1-composition");
    if(qa_json_string_equal(j,kind,"q1-fog") && qa_json_string_equal(j,k,"transition")){
        p->kind=Q1_FOG;qa_json_id transition=field(j,v,"transition"),previous=field(j,transition,"previous"),target=field(j,transition,"target");
        return wire(d,field(j,v,"player"),&p->actor,&p->actor_present,true,e) && vec(d,field(j,previous,"color"),&p->origin,e) &&
            vec(d,field(j,target,"color"),&p->end,e) && number(d,field(j,previous,"density"),&p->a,e) &&
            number(d,field(j,target,"density"),&p->b,e) && number(d,field(j,transition,"start"),&p->c,e) &&
            number(d,field(j,transition,"duration"),&p->d,e) && number(d,field(j,v,"skyFactor"),&p->sky_factor,e);
    }
    if((qa_json_string_equal(j,kind,"q1-level") || q1) && qa_json_string_equal(j,k,"finale")){
        p->kind=Q1_FINALE;p->a=4;
        return string(d,field(j,v,"text"),&p->text,e) && (q1?(word(d,field(j,v,"stage"),&p->a,6,e) && p->a>=1):word(d,field(j,v,"track"),&p->b,255,e));
    }
    if(qa_json_string_equal(j,kind,"view-reset")){
        p->kind=Q1_ACTION;p->value=row;
        return wire(d,field(j,row,"actor"),&p->actor,&p->actor_present,false,e) && vec(d,field(j,row,"angles"),&p->angles,e);
    }
    if(qa_json_string_equal(j,kind,"music")){
        if(qa_json_string_equal(j,k,"cd-track")){p->kind=Q1_MUSIC;return word(d,field(j,v,"track"),&p->a,255,e);}
        if(qa_json_string_equal(j,k,"pause")){p->kind=Q1_PAUSE;return qa_json_bool(j,field(j,v,"paused"),&p->flag,e);}
        return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 music event has no received player operation");
    }
    if(qa_json_string_equal(j,kind,"q1-session") && qa_json_string_equal(j,k,"level-completed")){
        p->kind=Q1_COMPLETED;return true;
    }
    if(qa_json_string_equal(j,kind,"q1-session") || qa_json_string_equal(j,kind,"q1-level")){
        p->kind=Q1_ACTION;return string(d,k,&p->name,e);
    }
    if(qa_json_string_equal(j,kind,"q1-client")) {
        p->kind=Q1_CLIENT;if(!word(d,field(j,v,"slot"),&p->a,255,e) || !string(d,k,&p->name,e))return false;
        if(qa_json_string_equal(j,k,"name") || qa_json_string_equal(j,k,"social") || qa_json_string_equal(j,k,"player-info"))return string(d,field(j,v,"value"),&p->text,e);
        if(qa_json_string_equal(j,k,"colors"))return word(d,field(j,v,"value"),&p->b,255,e);
        if(qa_json_string_equal(j,k,"frags") || qa_json_string_equal(j,k,"ping"))return number(d,field(j,v,"value"),&p->b,e);
        return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 client metadata has no actual field consumer");
    }
    if(qa_json_string_equal(j,kind,"q1-sky") && qa_json_string_equal(j,k,"skybox")) {p->kind=Q1_SKY;return string(d,field(j,v,"name"),&p->text,e);}
    if(!q1 && !composition) return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"This Q1 child does not consume that Source presentation domain");
    if(q1 && qa_json_string_equal(j,k,"particles")) {
        p->kind=Q1_PARTICLES;return vec(d,field(j,v,"origin"),&p->origin,e) && vec(d,field(j,v,"direction"),&p->end,e) && word(d,field(j,v,"color"),&p->a,255,e) && word(d,field(j,v,"count"),&p->b,INT32_MAX,e);
    }
    if(q1 && qa_json_string_equal(j,k,"effect")) {
        p->kind=Q1_EFFECT;
        if(!string(d,field(j,v,"effect"),&p->name,e) || !wire(d,field(j,v,"actor"),&p->actor,&p->actor_present,true,e) ||
            !vec(d,field(j,v,"origin"),&p->origin,e) || !word(d,field(j,v,"amount"),&p->a,INT32_MAX,e))return false;
        qa_json_id muzzle=field(j,v,"muzzle");
        if(muzzle!=QA_JSON_NONE && qa_json_type(j,muzzle)!=QA_JSON_NULL){p->muzzle=true;
            return vec(d,field(j,muzzle,"origin"),&p->end,e) && vec(d,field(j,muzzle,"angles"),&p->angles,e);}
        return true;
    }
    if(q1 && qa_json_string_equal(j,k,"colored-explosion")) {
        p->kind=Q1_COLORS;return vec(d,field(j,v,"origin"),&p->origin,e) && word(d,field(j,v,"colorStart"),&p->a,255,e) && word(d,field(j,v,"colorLength"),&p->b,256,e) && (p->b>0 || fail(e,"Q1 color explosion has an empty run"));
    }
    if(q1 && qa_json_string_equal(j,k,"beam")) {
        p->kind=Q1_BEAM;return wire(d,field(j,v,"actor"),&p->actor,&p->actor_present,false,e) && string(d,field(j,v,"style"),&p->name,e) && vec(d,field(j,v,"start"),&p->origin,e) && vec(d,field(j,v,"end"),&p->end,e);
    }
    if(q1 && qa_json_string_equal(j,k,"lightstyle")) {
        p->kind=Q1_STYLE;return word(d,field(j,v,"style"),&p->a,255,e) && string(d,field(j,v,"pattern"),&p->text,e);
    }
    if(q1 && qa_json_string_equal(j,k,"static-model")) {
        p->kind=Q1_STATIC;return string(d,field(j,v,"path"),&p->text,e) && vec(d,field(j,v,"origin"),&p->origin,e) && vec(d,field(j,v,"angles"),&p->angles,e) && word(d,field(j,v,"frame"),&p->a,UINT32_MAX,e) && word(d,field(j,v,"skin"),&p->b,UINT32_MAX,e) && word(d,field(j,v,"colorMap"),&p->c,255,e);
    }
    if(q1 && qa_json_string_equal(j,k,"weapon")) {
        p->kind=Q1_WEAPON;return wire(d,field(j,v,"player"),&p->actor,&p->actor_present,false,e) && string(d,field(j,v,"weapon"),&p->name,e) && string(d,field(j,v,"viewModel"),&p->text,e) && word(d,field(j,v,"frame"),&p->a,UINT32_MAX,e) && number(d,field(j,v,"punch"),&p->b,e);
    }
    if(q1 && qa_json_string_equal(j,k,"intermission")){
        p->kind=Q1_COMPLETED;
        return vec(d,field(j,v,"origin"),&p->origin,e) && vec(d,field(j,v,"angles"),&p->angles,e) &&
            string(d,field(j,v,"map"),&p->text,e) && number(d,field(j,v,"exitAfter"),&p->a,e) && number(d,field(j,v,"track"),&p->b,e);
    }
    if(q1 && (qa_json_string_equal(j,k,"teleport-player") || qa_json_string_equal(j,k,"camera"))){
        p->kind=Q1_ACTION;return string(d,k,&p->name,e);
    }
    if(q1 && qa_json_string_equal(j,k,"powerup")) {
        p->kind=Q1_POWER;return wire(d,field(j,v,"player"),&p->actor,&p->actor_present,false,e) && string(d,field(j,v,"powerup"),&p->name,e) && number(d,field(j,v,"expires"),&p->a,e);
    }
    if(q1 && qa_json_string_equal(j,k,"message")) {
        p->kind=Q1_MESSAGE;return wire(d,field(j,v,"player"),&p->actor,&p->actor_present,false,e) && string(d,field(j,v,"text"),&p->text,e) && qa_json_bool(j,field(j,v,"center"),&p->flag,e);
    }
    if(q1 && qa_json_string_equal(j,k,"stop-sound")) {
        p->kind=Q1_STOP;return wire(d,field(j,v,"actor"),&p->actor,&p->actor_present,false,e) && word(d,field(j,v,"channel"),&p->a,INT32_MAX,e);
    }
    if(q1 && (qa_json_string_equal(j,k,"sound") || qa_json_string_equal(j,k,"ambient"))) {
        p->kind=qa_json_string_equal(j,k,"sound")?Q1_SOUND:Q1_AMBIENT;
        qa_json_id origin=field(j,v,"origin");p->flag=origin!=QA_JSON_NONE && qa_json_type(j,origin)!=QA_JSON_NULL;
        return string(d,field(j,v,"path"),&p->text,e) && (p->flag?vec(d,origin,&p->origin,e):p->kind==Q1_SOUND) && number(d,field(j,v,"volume"),&p->a,e) && number(d,field(j,v,"attenuation"),&p->b,e) &&
            (p->kind==Q1_AMBIENT || wire(d,field(j,v,"actor"),&p->actor,&p->actor_present,false,e));
    }
    if(composition && qa_json_string_equal(j,k,"ctf-status")) {
        p->kind=Q1_CTF_STATUS;qa_json_id s=field(j,v,"status");return wire(d,field(j,v,"actor"),&p->actor,&p->actor_present,false,e) && number(d,field(j,s,"red"),&p->a,e) && number(d,field(j,s,"blue"),&p->b,e) && number(d,field(j,s,"flags"),&p->c,e) && number(d,field(j,s,"runeItems"),&p->d,e);
    }
    if(composition && qa_json_string_equal(j,k,"ctf-capture")) {
        p->kind=Q1_CTF_CAPTURE;return string(d,field(j,v,"team"),&p->name,e) && number(d,field(j,v,"total"),&p->a,e) && ((!strcmp((char *)p->name.data,"red") || !strcmp((char *)p->name.data,"blue")) || fail(e,"CTF capture has no actual team"));
    }
    if(composition && (qa_json_string_equal(j,k,"prompt") || qa_json_string_equal(j,k,"clear-prompt"))) {
        p->kind=qa_json_string_equal(j,k,"prompt")?Q1_PROMPT:Q1_CLEAR_PROMPT;
        return wire(d,field(j,v,"actor"),&p->actor,&p->actor_present,false,e) &&
            (p->kind==Q1_CLEAR_PROMPT || (string(d,field(j,v,"title"),&p->text,e) && qa_json_type(j,field(j,v,"choices"))==QA_JSON_ARRAY && qa_json_size(j,field(j,v,"choices"))<=256));
    }
    if(composition && qa_json_string_equal(j,k,"source-log")) {
        p->kind=Q1_LOG;return wire(d,field(j,v,"actor"),&p->actor,&p->actor_present,false,e) && string(d,field(j,v,"action"),&p->text,e);
    }
    if(q1 && qa_json_string_equal(j,k,"monster-total")) {p->kind=Q1_TOTAL;return number(d,field(j,v,"total"),&p->a,e);}
    if(q1 && (qa_json_string_equal(j,k,"secret") || qa_json_string_equal(j,k,"monster-killed"))) {
        p->kind=Q1_FOUND;return wire(d,field(j,v,"actor"),&p->actor,&p->actor_present,true,e) && number(d,field(j,v,"total"),&p->a,e) && number(d,field(j,v,"found"),&p->b,e);
    }
    if(q1 && qa_json_string_equal(j,k,"achievement")) {p->kind=Q1_ACHIEVEMENT;return wire(d,field(j,v,"player"),&p->actor,&p->actor_present,true,e) && string(d,field(j,v,"id"),&p->text,e);}
    return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 presentation event has no installed physical CLIENT consumer");
}
static bool owns(frontend_unified_q1 *o,const q1_event *p,qa_error *e)
{
    qa_actor_id actual,player;uint32_t source;
    if(!p->actor_present) return true;
    return frontend_remote_unified_actor(o->replica,p->actor.slot,p->actor.generation,&actual,e) &&
        frontend_remote_unified_player(o->replica,&player,&source) && qa_actor_id_equal(actual,player);
}
static bool progress_target(frontend_unified_q1 *o,const q1_event *p,qa_json_id row,
    bool *local,qa_player_progress **store,const char **value,uint32_t *seat,qa_error *e)
{
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    if(!domain || !o->frontend->seats || domain->physical_seat>=o->frontend->options.seats ||
        o->frontend->seats[domain->physical_seat].frontend!=o->frontend)
        return fail(e,"Q1 progress lost its actual local frontend seat");
    *local=!p->actor_present || same_wire(p->actor,o->replica->wire_player);
    const qa_json_document *j=qa_unified_document_json(p->document);qa_json_id recipient=field(j,row,"recipient");
    if(recipient!=QA_JSON_NONE && qa_json_type(j,recipient)!=QA_JSON_NULL){qa_saved_actor_id actor;bool present;
        if(!wire(p->document,recipient,&actor,&present,false,e))return false;
        *local=*local && same_wire(actor,o->replica->wire_player);
    }
    *value=p->kind==Q1_ACHIEVEMENT?(const char *)p->text.data:NULL;
    if(p->kind==Q1_COMPLETED){const qa_recipe_choices *choices=qa_executable_recipe_choices(frontend_remote_unified_recipe(o->replica));
        if(!choices || !choices->world.map || !*choices->world.map)return fail(e,"Q1 completion has no actual admitted map declaration");
        *value=choices->world.map;
    }
    *seat=o->frontend->seats[domain->physical_seat].id;
    *store=qa_application_player_progress(domain->application);
    return !*local || !*value || !**value || *store ||
        frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 progress has no installed player profile store");
}
static bool progress_record(frontend_unified_q1 *o,const q1_event *p,qa_json_id row,qa_error *e)
{
    bool local;qa_player_progress *store;const char *value;uint32_t seat;
    if(!progress_target(o,p,row,&local,&store,&value,&seat,e))return false;
    if(!local || !value || !*value)return true;
    const char *prefix=p->kind==Q1_ACHIEVEMENT?"achievement:":"level:";
    size_t a=strlen(prefix),b=p->content.size,c=strlen(value);
    if(b>SIZE_MAX-a-1 || c>SIZE_MAX-a-b-1)return fail(e,"Q1 progress identity exceeds storage");
    size_t length=a+b+1+c;uint8_t *identity=malloc(length);
    if(!identity)return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining received Q1 progress identity");
    memcpy(identity,prefix,a);memcpy(identity+a,p->content.data,b);identity[a+b]=':';memcpy(identity+a+b+1,value,c);
    char participant[32];int count=snprintf(participant,sizeof(participant),"local-seat:%u",(unsigned)seat);
    if(count<=0 || (size_t)count>=sizeof(participant)){free(identity);return fail(e,"Q1 progress local seat identity exceeds storage");}
    qa_progress_event event={.kind=p->kind==Q1_ACHIEVEMENT?QA_PROGRESS_ACHIEVEMENT:QA_PROGRESS_LEVEL_COMPLETED,
        .source=QA_GAME_Q1,.participant={(const uint8_t *)participant,(size_t)count},.event={identity,length}};
    if(p->kind==Q1_ACHIEVEMENT)event.value.award=(qa_bytes){(const uint8_t *)value,c};
    else event.value.map=(qa_bytes){(const uint8_t *)value,c};
    bool inserted;bool ok=qa_player_progress_record(store,&event,&inserted,e);
    free(identity);return ok;
}
static bool activation(frontend_unified_q1 *o,const q1_event *p,q1_activation **out,qa_error *e)
{
    *out=NULL;if(!p->provider.size)return true;
    for(q1_activation *a=o->activations;a;a=a->next)if(a->generation==p->owner_generation && !strcmp(a->provider,(char *)p->provider.data)){*out=a;return true;}
    q1_activation *a=calloc(1,sizeof(*a));if(!a)return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q1 Source activation");
    a->provider=malloc(p->provider.size+1);if(!a->provider){free(a);return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q1 Source provider identity");}
    memcpy(a->provider,p->provider.data,p->provider.size+1);a->generation=p->owner_generation;a->next=o->activations;o->activations=a;*out=a;return true;
}
static bool group(frontend_unified_q1 *o,const char *content,q1_activation *owner,q1_group **out,qa_error *e)
{
    for(q1_group *g=o->groups;g;g=g->next) if(g->activation==owner && !strcmp(g->content,content)) {*out=g;return true;}
    q1_group *g=calloc(1,sizeof(*g));qa_font_library *fonts;qa_vfs *files;
    if(!g) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q1 CLIENT content effects");
    g->content=malloc(strlen(content)+1);if(g->content) strcpy(g->content,content);
    if(!g->content || !qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),content,&files,&g->product,e) || g->product->family!=QA_GAME_Q1 ||
        !frontend_unified_media_bank(o->media,content,&g->images,&g->materials,&fonts,&g->sounds,e)) {free(g->content);free(g);return false;}
    g->parent=o;g->activation=owner;g->particles.family=QA_GAME_Q1;q1_group **tail=&o->groups;while(*tail) tail=&(*tail)->next;*tail=g;*out=g;return true;
}
static bool music_current(void *context,const frontend_music_origin *origin)
{
    q1_group *g=context;frontend_unified_q1 *o=g?g->parent:NULL;
    if(!o || !origin || (g->activation && g->activation->retired))return false;
    bool held=o->music_retiring || o->frontend->capture || o->frontend->resource_inventory || o->frontend->source_restoring;
    if(held){bool linked=false;
        for(frontend_remote_unified *row=o->frontend->remote_unified;row;row=row->next)if(row==o->replica){linked=true;break;}
        if(!linked || o->replica->busy || o->replica->frontend!=o->frontend ||
            o->frontend->application!=o->replica->options.domain.application || o->epoch!=o->replica->epoch ||
            frontend_unified_media_recipe(o->media)!=o->replica->recipe || !frontend_unified_media_current(o->media))return false;
    }else if(!frontend_unified_q1_current(o))return false;
    bool linked=false;for(q1_group *actual=o->groups;actual;actual=actual->next)if(actual==g){linked=true;break;}
    qa_executable_recipe *recipe=frontend_remote_unified_recipe(o->replica);qa_vfs *files=NULL;const qa_product *product=NULL;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    return linked && domain && origin->receiver==domain->command_context.owner && origin->physical_seat==domain->physical_seat &&
        origin->recipe==recipe && origin->recipe_content && !strcmp(origin->recipe_content,g->content) &&
        qa_executable_recipe_content_read(recipe,g->content,&files,&product) && product==g->product &&
        origin->catalog==qa_executable_recipe_catalog(recipe) && origin->product==product->id && origin->files==files;
}
static bool music_origin(q1_group *g,frontend_music_origin *out,qa_error *e)
{
    frontend_unified_q1 *o=g->parent;qa_executable_recipe *recipe=frontend_remote_unified_recipe(o->replica);
    qa_vfs *files=NULL;const qa_product *product=NULL;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    if(!domain || !domain->command_context.owner || domain->command_context.owner>UINT32_MAX ||
        !qa_executable_recipe_content_read(recipe,g->content,&files,&product) || product!=g->product)
        return fail(e,"Q1 music lost its actual received content declaration");
    *out=(frontend_music_origin){.kind=FRONTEND_MUSIC_REMOTE,.receiver=(qa_actor_owner)domain->command_context.owner,.physical_seat=domain->physical_seat,
        .recipe=recipe,.recipe_content=g->content,.catalog=qa_executable_recipe_catalog(recipe),.product=product->id,
        .files=files,.context=g,.current=music_current};
    return true;
}
static bool music_play(q1_group *g,double track,qa_error *e)
{
    if(!g->parent->frontend->audio)return true;
    frontend_music_origin origin;
    if(!music_origin(g,&origin,e) || (!g->music && !frontend_received_music_create(g->parent->frontend,&origin,&g->music,e)))return false;
    char cue[4];snprintf(cue,sizeof(cue),"%u",(unsigned)track);
    return frontend_received_music_play(g->music,cue,e);
}
static bool clone_row(const qa_unified_document *d,qa_json_id row,qa_unified_document **out,qa_error *e)
{ return qa_unified_document_create(QA_UNIFIED_CHECKPOINT,qa_json_source(qa_unified_document_json(d),row),out,e); }
static qa_scene_image_options model_options(void)
{ return (qa_scene_image_options){.family=QA_SCENE_Q1,.usage=QA_IMAGE_USAGE_SKIN,.wrap=QA_SCENE_REPEAT,.filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=255}; }
static q1_light *light(q1_group *g,qa_vec3 origin,double seconds,float radius,float decay,double duration)
{
    size_t i=0;while(i<Q1_LIGHTS && g->lights[i].until>seconds) ++i;if(i==Q1_LIGHTS) i=0;
    uint64_t identity=g->lights[i].identity;if(!identity) identity=qa_scene_identity();
    g->lights[i]=(q1_light){.origin=origin,.born=seconds,.until=seconds+duration,.radius=radius,.decay=decay,.identity=identity};
    return g->lights+i;
}
static bool effect_sound(frontend_unified_q1 *o,const q1_event *p,const char *path,qa_error *e)
{
    if(!o->events)return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 effect sound lost its actual CLIENT event/audio owner");
    return frontend_unified_events_sound_path(o->events,(char *)p->content.data,path,
        (qa_actor_id){0},p->origin,p->seconds*1000,0,1,1,0,e);
}
static bool pose_angles(frontend_unified_q1 *o,qa_saved_actor_id actor,qa_vec3 *angles,bool *found,qa_error *e)
{
    *found=false;
    const qa_unified_document *frame=frontend_remote_unified_frame(o->replica);
    if(!frame)return true;
    const qa_json_document *j=qa_unified_document_json(frame);
    qa_json_id models=field(j,qa_unified_document_root(frame),"models");
    for(size_t i=0;i<qa_json_size(j,models);++i){qa_json_id model=qa_json_at(j,models,i);
        qa_saved_actor_id candidate;bool present;
        if(!wire(frame,field(j,model,"actor"),&candidate,&present,false,e))return false;
        if(same_wire(actor,candidate)){*found=true;return vec(frame,field(j,model,"angles"),angles,e);}
    }
    return true;
}
static bool effect(frontend_unified_q1 *o,q1_group *g,const q1_event *p,qa_error *e)
{
    const char *k=(char *)p->name.data;qa_vec3 zero=qa_v3(0,0,0);double time=p->seconds;
    if(!strcmp(k,"explosion") || !strcmp(k,"tar-explosion")) {
        frontend_fx_q1_explosion(&g->particles,&o->random,p->origin,time,!strcmp(k,"tar-explosion"));
        if(!strcmp(k,"explosion"))light(g,p->origin,time,350,300,.5);
        return effect_sound(o,p,"weapons/r_exp3.wav",e);
    }
    else if(!strcmp(k,"teleport") || !strcmp(k,"lava-splash")) frontend_fx_q1_splash(&g->particles,&o->random,p->origin,time,!strcmp(k,"lava-splash"));
    else if(!strcmp(k,"muzzleflash")) {
        qa_vec3 axes[3],angles=p->angles,origin=p->muzzle?p->end:qa_vec_add(p->origin,qa_v3(0,0,16));
        bool direction=p->muzzle;
        if(!p->muzzle && p->actor_present && !pose_angles(o,p->actor,&angles,&direction,e))return false;
        if(direction){frontend_camera_axes(angles,axes);origin=qa_vec_add(origin,qa_vec_scale(axes[0],18));}
        light(g,origin,time,200+(float)(qa_builtin_random_integer(&o->random)&31),0,.1)->minimum=32;
    }
    else if(!strcmp(k,"pickup")) return true;
    else {
        int32_t count;uint32_t color=0;
        if(!strcmp(k,"blood") || !strcmp(k,"meat-spray")) {color=73;count=(int32_t)p->a;}
        else if(!strcmp(k,"gunshot")) count=20;
        else if(!strcmp(k,"spike")) count=10;
        else if(!strcmp(k,"superspike")) count=20;
        else if(!strcmp(k,"wizard-spike")) {count=30;color=20;}
        else if(!strcmp(k,"knight-spike")) {count=20;color=226;}
        else return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 effect recipe is not installed");
        frontend_fx_q1_impact(&g->particles,&o->random,p->origin,zero,color,count,time);
        if(!strcmp(k,"spike") || !strcmp(k,"superspike")) {
            const char *path="weapons/tink1.wav";
            if(qa_builtin_random_integer(&o->random)%5==0){uint32_t n=qa_builtin_random_integer(&o->random)&3;
                path=n==1?"weapons/ric1.wav":n==2?"weapons/ric2.wav":"weapons/ric3.wav";}
            return effect_sound(o,p,path,e);
        }
        if(!strcmp(k,"wizard-spike") || !strcmp(k,"knight-spike"))
            return effect_sound(o,p,!strcmp(k,"wizard-spike")?"wizard/hit.wav":"hknight/hit.wav",e);
    }
    return true;
}
static bool hud_read(void *context,const qa_hud_frame *frame,qa_hud_data *out,qa_error *e)
{
    frontend_unified_q1 *o=context;const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if(!o->busy || !d || frame->seat!=d->physical_seat) return fail(e,"Q1 HUD changed its actual CLIENT seat");
    size_t count=0;for(size_t i=0;i<Q1_POWERS;++i) if(o->powers[i]>o->seconds) o->timers[count++]=(qa_hud_timer){.label=powers[i],.until_ns=ns(o->powers[i])};
    size_t bars=0;if(o->ctf_present){memcpy(o->display_bars,o->ctf,sizeof(o->ctf));bars=4;}
    if(o->monsters_present)o->display_bars[bars++]=(qa_hud_value){.label="Monsters",.value=o->monsters};
    if(o->secrets_present)o->display_bars[bars++]=(qa_hud_value){.label="Secrets",.value=o->secrets};
    *out=(qa_hud_data){.source_vitals=true,.bars=o->display_bars,.bar_count=bars,.timers=o->timers,.timer_count=count,.scores=o->scores,.score_count=o->score_count,
        .help_title=o->prompt_title,.help_lines=(const char *const *)o->prompt_lines,.help_count=o->prompt_count};return true;
}
bool frontend_unified_q1_create(qa_frontend *f,frontend_remote_unified *r,frontend_unified_media *m,const frontend_unified_q1_options *options,frontend_unified_q1 **out,qa_error *e)
{
    if(!f || !r || !m || !options || !options->audio_owner || options->audio_owner==QA_AUDIO_NO_OWNER || !options->audio_actor || !out || *out) return fail(e,"Q1 CLIENT child requires its actual retained replica/media/audio route");
    frontend_unified_q1 *o=calloc(1,sizeof(*o));if(!o) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Allocating Q1 CLIENT presentation");
    o->frontend=f;o->replica=r;o->media=m;o->options=*options;o->epoch=frontend_remote_unified_epoch(r);
    qa_builtin_random_seed(&o->random,1);
    o->localizations=qa_localization_pool_create(e);const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(r);
    bool ok=o->localizations && d && (f->source_restoring?checkpoint_current(o,e):frontend_unified_q1_current(o)) && qa_hud_create(&(qa_hud_options){.ui=f->seats[d->physical_seat].ui,
        .application=d->application,.seat=d->physical_seat,.context=o,.read=hud_read},&o->hud,e);
    if(!ok) {qa_localization_pool_destroy(o->localizations);free(o);return false;}
    o->ctf[0].label="Red";o->ctf[1].label="Blue";o->ctf[2].label="Flags";o->ctf[3].label="Runes";*out=o;return true;
}
bool frontend_unified_q1_events(frontend_unified_q1 *o,frontend_unified_events *events,qa_error *e)
{
    if(!o || !events || !frontend_unified_q1_idle(o) || !frontend_unified_q1_current(o) ||
        frontend_unified_events_audio_owner(events)!=o->options.audio_owner || (o->events && o->events!=events))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 CLIENT event route changed its actual private audio owner");
    o->events=events;return true;
}
bool frontend_unified_q1_validate(frontend_unified_q1 *o,bool simulation,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    if(!frontend_unified_q1_current(o) || !d) return fail(e,"Q1 validation lost its real CLIENT recipe");
    if(simulation) return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 child does not acknowledge generic simulation events");
    q1_event p={0};bool ok=parse(o,d,row,&p,e);
    if(ok && p.kind==Q1_EFFECT) {static const char *const known[]={"blood","gunshot","spike","superspike","explosion","teleport","muzzleflash","pickup","lava-splash","tar-explosion","meat-spray","wizard-spike","knight-spike"};bool found=false;
        for(size_t i=0;i<sizeof(known)/sizeof(*known);++i) found|=!strcmp((char *)p.name.data,known[i]);
        ok=found || fail(e,"Q1 effect kind has no recipe");}
    if(ok && p.kind==Q1_BEAM) ok=(!strcmp((char *)p.name.data,"lightning1") || !strcmp((char *)p.name.data,"lightning2") || !strcmp((char *)p.name.data,"lightning3") || !strcmp((char *)p.name.data,"grapple")) || fail(e,"Q1 beam has no actual model recipe");
    if(ok && p.kind==Q1_POWER) {bool found=false;for(size_t i=0;i<Q1_POWERS;++i) found|=!strcmp((char *)p.name.data,powers[i]);ok=found || fail(e,"Q1 power timer has no declared identity");}
    if(ok && p.kind==Q1_ACTION)ok=(o->options.action && o->options.action_validate)?
        o->options.action_validate(o->options.context,d,row,e):
        frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 session/view/music action has no actual CLIENT callback");
    if(ok && (p.kind==Q1_ACHIEVEMENT || p.kind==Q1_COMPLETED)){
        bool local;qa_player_progress *store;const char *value;uint32_t seat;
        ok=progress_target(o,&p,row,&local,&store,&value,&seat,e);
    }
    event_free(&p);return ok;
}
static void prompt_clear(frontend_unified_q1 *o)
{free(o->prompt_title);o->prompt_title=NULL;for(size_t i=0;i<o->prompt_count;++i) free(o->prompt_lines[i]);free(o->prompt_lines);o->prompt_lines=NULL;o->prompt_count=0;qa_unified_document_destroy(o->prompt);o->prompt=NULL;o->prompt_activation=NULL;}
static void group_free(q1_group *g)
{
    while(g->ambient){q1_ambient *a=g->ambient;g->ambient=a->next;
        if(a->mixer)qa_audio_mixer_remove_static(a->mixer,a->identity);
        qa_audio_asset_release(a->asset);free(a->path);free(a);}
    while(g->statics){q1_static *s=g->statics;g->statics=s->next;free(s->path);free(s);}
    for(size_t i=0;i<256;++i){free(g->styles[i]);free(g->clients[i].name);free(g->clients[i].social);free(g->clients[i].info);}
    for(size_t i=0;i<6;++i)qa_scene_image_release(g->sky[i]);
    free(g->sky_name);qa_scene_image_release(g->particle_image);qa_localization_release(g->localization);free(g->content);free(g);
}
static bool retirement_parse(const qa_unified_document *d,qa_json_id row,q1_event *p,bool *retired,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);qa_json_id event=field(j,row,"event"),kind=field(j,event,"kind");
    if(!qa_json_string_equal(j,field(j,row,"kind"),"presentation-owner") ||
        (!qa_json_string_equal(j,kind,"retired") && !qa_json_string_equal(j,kind,"refreshed")))
        return fail(e,"Q1 Source retirement lacks an actual presentation-owner record");
    *retired=qa_json_string_equal(j,kind,"retired");
    return owner_parse(d,field(j,event,"owner"),p,e) && p->provider.size>0 &&
        number(d,field(j,row,"seconds"),&p->seconds,e) && qa_json_u64(j,field(j,row,"sequence"),&p->sequence,e);
}
bool frontend_unified_q1_owner_validate(frontend_unified_q1 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    if(!frontend_unified_q1_current(o) || !d)return fail(e,"Q1 Source retirement lost its retained replica");
    q1_event p={0};bool retired;bool ok=retirement_parse(d,row,&p,&retired,e);event_free(&p);return ok;
}
bool frontend_unified_q1_owner_retire(frontend_unified_q1 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    if(!o || !frontend_unified_q1_idle(o) || !mutable(o,e))return false;
    q1_event p={0};bool retired;q1_activation *owner=NULL;
    bool ok=retirement_parse(d,row,&p,&retired,e) && activation(o,&p,&owner,e);
    event_free(&p);if(!ok)return false;
    if(!retired || owner->retired)return true;
    for(q1_group *g=o->groups;g;g=g->next)if(g->activation==owner)
        for(q1_ambient *a=g->ambient;a;a=a->next)if(a->mixer && !qa_audio_mixer_callbacks_idle(a->mixer))
            return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 Source retirement overlaps an actual ambient mixer callback");
    if(o->pause_activation==owner){
        if(o->frontend->audio && !frontend_music_sources_received_pause(o->frontend->music_sources,false,e))return false;
        o->pause_activation=NULL;
    }
    for(q1_group *g=o->groups;g;g=g->next)if(g->activation==owner && !frontend_received_music_destroy(&g->music,e))return false;
    q1_group **next=&o->groups;
    while(*next){q1_group *g=*next;if(g->activation==owner){*next=g->next;group_free(g);}else next=&g->next;}
    if(o->weapon_activation==owner){qa_unified_document_destroy(o->weapon);o->weapon=NULL;o->weapon_activation=NULL;}
    if(o->prompt_activation==owner)prompt_clear(o);
    for(size_t i=0;i<Q1_POWERS;++i)if(o->power_activations[i]==owner){o->powers[i]=0;o->power_activations[i]=NULL;}
    if(o->ctf_activation==owner){o->ctf_present=false;o->capture_until=0;o->ctf_activation=NULL;}
    if(o->fog_activation==owner){o->fog.active=false;o->fog_activation=NULL;}
    if(o->finale_activation==owner){qa_unified_document_destroy(o->finale);o->finale=NULL;
        qa_scene_image_release(o->finale_image);o->finale_image=NULL;o->finale_activation=NULL;qa_hud_clear_center(o->hud,NULL);}
    owner->retired=true;return mutable(o,e);
}
static bool prompt_set(frontend_unified_q1 *o,const q1_event *p,qa_json_id row,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(p->document);qa_json_id choices=field(j,p->value,"choices");size_t count=qa_json_size(j,choices);
    char **lines=calloc(count?count:1,sizeof(*lines));qa_unified_document *doc=NULL;
    if(!lines) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q1 Source prompt");
    bool ok=clone_row(p->document,row,&doc,e);
    for(size_t i=0;ok && i<count;++i) {qa_json_id choice=qa_json_at(j,choices,i);qa_buffer label={0};double impulse;
        ok=string(p->document,field(j,choice,"label"),&label,e) && word(p->document,field(j,choice,"impulse"),&impulse,255,e);
        if(ok) {lines[i]=(char *)label.data;label=(qa_buffer){0};}qa_buffer_free(&label);}
    char *title=ok?malloc(p->text.size+1):NULL;if(ok && !title) ok=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q1 prompt title");
    if(!ok) {for(size_t i=0;i<count;++i) free(lines[i]);free(lines);qa_unified_document_destroy(doc);return false;}
    memcpy(title,p->text.data,p->text.size+1);prompt_clear(o);o->prompt=doc;o->prompt_title=title;o->prompt_lines=lines;o->prompt_count=count;return true;
}
static bool localize_piece(q1_group *g,const qa_unified_document *doc,qa_json_id text_id,qa_json_id args,qa_buffer *out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(doc);qa_buffer base={0};if(!string(doc,text_id,&base,e))return false;
    size_t count=qa_json_size(j,args);if(count>8){qa_buffer_free(&base);return fail(e,"Q1 message exceeds eight Source arguments");}
    qa_buffer values[8]={{0}};const char *argv[8]={0};char numeric[8][32];bool ok=true;
    for(size_t i=0;ok && i<count;++i){qa_json_id a=qa_json_at(j,args,i);if(qa_json_type(j,a)==QA_JSON_STRING){ok=string(doc,a,&values[i],e);argv[i]=(char *)values[i].data;}
        else {double n;ok=number(doc,a,&n,e) && qa_format_ecmascript_number(n,numeric[i],e);argv[i]=numeric[i];}}
    if(ok){out->data=malloc(65536);if(!out->data)ok=frontend_unified_fail(e,QA_ERROR_MEMORY,"Resolving received Q1 message");
        else if(g->product->edition!=QA_EDITION_RERELEASE && (base.data[0]!='$' || !qa_localization_find(g->localization,(char *)base.data+1))){
            ok=qa_q1_classic_text((char *)base.data,argv,count,(char *)out->data,65536,e);if(ok)out->size=strlen((char *)out->data);}
        else out->size=qa_localize_presentation(g->localization,(char *)base.data,argv,count,true,(char *)out->data,65536);}
    for(size_t i=0;i<count;++i)qa_buffer_free(values+i);
    qa_buffer_free(&base);return ok;
}
static bool localized(frontend_unified_q1 *o,q1_group *g,const q1_event *p,qa_buffer *out,qa_error *e)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);const char *language;
    if(!qa_ui_language_read(d->cvars,d->physical_seat,&language,e)) return false;
    if(!g->localization || strcmp(g->language,language)) {qa_vfs *files;const qa_product *product;qa_localization *catalog=NULL;
        if(strlen(language)>=sizeof(g->language) || !qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),g->content,&files,&product,e) ||
            !qa_localization_acquire(o->localizations,files,language,&(qa_localization_options){.profile=QA_LOCALIZATION_Q1_RERELEASE},&catalog,e)) return false;
        qa_localization_release(g->localization);g->localization=catalog;strcpy(g->language,language);}
    const qa_json_document *j=qa_unified_document_json(p->document);qa_json_id args=field(j,p->value,"args"),parts=field(j,p->value,"parts");
    if(parts==QA_JSON_NONE || qa_json_size(j,parts)==0)return localize_piece(g,p->document,field(j,p->value,"text"),args,out,e);
    if(qa_json_type(j,parts)!=QA_JSON_ARRAY || qa_json_size(j,parts)>256)return fail(e,"Q1 multipart message has no bounded actual parts");
    bool ok=true;for(size_t i=0;ok && i<qa_json_size(j,parts);++i){qa_json_id part=qa_json_at(j,parts,i);qa_buffer value={0};
        ok=localize_piece(g,p->document,field(j,part,"text"),field(j,part,"args"),&value,e);
        if(ok && value.size>UINT32_C(1048576)-out->size)ok=fail(e,"Q1 multipart message exceeds its retained text extent");
        if(ok){uint8_t *bytes=realloc(out->data,out->size+value.size+1);if(!bytes)ok=frontend_unified_fail(e,QA_ERROR_MEMORY,"Joining Q1 Source message parts");
            else {out->data=bytes;memcpy(bytes+out->size,value.data,value.size);out->size+=value.size;bytes[out->size]=0;}}qa_buffer_free(&value);}
    return ok;
}
bool frontend_unified_q1_presentation(frontend_unified_q1 *o,const qa_unified_document *d,qa_json_id row,bool *mirrored,qa_error *e)
{
    if(!o || !mirrored || o->busy || o->prepared || !mutable(o,e)) return false;
    *mirrored=false;
    q1_event p={0};q1_group *g=NULL;q1_activation *owner=NULL;
    bool ok=parse(o,d,row,&p,e) && activation(o,&p,&owner,e);if(!ok) {event_free(&p);return false;}
    if(owner && owner->retired){event_free(&p);return true;}
    if(p.kind==Q1_COMPLETED){ok=progress_record(o,&p,row,e);event_free(&p);return ok && mutable(o,e);}
    bool persistent=p.kind==Q1_BEAM || p.kind==Q1_STYLE || p.kind==Q1_STATIC || p.kind==Q1_AMBIENT || p.kind==Q1_CLIENT || p.kind==Q1_SKY ||
        p.kind==Q1_MUSIC || p.kind==Q1_PAUSE || p.kind==Q1_FINALE;
    if(!group(o,(char *)p.content.data,persistent?owner:NULL,&g,e)){event_free(&p);return false;}
    bool local=true;
    if(p.kind==Q1_WEAPON || p.kind==Q1_POWER || p.kind==Q1_MESSAGE || p.kind==Q1_CTF_STATUS || p.kind==Q1_PROMPT || p.kind==Q1_CLEAR_PROMPT || p.kind==Q1_LOG || p.kind==Q1_ACHIEVEMENT || p.kind==Q1_FOG || p.kind==Q1_FOUND) {
        local=owns(o,&p,e);if(!local && e && e->code!=QA_OK) {event_free(&p);return false;}}
    if(!local) {event_free(&p);return true;}
    switch(p.kind) {
    case Q1_PARTICLES:frontend_fx_q1_impact(&g->particles,&o->random,p.origin,p.end,(uint32_t)p.a,(int32_t)p.b,p.seconds);break;
    case Q1_EFFECT:case Q1_COLORS: {
        size_t original_count=g->particles.count;qa_builtin_random original_random=o->random;q1_light original_lights[Q1_LIGHTS];
        memcpy(original_lights,g->lights,sizeof(original_lights));
        if(p.kind==Q1_EFFECT)ok=effect(o,g,&p,e);
        else {frontend_fx_q1_color_explosion(&g->particles,&o->random,p.origin,p.seconds,(uint32_t)p.a,(uint32_t)p.b);
            light(g,p.origin,p.seconds,350,300,.5);ok=effect_sound(o,&p,"weapons/r_exp3.wav",e);}
        if(!ok){g->particles.count=original_count;o->random=original_random;memcpy(g->lights,original_lights,sizeof(original_lights));}
        if(ok && p.kind==Q1_EFFECT && !strcmp((char *)p.name.data,"pickup") && owns(o,&p,e))o->bonus_until=p.seconds+.5;
        break;}
    case Q1_BEAM: {size_t i=0;for(;i<Q1_BEAMS;++i) if(same_wire(g->beams[i].actor,p.actor) || g->beams[i].until<p.seconds) break;
        if(i==Q1_BEAMS) i=0;
        uint8_t kind=!strcmp((char *)p.name.data,"lightning1")?0:!strcmp((char *)p.name.data,"lightning2")?1:!strcmp((char *)p.name.data,"lightning3")?2:3;
        g->beams[i]=(q1_beam){.actor=p.actor,.start=p.origin,.end=p.end,.until=p.seconds+.2,.kind=kind,.roll_seed=qa_builtin_random_integer(&o->random)};break;}
    case Q1_STYLE:free(g->styles[(uint32_t)p.a]);g->styles[(uint32_t)p.a]=(char *)p.text.data;g->style_sequences[(uint32_t)p.a]=p.sequence;p.text=(qa_buffer){0};break;
    case Q1_STATIC: {if(!p.text.size)break;
        q1_static *s=calloc(1,sizeof(*s));qa_scene_image_options images=model_options();
        ok=s && frontend_unified_media_model(o->media,g->content,(char *)p.text.data,QA_SCENE_Q1,&images,&s->model,e);
        if(ok) {s->path=(char *)p.text.data;p.text=(qa_buffer){0};s->origin=p.origin;s->angles=p.angles;s->frame=(uint32_t)p.a;s->skin=(uint32_t)p.b;
            q1_static **tail=&g->statics;while(*tail) tail=&(*tail)->next;*tail=s;}else free(s);break;}
    case Q1_WEAPON: {qa_unified_document *doc=NULL;ok=clone_row(d,row,&doc,e);if(ok) {qa_unified_document_destroy(o->weapon);o->weapon=doc;o->weapon_activation=owner;}break;}
    case Q1_POWER:for(size_t i=0;i<Q1_POWERS;++i) if(!strcmp((char *)p.name.data,powers[i])) {o->powers[i]=p.a;o->power_activations[i]=owner;}break;
    case Q1_MESSAGE: {qa_buffer message={0};ok=localized(o,g,&p,&message,e);
        if(ok) ok=p.flag?qa_hud_center_print(o->hud,(char *)message.data,ns(p.seconds),UINT64_C(3000000000),true,0,e):qa_hud_notify(o->hud,(char *)message.data,false,ns(p.seconds),UINT64_C(3000000000),e);
        if(ok && !p.flag) {char *copy=realloc(message.data,message.size+2);if(!copy) ok=frontend_unified_fail(e,QA_ERROR_MEMORY,"Emitting Q1 Source print");else {message.data=(uint8_t *)copy;copy[message.size]='\n';copy[message.size+1]=0;
            const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);qa_console_emit(domain->console,&domain->command_context,copy);}}
        if(ok) *mirrored=true;
        qa_buffer_free(&message);break;}
    case Q1_STOP: {qa_actor_id actor;ok=frontend_remote_unified_actor(o->replica,p.actor.slot,p.actor.generation,&actor,e);
        if(ok && o->frontend->audio) {uint64_t audio;ok=o->options.audio_actor(o->options.context,actor,&audio,e);
            if(ok) qa_audio_engine_stop_channel(o->frontend->audio,audio,o->options.audio_owner,QA_AUDIO_Q1,(int32_t)p.a); }break;}
    case Q1_SOUND:ok=frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 sound must be paired with the real declared simulation sound route");break;
    case Q1_AMBIENT: {q1_ambient *a=calloc(1,sizeof(*a));ok=a && qa_audio_bank_register(g->sounds,(char *)p.text.data,QA_AUDIO_Q1,&a->asset,e);
        if(ok && a->asset && qa_audio_asset_sample(a->asset)->loop_start!=QA_AUDIO_NO_LOOP) {a->path=(char *)p.text.data;p.text=(qa_buffer){0};a->origin=p.origin;a->volume=(float)p.a;a->attenuation=(float)p.b;a->identity=qa_scene_identity();
            q1_ambient **tail=&g->ambient;while(*tail) tail=&(*tail)->next;*tail=a;}
        else {if(a) qa_audio_asset_release(a->asset);free(a);}break;}
    case Q1_CTF_STATUS:o->ctf[0].value=p.a;o->ctf[1].value=p.b;o->ctf[2].value=p.c;o->ctf[3].value=p.d;o->ctf_present=true;o->ctf_activation=owner;break;
    case Q1_CTF_CAPTURE:o->ctf[!strcmp((char *)p.name.data,"blue")?1:0].value=p.a;o->ctf_present=true;o->capture_until=p.seconds+3;o->ctf_activation=owner;break;
    case Q1_PROMPT:ok=prompt_set(o,&p,row,e);if(ok)o->prompt_activation=owner;break;
    case Q1_CLEAR_PROMPT:prompt_clear(o);break;
    case Q1_LOG:ok=qa_hud_notify(o->hud,(char *)p.text.data,false,ns(p.seconds),UINT64_C(3000000000),e);break;
    case Q1_ACHIEVEMENT:ok=progress_record(o,&p,row,e);
        if(ok && p.text.size)ok=qa_hud_notify(o->hud,(char *)p.text.data,false,ns(p.seconds),UINT64_C(3000000000),e);
        break;
    case Q1_COMPLETED:break;
    case Q1_TOTAL:o->total_monsters=p.a;o->monsters_present=true;break;
    case Q1_FOUND: {const qa_json_document *j=qa_unified_document_json(d);
        if(qa_json_string_equal(j,field(j,p.value,"kind"),"secret")){o->secrets=p.b;o->total_secrets=p.a;o->secrets_present=true;}
        else {o->monsters=p.b;o->total_monsters=p.a;o->monsters_present=true;}break;}
    case Q1_FOG:o->fog.previous=p.origin;o->fog.target=p.end;o->fog.previous_density=p.a;o->fog.target_density=p.b;
        o->fog.start=p.c;o->fog.duration=p.d;o->fog.sky_factor=p.sky_factor;o->fog.active=true;o->fog_activation=owner;break;
    case Q1_FINALE: {
        if(p.a>4)break;
        const qa_json_document *json=qa_unified_document_json(d);
        if(qa_json_string_equal(json,field(json,row,"kind"),"q1-level") && !music_play(g,p.b,e)){ok=false;break;}
        qa_unified_document *doc=NULL;qa_scene_image *image=NULL;qa_buffer text={0};
        qa_scene_image_options options={.family=QA_SCENE_Q1,.usage=QA_IMAGE_USAGE_PICTURE,.wrap=QA_SCENE_CLAMP,.filter=QA_SCENE_NEAREST,.transparent=true,.transparent_index=255};
        ok=clone_row(d,row,&doc,e) && localized(o,g,&p,&text,e) && qa_scene_image_load_exact(g->images,"gfx/finale.lmp",&options,&image,e);
        if(ok && !image)ok=fail(e,"Q1 finale picture is absent from its actual Source content");
        if(ok)ok=qa_hud_clear_center(o->hud,e) && qa_hud_center_print(o->hud,(char *)text.data,ns(p.seconds),UINT64_MAX,false,UINT64_C(125000000),e);
        if(ok){qa_unified_document_destroy(o->finale);o->finale=doc;doc=NULL;qa_scene_image_release(o->finale_image);o->finale_image=image;image=NULL;
            o->finale_banner=p.a>=4;o->finale_activation=owner;}
        qa_scene_image_release(image);qa_unified_document_destroy(doc);qa_buffer_free(&text);break;
    }
    case Q1_ACTION:ok=o->options.action?o->options.action(o->options.context,d,row,e):frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 action has no actual CLIENT owner");break;
    case Q1_MUSIC:ok=music_play(g,p.a,e);break;
    case Q1_PAUSE:if(o->frontend->audio){
        ok=frontend_music_sources_received_pause(o->frontend->music_sources,p.flag,e);
        if(ok)o->pause_activation=p.flag?owner:NULL;
        }break;
    case Q1_CLIENT: {uint32_t i=(uint32_t)p.a;g->clients[i].present=true;const char *kind=(char *)p.name.data;
        size_t column=!strcmp(kind,"name")?0:!strcmp(kind,"social")?1:!strcmp(kind,"player-info")?2:!strcmp(kind,"colors")?3:!strcmp(kind,"frags")?4:5;
        g->clients[i].sequences[column]=p.sequence;
        g->clients[i].fields[column]=true;
        if(!strcmp(kind,"name") || !strcmp(kind,"social") || !strcmp(kind,"player-info")) {char **target=!strcmp(kind,"name")?&g->clients[i].name:!strcmp(kind,"social")?&g->clients[i].social:&g->clients[i].info;
            free(*target);*target=(char *)p.text.data;p.text=(qa_buffer){0};}
        else if(!strcmp(kind,"colors"))g->clients[i].colors=p.b;else if(!strcmp(kind,"frags"))g->clients[i].frags=p.b;
        else {g->clients[i].ping=p.b;g->clients[i].has_ping=true;}break;}
    case Q1_SKY: {qa_scene_image *images[6]={0};bool found=false;static const char *const suffixes[]={"rt","bk","lf","ft","up","dn"};qa_scene_image_options options={.family=QA_SCENE_Q1,.usage=QA_IMAGE_USAGE_SKY,.wrap=QA_SCENE_CLAMP,.filter=QA_SCENE_LINEAR};
        for(size_t i=0;ok && p.text.size && i<6;++i){size_t n=p.text.size+32;char *path=malloc(n);if(!path){ok=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q1 received sky path");break;}
            snprintf(path,n,"gfx/env/%s%s.tga",(char *)p.text.data,suffixes[i]);ok=qa_scene_image_load_exact(g->images,path,&options,images+i,e);
            if(ok && !images[i]){snprintf(path,n,"gfx/env/%s%s.png",(char *)p.text.data,suffixes[i]);ok=qa_scene_image_load_exact(g->images,path,&options,images+i,e);}free(path);
            if(ok){found|=images[i]!=NULL;if(!images[i]){images[i]=(qa_scene_image *)qa_scene_missing(g->images);qa_scene_image_retain(images[i]);}}}
        if(ok){for(size_t i=0;i<6;++i){qa_scene_image_release(g->sky[i]);g->sky[i]=images[i];images[i]=NULL;}free(g->sky_name);g->sky_name=(char *)p.text.data;p.text=(qa_buffer){0};g->sky_found=found;g->sky_sequence=p.sequence;}
        for(size_t i=0;i<6;++i)qa_scene_image_release(images[i]);
        break;}
    }
    event_free(&p);return ok && mutable(o,e);
}
bool frontend_unified_q1_sound_presentation(frontend_unified_q1 *o,const qa_unified_document *d,qa_json_id row,bool simulation_owned,qa_error *e)
{
    if(!o || o->busy || o->prepared || !mutable(o,e)) return false;
    q1_event p={0};bool ok=parse(o,d,row,&p,e);q1_group *g=NULL;
    if(ok && p.kind!=Q1_SOUND) ok=fail(e,"Q1 sound pairing received a different Source event");
    if(ok && !simulation_owned) {
        ok=group(o,(char *)p.content.data,NULL,&g,e);qa_audio_asset *asset=NULL;qa_actor_id actor;uint64_t audio;
        if(ok) ok=frontend_remote_unified_actor(o->replica,p.actor.slot,p.actor.generation,&actor,e) &&
            o->options.audio_actor(o->options.context,actor,&audio,e) && qa_audio_bank_register(g->sounds,(char *)p.text.data,QA_AUDIO_Q1,&asset,e);
        const qa_json_document *j=qa_unified_document_json(d);qa_json_id channel=field(j,p.value,"channel");int32_t c=0;
        static const char *const channels[]={"auto","weapon","voice","item","body"};bool known=false;
        for(size_t i=0;i<5;++i) if(qa_json_string_equal(j,channel,channels[i])) {c=(int32_t)i;known=true;break;}
        if(ok && !known) {double n;ok=word(d,channel,&n,INT32_MAX,e);if(ok)c=(int32_t)n;}
        if(ok && asset && o->frontend->audio) {
            const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
            qa_audio_play play={.sample=qa_audio_asset_sample(asset),.asset=asset,.resource_id=qa_resource_id(qa_audio_asset_resource(asset)),
                .name=(char *)p.text.data,.family=QA_AUDIO_Q1,.actor=audio,.owner=o->options.audio_owner,.audience=domain->physical_seat,
                .origin_kind=p.flag?QA_AUDIO_FIXED:QA_AUDIO_ACTOR,.origin=p.origin,.channel=c,.volume=(float)p.a,.attenuation=(float)p.b,
                .server_milliseconds=p.seconds*1000,.has_server_time=true};
            double tick=fmod(trunc(p.seconds*1000),4294967296.0);if(tick<0)tick+=4294967296.0;uint32_t raw=(uint32_t)tick;int32_t signed_tick;memcpy(&signed_tick,&raw,sizeof(raw));
            ok=qa_audio_engine_play(o->frontend->audio,&play,signed_tick,e);
        }
        qa_audio_asset_release(asset);
    }
    event_free(&p);return ok && mutable(o,e);
}
bool frontend_unified_q1_frame_prepare(frontend_unified_q1 *o,const qa_unified_document *d,qa_error *e)
{
    if(!o || o->busy || o->prepared || !d || qa_unified_document_type(d)!=QA_UNIFIED_FRAME_DOCUMENT || !mutable(o,e)) return false;
    const qa_json_document *j=qa_unified_document_json(d);qa_json_id root=qa_unified_document_root(d),frame=field(j,field(j,field(j,root,"output"),"snapshot"),"frame"),time=field(j,frame,"time");
    uint64_t epoch,n;double seconds;
    if(!qa_json_u64(j,field(j,root,"epoch"),&epoch,e) || epoch!=o->epoch || !qa_json_u64(j,field(j,frame,"frame"),&n,e) || !number(d,field(j,time,"value"),&seconds,e)) return false;
    if(qa_json_string_equal(j,field(j,time,"kind"),"milliseconds"))seconds/=1000;
    else if(!qa_json_string_equal(j,field(j,time,"kind"),"seconds"))return fail(e,"Q1 received frame has no Source time domain");
    if(o->has_frame && n<=o->frame) return fail(e,"Q1 frame publication does not advance its actual receipt");
    o->prepared_frame=n;o->prepared_seconds=seconds;o->preparing_document=d;o->prepared=true;return true;
}
bool frontend_unified_q1_frame_ready(frontend_unified_q1 *o,const qa_unified_document *d,qa_error *e)
{
    if(!o || !o->prepared || o->preparing_document!=d || o->busy || o->music_retiring || !frontend_unified_q1_current(o) || !qa_hud_idle(o->hud))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 frame has no returned actual prepared CLIENT receipt");
    for(q1_group *g=o->groups;g;g=g->next)
        if(!frontend_received_music_idle(g->music))return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 frame overlaps its actual received music callback");
    for(q1_group *g=o->groups;g;g=g->next)for(q1_ambient *a=g->ambient;a;a=a->next)
        if(a->mixer && !qa_audio_mixer_callbacks_idle(a->mixer))return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 frame overlaps its actual ambient mixer callback");
    return true;
}
void frontend_unified_q1_frame_commit(frontend_unified_q1 *o)
{
    if(!o || !o->prepared || o->busy) return;
    for(q1_group *g=o->groups;g;g=g->next) {
        double elapsed=g->has_sample?fmax(0,o->prepared_seconds-g->sampled):0;size_t kept=0;
        for(size_t i=0;i<g->particles.count;++i) {qa_scene_q1_particle_state p=g->particles.values.q1[i];if(p.die<o->prepared_seconds)continue;
            qa_scene_q1_particle_advance(&p,elapsed,800);g->particles.values.q1[kept++]=p;}
        g->particles.count=kept;g->sampled=o->prepared_seconds;g->has_sample=true;
    }
    o->frame=o->prepared_frame;o->seconds=o->prepared_seconds;o->has_frame=true;o->prepared=false;o->preparing_document=NULL;qa_unified_document_destroy(o->restored_preparing);o->restored_preparing=NULL;
}
void frontend_unified_q1_frame_abort(frontend_unified_q1 *o)
{if(o && !o->busy){o->prepared=false;o->preparing_document=NULL;qa_unified_document_destroy(o->restored_preparing);o->restored_preparing=NULL;}}
bool frontend_unified_q1_world_input(frontend_unified_q1 *o,qa_scene_world_input *input,qa_error *e)
{
    if(!o || !input || o->busy || o->prepared || !o->has_frame || !mutable(o,e))return false;
    size_t capacity=input->light_count,groups=frontend_unified_q1_group_count(o);
    if(groups>(SIZE_MAX-capacity)/Q1_LIGHTS)return fail(e,"Q1 private scene light roster exceeds storage");
    capacity+=groups*Q1_LIGHTS;
    if(capacity>o->scene_capacity){qa_scene_light *lights=realloc(o->scene_lights,capacity*sizeof(*lights));if(!lights)return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q1 reached scene lights");o->scene_lights=lights;o->scene_capacity=capacity;}
    size_t count=input->light_count;if(count)memcpy(o->scene_lights,input->lights,count*sizeof(*input->lights));
    uint64_t sequences[256]={0};bool received[256]={0};q1_group *sky=NULL;
    for(size_t i=0;i<256;++i)o->scene_styles[i]=input->q1_styles && i<input->style_count?input->q1_styles[i]:256;
    for(q1_group *g=o->groups;g;g=g->next){
        for(size_t i=0;i<Q1_LIGHTS;++i){q1_light *l=g->lights+i;if(l->until<=input->seconds)continue;float radius=fmaxf(0,l->radius-(float)(input->seconds-l->born)*l->decay);
            if(radius>0)o->scene_lights[count++]=(qa_scene_light){.family=QA_SCENE_Q1,.origin=l->origin,.color={1,1,1},.radius=radius,.minimum=l->minimum,.scale=1,.additive=true,.identity=l->identity};}
        for(size_t i=0;i<256;++i)if(g->styles[i] && (!received[i] || g->style_sequences[i]>=sequences[i])){
            const char *pattern=g->styles[i];size_t length=strlen(pattern);float value=256;
            if(length){double sample=fmod(floor(input->seconds*10),(double)length);if(sample<0)sample+=(double)length;value=(float)((uint8_t)pattern[(size_t)sample]-'a')*22;}
            received[i]=true;sequences[i]=g->style_sequences[i];o->scene_styles[i]=value;}
        if(g->sky_name && (!sky || g->sky_sequence>=sky->sky_sequence))sky=g;
    }
    input->lights=o->scene_lights;input->light_count=count;input->q1_styles=o->scene_styles;input->style_count=256;
    if(o->fog.active){double fraction=o->fog.duration==0?1:fmin(1,fmax(0,(input->seconds-o->fog.start)/o->fog.duration));
        double density=o->fog.previous_density+(o->fog.target_density-o->fog.previous_density)*fraction;
        qa_vec3 color=qa_v3((float)(o->fog.previous.x+(o->fog.target.x-o->fog.previous.x)*fraction),
            (float)(o->fog.previous.y+(o->fog.target.y-o->fog.previous.y)*fraction),
            (float)(o->fog.previous.z+(o->fog.target.z-o->fog.previous.z)*fraction));
        color.x=roundf(fminf(1,fmaxf(0,color.x))*255)/255;color.y=roundf(fminf(1,fmaxf(0,color.y))*255)/255;color.z=roundf(fminf(1,fmaxf(0,color.z))*255)/255;
        input->fog=(qa_scene_fog){.kind=QA_FOG_EXP2,.effect=QA_FOG_COLOR,.density=(float)density,.color=color,.sky_factor=(float)o->fog.sky_factor,.far_depth=1};}
    if(sky){input->override_sky=sky->sky_found;for(size_t i=0;i<6;++i)input->sky_images[i]=sky->sky[i];input->sky_axis=qa_v3(0,0,1);input->sky_rotation=0;input->sky_auto_rotate=false;}
    return mutable(o,e);
}
bool frontend_unified_q1_audio_detach(frontend_unified_q1 *o,qa_error *e)
{
    if(!o)return true;
    if(!frontend_unified_q1_idle(o))return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 audio detach overlaps an entered CLIENT callback");
    for(q1_group *g=o->groups;g;g=g->next)for(q1_ambient *a=g->ambient;a;a=a->next)if(a->mixer && !qa_audio_mixer_callbacks_idle(a->mixer))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 audio detach overlaps its actual mixer callback");
    for(q1_group *g=o->groups;g;g=g->next)for(q1_ambient *a=g->ambient;a;a=a->next)if(a->mixer){qa_audio_mixer_remove_static(a->mixer,a->identity);a->mixer=NULL;}
    return true;
}
bool frontend_unified_q1_model(frontend_unified_q1 *o,qa_actor_id actor,const char *content,const char *path,qa_scene_model_input *input,qa_error *e)
{
    if(!o || !content || !path || !input || !mutable(o,e))return false;
    if(!o->weapon || !input->view_model || input->family!=QA_SCENE_Q1)return true;
    q1_event p={0};qa_json_id root=qa_unified_document_root(o->weapon);bool ok=parse(o,o->weapon,root,&p,e);qa_actor_id received;
    if(ok)ok=frontend_remote_unified_actor(o->replica,p.actor.slot,p.actor.generation,&received,e);
    if(ok && qa_actor_id_equal(actor,received) && !strcmp(content,(char *)p.content.data) && !strcmp(path,(char *)p.text.data))input->frame=(uint32_t)p.a;
    event_free(&p);return ok;
}
static void transform(qa_model_transform *out,qa_vec3 origin,qa_vec3 angles)
{
    qa_model_transform_identity(out);qa_vec3 axes[3];frontend_camera_axes(angles,axes);
    out->origin[0]=origin.x;out->origin[1]=origin.y;out->origin[2]=origin.z;
    for(unsigned i=0;i<3;++i){out->axes[i][0]=axes[i].x;out->axes[i][1]=axes[i].y;out->axes[i][2]=axes[i].z;}
}
static bool world_enter(frontend_unified_q1 *o,const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *e)
{
    if(!o || !world || frame!=&o->frontend->frame || o->busy || o->prepared ||
        !o->has_frame || !mutable(o,e))return false;
    if(world->view.mirror && !qa_scene_world_q1_mirror_scope(frontend_unified_media_world(o->media),world,frame))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 reflected output lost its genuine world mirror scope");
    o->busy=true; return true;
}
bool frontend_unified_q1_world_models(frontend_unified_q1 *o,const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *e)
{
    if(!world_enter(o,world,frame,e))return false;
    const qa_scene_view *view=&world->view;
    bool ok=true;
    for(q1_group *g=o->groups;ok && g;g=g->next) {
        for(q1_static *s=g->statics;ok && s;s=s->next) {
            qa_scene_model_input input={.view=*view,.family=QA_SCENE_Q1,.frame=s->frame,.old_frame=s->frame,.skin=s->skin,.color={1,1,1,1},
                .source_path=s->path,.material_library=frontend_unified_model_materials(s->model.scene),.seconds=world->seconds,.identity_light=world->identity_light,.ambient={1,1,1},
                .video_frame=world->video_frame,.video_context=world->video_context};
            transform(&input.transform,s->origin,s->angles);
            if(s->model.brush_world)ok=qa_scene_world_submit_model(s->model.brush_world,s->model.inline_model,&input.transform,world,0,input.color,frame,e);
            else {ok=qa_scene_world_sample_light_input(frontend_unified_media_world(o->media),world,s->origin,&input.ambient,&input.directed,&input.light_direction,e) &&
                frontend_legacy_model_input(frontend_unified_media_world(o->media),world,&input,e) && qa_scene_model_submit(s->model.scene,&input,frame,e);}
        }
        static const char *const models[]={"progs/bolt.mdl","progs/bolt2.mdl","progs/bolt3.mdl","progs/beam.mdl"};
        for(size_t i=0;ok && i<Q1_BEAMS;++i) {q1_beam *b=g->beams+i;if(b->until<=world->seconds)continue;qa_actor_id actual;
            ok=frontend_remote_unified_actor(o->replica,b->actor.slot,b->actor.generation,&actual,e);if(!ok)break;
            frontend_unified_model model;qa_scene_image_options images=model_options();ok=frontend_unified_media_model(o->media,g->content,models[b->kind],QA_SCENE_Q1,&images,&model,e);if(!ok)break;
            qa_vec3 direction=qa_vec_sub(b->end,b->start);float length=qa_vec_length(direction);if(length<=0)continue;direction=qa_vec_scale(direction,1/length);
            qa_vec3 angles=qa_v3(-atan2f(direction.z,hypotf(direction.x,direction.y))*57.29577951308232f,atan2f(direction.y,direction.x)*57.29577951308232f,0);
            qa_builtin_random roll;qa_builtin_random_seed(&roll,b->roll_seed);
            for(float step=0;ok && step<length;step+=30) {qa_scene_model_input input={.view=*view,.family=QA_SCENE_Q1,.source_path=models[b->kind],.material_library=frontend_unified_model_materials(model.scene),
                    .seconds=world->seconds,.identity_light=world->identity_light,.color={1,1,1,1},.ambient={1,1,1},.entity=actual.slot,
                    .video_frame=world->video_frame,.video_context=world->video_context};
                angles.z=(float)(qa_builtin_random_integer(&roll)%360);transform(&input.transform,qa_vec_add(b->start,qa_vec_scale(direction,step)),angles);
                ok=frontend_legacy_model_input(frontend_unified_media_world(o->media),world,&input,e) && qa_scene_model_submit(model.scene,&input,frame,e);}
        }
    }
    o->busy=false;return ok && mutable(o,e);
}
bool frontend_unified_q1_world_particles(frontend_unified_q1 *o,const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *e)
{
    if(!world_enter(o,world,frame,e))return false;
    const qa_scene_view *view=&world->view;
    bool ok=true;
    for(q1_group *g=o->groups;ok && g;g=g->next) {
        for(q1_ambient *a=view->mirror?NULL:g->ambient;ok && a;a=a->next) {
            const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
            qa_audio_mixer *m=o->frontend->audio?qa_audio_engine_seat_mixer(o->frontend->audio,domain->physical_seat):NULL;
            if(a->mixer && a->mixer!=m) {ok=frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 ambient must detach before its real mixer parent changes");break;}
            if(m && !a->mixer) {ok=qa_audio_mixer_static_asset(m,a->identity,a->asset,a->origin,truncf(a->volume*255),truncf(a->attenuation*64),e);
                qa_audio_static_view installed;if(ok && qa_audio_mixer_static_read(m,a->identity,&installed))a->mixer=m;}
        }
        if(ok && g->particles.count && !g->particle_image)ok=qa_scene_particle_image(g->images,QA_SCENE_Q1,&g->particle_image,e);
        qa_bytes palette={0};if(ok && g->particles.count)ok=qa_scene_resources_palette(g->images,QA_SCENE_Q1,&palette,e) && palette.size>=768;
        for(size_t i=g->particles.count;ok && i>0;--i) {const qa_scene_q1_particle_state *p=g->particles.values.q1+i-1;if(p->die<world->seconds)continue;
            uint32_t color=(p->color&255)*3;ok=qa_scene_indexed_particle(frame,view,QA_SCENE_Q1,p->origin,1,(qa_scene_vec4){palette.data[color]/255.0f,palette.data[color+1]/255.0f,palette.data[color+2]/255.0f,1},g->particle_image,e);}
    }
    o->busy=false;return ok && mutable(o,e);
}
bool frontend_unified_q1_world_dlights(frontend_unified_q1 *o,const qa_scene_world_input *world,
    qa_scene_frame *frame,qa_scene_vec4 *overlay,qa_error *e)
{
    if(!overlay || !world_enter(o,world,frame,e))return false;
    qa_executable_recipe *recipe=frontend_unified_media_recipe(o->media);
    const qa_recipe_choices *choices=qa_executable_recipe_choices(recipe);
    const qa_product *product=choices?qa_catalog_product(qa_executable_recipe_catalog(recipe),choices->world.presentation):NULL;
    bool ok=product!=NULL;
    if(!ok)frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 supplemental light lost its actual WORLD presentation");
    for(q1_group *g=product && product->family==QA_GAME_Q3?o->groups:NULL;ok && g;g=g->next) {
        qa_scene_light lights[Q1_LIGHTS];size_t count=0;
        for(size_t i=0;i<Q1_LIGHTS;++i){q1_light *l=g->lights+i;if(l->until<=world->seconds)continue;
            float radius=fmaxf(0,l->radius-(float)(world->seconds-l->born)*l->decay);
            if(radius>0)lights[count++]=(qa_scene_light){.family=QA_SCENE_Q1,.origin=l->origin,.color={1,1,1},
                .radius=radius,.minimum=l->minimum,.scale=1,.additive=true,.identity=l->identity};}
        frontend_legacy_render_policy policy;qa_scene_vec4 blend={0};
        if(count){const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
            ok=world->legacy_policy.present && world->legacy_policy.source_family==QA_SCENE_Q1?
                frontend_legacy_render_policy_read_registry(domain->cvars,g->product,&policy,e):
                frontend_legacy_render_policy_read(o->frontend,g->product,&policy,e);
            if(ok && policy.flashblend)ok=qa_scene_legacy_dlights(frame,&world->view,QA_SCENE_Q1,
                policy.quakeworld,lights,count,&blend,e);}
        if(ok && blend.w>0){float alpha=overlay->w+(1-overlay->w)*blend.w,weight=blend.w/alpha;
            *overlay=(qa_scene_vec4){overlay->x*(1-weight)+blend.x*weight,overlay->y*(1-weight)+blend.y*weight,
                overlay->z*(1-weight)+blend.z*weight,alpha};}
    }
    o->busy=false;return ok && mutable(o,e);
}
bool frontend_unified_q1_world_blend(frontend_unified_q1 *o,const qa_scene_world_input *world,qa_scene_vec4 overlay,qa_scene_frame *frame,qa_error *e)
{
    if(!world_enter(o,world,frame,e))return false;
    const qa_scene_view *view=&world->view;
    bool ok=true;
    if(ok && !view->mirror && o->groups && (!world->legacy_policy.present || world->legacy_policy.polyblend)){
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);qa_ui_preferences preferences;
        ok=qa_ui_preferences_read(qa_application_cvars(domain->application),domain->physical_seat,&preferences,e);
        if(ok){if(preferences.reduced_flashes)overlay=(qa_scene_vec4){0};
            float bonus=(float)fmin(50,fmax(0,(o->bonus_until-world->seconds)*100))/255;
            if(bonus>0){float alpha=overlay.w+(1-overlay.w)*bonus,weight=bonus/alpha;
                overlay=(qa_scene_vec4){overlay.x*(1-weight)+(215.0f/255)*weight,overlay.y*(1-weight)+(186.0f/255)*weight,
                    overlay.z*(1-weight)+(69.0f/255)*weight,alpha};}
            if(overlay.w>0)ok=qa_scene_frame_picture(frame,qa_scene_white(o->groups->images),view->viewport,view->viewport,
                (qa_scene_vec4){0,0,1,1},overlay,e);}
    }
    o->busy=false;return ok && mutable(o,e);
}
bool frontend_unified_q1_hud(frontend_unified_q1 *o,qa_ui *ui,qa_scene_rect viewport,qa_scene_frame *frame,qa_error *e)
{
    const frontend_remote_unified_domain *d=o?frontend_remote_unified_domain_read(o->replica):NULL;qa_actor_id player;uint32_t source;
    if(!o || !d || ui!=o->frontend->seats[d->physical_seat].ui || !frame || o->busy || o->prepared || !mutable(o,e) ||
        !frontend_remote_unified_player(o->replica,&player,&source))return false;
    size_t count=0;for(q1_group *g=o->groups;g;g=g->next)for(size_t i=0;i<256;++i)if(g->clients[i].present)++count;
    if(count>o->score_capacity){qa_hud_score *scores=realloc(o->scores,count*sizeof(*scores));if(!scores)return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining received Q1 scoreboard scratch");o->scores=scores;o->score_capacity=count;}
    o->score_count=0;
    for(q1_group *g=o->groups;g;g=g->next){bool earlier=false;
        for(q1_group *old=o->groups;old!=g;old=old->next)if(!strcmp(old->content,g->content)){earlier=true;break;}
        if(earlier)continue;
        for(size_t i=0;i<256;++i){bool present=false,has_name=false,has_score=false,has_ping=false;
            const char *name="";double score=0,ping=0;uint64_t name_sequence=0,score_sequence=0,ping_sequence=0;
            for(q1_group *received=g;received;received=received->next)if(!strcmp(received->content,g->content) && received->clients[i].present){
                present=true;
                if(received->clients[i].name && (!has_name || received->clients[i].sequences[0]>=name_sequence)){
                    name=received->clients[i].name;name_sequence=received->clients[i].sequences[0];has_name=true;}
                if(received->clients[i].fields[4] && (!has_score || received->clients[i].sequences[4]>=score_sequence)){score=received->clients[i].frags;score_sequence=received->clients[i].sequences[4];has_score=true;}
                if(received->clients[i].has_ping && (!has_ping || received->clients[i].sequences[5]>=ping_sequence)){
                    ping=received->clients[i].ping;ping_sequence=received->clients[i].sequences[5];has_ping=true;}
            }
            if(!present)continue;
            if(score<INT32_MIN || score>INT32_MAX || ping<INT32_MIN || ping>INT32_MAX)return fail(e,"Received Q1 scoreboard exceeds the HUD word domain");
            o->scores[o->score_count++]=(qa_hud_score){.name=name,.score=(int32_t)score,.ping=has_ping?(int32_t)ping:0,.local=i+1==source};
        }
    }
    o->busy=true;bool ok=true;
    if(o->finale && o->finale_banner && o->finale_image){float scale=fminf((float)viewport.width/320,(float)viewport.height/200);
        uint32_t width=(uint32_t)((float)o->finale_image->logical_width*scale),height=(uint32_t)((float)o->finale_image->logical_height*scale);
        qa_scene_rect rectangle={.x=viewport.x+(int32_t)(((int64_t)viewport.width-width)/2),.y=viewport.y+(int32_t)(16*scale),.width=width,.height=height};
        ok=qa_scene_frame_picture(frame,o->finale_image,rectangle,viewport,(qa_scene_vec4){0,0,1,1},(qa_scene_vec4){1,1,1,1},e);}
    if(ok)ok=qa_hud_draw(o->hud,&(qa_hud_frame){.seat=d->physical_seat,.actor=player,.time_ns=ns(o->seconds),
        .viewport=viewport,.safe_area=viewport,.scale=1,.visible=true,.show_scores=o->frontend->seats[d->physical_seat].scores},frame,e);
    o->busy=false;return ok && mutable(o,e);
}
bool frontend_unified_q1_checkpoint_ready(const frontend_unified_q1 *o)
{
    if(!o)return true;
    if(o->busy || o->music_retiring || !qa_hud_idle(o->hud))return false;
    for(q1_group *g=o->groups;g;g=g->next)if(!frontend_received_music_idle(g->music))return false;
    return true;
}
bool frontend_unified_q1_idle(const frontend_unified_q1 *o)
{ return (!o || !o->prepared) && frontend_unified_q1_checkpoint_ready(o); }
bool frontend_unified_q1_destroy(frontend_unified_q1 **slot,qa_error *e)
{
    if(!slot || !*slot)return true;
    frontend_unified_q1 *o=*slot;
    if(!frontend_unified_q1_idle(o))return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 CLIENT presentation still owns entered callbacks");
    for(q1_group *g=o->groups;g;g=g->next)for(q1_ambient *a=g->ambient;a;a=a->next)
        if(a->mixer && !qa_audio_mixer_callbacks_idle(a->mixer))return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 ambient source still borrows a busy mixer");
    o->music_retiring=true;
    for(q1_group *g=o->groups;g;g=g->next)if(!frontend_received_music_destroy(&g->music,e)){o->music_retiring=false;return false;}
    o->music_retiring=false;
    for(q1_group *g=o->groups;g;g=g->next)for(q1_ambient *a=g->ambient;a;a=a->next)if(a->mixer){qa_audio_mixer_remove_static(a->mixer,a->identity);a->mixer=NULL;}
    if(o->hud && !qa_hud_destroy(o->hud,e))return false;
    o->hud=NULL;
    while(o->groups){q1_group *g=o->groups;o->groups=g->next;group_free(g);}
    while(o->activations){q1_activation *a=o->activations;o->activations=a->next;free(a->provider);free(a);}
    prompt_clear(o);qa_unified_document_destroy(o->weapon);qa_unified_document_destroy(o->finale);qa_scene_image_release(o->finale_image);
    qa_localization_pool_destroy(o->localizations);free(o->scene_lights);free(o->scores);free(o);*slot=NULL;return true;
}
static const q1_group *group_at(const frontend_unified_q1 *o,size_t index)
{const q1_group *g=o?o->groups:NULL;while(g && index--)g=g->next;return g;}
size_t frontend_unified_q1_group_count(const frontend_unified_q1 *o)
{size_t count=0;for(const q1_group *g=o?o->groups:NULL;g;g=g->next)++count;return count;}
bool frontend_unified_q1_music_at(const frontend_unified_q1 *o,size_t index,uint64_t *bus,qa_audio_music **player)
{
    const q1_group *g=group_at(o,index);
    if(!g || !player || !frontend_received_music_bus(g->music,bus))return false;
    *player=frontend_received_music_player(g->music);return true;
}
const qa_scene_image *frontend_unified_q1_particle_image(const frontend_unified_q1 *o,size_t index)
{const q1_group *g=group_at(o,index);return g?g->particle_image:NULL;}
size_t frontend_unified_q1_light_count(const frontend_unified_q1 *o,size_t index)
{const q1_group *g=group_at(o,index);size_t count=0;for(size_t i=0;g && i<Q1_LIGHTS;++i)if(g->lights[i].identity)++count;return count;}
bool frontend_unified_q1_light_at(const frontend_unified_q1 *o,size_t group_index,size_t index,uint64_t *out)
{const q1_group *g=group_at(o,group_index);if(!g || !out)return false;for(size_t i=0;i<Q1_LIGHTS;++i)if(g->lights[i].identity){if(index--==0){*out=g->lights[i].identity;return true;}}return false;}
size_t frontend_unified_q1_static_count(const frontend_unified_q1 *o,size_t index)
{const q1_group *g=group_at(o,index);size_t count=0;for(const q1_ambient *a=g?g->ambient:NULL;a;a=a->next)++count;return count;}
bool frontend_unified_q1_static_at(const frontend_unified_q1 *o,size_t group_index,size_t index,uint64_t *identity,const qa_audio_asset **asset,qa_audio_mixer **mixer)
{
    const q1_group *g=group_at(o,group_index);const q1_ambient *a=g?g->ambient:NULL;while(a && index--)a=a->next;
    if(!a || !identity || !asset || !mixer)return false;
    *identity=a->identity;*asset=a->asset;*mixer=a->mixer;return true;
}
static bool saved_text(qa_source_save_io *io,char **value)
{
    bool present=*value!=NULL;if(!qa_source_save_bool(io,&present))return false;
    if(!present){if(io->direction==QA_SOURCE_SAVE_READ)*value=NULL;return true;}
    size_t count=io->direction==QA_SOURCE_SAVE_WRITE?strlen(*value):0;
    if(!qa_source_save_count(io,&count,UINT32_C(1048576)))return false;
    if(io->direction==QA_SOURCE_SAVE_READ){*value=malloc(count+1);if(!*value)return false;(*value)[count]=0;}
    return qa_source_save_bytes(io,*value,count) && !memchr(*value,0,count);
}
static bool saved_bytes(qa_source_save_io *io,qa_buffer *buffer)
{
    if(!qa_source_save_count(io,&buffer->size,UINT32_C(16777216)))return false;
    if(io->direction==QA_SOURCE_SAVE_READ && buffer->size){buffer->data=malloc(buffer->size);if(!buffer->data)return false;}
    return qa_source_save_bytes(io,buffer->data,buffer->size);
}
static bool saved_document(qa_source_save_io *io,qa_unified_document **doc,qa_error *e)
{
    bool present=*doc!=NULL;if(!qa_source_save_bool(io,&present))return false;if(!present)return true;
    qa_buffer bytes={0};bool ok=io->direction==QA_SOURCE_SAVE_READ || qa_unified_document_encode(*doc,&bytes,e);
    if(ok)ok=saved_bytes(io,&bytes);
    if(ok && io->direction==QA_SOURCE_SAVE_READ)ok=qa_unified_document_decode(QA_UNIFIED_CHECKPOINT,(qa_bytes){bytes.data,bytes.size},doc,e);
    qa_buffer_free(&bytes);return ok;
}
static bool saved_identity(qa_source_save_io *io,const frontend_unified_q1_refs *refs,bool audio,uint64_t *identity,qa_error *e)
{
    uint64_t value=0;bool present=*identity!=0;if(!qa_source_save_bool(io,&present))return false;if(!present)return true;
    bool ok=refs && (io->direction==QA_SOURCE_SAVE_READ?refs->identity_decode!=NULL:refs->identity_encode!=NULL);
    if(ok && io->direction==QA_SOURCE_SAVE_WRITE)ok=refs->identity_encode(refs->context,audio,*identity,&value,e);
    if(ok)ok=qa_source_save_u64(io,&value) && value!=0;
    if(ok && io->direction==QA_SOURCE_SAVE_READ)ok=refs->identity_decode(refs->context,audio,value,identity,e) && *identity!=0;
    return ok || fail(e,"Q1 cold producer lost its actual shared scene/audio identity");
}
static bool finite_field(qa_source_save_io *io,double *value)
{return qa_source_save_f64(io,value) && isfinite(*value);}
static bool saved_activation(frontend_unified_q1 *o,qa_source_save_io *io,q1_activation **owner,qa_error *e)
{
    uint64_t ordinal=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE && *owner){uint64_t index=1;
        for(q1_activation *a=o->activations;a;a=a->next,++index)if(a==*owner){ordinal=index;break;}
        if(!ordinal)return fail(e,"Q1 cold state has a foreign Source activation");}
    if(!qa_source_save_u64(io,&ordinal))return false;
    if(io->direction==QA_SOURCE_SAVE_READ){*owner=NULL;if(ordinal){q1_activation *a=o->activations;
        while(a && --ordinal)a=a->next;
        if(!a || a->retired)return fail(e,"Q1 cold state refers to an absent or retired Source activation");
        *owner=a;}}
    return true;
}
static bool fields(frontend_unified_q1 *o,qa_source_save_io *io,const frontend_unified_q1_refs *refs,qa_error *e)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;uint32_t magic=UINT32_C(0x31554651),epoch=o->epoch;
    if(!qa_source_save_u32(io,&magic) || magic!=UINT32_C(0x31554651) || !qa_source_save_u32(io,&epoch) || epoch!=o->epoch || !qa_source_save_u64(io,&o->frame) || !finite_field(io,&o->seconds) ||
        !qa_source_save_bool(io,&o->has_frame) || !finite_field(io,&o->bonus_until) || !finite_field(io,&o->capture_until) ||
        !qa_source_save_bool(io,&o->ctf_present))return false;
    if(!qa_source_save_bool(io,&o->prepared))return false;
    if(o->prepared){qa_buffer bytes={0};
        if(!qa_source_save_u64(io,&o->prepared_frame) || !finite_field(io,&o->prepared_seconds) ||
            (o->has_frame && o->prepared_frame<=o->frame))return false;
        bool ok=reading || (o->preparing_document && qa_unified_document_encode(o->preparing_document,&bytes,e));
        if(ok)ok=saved_bytes(io,&bytes);
        if(ok && reading)ok=qa_unified_document_decode(QA_UNIFIED_FRAME_DOCUMENT,(qa_bytes){bytes.data,bytes.size},&o->restored_preparing,e);
        qa_buffer_free(&bytes);if(!ok)return false;
        if(reading)o->preparing_document=o->restored_preparing;
        const qa_unified_document *d=o->preparing_document;const qa_json_document *j=qa_unified_document_json(d);
        qa_json_id root=qa_unified_document_root(d),f=field(j,field(j,field(j,root,"output"),"snapshot"),"frame"),t=field(j,f,"time");
        uint64_t actual_epoch,n;double seconds;
        if(!qa_json_u64(j,field(j,root,"epoch"),&actual_epoch,e) || actual_epoch!=epoch ||
            !qa_json_u64(j,field(j,f,"frame"),&n,e) || n!=o->prepared_frame || !number(d,field(j,t,"value"),&seconds,e))return false;
        if(qa_json_string_equal(j,field(j,t,"kind"),"milliseconds"))seconds/=1000;
        else if(!qa_json_string_equal(j,field(j,t,"kind"),"seconds"))return false;
        if(seconds!=o->prepared_seconds)return false;
    }
    if(!qa_source_save_bytes(io,o->random.words,sizeof(o->random.words)) || !qa_source_save_u8(io,&o->random.front) || !qa_source_save_u8(io,&o->random.rear) ||
        o->random.front>=31 || o->random.rear>=31 || !qa_source_save_u64(io,&o->random.draws))return false;
    size_t owners=0;for(q1_activation *owner=o->activations;owner;owner=owner->next)++owners;
    if(!qa_source_save_count(io,&owners,65536))return false;
    q1_activation *owner_row=o->activations,**owner_tail=&o->activations;
    for(size_t i=0;i<owners;++i){if(reading){owner_row=calloc(1,sizeof(*owner_row));if(!owner_row)return frontend_unified_fail(e,QA_ERROR_MEMORY,"Restoring Q1 Source activation");*owner_tail=owner_row;owner_tail=&owner_row->next;}
        if(!saved_text(io,&owner_row->provider) || !owner_row->provider || !*owner_row->provider || !qa_source_save_u64(io,&owner_row->generation) || !owner_row->generation || !qa_source_save_bool(io,&owner_row->retired))return false;
        if(reading){for(q1_activation *old=o->activations;old!=owner_row;old=old->next)
            if(old->generation==owner_row->generation && !strcmp(old->provider,owner_row->provider))return fail(e,"Q1 cold Source activation is duplicated");}
        else owner_row=owner_row->next;
    }
    for(size_t i=0;i<Q1_POWERS;++i)if(!finite_field(io,o->powers+i) || !saved_activation(o,io,o->power_activations+i,e))return false;
    if(!saved_activation(o,io,&o->weapon_activation,e) || !saved_activation(o,io,&o->prompt_activation,e) || !saved_activation(o,io,&o->ctf_activation,e))return false;
    if(!saved_activation(o,io,&o->fog_activation,e) || !saved_activation(o,io,&o->finale_activation,e) || !saved_activation(o,io,&o->pause_activation,e) ||
        !qa_source_save_bool(io,&o->fog.active) || !qa_source_save_vec3(io,&o->fog.previous) || !qa_source_save_vec3(io,&o->fog.target) ||
        !qa_vec_finite(o->fog.previous) || !qa_vec_finite(o->fog.target) || !finite_field(io,&o->fog.previous_density) ||
        !finite_field(io,&o->fog.target_density) || !finite_field(io,&o->fog.start) || !finite_field(io,&o->fog.duration) || !finite_field(io,&o->fog.sky_factor) ||
        !qa_source_save_bool(io,&o->monsters_present) || !qa_source_save_bool(io,&o->secrets_present) || !finite_field(io,&o->monsters) ||
        !finite_field(io,&o->total_monsters) || !finite_field(io,&o->secrets) || !finite_field(io,&o->total_secrets) || !qa_source_save_bool(io,&o->finale_banner))return false;
    for(size_t i=0;i<4;++i)if(!finite_field(io,&o->ctf[i].value))return false;
    if(!saved_document(io,&o->weapon,e) || !saved_document(io,&o->prompt,e) || !saved_document(io,&o->finale,e))return false;
    if(reading && o->finale){q1_event p={0};q1_activation *actual=NULL;
        bool valid=parse(o,o->finale,qa_unified_document_root(o->finale),&p,e) && p.kind==Q1_FINALE &&
            activation(o,&p,&actual,e) && actual==o->finale_activation;event_free(&p);if(!valid)return false;}
    bool has_finale_image=o->finale_image!=NULL;uint64_t finale_image_id=0;
    if(!qa_source_save_bool(io,&has_finale_image))return false;
    if(has_finale_image){if(!refs || (!reading && (!refs->hud.image_encode || !refs->hud.image_encode(refs->hud.context,o->finale_image,&finale_image_id,e))) ||
            !qa_source_save_u64(io,&finale_image_id) || !finale_image_id)return fail(e,"Q1 finale image has no actual cold image graph");
        if(reading){const qa_scene_image *image=NULL;q1_event p={0};qa_scene_resources *images;qa_material_library *materials;qa_font_library *fonts;qa_audio_bank *sounds;
            bool valid=o->finale && parse(o,o->finale,qa_unified_document_root(o->finale),&p,e) &&
                frontend_unified_media_bank(o->media,(char *)p.content.data,&images,&materials,&fonts,&sounds,e) &&
                refs->hud.image_decode && refs->hud.image_decode(refs->hud.context,finale_image_id,&image,e) && image && qa_scene_image_owner(image)==images;
            event_free(&p);if(!valid)return fail(e,"Q1 finale image does not belong to its restored content");
            o->finale_image=(qa_scene_image *)image;qa_scene_image_retain(image);}}
    if((o->finale!=NULL)!=has_finale_image)return fail(e,"Q1 finale lost its genuine retained image/text pair");
    if(reading && o->weapon){q1_event p={0};q1_activation *actual=NULL;
        bool ok=parse(o,o->weapon,qa_unified_document_root(o->weapon),&p,e) && p.kind==Q1_WEAPON && activation(o,&p,&actual,e) && actual==o->weapon_activation;
        event_free(&p);if(!ok)return false;}
    if(reading && o->prompt){qa_unified_document *doc=o->prompt;o->prompt=NULL;q1_event p={0};qa_json_id root=qa_unified_document_root(doc);
        q1_activation *actual=NULL,*expected=o->prompt_activation;
        bool ok=parse(o,doc,root,&p,e) && p.kind==Q1_PROMPT && activation(o,&p,&actual,e) && actual==expected && prompt_set(o,&p,root,e);
        if(ok)o->prompt_activation=expected;
        event_free(&p);qa_unified_document_destroy(doc);if(!ok)return false;}
    qa_buffer hud={0};bool ok=reading || qa_hud_checkpoint(o->hud,refs?&refs->hud:NULL,&hud,e);
    if(ok)ok=saved_bytes(io,&hud);
    if(ok && reading){const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);qa_hud *restored=NULL;
        ok=qa_hud_restore((qa_bytes){hud.data,hud.size},&(qa_hud_options){.ui=o->frontend->seats[d->physical_seat].ui,
            .application=d->application,.seat=d->physical_seat,.context=o,.read=hud_read},refs?&refs->hud:NULL,&restored,e);
        if(ok){ok=qa_hud_destroy(o->hud,e);if(ok)o->hud=restored;else qa_hud_destroy(restored,NULL);}}
    qa_buffer_free(&hud);if(!ok)return false;
    size_t count=frontend_unified_q1_group_count(o);if(!qa_source_save_count(io,&count,4096))return false;
    q1_group *g=o->groups;
    for(size_t index=0;index<count;++index){char *content=reading?NULL:g->content;q1_activation *owner=reading?NULL:g->activation;
        if(!saved_text(io,&content) || !content)return false;
        if(!saved_activation(o,io,&owner,e)){if(reading)free(content);return false;}
        if(reading){for(q1_group *old=o->groups;old;old=old->next)if(old->activation==owner && !strcmp(old->content,content)){free(content);return fail(e,"Q1 cold content/Source group is duplicated");}
            bool loaded=group(o,content,owner,&g,e);free(content);if(!loaded)return false;}
        if(!frontend_received_music_fields(o->frontend,&g->music,io,refs?refs->audio:NULL,e))return false;
        if(!frontend_fx_particles_fields(io,&g->particles) || g->particles.family!=QA_GAME_Q1 ||
            !finite_field(io,&g->sampled) || !qa_source_save_bool(io,&g->has_sample))return false;
        bool image=g->particle_image!=NULL;uint64_t image_id=0;
        if(!qa_source_save_bool(io,&image))return false;
        if(image){if(!refs)return fail(e,"Q1 particle image has no retained cold graph");
            if(!reading && (!refs->hud.image_encode || !refs->hud.image_encode(refs->hud.context,g->particle_image,&image_id,e)))return false;
            if(!qa_source_save_u64(io,&image_id) || !image_id)return false;
            if(reading){const qa_scene_image *actual=NULL;if(!refs->hud.image_decode || !refs->hud.image_decode(refs->hud.context,image_id,&actual,e) || !actual || qa_scene_image_owner(actual)!=g->images)return fail(e,"Q1 particle image is outside its restored content bank");
                g->particle_image=(qa_scene_image *)actual;qa_scene_image_retain(actual);}}
        for(size_t i=0;i<Q1_LIGHTS;++i){q1_light *l=g->lights+i;if(!qa_source_save_vec3(io,&l->origin) || !qa_vec_finite(l->origin) || !finite_field(io,&l->born) || !finite_field(io,&l->until) ||
                !qa_source_save_f32(io,&l->radius) || !qa_source_save_f32(io,&l->decay) || !qa_source_save_f32(io,&l->minimum) || !isfinite(l->radius) || l->radius<0 || !isfinite(l->decay) || l->decay<0 || !isfinite(l->minimum) || l->minimum<0 || !saved_identity(io,refs,false,&l->identity,e))return false;}
        for(size_t i=0;i<Q1_BEAMS;++i){q1_beam *b=g->beams+i;if(!qa_source_save_u32(io,&b->actor.slot) || !qa_source_save_u64(io,&b->actor.generation) || !qa_source_save_vec3(io,&b->start) || !qa_source_save_vec3(io,&b->end) ||
                !qa_vec_finite(b->start) || !qa_vec_finite(b->end) || !finite_field(io,&b->until) || !qa_source_save_u8(io,&b->kind) || b->kind>3 || !qa_source_save_u32(io,&b->roll_seed))return false;}
        for(size_t i=0;i<256;++i){if(!saved_text(io,g->styles+i) || !qa_source_save_u64(io,g->style_sequences+i) ||
            !qa_source_save_bool(io,&g->clients[i].present) || !qa_source_save_bool(io,&g->clients[i].has_ping) || !saved_text(io,&g->clients[i].name) ||
            !saved_text(io,&g->clients[i].social) || !saved_text(io,&g->clients[i].info) || !finite_field(io,&g->clients[i].colors) || !finite_field(io,&g->clients[i].frags) || !finite_field(io,&g->clients[i].ping))return false;}
        for(size_t i=0;i<256;++i)for(size_t column=0;column<6;++column)
            if(!qa_source_save_u64(io,g->clients[i].sequences+column) || !qa_source_save_bool(io,g->clients[i].fields+column))return false;
        if(!saved_text(io,&g->sky_name) || !qa_source_save_bool(io,&g->sky_found) || !qa_source_save_u64(io,&g->sky_sequence))return false;
        for(size_t i=0;i<6;++i){bool present=g->sky[i]!=NULL;uint64_t id=0;if(!qa_source_save_bool(io,&present))return false;if(!present)continue;
            if(!refs || (!reading && (!refs->hud.image_encode || !refs->hud.image_encode(refs->hud.context,g->sky[i],&id,e))) || !qa_source_save_u64(io,&id) || !id)return false;
            if(reading){const qa_scene_image *sky_image=NULL;if(!refs->hud.image_decode || !refs->hud.image_decode(refs->hud.context,id,&sky_image,e) || !sky_image || qa_scene_image_owner(sky_image)!=g->images)return fail(e,"Q1 sky is outside its actual cold image bank");g->sky[i]=(qa_scene_image *)sky_image;qa_scene_image_retain(sky_image);}}
        size_t statics=0;for(q1_static *s=g->statics;s;s=s->next)++statics;
        if(!qa_source_save_count(io,&statics,65536))return false;
        q1_static *s=g->statics,**static_tail=&g->statics;
        for(size_t i=0;i<statics;++i){if(reading){s=calloc(1,sizeof(*s));if(!s)return false;*static_tail=s;static_tail=&s->next;}
            if(!saved_text(io,&s->path) || !s->path || !qa_source_save_vec3(io,&s->origin) || !qa_source_save_vec3(io,&s->angles) ||
                !qa_vec_finite(s->origin) || !qa_vec_finite(s->angles) || !qa_source_save_u32(io,&s->frame) || !qa_source_save_u32(io,&s->skin))return false;
            if(reading){qa_scene_image_options options=model_options();if(!frontend_unified_media_model(o->media,g->content,s->path,QA_SCENE_Q1,&options,&s->model,e))return false;}else s=s->next;}
        size_t ambient=0;for(q1_ambient *a=g->ambient;a;a=a->next)++ambient;
        if(!qa_source_save_count(io,&ambient,65536))return false;
        q1_ambient *a=g->ambient,**audio_tail=&g->ambient;
        for(size_t i=0;i<ambient;++i){if(reading){a=calloc(1,sizeof(*a));if(!a)return false;*audio_tail=a;audio_tail=&a->next;}
            bool installed=a->mixer!=NULL;uint64_t asset=0;
            if(!saved_text(io,&a->path) || !a->path || !qa_source_save_vec3(io,&a->origin) || !qa_vec_finite(a->origin) || !qa_source_save_f32(io,&a->volume) || !qa_source_save_f32(io,&a->attenuation) ||
                !isfinite(a->volume) || !isfinite(a->attenuation) || !saved_identity(io,refs,true,&a->identity,e) || !a->identity || !qa_source_save_bool(io,&installed))return false;
            if(!refs || (!reading && (!refs->asset_encode || !refs->asset_encode(refs->context,a->asset,&asset,e))) || !qa_source_save_u64(io,&asset) || !asset)return fail(e,"Q1 ambient asset is absent from the actual cold audio graph");
            if(reading){const qa_audio_asset *actual=NULL;if(!refs->asset_decode || !refs->asset_decode(refs->context,asset,&actual,e) || !actual)return false;
                const qa_audio_asset *bank=qa_audio_bank_get(g->sounds,qa_resource_id(qa_audio_asset_resource(actual)),QA_AUDIO_Q1);
                if(bank!=actual)return fail(e,"Q1 ambient asset does not belong to its imported content bank");
                a->asset=(qa_audio_asset *)actual;qa_audio_asset_retain(a->asset);}
            if(reading)a->saved_installed=installed;
            if(!reading && installed){qa_audio_mixer *m=a->mixer;qa_audio_static_view view;
                if(!m || !qa_audio_mixer_static_read(m,a->identity,&view) || view.sample!=qa_audio_asset_sample(a->asset) ||
                    view.origin.x!=a->origin.x || view.origin.y!=a->origin.y || view.origin.z!=a->origin.z ||
                    view.volume!=trunc((double)truncf(a->volume*255)/255*255) || view.attenuation!=(double)truncf(a->attenuation*64)/64000)
                    return fail(e,"Q1 ambient voice does not match the imported actual mixer");
            }
            if(!reading)a=a->next;
        }
        if(!reading)g=g->next;
    }
    return true;
}
bool frontend_unified_q1_checkpoint(frontend_unified_q1 *o,const frontend_unified_q1_refs *refs,qa_buffer *out,qa_error *e)
{
    if(!o || o->restoring || !out || out->data || !frontend_unified_q1_checkpoint_ready(o) || !checkpoint_current(o,e))return fail(e,"Q1 cold capture overlaps a live or foreign CLIENT owner");
    qa_source_save_io io;if(!qa_source_save_writer(&io,NULL,e))return false;
    bool ok=fields(o,&io,refs,e) && qa_source_save_finish(&io,out);qa_source_save_dispose(&io);return ok;
}
bool frontend_unified_q1_restore(qa_frontend *f,frontend_remote_unified *r,frontend_unified_media *media,const frontend_unified_q1_options *options,
    const frontend_unified_q1_refs *refs,qa_bytes input,frontend_unified_q1 **out,qa_error *e)
{
    if(!out || *out)return fail(e,"Q1 cold candidate output is occupied");
    frontend_unified_q1 *o=NULL;
    if(!frontend_unified_q1_create(f,r,media,options,&o,e))return false;
    o->restoring=true;
    *out=o;
    qa_source_save_io io;
    bool opened=qa_source_save_reader(&io,NULL,input,e),ok=opened && fields(o,&io,refs,e) && qa_source_save_finish(&io,NULL);
    if(opened)qa_source_save_dispose(&io);
    if(!ok){/* Preserve a checked cleanup owner even when imported native audio prevents disposal. */
        frontend_unified_q1_frame_abort(o);(void)frontend_unified_q1_destroy(out,NULL);
        return false;}
    return true;
}
bool frontend_unified_q1_restore_finish(frontend_unified_q1 *o,qa_error *e)
{
    if(!o || !o->frontend->source_restoring || !checkpoint_current(o,e))return fail(e,"Q1 music import lost its real retained CLIENT recipe");
    for(q1_group *g=o->groups;g;g=g->next)if(g->music){frontend_music_origin origin;
        if(!music_origin(g,&origin,e) || !frontend_received_music_restore_finish(g->music,&origin,e))return false;}
    for(q1_group *g=o->groups;g;g=g->next)for(q1_ambient *a=g->ambient;a;a=a->next)if(a->saved_installed){
        qa_audio_mixer *m=qa_audio_engine_seat_mixer(o->frontend->audio,o->replica->options.domain.physical_seat);
        qa_audio_static_view view;
        if(!m || !qa_audio_mixer_callbacks_idle(m) || !qa_audio_mixer_static_read(m,a->identity,&view) ||
            view.sample!=qa_audio_asset_sample(a->asset) || view.origin.x!=a->origin.x ||
            view.origin.y!=a->origin.y || view.origin.z!=a->origin.z ||
            view.volume!=trunc((double)truncf(a->volume*255)/255*255) ||
            view.attenuation!=(double)truncf(a->attenuation*64)/64000)
            return fail(e,"Q1 ambient voice does not match the imported actual mixer");
        a->mixer=m;a->saved_installed=false;
    }
    o->restoring=false;
    return true;
}

bool frontend_unified_q1_frame_restore_bind(frontend_unified_q1 *o,const qa_unified_document *d,qa_error *e)
{
    if(!o || !o->frontend->source_restoring || !frontend_unified_q1_checkpoint_ready(o) || !checkpoint_current(o,e))return false;
    if(!o->prepared)return d==o->replica->prepared_frame;
    if(!d || d!=o->replica->prepared_frame || qa_unified_document_type(d)!=QA_UNIFIED_FRAME_DOCUMENT)return false;
    if(!o->restored_preparing)return o->preparing_document==d;
    if(!frontend_unified_document_restore_bind(&o->restored_preparing,d,true,e)) return false;
    o->preparing_document=d;qa_unified_document_destroy(o->restored_preparing);o->restored_preparing=NULL;return true;
}
