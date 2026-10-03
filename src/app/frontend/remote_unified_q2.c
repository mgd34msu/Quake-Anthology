#include "remote_unified_private.h"
#include "remote_unified_q2.h"
#include "remote_unified_q2_hud.h"
#include "remote_unified_presentation.h"
#include "remote_unified_save.h"
#include "remote_q2_footsteps.h"
#include "received_music.h"
#include "material_movies.h"
#include "legacy_render_policy.h"
#include "shared_register.h"
#include "qa/hud_q2.h"
#include "qa/caption_save.h"
#include "qa/ui_preferences.h"
#include "qa/text.h"
#include "qa/player_progress.h"
#include "qa/console_cvar_observer.h"
#include <float.h>
#include <math.h>

typedef struct q2_model {
    struct q2_model *next;
    char *path;
    qa_scene_model *scene;
} q2_model;
typedef struct q2_alias { uint32_t number; qa_actor_id actor; } q2_alias;
typedef struct q2_muzzle_receipt {
    qa_actor_id actor;
    int32_t number, flash;
    double milliseconds;
    bool monster, consumed;
} q2_muzzle_receipt;
typedef struct q2_activation {
    struct q2_activation *next;
    char *provider;
    uint64_t generation;
    bool retired;
} q2_activation;
typedef struct q2_visual {
    struct q2_visual *next;
    qa_actor_id actor;
    q2_activation *activation;
    char *content, *path, *source_provider;
    qa_unified_document *model;
    uint64_t effects;
    uint32_t event;
    uint64_t event_frame;
    bool visible;
} q2_visual;
typedef struct q2_loop {
    struct q2_loop *next;
    qa_actor_id actor;
    q2_activation *activation;
} q2_loop;
typedef struct q2_inventory_row {
    char *item,*label;
    double count;
    bool selected;
} q2_inventory_row;
typedef struct q2_player_name {
    struct q2_player_name *next;
    uint32_t slot;
    char *name;
} q2_player_name;
typedef struct q2_bank {
    struct q2_bank *next;
    struct frontend_unified_q2 *owner;
    char *content;
    qa_vfs *files;
    const qa_product *product;
    qa_scene_resources *images;
    qa_material_library *materials;
    frontend_remote_q2_effects *effects;
    frontend_q2_footsteps *footsteps;
    frontend_received_music *music;
    qa_audio_bank *sounds;
    q2_activation *activation;
    frontend_remote_q2_effects_profile profile;
    double frame_milliseconds;
    char *source_provider;
    q2_model *models;
    q2_alias *aliases;
    size_t alias_count;
    char *styles[256];
    uint64_t style_sequences[256];
} q2_bank;
struct frontend_unified_q2 {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    frontend_unified_events *events;
    q2_bank *banks;
    q2_activation *activations;
    q2_visual *visuals;
    q2_loop *loops;
    q2_player_name *names;
    qa_unified_document *story;
    q2_activation *story_owner,*sky_owner,*fog_owner;
    char *sky_name,*sky_content;
    qa_scene_image *sky_images[6];
    qa_vec3 sky_axis;
    float sky_rotation;
    bool sky_auto_rotate,fog_received,music_retiring;
    qa_scene_fog fog_start,fog_target;
    double fog_started_ms,fog_duration_ms;
    qa_unified_document *frame, *prepared_frame;
    qa_hud *hud;
    frontend_unified_q2_rr_hud *rr_hud;
    char *help, *layout, *help_text[2];
    bool help_visible;
    bool inventory_visible,score_visible;
    q2_inventory_row *items;
    size_t item_count;
    char **score_rows;
    size_t score_count;
    q2_activation *inventory_owner,*score_owner;
    q2_activation *view_owner;
    char *view_content,*view_provider;
    frontend_remote_q2_effects_profile view_profile;
    qa_vec3 view_gun_offset;
    qa_actor_id view_actor;
    qa_scene_vec4 view_blend,view_damage_blend;
    bool view_blend_present,view_damage_present;
    uint64_t view_layouts, marker_frame, marker_wall_ns;
    uint32_t marker_count;
    bool marker_set;
    qa_scene_image *marker_image;
    uint64_t effects_wall_ns;
    float effects_frame_seconds;
    qa_vec3 viewer_origin;
    float player_fov;
    uint64_t viewer_origin_frame;
    bool viewer_origin_present;
    qa_localization_pool *localizations;
    q2_activation *help_owner[2];
    char **config;
    size_t config_count;
    int32_t inventory[256];
    qa_hud_q2_table table;
    double seconds, prepared_seconds;
    uint64_t frame_number, prepared_number;
    qa_scene_light *lights;
    size_t light_count, light_capacity;
    qa_vec3 sampled_styles[256];
    q2_muzzle_receipt *muzzles;
    size_t muzzle_count;
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
static bool rgba_read(const qa_unified_document *d,qa_json_id row,qa_scene_vec4 *out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    return real(d,get(j,row,"x"),&out->x,e) && real(d,get(j,row,"y"),&out->y,e) &&
        real(d,get(j,row,"z"),&out->z,e) && real(d,get(j,row,"w"),&out->w,e);
}
static bool optional_color(const qa_unified_document *d,qa_json_id row,bool *present,qa_scene_vec4 *out,qa_error *e)
{
    *present=row!=QA_JSON_NONE && qa_json_type(qa_unified_document_json(d),row)!=QA_JSON_NULL;
    *out=(qa_scene_vec4){0};
    return !*present || rgba_read(d,row,out,e);
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
        (o->frontend->capture || o->frontend->source_restoring?
            frontend_remote_unified_actor_retained(o->replica,(uint32_t)slot,generation,out,e):
            frontend_remote_unified_actor(o->replica,(uint32_t)slot,generation,out,e));
}
static bool current(frontend_unified_q2 *o,qa_error *e)
{
    return o && frontend_unified_media_current(o->media) &&
        ((o->frontend->capture || o->frontend->source_restoring)?frontend_remote_unified_checkpoint_current(o->replica,e):
            frontend_remote_unified_current(o->replica,e));
}
static void inventory_clear(frontend_unified_q2 *o)
{
    if (o->items) for (size_t i=0;i<o->item_count;++i) { free(o->items[i].item); free(o->items[i].label); }
    free(o->items); o->items=NULL; o->item_count=0; o->inventory_visible=false; o->inventory_owner=NULL;
}
static void scores_clear(frontend_unified_q2 *o)
{
    if (o->score_rows) for (size_t i=0;i<o->score_count;++i) free(o->score_rows[i]);
    free(o->score_rows); o->score_rows=NULL; o->score_count=0; o->score_visible=false; o->score_owner=NULL;
}
static void sky_clear(frontend_unified_q2 *o)
{
    for (size_t i=0;i<6;++i) { qa_scene_image_release(o->sky_images[i]); o->sky_images[i]=NULL; }
    free(o->sky_name); free(o->sky_content); o->sky_name=NULL; o->sky_content=NULL;
    o->sky_owner=NULL; o->sky_axis=qa_v3(0,0,0); o->sky_rotation=0; o->sky_auto_rotate=false;
}
static bool owner_token(const qa_unified_document *d,qa_json_id token,qa_buffer *provider,uint64_t *generation,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    if (token==QA_JSON_NONE || qa_json_type(j,token)==QA_JSON_NULL) { *generation=0; return true; }
    return string(d,get(j,token,"provider"),provider,e) && provider->size &&
        qa_json_u64(j,get(j,token,"generation"),generation,e) && *generation;
}
static bool activation(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id token,q2_activation **out,qa_error *e)
{
    qa_buffer provider={0}; uint64_t generation=0;
    if (!owner_token(d,token,&provider,&generation,e)) { qa_buffer_free(&provider); return false; }
    if (!generation) { *out=NULL; return true; }
    q2_activation *a=o->activations;
    while (a && (a->generation!=generation || strcmp(a->provider,(const char *)provider.data))) a=a->next;
    if (!a) {
        a=calloc(1,sizeof(*a)); if (!a) { qa_buffer_free(&provider); return false; }
        a->provider=(char *)provider.data; a->generation=generation; a->next=o->activations; o->activations=a;
    } else qa_buffer_free(&provider);
    *out=a; return true;
}
static void visual_free(q2_visual *v)
{ qa_unified_document_destroy(v->model); free(v->content); free(v->path); free(v->source_provider); free(v); }
static q2_visual *visual_read(frontend_unified_q2 *o,qa_actor_id a)
{
    for (q2_visual *v=o->visuals;v;v=v->next) if (qa_actor_id_equal(v->actor,a)) return v;
    return NULL;
}
static bool visual_prepare(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_actor_id a,q2_visual **out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); q2_activation *owner=NULL;
    if (!activation(o,d,get(j,row,"owner"),&owner,e) || (owner && owner->retired))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 presentation uses a retired actual Source owner");
    q2_visual *v=visual_read(o,a);
    if (!v) { v=calloc(1,sizeof(*v)); if (!v) return false; v->actor=a; v->visible=true; v->next=o->visuals; o->visuals=v; }
    if (v->activation!=owner) {
        qa_unified_document_destroy(v->model); v->model=NULL;
        free(v->path); v->path=NULL; v->effects=0; v->event=0; v->event_frame=0; v->visible=true;
    }
    v->activation=owner; *out=v; return true;
}
static bool bank(frontend_unified_q2 *,const char *,q2_activation *,const char *,frontend_remote_q2_effects_profile,bool,q2_bank **,qa_error *);
static bool source_bank(frontend_unified_q2 *,const qa_unified_document *,qa_json_id,const char *,bool,q2_bank **,qa_error *);
static bool model(void *,const char *,bool,qa_scene_model **,qa_error *);
static bool source_interval(const qa_unified_document *d,qa_json_id row,double *out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    return (number(d,get(j,get(j,row,"source"),"frameMilliseconds"),out,e) && *out>0) ||
        frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 Source has no positive finite frame interval receipt");
}
static bool semantic_source(const qa_unified_document *d,qa_json_id row,bool required,qa_buffer *provider,
    frontend_remote_q2_effects_profile *profile,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id source_row=get(j,row,"source");
    *profile=0;
    if (source_row==QA_JSON_NONE || qa_json_type(j,source_row)==QA_JSON_NULL)
        return !required || frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 effects have no actual Source rules receipt");
    if (!string(d,get(j,source_row,"provider"),provider,e) || !provider->size) return false;
    if (qa_json_string_equal(j,get(j,source_row,"profile"),"classic")) *profile=FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC;
    else if (qa_json_string_equal(j,get(j,source_row,"profile"),"rerelease")) *profile=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE;
    else return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 Source rules receipt is unknown");
    double interval;
    return source_interval(d,row,&interval,e);
}
static bool visual_record(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_actor_id *a,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id event=get(j,row,"event"),kind=get(j,event,"kind");
    qa_buffer content={0},provider={0},source_provider={0}; uint64_t generation=0;
    frontend_remote_q2_effects_profile profile;
    bool okay=actor(o,d,get(j,event,"actor"),a,e) && a->registry &&
        string(d,get(j,row,"content"),&content,e) && content.size &&
        owner_token(d,get(j,row,"owner"),&provider,&generation,e) && semantic_source(d,row,false,&source_provider,&profile,e);
    qa_buffer_free(&content); qa_buffer_free(&provider); qa_buffer_free(&source_provider); if (!okay) return false;
    if (qa_json_string_equal(j,kind,"visibility")) { bool visible; return qa_json_bool(j,get(j,event,"visible"),&visible,e); }
    if (qa_json_string_equal(j,kind,"entity-event")) { uint64_t value;
        return qa_json_u64(j,get(j,event,"event"),&value,e) && value<=UINT32_MAX; }
    if (!qa_json_string_equal(j,kind,"model")) return false;
    qa_buffer path={0}; uint64_t value; float scalar;
    okay=string(d,get(j,event,"path"),&path,e);
    qa_buffer_free(&path);
    static const char *const words[]={"frame","oldFrame","skin","renderFlags"};
    for (size_t i=0;okay && i<4;++i) okay=qa_json_u64(j,get(j,event,words[i]),&value,e) && value<=UINT32_MAX;
    okay=okay && qa_json_u64(j,get(j,event,"effects"),&value,e) &&
        real(d,get(j,event,"scale"),&scalar,e) && real(d,get(j,event,"alpha"),&scalar,e);
    qa_json_id attachments=get(j,event,"attachedModels");
    okay=okay && qa_json_type(j,attachments)==QA_JSON_ARRAY;
    for (size_t i=0;okay && i<qa_json_size(j,attachments);++i) {
        qa_buffer name={0}; okay=string(d,qa_json_at(j,attachments,i),&name,e); qa_buffer_free(&name);
    }
    return okay;
}
static bool visual_apply(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    qa_actor_id a; if (!visual_record(o,d,row,&a,e)) return false;
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id event=get(j,row,"event"),kind=get(j,event,"kind");
    qa_buffer content={0},path={0},source_provider={0}; qa_unified_document *copy=NULL; q2_visual *v=NULL;
    frontend_remote_q2_effects_profile profile;
    bool is_model=qa_json_string_equal(j,kind,"model");
    bool okay=string(d,get(j,row,"content"),&content,e) && semantic_source(d,row,false,&source_provider,&profile,e);
    if (okay && is_model) okay=string(d,get(j,event,"path"),&path,e) &&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT,qa_json_source(j,row),&copy,e);
    if (okay && is_model) {
        q2_bank *b=NULL; qa_scene_model *scene=NULL;
        okay=bank(o,(const char *)content.data,NULL,NULL,0,false,&b,e) && model(b,(const char *)path.data,true,&scene,e);
        qa_json_id attached=get(j,event,"attachedModels");
        for (size_t i=0;okay && i<qa_json_size(j,attached);++i) {
            qa_buffer name={0}; okay=string(d,qa_json_at(j,attached,i),&name,e) && model(b,(const char *)name.data,true,&scene,e);
            qa_buffer_free(&name);
        }
    }
    if (okay) okay=visual_prepare(o,d,row,a,&v,e);
    if (okay && profile) { q2_bank *effects_bank=NULL;
        okay=source_bank(o,d,row,(const char *)content.data,true,&effects_bank,e); }
    if (okay) {
        bool same_source=(v->source_provider!=NULL)==(source_provider.data!=NULL) &&
            (!source_provider.data || !strcmp(v->source_provider,(const char *)source_provider.data));
        if (!same_source || (v->content && strcmp(v->content,(const char *)content.data))) {
            qa_unified_document_destroy(v->model); v->model=NULL;
            free(v->path); v->path=NULL; v->effects=0; v->event=0; v->event_frame=0; v->visible=true;
        }
        free(v->content); v->content=(char *)content.data; content=(qa_buffer){0};
        free(v->source_provider); v->source_provider=(char *)source_provider.data; source_provider=(qa_buffer){0};
        if (is_model) {
            free(v->path); v->path=(char *)path.data; path=(qa_buffer){0};
            qa_unified_document_destroy(v->model); v->model=copy; copy=NULL;
            okay=qa_json_u64(j,get(j,event,"effects"),&v->effects,e);
        } else if (qa_json_string_equal(j,kind,"visibility")) okay=qa_json_bool(j,get(j,event,"visible"),&v->visible,e);
        else { uint64_t value=0; okay=qa_json_u64(j,get(j,event,"event"),&value,e);
            if (okay) { v->event=(uint32_t)value; v->event_frame=o->frame_number; } }
    }
    qa_unified_document_destroy(copy); qa_buffer_free(&path); qa_buffer_free(&content); qa_buffer_free(&source_provider); return okay;
}
static bool owner_record(const qa_unified_document *d,qa_json_id row,qa_buffer *provider,uint64_t *generation,bool *retired,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id event=get(j,row,"event"),kind=get(j,event,"kind");
    double seconds; uint64_t sequence;
    if (!qa_json_string_equal(j,get(j,row,"kind"),"presentation-owner") ||
        (!qa_json_string_equal(j,kind,"retired") && !qa_json_string_equal(j,kind,"refreshed"))) return false;
    *retired=qa_json_string_equal(j,kind,"retired");
    return owner_token(d,get(j,event,"owner"),provider,generation,e) && *generation &&
        number(d,get(j,row,"seconds"),&seconds,e) && qa_json_u64(j,get(j,row,"sequence"),&sequence,e);
}
bool frontend_unified_q2_owner_validate(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    if (!o || !d || !current(o,e)) return false;
    qa_buffer provider={0}; uint64_t generation=0; bool retired;
    bool okay=owner_record(d,row,&provider,&generation,&retired,e); qa_buffer_free(&provider);
    return okay && frontend_unified_q2_rr_owner_validate(o->rr_hud,d,row,e);
}
bool frontend_unified_q2_owner_retire(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    if (!o || !d || !frontend_unified_q2_idle(o) || !current(o,e)) return false;
    qa_buffer provider={0}; uint64_t generation=0; bool retired;
    bool okay=owner_record(d,row,&provider,&generation,&retired,e); qa_buffer_free(&provider); if (!okay) return false;
    if (!frontend_unified_q2_rr_owner_retire(o->rr_hud,d,row,e)) return false;
    const qa_json_document *j=qa_unified_document_json(d); q2_activation *a=NULL;
    if (!activation(o,d,get(j,get(j,row,"event"),"owner"),&a,e)) return false;
    if (!retired || a->retired) return true;
    q2_loop **loop=&o->loops;
    while (*loop) {
        q2_loop *l=*loop;
        if (l->activation!=a) { loop=&l->next; continue; }
        if (!frontend_unified_events_sound_stop_loop(o->events,l->actor,e)) return false;
        *loop=l->next; free(l);
    }
    q2_visual **next=&o->visuals;
    while (*next) { q2_visual *v=*next;
        if (v->activation==a) { *next=v->next; visual_free(v); } else next=&v->next; }
    for (size_t i=0;i<2;++i) if (o->help_owner[i]==a) { free(o->help_text[i]); o->help_text[i]=NULL; o->help_owner[i]=NULL; }
    if (o->inventory_owner==a) inventory_clear(o);
    if (o->score_owner==a) scores_clear(o);
    if (o->story_owner==a) { qa_unified_document_destroy(o->story); o->story=NULL; o->story_owner=NULL; }
    if (o->sky_owner==a) sky_clear(o);
    if (o->fog_owner==a) { o->fog_received=false; o->fog_owner=NULL; o->fog_start=(qa_scene_fog){0};
        o->fog_target=(qa_scene_fog){0}; o->fog_started_ms=0; o->fog_duration_ms=0; }
    if (o->view_owner==a) { free(o->view_content); free(o->view_provider); o->view_content=NULL; o->view_provider=NULL;
        o->view_owner=NULL; o->view_profile=0; o->view_layouts=0; o->view_gun_offset=qa_v3(0,0,0);
        o->view_actor=(qa_actor_id){0}; o->view_blend_present=false; o->view_damage_present=false;
        o->view_blend=(qa_scene_vec4){0}; o->view_damage_blend=(qa_scene_vec4){0}; }
    for (q2_bank *b=o->banks;b;b=b->next) if (b->activation==a)
        for (size_t i=0;i<256;++i) { free(b->styles[i]); b->styles[i]=NULL; b->style_sequences[i]=0; }
    for (q2_bank *b=o->banks;b;b=b->next) if (b->activation==a && b->effects &&
        !frontend_remote_q2_effects_retire_presentation(b->effects,e)) return false;
    o->music_retiring=true;
    for (q2_bank *b=o->banks;b;b=b->next) if (b->activation==a && !frontend_received_music_destroy(&b->music,e)) {
        o->music_retiring=false; return false;
    }
    o->music_retiring=false;
    a->retired=true; return true;
}
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
        if (!actor(o,d,get(j,row,"actor"),&actual,e)) return false;
        if (!qa_actor_id_equal(actual,id)) continue;
        frontend_remote_q2_effects_pose p={.actor=id};
        q2_visual *visual=visual_read(o,id);
        if (visual && !strcmp(visual->content?visual->content:"",b->content)) {
            p.effects=visual->effects; p.event=visual->event_frame==o->frame_number?visual->event:0;
        }
        if (!vector(d,get(j,row,"origin"),&p.origin,e) || !vector(d,get(j,row,"angles"),&p.angles,e) ||
            !integer(d,get(j,row,"frame"),&p.frame,e) || !real(d,get(j,row,"scale"),&p.scale,e)) return false;
        if (visual && visual->model) {
            const qa_json_document *vj=qa_unified_document_json(visual->model);
            qa_json_id ev=get(vj,qa_unified_document_root(visual->model),"event");
            if (!real(visual->model,get(vj,ev,"scale"),&p.scale,e)) return false;
        }
        if (p.scale == 0) p.scale=1;
        qa_buffer path={0},content={0};
        qa_scene_family family=qa_json_string_equal(j,get(j,row,"family"),"q1")?QA_SCENE_Q1:
            qa_json_string_equal(j,get(j,row,"family"),"q2")?QA_SCENE_Q2:QA_SCENE_Q3;
        bool okay=string(d,get(j,row,"path"),&path,e) && string(d,get(j,row,"content"),&content,e);
        qa_scene_image_options options={.family=family,.usage=QA_IMAGE_USAGE_SKIN,.wrap=QA_SCENE_REPEAT,
            .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=255};
        frontend_unified_model m;
        if (okay) okay=frontend_unified_media_model(o->media,(const char *)content.data,(const char *)path.data,family,&options,&m,e);
        qa_buffer_free(&path); qa_buffer_free(&content); if (!okay) return false;
        p.model_present=m.scene!=NULL;
        if (m.model) {
            p.bounds=(qa_bounds){.mins=qa_v3(m.model->bounds.min[0],m.model->bounds.min[1],m.model->bounds.min[2]),
                .maxs=qa_v3(m.model->bounds.max[0],m.model->bounds.max[1],m.model->bounds.max[2])};
            p.radius=m.model->radius; p.bounds_present=true;
        }
        *out=p; return true;
    }
    qa_body_state body;
    if (!frontend_remote_unified_presentation_body(o->replica,id,&body,e)) return false;
    *out=(frontend_remote_q2_effects_pose){.actor=id,.origin=body.origin,.angles=body.angles,.bounds=body.bounds,.scale=1};
    return true;
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
static bool actor_live(void *ctx,qa_actor_id id,bool *out,qa_error *e)
{
    q2_bank *b=ctx;
    qa_saved_actor_id wire;
    if (!out || !current(b->owner,e)) return false;
    if (id.registry!=qa_actors_identity(b->owner->replica->actors) ||
        !frontend_remote_unified_wire_actor(b->owner->replica,id,&wire))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 effect actor has no retained replica identity");
    *out=qa_actors_get(b->owner->replica->actors,id)!=NULL;
    return true;
}
static bool viewer(void *ctx,qa_actor_id *out,qa_error *e)
{
    q2_bank *b=ctx; uint32_t source_number;
    return out && current(b->owner,e) && frontend_remote_unified_player(b->owner->replica,out,&source_number);
}
static frontend_remote_q2_effects_source source(q2_bank *);
static frontend_q2_footstep_source footstep_source(q2_bank *);
static bool source_current(void *ctx,const frontend_remote_q2_effects_source *s,qa_error *e)
{
    q2_bank *b=ctx;
    frontend_remote_q2_effects_source expected=source(b);
    bool okay=s && s->context==b && s->identity==frontend_unified_events_audio_owner(b->owner->events) &&
        s->content_generation==frontend_remote_unified_epoch(b->owner->replica) &&
        s->files==b->files && s->images==b->images && s->materials==b->materials &&
        s->world==frontend_unified_media_world(b->owner->media) &&
        s->map==qa_executable_recipe_map(frontend_remote_unified_recipe(b->owner->replica)) &&
        s->session==expected.session && s->white==expected.white && s->protocol.kind==expected.protocol.kind &&
        s->profile==expected.profile && s->protocol.revision==expected.protocol.revision && s->protocol.flags==expected.protocol.flags &&
        s->video_frame==expected.video_frame && s->video_context==expected.video_context &&
        s->current==expected.current && s->actor==expected.actor && s->actor_pose==expected.actor_pose &&
        s->actor_live==expected.actor_live && s->viewer==expected.viewer &&
        s->model==expected.model && s->sound==expected.sound && s->hit_marker==expected.hit_marker &&
        s->footstep==expected.footstep && s->trace==expected.trace && s->controls==expected.controls &&
        s->frame_milliseconds==expected.frame_milliseconds && s->render_clock==expected.render_clock && current(b->owner,e);
    if (okay && b->footsteps) { frontend_q2_footstep_source actual=footstep_source(b);
        okay=frontend_q2_footsteps_current(b->footsteps,&actual,e); }
    return okay;
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
    frontend_unified_q2 *o=b->owner;
    if (damage<=0 || !current(o,e)) return false;
    if (o->marker_set && o->marker_frame==o->frame_number) return true;
    if (!o->marker_image) {
        qa_scene_image_options opts={.family=QA_SCENE_Q2,.usage=QA_IMAGE_USAGE_PICTURE,.wrap=QA_SCENE_CLAMP,
            .filter=QA_SCENE_LINEAR,.transparent=true,.transparent_index=255};
        qa_error issue={0}; qa_scene_image *image=NULL;
        if (!qa_scene_image_load(b->images,"pics/marker.pcx",&opts,&image,&issue) && issue.code!=QA_ERROR_NOT_FOUND) {
            if (e) *e=issue;
            return false;
        }
        o->marker_image=image;
    }
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    const qa_cvar_view *mode=qa_cvars_find(domain->cvars,"cl_hit_markers");
    if (!mode) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 marker has no actual CLIENT mode row");
    o->marker_set=true; o->marker_frame=o->frame_number; o->marker_wall_ns=o->frontend->wall_time_ns;
    if (o->marker_count<UINT32_MAX) ++o->marker_count;
    if (mode->integer>1) {
        const qa_json_document *j=qa_unified_document_json(o->frame);
        qa_json_id view=get(j,get(j,qa_unified_document_root(o->frame),"player"),"view");
        qa_vec3 origin; float height;
        if (!vector(o->frame,get(j,view,"origin"),&origin,e) || !real(o->frame,get(j,view,"viewHeight"),&height,e)) return false;
        origin.z+=height;
        qa_actor_id viewer_actor; uint32_t number_id;
        if (!frontend_remote_unified_player(o->replica,&viewer_actor,&number_id)) return false;
        return sound(b,"weapons/marker.wav",origin,viewer_actor,o->seconds*1000,257,1,0,0,e);
    }
    return true;
}
static bool trace(void *ctx,const qa_trace_query *query,qa_trace_result *out,qa_error *e)
{
    q2_bank *b=ctx;
    return current(b->owner,e) && frontend_remote_unified_presentation_trace(b->owner->replica,query,out,e);
}
static bool rail_color(q2_bank *b,const char *name,uint32_t *out,qa_error *e)
{
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(b->owner->replica);
    const qa_cvar_view *row=qa_cvars_find(domain->cvars,name);
    if (!row) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 rail color lost its actual CLIENT declaration");
    if (frontend_remote_q2_effects_color(row->value,out)) return true;
    size_t value_length=strlen(row->value),name_length=strlen(name);
    if (value_length>SIZE_MAX-name_length-32)
        return frontend_unified_fail(e,QA_ERROR_MEMORY,"Q2 rail color warning overflow");
    size_t capacity=value_length+name_length+32; char *warning=malloc(capacity);
    if (!warning) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 rail color warning");
    snprintf(warning,capacity,"Invalid value '%s' for '%s'\n",row->value,name);
    qa_console_emit(domain->console,&domain->command_context,warning); free(warning);
    if (!current(b->owner,e) || !qa_cvars_reset(domain->cvars,name,true,e) || !current(b->owner,e)) return false;
    row=qa_cvars_find(domain->cvars,name);
    return (row && frontend_remote_q2_effects_color(row->value,out)) ||
        frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 rail color has no valid actual CLIENT reset value");
}
static bool controls(void *ctx,frontend_remote_q2_effects_controls *out,qa_error *e)
{
    q2_bank *b=ctx;
    if (!out || !current(b->owner,e)) return false;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(b->owner->replica);
    if (!qa_cvars_observer_idle(domain->cvars))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 effects controls require their returned CLIENT registry");
    uint32_t core,spiral;
    if (!rail_color(b,"cl_railcore_color",&core,e) || !rail_color(b,"cl_railspiral_color",&spiral,e)) return false;
    const qa_cvar_view *rail_time=qa_cvars_find(domain->cvars,"cl_railtrail_time");
    if (!rail_time || !isfinite(rail_time->number))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 rail time has no actual finite CLIENT row");
    float duration=rail_time->number;
    if ((duration<0 || duration>2073600) &&
        (!qa_cvars_set_number(domain->cvars,"cl_railtrail_time",duration<0?0:2073600,e) || !current(b->owner,e))) return false;
    rail_time=qa_cvars_find(domain->cvars,"cl_railtrail_time");
    const qa_cvar_view *core_row=qa_cvars_find(domain->cvars,"cl_railcore_color"),
        *spiral_row=qa_cvars_find(domain->cvars,"cl_railspiral_color");
    if (!core_row || !spiral_row || !frontend_remote_q2_effects_color(core_row->value,&core) ||
        !frontend_remote_q2_effects_color(spiral_row->value,&spiral))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 rail controls changed during their actual CLIENT normalization");
    const qa_cvar_view *time=qa_cvars_find(domain->cvars,"cl_muzzlelight_time"),
        *effects=qa_cvars_find(domain->cvars,"cl_rerelease_effects"),*hacks=qa_cvars_find(domain->cvars,"cl_dlight_hacks"),
        *flashes=qa_cvars_find(domain->cvars,"cl_muzzleflashes"),
        *particles=qa_cvars_find(domain->cvars,"cl_disable_particles"),*explosions=qa_cvars_find(domain->cvars,"cl_disable_explosions"),
        *gun=qa_cvars_find(domain->cvars,"cl_gun"),*gun_fov=qa_cvars_find(domain->cvars,"cl_gunfov"),
        *rail_type=qa_cvars_find(domain->cvars,"cl_railtrail_type"),*rail_width=qa_cvars_find(domain->cvars,"cl_railcore_width"),
        *rail_radius=qa_cvars_find(domain->cvars,"cl_railspiral_radius");
    if (!time || !effects || !hacks || !flashes || !particles || !explosions || !gun || !gun_fov ||
        !rail_time || !rail_type || !rail_width || !rail_radius || !isfinite(rail_time->number) ||
        rail_time->number<0 || rail_time->number>2073600 || !isfinite(rail_radius->number) ||
        !isfinite(gun_fov->number) || fabs(gun_fov->number)>FLT_MAX)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 semantic effects have no actual CLIENT controls registry");
    frontend_remote_q2_effects_controls result={.muzzlelight_milliseconds=time->integer,.rerelease_effects=effects->integer!=0,
        .muzzleflashes=flashes->integer!=0,.dlight_hacks=(uint32_t)hacks->integer,
        .disable_particles=(uint32_t)particles->integer,.disable_explosions=(uint32_t)explosions->integer,
        .gun=gun->integer,.gun_fov=(float)gun_fov->number,.rail_type=rail_type->integer,.rail_width=rail_width->integer,
        .rail_seconds=rail_time->number,.rail_radius=rail_radius->number,.rail_core_rgba=core,.rail_spiral_rgba=spiral};
    if (!current(b->owner,e)) return false;
    *out=result; return true;
}
static bool frame_milliseconds(void *ctx,double *out,qa_error *e)
{
    q2_bank *b=ctx;
    if (!out || !current(b->owner,e)) return false;
    if (!isfinite(b->frame_milliseconds) || b->frame_milliseconds<=0)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 effects lost the actual Source frame interval");
    *out=b->frame_milliseconds; return true;
}
static bool render_clock(void *ctx,uint64_t *wall,uint64_t *sequence,qa_error *e)
{
    q2_bank *b=ctx;
    if (!wall || !sequence || !current(b->owner,e)) return false;
    *wall=b->owner->frontend->wall_time_ns/UINT64_C(1000000);
    *sequence=b->owner->frontend->frame_number; return true;
}
static bool footstep_current(void *ctx,const frontend_q2_footstep_source *s)
{
    q2_bank *b=ctx; frontend_q2_footstep_source actual=footstep_source(b);
    return s && s->catalog==actual.catalog && s->product==actual.product && s->map==actual.map &&
        s->files==actual.files && s->geometry==actual.geometry && s->sounds==actual.sounds && s->context==b &&
        s->map_name && actual.map_name && !strcmp(s->map_name,actual.map_name) &&
        s->current==actual.current && s->trace==actual.trace && s->sound==actual.sound && current(b->owner,NULL);
}
static frontend_q2_footstep_source footstep_source(q2_bank *b)
{
    qa_executable_recipe *recipe=frontend_remote_unified_recipe(b->owner->replica);
    return (frontend_q2_footstep_source){.catalog=qa_executable_recipe_catalog(recipe),.product=b->product->id,
        .map_name=qa_executable_recipe_choices(recipe)->world.map,.map=qa_executable_recipe_map(recipe),
        .files=b->files,.geometry=qa_executable_recipe_geometry(recipe),.sounds=b->sounds,.context=b,
        .current=footstep_current,.trace=trace,.sound=sound};
}
static bool footstep(void *ctx,const frontend_remote_q2_effects_pose *pose_value,uint32_t event,double ms,
    qa_builtin_random *random,qa_error *e)
{
    q2_bank *b=ctx;
    if (!b->footsteps || !pose_value || !current(b->owner,e)) return false;
    const qa_cvar_view *control=qa_cvars_find(frontend_remote_unified_domain_read(b->owner->replica)->cvars,"cl_footsteps");
    if (!control || !isfinite(control->number) || fabs(control->number)>FLT_MAX)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 footstep has no actual finite CLIENT control");
    qa_body_state body;
    if (!frontend_remote_unified_presentation_body(b->owner->replica,pose_value->actor,&body,e)) return false;
    frontend_q2_footstep_source s=footstep_source(b);
    frontend_q2_footstep_sample sample={.pose=*pose_value,.trace_bounds=body.bounds,.bottom=body.bounds.mins.z,
        .footsteps=(float)control->number,.milliseconds=ms,.event=event};
    return frontend_q2_footsteps_emit(b->footsteps,&s,&sample,random,e);
}
static frontend_remote_q2_effects_source source(q2_bank *b)
{
    return (frontend_remote_q2_effects_source){.identity=frontend_unified_events_audio_owner(b->owner->events),
        .content_generation=frontend_remote_unified_epoch(b->owner->replica),
        .profile=b->profile,
        .map=qa_executable_recipe_map(frontend_remote_unified_recipe(b->owner->replica)),.files=b->files,
        .images=b->images,.materials=b->materials,.world=frontend_unified_media_world(b->owner->media),.white=qa_scene_white(b->images),.context=b,
        .video_frame=frontend_material_movies_frontend_resolve,.video_context=b->owner->frontend,
        .current=source_current,.actor=pose,.actor_pose=full_pose,.actor_live=actor_live,.viewer=viewer,.model=model,.sound=sound,.hit_marker=hit,
        .controls=controls,.frame_milliseconds=frame_milliseconds,.render_clock=render_clock,.trace=trace,.footstep=footstep};
}
static bool bank(frontend_unified_q2 *o,const char *content,q2_activation *activation_owner,const char *source_provider,
    frontend_remote_q2_effects_profile profile,bool effects,q2_bank **out,qa_error *e)
{
    q2_bank *b=o->banks;
    while (b && (strcmp(b->content,content) || b->activation!=activation_owner ||
        (b->source_provider!=NULL)!=(source_provider!=NULL) ||
        (source_provider && strcmp(b->source_provider,source_provider)))) b=b->next;
    if (!b) {
        b=calloc(1,sizeof(*b));
        if (!b) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 CLIENT content");
        b->owner=o; b->activation=activation_owner; b->profile=profile;
        b->content=malloc(strlen(content)+1);
        if (!b->content) { free(b); return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q2 content identity"); }
        strcpy(b->content,content);
        if (source_provider) {
            b->source_provider=malloc(strlen(source_provider)+1);
            if (!b->source_provider) { free(b->content); free(b); return false; }
            strcpy(b->source_provider,source_provider);
        }
        qa_font_library *fonts; qa_audio_bank *sounds;
        bool okay=qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),content,&b->files,&b->product,e) &&
            b->product->family==QA_GAME_Q2 && frontend_unified_media_bank(o->media,content,&b->images,&b->materials,&fonts,&sounds,e);
        if (okay) b->sounds=sounds;
        if (!okay) { free(b->source_provider); free(b->content); free(b); return false; }
        q2_bank **tail=&o->banks;
        while (*tail) tail=&(*tail)->next;
        *tail=b;
    }
    if (b->profile!=profile) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 Source changed rules without replacing its actual receipt");
    if (effects && !b->effects) {
        if (b->profile!=FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC && b->profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE)
            return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 effects have no actual Source rules declaration");
        if (o->frontend->source_restoring || o->frontend->capture)
            return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 effects are absent from their retained continuation");
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        if (!current(o,e) || !domain ||
            !frontend_source_q2_effects_register(domain->cvars,&domain->command_context,b->profile,e) ||
            !current(o,e)) return false;
        if (b->profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE && !b->footsteps) {
            frontend_q2_footstep_source footsteps=footstep_source(b);
            if (!frontend_q2_footsteps_create(&footsteps,&b->footsteps,e)) return false;
        }
        frontend_remote_q2_effects_source s=source(b);
        if (!frontend_remote_q2_effects_create(&s,&b->effects,e)) return false;
    }
    *out=b; return true;
}
static bool source_bank(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,const char *content,bool effects,q2_bank **out,qa_error *e)
{
    q2_activation *a=NULL;
    if (!activation(o,d,get(qa_unified_document_json(d),row,"owner"),&a,e) || (a && a->retired))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 effect lost its actual retained Source owner");
    qa_buffer provider={0}; frontend_remote_q2_effects_profile profile;
    double interval=0; q2_bank *b=NULL;
    bool okay=semantic_source(d,row,true,&provider,&profile,e) && source_interval(d,row,&interval,e) &&
        bank(o,content,a,(const char *)provider.data,profile,false,&b,e);
    if (okay) {
        b->frame_milliseconds=interval;
        okay=bank(o,content,a,(const char *)provider.data,profile,effects,out,e);
    }
    qa_buffer_free(&provider); return okay;
}
static bool event_bank(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,const char *content,q2_bank **out,qa_error *e)
{ return source_bank(o,d,row,content,true,out,e); }
static bool music_current(void *context,const frontend_music_origin *origin)
{
    q2_bank *b=context; frontend_unified_q2 *o=b?b->owner:NULL;
    if (!o || !origin || (b->activation && b->activation->retired)) return false;
    bool held=o->music_retiring;
    if (held) {
        bool linked=false;
        for (frontend_remote_unified *row=o->frontend->remote_unified;row;row=row->next) if (row==o->replica) linked=true;
        if (!linked || o->replica->busy || o->replica->frontend!=o->frontend ||
            o->frontend->application!=o->replica->options.domain.application ||
            frontend_unified_media_recipe(o->media)!=o->replica->recipe || !frontend_unified_media_current(o->media)) return false;
    } else if (!current(o,NULL)) return false;
    bool linked=false;
    for (q2_bank *row=o->banks;row;row=row->next) if (row==b) linked=true;
    qa_executable_recipe *recipe=frontend_remote_unified_recipe(o->replica);
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    return linked && domain && origin->kind==FRONTEND_MUSIC_REMOTE && origin->context==b &&
        origin->receiver==domain->command_context.owner && origin->physical_seat==domain->physical_seat &&
        origin->recipe==recipe && origin->recipe_content && !strcmp(origin->recipe_content,b->content) &&
        origin->catalog==qa_executable_recipe_catalog(recipe) && origin->product==b->product->id && origin->files==b->files;
}
static bool music_origin(q2_bank *b,frontend_music_origin *out,qa_error *e)
{
    qa_executable_recipe *recipe=frontend_remote_unified_recipe(b->owner->replica);
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(b->owner->replica);
    if (!domain || !domain->command_context.owner || domain->command_context.owner>UINT32_MAX)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 music has no actual private CLIENT receiver");
    *out=(frontend_music_origin){.kind=FRONTEND_MUSIC_REMOTE,.receiver=(qa_actor_owner)domain->command_context.owner,
        .physical_seat=domain->physical_seat,.recipe=recipe,.recipe_content=b->content,
        .catalog=qa_executable_recipe_catalog(recipe),.product=b->product->id,.files=b->files,.context=b,.current=music_current};
    return true;
}
static bool music_receive(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_buffer content={0},track={0}; q2_bank *b=NULL;
    bool okay=string(d,get(j,row,"content"),&content,e) && string(d,get(j,get(j,row,"event"),"track"),&track,e) &&
        source_bank(o,d,row,(const char *)content.data,false,&b,e);
    if (okay && o->frontend->audio) {
        frontend_music_origin origin;
        okay=music_origin(b,&origin,e) && (b->music || frontend_received_music_create(o->frontend,&origin,&b->music,e)) &&
            frontend_received_music_play(b->music,(const char *)track.data,e);
    }
    qa_buffer_free(&content); qa_buffer_free(&track); return okay;
}
static bool achievement(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,bool publish,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    qa_buffer award={0},content={0}; qa_vfs *files=NULL; const qa_product *product=NULL;
    bool okay=string(d,get(j,get(j,row,"event"),"id"),&award,e) &&
        string(d,get(j,row,"content"),&content,e) &&
        qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),(const char *)content.data,&files,&product,e) &&
        product->family==QA_GAME_Q2;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    qa_player_progress *store=okay?qa_application_player_progress(domain->application):NULL;
    if (okay && award.size && !store)
        okay=frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 achievement has no installed player profile store");
    if (okay && publish && award.size) {
        static const char prefix[]="achievement:";
        size_t start=sizeof(prefix)-1;
        if (content.size>SIZE_MAX-start-1 || award.size>SIZE_MAX-start-content.size-1)
            okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Q2 achievement identity exceeds storage");
        size_t length=okay?start+content.size+1+award.size:0;
        uint8_t *identity=okay?malloc(length):NULL;
        if (okay && !identity) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining received Q2 achievement identity");
        if (okay) {
            memcpy(identity,prefix,start); memcpy(identity+start,content.data,content.size);
            identity[start+content.size]=':'; memcpy(identity+start+content.size+1,award.data,award.size);
            char participant[32];
            int count=snprintf(participant,sizeof(participant),"local-seat:%u",(unsigned)o->frontend->seats[domain->physical_seat].id);
            if (count<=0 || (size_t)count>=sizeof(participant))
                okay=frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 achievement changed its actual local participant");
            if (okay) {
                qa_progress_event event={.kind=QA_PROGRESS_ACHIEVEMENT,.source=QA_GAME_Q2,
                    .participant={(const uint8_t *)participant,(size_t)count},.event={identity,length},
                    .value.award={award.data,award.size}};
                bool inserted;
                okay=qa_player_progress_record(store,&event,&inserted,e);
            }
        }
        free(identity);
    }
    qa_buffer_free(&award); qa_buffer_free(&content); return okay;
}
static bool hud_read(void *ctx,const qa_hud_frame *frame,qa_hud_data *out,qa_error *e)
{
    frontend_unified_q2 *o=ctx;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (!o->busy || frame->seat!=d->physical_seat) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 message HUD lost its CLIENT seat");
    *out=(qa_hud_data){.source_vitals=true};
    if (o->help_visible) {
        out->help_title="Help computer";
        if (o->help_text[0] && o->help_text[1]) { out->help_lines=(const char *const *)o->help_text; out->help_count=2; }
        else if (o->help_text[0]) { out->help_lines=(const char *const *)o->help_text; out->help_count=1; }
        else if (o->help_text[1]) { out->help_lines=(const char *const *)o->help_text+1; out->help_count=1; }
    }
    return true;
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
    *out=o;
    o->effects_wall_ns=f->wall_time_ns;
    o->localizations=qa_localization_pool_create(e);
    qa_hud_options h=hud_options(o);
    if (!o->localizations || !current(o,e) || !qa_hud_create(&h,&o->hud,e) ||
        !frontend_unified_q2_rr_create(f,r,media,events,&o->rr_hud,e)) {
        qa_error cleanup={0}; frontend_unified_q2_destroy(out,&cleanup); return false;
    }
    return true;
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
static bool player_names_expand(frontend_unified_q2 *o,qa_buffer *text,qa_error *e)
{
    const char *input=(const char *)text->data; size_t extent=0; char *output=NULL;
    for (unsigned pass=0;pass<2;++pass) {
        size_t used=0;
        for (size_t i=0;i<text->size;) {
            const char *part=input+i; size_t length=1;
            if (text->size-i>=4 && input[i]=='#' && input[i+1]=='#' && input[i+2]=='P' &&
                input[i+3]>='0' && input[i+3]<='9') {
                size_t next=i+3; uint32_t slot=0; bool overflow=false;
                while (next<text->size && input[next]>='0' && input[next]<='9') {
                    unsigned digit=(unsigned)(input[next]-'0');
                    if (slot>(UINT32_MAX-digit)/10) overflow=true;
                    if (!overflow) slot=slot*10+digit;
                    ++next;
                }
                q2_player_name *name=overflow?NULL:o->names;
                while (name && name->slot!=slot) name=name->next;
                part=name?name->name:""; length=strlen(part); i=next;
            } else ++i;
            if (length>SIZE_MAX-1-used) { free(output);
                return frontend_unified_fail(e,QA_ERROR_MEMORY,"Q2 player name expansion exceeds text storage"); }
            if (pass) memcpy(output+used,part,length);
            used+=length;
        }
        if (!pass) {
            extent=used; output=malloc(extent+1);
            if (!output) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q2 localized player names");
        }
    }
    output[extent]=0; qa_buffer_free(text); *text=(qa_buffer){(uint8_t *)output,extent}; return true;
}
static bool userinfo(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id event,bool publish,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_actor_id id; uint64_t slot;
    qa_buffer name={0},skin={0};
    bool okay=actor(o,d,get(j,event,"actor"),&id,e) && id.registry &&
        qa_json_u64(j,get(j,event,"slot"),&slot,e) && slot<=UINT32_MAX &&
        string(d,get(j,event,"name"),&name,e) && string(d,get(j,event,"skin"),&skin,e);
    if (okay && publish) {
        q2_player_name *row=o->names;
        while (row && row->slot!=(uint32_t)slot) row=row->next;
        if (!row) {
            row=calloc(1,sizeof(*row));
            if (!row) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q2 player name declaration");
            else { row->slot=(uint32_t)slot; row->next=o->names; o->names=row; }
        }
        if (okay) { free(row->name); row->name=(char *)name.data; name=(qa_buffer){0}; }
    }
    qa_buffer_free(&name); qa_buffer_free(&skin); return okay;
}
static bool localized(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_json_id event,qa_buffer *out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_buffer content={0},text={0},provider={0};
    frontend_remote_q2_effects_profile profile=0;
    q2_bank *b=NULL;
    bool okay=string(d,get(j,event,"text"),&text,e) && string(d,get(j,row,"content"),&content,e) &&
        semantic_source(d,row,false,&provider,&profile,e) && bank(o,(const char *)content.data,NULL,NULL,0,false,&b,e);
    bool localize=qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease") || profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE;
    if (okay && localize) {
        qa_json_id args=get(j,event,"args"); size_t count=args==QA_JSON_NONE?0:qa_json_size(j,args);
        qa_buffer *values=NULL; const char **arguments=NULL;
        okay=args==QA_JSON_NONE || qa_json_type(j,args)==QA_JSON_ARRAY;
        if (okay && count) {
            okay=count<=SIZE_MAX/sizeof(*values) && count<=SIZE_MAX/sizeof(*arguments);
            if (okay) { values=calloc(count,sizeof(*values)); arguments=calloc(count,sizeof(*arguments)); okay=values && arguments; }
            if (!okay) frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining received localization arguments");
        }
        for (size_t i=0;okay && i<count;++i) {
            okay=string(d,qa_json_at(j,args,i),values+i,e);
            if (okay) arguments[i]=(const char *)values[i].data;
        }
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        qa_ui_preferences preferences; qa_localization *catalog=NULL;
        qa_localization_options opts={.profile=QA_LOCALIZATION_Q2_RERELEASE};
        okay=okay && qa_ui_preferences_read(qa_application_cvars(domain->application),domain->physical_seat,&preferences,e) &&
            qa_localization_acquire(o->localizations,b->files,preferences.language,&opts,&catalog,e);
        if (okay) {
            char output[1024]; size_t n=qa_localize_presentation(catalog,(const char *)text.data,arguments,count,false,output,sizeof(output));
            uint8_t *p=malloc(n+1);
            if (!p) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining received Q2 localized text");
            else { memcpy(p,output,n+1); qa_buffer_free(&text); text=(qa_buffer){p,n}; }
        }
        qa_localization_release(catalog);
        for (size_t i=0;values && i<count;++i) qa_buffer_free(values+i);
        free(values); free(arguments);
        if (okay) okay=player_names_expand(o,&text,e);
    }
    qa_buffer_free(&content); qa_buffer_free(&provider);
    if (okay) { *out=text; return true; } qa_buffer_free(&text); return false;
}
static bool received_text(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_json_id event,
    double seconds,bool center,bool console,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_buffer text={0};
    if (!localized(o,d,row,event,&text,e)) return false;
    bool instant=true; double duration=3;
    qa_json_id value=get(j,event,"instant");
    if (center && value!=QA_JSON_NONE && !qa_json_bool(j,value,&instant,e)) { qa_buffer_free(&text); return false; }
    value=get(j,event,"durationSeconds");
    if (center && value!=QA_JSON_NONE && (!number(d,value,&duration,e) || duration<0)) { qa_buffer_free(&text); return false; }
    bool okay=center?qa_hud_center_print(o->hud,(const char *)text.data,nanoseconds(seconds),nanoseconds(duration),instant,UINT64_C(50000000),e):
        qa_hud_notify(o->hud,(const char *)text.data,qa_json_string_equal(j,get(j,event,"level"),"chat"),nanoseconds(seconds),UINT64_C(3000000000),e);
    if (okay && console) {
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
    } else if (qa_json_string_equal(j,kind,"q2-muzzle-flash")) {
        int32_t number_id,flash; bool monster;
        okay=integer(d,get(j,event,"entityNumber"),&number_id,e) && number_id>=0 &&
            integer(d,get(j,event,"flash"),&flash,e) && flash>=0 && qa_json_bool(j,get(j,event,"monster"),&monster,e);
    } else if (qa_json_string_equal(j,kind,"q2-layout")) okay=string(d,get(j,event,"program"),&text,e);
    else if (qa_json_string_equal(j,kind,"command-text")) okay=string(d,get(j,event,"text"),&text,e);
    else if (qa_json_string_equal(j,kind,"disconnect")) okay=string(d,get(j,event,"reason"),&text,e);
    else if (qa_json_string_equal(j,kind,"q2-inventory")) {
        qa_json_id counts=get(j,event,"counts"); okay=qa_json_type(j,counts)==QA_JSON_ARRAY && qa_json_size(j,counts)==256;
        for (size_t i=0;okay && i<256;++i) { int32_t v; okay=integer(d,qa_json_at(j,counts,i),&v,e); }
    }
    qa_buffer_free(&text); return okay || frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 simulation message has no installed state transition");
}
static bool beam(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id event,
    qa_buffer *name,qa_actor_id *a,qa_vec3 *start,qa_vec3 *end,double *duration,qa_error *e)
{
    static const char *const names[]={"rail","rail-water","bfg-laser","bfg-zap","bubble-trail","bfg-lightning","heatbeam","monster-heatbeam"};
    const qa_json_document *j=qa_unified_document_json(d); size_t i;
    for (i=0;i<sizeof(names)/sizeof(*names);++i) if (qa_json_string_equal(j,get(j,event,"effect"),names[i])) break;
    if (i==sizeof(names)/sizeof(*names)) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 beam has no actual source recipe");
    return string(d,get(j,event,"effect"),name,e) && actor(o,d,get(j,event,"actor"),a,e) &&
        vector(d,get(j,event,"start"),start,e) && vector(d,get(j,event,"end"),end,e) &&
        number(d,get(j,event,"duration"),duration,e);
}
static bool muzzle(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,
    qa_actor_id *a,int32_t *flash,bool *monster,bool *silenced,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id event=get(j,row,"event");
    *monster=qa_json_string_equal(j,get(j,event,"kind"),"monster-muzzleflash"); *silenced=false;
    if (!actor(o,d,get(j,event,"actor"),a,e) || !a->registry || !integer(d,get(j,event,"flash"),flash,e) || *flash<0) return false;
    if (!*monster) return qa_json_bool(j,get(j,event,"silenced"),silenced,e);
    qa_vec3 origin,direction;
    if (!vector(d,get(j,event,"origin"),&origin,e) || !vector(d,get(j,event,"direction"),&direction,e)) return false;
    qa_json_id angles=get(j,event,"angles"),scale=get(j,event,"scale");
    bool has_angles=angles!=QA_JSON_NONE,has_scale=scale!=QA_JSON_NONE;
    if (has_angles!=has_scale) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 monster muzzle has a partial source pose");
    if (has_angles) { qa_vec3 pose_angles; float pose_scale;
        return vector(d,angles,&pose_angles,e) && real(d,scale,&pose_scale,e); }
    return true;
}
typedef struct q2_received_sound {
    qa_buffer path;
    qa_actor_id actor;
    qa_vec3 origin;
    int32_t channel;
    float volume,attenuation;
    unsigned loop;
} q2_received_sound;
static bool sound_record(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id event,q2_received_sound *s,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id loop=get(j,event,"loop");
    if (qa_json_string_equal(j,loop,"once")) s->loop=0;
    else if (qa_json_string_equal(j,loop,"start")) s->loop=1;
    else if (qa_json_string_equal(j,loop,"stop")) s->loop=2;
    else return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 sound has no genuine loop declaration");
    return string(d,get(j,event,"path"),&s->path,e) && actor(o,d,get(j,event,"actor"),&s->actor,e) &&
        vector(d,get(j,event,"origin"),&s->origin,e) && integer(d,get(j,event,"channel"),&s->channel,e) &&
        real(d,get(j,event,"volume"),&s->volume,e) && real(d,get(j,event,"attenuation"),&s->attenuation,e);
}
static bool player_overlay_valid(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id event,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id kind=get(j,event,"kind"); qa_actor_id a;
    if (!actor(o,d,get(j,event,"actor"),&a,e) || !a.registry) return false;
    if (qa_json_string_equal(j,kind,"view")) {
        qa_json_id view=get(j,event,"view"); uint64_t layouts; qa_buffer selected={0}; qa_vec3 offset;
        bool present; qa_scene_vec4 rgba;
        bool okay=qa_json_u64(j,get(j,view,"layouts"),&layouts,e) && vector(d,get(j,view,"gunOffset"),&offset,e);
        if (okay) okay=optional_color(d,get(j,view,"blend"),&present,&rgba,e) &&
            optional_color(d,get(j,view,"damageBlend"),&present,&rgba,e);
        qa_json_id item=get(j,view,"selectedItem");
        if (okay && qa_json_type(j,item)!=QA_JSON_NULL) okay=string(d,item,&selected,e);
        qa_buffer_free(&selected); return okay;
    }
    bool inventory=qa_json_string_equal(j,kind,"inventory");
    qa_json_id rows=get(j,event,inventory?"entries":"rows");
    if (qa_json_type(j,rows)!=QA_JSON_ARRAY) return false;
    if (inventory) { bool visible; qa_buffer selected={0};
        bool okay=qa_json_bool(j,get(j,event,"visible"),&visible,e);
        qa_json_id item=get(j,event,"selected");
        if (okay && qa_json_type(j,item)!=QA_JSON_NULL) okay=string(d,item,&selected,e);
        qa_buffer_free(&selected); if (!okay) return false;
    }
    for (size_t i=0;i<qa_json_size(j,rows);++i) {
        qa_json_id r=qa_json_at(j,rows,i); qa_buffer name={0}; double value; bool spectator;
        bool okay=string(d,get(j,r,inventory?"item":"name"),&name,e); qa_buffer_free(&name);
        if (inventory) okay=okay && number(d,get(j,r,"count"),&value,e) && number(d,get(j,r,"capacity"),&value,e);
        else okay=okay && number(d,get(j,r,"score"),&value,e) && number(d,get(j,r,"ping"),&value,e) &&
            number(d,get(j,r,"minutes"),&value,e) && qa_json_bool(j,get(j,r,"spectator"),&spectator,e);
        if (!okay) return false;
    }
    return true;
}
static bool player_overlay(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id event=get(j,row,"event"),kind=get(j,event,"kind");
    qa_actor_id a,viewer; uint32_t number_id;
    if (!player_overlay_valid(o,d,event,e) || !actor(o,d,get(j,event,"actor"),&a,e) ||
        !frontend_remote_unified_player(o->replica,&viewer,&number_id)) return false;
    if (!qa_actor_id_equal(a,viewer)) return true;
    q2_activation *owner=NULL;
    if (!activation(o,d,get(j,row,"owner"),&owner,e) || (owner && owner->retired)) return false;
    if (qa_json_string_equal(j,kind,"view")) {
        qa_json_id view=get(j,event,"view"); uint64_t layouts; qa_vec3 gun_offset;
        bool has_blend,has_damage; qa_scene_vec4 blend,damage;
        if (!qa_json_u64(j,get(j,view,"layouts"),&layouts,e) || !vector(d,get(j,view,"gunOffset"),&gun_offset,e)) return false;
        if (!optional_color(d,get(j,view,"blend"),&has_blend,&blend,e) ||
            !optional_color(d,get(j,view,"damageBlend"),&has_damage,&damage,e)) return false;
        qa_buffer provider={0},content={0}; frontend_remote_q2_effects_profile profile;
        bool declared=semantic_source(d,row,false,&provider,&profile,e) && string(d,get(j,row,"content"),&content,e);
        if (!declared) { qa_buffer_free(&provider); qa_buffer_free(&content); return false; }
        if (!(layouts&2)) inventory_clear(o);
        else { qa_buffer selected={0}; qa_json_id item=get(j,view,"selectedItem");
            bool okay=qa_json_type(j,item)==QA_JSON_NULL || string(d,item,&selected,e);
            if (!okay) { qa_buffer_free(&selected); qa_buffer_free(&provider); qa_buffer_free(&content); return false; }
            for (size_t i=0;i<o->item_count;++i) o->items[i].selected=selected.data && !strcmp(o->items[i].item,(const char *)selected.data);
            qa_buffer_free(&selected); }
        if (!(layouts&1)) { o->help_visible=false; o->score_visible=false; }
        free(o->view_content); free(o->view_provider);
        o->view_content=(char *)content.data; o->view_provider=(char *)provider.data;
        o->view_profile=profile; o->view_owner=owner;
        o->view_layouts=layouts;
        o->view_gun_offset=gun_offset;
        o->view_actor=a; o->view_blend_present=has_blend; o->view_damage_present=has_damage;
        o->view_blend=blend; o->view_damage_blend=damage;
        return true;
    }
    qa_json_id rows=get(j,event,qa_json_string_equal(j,kind,"inventory")?"entries":"rows"); size_t count=qa_json_size(j,rows);
    if (qa_json_string_equal(j,kind,"inventory")) {
        bool visible; if (!qa_json_bool(j,get(j,event,"visible"),&visible,e)) return false;
        if (!visible) { inventory_clear(o); return true; }
        if (count>SIZE_MAX/sizeof(q2_inventory_row)) return false;
        q2_inventory_row *items=count?calloc(count,sizeof(*items)):NULL; size_t used=0; qa_buffer selected={0};
        qa_json_id item=get(j,event,"selected");
        bool okay=(!count || items) && (qa_json_type(j,item)==QA_JSON_NULL || string(d,item,&selected,e));
        for (size_t i=0;okay && i<count;++i) {
            qa_json_id r=qa_json_at(j,rows,i); double value;
            okay=number(d,get(j,r,"count"),&value,e); if (!okay || value<=0) continue;
            q2_inventory_row *v=items+used++; qa_buffer name={0};
            okay=string(d,get(j,r,"item"),&name,e); if (!okay) break;
            v->item=(char *)name.data; v->count=value; v->selected=selected.data && !strcmp(v->item,(const char *)selected.data);
            const char *label=!strncmp(v->item,"q2:",3)?v->item+3:v->item;
            qa_json_id labels=get(j,event,"labels"); qa_buffer actual={0};
            for (size_t k=0;okay && k<qa_json_size(j,labels);++k) {
                qa_json_id entry=qa_json_at(j,labels,k);
                if (qa_json_string_equal(j,get(j,entry,"item"),v->item)) { okay=string(d,get(j,entry,"name"),&actual,e); break; }
            }
            if (actual.data) { v->label=(char *)actual.data; }
            else { v->label=malloc(strlen(label)+1); okay=okay && v->label;
                if (v->label) { strcpy(v->label,label); for (char *p=v->label;*p;++p) if (*p=='_') *p=' '; } }
        }
        qa_buffer_free(&selected);
        if (!okay) { for (size_t i=0;i<used;++i) { free(items[i].item); free(items[i].label); } free(items); return false; }
        inventory_clear(o); o->items=items; o->item_count=used; o->inventory_visible=true; o->inventory_owner=owner; o->help_visible=false; return true;
    }
    if (count>SIZE_MAX/sizeof(char *)) return false;
    char **scores=count?calloc(count,sizeof(*scores)):NULL; bool okay=!count || scores;
    for (size_t i=0;okay && i<count;++i) {
        qa_json_id r=qa_json_at(j,rows,i); qa_buffer name={0}; double score,ping,minutes; bool spectator;
        char s[32],p[32],m[32];
        okay=string(d,get(j,r,"name"),&name,e) && number(d,get(j,r,"score"),&score,e) && number(d,get(j,r,"ping"),&ping,e) &&
            number(d,get(j,r,"minutes"),&minutes,e) && qa_json_bool(j,get(j,r,"spectator"),&spectator,e) &&
            qa_format_ecmascript_number(score,s,e) && qa_format_ecmascript_number(ping,p,e) && qa_format_ecmascript_number(minutes,m,e);
        if (okay && name.size<=SIZE_MAX-128) {
            scores[i]=malloc(name.size+128); okay=scores[i]!=NULL;
            if (okay) snprintf(scores[i],name.size+128,"%s  %s  %sms  %sm%s",s,(const char *)name.data,p,m,spectator?"  Spectator":"");
        } else okay=false;
        qa_buffer_free(&name);
    }
    if (!okay) { for (size_t i=0;i<count;++i) free(scores[i]); free(scores); return false; }
    scores_clear(o); o->score_rows=scores; o->score_count=count; o->score_visible=true; o->score_owner=owner;
    o->help_visible=false; inventory_clear(o); return true;
}
static float fog_fraction(float value)
{
    float scaled=value*255;
    if (!isfinite(scaled)) return 0;
    double word=fmod(trunc((double)scaled),256);
    if (word<0) word+=256;
    return (float)(word/255);
}
static qa_vec3 fog_color(qa_vec3 color)
{ return qa_v3(fog_fraction(color.x),fog_fraction(color.y),fog_fraction(color.z)); }
static bool fog_record(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id event,
    qa_scene_fog *out,double *duration,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_actor_id id;
    qa_json_id value=get(j,event,"value"),fog=get(j,value,"fog"),height=get(j,value,"heightFog");
    qa_scene_fog result={.kind=QA_FOG_Q2}; int32_t start,end;
    if (!actor(o,d,get(j,event,"actor"),&id,e) || !id.registry ||
        !vector(d,get(j,fog,"color"),&result.color,e) || !real(d,get(j,fog,"density"),&result.density,e) ||
        !real(d,get(j,fog,"skyFactor"),&result.sky_factor,e) ||
        !vector(d,get(j,height,"startColor"),&result.height_color,e) ||
        !vector(d,get(j,height,"endColor"),&result.height_end_color,e) ||
        !integer(d,get(j,height,"startDistance"),&start,e) || !integer(d,get(j,height,"endDistance"),&end,e) ||
        !real(d,get(j,height,"falloff"),&result.height_falloff,e) || !real(d,get(j,height,"density"),&result.height_density,e) ||
        !number(d,get(j,event,"transitionMilliseconds"),duration,e)) return false;
    result.color=fog_color(result.color); result.height_color=fog_color(result.height_color);
    result.height_end_color=fog_color(result.height_end_color); result.sky_factor=fog_fraction(result.sky_factor);
    result.height_start=(float)start; result.height_end=(float)end; *out=result; return true;
}
static qa_scene_fog fog_sample(frontend_unified_q2 *o,double seconds)
{
    double elapsed=seconds*1000-o->fog_started_ms;
    double front=o->fog_duration_ms==0 || elapsed>o->fog_duration_ms?1:elapsed/o->fog_duration_ms,back=1-front;
    qa_scene_fog a=o->fog_start,b=o->fog_target,result={.kind=QA_FOG_Q2};
    result.color=qa_v3((float)(a.color.x*back+b.color.x*front),(float)(a.color.y*back+b.color.y*front),(float)(a.color.z*back+b.color.z*front));
    result.height_color=qa_v3((float)(a.height_color.x*back+b.height_color.x*front),
        (float)(a.height_color.y*back+b.height_color.y*front),(float)(a.height_color.z*back+b.height_color.z*front));
    result.height_end_color=qa_v3((float)(a.height_end_color.x*back+b.height_end_color.x*front),
        (float)(a.height_end_color.y*back+b.height_end_color.y*front),(float)(a.height_end_color.z*back+b.height_end_color.z*front));
    result.density=(float)(a.density*back+b.density*front); result.sky_factor=(float)(a.sky_factor*back+b.sky_factor*front);
    result.height_start=(float)(a.height_start*back+b.height_start*front); result.height_end=(float)(a.height_end*back+b.height_end*front);
    result.height_falloff=(float)(a.height_falloff*back+b.height_falloff*front);
    result.height_density=(float)(a.height_density*back+b.height_density*front); return result;
}
static bool sky_record(const qa_unified_document *d,qa_json_id event,qa_buffer *name,float *rotation,
    bool *auto_rotate,qa_vec3 *axis,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    return string(d,get(j,event,"name"),name,e) && real(d,get(j,event,"rotation"),rotation,e) &&
        qa_json_bool(j,get(j,event,"autoRotate"),auto_rotate,e) && vector(d,get(j,event,"axis"),axis,e);
}
static bool sky_receive(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_buffer name={0},content={0};
    float rotation; bool auto_rotate; qa_vec3 axis; q2_activation *owner=NULL; q2_bank *b=NULL;
    qa_scene_image *images[6]={0};
    bool okay=sky_record(d,get(j,row,"event"),&name,&rotation,&auto_rotate,&axis,e) &&
        string(d,get(j,row,"content"),&content,e) && activation(o,d,get(j,row,"owner"),&owner,e) &&
        !(owner && owner->retired) && bank(o,(const char *)content.data,NULL,NULL,0,false,&b,e);
    static const char *const suffixes[]={"rt","lf","bk","ft","up","dn"};
    qa_scene_image_options options={.family=QA_SCENE_Q2,.usage=QA_IMAGE_USAGE_SKY,.wrap=QA_SCENE_CLAMP,
        .filter=QA_SCENE_LINEAR,.transparent_index=-1};
    if (okay && name.size>SIZE_MAX-7) okay=false;
    char *path=okay?malloc(name.size+7):NULL;
    if (okay && !path) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining received Q2 sky path");
    for (size_t i=0;okay && i<6;++i) {
        snprintf(path,name.size+7,"env/%s%s",(const char *)name.data,suffixes[i]);
        okay=qa_scene_image_load(b->images,path,&options,images+i,e);
        if (okay && !images[i]) { images[i]=(qa_scene_image *)qa_scene_missing(b->images); qa_scene_image_retain(images[i]); }
    }
    free(path);
    if (okay) { sky_clear(o); o->sky_name=(char *)name.data; name=(qa_buffer){0};
        o->sky_content=(char *)content.data; content=(qa_buffer){0}; o->sky_rotation=rotation;
        o->sky_axis=axis; o->sky_auto_rotate=rotation!=0 && auto_rotate; o->sky_owner=owner;
        memcpy(o->sky_images,images,sizeof(images)); memset(images,0,sizeof(images)); }
    for (size_t i=0;i<6;++i) qa_scene_image_release(images[i]);
    qa_buffer_free(&name); qa_buffer_free(&content); return okay;
}
typedef enum q2_persistent_kind {
    Q2_PERSISTENT_BEAM, Q2_PERSISTENT_MONSTER_BEAM, Q2_PERSISTENT_SHADOW,
    Q2_PERSISTENT_LIGHT, Q2_PERSISTENT_FLASHLIGHT
} q2_persistent_kind;
typedef struct q2_persistent {
    q2_persistent_kind kind;
    qa_actor_id actor;
    qa_vec3 start,end,color;
    float width,radius;
    uint32_t packed_color;
    bool visible;
    int32_t hand;
    frontend_remote_q2_effects_shadow_light shadow;
} q2_persistent;
static bool persistent_known(const qa_unified_document *d,qa_json_id row)
{
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id family=get(j,row,"kind"),kind=get(j,get(j,row,"event"),"kind");
    return (qa_json_string_equal(j,family,"q2") &&
        (qa_json_string_equal(j,kind,"beam") || qa_json_string_equal(j,kind,"monster-beam") ||
            qa_json_string_equal(j,kind,"dynamic-light"))) ||
        (qa_json_string_equal(j,family,"q2-rerelease") &&
            (qa_json_string_equal(j,kind,"flashlight") || qa_json_string_equal(j,kind,"dynamic-light")));
}
static bool persistent_record(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,
    q2_persistent *out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id event=get(j,row,"event"),kind=get(j,event,"kind");
    q2_persistent p={0};
    if (!actor(o,d,get(j,event,"actor"),&p.actor,e)) return false;
    if (!p.actor.registry)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Persistent Q2 effect requires its full actor");
    if (qa_json_string_equal(j,kind,"beam") || qa_json_string_equal(j,kind,"monster-beam")) {
        if (!vector(d,get(j,event,"start"),&p.start,e) || !vector(d,get(j,event,"end"),&p.end,e)) return false;
        if (qa_json_string_equal(j,kind,"beam")) {
            uint64_t color;
            p.kind=Q2_PERSISTENT_BEAM;
            if (!real(d,get(j,event,"width"),&p.width,e) ||
                !qa_json_u64(j,get(j,event,"color"),&color,e) || color>UINT32_MAX ||
                !qa_json_bool(j,get(j,event,"visible"),&p.visible,e)) return false;
            p.packed_color=(uint32_t)color;
        } else {
            p.kind=Q2_PERSISTENT_MONSTER_BEAM;
            if (!qa_json_string_equal(j,get(j,event,"effect"),"parasite") &&
                !qa_json_string_equal(j,get(j,event,"effect"),"medic"))
                return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Unknown Q2 monster beam recipe");
        }
    } else if (qa_json_string_equal(j,kind,"flashlight")) {
        p.kind=Q2_PERSISTENT_FLASHLIGHT;
        static const struct { const char *name; int32_t offset; } hands[]={
            {"right",1},{"left",-1},{"center",0}};
        bool found=false;
        for (size_t i=0;i<3;++i) if (qa_json_string_equal(j,get(j,event,"hand"),hands[i].name)) {
            p.hand=hands[i].offset; found=true; break;
        }
        if (!found || !qa_json_bool(j,get(j,event,"enabled"),&p.visible,e))
            return frontend_unified_fail(e,QA_ERROR_FORMAT,"Invalid Q2 flashlight hand or state");
    } else if (qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease")) {
        p.kind=Q2_PERSISTENT_LIGHT;
        if (!vector(d,get(j,event,"origin"),&p.start,e) || !vector(d,get(j,event,"color"),&p.color,e) ||
            !real(d,get(j,event,"radius"),&p.radius,e) || !qa_json_bool(j,get(j,event,"visible"),&p.visible,e)) return false;
    } else {
        p.kind=Q2_PERSISTENT_SHADOW;
        p.shadow.actor=p.actor;
        if (!vector(d,get(j,event,"origin"),&p.shadow.origin,e) ||
            !vector(d,get(j,event,"color"),&p.shadow.color,e) ||
            !real(d,get(j,event,"radius"),&p.shadow.radius,e) ||
            !real(d,get(j,event,"intensity"),&p.shadow.intensity,e) ||
            !integer(d,get(j,event,"resolution"),&p.shadow.resolution,e) ||
            !real(d,get(j,event,"fadeStart"),&p.shadow.fade_start,e) ||
            !real(d,get(j,event,"fadeEnd"),&p.shadow.fade_end,e) ||
            !integer(d,get(j,event,"lightstyle"),&p.shadow.lightstyle,e) ||
            !qa_json_bool(j,get(j,event,"visible"),&p.shadow.visible,e)) return false;
        if (p.shadow.resolution<0 || p.shadow.lightstyle< -1)
            return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 shadow light has an invalid resolution or style");
        qa_json_id cone=get(j,event,"cone");
        p.shadow.cone=qa_json_type(j,cone)!=QA_JSON_NULL;
        if (p.shadow.cone && (!vector(d,get(j,cone,"direction"),&p.shadow.direction,e) ||
            !real(d,get(j,cone,"cosHalfAngle"),&p.shadow.cos_half_angle,e))) return false;
        if (p.shadow.cone && (p.shadow.cos_half_angle< -1 || p.shadow.cos_half_angle>1))
            return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 shadow cone exceeds its Source cosine domain");
    }
    *out=p; return true;
}
static bool persistent_receive(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,
    double seconds,qa_error *e)
{
    q2_persistent p; qa_buffer content={0}; q2_bank *b=NULL;
    bool okay=persistent_record(o,d,row,&p,e) && string(d,get(qa_unified_document_json(d),row,"content"),&content,e) &&
        event_bank(o,d,row,(const char *)content.data,&b,e);
    if (okay) {
        ++o->busy;
        frontend_remote_q2_effects_presentation_kind replaced=FRONTEND_REMOTE_Q2_ORDINARY_BEAM;
        switch (p.kind) {
        case Q2_PERSISTENT_BEAM:
            replaced=p.visible?FRONTEND_REMOTE_Q2_ORDINARY_BEAM:FRONTEND_REMOTE_Q2_ALL_BEAMS;
            okay=frontend_remote_q2_effects_source_beam(b->effects,p.actor,p.start,p.end,p.width,p.packed_color,p.visible,e); break;
        case Q2_PERSISTENT_MONSTER_BEAM:
            replaced=FRONTEND_REMOTE_Q2_MONSTER_BEAM;
            okay=frontend_remote_q2_effects_monster_beam(b->effects,p.actor,p.start,p.end,seconds*1000,e); break;
        case Q2_PERSISTENT_SHADOW:
            replaced=FRONTEND_REMOTE_Q2_SHADOW_LIGHT;
            okay=frontend_remote_q2_effects_shadow_light_set(b->effects,&p.shadow,e); break;
        case Q2_PERSISTENT_LIGHT:
            replaced=FRONTEND_REMOTE_Q2_SOURCE_LIGHT;
            okay=frontend_remote_q2_effects_source_light(b->effects,p.actor,p.start,p.color,p.radius,p.visible,e); break;
        case Q2_PERSISTENT_FLASHLIGHT:
            replaced=FRONTEND_REMOTE_Q2_FLASHLIGHT;
            okay=frontend_remote_q2_effects_flashlight(b->effects,p.actor,p.visible,p.hand,e); break;
        }
        for (q2_bank *previous=o->banks;okay && previous;previous=previous->next)
            if (previous!=b && previous->effects)
                okay=frontend_remote_q2_effects_remove_actor_presentation(previous->effects,p.actor,replaced,e);
        --o->busy;
    }
    qa_buffer_free(&content); return okay;
}
bool frontend_unified_q2_validate(frontend_unified_q2 *o,bool simulation,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    if (!o || !d) return false;
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id event=get(j,row,"event"),kind=get(j,event,"kind");
    if (!simulation) {
        qa_buffer provider={0}; frontend_remote_q2_effects_profile profile;
        bool okay=semantic_source(d,row,false,&provider,&profile,e); qa_buffer_free(&provider);
        if (!okay) return false;
    }
    if (!simulation && frontend_unified_q2_rr_known(d,row))
        return frontend_unified_q2_rr_validate(o->rr_hud,d,row,e);
    if (!simulation && persistent_known(d,row)) {
        qa_buffer provider={0}; frontend_remote_q2_effects_profile profile; q2_persistent p;
        bool okay=semantic_source(d,row,true,&provider,&profile,e) && persistent_record(o,d,row,&p,e);
        if (okay && qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease") &&
            profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE)
            okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Rerelease effect has a different Source profile");
        qa_buffer_free(&provider); return okay;
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease") && qa_json_string_equal(j,kind,"achievement"))
        return achievement(o,d,row,false,e);
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease") && qa_json_string_equal(j,kind,"screen-blend")) {
        qa_actor_id a; qa_scene_vec4 blend; qa_buffer provider={0}; frontend_remote_q2_effects_profile profile;
        bool okay=semantic_source(d,row,true,&provider,&profile,e);
        if (okay && profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE)
            okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Rerelease screen blend has a different Source profile");
        if (okay) okay=actor(o,d,get(j,event,"actor"),&a,e) && a.registry && rgba_read(d,get(j,event,"blend"),&blend,e);
        qa_buffer_free(&provider); return okay;
    }
    if (simulation) {
        qa_json_id payload=get(j,row,"payload");
        if (qa_json_string_equal(j,get(j,payload,"kind"),"message")) return message_valid(d,get(j,payload,"event"),e);
    }
    if (!simulation && (qa_json_string_equal(j,get(j,row,"kind"),"q2-temp-entity") ||
        qa_json_string_equal(j,kind,"effect") || qa_json_string_equal(j,kind,"beam") ||
        qa_json_string_equal(j,kind,"muzzleflash") || qa_json_string_equal(j,kind,"monster-muzzleflash"))) {
        qa_buffer provider={0}; frontend_remote_q2_effects_profile profile;
        bool okay=semantic_source(d,row,true,&provider,&profile,e); qa_buffer_free(&provider); if (!okay) return false;
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2") &&
        (qa_json_string_equal(j,kind,"model") || qa_json_string_equal(j,kind,"visibility") || qa_json_string_equal(j,kind,"entity-event"))) {
        qa_actor_id a; return visual_record(o,d,row,&a,e);
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2-temp-entity")) {
        qa_q2_temp_entity t; qa_net_protocol_id protocol; qa_actor_id actors[7];
        return temporary(o,d,get(j,row,"event"),&protocol,&t,actors,e);
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"effect")) {
        qa_q2_temp_entity t; return effect(d,event,&t,e);
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2-weapon") && qa_json_string_equal(j,kind,"beam")) {
        qa_buffer name={0}; qa_actor_id a; qa_vec3 start,end; double duration;
        bool okay=beam(o,d,event,&name,&a,&start,&end,&duration,e); qa_buffer_free(&name); return okay;
    }
    if (!simulation && (qa_json_string_equal(j,kind,"muzzleflash") || qa_json_string_equal(j,kind,"monster-muzzleflash"))) {
        qa_actor_id a; int32_t flash; bool monster,silenced;
        return muzzle(o,d,row,&a,&flash,&monster,&silenced,e);
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"sound")) {
        q2_received_sound s={0}; bool okay=sound_record(o,d,event,&s,e); qa_buffer_free(&s.path); return okay;
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"pickup")) {
        qa_actor_id a; qa_buffer name={0},icon={0},item={0};
        bool okay=actor(o,d,get(j,event,"player"),&a,e) && a.registry && string(d,get(j,event,"name"),&name,e) &&
            string(d,get(j,event,"icon"),&icon,e) && string(d,get(j,event,"item"),&item,e);
        qa_buffer_free(&name); qa_buffer_free(&icon); qa_buffer_free(&item); return okay;
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"lightstyle")) {
        qa_buffer pattern={0},provider={0}; int32_t style; frontend_remote_q2_effects_profile profile;
        bool okay=integer(d,get(j,event,"style"),&style,e) && style>=0 && style<256 &&
            string(d,get(j,event,"pattern"),&pattern,e) && semantic_source(d,row,true,&provider,&profile,e);
        qa_buffer_free(&pattern); qa_buffer_free(&provider); return okay;
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"music")) {
        qa_buffer track={0},provider={0}; frontend_remote_q2_effects_profile profile;
        bool okay=string(d,get(j,event,"track"),&track,e) && semantic_source(d,row,true,&provider,&profile,e);
        qa_buffer_free(&track); qa_buffer_free(&provider); return okay;
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2-player") && qa_json_string_equal(j,kind,"help")) {
        qa_actor_id a; bool visible; return actor(o,d,get(j,event,"actor"),&a,e) && qa_json_bool(j,get(j,event,"visible"),&visible,e);
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2-player") && qa_json_string_equal(j,kind,"userinfo"))
        return userinfo(o,d,event,false,e);
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2-player") && qa_json_string_equal(j,kind,"stufftext")) {
        qa_actor_id a; qa_buffer text={0};
        bool okay=o->replica->options.command_text && actor(o,d,get(j,event,"actor"),&a,e) && a.registry &&
            string(d,get(j,event,"text"),&text,e);
        qa_buffer_free(&text); return okay;
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease") && qa_json_string_equal(j,kind,"localized-print")) {
        qa_actor_id a; qa_buffer text={0}; qa_json_id args=get(j,event,"args");
        bool okay=actor(o,d,get(j,event,"actor"),&a,e) && string(d,get(j,event,"text"),&text,e) &&
            qa_json_type(j,args)==QA_JSON_ARRAY;
        qa_buffer_free(&text);
        static const char *const levels[]={"low","medium","high","chat"}; bool known=false;
        for (size_t i=0;i<4;++i) if (qa_json_string_equal(j,get(j,event,"level"),levels[i])) known=true;
        for (size_t i=0;okay && i<qa_json_size(j,args);++i) {
            qa_buffer arg={0}; okay=string(d,qa_json_at(j,args,i),&arg,e); qa_buffer_free(&arg);
        }
        return okay && known;
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease")) {
        if (qa_json_string_equal(j,kind,"fog")) { qa_scene_fog fog; double duration; return fog_record(o,d,event,&fog,&duration,e); }
        if (qa_json_string_equal(j,kind,"sky")) { qa_buffer name={0}; float rotation; bool auto_rotate; qa_vec3 axis;
            bool okay=sky_record(d,event,&name,&rotation,&auto_rotate,&axis,e); qa_buffer_free(&name); return okay; }
        if (qa_json_string_equal(j,kind,"story")) { qa_buffer text={0};
            bool okay=string(d,get(j,event,"text"),&text,e); qa_buffer_free(&text); return okay; }
    }
    if (!simulation && qa_json_string_equal(j,get(j,row,"kind"),"q2-player") &&
        (qa_json_string_equal(j,kind,"inventory") || qa_json_string_equal(j,kind,"scoreboard") || qa_json_string_equal(j,kind,"view")))
        return player_overlay_valid(o,d,event,e);
    if (!simulation && (qa_json_string_equal(j,kind,"centerprint") || qa_json_string_equal(j,kind,"print") ||
        (qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"help")))) {
        qa_buffer text={0}; bool okay=string(d,get(j,event,"text"),&text,e);
        if (okay && qa_json_string_equal(j,kind,"help")) { int32_t slot;
            okay=integer(d,get(j,event,"slot"),&slot,e) && slot>=1 && slot<=2; }
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
    if (frontend_unified_q2_rr_known(d,row)) {
        bool okay=frontend_unified_q2_rr_presentation(o->rr_hud,d,row,mirrored,e);
        if (okay && qa_json_string_equal(j,kind,"help-computer") && frontend_unified_q2_rr_help_visible(o->rr_hud)) {
            inventory_clear(o); scores_clear(o); o->help_visible=false;
        }
        return okay;
    }
    double seconds;
    if (!number(d,get(j,row,"seconds"),&seconds,e)) return false;
    if (persistent_known(d,row)) return persistent_receive(o,d,row,seconds,e);
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease") && qa_json_string_equal(j,kind,"screen-blend")) {
        qa_actor_id a; qa_scene_vec4 blend; qa_buffer provider={0}; frontend_remote_q2_effects_profile profile;
        q2_activation *owner=NULL;
        bool okay=frontend_unified_q2_validate(o,false,d,row,e) && actor(o,d,get(j,event,"actor"),&a,e) &&
            rgba_read(d,get(j,event,"blend"),&blend,e) && semantic_source(d,row,true,&provider,&profile,e) &&
            activation(o,d,get(j,row,"owner"),&owner,e) && !(owner && owner->retired);
        if (okay && qa_actor_id_equal(a,o->view_actor) && o->view_provider &&
            !strcmp(o->view_provider,(const char *)provider.data) && o->view_owner==owner && o->view_profile==profile) {
            o->view_blend=blend; o->view_blend_present=true;
        }
        qa_buffer_free(&provider); return okay;
    }
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease") && qa_json_string_equal(j,kind,"achievement"))
        return achievement(o,d,row,true,e);
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"music")) return music_receive(o,d,row,e);
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2-player") && qa_json_string_equal(j,kind,"userinfo"))
        return userinfo(o,d,event,true,e);
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2-player") && qa_json_string_equal(j,kind,"stufftext")) {
        qa_actor_id a,player; uint32_t source_number; qa_buffer text={0};
        bool okay=actor(o,d,get(j,event,"actor"),&a,e) &&
            frontend_remote_unified_player(o->replica,&player,&source_number) &&
            string(d,get(j,event,"text"),&text,e);
        if (okay && qa_actor_id_equal(a,player)) okay=frontend_remote_unified_command_text(o->replica,(const char *)text.data,e);
        qa_buffer_free(&text); *mirrored=okay; return okay;
    }
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease")) {
        if (qa_json_string_equal(j,kind,"sky")) return sky_receive(o,d,row,e);
        if (qa_json_string_equal(j,kind,"story")) {
            qa_unified_document *story=NULL; q2_activation *owner=NULL;
            if (!activation(o,d,get(j,row,"owner"),&owner,e) || (owner && owner->retired) ||
                !qa_unified_document_create(QA_UNIFIED_CHECKPOINT,qa_json_source(j,row),&story,e)) return false;
            qa_unified_document_destroy(o->story); o->story=story; o->story_owner=owner; return true;
        }
        if (qa_json_string_equal(j,kind,"fog")) {
            qa_scene_fog fog; double duration; qa_actor_id a,viewer; uint32_t number_id; q2_activation *owner=NULL;
            if (!fog_record(o,d,event,&fog,&duration,e) || !actor(o,d,get(j,event,"actor"),&a,e) ||
                !frontend_remote_unified_player(o->replica,&viewer,&number_id)) return false;
            if (!qa_actor_id_equal(a,viewer)) return true;
            if (!activation(o,d,get(j,row,"owner"),&owner,e) || (owner && owner->retired)) return false;
            if (duration!=0) { o->fog_start=o->fog_target; o->fog_started_ms=seconds*1000; }
            o->fog_target=fog; o->fog_duration_ms=duration; o->fog_received=true; o->fog_owner=owner; return true;
        }
    }
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"lightstyle")) {
        qa_buffer content={0},pattern={0}; int32_t style; uint64_t sequence; q2_bank *b=NULL;
        bool okay=integer(d,get(j,event,"style"),&style,e) && style>=0 && style<256 &&
            qa_json_u64(j,get(j,row,"sequence"),&sequence,e) && string(d,get(j,row,"content"),&content,e) &&
            string(d,get(j,event,"pattern"),&pattern,e) && source_bank(o,d,row,(const char *)content.data,false,&b,e);
        if (okay) { free(b->styles[style]); b->styles[style]=(char *)pattern.data; pattern=(qa_buffer){0}; b->style_sequences[style]=sequence; }
        qa_buffer_free(&content); qa_buffer_free(&pattern); return okay;
    }
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"pickup")) {
        qa_actor_id a,viewer; uint32_t number_id;
        if (!actor(o,d,get(j,event,"player"),&a,e) || !frontend_remote_unified_player(o->replica,&viewer,&number_id)) return false;
        if (!qa_actor_id_equal(a,viewer)) return true;
        qa_buffer content={0},name={0},icon={0}; q2_bank *b=NULL; qa_scene_image *image=NULL;
        qa_scene_image_options opts={.family=QA_SCENE_Q2,.usage=QA_IMAGE_USAGE_PICTURE,.wrap=QA_SCENE_CLAMP,
            .filter=QA_SCENE_LINEAR,.transparent=true,.transparent_index=255};
        bool okay=string(d,get(j,row,"content"),&content,e) && string(d,get(j,event,"name"),&name,e) &&
            string(d,get(j,event,"icon"),&icon,e) && bank(o,(const char *)content.data,NULL,NULL,0,false,&b,e);
        if (okay && icon.size) okay=qa_scene_image_load_exact(b->images,(const char *)icon.data,&opts,&image,e);
        if (okay) okay=qa_hud_pickup(o->hud,(const char *)name.data,image,nanoseconds(seconds+3),e);
        qa_scene_image_release(image); qa_buffer_free(&content); qa_buffer_free(&name); qa_buffer_free(&icon); return okay;
    }
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2-player") &&
        (qa_json_string_equal(j,kind,"inventory") || qa_json_string_equal(j,kind,"scoreboard") || qa_json_string_equal(j,kind,"view")))
        return player_overlay(o,d,row,e) && frontend_unified_q2_rr_overlay(o->rr_hud,d,row,e);
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2") &&
        (qa_json_string_equal(j,kind,"model") || qa_json_string_equal(j,kind,"visibility") || qa_json_string_equal(j,kind,"entity-event")))
        return visual_apply(o,d,row,e);
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"sound")) {
        q2_received_sound s={0}; qa_buffer content={0};
        bool okay=sound_record(o,d,event,&s,e) && string(d,get(j,row,"content"),&content,e);
        q2_loop *retained=NULL; q2_activation *loop_owner=NULL;
        if (okay && s.loop==1) {
            okay=activation(o,d,get(j,row,"owner"),&loop_owner,e) && !(loop_owner && loop_owner->retired);
            if (okay) { retained=calloc(1,sizeof(*retained)); okay=retained!=NULL;
                if (okay) { retained->actor=s.actor; retained->activation=loop_owner; } }
        }
        bool paired=false;
        if (okay && !s.loop) okay=frontend_unified_events_sound_mirrored(o->events,d,row,&paired,e);
        if (okay && s.loop==2) okay=frontend_unified_events_sound_stop_loop(o->events,s.actor,e);
        else if (okay && s.loop==1) {
            uint32_t raw=(uint32_t)o->frame_number; int32_t frame; memcpy(&frame,&raw,sizeof(frame));
            okay=frontend_unified_events_sound_loop_path(o->events,(const char *)content.data,(const char *)s.path.data,
                s.actor,s.origin,qa_v3(0,0,0),seconds*1000,s.channel,s.volume,s.attenuation,frame,true,e);
        } else if (okay && !paired) okay=frontend_unified_events_sound_path(o->events,(const char *)content.data,
            (const char *)s.path.data,s.actor,s.origin,seconds*1000,s.channel,s.volume,s.attenuation,0,e);
        if (okay && s.loop) {
            q2_loop **next=&o->loops;
            while (*next) { q2_loop *l=*next;
                if (qa_actor_id_equal(l->actor,s.actor)) { *next=l->next; free(l); } else next=&l->next; }
            if (retained) { retained->next=o->loops; o->loops=retained; retained=NULL; }
        }
        free(retained);
        qa_buffer_free(&content); qa_buffer_free(&s.path); return okay;
    }
    if (qa_json_string_equal(j,kind,"muzzleflash") || qa_json_string_equal(j,kind,"monster-muzzleflash")) {
        qa_actor_id a; int32_t flash,number_id=-1; bool monster,silenced; qa_buffer content={0}; q2_bank *b=NULL;
        bool okay=muzzle(o,d,row,&a,&flash,&monster,&silenced,e) && string(d,get(j,row,"content"),&content,e) &&
            event_bank(o,d,row,(const char *)content.data,&b,e);
        qa_json_id id=get(j,row,"sourceEntity");
        if (okay && id!=QA_JSON_NONE && qa_json_type(j,id)!=QA_JSON_NULL) okay=integer(d,id,&number_id,e);
        if (okay && o->muzzle_count==SIZE_MAX/sizeof(*o->muzzles)) okay=false;
        if (okay) { void *p=realloc(o->muzzles,(o->muzzle_count+1)*sizeof(*o->muzzles));
            if (!p) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q2 muzzle delivery witness"); else o->muzzles=p; }
        if (okay) okay=alias(b,number_id,a,e);
        if (okay) { ++o->busy;
            if (monster) {
                qa_vec3 origin,direction;
                okay=vector(d,get(j,event,"origin"),&origin,e) && vector(d,get(j,event,"direction"),&direction,e);
                if (okay && get(j,event,"angles")!=QA_JSON_NONE) { qa_vec3 angles; float scale;
                    okay=vector(d,get(j,event,"angles"),&angles,e) && real(d,get(j,event,"scale"),&scale,e) &&
                        frontend_remote_q2_effects_monster_muzzle_pose(b->effects,a,(uint32_t)flash,origin,angles,scale,seconds*1000,seconds*1000,e);
                } else if (okay) okay=frontend_remote_q2_effects_monster_muzzle(b->effects,a,(uint32_t)flash,origin,direction,seconds*1000,seconds*1000,e);
            } else okay=frontend_remote_q2_effects_actor_muzzle(b->effects,a,(uint32_t)flash,false,silenced,seconds*1000,seconds*1000,e);
            --o->busy; }
        if (okay) o->muzzles[o->muzzle_count++]=(q2_muzzle_receipt){a,number_id,flash,seconds*1000,monster,false};
        qa_buffer_free(&content); return okay;
    }
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2-weapon") && qa_json_string_equal(j,kind,"beam")) {
        qa_buffer name={0},content={0}; qa_actor_id a; qa_vec3 start,end; double duration; q2_bank *b=NULL;
        bool okay=beam(o,d,event,&name,&a,&start,&end,&duration,e) && string(d,get(j,row,"content"),&content,e) &&
            event_bank(o,d,row,(const char *)content.data,&b,e);
        if (okay) { ++o->busy;
            okay=frontend_remote_q2_effects_named_beam(b->effects,(const char *)name.data,a,start,end,duration,seconds*1000,e);
            --o->busy; }
        qa_buffer_free(&name); qa_buffer_free(&content); return okay;
    }
    bool localized_print=qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease") && qa_json_string_equal(j,kind,"localized-print");
    if (qa_json_string_equal(j,kind,"centerprint") || qa_json_string_equal(j,kind,"print") || localized_print) {
        qa_json_id target=get(j,event,qa_json_string_equal(j,kind,"print")?"target":"actor");
        if (target==QA_JSON_NONE) target=get(j,event,"actor");
        qa_actor_id actual,viewer; uint32_t n;
        if (!actor(o,d,target,&actual,e) || !frontend_remote_unified_player(o->replica,&viewer,&n)) return false;
        if (actual.registry && !qa_actor_id_equal(actual,viewer)) return true;
        bool center=qa_json_string_equal(j,kind,"centerprint");
        /* Source print has an independent simulation console record. */
        if (!received_text(o,d,row,event,seconds,center,!center,e)) return false;
        *mirrored=center || localized_print; return true;
    }
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2-player") && qa_json_string_equal(j,kind,"help")) {
        qa_actor_id a,viewer; uint32_t n; bool visible;
        if (!actor(o,d,get(j,event,"actor"),&a,e) || !qa_json_bool(j,get(j,event,"visible"),&visible,e) ||
            !frontend_remote_unified_player(o->replica,&viewer,&n)) return false;
        if (qa_actor_id_equal(a,viewer)) { o->help_visible=visible;
            if (visible) { inventory_clear(o); o->score_visible=false; } }
        return frontend_unified_q2_rr_overlay(o->rr_hud,d,row,e);
    }
    if (qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"help")) {
        qa_buffer text={0};
        q2_activation *help_owner=NULL;
        int32_t slot;
        if (!integer(d,get(j,event,"slot"),&slot,e) || slot<1 || slot>2 ||
            !activation(o,d,get(j,row,"owner"),&help_owner,e) || (help_owner && help_owner->retired) ||
            !localized(o,d,row,event,&text,e)) return false;
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        size_t n=text.size;
        if (n>SIZE_MAX-2) { qa_buffer_free(&text); return false; }
        char *print=malloc(n+2); if (!print) { qa_buffer_free(&text); return false; }
        memcpy(print,text.data,n); print[n]='\n'; print[n+1]=0;
        free(o->help_text[slot-1]); o->help_text[slot-1]=(char *)text.data; o->help_owner[slot-1]=help_owner;
        qa_console_emit(domain->console,&domain->command_context,print); free(print);
        *mirrored=true; return true;
    }
    bool residual=qa_json_string_equal(j,get(j,row,"kind"),"q2-temp-entity");
    bool normalized=qa_json_string_equal(j,get(j,row,"kind"),"q2") && qa_json_string_equal(j,kind,"effect");
    if (!residual && !normalized) return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 normalized presentation variant has no CLIENT handler");
    qa_q2_temp_entity t; qa_net_protocol_id protocol={0}; qa_actor_id actors[7]={0}; qa_buffer content={0};
    bool okay=(residual?temporary(o,d,event,&protocol,&t,actors,e):effect(d,event,&t,e)) &&
        string(d,get(j,row,"content"),&content,e) && number(d,get(j,row,"seconds"),&seconds,e);
    q2_bank *b=NULL;
    if (okay) okay=event_bank(o,d,row,(const char *)content.data,&b,e);
    qa_buffer_free(&content);
    if (okay && residual && b->profile!=(protocol.kind==QA_NET_Q2KEX_2023?FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE:FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC))
        okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 temporary protocol differs from its declared content program");
    for (size_t i=0;okay && i<t.field_count;++i)
        if (t.fields[i].name==QA_Q2_TEMP_ENTITY1 || t.fields[i].name==QA_Q2_TEMP_ENTITY2)
            okay=alias(b,t.fields[i].value.integer,actors[i],e);
    if (okay) { ++o->busy;
        if (normalized) {
            qa_buffer name={0}; qa_vec3 origin,direction; int32_t count,color;
            okay=string(d,get(j,event,"effect"),&name,e) && vector(d,get(j,event,"origin"),&origin,e) &&
                vector(d,get(j,event,"direction"),&direction,e) && integer(d,get(j,event,"count"),&count,e) &&
                integer(d,get(j,event,"color"),&color,e) &&
                frontend_remote_q2_effects_named_effect(b->effects,(const char *)name.data,origin,direction,count,color,seconds*1000,e);
            qa_buffer_free(&name);
        } else okay=frontend_remote_q2_effects_temporary(b->effects,&t,actors,seconds*1000,seconds*1000,e);
        --o->busy; }
    return okay;
}
bool frontend_unified_q2_simulation(frontend_unified_q2 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    if (!o || !d || o->busy || !current(o,e)) return false;
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id payload=get(j,row,"payload"),event=get(j,payload,"event"),kind=get(j,event,"kind");
    if (!qa_json_string_equal(j,get(j,payload,"kind"),"message") || !message_valid(d,event,e)) return false;
    if (qa_json_string_equal(j,kind,"disconnect")) {
        qa_buffer reason={0};
        bool okay=string(d,get(j,event,"reason"),&reason,e) &&
            frontend_remote_unified_source_disconnect(o->replica,(const char *)reason.data,e);
        qa_buffer_free(&reason); return okay;
    }
    if (qa_json_string_equal(j,kind,"command-text")) {
        qa_buffer text={0};
        bool okay=string(d,get(j,event,"text"),&text,e) &&
            frontend_remote_unified_command_text(o->replica,(const char *)text.data,e);
        qa_buffer_free(&text); return okay;
    }
    if (qa_json_string_equal(j,kind,"q2-muzzle-flash")) {
        int32_t number_id,flash; bool monster; double ms;
        qa_json_id t=get(j,row,"time");
        if (!integer(d,get(j,event,"entityNumber"),&number_id,e) || !integer(d,get(j,event,"flash"),&flash,e) ||
            !qa_json_bool(j,get(j,event,"monster"),&monster,e) || !number(d,get(j,t,"value"),&ms,e)) return false;
        if (qa_json_string_equal(j,get(j,t,"kind"),"seconds")) ms*=1000;
        for (size_t i=0;i<o->muzzle_count;++i) {
            q2_muzzle_receipt *m=o->muzzles+i;
            if (!m->consumed && m->number==number_id && m->flash==flash && m->monster==monster && m->milliseconds==ms) {
                m->consumed=true; return true;
            }
        }
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 simulation muzzle has no exact received presentation witness");
    }
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
    if (!frontend_unified_q2_rr_frame_prepare(o->rr_hud,d,e)) {
        frontend_unified_q2_frame_abort(o); return false;
    }
    o->prepared_seconds=seconds; o->prepared_number=n; return true;
}
void frontend_unified_q2_frame_commit(frontend_unified_q2 *o)
{
    if (!o || !o->prepared_frame || o->busy) return;
    qa_unified_document_destroy(o->frame); o->frame=o->prepared_frame; o->prepared_frame=NULL;
    o->seconds=o->prepared_seconds; o->frame_number=o->prepared_number;
    frontend_unified_q2_rr_frame_commit(o->rr_hud);
    size_t retained=0;
    for (size_t i=0;i<o->muzzle_count;++i) if (!o->muzzles[i].consumed) o->muzzles[retained++]=o->muzzles[i];
    o->muzzle_count=retained;
}
bool frontend_unified_q2_frame_ready(frontend_unified_q2 *o,const qa_unified_document *d,qa_error *e)
{
    if (!o || o->busy || !o->prepared_frame || !d ||
        qa_unified_document_type(d)!=QA_UNIFIED_FRAME_DOCUMENT ||
        (o->hud && !qa_hud_idle(o->hud)) || !current(o,e))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 CLIENT has no returned prepared frame");
    for (q2_bank *b=o->banks;b;b=b->next) if (!frontend_remote_q2_effects_idle(b->effects) || !frontend_received_music_idle(b->music))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 frame retains an effects callback");
    if (!frontend_unified_q2_rr_frame_ready(o->rr_hud,d,e)) return false;
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
{
    if (o && !o->busy) {
        frontend_unified_q2_rr_frame_abort(o->rr_hud);
        qa_unified_document_destroy(o->prepared_frame); o->prepared_frame=NULL;
    }
}
bool frontend_unified_q2_model(frontend_unified_q2 *o,qa_actor_id a,const char *content,const char *path,
    qa_scene_model_input *input,qa_error *e)
{
    if (!o || !input || !content || !path || !current(o,e)) return false;
    if (input->family!=QA_SCENE_Q2 || input->view_model) return true;
    q2_visual *v=visual_read(o,a);
    if (!v || !v->content || strcmp(v->content,content)) return true;
    if (!v->visible) { input->color.w=0; return true; }
    if (!v->model || !v->path || strcmp(v->path,path)) return true;
    const qa_json_document *j=qa_unified_document_json(v->model);
    qa_json_id event=get(j,qa_unified_document_root(v->model),"event"); uint64_t word;
    float scale,alpha;
    if (!qa_json_u64(j,get(j,event,"frame"),&word,e)) return false;
    input->frame=(uint32_t)word;
    if (!qa_json_u64(j,get(j,event,"oldFrame"),&word,e)) return false;
    input->old_frame=(uint32_t)word;
    if (!qa_json_u64(j,get(j,event,"skin"),&word,e)) return false;
    input->skin=(uint32_t)word;
    if (!qa_json_u64(j,get(j,event,"renderFlags"),&word,e)) return false;
    input->flags=(uint32_t)word;
    if (!real(v->model,get(j,event,"scale"),&scale,e) || !real(v->model,get(j,event,"alpha"),&alpha,e)) return false;
    input->color.w=alpha; if (scale == 0) scale=1;
    for (size_t i=0;i<3;++i) input->transform.scale[i]=scale;
    return true;
}
bool frontend_unified_q2_model_after(frontend_unified_q2 *o,qa_actor_id a,const char *content,const char *path,
    const qa_scene_model_input *input,qa_scene_frame *frame,qa_error *e)
{
    if (!o || !content || !path || !input || !frame || !current(o,e)) return false;
    if (input->family!=QA_SCENE_Q2) return true;
    if (input->view_model) {
        qa_actor_id viewer_actor; uint32_t number_id;
        if (!frontend_remote_unified_player(o->replica,&viewer_actor,&number_id) || !qa_actor_id_equal(a,viewer_actor))
            return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 received view weapon changed its actual viewer actor");
        ++o->busy; bool okay=true;
        for (q2_bank *b=o->banks;okay && b;b=b->next)
            if (b->effects && o->view_content && o->view_provider &&
                !strcmp(o->view_content,content) && !strcmp(b->content,content) &&
                b->source_provider && !strcmp(b->source_provider,o->view_provider) &&
                b->profile==o->view_profile && b->activation==o->view_owner && b->materials==input->material_library)
                okay=frontend_remote_q2_effects_weapon_draw(b->effects,viewer_actor,input,frame,e);
        --o->busy; return okay;
    }
    q2_visual *v=visual_read(o,a);
    if (!v || !v->visible || !v->model || !v->content || strcmp(v->content,content) || !v->path || strcmp(v->path,path)) return true;
    q2_bank *b=NULL; if (!bank(o,content,NULL,NULL,0,false,&b,e)) return false;
    const qa_json_document *j=qa_unified_document_json(v->model);
    qa_json_id event=get(j,qa_unified_document_root(v->model),"event"),attached=get(j,event,"attachedModels");
    for (size_t i=0;i<qa_json_size(j,attached);++i) {
        qa_buffer name={0}; qa_scene_model *scene=NULL;
        bool okay=string(v->model,qa_json_at(j,attached,i),&name,e) && model(b,(const char *)name.data,false,&scene,e);
        if (okay && scene) { qa_scene_model_input child=*input;
            for (q2_model *m=b->models;m;m=m->next) if (!strcmp(m->path,(const char *)name.data)) { child.source_path=m->path; break; }
            child.attachments=NULL; child.attachment_count=0; child.skin=0;
            okay=qa_scene_model_submit(scene,&child,frame,e); }
        qa_buffer_free(&name); if (!okay) return false;
    }
    return true;
}
static bool sample_entities(q2_bank *b,frontend_remote_q2_effects_pose **out,size_t *count,qa_error *e)
{
    frontend_unified_q2 *o=b->owner; *out=NULL; *count=0; if (!o->frame) return true;
    const qa_json_document *j=qa_unified_document_json(o->frame);
    qa_json_id models=get(j,qa_unified_document_root(o->frame),"models");
    size_t n=qa_json_size(j,models);
    if (n>SIZE_MAX/sizeof(**out)) return false;
    frontend_remote_q2_effects_pose *rows=n?calloc(n,sizeof(*rows)):NULL;
    if (n && !rows) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 frame effect poses");
    size_t used=0; bool okay=true;
    for (size_t i=0;okay && i<n;++i) {
        qa_json_id row=qa_json_at(j,models,i); qa_actor_id a;
        if (!qa_json_string_equal(j,get(j,row,"family"),"q2") || !qa_json_string_equal(j,get(j,row,"content"),b->content)) continue;
        okay=actor(o,o->frame,get(j,row,"actor"),&a,e); if (!okay) break;
        q2_visual *v=visual_read(o,a);
        if (!v || !v->visible || v->activation!=b->activation || !v->source_provider ||
            !b->source_provider || strcmp(v->source_provider,b->source_provider)) continue;
        bool duplicate=false;
        for (size_t k=0;k<used;++k) if (qa_actor_id_equal(rows[k].actor,a)) { duplicate=true; break; }
        if (duplicate) continue;
        okay=pose_actor(b,a,rows+used,e); if (okay) ++used;
    }
    if (!okay) { free(rows); return false; } *out=rows; *count=used; return true;
}
static bool style_sample(const char *pattern,double seconds,float *out,qa_error *e)
{
    qa_bytes bytes={(const uint8_t *)pattern,strlen(pattern)}; size_t cursor=0,length=0; uint32_t scalar;
    while (qa_utf8_next(bytes,&cursor,&scalar)) length+=scalar>UINT32_C(0xffff)?2:1;
    if (!length) { *out=1; return true; }
    double sample=fmod(floor(seconds*10),(double)length);
    if (!isfinite(sample)) sample=0;
    if (sample<0) {
        frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 lightstyle sampled an absent Source string cell");
        return false;
    }
    size_t target=(size_t)sample,index=0; cursor=0;
    while (qa_utf8_next(bytes,&cursor,&scalar)) {
        uint32_t words[2]={scalar,0}; size_t count=1;
        if (scalar>UINT32_C(0xffff)) { scalar-=UINT32_C(0x10000); words[0]=UINT32_C(0xd800)+(scalar>>10); words[1]=UINT32_C(0xdc00)+(scalar&1023); count=2; }
        for (size_t i=0;i<count;++i,++index) if (index==target) { *out=((float)words[i]-97)/12; return true; }
    }
    frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 lightstyle has no sampled Source string cell");
    return false;
}
bool frontend_unified_q2_world_input(frontend_unified_q2 *o,qa_scene_world_input *input,qa_error *e)
{
    if (!o || !input || o->busy || !current(o,e)) return false;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    qa_executable_recipe *recipe=frontend_remote_unified_recipe(o->replica);
    const qa_recipe_choices *choices=qa_executable_recipe_choices(recipe);
    const qa_product *product=choices?qa_catalog_product(qa_executable_recipe_catalog(recipe),choices->world.presentation):NULL;
    frontend_legacy_render_policy policy;
    if (!domain || !frontend_legacy_render_policy_read_registry(domain->cvars,product,&policy,e) || !current(o,e)) return false;
    input->legacy_policy=policy.lighting;
    input->legacy_flashblend=policy.flashblend;
    input->legacy_texture_sort=policy.texture_sort;
    if (policy.lighting.present) input->view.clear_color=policy.lighting.clear;
    bool present[256]={0}; uint64_t sequences[256]={0};
    for (size_t i=0;i<256;++i) o->sampled_styles[i]=input->q2_styles && i<input->style_count?input->q2_styles[i]:qa_v3(1,1,1);
    for (q2_bank *b=o->banks;b;b=b->next) for (size_t i=0;i<256;++i)
        if (b->styles[i] && (!present[i] || b->style_sequences[i]>=sequences[i])) {
            float value; if (!style_sample(b->styles[i],input->seconds,&value,e)) return false;
            o->sampled_styles[i]=qa_v3(value,value,value); present[i]=true; sequences[i]=b->style_sequences[i];
        }
    input->q2_styles=o->sampled_styles; input->style_count=256;
    if (o->fog_received) input->fog=fog_sample(o,input->seconds);
    if (o->sky_name) {
        input->override_sky=true; input->sky_rotation=o->sky_rotation; input->sky_axis=o->sky_axis;
        input->sky_auto_rotate=o->sky_auto_rotate;
        for (size_t i=0;i<6;++i) input->sky_images[i]=o->sky_images[i];
    }
    return current(o,e);
}
bool frontend_unified_q2_view_origin(frontend_unified_q2 *o,qa_actor_id actor_id,qa_vec3 origin,float player_fov,qa_error *e)
{
    qa_actor_id actual; uint32_t number_id;
    if (!o || o->busy || !qa_vec_finite(origin) || !isfinite(player_fov) || player_fov<=0 || player_fov>=180 || !current(o,e) ||
        !frontend_remote_unified_player(o->replica,&actual,&number_id) || !qa_actor_id_equal(actor_id,actual))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 render origin has no genuine entered CLIENT viewer");
    o->viewer_origin=origin; o->player_fov=player_fov;
    o->viewer_origin_frame=o->frontend->frame_number; o->viewer_origin_present=true; return true;
}
bool frontend_unified_q2_world(frontend_unified_q2 *o,const qa_scene_view *view,const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *e)
{
    if (!o || !view || !frame || o->busy || !current(o,e)) return false;
    return frontend_unified_q2_rr_world(o->rr_hud,view,world,frame,e);
}
static bool retained_world(frontend_unified_q2 *o,const qa_scene_world_input *world,
    bool particles,bool models,qa_scene_frame *frame,qa_error *e)
{
    if (!o || !world || !frame || o->busy || !current(o,e)) return false;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    if (!o->frame || frame!=&o->frontend->frame || !domain || world->view.seat!=domain->physical_seat || world->seconds!=o->seconds)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 effects lost their actual entered CLIENT draw frame");
    if (world->view.mirror && !qa_scene_world_q1_mirror_scope(frontend_unified_media_world(o->media),world,frame))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 reflected effects have no actual entered mirror scope");
    frontend_remote_q2_effects_sample sample={.milliseconds=o->seconds*1000,.server_milliseconds=o->seconds*1000,
        .frame_sequence=o->frame_number,.view=world->view,.world_input=world};
    ++o->busy; bool okay=true;
    for (q2_bank *b=o->banks;okay && b;b=b->next) if (b->effects)
        okay=frontend_remote_q2_effects_draw(b->effects,&sample,particles,models,frame,e);
    --o->busy; return okay && current(o,e);
}
bool frontend_unified_q2_world_models(frontend_unified_q2 *o,const qa_scene_world_input *world,
    qa_scene_frame *frame,qa_error *e)
{ return retained_world(o,world,false,true,frame,e); }
bool frontend_unified_q2_world_particles(frontend_unified_q2 *o,const qa_scene_world_input *world,
    qa_scene_frame *frame,qa_error *e)
{ return retained_world(o,world,true,false,frame,e); }
static bool damage_blend_draw(qa_scene_frame *frame,const qa_scene_image *white,qa_scene_rect viewport,
    qa_scene_vec4 rgba,qa_error *e)
{
    if (rgba.w==0 || !viewport.width || !viewport.height) return true;
    rgba=(qa_scene_vec4){fog_fraction(rgba.x),fog_fraction(rgba.y),fog_fraction(rgba.z),fog_fraction(rgba.w)};
    qa_scene_vertex *vertices=qa_arena_alloc(&frame->storage,8*sizeof(*vertices),_Alignof(qa_scene_vertex),e);
    uint32_t *indices=qa_arena_alloc(&frame->storage,24*sizeof(*indices),_Alignof(uint32_t),e);
    if (!vertices || !indices) return false;
    static const uint32_t order[24]={0,5,4,0,1,5,1,6,5,1,2,6,6,2,3,6,3,7,0,7,3,0,4,7};
    memcpy(indices,order,sizeof(order));
    float distance=truncf(fminf((float)viewport.width,(float)viewport.height)*.2f);
    float x=2*distance/(float)viewport.width,y=2*distance/(float)viewport.height;
    qa_vec3 positions[8]={{-1,1,0},{1,1,0},{1,-1,0},{-1,-1,0},
        {-1+x,1-y,0},{1-x,1-y,0},{1-x,-1+y,0},{-1+x,-1+y,0}};
    for (size_t i=0;i<8;++i) {
        vertices[i]=(qa_scene_vertex){.position=positions[i],.color=rgba};
        if (i>=4) vertices[i].color.w=0;
    }
    qa_scene_draw draw={.mesh={.vertices=vertices,.indices=indices,.vertex_count=8,.index_count=24,
        .primitive=QA_SCENE_TRIANGLES,.bounds={{-1,-1,0},{1,1,0}}},.textures={white,NULL},.texture_count=1};
    qa_scene_matrix_identity(&draw.model); qa_scene_matrix_identity(&draw.mvp);
    qa_scene_state_default(&draw.state);
    draw.state.blend_source=QA_BLEND_SRC_ALPHA; draw.state.blend_destination=QA_BLEND_ONE_MINUS_SRC_ALPHA;
    draw.state.depth_test=QA_DEPTH_ALWAYS; draw.state.depth_write=false; draw.state.cull=QA_CULL_NONE;
    qa_scene_command view={.kind=QA_SCENE_COMMAND_VIEW,.data.view={.viewport=viewport}};
    return qa_scene_frame_emit(frame,&view,e) && qa_scene_frame_draw(frame,&draw,e);
}
bool frontend_unified_q2_player_blend(frontend_unified_q2 *o,qa_actor_id full_viewer,
    bool blend_present,const qa_scene_vec4 *blend,bool damage_present,const qa_scene_vec4 *damage,
    qa_scene_rect viewport,qa_scene_frame *frame,qa_error *e)
{
    if (!o || o->busy || !current(o,e)) return false;
    qa_actor_id viewer_actor; uint32_t source_number;
    if (frame!=&o->frontend->frame || !frontend_remote_unified_player(o->replica,&viewer_actor,&source_number) ||
        !qa_actor_id_equal(full_viewer,viewer_actor) || (blend_present && !blend) || (damage_present && !damage))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Player blend lost its actual received viewer or draw frame");
    bool fallback=qa_actor_id_equal(o->view_actor,full_viewer) && o->view_provider &&
        (!o->view_owner || !o->view_owner->retired);
    if (!blend_present && fallback && o->view_blend_present) { blend=&o->view_blend; blend_present=true; }
    if (!damage_present && fallback && o->view_damage_present) { damage=&o->view_damage_blend; damage_present=true; }
    if (!blend_present && !damage_present) return true;
    frontend_unified_bank_view media_bank;
    if (!frontend_unified_media_bank_read(o->media,0,&media_bank) || !media_bank.images)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Received player blend has no actual shared white image");
    const qa_scene_image *white=qa_scene_white(media_bank.images);
    ++o->busy;
    bool okay=(!blend_present || qa_scene_frame_picture(frame,white,viewport,viewport,
        (qa_scene_vec4){0,0,1,1},*blend,e)) && (!damage_present || damage_blend_draw(frame,white,viewport,*damage,e));
    --o->busy;
    return okay && current(o,e);
}
bool frontend_unified_q2_lights(frontend_unified_q2 *o,const qa_scene_view *view,const qa_scene_world_input *world,
    const qa_scene_light **out,size_t *count,qa_error *e)
{
    if (!o || !view || !world || !out || !count || o->busy || !current(o,e)) return false;
    bool reflected=view->mirror;
    if (reflected && !qa_scene_world_q1_mirror_scope(frontend_unified_media_world(o->media),world,&o->frontend->frame))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 reflected lights lost their actual mirror draw scope");
    qa_actor_id viewer; uint32_t number_id;
    if (!frontend_remote_unified_player(o->replica,&viewer,&number_id)) return false;
    const qa_cvars *registry=frontend_remote_unified_domain_read(o->replica)->cvars;
    const qa_cvar_view *steps=qa_cvars_find(registry,"cl_footsteps"),*hand=qa_cvars_find(registry,"hand");
    bool has_effects=false;
    for (q2_bank *b=o->banks;b;b=b->next) if (b->effects) has_effects=true;
    if (has_effects && (!steps || !hand || !isfinite(steps->number) || fabs(steps->number)>FLT_MAX))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 effects sampling has no actual finite CLIENT routing controls");
    if (!reflected) {
        o->effects_frame_seconds=o->frontend->wall_time_ns>=o->effects_wall_ns?
            (float)((double)(o->frontend->wall_time_ns-o->effects_wall_ns)/1e9):0;
        o->effects_wall_ns=o->frontend->wall_time_ns;
    }
    frontend_remote_q2_effects_sample sample={.milliseconds=o->seconds*1000,.server_milliseconds=o->seconds*1000,
        .fraction=1,.frame_sequence=o->frame_number,.view=*view,.viewer=viewer,.hardware=o->frontend->gl!=NULL,
        .footsteps=steps?(float)steps->number:0,.hand=hand && hand->integer>=0 && hand->integer<=2?hand->integer:0,
        .gun_offset=o->view_gun_offset,.frame_seconds=o->effects_frame_seconds,.world_input=world,
        .viewer_origin=o->viewer_origin,.player_fov=o->player_fov,
        .viewer_origin_present=o->viewer_origin_present && o->viewer_origin_frame==o->frontend->frame_number};
    o->light_count=0; ++o->busy; bool okay=true;
    for (q2_bank *b=o->banks;okay && b;b=b->next) if (b->effects) {
        const qa_scene_light *lights; size_t n;
        frontend_remote_q2_effects_pose *rows=NULL;
        if (reflected) okay=frontend_remote_q2_effects_view_lights(b->effects,&sample,&lights,&n,e);
        else {
            okay=sample_entities(b,&rows,&sample.entity_count,e); sample.entities=rows;
            if (okay) okay=frontend_remote_q2_effects_frame(b->effects,&sample,e) &&
                frontend_remote_q2_effects_prepare(b->effects,&sample,&lights,&n,e);
        }
        free(rows);
        if (!okay) break;
        if (n>SIZE_MAX-o->light_count || o->light_count+n>SIZE_MAX/sizeof(*o->lights)) {
            okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Unified Q2 light span overflow"); break;
        }
        size_t needed=o->light_count+n;
        if (needed>o->light_capacity) {
            void *light_rows=realloc(o->lights,needed*sizeof(*o->lights));
            if (!light_rows) { okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 CLIENT light span"); break; }
            o->lights=light_rows; o->light_capacity=needed;
        }
        if (n) memcpy(o->lights+o->light_count,lights,n*sizeof(*lights));
        o->light_count=needed;
    }
    --o->busy; if (okay) { *out=o->lights; *count=o->light_count; } return okay;
}
static bool overlay_text(qa_ui *ui,qa_scene_rect target,qa_scene_frame *frame,const char *text,float x,float y,
    qa_scene_vec4 color,qa_error *e)
{
    qa_ui_presentation presentation; qa_font_layout layout;
    if (!qa_ui_presentation_read(ui,&presentation,e)) return false;
    qa_font_layout_options opts={.text={(const uint8_t *)text,strlen(text)},.scale=presentation.text_scale,
        .color=color,.color_codes=QA_FONT_COLOR_LITERAL,.alignment=QA_FONT_ALIGN_LEFT,.force_color=true,.max_width=480};
    return qa_font_layout_build(&presentation.fonts,&opts,&frame->storage,&layout,e) &&
        qa_font_draw_layout(frame,&layout,&(qa_font_draw_options){.seat=presentation.fonts.seat,.target=target,
            .origin={x,y},.space=QA_FONT_BASE_UI_640},e);
}
static bool story_draw(frontend_unified_q2 *o,qa_ui *ui,qa_scene_rect viewport,qa_scene_frame *frame,qa_error *e)
{
    if (!o->story) return true;
    const qa_json_document *j=qa_unified_document_json(o->story); qa_json_id row=qa_unified_document_root(o->story);
    qa_buffer text={0};
    if (!localized(o,o->story,row,get(j,row,"event"),&text,e)) return false;
    qa_ui_presentation presentation; qa_font_layout layout;
    bool okay=qa_ui_presentation_read(ui,&presentation,e);
    qa_font_layout_options opts={.text={text.data,text.size},.color={1,1,1,1},
        .color_codes=QA_FONT_COLOR_LITERAL,.alignment=QA_FONT_ALIGN_CENTER,.force_color=true};
    if (okay) { opts.scale=presentation.text_scale;
        okay=qa_font_layout_build(&presentation.fonts,&opts,&frame->storage,&layout,e); }
    if (okay) { opts.max_width=fmaxf(1,layout.width);
        okay=qa_font_layout_build(&presentation.fonts,&opts,&frame->storage,&layout,e); }
    if (okay) okay=qa_font_draw_layout(frame,&layout,&(qa_font_draw_options){.seat=presentation.fonts.seat,
        .target=viewport,.origin={(640-layout.width)*.5f,(480-layout.height)*.5f},.space=QA_FONT_STRETCH_640},e);
    qa_buffer_free(&text); return okay;
}
static bool marker_draw(frontend_unified_q2 *o,qa_scene_rect viewport,qa_scene_frame *frame,qa_error *e)
{
    if (!o->marker_count) return true;
    const qa_cvars *registry=frontend_remote_unified_domain_read(o->replica)->cvars;
    const qa_cvar_view *crosshair=qa_cvars_find(registry,"crosshair"),*duration=qa_cvars_find(registry,"scr_hit_marker_time"),
        *size=qa_cvars_find(registry,"ch_scale"),*alpha=qa_cvars_find(registry,"ch_alpha"),
        *x=qa_cvars_find(registry,"ch_x"),*y=qa_cvars_find(registry,"ch_y");
    if (!crosshair || !duration || !size || !alpha || !x || !y)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 marker draw has no admitted physical CLIENT crosshair controls");
    if (crosshair->number == 0 || (o->view_layouts&(4|32))) return true;
    double elapsed=o->frontend->wall_time_ns>=o->marker_wall_ns?
        (double)(o->frontend->wall_time_ns-o->marker_wall_ns)/1e6:0;
    if (!o->marker_image || duration->integer<=0 || elapsed>duration->integer) { o->marker_count=0; return true; }
    if (!isfinite(size->number) || !isfinite(alpha->number) || !isfinite(x->number) || !isfinite(y->number))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 marker controls are not finite");
    float fraction=(float)(elapsed/duration->integer),scale=fmaxf(1,1.5f*(1-fraction));
    double base=fmax(.1,fmin(9,size->number));
    float width=truncf((float)floor(o->marker_image->logical_width*base+.5)*scale),
        height=truncf((float)floor(o->marker_image->logical_height*base+.5)*scale);
    float dx=(float)fmax(INT32_MIN,fmin(INT32_MAX,trunc(x->number))),
        dy=(float)fmax(INT32_MIN,fmin(INT32_MAX,trunc(y->number)));
    qa_scene_rect_f rect={(float)viewport.x+truncf(((float)viewport.width-width)*.5f)+dx,
        (float)viewport.y+truncf(((float)viewport.height-height)*.5f)+dy,width,height};
    float opacity=(float)fmax(0,fmin(1,alpha->number));
    return qa_scene_frame_picture_f(frame,o->marker_image,viewport,rect,(qa_scene_vec4){0,0,1,1},
        (qa_scene_vec4){1,0,0,opacity*(1-fraction*fraction)},e);
}
bool frontend_unified_q2_hud(frontend_unified_q2 *o,qa_ui *ui,qa_scene_rect viewport,qa_scene_frame *frame,qa_error *e)
{
    if (!o || o->busy || !current(o,e)) return false;
    qa_actor_id player; uint32_t n;
    if (!frontend_remote_unified_player(o->replica,&player,&n)) return false;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (ui!=o->frontend->seats[d->physical_seat].ui) return false;
    ++o->busy;
    bool okay=qa_hud_draw(o->hud,&(qa_hud_frame){.seat=d->physical_seat,.actor=player,.time_ns=nanoseconds(o->seconds),
        .viewport=viewport,.safe_area=viewport,.scale=1,.visible=true},frame,e);
    --o->busy;
    if (okay) okay=marker_draw(o,viewport,frame,e);
    if (okay && o->inventory_visible) {
        okay=overlay_text(ui,viewport,frame,"Inventory",160,80,(qa_scene_vec4){1,1,1,1},e);
        for (size_t i=0;okay && i<o->item_count;++i) {
            char count[32]; if (!qa_format_ecmascript_number(o->items[i].count,count,e)) { okay=false; break; }
            size_t label_size=strlen(o->items[i].label); if (label_size>SIZE_MAX-64) { okay=false; break; }
            char *row=malloc(label_size+64); if (!row) { okay=false; break; }
            snprintf(row,label_size+64,"%s  %s",count,o->items[i].label);
            okay=overlay_text(ui,viewport,frame,row,160,108+(float)i*16,
                o->items[i].selected?(qa_scene_vec4){1,.8f,.3f,1}:(qa_scene_vec4){1,1,1,1},e); free(row);
        }
    }
    if (okay && (o->score_visible || o->frontend->seats[d->physical_seat].scores))
        for (size_t i=0;okay && i<o->score_count;++i)
            okay=overlay_text(ui,viewport,frame,o->score_rows[i],64,110+(float)i*20,(qa_scene_vec4){1,1,1,1},e);
    if (okay) okay=story_draw(o,ui,viewport,frame,e);
    if (okay) okay=frontend_unified_q2_rr_draw(o->rr_hud,ui,viewport,frame,e);
    return okay;
}
bool frontend_unified_q2_checkpoint_ready(const frontend_unified_q2 *o)
{
    if (!o) return true;
    if (o->busy || o->music_retiring || (o->hud && !qa_hud_idle(o->hud)) ||
        !frontend_unified_q2_rr_checkpoint_ready(o->rr_hud)) return false;
    for (q2_bank *b=o->banks;b;b=b->next)
        if (!frontend_remote_q2_effects_idle(b->effects) || !frontend_received_music_idle(b->music)) return false;
    return true;
}
bool frontend_unified_q2_idle(const frontend_unified_q2 *o)
{ return (!o || !o->prepared_frame) && frontend_unified_q2_checkpoint_ready(o); }
bool frontend_unified_q2_destroy(frontend_unified_q2 **slot,qa_error *e)
{
    if (!slot || !*slot) return true;
    frontend_unified_q2 *o=*slot;
    if (!frontend_unified_q2_idle(o)) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 CLIENT still has callback custody");
    if (!frontend_unified_q2_rr_destroy(&o->rr_hud,e)) return false;
    o->music_retiring=true;
    for (q2_bank *b=o->banks;b;b=b->next) if (!frontend_received_music_destroy(&b->music,e)) { o->music_retiring=false; return false; }
    o->music_retiring=false;
    while (o->banks) {
        q2_bank *b=o->banks;
        if (!frontend_remote_q2_effects_destroy(&b->effects,e)) return false;
        if (!frontend_q2_footsteps_destroy(&b->footsteps,e)) return false;
        while (b->models) { q2_model *m=b->models; b->models=m->next; free(m->path); free(m); }
        for (size_t i=0;i<256;++i) free(b->styles[i]);
        o->banks=b->next; free(b->aliases); free(b->source_provider); free(b->content); free(b);
    }
    if (o->hud && !qa_hud_destroy(o->hud,e)) return false;
    qa_unified_document_destroy(o->frame); qa_unified_document_destroy(o->prepared_frame);
    if (o->config) for (size_t i=0;i<o->config_count;++i) free(o->config[i]);
    free(o->config); free(o->help); free(o->help_text[0]); free(o->help_text[1]);
    qa_localization_pool_destroy(o->localizations);
    inventory_clear(o); scores_clear(o);
    free(o->view_content); free(o->view_provider);
    qa_unified_document_destroy(o->story); sky_clear(o);
    qa_scene_image_release(o->marker_image);
    while (o->visuals) { q2_visual *v=o->visuals; o->visuals=v->next; visual_free(v); }
    while (o->activations) { q2_activation *a=o->activations; o->activations=a->next; free(a->provider); free(a); }
    while (o->loops) { q2_loop *l=o->loops; o->loops=l->next; free(l); }
    while (o->names) { q2_player_name *n=o->names; o->names=n->next; free(n->name); free(n); }
    free(o->layout); free(o->lights); free(o->muzzles); free(o); *slot=NULL; return true;
}
bool frontend_unified_q2_visit(const frontend_unified_q2 *o,const qa_application_content_visitor *visitor,qa_error *e)
{
    if (!o) return true;
    if (!visitor || !frontend_unified_q2_checkpoint_ready(o)) return false;
    for (q2_bank *b=o->banks;b;b=b->next)
        if (!frontend_q2_footsteps_visit(b->footsteps,visitor,e)) return false;
    return true;
}
static const q2_bank *bank_at(const frontend_unified_q2 *o,size_t ordinal)
{
    const q2_bank *b=o?o->banks:NULL;
    while (b && ordinal) { b=b->next; --ordinal; }
    return b;
}
size_t frontend_unified_q2_bank_count(const frontend_unified_q2 *o)
{
    size_t count=0;
    for (const q2_bank *b=o?o->banks:NULL;b;b=b->next) ++count;
    return count;
}
size_t frontend_unified_q2_light_count(const frontend_unified_q2 *o,size_t ordinal)
{
    const q2_bank *b=bank_at(o,ordinal);
    return b?frontend_remote_q2_effects_light_identity_count(b->effects):0;
}
bool frontend_unified_q2_light_at(const frontend_unified_q2 *o,size_t ordinal,size_t light,uint64_t *out)
{
    const q2_bank *b=bank_at(o,ordinal);
    return b && frontend_remote_q2_effects_light_identity_at(b->effects,light,out);
}
size_t frontend_unified_q2_music_count(const frontend_unified_q2 *o)
{
    size_t count=0;
    for (const q2_bank *b=o?o->banks:NULL;b;b=b->next) if (b->music) ++count;
    return count;
}
bool frontend_unified_q2_music_at(const frontend_unified_q2 *o,size_t ordinal,uint64_t *bus,qa_audio_music **player)
{
    if (!player) return false;
    for (const q2_bank *b=o?o->banks:NULL;b;b=b->next) if (b->music) {
        if (ordinal) { --ordinal; continue; }
        if (!frontend_received_music_bus(b->music,bus)) return false;
        *player=frontend_received_music_player(b->music); return true;
    }
    return false;
}
bool frontend_unified_q2_restore_finish(frontend_unified_q2 *o,qa_error *e)
{
    if (!o || !o->frontend->source_restoring || !frontend_unified_q2_checkpoint_ready(o) || !current(o,e)) return false;
    for (q2_bank *b=o->banks;b;b=b->next) if (b->music) {
        frontend_music_origin origin;
        if (!music_origin(b,&origin,e) || !frontend_received_music_restore_finish(b->music,&origin,e)) return false;
    }
    return current(o,e);
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
    if (!qa_source_save_u32(io,&s.slot) || !qa_source_save_u64(io,&s.generation)) return false;
    return !read || refs->effects.actor_decode(refs->effects.context,s,a,io->error);
}
static bool saved_activation_retained(qa_source_save_io *io,frontend_unified_q2 *o,q2_activation **owner,bool retired_allowed)
{
    uint64_t ordinal=0; bool read=io->direction==QA_SOURCE_SAVE_READ;
    if (!read && *owner) {
        uint64_t index=1;
        for (q2_activation *a=o->activations;a;a=a->next,++index) if (a==*owner) { ordinal=index; break; }
        if (!ordinal) return frontend_unified_fail(io->error,QA_ERROR_FORMAT,"Q2 state lost its actual Source activation");
    }
    if (!qa_source_save_u64(io,&ordinal)) return false;
    if (read) {
        *owner=NULL;
        if (ordinal) { q2_activation *a=o->activations;
            while (a && --ordinal) a=a->next;
            if (!a || (a->retired && !retired_allowed)) return frontend_unified_fail(io->error,QA_ERROR_FORMAT,"Q2 state refers to a missing or retired Source activation");
            *owner=a;
        }
    }
    return true;
}
static bool saved_activation(qa_source_save_io *io,frontend_unified_q2 *o,q2_activation **owner)
{ return saved_activation_retained(io,o,owner,false); }
static bool saved_fog(qa_source_save_io *io,qa_scene_fog *fog)
{
    if (!qa_source_save_vec3(io,&fog->color) || !qa_vec_finite(fog->color) ||
        !qa_source_save_vec3(io,&fog->height_color) || !qa_vec_finite(fog->height_color) ||
        !qa_source_save_vec3(io,&fog->height_end_color) || !qa_vec_finite(fog->height_end_color)) return false;
    float *values[]={&fog->density,&fog->sky_factor,&fog->height_density,&fog->height_start,&fog->height_end,&fog->height_falloff};
    for (size_t i=0;i<6;++i) if (!qa_source_save_f32(io,values[i]) || !isfinite(*values[i])) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) fog->kind=QA_FOG_Q2;
    return true;
}
static bool saved_scene(qa_source_save_io *io,frontend_unified_q2 *o,const frontend_unified_q2_refs *refs)
{
    bool read=io->direction==QA_SOURCE_SAVE_READ,story=o->story!=NULL;
    if (!qa_source_save_bool(io,&story) || !saved_activation(io,o,&o->story_owner) || (!story && o->story_owner)) return false;
    if (story) {
        qa_buffer bytes={0};
        bool okay=read?capsule(io,&bytes):qa_unified_document_encode(o->story,&bytes,io->error) && capsule(io,&bytes);
        if (okay && read) okay=qa_unified_document_decode(QA_UNIFIED_CHECKPOINT,(qa_bytes){bytes.data,bytes.size},&o->story,io->error);
        qa_buffer_free(&bytes); if (!okay) return false;
        const qa_json_document *j=qa_unified_document_json(o->story); qa_json_id row=qa_unified_document_root(o->story);
        qa_buffer provider={0},content={0}; uint64_t generation=0; qa_vfs *files; const qa_product *product;
        okay=qa_json_string_equal(j,get(j,row,"kind"),"q2-rerelease") &&
            qa_json_string_equal(j,get(j,get(j,row,"event"),"kind"),"story") &&
            frontend_unified_q2_validate(o,false,o->story,row,io->error) &&
            owner_token(o->story,get(j,row,"owner"),&provider,&generation,io->error) &&
            ((!o->story_owner && !generation) || (o->story_owner && generation==o->story_owner->generation &&
                !strcmp((const char *)provider.data,o->story_owner->provider))) &&
            string(o->story,get(j,row,"content"),&content,io->error) &&
            qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),(const char *)content.data,&files,&product,io->error);
        qa_buffer_free(&provider); qa_buffer_free(&content); if (!okay) return false;
    }
    bool sky=o->sky_name!=NULL;
    if (!qa_source_save_bool(io,&sky) || !saved_activation(io,o,&o->sky_owner) || (!sky && o->sky_owner)) return false;
    if (sky) {
        if (!saved_text(io,&o->sky_name) || !o->sky_name || !saved_text(io,&o->sky_content) || !o->sky_content ||
            !qa_source_save_f32(io,&o->sky_rotation) || !isfinite(o->sky_rotation) ||
            !qa_source_save_vec3(io,&o->sky_axis) || !qa_vec_finite(o->sky_axis) ||
            !qa_source_save_bool(io,&o->sky_auto_rotate) || (o->sky_auto_rotate && o->sky_rotation==0)) return false;
        qa_vfs *files; const qa_product *product;
        if (!qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),o->sky_content,&files,&product,io->error) ||
            product->family!=QA_GAME_Q2) return false;
        for (size_t i=0;i<6;++i) {
            uint64_t image=0;
            if ((!read && (!o->sky_images[i] || !refs->effects.image_encode ||
                !refs->effects.image_encode(refs->effects.context,o->sky_images[i],&image,io->error))) ||
                !qa_source_save_u64(io,&image) || !image) return false;
            if (read) { const qa_scene_image *actual=NULL;
                if (!refs->effects.image_decode || !refs->effects.image_decode(refs->effects.context,image,&actual,io->error) || !actual) return false;
                qa_scene_image_retain(actual); o->sky_images[i]=(qa_scene_image *)actual;
            }
        }
    }
    if (!qa_source_save_bool(io,&o->fog_received) || !saved_activation(io,o,&o->fog_owner) ||
        (!o->fog_received && o->fog_owner)) return false;
    if (o->fog_received && (!saved_fog(io,&o->fog_start) || !saved_fog(io,&o->fog_target) ||
        !qa_source_save_f64(io,&o->fog_started_ms) || !isfinite(o->fog_started_ms) ||
        !qa_source_save_f64(io,&o->fog_duration_ms) || !isfinite(o->fog_duration_ms))) return false;
    return true;
}
static bool saved_visuals(qa_source_save_io *io,frontend_unified_q2 *o,const frontend_unified_q2_refs *refs)
{
    bool read=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=0; for (q2_activation *a=o->activations;a;a=a->next) ++count;
    if (!qa_source_save_count(io,&count,65536) || (read && count>(io->input.size-io->offset)/11)) return false;
    q2_activation *a=o->activations,**tail=&o->activations;
    for (size_t i=0;i<count;++i) {
        if (read) { a=calloc(1,sizeof(*a)); if (!a) return false; *tail=a; tail=&a->next; }
        if (!saved_text(io,&a->provider) || !a->provider || !*a->provider ||
            !qa_source_save_u64(io,&a->generation) || !a->generation || !qa_source_save_bool(io,&a->retired)) return false;
        for (q2_activation *old=o->activations;old!=a;old=old->next)
            if (old->generation==a->generation && !strcmp(old->provider,a->provider)) return false;
        if (!read) a=a->next;
    }
    count=0; for (q2_visual *v=o->visuals;v;v=v->next) ++count;
    if (!qa_source_save_count(io,&count,65536) || (read && count>(io->input.size-io->offset)/39)) return false;
    q2_visual *v=o->visuals,**visual_tail=&o->visuals;
    for (size_t i=0;i<count;++i) {
        if (read) { v=calloc(1,sizeof(*v)); if (!v) return false; *visual_tail=v; visual_tail=&v->next; }
        if (!saved_actor(io,refs,&v->actor) || !v->actor.registry || !saved_activation(io,o,&v->activation) ||
            !saved_text(io,&v->content) || !v->content || !*v->content || !saved_text(io,&v->path) || !saved_text(io,&v->source_provider) ||
            !qa_source_save_u64(io,&v->effects) || !qa_source_save_u32(io,&v->event) ||
            !qa_source_save_u64(io,&v->event_frame) || v->event_frame>o->frame_number ||
            !qa_source_save_bool(io,&v->visible)) return false;
        bool present=v->model!=NULL;
        if (!qa_source_save_bool(io,&present)) return false;
        if (present) {
            qa_buffer bytes={0};
            bool okay=read?capsule(io,&bytes):qa_unified_document_encode(v->model,&bytes,io->error) && capsule(io,&bytes);
            if (okay && read) okay=qa_unified_document_decode(QA_UNIFIED_CHECKPOINT,(qa_bytes){bytes.data,bytes.size},&v->model,io->error);
            qa_buffer_free(&bytes); if (!okay) return false;
            qa_actor_id actual;
            if (!visual_record(o,v->model,qa_unified_document_root(v->model),&actual,io->error) || !qa_actor_id_equal(actual,v->actor)) return false;
            const qa_json_document *j=qa_unified_document_json(v->model); qa_json_id root=qa_unified_document_root(v->model),event=get(j,root,"event");
            uint64_t effects=0; qa_buffer content={0},path={0},provider={0},source_provider={0}; uint64_t generation=0;
            frontend_remote_q2_effects_profile profile;
            okay=qa_json_string_equal(j,get(j,event,"kind"),"model") && string(v->model,get(j,root,"content"),&content,io->error) &&
                string(v->model,get(j,event,"path"),&path,io->error) && qa_json_u64(j,get(j,event,"effects"),&effects,io->error) &&
                owner_token(v->model,get(j,root,"owner"),&provider,&generation,io->error) &&
                semantic_source(v->model,root,false,&source_provider,&profile,io->error) &&
                (v->source_provider?(source_provider.size && !strcmp(v->source_provider,(const char *)source_provider.data)):!source_provider.size) &&
                !strcmp(v->content,(const char *)content.data) && v->path && !strcmp(v->path,(const char *)path.data) && effects==v->effects &&
                (v->activation?(generation==v->activation->generation && !strcmp(v->activation->provider,(const char *)provider.data)):generation==0);
            qa_buffer_free(&content); qa_buffer_free(&path); qa_buffer_free(&provider); qa_buffer_free(&source_provider); if (!okay) return false;
        } else if (v->path || v->effects) return false;
        for (q2_visual *old=o->visuals;old!=v;old=old->next) if (qa_actor_id_equal(old->actor,v->actor)) return false;
        if (!read) v=v->next;
    }
    if (!saved_activation(io,o,o->help_owner) || !saved_activation(io,o,o->help_owner+1)) return false;
    uint32_t view_profile=o->view_profile;
    bool view_actor_present=o->view_actor.registry!=0;
    if (!saved_text(io,&o->view_content) || !saved_text(io,&o->view_provider) || !qa_source_save_u32(io,&view_profile) ||
        !saved_activation(io,o,&o->view_owner) || !qa_source_save_u64(io,&o->view_layouts) ||
        !qa_source_save_vec3(io,&o->view_gun_offset) || !qa_vec_finite(o->view_gun_offset) ||
        !qa_source_save_bool(io,&view_actor_present) || (view_actor_present && !saved_actor(io,refs,&o->view_actor)) ||
        !qa_source_save_bool(io,&o->view_blend_present) ||
        !qa_source_save_bool(io,&o->view_damage_present)) return false;
    qa_scene_vec4 *view_colors[2]={&o->view_blend,&o->view_damage_blend};
    for (size_t i=0;i<2;++i) {
        qa_scene_vec4 *rgba=view_colors[i];
        if (!qa_source_save_f32(io,&rgba->x) || !qa_source_save_f32(io,&rgba->y) ||
            !qa_source_save_f32(io,&rgba->z) || !qa_source_save_f32(io,&rgba->w) ||
            !isfinite(rgba->x) || !isfinite(rgba->y) || !isfinite(rgba->z) || !isfinite(rgba->w)) return false;
        bool present=i?o->view_damage_present:o->view_blend_present;
        if (!present && (rgba->x != 0 || rgba->y != 0 || rgba->z != 0 || rgba->w != 0)) return false;
    }
    if (read) o->view_profile=(frontend_remote_q2_effects_profile)view_profile;
    if (o->view_provider && *o->view_provider) {
        qa_vfs *files; const qa_product *product;
        if (!o->view_content || !*o->view_content || !o->view_actor.registry ||
            (view_profile!=FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC && view_profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE) ||
            !qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),o->view_content,&files,&product,io->error) ||
            product->family!=QA_GAME_Q2) return false;
    } else if (view_profile || o->view_actor.registry || o->view_blend_present || o->view_damage_present) return false;
    count=0; for (q2_loop *l=o->loops;l;l=l->next) ++count;
    if (!qa_source_save_count(io,&count,65536) || (read && count>(io->input.size-io->offset)/20)) return false;
    q2_loop *l=o->loops,**loop_tail=&o->loops;
    for (size_t i=0;i<count;++i) {
        if (read) { l=calloc(1,sizeof(*l)); if (!l) return false; *loop_tail=l; loop_tail=&l->next; }
        if (!saved_actor(io,refs,&l->actor) || !saved_activation(io,o,&l->activation)) return false;
        for (q2_loop *old=o->loops;old!=l;old=old->next) if (qa_actor_id_equal(old->actor,l->actor)) return false;
        if (!read) l=l->next;
    }
    return true;
}
static bool persistent_domains_valid(frontend_unified_q2 *o,qa_error *e)
{
    static const frontend_remote_q2_effects_presentation_kind kinds[]={
        FRONTEND_REMOTE_Q2_ORDINARY_BEAM,FRONTEND_REMOTE_Q2_MONSTER_BEAM,
        FRONTEND_REMOTE_Q2_SHADOW_LIGHT,FRONTEND_REMOTE_Q2_SOURCE_LIGHT,FRONTEND_REMOTE_Q2_FLASHLIGHT};
    for (q2_bank *b=o->banks;b;b=b->next) if (b->effects)
        for (size_t k=0;k<sizeof(kinds)/sizeof(*kinds);++k) {
            qa_actor_id id;
            for (size_t i=0;frontend_remote_q2_effects_presentation_actor_at(b->effects,kinds[k],i,&id);++i) {
                if (b->activation && b->activation->retired)
                    return frontend_unified_fail(e,QA_ERROR_FORMAT,"Retired Q2 Source retains a semantic effect");
                for (q2_bank *prior=o->banks;prior!=b;prior=prior->next) if (prior->effects) {
                    qa_actor_id other;
                    for (size_t n=0;frontend_remote_q2_effects_presentation_actor_at(prior->effects,kinds[k],n,&other);++n)
                        if (qa_actor_id_equal(id,other))
                            return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 semantic effect has multiple retained Source owners");
                }
            }
        }
    return true;
}
static bool q2_fields(qa_source_save_io *io,frontend_unified_q2 *o,const frontend_unified_q2_refs *refs)
{
    bool read=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[5]={'Q','U','Q','3','4'};
    uint32_t epoch=frontend_remote_unified_epoch(o->replica);
    uint32_t physical=frontend_remote_unified_domain_read(o->replica)->physical_seat;
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QUQ34",sizeof(magic)) ||
        !qa_source_save_u32(io,&epoch) || epoch!=frontend_remote_unified_epoch(o->replica) ||
        !qa_source_save_u32(io,&physical) || physical!=frontend_remote_unified_domain_read(o->replica)->physical_seat ||
        !qa_source_save_f64(io,&o->seconds) || !isfinite(o->seconds) || !qa_source_save_u64(io,&o->frame_number) ||
        !qa_source_save_u64(io,&o->effects_wall_ns) || !qa_source_save_f32(io,&o->effects_frame_seconds) ||
        !isfinite(o->effects_frame_seconds) || o->effects_frame_seconds<0 ||
        !qa_source_save_bool(io,&o->viewer_origin_present) || !qa_source_save_u64(io,&o->viewer_origin_frame) ||
        !qa_source_save_vec3(io,&o->viewer_origin) || !qa_vec_finite(o->viewer_origin) ||
        !qa_source_save_f32(io,&o->player_fov) || !isfinite(o->player_fov) ||
        (o->viewer_origin_present?(o->player_fov<=0 || o->player_fov>=180):
            (o->player_fov != 0 || o->viewer_origin_frame || o->viewer_origin.x != 0 || o->viewer_origin.y != 0 || o->viewer_origin.z != 0))) return false;
    bool frame=o->frame!=NULL;
    if (!qa_source_save_bool(io,&frame)) return false;
    if (frame) {
        qa_buffer bytes={0};
        bool okay=read?capsule(io,&bytes):qa_unified_document_encode(o->frame,&bytes,io->error) && capsule(io,&bytes);
        if (okay && read) okay=qa_unified_document_decode(QA_UNIFIED_FRAME_DOCUMENT,(qa_bytes){bytes.data,bytes.size},&o->frame,io->error);
        qa_buffer_free(&bytes); if (!okay) return false;
        const qa_json_document *j=qa_unified_document_json(o->frame); qa_json_id root=qa_unified_document_root(o->frame),
            f=get(j,get(j,get(j,root,"output"),"snapshot"),"frame"),t=get(j,f,"time");
        uint64_t saved_epoch,n; double seconds;
        if (!qa_json_u64(j,get(j,root,"epoch"),&saved_epoch,io->error) || saved_epoch!=epoch ||
            !qa_json_u64(j,get(j,f,"frame"),&n,io->error) || n!=o->frame_number || !number(o->frame,get(j,t,"value"),&seconds,io->error)) return false;
        if (qa_json_string_equal(j,get(j,t,"kind"),"milliseconds")) seconds/=1000;
        else if (!qa_json_string_equal(j,get(j,t,"kind"),"seconds")) return false;
        if (seconds!=o->seconds) return false;
    } else if (o->seconds != 0 || o->frame_number) return false;
    bool pending=o->prepared_frame!=NULL;
    if(!qa_source_save_bool(io,&pending))return false;
    if(pending){qa_buffer bytes={0};
        if(!qa_source_save_u64(io,&o->prepared_number) || !qa_source_save_f64(io,&o->prepared_seconds) ||
            !isfinite(o->prepared_seconds) || (frame && o->prepared_number<=o->frame_number))return false;
        bool okay=read?capsule(io,&bytes):qa_unified_document_encode(o->prepared_frame,&bytes,io->error) && capsule(io,&bytes);
        if(okay && read)okay=qa_unified_document_decode(QA_UNIFIED_FRAME_DOCUMENT,(qa_bytes){bytes.data,bytes.size},&o->prepared_frame,io->error);
        qa_buffer_free(&bytes);if(!okay)return false;
        const qa_unified_document *d=o->prepared_frame;const qa_json_document *j=qa_unified_document_json(d);
        qa_json_id root=qa_unified_document_root(d),f=get(j,get(j,get(j,root,"output"),"snapshot"),"frame"),t=get(j,f,"time");
        uint64_t actual_epoch,n;double seconds;
        if(!qa_json_u64(j,get(j,root,"epoch"),&actual_epoch,io->error) || actual_epoch!=epoch ||
            !qa_json_u64(j,get(j,f,"frame"),&n,io->error) || n!=o->prepared_number || !number(d,get(j,t,"value"),&seconds,io->error))return false;
        if(qa_json_string_equal(j,get(j,t,"kind"),"milliseconds"))seconds/=1000;
        else if(!qa_json_string_equal(j,get(j,t,"kind"),"seconds"))return false;
        if(seconds!=o->prepared_seconds)return false;
    }
    if (!qa_source_save_bool(io,&o->marker_set) || !qa_source_save_u64(io,&o->marker_frame) ||
        !qa_source_save_u64(io,&o->marker_wall_ns) || !qa_source_save_u32(io,&o->marker_count) ||
        (!o->marker_set && (o->marker_frame || o->marker_wall_ns || o->marker_count)) ||
        (o->marker_set && (!frame || o->marker_frame>o->frame_number))) return false;
    bool marker_image=o->marker_image!=NULL;
    if (!qa_source_save_bool(io,&marker_image)) return false;
    if (marker_image) {
        uint64_t image=0;
        if ((!read && (!refs->effects.image_encode || !refs->effects.image_encode(refs->effects.context,o->marker_image,&image,io->error))) ||
            !qa_source_save_u64(io,&image) || !image) return false;
        if (read) { const qa_scene_image *actual=NULL;
            if (!refs->effects.image_decode || !refs->effects.image_decode(refs->effects.context,image,&actual,io->error) || !actual) return false;
            qa_scene_image_retain(actual); o->marker_image=(qa_scene_image *)actual;
        }
    }
    if (!saved_visuals(io,o,refs) || !saved_scene(io,o,refs)) return false;
    qa_buffer catalogs={0};
    if (read) o->localizations=qa_localization_pool_create(io->error);
    bool catalog_ok=o->localizations && (read?capsule(io,&catalogs):qa_localization_pool_checkpoint(o->localizations,&catalogs,io->error) && capsule(io,&catalogs));
    if (catalog_ok && read) catalog_ok=qa_localization_pool_restore(o->localizations,(qa_bytes){catalogs.data,catalogs.size},io->error);
    qa_buffer_free(&catalogs); if (!catalog_ok) return false;
    if (!saved_text(io,&o->help) || !saved_text(io,&o->help_text[0]) || !saved_text(io,&o->help_text[1]) ||
        !qa_source_save_bool(io,&o->help_visible) || !saved_text(io,&o->layout) ||
        !qa_source_save_count(io,&o->config_count,65536)) return false;
    if (read && o->config_count) {
        if (o->config_count>io->input.size-io->offset) return false;
        o->config=calloc(o->config_count,sizeof(*o->config));
        if (!o->config) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Restoring Q2 CLIENT configstrings");
    }
    for (size_t i=0;i<o->config_count;++i) if (!saved_text(io,o->config+i)) return false;
    size_t names=0; for (q2_player_name *n=o->names;n;n=n->next) ++names;
    if (!qa_source_save_count(io,&names,65536)) return false;
    q2_player_name *name=o->names,**name_tail=&o->names;
    for (size_t i=0;i<names;++i) {
        if (read) { name=calloc(1,sizeof(*name)); if (!name) return false; *name_tail=name; name_tail=&name->next; }
        if (!qa_source_save_u32(io,&name->slot) || !saved_text(io,&name->name) || !name->name) return false;
        for (q2_player_name *previous=o->names;previous!=name;previous=previous->next)
            if (previous->slot==name->slot) return false;
        if (!read) name=name->next;
    }
    if (!saved_activation(io,o,&o->inventory_owner) || !saved_activation(io,o,&o->score_owner) ||
        !qa_source_save_bool(io,&o->inventory_visible) || !qa_source_save_bool(io,&o->score_visible) ||
        !qa_source_save_count(io,&o->item_count,65536)) return false;
    if (read && o->item_count) {
        if (o->item_count>(io->input.size-io->offset)/11) return false;
        o->items=calloc(o->item_count,sizeof(*o->items)); if (!o->items) return false;
    }
    for (size_t i=0;i<o->item_count;++i) { q2_inventory_row *v=o->items+i;
        if (!saved_text(io,&v->item) || !v->item || !*v->item || !saved_text(io,&v->label) || !v->label ||
            !qa_source_save_f64(io,&v->count) || !isfinite(v->count) || v->count<=0 || !qa_source_save_bool(io,&v->selected)) return false;
    }
    if ((!o->inventory_visible && (o->item_count || o->inventory_owner)) ||
        !qa_source_save_count(io,&o->score_count,65536)) return false;
    if (read && o->score_count) {
        if (o->score_count>io->input.size-io->offset) return false;
        o->score_rows=calloc(o->score_count,sizeof(*o->score_rows)); if (!o->score_rows) return false;
    }
    for (size_t i=0;i<o->score_count;++i) if (!saved_text(io,o->score_rows+i) || !o->score_rows[i]) return false;
    for (size_t i=0;i<256;++i) if (!qa_source_save_i32(io,o->inventory+i)) return false;
    if (!qa_source_save_count(io,&o->muzzle_count,65536)) return false;
    if (read && o->muzzle_count) {
        if (o->muzzle_count>(io->input.size-io->offset)/30) return false;
        o->muzzles=calloc(o->muzzle_count,sizeof(*o->muzzles)); if (!o->muzzles) return false;
    }
    for (size_t i=0;i<o->muzzle_count;++i) {
        q2_muzzle_receipt *m=o->muzzles+i;
        if (!saved_actor(io,refs,&m->actor) || !m->actor.registry || !qa_source_save_i32(io,&m->number) || m->number< -1 ||
            !qa_source_save_i32(io,&m->flash) || m->flash<0 || !qa_source_save_f64(io,&m->milliseconds) || !isfinite(m->milliseconds) ||
            !qa_source_save_bool(io,&m->monster) || !qa_source_save_bool(io,&m->consumed)) return false;
    }
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
        q2_activation *bank_owner=read?NULL:b->activation;
        if (!saved_activation_retained(io,o,&bank_owner,true)) { if (read) free(content); return false; }
        char *source_provider=read?NULL:b->source_provider;
        if (!saved_text(io,&source_provider)) { if (read) free(content); return false; }
        uint32_t profile=read?0:b->profile;
        if (!qa_source_save_u32(io,&profile) || profile>FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE ||
            (profile==0)!=(source_provider==NULL) || (source_provider && !*source_provider)) {
            if (read) { free(content); free(source_provider); } return false; }
        if (read) {
            for (q2_bank *previous=o->banks;previous;previous=previous->next)
                if (previous->activation==bank_owner && !strcmp(previous->content,content) &&
                    (previous->source_provider!=NULL)==(source_provider!=NULL) &&
                    (!source_provider || !strcmp(previous->source_provider,source_provider))) {
                    free(content); free(source_provider); return false; }
            bool okay=bank(o,content,bank_owner,source_provider,(frontend_remote_q2_effects_profile)profile,false,&b,io->error);
            free(content); free(source_provider); if (!okay) return false;
        }
        if (!qa_source_save_f64(io,&b->frame_milliseconds) || !isfinite(b->frame_milliseconds) ||
            (profile?b->frame_milliseconds<=0:b->frame_milliseconds!=0)) return false;
        if (!frontend_received_music_fields(o->frontend,&b->music,io,refs->audio,io->error) || (b->music && !profile)) return false;
        for (size_t k=0;k<256;++k)
            if (!saved_text(io,b->styles+k) || !qa_source_save_u64(io,b->style_sequences+k) ||
                (!b->styles[k] && b->style_sequences[k])) return false;
        size_t models=0; for (q2_model *m=b->models;m;m=m->next) ++models;
        if (!qa_source_save_count(io,&models,65536)) return false;
        q2_model *m=b->models,**tail=&b->models;
        for (size_t k=0;k<models;++k) {
            if (read) { m=calloc(1,sizeof(*m)); if (!m) return false; *tail=m; tail=&m->next; }
            uint64_t id=0; bool present=m->scene!=NULL;
            if (!saved_text(io,&m->path) || !m->path || !qa_source_save_bool(io,&present)) return false;
            for (q2_model *old=b->models;old!=m;old=old->next) if (!strcmp(old->path,m->path)) return false;
            if (present) {
                if ((!read && !refs->model_encode(refs->effects.context,m->scene,&id,io->error)) ||
                    !qa_source_save_u64(io,&id) || !id ||
                    (read && (!refs->model_decode(refs->effects.context,id,&m->scene,io->error) || !m->scene))) return false;
            }
            if (!read) m=m->next;
        }
        if (!qa_source_save_count(io,&b->alias_count,65536)) return false;
        if (read && b->alias_count) {
            if (b->alias_count>(io->input.size-io->offset)/16) return false;
            b->aliases=calloc(b->alias_count,sizeof(*b->aliases)); if (!b->aliases) return false;
        }
        for (size_t k=0;k<b->alias_count;++k) {
            if (!qa_source_save_u32(io,&b->aliases[k].number) || !saved_actor(io,refs,&b->aliases[k].actor) ||
                !b->aliases[k].actor.registry) return false;
            for (size_t p=0;p<k;++p) if (b->aliases[p].number==b->aliases[k].number) return false;
        }
        bool footsteps=b->footsteps!=NULL;
        if (!qa_source_save_bool(io,&footsteps)) return false;
        if (footsteps) {
            if (!refs->content || b->profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE) return false;
            frontend_q2_footstep_source s=footstep_source(b); qa_buffer bytes={0};
            bool okay=read?capsule(io,&bytes):frontend_q2_footsteps_checkpoint(b->footsteps,&s,refs->content,&bytes,io->error) && capsule(io,&bytes);
            if (okay && read) okay=frontend_q2_footsteps_restore(&s,refs->content,(qa_bytes){bytes.data,bytes.size},&b->footsteps,io->error);
            qa_buffer_free(&bytes); if (!okay) return false;
        }
        bool effects=b->effects!=NULL;
        if (!qa_source_save_bool(io,&effects)) return false;
        if (effects) {
            if (b->profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE && !b->footsteps) return false;
            qa_buffer bytes={0}; frontend_remote_q2_effects_source s=source(b);
            bool okay=read?capsule(io,&bytes):frontend_remote_q2_effects_checkpoint(b->effects,&refs->effects,&bytes,io->error) && capsule(io,&bytes);
            if (okay && read) okay=frontend_remote_q2_effects_restore(&s,&refs->effects,(qa_bytes){bytes.data,bytes.size},&b->effects,io->error);
            qa_buffer_free(&bytes); if (!okay) return false;
        }
        if (!read) b=b->next;
    }
    if (!persistent_domains_valid(o,io->error)) return false;
    qa_buffer bytes={0};
    qa_hud_checkpoint_refs hud_refs={.context=refs->effects.context,.image_encode=refs->effects.image_encode,.image_decode=refs->effects.image_decode};
    bool okay=read?capsule(io,&bytes):qa_hud_checkpoint(o->hud,&hud_refs,&bytes,io->error) && capsule(io,&bytes);
    if (okay && read) { qa_hud_options h=hud_options(o); okay=qa_hud_restore((qa_bytes){bytes.data,bytes.size},&h,&hud_refs,&o->hud,io->error); }
    qa_buffer_free(&bytes);
    if (okay) okay=read?capsule(io,&bytes):
        frontend_unified_q2_rr_checkpoint(o->rr_hud,refs,&bytes,io->error) && capsule(io,&bytes);
    if (okay && read) okay=frontend_unified_q2_rr_restore(o->frontend,o->replica,o->media,o->events,
        refs,(qa_bytes){bytes.data,bytes.size},&o->rr_hud,io->error);
    qa_buffer_free(&bytes); return okay;
}
bool frontend_unified_q2_checkpoint(frontend_unified_q2 *o,const frontend_unified_q2_refs *refs,qa_buffer *out,qa_error *e)
{
    if (!o || !refs || !refs->model_encode || !refs->effects.actor_encode || !out || out->data || out->size ||
        !frontend_unified_q2_checkpoint_ready(o) || !current(o,e)) return false;
    if (!frontend_remote_unified_checkpoint_current(o->replica,e)) return false;
    qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,NULL,e) && q2_fields(&io,o,refs) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_unified_q2_restore(qa_frontend *f,frontend_remote_unified *replica,frontend_unified_media *media,
    frontend_unified_events *events,const frontend_unified_q2_refs *refs,qa_bytes bytes,frontend_unified_q2 **out,qa_error *e)
{
    if (!f || !replica || !media || !events || !refs || !refs->model_decode || !refs->effects.actor_decode || !out || *out) return false;
    if (!frontend_remote_unified_checkpoint_current(replica,e)) return false;
    frontend_unified_q2 *o=calloc(1,sizeof(*o)); if (!o) return false;
    o->frontend=f; o->replica=replica; o->media=media; o->events=events;
    *out=o;
    qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && q2_fields(&io,o,refs) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!okay) { qa_error ignored={0}; frontend_unified_q2_frame_abort(o);frontend_unified_q2_destroy(out,&ignored);
        if (e && e->code==QA_OK) frontend_unified_fail(e,QA_ERROR_FORMAT,"Invalid Q2 private CLIENT state");
        return false; }
    return true;
}

bool frontend_unified_q2_frame_restore_bind(frontend_unified_q2 *o,const qa_unified_document *d,qa_error *e)
{
    if(!o || !o->frontend->source_restoring || !frontend_unified_q2_checkpoint_ready(o) || !current(o,e))return false;
    if(!o->prepared_frame)return d==o->replica->prepared_frame && frontend_unified_q2_rr_idle(o->rr_hud);
    if(!d || qa_unified_document_type(d)!=QA_UNIFIED_FRAME_DOCUMENT)return false;
    qa_buffer a={0},b={0};bool ok=qa_unified_document_encode(o->prepared_frame,&a,e) && qa_unified_document_encode(d,&b,e) &&
        a.size==b.size && (!a.size || !memcmp(a.data,b.data,a.size));
    qa_buffer_free(&a);qa_buffer_free(&b);
    return (ok && frontend_unified_q2_rr_frame_ready(o->rr_hud,d,e)) || frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 restored prepared frame differs from its actual parent");
}
