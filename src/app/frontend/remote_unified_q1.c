#include "remote_unified_q1.h"
#include "remote_unified_private.h"
#include "remote_unified_metadata.h"
#include "remote_unified_save.h"
#include "selected_effects_particles.h"
#include "selected_effects_q1_temporary.h"
#include "legacy_render_policy.h"
#include "q1_help.h"
#include "scene_identity.h"
#include "received_music.h"
#include "qa/localization.h"
#include "qa/ui_language.h"
#include "qa/text.h"
#include "qa/q1_text.h"
#include "qa/ui_preferences.h"
#include "qa/player_progress.h"
#include "qa/network_unified_frame.h"
#include "qa/unified_frame_events.h"
#include "qa/application_ui_names.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { Q1_LIGHTS=64, Q1_BEAMS=32, Q1_POWERS=9, Q1_PROMPT_CHOICES=65536 };
static const uint8_t q1_beam_types[]={5,6,9,13};
typedef enum q1_event_kind {
    Q1_PARTICLES,Q1_EFFECT,Q1_COLORS,Q1_BEAM,Q1_STYLE,Q1_STATIC,Q1_WEAPON,
    Q1_POWER,Q1_MESSAGE,Q1_STOP,Q1_AMBIENT,Q1_SOUND,Q1_CTF_STATUS,Q1_CTF_CAPTURE,
    Q1_PROMPT,Q1_CLEAR_PROMPT,Q1_LOG,Q1_TOTAL,Q1_FOUND,Q1_ACHIEVEMENT,Q1_SKY,
    Q1_FOG,Q1_FINALE,Q1_ACTION,Q1_MUSIC,Q1_PAUSE,Q1_COMPLETED,Q1_SELL_SCREEN
} q1_event_kind;
typedef enum q1_effect_type { Q1_FX_POINT, Q1_FX_MUZZLE, Q1_FX_PICKUP, Q1_FX_BLOOD } q1_effect_type;
typedef struct q1_event {
    q1_event_kind kind;
    qa_bytes content,text,extra,provider;
    q1_effect_type effect;
    uint8_t index;
    qa_actor_id actor;
    qa_vec3 origin,end,angles;
    double seconds,a,b,c,d,sky_factor;
    uint64_t sequence,owner_generation;
    bool actor_present,flag,muzzle;
    const qa_unified_presentation_event *row;
} q1_event;
typedef struct q1_continuation {
    qa_string_id content, path;
    const qa_unified_presentation_event *row;
    qa_unified_document *document;
} q1_continuation;
typedef struct q1_activation {
    struct q1_activation *next;
    qa_string_id provider;
    uint64_t generation;
    bool retired;
} q1_activation;
typedef struct q1_static {
    struct q1_static *next;
    const char *path;
    qa_vec3 origin,angles;
    uint32_t frame,skin;
    frontend_unified_model model;
} q1_static;
typedef struct q1_ambient {
    struct q1_ambient *next;
    const char *path;
    qa_audio_asset *asset;
    qa_audio_mixer *mixer;
    qa_vec3 origin;
    float volume,attenuation;
    uint64_t identity;
} q1_ambient;
typedef struct q1_entity_trail {
    qa_actor_id actor;
    const qa_model *model;
    const qa_product *product;
    qa_vec3 origin;
    uint64_t frame;
    bool present;
} q1_entity_trail;
typedef struct q1_group {
    frontend_unified_q1 *parent;
    frontend_received_music *music;
    struct q1_group *next;
    qa_string_id content_name;
    const char *content;
    q1_activation *activation;
    const qa_product *product;
    qa_vfs *files;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_audio_bank *sounds;
    qa_localization *localization;
    char language[64];
    frontend_fx_q1_state effects;
    qa_scene_image *particle_image;
    qa_string_id beam_models[4];
    q1_static *statics;
    q1_ambient *ambient;
    const char *styles[256];
    uint64_t style_sequences[256],sky_sequence;
    struct {const char *name,*social,*info;double colors,frags,ping;uint64_t sequences[6];bool fields[6],present,has_ping;} clients[256];
    const char *sky_name;
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
    q1_continuation weapon,prompt,finale;
    qa_scene_image *finale_image;
    q1_activation *fog_activation,*finale_activation,*intermission_activation;
    struct {qa_vec3 previous,target;double previous_density,target_density,start,duration,sky_factor;bool active;} fog;
    bool finale_banner,intermission;
    double completed_seconds;
    q1_activation *pause_activation;
    bool music_retiring;
    double monsters,total_monsters,secrets,total_secrets;
    const qa_unified_document *preparing_document;
    qa_hud_timer timers[Q1_POWERS];
    double powers[Q1_POWERS];
    qa_hud_value ctf[4];
    qa_hud_value display_bars[6];
    const char *prompt_title,**prompt_lines;
    size_t prompt_count;
    uint32_t epoch;
    uint64_t frame,prepared_frame;
    double seconds,prepared_seconds,bonus_until,capture_until;
    bool has_frame,prepared,busy,ctf_present;
    bool monsters_present,secrets_present;
    qa_arena semantic_storage;
    q1_entity_trail *trails;
    size_t trail_capacity;
    qa_scene_light *scene_lights;
    float scene_styles[256];
    qa_hud_score *scores;
    size_t score_count;
    char localized_text[65538];
};
static const char *const powers[Q1_POWERS]={"quad","invulnerability","invisibility","suit",
    "hipnotic:wetsuit","hipnotic:empathy","rogue:shield","rogue:antigrav","mg3:lavasuit"};
static bool fail(qa_error *e,const char *message)
{ return frontend_unified_fail(e,QA_ERROR_FORMAT,message); }
static void continuation_clear(q1_continuation *value)
{ qa_unified_document_destroy(value->document); *value=(q1_continuation){0}; }
static void continuation_replace(q1_continuation *destination,q1_continuation *source)
{ continuation_clear(destination); *destination=*source; *source=(q1_continuation){0}; }
static bool same_wire(qa_actor_id a,qa_actor_id b)
{ return qa_actor_id_equal(a,b); }
const qa_unified_q1_world_state *frontend_unified_q1_world_read(const frontend_remote_unified *replica)
{
    const qa_unified_frame *frame=qa_unified_document_frame(frontend_remote_unified_frame(replica));
    const qa_unified_frame_metadata *metadata=frontend_remote_unified_metadata(replica);
    return frontend_remote_unified_current(replica,NULL) && frame && frame->world && metadata &&
        metadata->epoch==frame->epoch && metadata->frame<=frame->world->source.number ? metadata->q1 : NULL;
}
static uint64_t ns(double value)
{ return value<=0?0:value>=18446744073.709551615?UINT64_MAX:(uint64_t)(value*1e9); }
static bool text_view(const char *text,qa_bytes *out,qa_error *e)
{
    (void)e; if(!text) text="";
    *out=(qa_bytes){(const uint8_t *)text,strlen(text)}; return true;
}
static bool owner_parse(const qa_source_owner *owner,q1_event *p,qa_error *e)
{
    p->owner_generation=owner->generation;
    return !owner->provider || text_view(owner->provider,&p->provider,e);
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
    return (frontend_unified_q1_current(o) && !o->frontend->capture && !o->frontend->resource_inventory &&
        !o->frontend->source_restoring) || frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 CLIENT presentation has a foreign or captured parent");
}
static bool parse(frontend_unified_q1 *o,const qa_unified_presentation_event *row,q1_event *p,qa_error *e)
{
    if (!row || row->payload.kind!=QA_UNIFIED_PRESENTATION_BUILTIN || row->family!=QA_GAME_Q1)
        return fail(e,"Q1 presentation has no actual builtin Source event");
    const qa_builtin_event *v=&row->payload.value.builtin;
    const char *resource=qa_strings_cstr(o->replica->strings,v->resource);
    const char *text=qa_strings_cstr(o->replica->strings,v->text);
    p->row=row; p->seconds=row->seconds; p->sequence=row->sequence;
    p->actor=v->actor; p->actor_present=v->actor.registry!=0;
    p->origin=v->origin; p->end=v->end; p->angles=v->muzzle_angles;
    if (!text_view(row->content,&p->content,e) || !owner_parse(&row->owner,p,e)) return false;
    const qa_product *product=qa_catalog_find(qa_executable_recipe_catalog(frontend_remote_unified_recipe(o->replica)),(char *)p->content.data);
    if (!product || product->family!=QA_GAME_Q1) return fail(e,"Q1 event content is absent from its admitted Source");
    const qa_application_ui_names *names=qa_application_ui_names_read(o->frontend->application);
    p->kind=Q1_EFFECT;
    switch (v->kind) {
    case QA_BUILTIN_SOUND:
        p->kind=v->flags&1u?Q1_AMBIENT:Q1_SOUND; p->flag=true;
        p->a=v->volume; p->b=v->attenuation; p->c=v->channel;
        return text_view(resource,&p->text,e);
    case QA_BUILTIN_STOP_SOUND:p->kind=Q1_STOP; p->a=v->channel; return true;
    case QA_BUILTIN_PARTICLES:p->kind=Q1_PARTICLES; p->end=v->direction; p->a=v->code; p->b=v->count; return true;
    case QA_BUILTIN_LIGHT:p->kind=Q1_STYLE; p->a=v->code; return text_view(resource,&p->text,e);
    case QA_BUILTIN_BEAM:
        if (v->code<1 || v->code>4) return fail(e,"Q1 beam has no original Source style");
        p->kind=Q1_BEAM;p->index=(uint8_t)(v->code-1);return true;
    case QA_BUILTIN_IMPACT:
        if (v->code==1) {p->effect=Q1_FX_BLOOD;p->a=v->value*2;break;}
        switch (v->code) {
        case 2:p->index=2;break;case 3:p->index=0;break;case 4:p->index=1;break;
        case 7:p->index=7;break;case 8:p->index=8;break;case 10:p->index=10;break;
        default:return fail(e,"Q1 impact has no original effect recipe");
        }
        break;
    case QA_BUILTIN_EXPLOSION:p->index=v->code==1?4:v->code==10?10:3;break;
    case QA_BUILTIN_TELEPORT:p->index=11;break;
    case QA_BUILTIN_MUZZLE:p->effect=Q1_FX_MUZZLE;p->muzzle=v->has_muzzle_pose;p->end=v->origin;break;
    case QA_BUILTIN_ITEM:p->actor=v->other;p->actor_present=v->other.registry!=0;p->effect=Q1_FX_PICKUP;break;
    case QA_BUILTIN_ANIMATION:p->kind=Q1_WEAPON;p->a=v->frame;p->b=v->value;return text_view(resource,&p->text,e);
    case QA_BUILTIN_MESSAGE:case QA_BUILTIN_CENTERPRINT:
        p->kind=Q1_MESSAGE;p->flag=v->kind==QA_BUILTIN_CENTERPRINT || !(v->flags&2u);return text_view(text,&p->text,e);
    case QA_BUILTIN_ACHIEVEMENT:p->kind=Q1_ACHIEVEMENT;return text_view(text,&p->text,e);
    case QA_BUILTIN_CTF_STATUS:p->kind=Q1_CTF_STATUS;p->a=v->ctf_status.red;p->b=v->ctf_status.blue;p->c=v->ctf_status.flags;p->d=v->ctf_status.rune_items;return true;
    case QA_BUILTIN_CTF_CAPTURE:p->kind=Q1_CTF_CAPTURE;p->a=v->ctf_capture.total;p->index=v->ctf_capture.blue?1:0;return true;
    case QA_BUILTIN_SOURCE_LOG:p->kind=Q1_LOG;return text_view(text,&p->text,e);
    case QA_BUILTIN_SOURCE_PROMPT:p->kind=Q1_PROMPT;return text_view(text,&p->text,e);
    case QA_BUILTIN_CLEAR_PROMPT:p->kind=Q1_CLEAR_PROMPT;return true;
    case QA_BUILTIN_Q1_POWERUP:
        if (v->q1_powerup.power>=Q1_POWERS) return fail(e,"Q1 power has no Source timer identity");
        p->kind=Q1_POWER;p->a=v->q1_powerup.expires;p->index=(uint8_t)v->q1_powerup.power;return true;
    case QA_BUILTIN_DEATH:p->kind=Q1_FOUND;p->a=v->count;p->b=v->code;return true;
    case QA_BUILTIN_TARGET:
        if ((v->flags&UINT32_C(0x80000000)) && v->code==1) {p->kind=Q1_COMPLETED;return true;}
        return fail(e,"Q1 target has no native presentation operation");
    case QA_BUILTIN_EFFECT:
        if (v->resource==names->music) {p->kind=Q1_MUSIC;p->a=v->code;return true;}
        if (v->resource==names->sell_screen) {p->kind=Q1_SELL_SCREEN;return true;}
        if (v->flags&UINT32_C(0x80000000)) {p->kind=v->code==1?Q1_COMPLETED:Q1_FINALE;p->a=v->code;
            return text_view(text,&p->text,e);}
        if (v->resource==names->cutscene) {p->kind=Q1_FINALE;p->a=3;return text_view(text,&p->text,e);}
        if (v->resource==names->colored_explosion) {p->kind=Q1_COLORS;p->a=v->code;p->b=v->count;return true;}
        if (v->resource==names->developer_message) {p->kind=Q1_MESSAGE;p->flag=false;return text_view(text,&p->text,e);}
        if (!v->actor.registry && resource) {p->kind=Q1_STATIC;p->a=v->frame;p->b=v->code;p->c=v->channel;p->angles=v->direction;return text_view(resource,&p->text,e);}
        if (!resource && v->other.registry && v->count>0) {p->kind=Q1_FOUND;p->flag=true;p->a=v->count;p->b=v->code;return true;}
        return fail(e,"Q1 effect has no reached original Source operation");
    default:return fail(e,"Q1 presentation has no installed native consumer");
    }
    return true;
}
static bool owns(frontend_unified_q1 *o,const q1_event *p,qa_error *e)
{
    qa_actor_id actual,player;uint32_t source;
    if(!p->actor_present) return true;
    return frontend_remote_unified_source_actor(o->replica,qa_unified_document_frame(frontend_remote_unified_frame(o->replica)),p->actor,false,&actual,e) &&
        frontend_remote_unified_player(o->replica,&player,&source) && qa_actor_id_equal(actual,player);
}
static bool progress_target(frontend_unified_q1 *o,const q1_event *p,
    bool *local,qa_player_progress **store,const char **value,uint32_t *seat,qa_error *e)
{
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    if(!domain || !o->frontend->seats || domain->physical_seat>=o->frontend->options.seats ||
        o->frontend->seats[domain->physical_seat].frontend!=o->frontend)
        return fail(e,"Q1 progress lost its actual local frontend seat");
    *local=!p->actor_present || same_wire(p->actor,o->replica->wire_player);
    if (p->row->recipient.registry)
        *local=*local && same_wire(p->row->recipient,o->replica->wire_player);
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
static bool progress_record(frontend_unified_q1 *o,const q1_event *p,qa_error *e)
{
    bool local;qa_player_progress *store;const char *value;uint32_t seat;
    if(!progress_target(o,p,&local,&store,&value,&seat,e))return false;
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
    qa_string_id provider=qa_strings_find(o->replica->strings,p->provider);
    for(q1_activation *a=o->activations;a;a=a->next)if(a->generation==p->owner_generation && a->provider==provider){*out=a;return true;}
    q1_activation *a=calloc(1,sizeof(*a));if(!a)return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q1 Source activation");
    if(!qa_strings_intern(o->replica->strings,p->provider,&a->provider,e)){free(a);return false;}
    a->generation=p->owner_generation;a->next=o->activations;o->activations=a;*out=a;return true;
}
static bool group(frontend_unified_q1 *o,const char *content,q1_activation *owner,q1_group **out,qa_error *e)
{
    qa_string_id name=qa_strings_find(o->replica->strings,(qa_bytes){(const uint8_t *)content,strlen(content)});
    for(q1_group *g=o->groups;g;g=g->next) if(g->activation==owner && g->content_name==name) {*out=g;return true;}
    q1_group *g=calloc(1,sizeof(*g));qa_font_library *fonts;qa_vfs *files;
    if(!g) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q1 CLIENT content effects");
    if (!qa_strings_intern_cstr(o->replica->strings,content,&g->content_name,e)) {free(g);return false;}
    g->content=qa_strings_cstr(o->replica->strings,g->content_name);
    if(!g->content || !qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),content,&files,&g->product,e) || g->product->family!=QA_GAME_Q1 ||
        !frontend_unified_media_bank(o->media,g->content_name,&g->images,&g->materials,&fonts,&g->sounds,e)) {free(g);return false;}
    for (size_t i=0;i<4;++i) if (!qa_strings_intern_cstr(o->replica->strings,frontend_fx_q1_beam_model(q1_beam_types[i]),&g->beam_models[i],e)) {free(g);return false;}
    g->parent=o;g->files=files;g->activation=owner;frontend_fx_q1_state_initialize(&g->effects,Q1_LIGHTS,Q1_BEAMS);q1_group **tail=&o->groups;while(*tail) tail=&(*tail)->next;*tail=g;*out=g;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    frontend_q1_help_bind_source(o->frontend->seats+domain->physical_seat,g->images);return true;
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
    qa_executable_recipe *recipe=frontend_remote_unified_recipe(o->replica);
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    return linked && domain && origin->receiver==domain->command_context.owner && origin->physical_seat==domain->physical_seat &&
        origin->recipe==recipe && origin->catalog==qa_executable_recipe_catalog(recipe) &&
        origin->product==g->product->id && origin->files==g->files;
}
static bool music_origin(q1_group *g,frontend_music_origin *out,qa_error *e)
{
    frontend_unified_q1 *o=g->parent;qa_executable_recipe *recipe=frontend_remote_unified_recipe(o->replica);
    qa_vfs *files=NULL;const qa_product *product=NULL;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    if(!domain || !domain->command_context.owner ||
        !qa_executable_recipe_content_read(recipe,g->content,&files,&product) || product!=g->product)
        return fail(e,"Q1 music lost its actual received content declaration");
    *out=(frontend_music_origin){.kind=FRONTEND_MUSIC_REMOTE,.receiver=domain->command_context.owner,.physical_seat=domain->physical_seat,
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
static bool retain_row(frontend_unified_q1 *o,const qa_unified_presentation_event *row,
    q1_continuation *out,qa_error *e)
{
    if(!qa_unified_document_retain(frontend_unified_events_document(o->events),&out->document,e))return false;
    out->row=row;return true;
}
static qa_scene_image_options model_options(void)
{ return (qa_scene_image_options){.family=QA_GAME_Q1,.usage=QA_IMAGE_USAGE_SKIN,.wrap=QA_SCENE_REPEAT,.filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=255}; }
static bool effect_sound(frontend_unified_q1 *o,const q1_event *p,const char *path,qa_error *e)
{
    if(!o->events)return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 effect sound lost its actual CLIENT event/audio owner");
    return frontend_unified_events_sound_path(o->events,(char *)p->content.data,path,
        (qa_actor_id){0},p->origin,p->seconds*1000,0,1,1,0,e);
}
static bool pose_angles(frontend_unified_q1 *o,qa_actor_id actor,qa_vec3 *angles,bool *found,qa_error *e)
{
    *found=false;
    const qa_unified_document *frame=frontend_remote_unified_frame(o->replica);
    if(!frame)return true;
    const qa_unified_frame *received=qa_unified_document_frame(frame);
    if (!received || !received->visuals) return fail(e,"Q1 muzzle pose lost its actual typed frame");
    for (size_t i=0;i<received->visuals->model_count;++i) {
        const qa_unified_model_state *model=received->visuals->models+i;
        if (same_wire(actor,model->actor)) {*found=true;*angles=model->angles;return true;}
    }
    (void)e;
    return true;
}
static bool effect(frontend_unified_q1 *o,q1_group *g,const q1_event *p,qa_error *e)
{
    double time=p->seconds;
    if(p->effect==Q1_FX_MUZZLE) {
        qa_vec3 axes[3],angles=p->angles,origin=p->muzzle?p->end:qa_vec_add(p->origin,qa_v3(0,0,16));
        bool direction=p->muzzle;
        if(!p->muzzle && p->actor_present && !pose_angles(o,p->actor,&angles,&direction,e))return false;
        if(direction){frontend_camera_axes(angles,axes);origin=qa_vec_add(origin,qa_vec_scale(axes[0],18));}
        frontend_fx_q1_light_recipe recipe={.radius=200+(float)(qa_builtin_random_integer(&o->random)&31),
            .duration=.1,.minimum=32,.color={1,1,1}};
        frontend_fx_q1_state_light(&g->effects,(qa_actor_id){0},0,origin,time,&recipe);
        return true;
    }
    if(p->effect==Q1_FX_PICKUP) return true;
    if(p->effect==Q1_FX_BLOOD) {
        frontend_fx_q1_particle_event(&g->effects.particles,&o->random,p->origin,qa_v3(0,0,0),73,(int32_t)p->a,time);
        return true;
    }
    qa_q1_temp temporary={.kind=QA_Q1_TEMP_POINT,.type=p->index,.count=1,
        .origin={p->origin.x,p->origin.y,p->origin.z}};
    const char *path;
    if(!frontend_fx_q1_state_temporary(&g->effects,&o->random,&temporary,
            (qa_actor_id){0},false,false,time,&path))
        return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 effect recipe is not installed");
    return !path || effect_sound(o,p,path,e);
}
static bool hud_read(void *context,const qa_hud_frame *frame,qa_hud_data *out,qa_error *e)
{
    frontend_unified_q1 *o=context;const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if(!o->busy || !d || frame->seat!=d->physical_seat) return fail(e,"Q1 HUD changed its actual CLIENT seat");
    size_t count=0;for(size_t i=0;i<Q1_POWERS;++i) if(o->powers[i]>o->seconds) o->timers[count++]=(qa_hud_timer){.label=powers[i],.until_ns=ns(o->powers[i])};
    size_t bars=0;if(o->ctf_present){memcpy(o->display_bars,o->ctf,sizeof(o->ctf));bars=4;}
    const qa_unified_q1_world_state *world=frontend_unified_q1_world_read(o->replica);
    const qa_recipe_provider *hud=frontend_remote_unified_provider(o->replica,QA_ROLE_HUD,"");
    const qa_product *product=hud?qa_catalog_product(qa_executable_recipe_catalog(
        frontend_remote_unified_recipe(o->replica)),hud->selection.product):NULL;
    bool stock=product && product->family==QA_GAME_Q1;
    if(!stock && (world || o->monsters_present))o->display_bars[bars++]=(qa_hud_value){.label="Monsters",
        .value=world?world->killed_monsters:o->monsters,.maximum=world?world->total_monsters:o->total_monsters};
    if(!stock && (world || o->secrets_present))o->display_bars[bars++]=(qa_hud_value){.label="Secrets",
        .value=world?world->found_secrets:o->secrets,.maximum=world?world->total_secrets:o->total_secrets};
    *out=(qa_hud_data){.source_vitals=true,.bars=o->display_bars,.bar_count=bars,.timers=o->timers,.timer_count=count,.scores=o->scores,.score_count=o->score_count,
        .help_title=o->prompt_title,.help_lines=(const char *const *)o->prompt_lines,.help_count=o->prompt_count};return true;
}
bool frontend_unified_q1_create(qa_frontend *f,frontend_remote_unified *r,frontend_unified_media *m,const frontend_unified_q1_options *options,frontend_unified_q1 **out,qa_error *e)
{
    if(!f || !r || !m || !options || !options->audio_owner || options->audio_owner==QA_AUDIO_NO_OWNER || !options->audio_actor || !out || *out) return fail(e,"Q1 CLIENT child requires its actual retained replica/media/audio route");
    frontend_unified_q1 *o=calloc(1,sizeof(*o));if(!o) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Allocating Q1 CLIENT presentation");
    o->frontend=f;o->replica=r;o->media=m;o->options=*options;o->epoch=frontend_remote_unified_epoch(r);
    qa_builtin_random_seed(&o->random,1);
    o->trail_capacity=qa_actors_capacity(frontend_remote_unified_registry(r));
    size_t labels=Q1_PROMPT_CHOICES*sizeof(*o->prompt_lines);
    if(o->trail_capacity>(SIZE_MAX-labels)/sizeof(*o->trails)){
        free(o);return frontend_unified_fail(e,QA_ERROR_MEMORY,"Q1 semantic storage exceeds addressable capacity");
    }
    qa_arena_init(&o->semantic_storage,0);
    bool ok=qa_arena_reserve(&o->semantic_storage,labels+o->trail_capacity*sizeof(*o->trails),e);
    if(ok){
        o->prompt_lines=qa_arena_alloc(&o->semantic_storage,labels,_Alignof(const char *),e);
        o->trails=qa_arena_alloc(&o->semantic_storage,o->trail_capacity*sizeof(*o->trails),_Alignof(q1_entity_trail),e);
        ok=o->prompt_lines && o->trails;
    }
    if(ok){memset(o->trails,0,o->trail_capacity*sizeof(*o->trails));qa_arena_seal(&o->semantic_storage);}
    o->localizations=ok?qa_localization_pool_create(e):NULL;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(r);
    ok=ok && o->localizations && d && (f->source_restoring?checkpoint_current(o,e):frontend_unified_q1_current(o));
    if(ok)o->hud=f->seats[d->physical_seat].hud;
    if(!ok){qa_localization_pool_destroy(o->localizations);qa_arena_destroy(&o->semantic_storage);free(o);return false;}
    o->ctf[0].label="Red";o->ctf[1].label="Blue";o->ctf[2].label="Flags";o->ctf[3].label="Runes";*out=o;return true;
}
bool frontend_unified_q1_events(frontend_unified_q1 *o,frontend_unified_events *events,qa_error *e)
{
    if(!o || !events || !frontend_unified_q1_idle(o) || !frontend_unified_q1_current(o) ||
        frontend_unified_events_audio_owner(events)!=o->options.audio_owner || (o->events && o->events!=events))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 CLIENT event route changed its actual private audio owner");
    o->events=events;return true;
}
bool frontend_unified_q1_presentation_validate(frontend_unified_q1 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    if(!frontend_unified_q1_current(o) || !row) return fail(e,"Q1 validation lost its real CLIENT recipe");
    q1_event p={0};bool ok=parse(o,row,&p,e);
    if(ok && (p.kind==Q1_ACHIEVEMENT || p.kind==Q1_COMPLETED)){
        bool local;qa_player_progress *store;const char *value;uint32_t seat;
        ok=progress_target(o,&p,&local,&store,&value,&seat,e);
    }
    return ok;
}
bool frontend_unified_q1_simulation_validate(frontend_unified_q1 *o,const qa_unified_simulation_event *row,qa_error *e)
{ (void)o;(void)row;return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 child has no separate native simulation message consumer"); }
static void prompt_clear(frontend_unified_q1 *o)
{ o->prompt_title=NULL;o->prompt_count=0;continuation_clear(&o->prompt);o->prompt_activation=NULL; }
static void group_free(q1_group *g)
{
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(g->parent->replica);
    frontend_q1_help_forget_source(g->parent->frontend->seats+domain->physical_seat,g->images);
    while(g->ambient){q1_ambient *a=g->ambient;g->ambient=a->next;
        if(a->mixer)qa_audio_mixer_remove_static(a->mixer,a->identity);
        qa_audio_asset_release(a->asset);free(a);}
    while(g->statics){q1_static *s=g->statics;g->statics=s->next;free(s);}
    for(size_t i=0;i<6;++i)qa_scene_image_release(g->sky[i]);
    qa_scene_image_release(g->particle_image);qa_localization_release(g->localization);free(g);
}
static bool retirement_parse(const qa_unified_presentation_event *row,q1_event *p,bool *retired,qa_error *e)
{
    if (!row || row->payload.kind!=QA_UNIFIED_PRESENTATION_OWNER) return fail(e,"Q1 retirement has no typed owner record");
    const qa_unified_owner_event *v=&row->payload.value.owner;
    *retired=v->kind==QA_UNIFIED_OWNER_RETIRED;p->seconds=row->seconds;p->sequence=row->sequence;
    return owner_parse(&v->owner,p,e) && p->provider.size>0;
}
bool frontend_unified_q1_owner_validate(frontend_unified_q1 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    if(!frontend_unified_q1_current(o) || !row)return fail(e,"Q1 Source retirement lost its retained replica");
    q1_event p={0};bool retired=false;bool ok=retirement_parse(row,&p,&retired,e);return ok;
}
bool frontend_unified_q1_owner_retire(frontend_unified_q1 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    if(!o || !frontend_unified_q1_idle(o) || !mutable(o,e))return false;
    q1_event p={0};bool retired=false;q1_activation *owner=NULL;
    bool ok=retirement_parse(row,&p,&retired,e) && activation(o,&p,&owner,e);
    if(!ok)return false;
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
    if(o->weapon_activation==owner){continuation_clear(&o->weapon);o->weapon_activation=NULL;}
    if(o->prompt_activation==owner)prompt_clear(o);
    for(size_t i=0;i<Q1_POWERS;++i)if(o->power_activations[i]==owner){o->powers[i]=0;o->power_activations[i]=NULL;}
    if(o->ctf_activation==owner){o->ctf_present=false;o->capture_until=0;o->ctf_activation=NULL;}
    if(o->fog_activation==owner){o->fog.active=false;o->fog_activation=NULL;}
    if(o->intermission_activation==owner){o->intermission=false;o->intermission_activation=NULL;}
    if(o->finale_activation==owner){continuation_clear(&o->finale);
        qa_scene_image_release(o->finale_image);o->finale_image=NULL;o->finale_activation=NULL;qa_hud_clear_center(o->hud,NULL);}
    owner->retired=true;return mutable(o,e);
}
static bool prompt_set(frontend_unified_q1 *o,const q1_event *p,qa_error *e)
{
    const qa_builtin_event *value=&p->row->payload.value.builtin;
    q1_continuation next={0};
    if(value->prompt_choice_count>Q1_PROMPT_CHOICES || !retain_row(o,p->row,&next,e))return false;
    prompt_clear(o);o->prompt=next;o->prompt_title=(const char *)p->text.data;
    o->prompt_count=value->prompt_choice_count;
    for(size_t i=0;i<o->prompt_count;++i){
        const char *label=qa_strings_cstr(o->replica->strings,value->prompt_choices[i].label);
        o->prompt_lines[i]=label?label:"";
    }
    return true;
}
static bool localize_piece(q1_group *g,const qa_builtin_event *value,qa_buffer *out,qa_error *e)
{
    size_t count=value->argument_count; if (count>8)return fail(e,"Q1 message exceeds its Source arguments");
    const char *base=qa_strings_cstr(g->parent->replica->strings,value->text); if (!base) base=""; const char *argv[8]={0}; char numeric[8][32];
    for (size_t i=0;i<count;++i) {
        const qa_builtin_message_arg *arg=value->arguments+i;
        if (arg->kind==QA_BUILTIN_MESSAGE_STRING) argv[i]=qa_strings_cstr(g->parent->replica->strings,arg->value.text);
        else {if(!qa_format_number(arg->value.number,numeric[i],e))return false;argv[i]=numeric[i];}
    }
    out->data=(uint8_t *)g->parent->localized_text;
    if (g->product->edition!=QA_EDITION_RERELEASE && (base[0]!='$' || !qa_localization_find(g->localization,base+1))) {
        if (!qa_q1_classic_text(base,argv,count,(char *)out->data,65536,e))return false;
        out->size=strlen((char *)out->data);
    } else out->size=qa_localize_presentation(g->localization,base,argv,count,true,(char *)out->data,65536);
    return true;
}
static bool localized(frontend_unified_q1 *o,q1_group *g,const q1_event *p,qa_buffer *out,qa_error *e)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);const char *language;
    if(!qa_ui_language_read(d->cvars,d->physical_seat,&language,e)) return false;
    if(!g->localization || strcmp(g->language,language)) {qa_vfs *files;const qa_product *product;qa_localization *catalog=NULL;
        if(strlen(language)>=sizeof(g->language) || !qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),g->content,&files,&product,e) ||
            !qa_localization_acquire(o->localizations,files,language,&(qa_localization_options){.profile=QA_LOCALIZATION_Q1_RERELEASE},&catalog,e)) return false;
        qa_localization_release(g->localization);g->localization=catalog;strcpy(g->language,language);}
    return localize_piece(g,&p->row->payload.value.builtin,out,e);
}
bool frontend_unified_q1_presentation(frontend_unified_q1 *o,const qa_unified_presentation_event *row,bool *mirrored,qa_error *e)
{
    if(!o || !mirrored || o->busy || o->prepared || !mutable(o,e)) return false;
    *mirrored=false;
    q1_event p={0};q1_group *g=NULL;q1_activation *owner=NULL;
    bool ok=parse(o,row,&p,e) && activation(o,&p,&owner,e);if(!ok) {return false;}
    if(owner && owner->retired){return true;}
    if(p.kind==Q1_COMPLETED){
        ok=progress_record(o,&p,e);
        if(ok){if(!o->intermission)o->completed_seconds=p.seconds;
            o->intermission=true;o->intermission_activation=owner;}
        return ok && mutable(o,e);
    }
    if(p.kind==Q1_SELL_SCREEN){const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
        ok=group(o,(char *)p.content.data,owner,&g,e);
        if(ok){frontend_q1_help_bind_source(o->frontend->seats+d->physical_seat,g->images);
            ok=frontend_remote_unified_command_text(o->replica,"help\n",e);}
        return ok && mutable(o,e);}
    bool persistent=p.kind==Q1_BEAM || p.kind==Q1_STYLE || p.kind==Q1_STATIC || p.kind==Q1_AMBIENT || p.kind==Q1_SKY ||
        p.kind==Q1_MUSIC || p.kind==Q1_PAUSE || p.kind==Q1_FINALE;
    if(!group(o,(char *)p.content.data,persistent?owner:NULL,&g,e)){return false;}
    bool local=true;
    if(p.kind==Q1_WEAPON || p.kind==Q1_POWER || p.kind==Q1_MESSAGE || p.kind==Q1_CTF_STATUS || p.kind==Q1_PROMPT || p.kind==Q1_CLEAR_PROMPT || p.kind==Q1_LOG || p.kind==Q1_ACHIEVEMENT || p.kind==Q1_FOG || p.kind==Q1_FOUND) {
        local=owns(o,&p,e);if(!local && e && e->code!=QA_OK) {return false;}}
    if(!local) {return true;}
    switch(p.kind) {
    case Q1_PARTICLES:frontend_fx_q1_particle_event(&g->effects.particles,&o->random,p.origin,p.end,(int32_t)p.a,(int32_t)p.b,p.seconds);break;
    case Q1_EFFECT:case Q1_COLORS: {
        size_t original_count=g->effects.particles.count;qa_builtin_random original_random=o->random;frontend_fx_q1_light original_lights[Q1_LIGHTS];
        memcpy(original_lights,g->effects.lights,sizeof(original_lights));
        if(p.kind==Q1_EFFECT)ok=effect(o,g,&p,e);
        else {qa_q1_temp temporary={.kind=QA_Q1_TEMP_COLORS,.origin={p.origin.x,p.origin.y,p.origin.z},
                .color_start=(uint8_t)p.a,.color_length=(uint8_t)p.b};
            const char *path=NULL;
            ok=frontend_fx_q1_state_temporary(&g->effects,&o->random,&temporary,
                (qa_actor_id){0},false,false,p.seconds,&path);
            if(ok && path)ok=effect_sound(o,&p,path,e);}
        if(!ok){g->effects.particles.count=original_count;o->random=original_random;memcpy(g->effects.lights,original_lights,sizeof(original_lights));}
        if(ok && p.kind==Q1_EFFECT && p.effect==Q1_FX_PICKUP && owns(o,&p,e))o->bonus_until=p.seconds+.5;
        break;}
    case Q1_BEAM: {
        qa_q1_temp beam={.kind=QA_Q1_TEMP_BEAM,.type=q1_beam_types[p.index],
            .origin={p.origin.x,p.origin.y,p.origin.z},.end={p.end.x,p.end.y,p.end.z}};
        (void)frontend_fx_q1_state_beam(&g->effects,p.actor,false,&beam,p.seconds,
            qa_builtin_random_integer(&o->random));
        break;}
    case Q1_STYLE:g->styles[(uint32_t)p.a]=(char *)p.text.data;g->style_sequences[(uint32_t)p.a]=p.sequence;p.text=(qa_bytes){0};break;
    case Q1_STATIC: {if(!p.text.size)break;
        q1_static *s=calloc(1,sizeof(*s));qa_scene_image_options images=model_options();
        qa_string_id path;
        ok=s && qa_strings_intern(o->replica->strings,p.text,&path,e) && frontend_unified_media_model(o->media,g->content_name,path,QA_GAME_Q1,&images,&s->model,e);
        if(ok) {s->path=(char *)p.text.data;p.text=(qa_bytes){0};s->origin=p.origin;s->angles=p.angles;s->frame=(uint32_t)p.a;s->skin=(uint32_t)p.b;
            q1_static **tail=&g->statics;while(*tail) tail=&(*tail)->next;*tail=s;}else free(s);break;}
    case Q1_WEAPON: {q1_continuation next={0};ok=retain_row(o,row,&next,e) &&
        qa_strings_intern(o->replica->strings,p.content,&next.content,e) && qa_strings_intern(o->replica->strings,p.text,&next.path,e);if(ok) {continuation_replace(&o->weapon,&next);o->weapon_activation=owner;}else continuation_clear(&next);break;}
    case Q1_POWER:o->powers[p.index]=p.a;o->power_activations[p.index]=owner;break;
    case Q1_MESSAGE: {qa_buffer message={0};ok=localized(o,g,&p,&message,e);
        if(ok) ok=p.flag?qa_hud_center_print(o->hud,(char *)message.data,ns(p.seconds),UINT64_C(3000000000),(qa_hud_center_policy){.instant=true,.character_ns=0,.columns=40},e):qa_hud_notify(o->hud,(char *)message.data,false,ns(p.seconds),UINT64_C(3000000000),e);
        if(ok && !p.flag) {message.data[message.size]='\n';message.data[message.size+1]=0;
            const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);qa_console_emit(domain->console,&domain->command_context,(char *)message.data);}
        if(ok) *mirrored=true;
        break;}
    case Q1_STOP: {qa_actor_id actor;ok=frontend_remote_unified_source_actor(o->replica,qa_unified_document_frame(frontend_remote_unified_frame(o->replica)),p.actor,false,&actor,e);
        if(ok && o->frontend->audio) {uint64_t audio;ok=o->options.audio_actor(o->options.context,actor,&audio,e);
            if(ok) qa_audio_engine_stop_channel(o->frontend->audio,audio,o->options.audio_owner,QA_GAME_Q1,(int32_t)p.a); }break;}
    case Q1_SOUND:ok=frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q1 sound must be paired with the real declared simulation sound route");break;
    case Q1_AMBIENT: {q1_ambient *a=calloc(1,sizeof(*a));ok=a && qa_audio_bank_register(g->sounds,(char *)p.text.data,QA_GAME_Q1,&a->asset,e);
        if(ok && a->asset && qa_audio_asset_sample(a->asset)->loop_start!=QA_AUDIO_NO_LOOP) {a->path=(char *)p.text.data;p.text=(qa_bytes){0};a->origin=p.origin;a->volume=(float)p.a;a->attenuation=(float)p.b;a->identity=qa_scene_identity();
            q1_ambient **tail=&g->ambient;while(*tail) tail=&(*tail)->next;*tail=a;}
        else {if(a) qa_audio_asset_release(a->asset);free(a);}break;}
    case Q1_CTF_STATUS:o->ctf[0].value=p.a;o->ctf[1].value=p.b;o->ctf[2].value=p.c;o->ctf[3].value=p.d;o->ctf_present=true;o->ctf_activation=owner;break;
    case Q1_CTF_CAPTURE:o->ctf[p.index].value=p.a;o->ctf_present=true;o->capture_until=p.seconds+3;o->ctf_activation=owner;break;
    case Q1_PROMPT:ok=prompt_set(o,&p,e);if(ok)o->prompt_activation=owner;break;
    case Q1_CLEAR_PROMPT:prompt_clear(o);break;
    case Q1_LOG:ok=qa_hud_notify(o->hud,(char *)p.text.data,false,ns(p.seconds),UINT64_C(3000000000),e);break;
    case Q1_ACHIEVEMENT:ok=progress_record(o,&p,e);
        if(ok && p.text.size)ok=qa_hud_notify(o->hud,(char *)p.text.data,false,ns(p.seconds),UINT64_C(3000000000),e);
        break;
    case Q1_COMPLETED:case Q1_SELL_SCREEN:break;
    case Q1_TOTAL:o->total_monsters=p.a;o->monsters_present=true;break;
    case Q1_FOUND:
        if(p.flag){o->secrets=p.b;o->total_secrets=p.a;o->secrets_present=true;}
        else {o->monsters=p.b;o->total_monsters=p.a;o->monsters_present=true;}
        break;
    case Q1_FOG:o->fog.previous=p.origin;o->fog.target=p.end;o->fog.previous_density=p.a;o->fog.target_density=p.b;
        o->fog.start=p.c;o->fog.duration=p.d;o->fog.sky_factor=p.sky_factor;o->fog.active=true;o->fog_activation=owner;break;
    case Q1_FINALE: {
        o->intermission=false;o->intermission_activation=NULL;
        if(p.a>4)break;
        q1_continuation next={0};qa_scene_image *image=NULL;qa_buffer text={0};
        qa_scene_image_options options={.family=QA_GAME_Q1,.usage=QA_IMAGE_USAGE_PICTURE,.wrap=QA_SCENE_CLAMP,.filter=QA_SCENE_NEAREST,.transparent=true,.transparent_index=255};
        ok=retain_row(o,row,&next,e) && localized(o,g,&p,&text,e) && qa_scene_image_load_exact(g->images,"gfx/finale.lmp",&options,&image,e);
        if(ok && !image)ok=fail(e,"Q1 finale picture is absent from its actual Source content");
        if(ok)ok=qa_hud_clear_center(o->hud,e) && qa_hud_center_print(o->hud,(char *)text.data,ns(p.seconds),UINT64_MAX,(qa_hud_center_policy){.instant=false,.character_ns=UINT64_C(125000000),.initial_characters=1,.columns=40},e);
        if(ok){continuation_replace(&o->finale,&next);qa_scene_image_release(o->finale_image);o->finale_image=image;image=NULL;
            o->finale_banner=p.a>=4;o->finale_activation=owner;}
        qa_scene_image_release(image);continuation_clear(&next);break;
    }
    case Q1_ACTION:ok=fail(e,"Q1 action is not a native emitted presentation");break;
    case Q1_MUSIC:ok=music_play(g,p.a,e);break;
    case Q1_PAUSE:if(o->frontend->audio){
        ok=frontend_music_sources_received_pause(o->frontend->music_sources,p.flag,e);
        if(ok)o->pause_activation=p.flag?owner:NULL;
        }break;
    case Q1_SKY: {qa_scene_image *images[6]={0};bool found=false;static const char *const suffixes[]={"rt","bk","lf","ft","up","dn"};qa_scene_image_options options={.family=QA_GAME_Q1,.usage=QA_IMAGE_USAGE_SKY,.wrap=QA_SCENE_CLAMP,.filter=QA_SCENE_LINEAR};
        for(size_t i=0;ok && p.text.size && i<6;++i){size_t n=p.text.size+32;char *path=malloc(n);if(!path){ok=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q1 received sky path");break;}
            snprintf(path,n,"gfx/env/%s%s.tga",(char *)p.text.data,suffixes[i]);ok=qa_scene_image_load_exact(g->images,path,&options,images+i,e);
            if(ok && !images[i]){snprintf(path,n,"gfx/env/%s%s.png",(char *)p.text.data,suffixes[i]);ok=qa_scene_image_load_exact(g->images,path,&options,images+i,e);}free(path);
            if(ok){found|=images[i]!=NULL;if(!images[i]){images[i]=(qa_scene_image *)qa_scene_missing(g->images);qa_scene_image_retain(images[i]);}}}
        if(ok){for(size_t i=0;i<6;++i){qa_scene_image_release(g->sky[i]);g->sky[i]=images[i];images[i]=NULL;}g->sky_name=(char *)p.text.data;p.text=(qa_bytes){0};g->sky_found=found;g->sky_sequence=p.sequence;}
        for(size_t i=0;i<6;++i)qa_scene_image_release(images[i]);
        break;}
    }
    return ok && mutable(o,e);
}
bool frontend_unified_q1_sound_presentation(frontend_unified_q1 *o,const qa_unified_presentation_event *row,bool simulation_owned,qa_error *e)
{
    if(!o || o->busy || o->prepared || !mutable(o,e)) return false;
    q1_event p={0};bool ok=parse(o,row,&p,e);q1_group *g=NULL;
    if(ok && p.kind!=Q1_SOUND) ok=fail(e,"Q1 sound pairing received a different Source event");
    if(ok && !simulation_owned) {
        ok=group(o,(char *)p.content.data,NULL,&g,e);qa_audio_asset *asset=NULL;qa_actor_id actor;uint64_t audio;
        if(ok) ok=frontend_remote_unified_source_actor(o->replica,qa_unified_document_frame(frontend_remote_unified_frame(o->replica)),p.actor,false,&actor,e) &&
            o->options.audio_actor(o->options.context,actor,&audio,e) && qa_audio_bank_register(g->sounds,(char *)p.text.data,QA_GAME_Q1,&asset,e);
        int32_t c=(int32_t)p.c;
        if(ok && asset && o->frontend->audio) {
            const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
            qa_audio_play play={.sample=qa_audio_asset_sample(asset),.asset=asset,.resource_id=qa_resource_id(qa_audio_asset_resource(asset)),
                .name=(char *)p.text.data,.family=QA_GAME_Q1,.actor=audio,.owner=o->options.audio_owner,.audience=domain->physical_seat,
                .origin_kind=p.flag?QA_AUDIO_FIXED:QA_AUDIO_ACTOR,.origin=p.origin,.channel=c,.volume=(float)p.a,.attenuation=(float)p.b,
                .server_milliseconds=p.seconds*1000,.has_server_time=true};
            int32_t signed_tick;
            ok=qa_audio_source_milliseconds(play.server_milliseconds,&signed_tick,e) &&
                qa_audio_engine_play(o->frontend->audio,&play,signed_tick,e);
        }
        qa_audio_asset_release(asset);
    }
    return ok && mutable(o,e);
}
bool frontend_unified_q1_frame_prepare(frontend_unified_q1 *o,const qa_unified_document *d,qa_error *e)
{
    if(!o || o->busy || o->prepared || !d || qa_unified_document_type(d)!=QA_UNIFIED_FRAME_DOCUMENT || !mutable(o,e)) return false;
    const qa_unified_frame *frame=qa_unified_document_frame(d);
    if(!frame || !frame->world || frame->epoch!=o->epoch) return fail(e,"Q1 received frame lost its typed Source owner");
    uint64_t n=frame->world->source.number;
    double seconds=frame->world->presentation_seconds;
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
        double elapsed=g->has_sample?fmax(0,o->prepared_seconds-g->sampled):0;
        frontend_fx_q1_advance(&g->effects.particles,o->prepared_seconds,elapsed,800);
        g->sampled=o->prepared_seconds;g->has_sample=true;
    }
    o->frame=o->prepared_frame;o->seconds=o->prepared_seconds;o->has_frame=true;o->prepared=false;o->preparing_document=NULL;
}
void frontend_unified_q1_frame_abort(frontend_unified_q1 *o)
{if(o && !o->busy){o->prepared=false;o->preparing_document=NULL;}}
bool frontend_unified_q1_entity_effects(frontend_unified_q1 *o,
    const frontend_unified_render_entity_effects *entity,double seconds,qa_error *e)
{
    uint32_t flags=entity->model && entity->model->format==QA_MODEL_MDL ? (uint32_t)entity->model->flags:0;
    uint32_t effects=entity->q1_effects | (entity->family==QA_GAME_Q1 ? (uint32_t)entity->effects:0);
    if (!(effects&UINT32_C(0xff)) && !(flags&UINT32_C(0xf7))) return true;
    if (!o || !mutable(o,e)) return false;
    const qa_product *product=entity->product;
    if (product->family!=QA_GAME_Q1) {
        const qa_recipe_provider *source=frontend_remote_unified_provider(o->replica,QA_ROLE_ENTITIES,"");
        product=source?qa_catalog_product(qa_executable_recipe_catalog(
            frontend_remote_unified_recipe(o->replica)),source->selection.product):NULL;
        if (!product || product->family!=QA_GAME_Q1) return true;
    }
    q1_group *g;
    if (!group(o,product->identity,NULL,&g,e)) return false;
    size_t slot=entity->actor.slot;
    if(slot>=o->trail_capacity)return fail(e,"Q1 trail actor lies outside its loaded registry");
    q1_entity_trail *trail=o->trails+slot;
    bool actor_same=trail->present && qa_actor_id_equal(trail->actor,entity->actor);
    if (actor_same && trail->frame==o->frontend->frame_number) return true;
    bool same=actor_same && trail->model==entity->model && trail->product==product;
    qa_vec3 start=same?trail->origin:entity->origin;
    qa_vec3 delta=qa_vec_sub(entity->origin,start);
    if (fabsf(delta.x)>100 || fabsf(delta.y)>100 || fabsf(delta.z)>100) start=entity->origin;
    *trail=(q1_entity_trail){.actor=entity->actor,.model=entity->model,.product=product,.origin=entity->origin,
        .frame=o->frontend->frame_number,.present=true};
    frontend_fx_q1_light_recipe recipe;qa_vec3 origin;
    if (frontend_fx_q1_entity_effects(&g->effects.particles,&o->random,start,entity->origin,entity->angles,
        effects,flags,false,g->product->edition==QA_EDITION_RERELEASE,seconds,&origin,&recipe)) {
        frontend_fx_q1_state_light(&g->effects,entity->actor,0,origin,seconds,&recipe);
    }
    return true;
}
bool frontend_unified_q1_world_input(frontend_unified_q1 *o,qa_scene_world_input *input,qa_error *e)
{
    if(!o || !input || o->busy || o->prepared || !o->has_frame || !mutable(o,e))return false;
    size_t capacity=input->light_count,groups=0;
    for (const q1_group *g=o->groups;g;g=g->next) ++groups;
    if(groups>(SIZE_MAX-capacity)/Q1_LIGHTS)return fail(e,"Q1 private scene light roster exceeds storage");
    capacity+=groups*Q1_LIGHTS;
    o->scene_lights=capacity?qa_arena_alloc(&o->frontend->frame.storage,capacity*sizeof(*o->scene_lights),_Alignof(qa_scene_light),e):NULL;
    if(capacity && !o->scene_lights)return false;
    size_t count=input->light_count;if(count)memcpy(o->scene_lights,input->lights,count*sizeof(*input->lights));
    uint64_t sequences[256]={0};bool received[256]={0};q1_group *sky=NULL;
    for(size_t i=0;i<256;++i)o->scene_styles[i]=input->q1_styles && i<input->style_count?input->q1_styles[i]:256;
    for(q1_group *g=o->groups;g;g=g->next){
        for(size_t i=0;i<Q1_LIGHTS;++i){frontend_fx_q1_light *l=g->effects.lights+i;if(l->die<=input->seconds)continue;float radius=fmaxf(0,l->radius-(float)(input->seconds-l->born)*l->decay);
            if(radius>0)o->scene_lights[count++]=(qa_scene_light){.family=QA_GAME_Q1,.origin=l->origin,.color=l->color,.radius=radius,.minimum=l->minimum,.scale=1,.additive=true,.identity=l->identity};}
        for(size_t i=0;i<256;++i)if(g->styles[i] && (!received[i] || g->style_sequences[i]>=sequences[i])){
            float value=frontend_legacy_lightstyle_sample(QA_GAME_Q1,g->styles[i],input->seconds);
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
bool frontend_unified_q1_model(frontend_unified_q1 *o,qa_actor_id actor,qa_string_id content,qa_string_id path,qa_scene_model_input *input,qa_error *e)
{
    if(!o || !content || !path || !input || !mutable(o,e))return false;
    if(!o->weapon.row || !input->view_model || input->family!=QA_GAME_Q1)return true;
    q1_event p={0};bool ok=parse(o,o->weapon.row,&p,e);qa_actor_id received;
    if(ok)ok=frontend_remote_unified_source_actor(o->replica,qa_unified_document_frame(frontend_remote_unified_frame(o->replica)),p.actor,false,&received,e);
    if(ok && qa_actor_id_equal(actor,received) && content==o->weapon.content && path==o->weapon.path)input->frame=(uint32_t)p.a;
    return ok;
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
            qa_scene_model_input input={.view=*view,.family=QA_GAME_Q1,.frame=s->frame,.old_frame=s->frame,.skin=s->skin,.color={1,1,1,1},
                .source_path=s->path,.material_library=frontend_unified_model_materials(s->model.scene),.seconds=world->seconds,.identity_light=world->identity_light,.ambient={1,1,1},
                .video_frame=world->video_frame,.video_context=world->video_context};
            transform(&input.transform,s->origin,s->angles);
            if(s->model.brush_world)ok=qa_scene_world_submit_model(s->model.brush_world,s->model.inline_model,&input.transform,world,0,input.color,frame,e);
            else {ok=qa_scene_world_sample_light_input(frontend_unified_media_world(o->media),world,s->origin,&input.ambient,&input.directed,&input.light_direction,e) &&
                frontend_legacy_model_input(frontend_unified_media_world(o->media),world,&input,e) && qa_scene_model_submit(s->model.scene,&input,frame,e);}
        }
        for(size_t i=0;ok && i<Q1_BEAMS;++i) {frontend_fx_q1_beam *b=g->effects.beams+i;if(b->die<=world->seconds)continue;qa_actor_id actual;
            ok=frontend_remote_unified_source_actor(o->replica,qa_unified_document_frame(frontend_remote_unified_frame(o->replica)),b->actor,false,&actual,e);if(!ok)break;
            frontend_unified_model model;qa_scene_image_options images=model_options();ok=frontend_unified_media_model(o->media,g->content_name,g->beam_models[frontend_fx_q1_beam_index(b->type)],QA_GAME_Q1,&images,&model,e);if(!ok)break;
            frontend_fx_q1_beam_cursor cursor;frontend_fx_q1_beam_begin(&cursor,b->start,b->end);
            qa_builtin_random roll;qa_builtin_random_seed(&roll,b->roll_seed);
            qa_model_transform placement;
            while(ok && frontend_fx_q1_beam_next(&cursor,&roll,&placement)) {
                qa_scene_model_input input={.view=*view,.transform=placement,.family=QA_GAME_Q1,
                    .source_path=frontend_fx_q1_beam_model(b->type),.material_library=frontend_unified_model_materials(model.scene),
                    .seconds=world->seconds,.identity_light=world->identity_light,.color={1,1,1,1},.ambient={1,1,1},.entity=actual.slot,
                    .video_frame=world->video_frame,.video_context=world->video_context};
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
        if(ok && g->effects.particles.count && !g->particle_image)ok=qa_scene_particle_image(g->images,QA_GAME_Q1,&g->particle_image,e);
        qa_bytes palette={0};if(ok && g->effects.particles.count)ok=qa_scene_resources_palette(g->images,QA_GAME_Q1,&palette,e) && palette.size>=768;
        qa_scene_particle_sample *particles=ok?qa_scene_particles_alloc(frame,g->effects.particles.count,e):NULL;
        if(ok && g->effects.particles.count && !particles)ok=false;
        qa_scene_particle_batch batch={.view=*view,.family=QA_GAME_Q1,.image=g->particle_image,.samples=particles};
        for(size_t i=g->effects.particles.count;ok && i>0;--i) {const qa_scene_q1_particle_state *p=g->effects.particles.values.q1+i-1;if(p->die<world->seconds)continue;
            uint32_t color=(p->color&255)*3;particles[batch.count++]=(qa_scene_particle_sample){.origin=p->origin,
                .color={palette.data[color]/255.0f,palette.data[color+1]/255.0f,palette.data[color+2]/255.0f,1}};}
        if(ok && batch.count)ok=qa_scene_particles(frame,&batch,e);
    }
    o->busy=false;return ok && mutable(o,e);
}
bool frontend_unified_q1_world_dlights(frontend_unified_q1 *o,const qa_scene_world_input *world,
    qa_scene_frame *frame,qa_vec4 *overlay,qa_error *e)
{
    if(!overlay || !world_enter(o,world,frame,e))return false;
    qa_executable_recipe *recipe=frontend_unified_media_recipe(o->media);
    const qa_recipe_choices *choices=qa_executable_recipe_choices(recipe);
    const qa_product *product=choices?qa_catalog_product(qa_executable_recipe_catalog(recipe),choices->world.presentation):NULL;
    bool ok=product!=NULL;
    if(!ok)frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q1 supplemental light lost its actual WORLD presentation");
    for(q1_group *g=product && product->family==QA_GAME_Q3?o->groups:NULL;ok && g;g=g->next) {
        qa_scene_light lights[Q1_LIGHTS];size_t count=0;
        for(size_t i=0;i<Q1_LIGHTS;++i){frontend_fx_q1_light *l=g->effects.lights+i;if(l->die<=world->seconds)continue;
            float radius=fmaxf(0,l->radius-(float)(world->seconds-l->born)*l->decay);
            if(radius>0)lights[count++]=(qa_scene_light){.family=QA_GAME_Q1,.origin=l->origin,.color=l->color,
                .radius=radius,.minimum=l->minimum,.scale=1,.additive=true,.identity=l->identity};}
        frontend_legacy_render_policy policy;qa_vec4 blend={0};
        if(count){const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
            ok=world->legacy_policy.present && world->legacy_policy.source_family==QA_GAME_Q1?
                frontend_legacy_render_policy_read_controls(domain->cvars,&o->replica->legacy_cvars,g->product,&policy,e):
                frontend_legacy_render_policy_read(o->frontend,g->product,&policy,e);
            if(ok && policy.flashblend)ok=qa_scene_legacy_dlights(frame,&world->view,QA_GAME_Q1,
                policy.quakeworld,lights,count,&blend,e);}
        if(ok && blend.w>0){float alpha=overlay->w+(1-overlay->w)*blend.w,weight=blend.w/alpha;
            *overlay=(qa_vec4){overlay->x*(1-weight)+blend.x*weight,overlay->y*(1-weight)+blend.y*weight,
                overlay->z*(1-weight)+blend.z*weight,alpha};}
    }
    o->busy=false;return ok && mutable(o,e);
}
bool frontend_unified_q1_world_blend(frontend_unified_q1 *o,const qa_scene_world_input *world,qa_vec4 overlay,qa_scene_frame *frame,qa_error *e)
{
    if(!world_enter(o,world,frame,e))return false;
    const qa_scene_view *view=&world->view;
    bool ok=true;
    if(ok && !view->mirror && o->groups && (!world->legacy_policy.present || world->legacy_policy.polyblend)){
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);qa_ui_preferences preferences;
        ok=qa_ui_preferences_read(qa_application_cvars(domain->application),qa_application_ui_preference_handles(domain->application),domain->physical_seat,&preferences,e);
        if(ok){if(preferences.reduced_flashes)overlay=(qa_vec4){0};
            float bonus=(float)fmin(50,fmax(0,(o->bonus_until-world->seconds)*100))/255;
            if(bonus>0){float alpha=overlay.w+(1-overlay.w)*bonus,weight=bonus/alpha;
                overlay=(qa_vec4){overlay.x*(1-weight)+(215.0f/255)*weight,overlay.y*(1-weight)+(186.0f/255)*weight,
                    overlay.z*(1-weight)+(69.0f/255)*weight,alpha};}
            if(overlay.w>0)ok=qa_scene_frame_picture(frame,qa_scene_white(o->groups->images),view->viewport,view->viewport,
                (qa_vec4){0,0,1,1},overlay,e);}
    }
    o->busy=false;return ok && mutable(o,e);
}
void frontend_unified_q1_hud_status(const frontend_unified_q1 *o,qa_hud_q1_status *status)
{
    status->intermission=o->intermission;
    if(o->intermission)status->seconds=o->completed_seconds;
}
bool frontend_unified_q1_hud(frontend_unified_q1 *o,qa_ui *ui,qa_scene_rect viewport,qa_scene_frame *frame,qa_error *e)
{
    const frontend_remote_unified_domain *d=o?frontend_remote_unified_domain_read(o->replica):NULL;qa_actor_id player;uint32_t source;
    if(!o || !d || ui!=o->frontend->seats[d->physical_seat].ui || !frame || o->busy || o->prepared || !mutable(o,e) ||
        !frontend_remote_unified_player(o->replica,&player,&source))return false;
    size_t count=0;for(q1_group *g=o->groups;g;g=g->next)for(size_t i=0;i<256;++i)if(g->clients[i].present)++count;
    o->scores=count?qa_arena_alloc(&frame->storage,count*sizeof(*o->scores),_Alignof(qa_hud_score),e):NULL;
    if(count && !o->scores)return false;
    o->score_count=0;
    for(q1_group *g=o->groups;g;g=g->next){bool earlier=false;
        for(q1_group *old=o->groups;old!=g;old=old->next)if(old->content_name==g->content_name){earlier=true;break;}
        if(earlier)continue;
        for(size_t i=0;i<256;++i){bool present=false,has_name=false,has_score=false,has_ping=false;
            const char *name="";double score=0,ping=0;uint64_t name_sequence=0,score_sequence=0,ping_sequence=0;
            for(q1_group *received=g;received;received=received->next)if(received->content_name==g->content_name && received->clients[i].present){
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
    if(o->finale.row && o->finale_banner && o->finale_image){float scale=fminf((float)viewport.width/320,(float)viewport.height/200);
        uint32_t width=(uint32_t)((float)o->finale_image->logical_width*scale),height=(uint32_t)((float)o->finale_image->logical_height*scale);
        qa_scene_rect rectangle={.x=viewport.x+(int32_t)(((int64_t)viewport.width-width)/2),.y=viewport.y+(int32_t)(16*scale),.width=width,.height=height};
        ok=qa_scene_frame_picture(frame,o->finale_image,rectangle,viewport,(qa_vec4){0,0,1,1},(qa_vec4){1,1,1,1},e);}
    if(ok)ok=qa_hud_draw_content(o->hud,&(qa_hud_options){.ui=ui,.application=d->application,
        .seat=d->physical_seat,.context=o,.read=hud_read},&(qa_hud_frame){.seat=d->physical_seat,.actor=player,.time_ns=ns(o->seconds),
        .viewport=viewport,.safe_area=viewport,.scale=1,.visible=true,.show_scores=qa_input_seat_action_active(o->frontend->seats[d->physical_seat].input,QA_INPUT_SCORES)},frame,e);
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
    o->hud=NULL;
    while(o->groups){q1_group *g=o->groups;o->groups=g->next;group_free(g);}
    while(o->activations){q1_activation *a=o->activations;o->activations=a->next;free(a);}
    prompt_clear(o);continuation_clear(&o->weapon);continuation_clear(&o->finale);qa_scene_image_release(o->finale_image);
    qa_localization_pool_destroy(o->localizations);qa_arena_destroy(&o->semantic_storage);free(o);*slot=NULL;return true;
}
