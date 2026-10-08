#include "qa/network_unified_control.h"
#include "remote_unified_private.h"
#include "remote_unified_metadata.h"
#include "remote_unified_q2.h"
#include "q2_entity_effects.h"
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
#include "qa/text.h"
#include "qa/ui_preferences.h"
#include "qa/text.h"
#include "qa/player_progress.h"
#include "qa/network_unified_frame.h"
#include "qa/unified_frame_visuals.h"
#include "qa/unified_frame_components.h"
#include "qa/unified_frame_metadata.h"
#include "../application/native_q2_publication.h"
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
    qa_unified_presentation_event *model;
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
typedef struct q2_native_picture q2_native_picture;
typedef struct q2_bank {
    struct q2_bank *next;
    struct frontend_unified_q2 *owner;
    char *content;
    qa_vfs *files;
    const qa_product *product;
    qa_scene_resources *images;
    qa_material_library *materials;
    frontend_remote_q2_effects *effects;
    qa_builtin_random entity_random;
    frontend_q2_footsteps *footsteps;
    frontend_received_music *music;
    qa_audio_bank *sounds;
    q2_activation *activation;
    frontend_remote_q2_effects_profile profile;
    double frame_milliseconds;
    char *source_provider;
    q2_model *models;
    q2_native_picture *native_pictures;
    const qa_font *native_font;
    q2_alias *aliases;
    size_t alias_count;
    char *styles[256];
    uint64_t style_sequences[256];
} q2_bank;
struct q2_native_picture {
    struct q2_native_picture *next;
    char *name;
    qa_scene_image *image;
};
typedef struct q2_native {
    q2_bank *bank;
    qa_unified_component_identity identity;
    qa_unified_component_owner presentation_owner;
    char *provider,*layout;
    uint64_t owner_generation,generation;
    qa_net_protocol_id protocol;
    char **configs;
    size_t config_count;
    const qa_unified_q2_hud_configuration *configuration;
    int32_t inventory[256],player_number;
    bool hud,replace_status,camera;
    const qa_font *font;
} q2_native;
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
    qa_unified_presentation_event *story;
    q2_activation *story_owner,*sky_owner,*fog_owner;
    char *sky_name,*sky_content;
    qa_scene_image *sky_images[6];
    qa_vec3 sky_axis;
    float sky_rotation;
    bool sky_auto_rotate,fog_received,music_retiring;
    qa_scene_fog fog_start,fog_target;
    double fog_started_ms,fog_duration_ms;
    qa_unified_document *frame, *prepared_frame;
    qa_unified_document *status_metadata, *prepared_status_metadata;
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
    q2_native *native;
    q2_native status, prepared_status;
    size_t native_count;
    uint64_t native_revision;
    unsigned busy;
};
static qa_json_id get(const qa_json_document *j,qa_json_id row,const char *name)
{ return qa_json_get(j,row,name); }
static char *text_copy(const char *text)
{
    if (!text) return NULL;
    size_t n=strlen(text)+1; char *copy=malloc(n); if (copy) memcpy(copy,text,n); return copy;
}
static bool source_actor(frontend_unified_q2 *o,qa_actor_id source,qa_actor_id *out,qa_error *e)
{
    return frontend_remote_unified_source_actor(o->replica,qa_unified_document_frame(o->frame),source,
        o->frontend->capture || o->frontend->source_restoring,out,e);
}
static void record_free(qa_unified_presentation_event *row)
{ if (row) { qa_unified_presentation_event_dispose(row); free(row); } }
static bool record_copy(const qa_unified_presentation_event *row,qa_unified_presentation_event **out,qa_error *e)
{
    qa_unified_presentation_event *copy=calloc(1,sizeof(*copy));
    if (!copy) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 Source presentation");
    if (!qa_unified_presentation_event_clone(row,copy,e)) { record_free(copy); return false; }
    *out=copy; return true;
}
static bool current(const frontend_unified_q2 *o,qa_error *e)
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
static bool activation(frontend_unified_q2 *o,const qa_unified_presentation_owner *token,q2_activation **out,qa_error *e)
{
    if (!token->generation) { *out=NULL; return true; }
    q2_activation *a=o->activations;
    while (a && (a->generation!=token->generation || strcmp(a->provider,token->provider))) a=a->next;
    if (!a) {
        a=calloc(1,sizeof(*a)); if (!a) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 Source owner");
        a->provider=text_copy(token->provider); if (!a->provider) { free(a); return false; }
        a->generation=token->generation; a->next=o->activations; o->activations=a;
    }
    *out=a; return true;
}
static void visual_free(q2_visual *v)
{ record_free(v->model); free(v->content); free(v->path); free(v->source_provider); free(v); }
static q2_visual *visual_read(frontend_unified_q2 *o,qa_actor_id a)
{
    for (q2_visual *v=o->visuals;v;v=v->next) if (qa_actor_id_equal(v->actor,a)) return v;
    return NULL;
}
static bool visual_prepare(frontend_unified_q2 *o,const qa_unified_presentation_event *row,qa_actor_id a,q2_visual **out,qa_error *e)
{
    q2_activation *owner=NULL;
    if (!activation(o,&row->owner,&owner,e) || (owner && owner->retired))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 presentation uses a retired actual Source owner");
    q2_visual *v=visual_read(o,a);
    if (!v) { v=calloc(1,sizeof(*v)); if (!v) return false; v->actor=a; v->visible=true; v->next=o->visuals; o->visuals=v; }
    if (v->activation!=owner) {
        record_free(v->model); v->model=NULL;
        free(v->path); v->path=NULL; v->effects=0; v->event=0; v->event_frame=0; v->visible=true;
    }
    v->activation=owner; *out=v; return true;
}
static bool bank(frontend_unified_q2 *,const char *,q2_activation *,const char *,frontend_remote_q2_effects_profile,bool,q2_bank **,qa_error *);
static bool source_bank(frontend_unified_q2 *,const qa_unified_presentation_event *,bool,q2_bank **,qa_error *);
static bool model(void *,const char *,bool,qa_scene_model **,qa_error *);
static bool source_profile(const qa_unified_presentation_event *row,bool required,frontend_remote_q2_effects_profile *profile,qa_error *e)
{
    *profile=(frontend_remote_q2_effects_profile)row->q2_profile;
    return (!required && !row->q2_profile) ||
        ((row->q2_profile==FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC || row->q2_profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE) &&
        row->provider && *row->provider && row->q2_interval_ns) ||
        frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 presentation lost its actual Source profile and interval");
}
static bool visual_record(frontend_unified_q2 *o,const qa_unified_presentation_event *row,qa_actor_id *a,qa_error *e)
{
    qa_actor_id id={0};
    switch (row->payload.kind) {
    case QA_UNIFIED_PRESENTATION_MODEL: id=row->payload.value.model.actor; break;
    case QA_UNIFIED_PRESENTATION_VISIBILITY: id=row->payload.value.visibility.actor; break;
    case QA_UNIFIED_PRESENTATION_BUILTIN: id=row->payload.value.builtin.actor; break;
    default: return false;
    }
    return source_actor(o,id,a,e) && a->registry && row->content && *row->content;
}
static bool visual_apply(frontend_unified_q2 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    qa_actor_id a; q2_visual *v=NULL; q2_bank *b=NULL;
    if (!visual_record(o,row,&a,e) || !visual_prepare(o,row,a,&v,e)) return false;
    bool is_model=row->payload.kind==QA_UNIFIED_PRESENTATION_MODEL;
    const qa_unified_model_state *m=is_model?&row->payload.value.model:NULL;
    qa_unified_presentation_event *copy=NULL;
    if (is_model) {
        qa_scene_model *scene=NULL;
        if (!m->path || !record_copy(row,&copy,e) || !bank(o,row->content,NULL,NULL,0,false,&b,e) ||
            !model(b,m->path,true,&scene,e)) { record_free(copy); return false; }
        for (size_t i=0;i<m->attachment_count;++i)
            if (!model(b,m->attachments[i].path,true,&scene,e)) { record_free(copy); return false; }
    }
    if (row->q2_profile && !source_bank(o,row,true,&b,e)) { record_free(copy); return false; }
    const char *provider=row->q2_profile?row->provider:NULL;
    bool same_source=(v->source_provider!=NULL)==(provider!=NULL) && (!provider || !strcmp(v->source_provider,provider));
    if (!same_source || (v->content && strcmp(v->content,row->content))) {
        record_free(v->model); v->model=NULL; free(v->path); v->path=NULL;
        v->effects=0; v->event=0; v->event_frame=0; v->visible=true;
    }
    char *content=text_copy(row->content),*source=text_copy(provider),*path=is_model?text_copy(m->path):NULL;
    if (!content || (provider && !source) || (is_model && !path)) { free(content);free(source);free(path);record_free(copy);return false; }
    free(v->content);v->content=content;free(v->source_provider);v->source_provider=source;
    if (is_model) { free(v->path);v->path=path;record_free(v->model);v->model=copy;v->effects=m->effects; }
    else if (row->payload.kind==QA_UNIFIED_PRESENTATION_VISIBILITY) v->visible=row->payload.value.visibility.visible;
    else { const qa_unified_builtin_event *event=&row->payload.value.builtin;
        v->event=event->kind==QA_BUILTIN_ITEM?2u:(uint32_t)event->code;v->event_frame=o->frame_number; }
    return true;
}
bool frontend_unified_q2_owner_validate(frontend_unified_q2 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    return o && row && current(o,e) && row->payload.kind==QA_UNIFIED_PRESENTATION_OWNER &&
        row->payload.value.owner.owner.generation && frontend_unified_q2_rr_owner_validate(o->rr_hud,row,e);
}
bool frontend_unified_q2_owner_retire(frontend_unified_q2 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    if (!frontend_unified_q2_owner_validate(o,row,e) || !frontend_unified_q2_idle(o) ||
        !frontend_unified_q2_rr_owner_retire(o->rr_hud,row,e)) return false;
    const qa_unified_owner_event *event=&row->payload.value.owner; q2_activation *a=NULL;
    if (!activation(o,&event->owner,&a,e)) return false;
    if (event->kind!=QA_UNIFIED_OWNER_RETIRED || a->retired) return true;
    q2_loop **loop=&o->loops;
    while (*loop) { q2_loop *l=*loop;
        if (l->activation!=a) { loop=&l->next; continue; }
        if (!frontend_unified_events_sound_stop_loop(o->events,l->actor,e)) return false;
        *loop=l->next;free(l);
    }
    q2_visual **next=&o->visuals;
    while (*next) { q2_visual *v=*next;if (v->activation==a) { *next=v->next;visual_free(v); }else next=&v->next; }
    for (size_t i=0;i<2;++i) if (o->help_owner[i]==a) { free(o->help_text[i]);o->help_text[i]=NULL;o->help_owner[i]=NULL; }
    if (o->inventory_owner==a) inventory_clear(o);
    if (o->score_owner==a) scores_clear(o);
    if (o->story_owner==a) { record_free(o->story);o->story=NULL;o->story_owner=NULL; }
    if (o->sky_owner==a) sky_clear(o);
    if (o->fog_owner==a) { o->fog_received=false;o->fog_owner=NULL;o->fog_start=(qa_scene_fog){0};o->fog_target=(qa_scene_fog){0};o->fog_started_ms=0;o->fog_duration_ms=0; }
    if (o->view_owner==a) { free(o->view_content);free(o->view_provider);o->view_content=NULL;o->view_provider=NULL;
        o->view_owner=NULL;o->view_profile=0;o->view_layouts=0;o->view_gun_offset=qa_v3(0,0,0);o->view_actor=(qa_actor_id){0};
        o->view_blend_present=false;o->view_damage_present=false;o->view_blend=(qa_scene_vec4){0};o->view_damage_blend=(qa_scene_vec4){0}; }
    for (q2_bank *b=o->banks;b;b=b->next) if (b->activation==a) {
        for (size_t i=0;i<256;++i) {free(b->styles[i]);b->styles[i]=NULL;b->style_sequences[i]=0;}
        if (b->effects && !frontend_remote_q2_effects_retire_presentation(b->effects,e)) return false;
    }
    o->music_retiring=true;
    for (q2_bank *b=o->banks;b;b=b->next) if (b->activation==a && !frontend_received_music_destroy(&b->music,e)) { o->music_retiring=false;return false; }
    o->music_retiring=false;a->retired=true;return true;
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
    const qa_unified_frame *frame=qa_unified_document_frame(d);
    if (!frame || !frame->visuals) return false;
    for (size_t i=0;i<frame->visuals->model_count;++i) {
        const qa_unified_model_state *row=frame->visuals->models+i; qa_actor_id actual;
        if (!frontend_remote_unified_source_actor(o->replica,frame,row->actor,false,&actual,e)) return false;
        if (!qa_actor_id_equal(actual,id)) continue;
        if (row->frame<INT32_MIN || row->frame>INT32_MAX) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 effect pose frame exceeds its signed Source frame");
        frontend_remote_q2_effects_pose p={.actor=id,.origin=row->origin,.angles=row->angles,
            .frame=(int32_t)row->frame,.scale=row->scale};
        q2_visual *visual=visual_read(o,id);
        if (visual && !strcmp(visual->content?visual->content:"",b->content)) {
            p.effects=visual->effects; p.event=visual->event_frame==o->frame_number?visual->event:0;
        }
        if (visual && visual->model) {
            p.scale=visual->model->payload.value.model.scale;
        }
        if (p.scale == 0) p.scale=1;
        qa_scene_family family=row->family==QA_GAME_Q1?QA_SCENE_Q1:row->family==QA_GAME_Q2?QA_SCENE_Q2:QA_SCENE_Q3;
        qa_scene_image_options options={.family=family,.usage=QA_IMAGE_USAGE_SKIN,.wrap=QA_SCENE_REPEAT,
            .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=255};
        frontend_unified_model m;
        if (!frontend_unified_media_model(o->media,row->content,row->path,family,&options,&m,e)) return false;
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
        const qa_unified_frame *frame=qa_unified_document_frame(o->frame);
        if (!frame || !frame->player) return false;
        qa_vec3 origin=frame->player->view.origin; float height=frame->player->view.view_height;
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
        qa_builtin_random_seed(&b->entity_random,1);
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
static bool source_bank(frontend_unified_q2 *o,const qa_unified_presentation_event *row,bool effects,q2_bank **out,qa_error *e)
{
    q2_activation *a=NULL; frontend_remote_q2_effects_profile profile; q2_bank *b=NULL;
    if (!activation(o,&row->owner,&a,e) || (a && a->retired) || !source_profile(row,true,&profile,e) ||
        !bank(o,row->content,a,row->provider,profile,false,&b,e)) return false;
    b->frame_milliseconds=(double)row->q2_interval_ns/1e6;
    return bank(o,row->content,a,row->provider,profile,effects,out,e);
}
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
    if (!domain || !domain->command_context.owner)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 music has no actual private CLIENT receiver");
    *out=(frontend_music_origin){.kind=FRONTEND_MUSIC_REMOTE,.receiver=domain->command_context.owner,
        .physical_seat=domain->physical_seat,.recipe=recipe,.recipe_content=b->content,
        .catalog=qa_executable_recipe_catalog(recipe),.product=b->product->id,.files=b->files,.context=b,.current=music_current};
    return true;
}
static bool music_receive(frontend_unified_q2 *o,const qa_unified_presentation_event *row,const char *track,qa_error *e)
{
    q2_bank *b=NULL;
    if (!source_bank(o,row,false,&b,e)) return false;
    if (!o->frontend->audio) return true;
    frontend_music_origin origin;
    return music_origin(b,&origin,e) && (b->music || frontend_received_music_create(o->frontend,&origin,&b->music,e)) &&
        frontend_received_music_play(b->music,track?track:"",e);
}
static bool achievement(frontend_unified_q2 *o,const qa_unified_presentation_event *row,const char *award,bool publish,qa_error *e)
{
    qa_vfs *files=NULL;const qa_product *product=NULL;
    if (!qa_executable_recipe_content(frontend_remote_unified_recipe(o->replica),row->content,&files,&product,e) || product->family!=QA_GAME_Q2) return false;
    if (!award || !*award) return true;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    qa_player_progress *store=qa_application_player_progress(domain->application);
    if (!store) return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 achievement has no installed player profile store");
    if (!publish) return true;
    size_t content_size=strlen(row->content),award_size=strlen(award),length=12+content_size+award_size;
    char *identity=malloc(length+1);if (!identity) return false;
    snprintf(identity,length+1,"achievement:%s:%s",row->content,award);
    char participant[32];int count=snprintf(participant,sizeof(participant),"local-seat:%u",(unsigned)o->frontend->seats[domain->physical_seat].id);
    bool okay=count>0 && (size_t)count<sizeof(participant);
    if (okay) {qa_progress_event event={.kind=QA_PROGRESS_ACHIEVEMENT,.source=QA_GAME_Q2,
        .participant={(const uint8_t *)participant,(size_t)count},.event={(const uint8_t *)identity,length},
        .value.award={(const uint8_t *)award,award_size}};bool inserted;okay=qa_player_progress_record(store,&event,&inserted,e);}
    free(identity);return okay;
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
static void native_clear(q2_native *v)
{
    qa_unified_component_identity_dispose(&v->identity);
    free(v->provider); free(v->layout);
    for (size_t i=0;i<v->config_count;++i) free(v->configs[i]);
    free(v->configs);
    *v=(q2_native){0};
}
static bool native_text(const char *text,char **out,qa_error *e)
{
    if (!text) return false;
    size_t size=strlen(text)+1; *out=malloc(size);
    if (!*out) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining native Q2 Source text");
    memcpy(*out,text,size); return true;
}
static const char *native_config(void *context,int32_t index)
{
    const q2_native *v=context;
    if (index < 0) return "";
    if (v->configuration) {
        size_t low=0,high=v->configuration->configstring_count;
        const qa_unified_q2_configstring *rows=v->configuration->configstrings;
        while (low<high) {
            size_t middle=low+(high-low)/2;
            if (rows[middle].index<(uint32_t)index) low=middle+1; else high=middle;
        }
        return low<v->configuration->configstring_count && rows[low].index==(uint32_t)index?rows[low].value:"";
    }
    return (size_t)index<v->config_count && v->configs[index]?v->configs[index]:"";
}
static const qa_scene_image *native_picture(void *context,const char *name,qa_error *e)
{
    q2_native *v=context;
    for (q2_native_picture *p=v->bank->native_pictures;p;p=p->next) if (!strcmp(p->name,name)) return p->image;
    const char *slash=strrchr(name,'/'),*extension=strrchr(name,'.');
    bool direct=name[0]=='/' || name[0]=='\\' ||
        (v->protocol.kind==QA_NET_Q2KEX_2023 && slash && extension && extension>slash && extension[1]);
    size_t length=strlen(name);
    if (length>SIZE_MAX-11) return NULL;
    char *path=malloc(length+11); q2_native_picture *p=calloc(1,sizeof(*p));
    if (!path || !p) { free(path); free(p); frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining native Q2 picture"); return NULL; }
    if (direct) strcpy(path,name+(name[0]=='/' || name[0]=='\\')); else snprintf(path,length+11,"pics/%s.pcx",name);
    qa_scene_image_options options={.family=QA_SCENE_Q2,.usage=QA_IMAGE_USAGE_PICTURE,
        .wrap=QA_SCENE_CLAMP,.filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.transparent=true,.transparent_index=255};
    if (direct && name[0]!='/' && name[0]!='\\') {
        options.usage=!strncmp(name,"sprites/",8)?QA_IMAGE_USAGE_SPRITE:QA_IMAGE_USAGE_SKIN;
        options.mipmap=options.usage!=QA_IMAGE_USAGE_SPRITE;
        options.filter=options.mipmap?QA_SCENE_LINEAR_MIPMAP_LINEAR:QA_SCENE_LINEAR;
        options.wrap=QA_SCENE_REPEAT;
    }
    bool okay=qa_scene_image_load(v->bank->images,path,&options,&p->image,e); free(path);
    if (!okay) { free(p); return NULL; }
    p->name=malloc(length+1);
    if (!p->name) { qa_scene_image_release(p->image); free(p); return NULL; }
    memcpy(p->name,name,length+1); p->next=v->bank->native_pictures; v->bank->native_pictures=p; return p->image;
}
static bool native_font(frontend_unified_q2 *o,q2_native *v,qa_error *e)
{
    if (!v->bank->native_font) {
        qa_font_library *fonts;qa_scene_resources *images;qa_material_library *materials;qa_audio_bank *sounds;
        const qa_scene_image *conchars=native_picture(v,"conchars",e);
        if (!conchars || !frontend_unified_media_bank(o->media,v->bank->content,&images,&materials,&fonts,&sounds,e) ||
            !qa_font_classic_create(fonts,"unified-native-q2:conchars",conchars,QA_FONT_BAKED_COLOR,&v->bank->native_font,e))return false;
    }
    v->font=v->bank->native_font;return true;
}
static bool native_configs(const qa_unified_component_q2 *row,const q2_native *old,q2_native *v,qa_error *e)
{
    qa_q2_config_layout layout; qa_q2_codec codec={.protocol=v->protocol};
    if (!qa_q2_config_layout_read(&codec,&layout,e)) return false;
    v->config_count=layout.max_configs; v->configs=calloc(v->config_count,sizeof(*v->configs));
    if (!v->configs) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining native Q2 configstrings");
    if (!row->replace_configstrings) {
        if (!old || old->config_count!=v->config_count) return false;
        for (size_t i=0;i<v->config_count;++i) if (old->configs[i] && !native_text(old->configs[i],v->configs+i,e)) return false;
        return true;
    }
    for (size_t i=0;i<row->configstring_count;++i) {
        const qa_unified_component_configstring *config=row->configstrings+i;
        if (config->index>=v->config_count || v->configs[config->index] || !native_text(config->value,v->configs+config->index,e)) return false;
    }
    return true;
}
static bool native_read(frontend_unified_q2 *o,const qa_unified_component_q2 *row,
    const q2_native *old,q2_native *v,qa_error *e)
{
    bool okay=native_text(row->owner.provider,&v->provider,e) && qa_unified_component_identity_clone(&row->identity,&v->identity,e);
    if (!okay) return false;
    v->owner_generation=row->owner.generation; v->generation=row->generation;
    v->presentation_owner=(qa_unified_component_owner){v->provider,v->owner_generation};
    if (old && (old->owner_generation!=v->owner_generation || old->generation!=v->generation)) old=NULL;
    if (old) {
        if (!qa_unified_component_identity_equal(&v->identity,&old->identity))
            return frontend_unified_fail(e,QA_ERROR_FORMAT,"Native Q2 activation changed its qualified module identity");
        v->hud=old->hud; v->replace_status=old->replace_status; v->camera=old->camera;
        v->protocol=old->protocol; v->bank=old->bank;
    } else {
        qa_executable_recipe *recipe=frontend_remote_unified_recipe(o->replica);
        const qa_recipe_provider *admitted=NULL; const qa_catalog_mod *mod=NULL; const qa_product *product=NULL;
        for (size_t i=0;!admitted && i<qa_executable_recipe_provider_count(recipe);++i) {
            const qa_recipe_provider *provider=qa_executable_recipe_provider(recipe,i);
            if (provider->selection.runtime!=QA_PROGRAM_NATIVE || !provider->selection.component || !provider->declaration ||
                strcmp(row->identity.provider,provider->selection.instance)) continue;
            const qa_catalog_mod *metadata=qa_catalog_mod_find(qa_executable_recipe_catalog(recipe),provider->selection.component);
            const qa_product *content=metadata?qa_catalog_product(qa_executable_recipe_catalog(recipe),metadata->product):NULL;
            if (!metadata || metadata->unavailable || !content || content->family!=QA_GAME_Q2 ||
                (content->edition!=QA_EDITION_CLASSIC && content->edition!=QA_EDITION_RERELEASE)) continue;
            qa_unified_component_identity expected={0};
            okay=application_unified_component_identity_create(metadata,content,provider->selection.instance,&expected,e);
            bool matches=okay && qa_unified_component_identity_equal(&v->identity,&expected);
            qa_unified_component_identity_dispose(&expected);
            if (!okay) return false;
            if (matches) { admitted=provider; mod=metadata; product=content; }
        }
        if (!admitted || !mod || !product) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Native Q2 component differs from its admitted recipe identity");
        qa_json_document *declaration=NULL;
        if (!qa_json_parse(qa_resource_bytes(admitted->declaration),&declaration,e)) return false;
        qa_json_id policy=get(declaration,qa_json_root(declaration),"clientPresentation");
        qa_unified_component_hud hud=qa_json_string_equal(declaration,get(declaration,policy,"hud"),"replace-status")?QA_UNIFIED_COMPONENT_HUD_REPLACE:
            qa_json_string_equal(declaration,get(declaration,policy,"hud"),"layout-overlay")?QA_UNIFIED_COMPONENT_HUD_OVERLAY:QA_UNIFIED_COMPONENT_HUD_NONE;
        v->camera=qa_json_string_equal(declaration,get(declaration,policy,"view"),"playerstate");
        v->hud=hud!=QA_UNIFIED_COMPONENT_HUD_NONE; v->replace_status=hud==QA_UNIFIED_COMPONENT_HUD_REPLACE;
        qa_json_destroy(declaration);
        v->protocol=(qa_net_protocol_id){.kind=product->edition==QA_EDITION_CLASSIC?QA_NET_Q2_34:QA_NET_Q2KEX_2023};
        if (!bank(o,product->identity,NULL,admitted->selection.instance,
            product->edition==QA_EDITION_CLASSIC?FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC:FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE,false,&v->bank,e)) return false;
    }
    qa_unified_component_hud hud=!v->hud?QA_UNIFIED_COMPONENT_HUD_NONE:
        v->replace_status?QA_UNIFIED_COMPONENT_HUD_REPLACE:QA_UNIFIED_COMPONENT_HUD_OVERLAY;
    if (row->hud!=hud) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Native Q2 component changed its qualified presentation policy");
    if (!v->hud) return true;
    okay=row->protocol.kind==v->protocol.kind && row->protocol.flags==v->protocol.flags && row->protocol.revision==v->protocol.revision &&
        native_configs(row,old,v,e) && native_text(row->layout,&v->layout,e);
    v->player_number=row->player_number;
    for (size_t i=0;i<256;++i) v->inventory[i]=row->inventory[i];
    if (!okay) return false;
    return native_font(o,v,e);
}
bool frontend_unified_q2_components_control(frontend_unified_q2 *o,const qa_unified_document *d,qa_error *e)
{
    if (!o || o->busy || o->prepared_frame || !d || !current(o,e)) return false;
    const qa_unified_control *control=qa_unified_document_control(d);
    const qa_unified_components_control *update=control&&control->kind==QA_UNIFIED_CONTROL_COMPONENTS?&control->value.components:NULL;
    if (!update || o->native_revision==UINT64_MAX || update->revision!=o->native_revision+1) return false;
    uint64_t revision=update->revision; size_t count=update->native_count;
    q2_native *next=count?calloc(count,sizeof(*next)):NULL;
    if (count && !next) return false;
    bool okay=true;
    for (size_t i=0;okay && i<count;++i) {
        const qa_unified_component_q2 *row=update->native+i; const q2_native *old=NULL;
        for (size_t k=0;k<o->native_count;++k) if (!strcmp(row->owner.provider,o->native[k].provider)) old=o->native+k;
        okay=native_read(o,row,old,next+i,e);
        for (size_t k=0;okay && k<i;++k) okay=strcmp(next[k].provider,next[i].provider)!=0;
    }
    for (size_t i=0;okay && i<count;++i) okay=frontend_unified_events_component_admit(o->events,&next[i].presentation_owner,next[i].bank->content,e);
    for (size_t i=0;okay && i<o->native_count;++i) {
        bool retained=false;
        for (size_t k=0;k<count;++k) if (!strcmp(next[k].provider,o->native[i].provider) && next[k].owner_generation==o->native[i].owner_generation) retained=true;
        if (!retained) okay=frontend_unified_events_component_retire(o->events,&o->native[i].presentation_owner,e);
    }
    if (okay) {
        for (size_t i=0;i<o->native_count;++i) native_clear(o->native+i);
        free(o->native); o->native=next; o->native_count=count; o->native_revision=revision; return true;
    }
    for (size_t i=0;i<count;++i) native_clear(next+i);
    free(next); return false;
}
static bool native_frame_read(frontend_unified_q2 *o,const qa_unified_document *d,qa_error *e)
{
    const qa_unified_frame *frame=qa_unified_document_frame(d);
    const qa_unified_frame_components *components=frame?frame->components:NULL;
    if ((!components && o->native_count) || (components &&
        (components->revision!=o->native_revision || components->native_count!=o->native_count)))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Native Q2 frame lacks reliable component admission");
    qa_actor_id viewer; uint32_t number_id;
    if (!frontend_remote_unified_player(o->replica,&viewer,&number_id)) return false;
    size_t cameras=0,replacements=0;
    for (size_t i=0;components && i<components->native_count;++i) {
        const qa_unified_native_component *row=components->native+i; const q2_native *v=NULL;
        for (size_t k=0;k<o->native_count;++k) if (!strcmp(row->owner.provider,o->native[k].provider)) v=o->native+k;
        qa_actor_id actual;
        if (!v || row->owner.generation!=v->owner_generation || row->generation!=v->generation ||
            (row->hud!=NULL)!=v->hud || (row->view!=NULL)!=v->camera ||
            !frontend_remote_unified_source_actor(o->replica,frame,row->viewer,false,&actual,e) ||
            !qa_actor_id_equal(actual,viewer) || (row->hud && row->hud->stat_count!=(v->protocol.kind==QA_NET_Q2_34?32u:64u)) ||
            (row->view && row->view->rerelease!=(v->protocol.kind==QA_NET_Q2KEX_2023)))
            return frontend_unified_fail(e,QA_ERROR_FORMAT,"Native Q2 frame changed its admitted source or full viewer");
        cameras+=row->view!=NULL; replacements+=v->replace_status;
        for (size_t k=0;k<i;++k) if (!strcmp(row->owner.provider,components->native[k].owner.provider)) return false;
    }
    return cameras<=1 && replacements<=1;
}
static bool status_prepare(frontend_unified_q2 *o,const qa_unified_frame *frame,qa_error *e)
{
    o->prepared_status=(q2_native){0};
    if (!frame->player->has_q2_hud) return true;
    const qa_unified_document *document=frontend_remote_unified_metadata_document(o->replica,frame);
    const qa_unified_frame_metadata *metadata=qa_unified_document_metadata(document);
    const qa_unified_configuration_state *selected=NULL;
    for (size_t i=0;metadata && i<metadata->configuration_count;++i)
        if (qa_actor_id_equal(metadata->configurations[i].actor,frame->player->actor)) {
            selected=metadata->configurations+i;break;
        }
    if (!selected || !selected->q2_hud) return true;
    const qa_recipe_provider *provider=frontend_remote_unified_provider(o->replica,QA_ROLE_HUD,"");
    if (!provider) return true;
    const qa_product *product=qa_catalog_product(qa_executable_recipe_catalog(
        frontend_remote_unified_recipe(o->replica)),provider->selection.product);
    q2_native *status=&o->prepared_status;
    status->configuration=selected->q2_hud;status->protocol=selected->q2_hud->protocol;
    status->hud=true;status->replace_status=true;
    if (!bank(o,selected->hud.content,NULL,selected->hud.provider,
        product->edition==QA_EDITION_RERELEASE?FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE:FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC,
        false,&status->bank,e)) return false;
    return native_font(o,status,e) && qa_unified_document_retain(document,&o->prepared_status_metadata,e);
}
bool frontend_unified_q2_status_replacement(const frontend_unified_q2 *o,bool *out,qa_error *e)
{
    if (!o || !out || o->busy || !current(o,e)) return false;
    *out=false;
    if (!o->frame) return true;
    *out=o->status.hud;
    for (size_t i=0;i<o->native_count;++i) if (o->native[i].replace_status) *out=true;
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
static bool temporary(frontend_unified_q2 *o,const qa_unified_q2_temporary *event,qa_q2_temp_entity *t,qa_actor_id actors[7],qa_error *e)
{
    if (event->field_count>7) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 temporary exceeds its native field capacity");
    *t=(qa_q2_temp_entity){.type=event->type,.field_count=event->field_count};memset(actors,0,7*sizeof(*actors));
    for (size_t i=0;i<t->field_count;++i) {
        const qa_unified_q2_temp_field *from=event->fields+i;qa_q2_temp_field *to=t->fields+i;
        to->name=from->name;to->kind=from->kind;
        if (from->kind==QA_Q2_TEMP_INTEGER) {to->value.integer=from->integer;
            if ((from->name==QA_Q2_TEMP_ENTITY1 || from->name==QA_Q2_TEMP_ENTITY2) && !source_actor(o,from->actor,actors+i,e)) return false;}
        else {to->value.vector[0]=from->vector.x;to->value.vector[1]=from->vector.y;to->value.vector[2]=from->vector.z;}
    }
    return true;
}
static bool temporary_receive(frontend_unified_q2 *o,const qa_unified_presentation_event *row,const qa_unified_q2_temporary *event,qa_error *e)
{
    qa_q2_temp_entity t;qa_actor_id actors[7];q2_bank *b=NULL;
    if (!temporary(o,event,&t,actors,e) || !source_bank(o,row,true,&b,e)) return false;
    if (event->rerelease!=(b->profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 temporary protocol differs from its actual Source");
    for (size_t i=0;i<t.field_count;++i)
        if ((t.fields[i].name==QA_Q2_TEMP_ENTITY1 || t.fields[i].name==QA_Q2_TEMP_ENTITY2) && actors[i].registry &&
            !alias(b,t.fields[i].value.integer,actors[i],e)) return false;
    ++o->busy;bool okay=frontend_remote_q2_effects_temporary(b->effects,&t,actors,row->seconds*1000,row->seconds*1000,e);--o->busy;
    return okay;
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
static bool userinfo(frontend_unified_q2 *o,const qa_unified_q2_player_event *event,bool publish,qa_error *e)
{
    if (!event->text) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 userinfo lost its Source name");
    if (!publish) return true;
    q2_player_name *v=o->names;while (v && v->slot!=event->slot)v=v->next;
    if (!v){v=calloc(1,sizeof(*v));if (!v)return false;v->slot=event->slot;v->next=o->names;o->names=v;}
    char *copy=text_copy(event->text);if (!copy)return false;free(v->name);v->name=copy;return true;
}
static bool localized(frontend_unified_q2 *o,const qa_unified_presentation_event *row,const char *input,
    const qa_unified_message_arg *args,size_t count,qa_buffer *out,qa_error *e)
{
    char *text=text_copy(input?input:"");if (!text)return false;
    *out=(qa_buffer){(uint8_t *)text,strlen(text)};
    if (row->q2_profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE)return true;
    q2_bank *b=NULL;const char **arguments=count?calloc(count,sizeof(*arguments)):NULL;
    bool okay=(!count || arguments) && bank(o,row->content,NULL,NULL,0,false,&b,e);
    for (size_t i=0;okay && i<count;++i){okay=args[i].kind==QA_BUILTIN_MESSAGE_STRING;arguments[i]=args[i].text?args[i].text:"";}
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    qa_ui_preferences preferences;qa_localization *catalog=NULL;qa_localization_options opts={.profile=QA_LOCALIZATION_Q2_RERELEASE};
    okay=okay && qa_ui_preferences_read(qa_application_cvars(domain->application),domain->physical_seat,&preferences,e) &&
        qa_localization_acquire(o->localizations,b->files,preferences.language,&opts,&catalog,e);
    if (okay){char output[1024];size_t n=qa_localize_presentation(catalog,text,arguments,count,false,output,sizeof(output));
        char *copy=malloc(n+1);if (!copy)okay=false;else{memcpy(copy,output,n+1);qa_buffer_free(out);*out=(qa_buffer){(uint8_t *)copy,n};}}
    qa_localization_release(catalog);free(arguments);
    if (okay)okay=player_names_expand(o,out,e);
    if (!okay)qa_buffer_free(out);
    return okay;
}
static bool received_text(frontend_unified_q2 *o,const qa_unified_presentation_event *row,const char *input,
    const qa_unified_message_arg *args,size_t count,bool center,bool console,bool chat,bool instant,double duration,qa_error *e)
{
    qa_buffer text={0};if (!localized(o,row,input,args,count,&text,e))return false;
    bool okay=center?qa_hud_center_print(o->hud,(const char *)text.data,nanoseconds(row->seconds),nanoseconds(duration),instant,UINT64_C(50000000),e):
        qa_hud_notify(o->hud,(const char *)text.data,chat,nanoseconds(row->seconds),UINT64_C(3000000000),e);
    if (okay && console){const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        qa_console_emit(domain->console,&domain->command_context,(const char *)text.data);}
    qa_buffer_free(&text);return okay;
}
static bool viewer_matches(frontend_unified_q2 *o,qa_actor_id source,bool *matches,qa_error *e)
{
    qa_actor_id a,viewer;uint32_t number_id;
    if (!source_actor(o,source,&a,e) || !frontend_remote_unified_player(o->replica,&viewer,&number_id))return false;
    *matches=!a.registry || qa_actor_id_equal(a,viewer);return true;
}
static bool selected_view_provider(frontend_unified_q2 *o,const char *provider)
{
    const qa_recipe_provider *character=frontend_remote_unified_provider(o->replica,QA_ROLE_CHARACTER,"");
    return character && provider && !strcmp(character->selection.instance,provider);
}
static bool player_overlay(frontend_unified_q2 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    const qa_unified_q2_player_event *event=&row->payload.value.q2_player;
    bool matches;qa_actor_id a;q2_activation *owner=NULL;
    if (!viewer_matches(o,event->actor,&matches,e))return false;
        if (!matches)return true;
    if (!source_actor(o,event->actor,&a,e) || !activation(o,&row->owner,&owner,e) || (owner && owner->retired))return false;
    if (event->kind==QA_Q2_PLAYER_VIEW){const qa_unified_q2_player_view *view=&event->view;
        if (!selected_view_provider(o,row->provider)) return true;
        if (!(view->layouts&2))inventory_clear(o);
        else for (size_t i=0;i<o->item_count;++i)o->items[i].selected=view->selected_item && !strcmp(o->items[i].item,view->selected_item);
        if (!(view->layouts&1)){o->help_visible=false;o->score_visible=false;}
        char *content=text_copy(row->content),*provider=text_copy(row->q2_profile?row->provider:NULL);
        if (!content || (row->q2_profile && !provider)){free(content);free(provider);return false;}
        free(o->view_content);free(o->view_provider);o->view_content=content;o->view_provider=provider;
        o->view_profile=(frontend_remote_q2_effects_profile)row->q2_profile;o->view_owner=owner;o->view_layouts=(uint32_t)view->layouts;
        o->view_gun_offset=view->gun_offset;o->view_actor=a;o->view_blend_present=true;o->view_damage_present=false;
        o->view_blend=(qa_scene_vec4){view->blend.x,view->blend.y,view->blend.z,view->blend.w};o->view_damage_blend=(qa_scene_vec4){0};
        return true;
    }
    if (event->kind==QA_Q2_PLAYER_INVENTORY){
        if (!event->visible){inventory_clear(o);return true;}
        q2_inventory_row *items=event->inventory_count?calloc(event->inventory_count,sizeof(*items)):NULL;size_t used=0;
        bool okay=!event->inventory_count || items;
        for (size_t i=0;okay && i<event->inventory_count;++i){const qa_unified_inventory_entry *from=event->inventory+i;if (from->count<=0)continue;
            q2_inventory_row *v=items+used++;v->item=text_copy(from->item);v->count=from->count;
            v->selected=event->selected_item && !strcmp(from->item,event->selected_item);
            const char *label=!strncmp(from->item,"q2:",3)?from->item+3:from->item;
            v->label=text_copy(label);okay=v->item && v->label;
            if (v->label)for (char *t=v->label;*t;++t)if (*t=='_')*t=' ';
        }
        if (!okay){for(size_t i=0;i<used;++i){free(items[i].item);free(items[i].label);}free(items);return false;}
        inventory_clear(o);o->items=items;o->item_count=used;o->inventory_visible=true;o->inventory_owner=owner;o->help_visible=false;return true;
    }
    size_t count=event->score_count;char **scores=count?calloc(count,sizeof(*scores)):NULL;bool okay=!count || scores;
    for (size_t i=0;okay && i<count;++i){const qa_unified_q2_score_row *from=event->scores+i;size_t length=strlen(from->name)+128;
        scores[i]=malloc(length);okay=scores[i]!=NULL;
        if (okay)snprintf(scores[i],length,"%d  %s  %dms  %dm%s",from->score,from->name,from->ping,from->minutes,from->spectator?"  Spectator":"");}
    if (!okay){for(size_t i=0;i<count;++i)free(scores[i]);free(scores);return false;}
    scores_clear(o);o->score_rows=scores;o->score_count=count;o->score_visible=true;o->score_owner=owner;o->help_visible=false;inventory_clear(o);return true;
}
static float fog_fraction(float value)
{
    uint8_t word=(uint8_t)(uint32_t)qa_source_float_to_i32(value*255.0f);
    return (float)word/255;
}
static qa_vec3 fog_color(qa_vec3 color)
{ return qa_v3(fog_fraction(color.x),fog_fraction(color.y),fog_fraction(color.z)); }
static qa_scene_fog fog_record(const qa_q2_fog *from)
{
    return (qa_scene_fog){.kind=QA_FOG_Q2,.color=fog_color(from->color),.density=from->density,.sky_factor=fog_fraction(from->sky_factor),
        .height_color=fog_color(from->start_color),.height_end_color=fog_color(from->end_color),
        .height_start=(float)qa_source_float_to_i32(from->start_distance),.height_end=(float)qa_source_float_to_i32(from->end_distance),
        .height_falloff=from->falloff,.height_density=from->height_density,.far_depth=1};
}
static qa_scene_fog fog_sample(frontend_unified_q2 *o,double seconds)
{
    double elapsed=seconds*1000-o->fog_started_ms;
    double front=o->fog_duration_ms==0 || elapsed>o->fog_duration_ms?1:elapsed/o->fog_duration_ms,back=1-front;
    qa_scene_fog a=o->fog_start,b=o->fog_target,result={.kind=QA_FOG_Q2,.far_depth=1};
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
static bool sky_receive(frontend_unified_q2 *o,const qa_unified_presentation_event *row,const qa_unified_q2_map_event *event,qa_error *e)
{
    q2_activation *owner=NULL;q2_bank *b=NULL;qa_scene_image *images[6]={0};
    bool okay=activation(o,&row->owner,&owner,e) && !(owner && owner->retired) && bank(o,row->content,NULL,NULL,0,false,&b,e);
    const char *name=event->resource?event->resource:"";size_t n=strlen(name);
    char *path=okay?malloc(n+7):NULL;if (okay && !path)return false;
    static const char *const suffixes[]={"rt","lf","bk","ft","up","dn"};
    qa_scene_image_options options={.family=QA_SCENE_Q2,.usage=QA_IMAGE_USAGE_SKY,.wrap=QA_SCENE_CLAMP,.filter=QA_SCENE_LINEAR,.transparent_index=-1};
    for (size_t i=0;okay && i<6;++i){snprintf(path,n+7,"env/%s%s",name,suffixes[i]);okay=qa_scene_image_load(b->images,path,&options,images+i,e);
        if (okay && !images[i]){images[i]=(qa_scene_image *)qa_scene_missing(b->images);qa_scene_image_retain(images[i]);}}
    free(path);
    char *name_copy=okay?text_copy(name):NULL,*content=okay?text_copy(row->content):NULL;okay=okay && name_copy && content;
    if (okay){sky_clear(o);o->sky_name=name_copy;o->sky_content=content;o->sky_rotation=event->value;o->sky_axis=event->direction;
        o->sky_auto_rotate=event->value!=0 && (event->flags&1);o->sky_owner=owner;memcpy(o->sky_images,images,sizeof(images));memset(images,0,sizeof(images));}
    else {free(name_copy);free(content);}for(size_t i=0;i<6;++i)qa_scene_image_release(images[i]);return okay;
}
static bool effect_replace(frontend_unified_q2 *o,q2_bank *bank_value,qa_actor_id actor_id,
    frontend_remote_q2_effects_presentation_kind kind,qa_error *e)
{
    for (q2_bank *b=o->banks;b;b=b->next)
        if (b!=bank_value && b->effects && !frontend_remote_q2_effects_remove_actor_presentation(b->effects,actor_id,kind,e)) return false;
    return true;
}
static bool muzzle_receive(frontend_unified_q2 *o,const qa_unified_presentation_event *row,
    const qa_unified_q2_muzzle *m,qa_error *e)
{
    qa_actor_id a;q2_bank *b=NULL;int32_t number_id=row->has_source_entity?row->source_entity:(int32_t)m->entity;
    if (!source_actor(o,m->actor,&a,e) || !a.registry || !source_bank(o,row,true,&b,e) || !alias(b,number_id,a,e)) return false;
    void *p=realloc(o->muzzles,(o->muzzle_count+1)*sizeof(*o->muzzles));if (!p)return false;o->muzzles=p;
    double ms=row->seconds*1000;++o->busy;bool okay;
    if (!m->monster)okay=frontend_remote_q2_effects_actor_muzzle(b->effects,a,m->flash,false,m->silenced,ms,ms,e);
    else if (m->has_pose)okay=frontend_remote_q2_effects_monster_muzzle_pose(b->effects,a,m->flash,m->origin,m->angles,m->scale,ms,ms,e);
    else okay=frontend_remote_q2_effects_monster_muzzle(b->effects,a,m->flash,m->origin,m->direction,ms,ms,e);
    --o->busy;
    if (okay)o->muzzles[o->muzzle_count++]=(q2_muzzle_receipt){a,number_id,m->flash,ms,m->monster,false};
    return okay;
}
static bool sound_receive(frontend_unified_q2 *o,const qa_unified_presentation_event *row,
    const char *path,qa_actor_id source,qa_vec3 origin,int32_t channel,float volume,float attenuation,unsigned loop,qa_error *e)
{
    qa_actor_id a;if (!source_actor(o,source,&a,e))return false;
    bool paired=false,okay=true;q2_loop *retained=NULL;q2_activation *owner=NULL;
    if (!loop)okay=frontend_unified_events_sound_mirrored(o->events,row,&paired,e);
    else if (loop==1){okay=activation(o,&row->owner,&owner,e) && !(owner && owner->retired);
        if (okay){retained=calloc(1,sizeof(*retained));okay=retained!=NULL;if (okay){retained->actor=a;retained->activation=owner;}}}
    double ms=row->seconds*1000;
    if (okay && loop==2)okay=frontend_unified_events_sound_stop_loop(o->events,a,e);
    else if (okay && loop==1){uint32_t raw=(uint32_t)o->frame_number;int32_t frame;memcpy(&frame,&raw,sizeof(frame));
        okay=frontend_unified_events_sound_loop_path(o->events,row->content,path?path:"",a,origin,qa_v3(0,0,0),ms,channel,volume,attenuation,frame,true,e);}
    else if (okay && !paired)okay=frontend_unified_events_sound_path(o->events,row->content,path?path:"",a,origin,ms,channel,volume,attenuation,0,e);
    if (okay && loop){q2_loop **next=&o->loops;while (*next){q2_loop *l=*next;if (qa_actor_id_equal(l->actor,a)){*next=l->next;free(l);}else next=&l->next;}
        if (retained){retained->next=o->loops;o->loops=retained;retained=NULL;}}
    free(retained);return okay;
}
static bool fog_receive(frontend_unified_q2 *o,const qa_unified_presentation_event *row,
    qa_actor_id source,const qa_q2_fog *fog,double duration,qa_error *e)
{
    bool matches;q2_activation *owner=NULL;
    if (!viewer_matches(o,source,&matches,e))return false;
        if (!matches)return true;
    if (!activation(o,&row->owner,&owner,e) || (owner && owner->retired))return false;
    if (duration!=0){o->fog_start=o->fog_target;o->fog_started_ms=row->seconds*1000;}
    o->fog_target=fog_record(fog);o->fog_duration_ms=duration;o->fog_received=true;o->fog_owner=owner;return true;
}
static bool map_receive(frontend_unified_q2 *o,const qa_unified_presentation_event *row,bool *mirrored,qa_error *e)
{
    const qa_unified_q2_map_event *v=&row->payload.value.q2_map;q2_bank *b=NULL;q2_activation *owner=NULL;
    switch (v->kind){
    case QA_Q2_MAP_MUSIC:return music_receive(o,row,v->resource?v->resource:v->text,e);
    case QA_Q2_MAP_ACHIEVEMENT:return achievement(o,row,v->text,true,e);
    case QA_Q2_MAP_LIGHTSTYLE:{if (v->style<0 || v->style>=256 || !source_bank(o,row,false,&b,e))return false;
        char *text=text_copy(v->text?v->text:"");if (!text)return false;free(b->styles[v->style]);b->styles[v->style]=text;b->style_sequences[v->style]=row->sequence;return true;}
    case QA_Q2_MAP_SKY:return sky_receive(o,row,v,e);
    case QA_Q2_MAP_STORY:{qa_unified_presentation_event *copy=NULL;
        if (!activation(o,&row->owner,&owner,e) || (owner && owner->retired) || !record_copy(row,&copy,e))return false;
        record_free(o->story);o->story=copy;o->story_owner=owner;return true;}
    case QA_Q2_MAP_FOG:return fog_receive(o,row,v->recipient,&v->fog,(double)v->duration*1000,e);
    case QA_Q2_MAP_HELP:{qa_buffer text={0};
        if (v->slot<1 || v->slot>2 || !activation(o,&row->owner,&owner,e) || (owner && owner->retired) ||
            !localized(o,row,v->text,v->arguments,v->argument_count,&text,e))return false;
        char *print=malloc(text.size+2);if (!print){qa_buffer_free(&text);return false;}
        memcpy(print,text.data,text.size);print[text.size]='\n';print[text.size+1]=0;
        free(o->help_text[v->slot-1]);o->help_text[v->slot-1]=(char *)text.data;o->help_owner[v->slot-1]=owner;
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        qa_console_emit(domain->console,&domain->command_context,print);free(print);*mirrored=true;return true;}
    case QA_Q2_MAP_SCREEN_BLEND:{bool matches;
        if (!viewer_matches(o,v->recipient,&matches,e) || !activation(o,&row->owner,&owner,e))return false;
        if (matches && o->view_provider && row->provider && !strcmp(o->view_provider,row->provider) && o->view_owner==owner){
            o->view_blend=(qa_scene_vec4){v->color.x,v->color.y,v->color.z,v->alpha};o->view_blend_present=true;}return true;}
    case QA_Q2_MAP_DYNAMIC_LIGHT:{qa_actor_id a;if (!source_actor(o,v->actor,&a,e) || !source_bank(o,row,true,&b,e))return false;
        frontend_remote_q2_effects_shadow_light light={.actor=a,.origin=v->origin,.color=v->color,.radius=v->radius,.intensity=v->intensity,
            .resolution=(int32_t)v->resolution,.fade_start=v->fade_start,.fade_end=v->fade_end,.lightstyle=v->style,.visible=v->visible,
            .cone=(v->flags&1)!=0,.direction=v->direction,.cos_half_angle=v->cone_cosine};
        return frontend_remote_q2_effects_shadow_light_set(b->effects,&light,e) && effect_replace(o,b,a,FRONTEND_REMOTE_Q2_SHADOW_LIGHT,e);}
    case QA_Q2_MAP_STEAM:case QA_Q2_MAP_FORCE_WALL:{qa_unified_q2_temp_field fields[7]={0};size_t n=0;
        if (v->kind==QA_Q2_MAP_STEAM){fields[n++]=(qa_unified_q2_temp_field){.name=QA_Q2_TEMP_ENTITY1,.kind=QA_Q2_TEMP_INTEGER,.integer=v->slot};
            fields[n++]=(qa_unified_q2_temp_field){.name=QA_Q2_TEMP_COUNT,.kind=QA_Q2_TEMP_INTEGER,.integer=v->count};}
        fields[n++]=(qa_unified_q2_temp_field){.name=QA_Q2_TEMP_POSITION1,.kind=QA_Q2_TEMP_VECTOR,.vector=v->origin};
        fields[n++]=(qa_unified_q2_temp_field){.name=v->kind==QA_Q2_MAP_STEAM?QA_Q2_TEMP_DIRECTION:QA_Q2_TEMP_POSITION2,.kind=QA_Q2_TEMP_VECTOR,.vector=v->direction};
        fields[n++]=(qa_unified_q2_temp_field){.name=QA_Q2_TEMP_COLOR,.kind=QA_Q2_TEMP_INTEGER,.integer=v->style};
        if (v->kind==QA_Q2_MAP_STEAM){fields[n++]=(qa_unified_q2_temp_field){.name=QA_Q2_TEMP_ENTITY2,.kind=QA_Q2_TEMP_INTEGER,.integer=qa_source_float_to_i32(v->value)};
            if (v->slot!=-1)fields[n++]=(qa_unified_q2_temp_field){.name=QA_Q2_TEMP_TIME,.kind=QA_Q2_TEMP_INTEGER,.integer=qa_source_float_to_i32(v->duration)};}
        qa_unified_q2_temporary t={.type=v->kind==QA_Q2_MAP_STEAM?QA_Q2_TE_STEAM:QA_Q2_TE_FORCEWALL,.rerelease=row->q2_profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE,.fields=fields,.field_count=n};
        return temporary_receive(o,row,&t,e);}
    case QA_Q2_MAP_AUTOSAVE: /* The actual application save owner handles this synchronous Source request. */return true;
    default:return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 map presentation has no installed CLIENT handler");
    }
}
static bool builtin_receive(frontend_unified_q2 *o,const qa_unified_presentation_event *row,bool *mirrored,qa_error *e)
{
    const qa_unified_builtin_event *v=&row->payload.value.builtin;q2_bank *b=NULL;qa_actor_id a;
    switch (v->kind){
    case QA_BUILTIN_SOUND:case QA_BUILTIN_STOP_SOUND:return sound_receive(o,row,v->resource,v->actor,v->origin,v->channel,v->volume,v->attenuation,
        v->kind==QA_BUILTIN_STOP_SOUND?2u:(v->flags&1)?1u:0u,e);
    case QA_BUILTIN_MUZZLE:{qa_unified_q2_muzzle m={.actor=v->actor,.flash=(uint16_t)v->code,
        .monster=v->resource && !strcmp(v->resource,"q2:monster-muzzle"),.silenced=(v->flags&128)!=0,
        .has_pose=v->has_muzzle_pose,.origin=v->origin,.direction=v->direction,.angles=v->muzzle_angles,.scale=v->muzzle_scale,
        .entity=row->has_source_entity?(uint16_t)row->source_entity:0};return muzzle_receive(o,row,&m,e);}
    case QA_BUILTIN_MESSAGE:case QA_BUILTIN_CENTERPRINT:{bool matches;
        if (!viewer_matches(o,v->actor,&matches,e))return false;
        if (!matches)return true;
        bool center=v->kind==QA_BUILTIN_CENTERPRINT;*mirrored=center || v->argument_count!=0;
        return received_text(o,row,v->text,v->arguments,v->argument_count,center,!center,v->code==3,true,3,e);}
    case QA_BUILTIN_Q2_ENTITY_EVENT:return visual_apply(o,row,e);
    case QA_BUILTIN_ITEM:{if (v->code==1)return visual_apply(o,row,e);bool matches;
        if (!viewer_matches(o,v->actor,&matches,e))return false;
        if (!matches)return true;
        qa_scene_image *image=NULL;qa_scene_image_options opts={.family=QA_SCENE_Q2,.usage=QA_IMAGE_USAGE_PICTURE,.wrap=QA_SCENE_CLAMP,
            .filter=QA_SCENE_LINEAR,.transparent=true,.transparent_index=255};
        bool okay=bank(o,row->content,NULL,NULL,0,false,&b,e);
        if (okay && v->resource && *v->resource)okay=qa_scene_image_load_exact(b->images,v->resource,&opts,&image,e);
        if (okay)okay=qa_hud_pickup(o->hud,v->text?v->text:"",image,nanoseconds(row->seconds+3),e);
        qa_scene_image_release(image);return okay;}
    case QA_BUILTIN_BEAM:{if (!source_actor(o,v->actor,&a,e) || !source_bank(o,row,true,&b,e))return false;
        const char *name=v->resource;double ms=row->seconds*1000;
        if (!name)return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 entity beam requires its received entity state");
        if (!strcmp(name,"q2:parasite") || !strcmp(name,"q2:medic-cable"))return frontend_remote_q2_effects_monster_beam(b->effects,a,v->origin,v->end,ms,e) &&
            effect_replace(o,b,a,FRONTEND_REMOTE_Q2_MONSTER_BEAM,e);
        if (!strcmp(name,"q2:grapple-cable")){qa_unified_q2_temp_field fields[4]={
            {.name=QA_Q2_TEMP_ENTITY1,.kind=QA_Q2_TEMP_INTEGER,.integer=row->has_source_entity?row->source_entity:0,.actor=v->actor},
            {.name=QA_Q2_TEMP_POSITION1,.kind=QA_Q2_TEMP_VECTOR,.vector=v->origin},
            {.name=QA_Q2_TEMP_POSITION2,.kind=QA_Q2_TEMP_VECTOR,.vector=v->end},
            {.name=QA_Q2_TEMP_OFFSET,.kind=QA_Q2_TEMP_VECTOR,.vector=v->direction}};
            qa_unified_q2_temporary t={.type=QA_Q2_TE_GRAPPLE_CABLE,.rerelease=row->q2_profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE,.fields=fields,.field_count=4};return temporary_receive(o,row,&t,e);}
        return frontend_remote_q2_effects_named_beam(b->effects,!strncmp(name,"q2:",3)?name+3:name,a,v->origin,v->end,v->value,ms,e);}
    case QA_BUILTIN_PARTICLES:case QA_BUILTIN_IMPACT:case QA_BUILTIN_EXPLOSION:case QA_BUILTIN_EFFECT:case QA_BUILTIN_TELEPORT:{
        const char *name=v->resource;if (name && !strncmp(name,"q2:",3))name+=3;
        if (v->kind==QA_BUILTIN_EFFECT && name && !strcmp(name,"entity-event"))return visual_apply(o,row,e);
        if (!name && v->kind==QA_BUILTIN_PARTICLES){switch(v->code){case 0:name="gunshot";break;case 1:name="blood";break;case 4:name="shotgun";break;
            case 9:name="sparks";break;case 12:name="screen-sparks";break;case 13:name="shield-sparks";break;case 14:name="bullet-sparks";break;
            case 26:name="greenblood";break;case 42:name="moreblood";break;case 46:name="electric-sparks";break;default:break;}}
        if (!name || !source_bank(o,row,true,&b,e))return false;
        bool palette=!strcmp(name,"splash") || !strcmp(name,"laser-sparks") || !strcmp(name,"laser_sparks") || !strcmp(name,"tunnel-sparks") || !strcmp(name,"welding-sparks");
        return frontend_remote_q2_effects_named_effect(b->effects,name,v->origin,v->direction,v->count,palette?v->code:0,row->seconds*1000,e);}
    case QA_BUILTIN_Q2_PLAYER_ANIMATION: /* Source animation is carried by its actual typed model frame. */return true;
    default:return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 builtin presentation has no installed CLIENT handler");
    }
}
static bool protocol_receive(frontend_unified_q2 *o,const qa_unified_presentation_event *row,bool *mirrored,qa_error *e)
{
    const qa_unified_q2_protocol_event *v=&row->payload.value.q2_protocol;
    switch(v->kind){
    case QA_Q2_SVC_SOUND:return sound_receive(o,row,v->resource,v->actor,v->origin,v->channel,v->volume,v->attenuation,0,e);
    case QA_Q2_SVC_MUZZLEFLASH:return muzzle_receive(o,row,&v->muzzle,e);
    case QA_Q2_SVC_TEMP_ENTITY:return temporary_receive(o,row,&v->temporary,e);
    case QA_Q2_SVC_ACHIEVEMENT:return achievement(o,row,v->text,true,e);
    case QA_Q2_SVC_FOG:return fog_receive(o,row,v->actor,&v->fog,v->transition_ms,e);
    case QA_Q2_SVC_PRINT:case QA_Q2_SVC_CENTERPRINT:case QA_Q2_SVC_LOCALIZED_PRINT:{bool matches;
        if (!viewer_matches(o,v->actor,&matches,e))return false;
        if (!matches)return true;
        bool center=v->kind==QA_Q2_SVC_CENTERPRINT;*mirrored=center || v->kind==QA_Q2_SVC_LOCALIZED_PRINT;
        return received_text(o,row,v->text,v->arguments,v->argument_count,center,!center,v->level==3,v->instant,3,e);}
    case QA_Q2_SVC_COMMAND:*mirrored=true;return frontend_remote_unified_command_text(o->replica,v->text?v->text:"",e);
    case QA_Q2_SVC_DISCONNECT:*mirrored=true;return frontend_remote_unified_source_disconnect(o->replica,v->text?v->text:"",e);
    default:return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 protocol presentation has no installed CLIENT handler");
    }
}
bool frontend_unified_q2_presentation_validate(frontend_unified_q2 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    if (!o || !row)return false;
    frontend_remote_q2_effects_profile profile;
    if (!source_profile(row,false,&profile,e))return false;
    if (frontend_unified_q2_rr_known(row))return frontend_unified_q2_rr_validate(o->rr_hud,row,e);
    switch(row->payload.kind){
    case QA_UNIFIED_PRESENTATION_MODEL:case QA_UNIFIED_PRESENTATION_VISIBILITY:{qa_actor_id a;return visual_record(o,row,&a,e);}
    case QA_UNIFIED_PRESENTATION_Q2_TEMPORARY:{qa_q2_temp_entity t;qa_actor_id actors[7];return source_profile(row,true,&profile,e) && temporary(o,&row->payload.value.q2_temporary,&t,actors,e);}
    case QA_UNIFIED_PRESENTATION_Q2_PLAYER:{const qa_unified_q2_player_event *v=&row->payload.value.q2_player;
        if (v->kind==QA_Q2_PLAYER_USERINFO)return userinfo(o,v,false,e);
        qa_actor_id a;return !v->actor.registry || source_actor(o,v->actor,&a,e);}
    case QA_UNIFIED_PRESENTATION_Q2_MAP:if (row->payload.value.q2_map.kind==QA_Q2_MAP_ACHIEVEMENT)return achievement(o,row,row->payload.value.q2_map.text,false,e);return true;
    case QA_UNIFIED_PRESENTATION_Q2_PROTOCOL:return true;
    case QA_UNIFIED_PRESENTATION_BUILTIN:{qa_actor_id a;return !row->payload.value.builtin.actor.registry || source_actor(o,row->payload.value.builtin.actor,&a,e);}
    case QA_UNIFIED_PRESENTATION_OWNER:return frontend_unified_q2_owner_validate(o,row,e);
    default:return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 presentation belongs to another family");
    }
}
bool frontend_unified_q2_simulation_validate(frontend_unified_q2 *o,const qa_unified_simulation_event *row,qa_error *e)
{
    if (!o || !row || row->payload.kind!=QA_UNIFIED_SIMULATION_MESSAGE)return false;
    const qa_unified_message_event *v=&row->payload.value.message;
    switch(v->kind){
    case QA_UNIFIED_MESSAGE_CONFIG_STRING:return v->index<65536 && v->text;
    case QA_UNIFIED_MESSAGE_Q2_INVENTORY:return v->count==256 && v->counts;
    case QA_UNIFIED_MESSAGE_Q2_LAYOUT:case QA_UNIFIED_MESSAGE_COMMAND_TEXT:case QA_UNIFIED_MESSAGE_DISCONNECT:return v->text!=NULL;
    case QA_UNIFIED_MESSAGE_Q2_MUZZLE_FLASH:return true;
    default:return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 simulation message has no installed state transition");
    }
}
bool frontend_unified_q2_presentation(frontend_unified_q2 *o,const qa_unified_presentation_event *row,bool *mirrored,qa_error *e)
{
    if (!o || !row || !mirrored || !current(o,e))return false;
    *mirrored=false;
    if (frontend_unified_q2_rr_known(row)){bool okay=frontend_unified_q2_rr_presentation(o->rr_hud,row,mirrored,e);
        if (okay && frontend_unified_q2_rr_help_visible(o->rr_hud)){inventory_clear(o);scores_clear(o);o->help_visible=false;}return okay;}
    switch(row->payload.kind){
    case QA_UNIFIED_PRESENTATION_MODEL:case QA_UNIFIED_PRESENTATION_VISIBILITY:return visual_apply(o,row,e);
    case QA_UNIFIED_PRESENTATION_Q2_TEMPORARY:return temporary_receive(o,row,&row->payload.value.q2_temporary,e);
    case QA_UNIFIED_PRESENTATION_Q2_MAP:return map_receive(o,row,mirrored,e);
    case QA_UNIFIED_PRESENTATION_Q2_PROTOCOL:return protocol_receive(o,row,mirrored,e);
    case QA_UNIFIED_PRESENTATION_BUILTIN:return builtin_receive(o,row,mirrored,e);
    case QA_UNIFIED_PRESENTATION_Q2_PLAYER:{const qa_unified_q2_player_event *v=&row->payload.value.q2_player;bool matches;
        if (v->kind==QA_Q2_PLAYER_USERINFO)return userinfo(o,v,true,e);
        if (!viewer_matches(o,v->actor,&matches,e))return false;
        if (!matches)return true;
        switch(v->kind){
        case QA_Q2_PLAYER_VIEW:case QA_Q2_PLAYER_SCOREBOARD:case QA_Q2_PLAYER_INVENTORY:return player_overlay(o,row,e) && frontend_unified_q2_rr_overlay(o->rr_hud,row,e);
        case QA_Q2_PLAYER_PRINT:return received_text(o,row,v->text,NULL,0,false,true,v->level==3,true,3,e);
        case QA_Q2_PLAYER_STUFFTEXT:*mirrored=true;return frontend_remote_unified_command_text(o->replica,v->text?v->text:"",e);
        case QA_Q2_PLAYER_HELP:o->help_visible=v->visible;if (v->visible){inventory_clear(o);o->score_visible=false;}return frontend_unified_q2_rr_overlay(o->rr_hud,row,e);
        case QA_Q2_PLAYER_FLASHLIGHT:{qa_actor_id a;q2_bank *b=NULL;if (!source_actor(o,v->actor,&a,e) || !source_bank(o,row,true,&b,e))return false;
            int32_t hand=v->hand==QA_Q2_LEFT_HAND?-1:v->hand==QA_Q2_RIGHT_HAND?1:0;
            return frontend_remote_q2_effects_flashlight(b->effects,a,v->visible,hand,e) && effect_replace(o,b,a,FRONTEND_REMOTE_Q2_FLASHLIGHT,e);}
        case QA_Q2_PLAYER_LOAD_MENU:{const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
            return domain && frontend_menu_open(o->frontend->seats+domain->physical_seat,FRONTEND_LOAD,e);}
        case QA_Q2_PLAYER_CHASE:case QA_Q2_PLAYER_ALPHA:case QA_Q2_PLAYER_DOGTAG:return true;
        case QA_Q2_PLAYER_TRAIL:return true;
        default:return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 player presentation has no installed CLIENT handler");
        }
    }
    case QA_UNIFIED_PRESENTATION_OWNER:return frontend_unified_q2_owner_retire(o,row,e);
    default:return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q2 presentation belongs to another family");
    }
}
bool frontend_unified_q2_simulation(frontend_unified_q2 *o,const qa_unified_simulation_event *row,qa_error *e)
{
    if (!o || !row || o->busy || !current(o,e) || !frontend_unified_q2_simulation_validate(o,row,e))return false;
    const qa_unified_message_event *v=&row->payload.value.message;
    if (v->kind==QA_UNIFIED_MESSAGE_DISCONNECT)return frontend_remote_unified_source_disconnect(o->replica,v->text,e);
    if (v->kind==QA_UNIFIED_MESSAGE_COMMAND_TEXT)return frontend_remote_unified_command_text(o->replica,v->text,e);
    if (v->kind==QA_UNIFIED_MESSAGE_Q2_MUZZLE_FLASH){double ms=row->milliseconds?row->time:row->time*1000;
        for (size_t i=0;i<o->muzzle_count;++i){q2_muzzle_receipt *m=o->muzzles+i;
            if (!m->consumed && m->number==v->entity && m->flash==v->flash && m->monster==v->monster && m->milliseconds==ms){m->consumed=true;return true;}}
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 simulation muzzle has no exact received presentation witness");}
    if (v->kind==QA_UNIFIED_MESSAGE_Q2_INVENTORY){for(size_t i=0;i<256;++i)o->inventory[i]=v->counts[i];return true;}
    char *text=text_copy(v->text);if (!text)return false;
    if (v->kind==QA_UNIFIED_MESSAGE_Q2_LAYOUT){free(o->layout);o->layout=text;return true;}
    size_t index=v->index;
    if (index>=o->config_count){size_t n=index+1;void *p=realloc(o->config,n*sizeof(*o->config));if (!p){free(text);return false;}
        o->config=p;memset(o->config+o->config_count,0,(n-o->config_count)*sizeof(*o->config));o->config_count=n;}
    free(o->config[index]);o->config[index]=text;
    return true;
}

bool frontend_unified_q2_frame_prepare(frontend_unified_q2 *o,const qa_unified_document *d,qa_error *e)
{
    if (!o || o->busy || o->prepared_frame || !d || !current(o,e)) return false;
    const qa_unified_frame *frame=qa_unified_document_frame(d);
    if (!frame || !frame->world || !native_frame_read(o,d,e)) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 received frame lost its typed Source owner");
    double seconds=frame->world->presentation_seconds;
    uint64_t n=frame->world->source.number;
    if (!qa_unified_document_retain(d,&o->prepared_frame,e)) return false;
    if (!status_prepare(o,frame,e) || !frontend_unified_q2_rr_frame_prepare(o->rr_hud,d,e)) {
        frontend_unified_q2_frame_abort(o); return false;
    }
    o->prepared_seconds=seconds; o->prepared_number=n; return true;
}
void frontend_unified_q2_frame_commit(frontend_unified_q2 *o)
{
    if (!o || !o->prepared_frame || o->busy) return;
    qa_unified_document_destroy(o->frame); o->frame=o->prepared_frame; o->prepared_frame=NULL;
    o->seconds=o->prepared_seconds; o->frame_number=o->prepared_number;
    o->status=o->prepared_status;o->prepared_status=(q2_native){0};
    qa_unified_document_destroy(o->status_metadata);
    o->status_metadata=o->prepared_status_metadata;o->prepared_status_metadata=NULL;
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
    const qa_unified_frame *frame=qa_unified_document_frame(d);
    if (!frame || !frame->world || !native_frame_read(o,d,e)) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q2 prepared frame lost its typed Source owner");
    double seconds=frame->world->presentation_seconds;
    return (frame->epoch==frontend_remote_unified_epoch(o->replica) && frame->world->source.number==o->prepared_number && seconds==o->prepared_seconds) ||
        frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q2 prepared frame differs from publication");
}
void frontend_unified_q2_frame_abort(frontend_unified_q2 *o)
{
    if (o && !o->busy) {
        frontend_unified_q2_rr_frame_abort(o->rr_hud);
        qa_unified_document_destroy(o->prepared_frame); o->prepared_frame=NULL;
        o->prepared_status=(q2_native){0};
        qa_unified_document_destroy(o->prepared_status_metadata);o->prepared_status_metadata=NULL;
    }
}
bool frontend_unified_q2_entity_beam(frontend_unified_q2 *o,const char *content,
    const qa_scene_view *view,qa_vec3 start,qa_vec3 end,uint32_t colors,int32_t width,
    qa_scene_frame *frame,qa_error *e)
{
    if (!o || o->busy || !current(o,e)) return false;
    q2_bank *b=NULL; qa_bytes palette={0};
    if (!bank(o,content,NULL,NULL,0,false,&b,e) ||
        !qa_scene_resources_palette(b->images,QA_SCENE_Q2,&palette,e)) return false;
    ++o->busy;
    bool okay=frontend_q2_entity_beam(&b->entity_random,palette,qa_scene_white(b->images),
        view,start,end,colors,width,frame,e);
    --o->busy; return okay && current(o,e);
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
    const qa_unified_model_state *model_state=&v->model->payload.value.model;
    input->frame=(uint32_t)model_state->frame;input->old_frame=(uint32_t)model_state->old_frame;
    input->skin=(uint32_t)model_state->skin;input->flags=model_state->render_flags;
    input->color.w=model_state->alpha;float scale=model_state->scale;if (scale==0)scale=1;
    for (size_t i=0;i<3;++i)input->transform.scale[i]=scale;
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
                b->profile==o->view_profile && b->activation==o->view_owner)
                okay=frontend_remote_q2_effects_weapon_draw(b->effects,viewer_actor,input,frame,e);
        --o->busy; return okay;
    }
    q2_visual *v=visual_read(o,a);
    if (!v || !v->visible || !v->model || !v->content || strcmp(v->content,content) || !v->path || strcmp(v->path,path)) return true;
    q2_bank *b=NULL; if (!bank(o,content,NULL,NULL,0,false,&b,e)) return false;
    const qa_unified_model_state *model_state=&v->model->payload.value.model;
    for (size_t i=0;i<model_state->attachment_count;++i) {
        const char *name=model_state->attachments[i].path;qa_scene_model *scene=NULL;
        bool okay=model(b,name,false,&scene,e);
        if (okay && scene) {qa_scene_model_input child=*input;
            for (q2_model *m=b->models;m;m=m->next)if (!strcmp(m->path,name)){child.source_path=m->path;break;}
            child.attachments=NULL;child.attachment_count=0;child.skin=0;
            child.material_library=frontend_unified_model_materials(scene);
            okay=qa_scene_model_submit(scene,&child,frame,e);}
        if (!okay)return false;
    }
    return true;
}
static bool sample_entities(q2_bank *b,frontend_remote_q2_effects_pose **out,size_t *count,qa_error *e)
{
    frontend_unified_q2 *o=b->owner; *out=NULL; *count=0; if (!o->frame) return true;
    const qa_unified_frame *frame=qa_unified_document_frame(o->frame);
    if (!frame || !frame->visuals) return false;
    size_t n=frame->visuals->model_count;
    if (n>SIZE_MAX/sizeof(**out)) return false;
    frontend_remote_q2_effects_pose *rows=n?calloc(n,sizeof(*rows)):NULL;
    if (n && !rows) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q2 frame effect poses");
    size_t used=0; bool okay=true;
    for (size_t i=0;okay && i<n;++i) {
        const qa_unified_model_state *row=frame->visuals->models+i; qa_actor_id a;
        if (row->family!=QA_GAME_Q2 || strcmp(row->content,b->content)) continue;
        okay=frontend_remote_unified_source_actor(o->replica,frame,row->actor,false,&a,e); if (!okay) break;
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
            float value=frontend_legacy_lightstyle_sample(QA_GAME_Q2,b->styles[i],input->seconds);
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
    bool fallback=qa_actor_id_equal(o->view_actor,full_viewer) &&
        selected_view_provider(o,o->view_provider) && (!o->view_owner || !o->view_owner->retired);
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
    const qa_unified_q2_map_event *story=&o->story->payload.value.q2_map;qa_buffer text={0};
    if (!localized(o,o->story,story->text,story->arguments,story->argument_count,&text,e))return false;
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
static bool status_draw(frontend_unified_q2 *o,q2_native *v,const qa_unified_native_hud *state,
    const int32_t inventory[256],const char *layout,int32_t player_number,
    qa_scene_rect viewport,qa_scene_frame *frame,qa_error *e)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    qa_hud_q2_options options={.viewport=viewport,.scale=1,.font_line_height=8,
        .white=qa_scene_white(v->bank->images),.fonts=o->frontend->seats[d->physical_seat].fonts,
        .table=&o->table,.context=v,.configstring=native_config,.picture=native_picture};
    options.fonts.classic=v->font;
    const qa_cvar_view *use_font=qa_cvars_find(d->cvars,"scr_usekfont");
    options.use_font=v->protocol.kind==QA_NET_Q2KEX_2023 && use_font && use_font->integer!=0;
    qa_hud_q2_frame hud={.protocol=v->protocol,.stats=state->stats,.stat_count=state->stat_count,
        .inventory=inventory,.inventory_count=256,.layout=layout,.player_number=player_number,
        .server_frame=state->server_frame,.time_ns=nanoseconds(state->time_ms*.001),
        .frame_ns=state->has_frame_time?nanoseconds(state->frame_time_ms*.001):0};
    return qa_hud_q2_draw(&options,&hud,!v->replace_status,frame,e);
}
bool frontend_unified_q2_hud(frontend_unified_q2 *o,qa_ui *ui,qa_scene_rect viewport,qa_scene_frame *frame,qa_error *e)
{
    if (!o || o->busy || !current(o,e)) return false;
    qa_actor_id player; uint32_t n;
    if (!frontend_remote_unified_player(o->replica,&player,&n)) return false;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (ui!=o->frontend->seats[d->physical_seat].ui) return false;
    const qa_unified_frame *received=qa_unified_document_frame(o->frame);
    const qa_unified_frame_components *components=received?received->components:NULL;
    bool replacement=false;
    for (size_t i=0;i<o->native_count;++i) replacement|=o->native[i].replace_status;
    if (o->status.hud && !replacement) {
        const qa_unified_q2_hud_state *state=&received->player->q2_hud;
        if (!status_draw(o,&o->status,&state->frame,o->inventory,o->layout,state->player_number,viewport,frame,e)) return false;
    }
    for (size_t i=0;components && i<components->native_count;++i) {
        const qa_unified_native_component *row=components->native+i;
        if (!row->hud) continue;
        q2_native *v=NULL;
        for (size_t k=0;k<o->native_count;++k) if (!strcmp(row->owner.provider,o->native[k].provider)) v=o->native+k;
        if (!v || !v->font) return false;
        if (!status_draw(o,v,row->hud,v->inventory,v->layout,v->player_number,viewport,frame,e)) return false;
    }
    ++o->busy;
    bool okay=qa_hud_draw(o->hud,&(qa_hud_frame){.seat=d->physical_seat,.actor=player,.time_ns=nanoseconds(o->seconds),
        .viewport=viewport,.safe_area=viewport,.scale=1,.visible=true},frame,e);
    --o->busy;
    if (okay) okay=marker_draw(o,viewport,frame,e);
    if (okay && o->inventory_visible && !o->status.hud && !replacement) {
        okay=overlay_text(ui,viewport,frame,"Inventory",160,80,(qa_scene_vec4){1,1,1,1},e);
        for (size_t i=0;okay && i<o->item_count;++i) {
            char count[32]; if (!qa_format_number(o->items[i].count,count,e)) { okay=false; break; }
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
        q2_native_picture *picture=b->native_pictures;
        while (picture){q2_native_picture *next=picture->next;qa_scene_image_release(picture->image);free(picture->name);free(picture);picture=next;}
        for (size_t i=0;i<256;++i) free(b->styles[i]);
        o->banks=b->next; free(b->aliases); free(b->source_provider); free(b->content); free(b);
    }
    if (o->hud && !qa_hud_destroy(o->hud,e)) return false;
    qa_unified_document_destroy(o->frame); qa_unified_document_destroy(o->prepared_frame);
    qa_unified_document_destroy(o->status_metadata); qa_unified_document_destroy(o->prepared_status_metadata);
    for (size_t i=0;i<o->native_count;++i) native_clear(o->native+i);
    free(o->native);
    if (o->config) for (size_t i=0;i<o->config_count;++i) free(o->config[i]);
    free(o->config); free(o->help); free(o->help_text[0]); free(o->help_text[1]);
    qa_localization_pool_destroy(o->localizations);
    inventory_clear(o); scores_clear(o);
    free(o->view_content); free(o->view_provider);
    record_free(o->story); sky_clear(o);
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
