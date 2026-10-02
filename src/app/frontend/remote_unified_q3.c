#include "remote_unified_private.h"
#include "remote_unified_q3.h"
#include "remote_unified_events.h"
#include "remote_unified_components.h"
#include "remote_unified_save.h"
#include "remote_unified_presentation.h"
#include "remote_unified_render.h"
#include "q3_render_policy.h"
#include "qa/q3_source_scene_bank.h"
#include "../../presentation/q3_native/events.h"
#include "../../presentation/q3_native/marks.h"
#include "../../presentation/q3_native/pose.h"
#include "../../presentation/q3_native/attachments.h"
#include "../../presentation/q3_native/selected_media.h"
#include "../../presentation/q3_native/particles.h"
#include "../../presentation/q3_native/events_internal.h"
#include "../../presentation/q3_native/trajectory.h"
#include "../../presentation/q3/internal.h"
#include "qa/hud.h"
#include "qa/vfs_view_save.h"
#include "qa/scene_marks.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_presentation_save.h"
#include "qa/source_save.h"
#include <float.h>
#include <math.h>

typedef struct unified_q3_bank {
    struct unified_q3_bank *next;
    struct frontend_unified_q3 *owner;
    char *content;
    char *activation;
    uint64_t generation;
    bool retired, component_recipient;
    qa_vfs *files;
    const qa_product *product;
    qa_actor_owner provider;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *backend;
    qa_q3_supplement *supplement;
    q3n_media *media;
    q3n_events *effects;
    q3n_weapons *weapons;
    q3n_particles *particles;
    q3n_unified_effect_source source;
    qa_actor_id blood_owners[Q3N_LOCAL_CAPACITY];
} unified_q3_bank;
typedef struct unified_q3_character {
    struct unified_q3_character *next;
    qa_actor_id actor;
    unified_q3_bank *bank;
    q3n_player_pose pose;
    qa_player_animation_config animation;
    qa_resource *animation_holder;
    int32_t models[3], skins[3];
    qa_vec3 origin, angles, velocity;
    int32_t movement, legs, torso, time, team;
    uint32_t flags, powerups;
    float color[4], scale, opacity;
    bool visible, reset;
    qa_q3_ref_entity submitted_parts[3];
    bool submitted, hidden;
} unified_q3_character;
typedef struct unified_q3_ballistic {
    struct unified_q3_ballistic *next;
    unified_q3_bank *bank;
    qa_actor_id actor;
    int32_t weapon, time, fire_time, last_fire_time, last_fire_weapon, bolt_time;
    qa_vec3 origin, end, flash_origin, flash_end, bolt_origin, bolt_end;
    qa_q3_trajectory trajectory;
    int32_t bolt_weapon;
    bool projectile, flash, bolt, last_fire;
} unified_q3_ballistic;
struct frontend_unified_q3 {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    frontend_unified_events *events;
    frontend_unified_components *components;
    unified_q3_bank *component_bank;
    unified_q3_bank *banks;
    qa_q3_source_scene_bank *scene_bank;
    unified_q3_character *characters;
    unified_q3_character **character_order;
    size_t character_count, character_capacity;
    unified_q3_ballistic *ballistics;
    qa_unified_document *frame, *candidate;
    const qa_unified_document *candidate_input;
    uint64_t audio_owner;
    void *audio_context;
    bool (*audio_actor)(void *, qa_actor_id, uint64_t *, qa_error *);
    uint32_t epoch;
    int32_t time, previous_time, candidate_time;
    qa_actor_id entered_actor;
    const qa_unified_document *entered_frame;
    const qa_scene_view *view;
    const qa_scene_world_input *world;
    qa_scene_frame *scene;
    qa_scene_light *lights;
    size_t light_count, light_capacity;
    qa_scene_view sampled_view;
    qa_scene_frame *sampled_scene;
    uint64_t sampled_frame_number;
    bool sampled;
    bool world_submitted;
    qa_ui_preferences preferences;
    bool busy, prepared, has_frame;
};
static qa_json_id field(const qa_json_document *j, qa_json_id row, const char *name)
{ return qa_json_get(j, row, name); }
static bool number(const qa_unified_document *d, qa_json_id row, double *n, qa_error *e)
{ return (qa_unified_document_number(d, row, n, e) && isfinite(*n)) ||
    frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified Q3 scalar is not finite"); }
static bool real(const qa_unified_document *d, qa_json_id row, float *out, qa_error *e)
{
    double n; if (!number(d, row, &n, e)) return false;
    if (fabs(n) > FLT_MAX) return frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified Q3 scalar exceeds float storage");
    *out = (float)n; return true;
}
static bool integer(const qa_unified_document *d, qa_json_id row, int32_t *out, qa_error *e)
{
    double n; if (!number(d, row, &n, e)) return false;
    if (n < INT32_MIN || n > INT32_MAX || trunc(n) != n)
        return frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified Q3 integer exceeds its Source domain");
    *out = (int32_t)n; return true;
}
static bool word(const qa_unified_document *d,qa_json_id row,uint32_t *out,qa_error *e)
{
    double n;
    if(!number(d,row,&n,e) || n<INT32_MIN || n>UINT32_MAX || trunc(n)!=n)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified Q3 word exceeds its actual Source storage");
    *out=n<0?(uint32_t)(int32_t)n:(uint32_t)n;return true;
}
static bool clock_integer(const qa_unified_document *d,qa_json_id row,int32_t *out,qa_error *e)
{
    double value;
    if(!number(d,row,&value,e))return false;
    double wrapped=fmod(trunc(value),4294967296.0);
    if(wrapped<0)wrapped+=4294967296.0;
    uint32_t bits=(uint32_t)wrapped;memcpy(out,&bits,sizeof(bits));return true;
}
static bool vector(const qa_unified_document *d, qa_json_id row, qa_vec3 *v, qa_error *e)
{
    const qa_json_document *j = qa_unified_document_json(d);
    return real(d, field(j,row,"x"), &v->x,e) && real(d, field(j,row,"y"), &v->y,e) &&
        real(d, field(j,row,"z"), &v->z,e);
}
static bool text(const qa_unified_document *d, qa_json_id row, qa_buffer *b, qa_error *e)
{
    if (!qa_json_string(qa_unified_document_json(d), row, b, e)) return false;
    if (!memchr(b->data, 0, b->size)) return true;
    qa_buffer_free(b); return frontend_unified_fail(e, QA_ERROR_FORMAT, "Unified Q3 text contains NUL");
}
static bool actor(frontend_unified_q3 *o, const qa_unified_document *d, qa_json_id row, qa_actor_id *id, qa_error *e)
{
    const qa_json_document *j = qa_unified_document_json(d); uint64_t slot, generation;
    if (qa_json_type(j,row) == QA_JSON_NULL) { *id = (qa_actor_id){0}; return true; }
    return qa_json_u64(j,field(j,row,"slot"),&slot,e) && slot <= UINT32_MAX &&
        qa_json_u64(j,field(j,row,"generation"),&generation,e) &&
        frontend_remote_unified_actor(o->replica,(uint32_t)slot,generation,id,e);
}
bool frontend_unified_q3_current(const frontend_unified_q3 *o)
{ return o && o->epoch == frontend_remote_unified_epoch(o->replica) &&
    frontend_unified_media_recipe(o->media)==frontend_remote_unified_recipe(o->replica) &&
    frontend_unified_media_current(o->media) && frontend_remote_unified_current(o->replica,NULL); }
static bool current(frontend_unified_q3 *o, qa_error *e)
{ return frontend_unified_q3_current(o) || frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified Q3 CLIENT owner changed"); }
static bool retained_current(const frontend_unified_q3 *o,qa_error *e)
{
    if(!o || !o->frontend || o->epoch!=frontend_remote_unified_epoch(o->replica) ||
        frontend_unified_media_recipe(o->media)!=frontend_remote_unified_recipe(o->replica) ||
        !frontend_unified_media_current(o->media))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified Q3 retained CLIENT owner changed");
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    if(!domain || domain->application!=o->frontend->application)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified Q3 retained CLIENT lost its actual application");
    return o->frontend->source_restoring || o->frontend->capture?
        frontend_remote_unified_checkpoint_current(o->replica,e):frontend_remote_unified_current(o->replica,e);
}
static bool effect_current(const q3n_unified_effect_source *s)
{
    unified_q3_bank *b = s ? s->context : NULL;
    frontend_unified_q3 *o = b ? b->owner : NULL;
    return o && o->busy && o->entered_frame == frontend_remote_unified_frame(o->replica) &&
        frontend_unified_q3_current(o) && s == &b->source && s->assets == b->assets &&
        s->content == b->files && s->provider == b->provider &&
        (!s->actor.registry || qa_actors_get(frontend_remote_unified_registry(o->replica),s->actor));
}
static double bank_clock(void *context)
{ unified_q3_bank *b = context; return (double)b->source.time; }
static bool trace(void *context, const q3n_frame *f, qa_vec3 start, qa_vec3 end,
    qa_bounds bounds, int32_t skip, uint32_t mask, qa_trace_result *out, qa_error *e)
{
    unified_q3_bank *b = context; (void)skip;
    if (f->unified_effects != &b->source || !effect_current(&b->source))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified Q3 trace lost its entered CLIENT");
    qa_trace_query q = {.start=start,.end=end,.shape={.kind=QA_SHAPE_BOX,.bounds=bounds},
        .policy={.family=QA_COLLISION_Q3,.contents_mask=mask,.curves=true},.pass_actor=b->source.actor};
    qa_collision_geometry *g = (qa_collision_geometry *)frontend_remote_unified_geometry(b->owner->replica);
    return g && qa_collision_trace(g,&q,out,e) && effect_current(&b->source);
}
static bool contents(void *context, const q3n_frame *f, qa_vec3 point, int32_t pass, uint32_t *out, qa_error *e)
{
    unified_q3_bank *b = context; (void)pass;
    if (f->unified_effects != &b->source || !effect_current(&b->source)) return false;
    qa_point_contents result; qa_point_query q = {.point=point,
        .policy={.family=QA_COLLISION_Q3,.curves=true},.pass_actor=b->source.actor};
    qa_collision_geometry *g = (qa_collision_geometry *)frontend_remote_unified_geometry(b->owner->replica);
    if (!g || !qa_collision_point_contents(g,&q,&result,e) || !effect_current(&b->source)) return false;
    *out=(uint32_t)result.contents; return true;
}
static bool fragments(void *context, const q3n_frame *f, const qa_vec3 *points, size_t count,
    qa_vec3 projection, qa_vec3 *output, size_t capacity, q3n_mark_fragment *out,
    size_t fragment_capacity, size_t *returned, qa_error *e)
{
    unified_q3_bank *b = context;
    if (f->unified_effects != &b->source || !effect_current(&b->source)) return false;
    qa_scene_mark_fragment *rows = fragment_capacity ? calloc(fragment_capacity,sizeof(*rows)) : NULL;
    if (fragment_capacity && !rows) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Preparing real Q3 mark fragments");
    qa_scene_mark_result result={0}; qa_scene_world *w=frontend_unified_media_world(b->owner->media);
    bool okay = w && qa_scene_world_mark_fragments(w,points,count,projection,output,capacity,
        rows,fragment_capacity,&result,e) && effect_current(&b->source);
    for (size_t i=0; okay && i<result.fragment_count; ++i) {
        okay=rows[i].first_point<=UINT32_MAX && rows[i].point_count<=UINT32_MAX;
        if (okay) out[i]=(q3n_mark_fragment){(uint32_t)rows[i].first_point,(uint32_t)rows[i].point_count};
    }
    free(rows); if (okay) *returned=result.fragment_count; return okay;
}
static qa_actor_owner provider(frontend_unified_q3 *o, const char *content,const char *instance)
{
    qa_executable_recipe *r=frontend_remote_unified_recipe(o->replica);
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    qa_actor_owner found=0;
    for (size_t i=0; i<qa_executable_recipe_provider_count(r); ++i) {
        const qa_recipe_provider *p=qa_executable_recipe_provider(r,i);
        const qa_product *product=qa_catalog_product(d->catalog,p->selection.product);
        if (product && !strcmp(product->identity,content) && (!instance || !strcmp(p->selection.instance,instance))) {
            if (found && found!=p->source_owner) return 0;
            found=p->source_owner;
        }
    }
    return found;
}
static void local_allocated(void *context,int32_t slot)
{
    unified_q3_bank *b=context;
    b->blood_owners[slot]=(qa_actor_id){0};
}
static bool local_visible(void *context,const qa_q3_ref_entity *ref)
{
    unified_q3_bank *b=context;qa_actor_id viewer;uint32_t physical;
    if(!frontend_remote_unified_player(b->owner->replica,&viewer,&physical))return true;
    for(int32_t i=0;i<Q3N_LOCAL_CAPACITY;++i)
        if(&b->effects->locals[i].value.ref==ref && b->effects->locals[i].active){
            if(b->blood_owners[i].registry && qa_actor_id_equal(b->blood_owners[i],viewer))return false;
            float radius=b->effects->locals[i].value.radius;
            if(radius>0 && b->owner->view && qa_vec_length(qa_vec_sub(ref->origin,b->owner->view->origin))<radius)return false;
            break;
        }
    return true;
}
static void bleed(const q3n_frame *f,unified_q3_bank *b,qa_vec3 origin,qa_actor_id target)
{
    q3n_local_entity *local=q3n_effect_bleed_entity(f,origin);
    if(!local)return;
    for(int32_t i=0;i<Q3N_LOCAL_CAPACITY;++i)
        if(&b->effects->locals[i].value==local){b->blood_owners[i]=target;return;}
}
static bool bank_children(unified_q3_bank *b,qa_q3_product product,qa_error *e)
{
    q3n_event_options events={.product=product,.assets=b->assets,.context=b,
        .trace=trace,.point_contents=contents,.mark_fragments=fragments,.local_allocated=local_allocated};
    q3n_media_options media={.product=product,.assets=b->assets};
    q3n_weapon_options weapons={.product=product,.assets=b->assets,.particle_explosion=q3n_particles_weapon_explosion};
    if((!b->media && !q3n_media_create(&media,&b->media,e)) ||
        (!b->effects && !q3n_events_create_effects(&events,&b->effects,e)) ||
        (!b->particles && !q3n_particles_create(b->assets,product,&b->particles,e)))return false;
    weapons.context=b->particles;
    if(!b->weapons && !q3n_weapons_create(&weapons,&b->weapons,e))return false;
    b->source=(q3n_unified_effect_source){.context=b,.provider=b->provider,.content=b->files,
        .assets=b->assets,.product=product,.current=effect_current};return true;
}
static bool bank_read(frontend_unified_q3 *o, const char *content,const char *instance,uint64_t generation,
    unified_q3_bank **out, qa_error *e)
{
    for (unified_q3_bank *b=o->banks; b; b=b->next) if (b->content && !b->component_recipient && !strcmp(b->content,content) && b->generation==generation &&
        ((!instance && !b->activation) || (instance && b->activation && !strcmp(instance,b->activation)))) {
        if(b->retired)return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 presentation event names a retired activation");
        *out=b; return true; }
    unified_q3_bank *b=calloc(1,sizeof(*b));
    if (!b) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining real Q3 CLIENT resource namespace");
    b->owner=o; b->content=malloc(strlen(content)+1); b->provider=provider(o,content,instance);b->generation=generation;
    if(b->content)memcpy(b->content,content,strlen(content)+1);
    if(instance){b->activation=malloc(strlen(instance)+1);if(b->activation)memcpy(b->activation,instance,strlen(instance)+1);}
    bool okay=b->content && (!instance || b->activation) && b->provider && frontend_unified_media_files(o->media,content,&b->files,&b->product,e) &&
        b->product->family==QA_GAME_Q3 && frontend_unified_media_q3_assets(o->media,content,&b->assets,e);
    qa_q3_product product=okay && b->product->campaign && !strcmp(b->product->campaign,"missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    if (okay) okay=bank_children(b,product,e);
    if (!okay) { q3n_weapons_destroy(b->weapons); q3n_particles_destroy(b->particles);
        q3n_events_destroy(b->effects); q3n_media_destroy(b->media); free(b->content);free(b->activation); free(b); return false; }
    unified_q3_bank **tail=&o->banks;while(*tail)tail=&(*tail)->next;*tail=b;*out=b;return true;
}
static bool component_submit(void *context,const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *e)
{
    unified_q3_bank *b=context;frontend_unified_q3 *o=b->owner;
    if(b!=o->component_bank)return true;
    return o->components && effect_current(&b->source) && frame==&o->frontend->frame &&
        (options->world.view.mirror?
            frontend_unified_components_submit_reflected(o->components,b->backend,o->scene_bank,options,frame,e):
            frontend_unified_components_submit(o->components,b->backend,o->scene_bank,options,frame,e)) && effect_current(&b->source);
}
static bool backend_read(unified_q3_bank *b, qa_scene_rect viewport, qa_error *e)
{
    if (b->backend) return true;
    frontend_unified_q3 *o=b->owner;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (!o->audio_owner) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 backend has no actual private CLIENT identity");
    qa_q3_presentation_options options={.assets=b->assets,.clock={b,bank_clock},
        .seat=d->physical_seat,.owner=o->audio_owner,.viewport=viewport,.near_clip=4,.far_clip=8192,
        .identity_light=1,.lod_scale=5,.rail_core_width=6,.rail_ring_width=16,.rail_segment_length=32,
        .context=b,.submit_view=component_submit};
    return qa_q3_presentation_create(&options,&b->backend,e);
}
static bool sound_output(void *context,const q3n_frame *f,qa_audio_asset *asset,const qa_vec3 *origin,int32_t channel,qa_error *e)
{
    unified_q3_bank *b=context; frontend_unified_q3 *o=b->owner;
    return f->unified_effects==&b->source && effect_current(&b->source) && o->events && origin &&
        frontend_unified_events_sound_path(o->events,b->content,qa_audio_asset_name(asset),
            b->source.actor,*origin,b->source.time,channel,1,1,0,e) && effect_current(&b->source);
}
static bool frame_read(frontend_unified_q3 *o, const qa_unified_document *d, int32_t *time, qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id root=qa_unified_document_root(d);
    qa_json_id snap=field(j,field(j,root,"output"),"snapshot"), t=field(j,field(j,snap,"frame"),"time");
    uint64_t epoch; double n;
    if (!qa_json_u64(j,field(j,root,"epoch"),&epoch,e) || epoch!=o->epoch || !number(d,field(j,t,"value"),&n,e)) return false;
    if (qa_json_string_equal(j,field(j,t,"kind"),"seconds")) n*=1000;
    else if (!qa_json_string_equal(j,field(j,t,"kind"),"milliseconds"))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified Q3 clock has no Source time domain");
    double reduced=fmod(trunc(n),4294967296.0); if (reduced<0) reduced+=4294967296.0;
    uint32_t bits=(uint32_t)reduced; memcpy(time,&bits,sizeof(bits)); return true;
}
bool frontend_unified_q3_create(qa_frontend *f, frontend_remote_unified *r,
    frontend_unified_media *m, frontend_unified_q3 **out, qa_error *e)
{
    if (!f || !r || !m || !out || *out || !frontend_remote_unified_domain_read(r))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 CLIENT requires its actual replica and media owners");
    frontend_unified_q3 *o=calloc(1,sizeof(*o));
    if (!o) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Allocating persistent Unified Q3 CLIENT");
    o->frontend=f; o->replica=r; o->media=m; o->epoch=frontend_remote_unified_epoch(r);
    if (!retained_current(o,e)) { free(o); return false; } *out=o; return true;
}
bool frontend_unified_q3_audio(frontend_unified_q3 *o, uint64_t owner, void *context,
    bool (*resolve)(void *,qa_actor_id,uint64_t *,qa_error *), qa_error *e)
{
    if (!o || o->busy || !owner || owner==QA_AUDIO_NO_OWNER || !resolve ||
        (o->audio_owner && o->audio_owner!=owner) ||
        (o->audio_actor && (o->audio_context!=context || o->audio_actor!=resolve)))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 CLIENT audio must bind its real shared bus once");
    o->audio_owner=owner; o->audio_context=context; o->audio_actor=resolve; return true;
}
bool frontend_unified_q3_events(frontend_unified_q3 *o, frontend_unified_events *events, qa_error *e)
{
    if (!o || o->busy || (o->events && o->events!=events) || !events ||
        frontend_unified_events_audio_owner(events)!=o->audio_owner)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 CLIENT events require the same real audio ledger");
    o->events=events; return true;
}
bool frontend_unified_q3_components(frontend_unified_q3 *o,frontend_unified_components *components,qa_error *e)
{
    if(!o || o->busy || !components || (o->components && o->components!=components) ||
        (!frontend_unified_components_current(components) && !frontend_unified_components_retained_current(components)))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 component submission requires its real retained CLIENT child");
    o->components=components;return true;
}
static bool component_bank_read(frontend_unified_q3 *o,qa_error *e)
{
    o->component_bank=NULL;
    if(!o->components)return true;
    const char *content=NULL;const qa_recipe_provider *p=NULL;bool present=false;
    if(!frontend_unified_components_recipient_content(o->components,&content,&p,&present,e))return false;
    if(!present)return true;
    if(!p || !p->source_owner || !content)return false;
    for(unified_q3_bank *b=o->banks;b;b=b->next)if(b->component_recipient && b->provider==p->source_owner &&
        b->content && b->activation && !strcmp(b->activation,p->selection.instance) && !strcmp(b->content,content)){
        if(!b->files || !b->assets || !b->product)return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Component recipient construction did not retain its resource tuple");
        qa_q3_product product=b->product->campaign && !strcmp(b->product->campaign,"missionpack")?QA_Q3_TEAM_ARENA:QA_Q3_ARENA;
        if(!bank_children(b,product,e))return false;
        o->component_bank=b;return true;}
    unified_q3_bank *b=calloc(1,sizeof(*b));if(!b)return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual component recipient namespace");
    b->owner=o;b->provider=p->source_owner;b->component_recipient=true;b->content=malloc(strlen(content)+1);b->activation=malloc(strlen(p->selection.instance)+1);
    if(b->content)memcpy(b->content,content,strlen(content)+1);
    if(b->activation)memcpy(b->activation,p->selection.instance,strlen(p->selection.instance)+1);
    unified_q3_bank **tail=&o->banks;while(*tail)tail=&(*tail)->next;*tail=b;
    if(!b->content || !b->activation || !frontend_unified_media_files(o->media,content,&b->files,&b->product,e) ||
        b->product->family!=QA_GAME_Q3 || !frontend_unified_media_q3_assets(o->media,content,&b->assets,e))return false;
    qa_q3_product product=b->product->campaign && !strcmp(b->product->campaign,"missionpack")?QA_Q3_TEAM_ARENA:QA_Q3_ARENA;
    if(!bank_children(b,product,e) || !frontend_unified_components_current(o->components))return false;
    o->component_bank=b;return true;
}
static bool retirement_read(const qa_unified_document *d,qa_json_id row,qa_buffer *instance,
    uint64_t *generation,bool *retired,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);qa_json_id v=field(j,row,"event"),kind=field(j,v,"kind");
    if(!qa_json_string_equal(j,field(j,row,"kind"),"presentation-owner") ||
        (!qa_json_string_equal(j,kind,"retired") && !qa_json_string_equal(j,kind,"refreshed")))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 owner retirement lacks its actual tagged record");
    *retired=qa_json_string_equal(j,kind,"retired");qa_json_id owner=field(j,v,"owner");
    return text(d,field(j,owner,"provider"),instance,e) && instance->size>1 &&
        qa_json_u64(j,field(j,owner,"generation"),generation,e) && *generation;
}
bool frontend_unified_q3_owner_validate(frontend_unified_q3 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    qa_buffer instance={0};uint64_t generation;bool retired;
    bool okay=o && d && current(o,e) && retirement_read(d,row,&instance,&generation,&retired,e);
    qa_buffer_free(&instance);return okay;
}
bool frontend_unified_q3_owner_retire(frontend_unified_q3 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    qa_buffer instance={0};uint64_t generation;bool retired;
    bool okay=o && d && frontend_unified_q3_idle(o) && current(o,e) &&
        retirement_read(d,row,&instance,&generation,&retired,e);
    if(okay && retired)for(unified_q3_bank *b=o->banks;b;b=b->next)
        if(b->activation && !strcmp(b->activation,(const char *)instance.data) && b->generation==generation && !b->retired){
            for(unified_q3_ballistic *v=o->ballistics;v;v=v->next)if(v->bank==b){
                if(o->events && (v->bolt || v->projectile) && !frontend_unified_events_sound_stop_loop(o->events,v->actor,e)){okay=false;break;}
                v->projectile=v->flash=v->bolt=v->last_fire=false;}
            if(!okay)break;
            q3n_events_round(b->effects);q3n_particles_round(b->particles,o->time);b->retired=true;
        }
    qa_buffer_free(&instance);return okay;
}
bool frontend_unified_q3_frame_prepare(frontend_unified_q3 *o, const qa_unified_document *d, qa_error *e)
{
    if (!o || o->busy || o->prepared || !current(o,e)) return false;
    if (!frame_read(o,d,&o->candidate_time,e) || !frontend_unified_clone(d,&o->candidate,e)) return false;
    o->candidate_input=d; o->prepared=true; return true;
}
bool frontend_unified_q3_frame_ready(frontend_unified_q3 *o, const qa_unified_document *d, qa_error *e)
{
    int32_t time;
    return o && !o->busy && o->prepared && o->candidate && o->candidate_input==d && current(o,e) &&
        frame_read(o,d,&time,e) && time==o->candidate_time;
}
void frontend_unified_q3_frame_commit(frontend_unified_q3 *o)
{
    if (!o || !o->prepared || o->busy) return;
    for(unified_q3_bank *b=o->banks;b;b=b->next)qa_q3_presentation_supplement_release(&b->supplement);
    qa_unified_document_destroy(o->frame); o->frame=o->candidate; o->candidate=NULL;
    o->previous_time=o->has_frame?o->time:o->candidate_time; o->time=o->candidate_time;
    o->has_frame=true; o->prepared=false; o->candidate_input=NULL; o->sampled=false; o->sampled_scene=NULL;o->world_submitted=false;
}
void frontend_unified_q3_frame_abort(frontend_unified_q3 *o)
{ if (o && !o->busy) { qa_unified_document_destroy(o->candidate); o->candidate=NULL; o->candidate_input=NULL; o->prepared=false; } }
static bool enter(frontend_unified_q3 *o, qa_actor_id id, qa_error *e)
{
    if (!o || o->busy || !o->has_frame || !current(o,e))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 effects require their committed real CLIENT FRAME");
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if(!qa_ui_preferences_read(qa_application_cvars(o->frontend->application),d->physical_seat,&o->preferences,e) ||
        !current(o,e))return false;
    o->entered_frame=frontend_remote_unified_frame(o->replica); o->entered_actor=id; o->busy=true; return true;
}
static bool leave(frontend_unified_q3 *o, bool okay, qa_error *e)
{ if (okay) okay=current(o,e); o->busy=false; o->entered_frame=NULL; o->entered_actor=(qa_actor_id){0}; return okay; }
static q3n_frame effect_frame(frontend_unified_q3 *o, unified_q3_bank *b, int32_t time)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    b->source.time=time; b->source.actor=o->entered_actor;
    return (q3n_frame){.unified_effects=&b->source,.presentation=b->backend,.assets=b->assets,
        .media=b->media,.events=b->effects,.physical_presentation_seat=d->physical_seat,
        .weapons=b->weapons,.particles=b->particles,.effect_output_context=b,.effect_sound_output=sound_output,.effect_entity_visible=local_visible,
        .time=time,.frame_milliseconds=q3ne_word((uint32_t)o->time-(uint32_t)o->previous_time),.preferences=o->preferences};
}
static const q3n_event_settings effect_settings={.blood=true,.gibs=true,.add_marks=true};
static const q3n_weapon_settings weapon_settings={.rail_trail_time=400,.tracer_length=160,
    .tracer_width=1,.tracer_chance=.4f,.draw_gun=true};
static bool content_text(const qa_unified_document *d, qa_json_id row, qa_buffer *content, qa_error *e)
{ return text(d,field(qa_unified_document_json(d),row,"content"),content,e); }
static bool event_owner(const qa_unified_document *d,qa_json_id row,qa_buffer *instance,uint64_t *generation,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);qa_json_id owner=field(j,row,"owner");*generation=0;
    if(owner==QA_JSON_NONE || qa_json_type(j,owner)==QA_JSON_NULL)return true;
    return text(d,field(j,owner,"provider"),instance,e) && instance->size>1 &&
        qa_json_u64(j,field(j,owner,"generation"),generation,e) && *generation;
}
typedef enum ballistic_kind {
    BALL_REMOVE,BALL_FIRE,BALL_PROJECTILE,BALL_BOUNCE,BALL_TRAIL,BALL_IMPACT,
    BALL_CONTACT,BALL_SHOTGUN,BALL_RAIL,BALL_RAIL_AWARD
} ballistic_kind;
typedef struct ballistic_event {
    ballistic_kind kind;
    qa_actor_id actor,target;
    int32_t weapon,time,surface,contact,count,until;
    qa_vec3 origin,end,normal,point,start,direction;
    qa_q3_trajectory trajectory;
    uint32_t seed;
    float volume;
    bool flesh,rail_surface;
} ballistic_event;
static bool ballistic_read(frontend_unified_q3 *o,const qa_unified_document *d,qa_json_id row,
    ballistic_event *out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);qa_json_id v=field(j,row,"event"),kind=field(j,v,"kind");
    const char *names[]={"remove","fire","projectile","bounce","trail","impact","contact","shotgun","rail","rail-award"};
    size_t k=0;while(k<sizeof(names)/sizeof(*names) && !qa_json_string_equal(j,kind,names[k]))++k;
    if(k==sizeof(names)/sizeof(*names))return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unknown Unified Q3 ballistic variant");
    *out=(ballistic_event){.kind=(ballistic_kind)k};
    if(!actor(o,d,field(j,v,"actor"),&out->actor,e) || !out->actor.registry ||
        !integer(d,field(j,v,"weapon"),&out->weapon,e) || out->weapon<0 || out->weapon>13 ||
        !vector(d,field(j,v,"origin"),&out->origin,e) || !vector(d,field(j,v,"end"),&out->end,e) ||
        !vector(d,field(j,v,"normal"),&out->normal,e) || !actor(o,d,field(j,v,"target"),&out->target,e) ||
        !integer(d,field(j,v,"surfaceFlags"),&out->surface,e) ||
        !clock_integer(d,field(j,v,"timeMilliseconds"),&out->time,e))return false;
    qa_json_id extra;
    switch(out->kind){
    case BALL_FIRE:return real(d,field(j,v,"volume"),&out->volume,e);
    case BALL_PROJECTILE:{
        extra=field(j,v,"trajectory");qa_vec3 base,delta;
        if(!integer(d,field(j,extra,"type"),&out->trajectory.type,e) || out->trajectory.type<0 || out->trajectory.type>5 ||
            !integer(d,field(j,extra,"time"),&out->trajectory.time,e) || !integer(d,field(j,extra,"duration"),&out->trajectory.duration,e) ||
            !vector(d,field(j,extra,"base"),&base,e) || !vector(d,field(j,extra,"delta"),&delta,e))return false;
        q3ne_store(out->trajectory.base,base);q3ne_store(out->trajectory.delta,delta);return true;}
    case BALL_IMPACT:
        extra=field(j,v,"hitKind");out->flesh=qa_json_string_equal(j,extra,"flesh");
        return out->flesh || qa_json_string_equal(j,extra,"wall") || frontend_unified_fail(e,QA_ERROR_FORMAT,"Unknown Q3 impact hit kind");
    case BALL_CONTACT:
        extra=field(j,v,"contact");kind=field(j,extra,"kind");
        if(qa_json_string_equal(j,kind,"gauntlet-quad")){out->contact=0;return true;}
        if(qa_json_string_equal(j,kind,"hit") || qa_json_string_equal(j,kind,"miss")){
            out->contact=qa_json_string_equal(j,kind,"hit")?1:2;
            return vector(d,field(j,extra,"point"),&out->point,e) && vector(d,field(j,extra,"normal"),&out->normal,e) &&
                (out->contact!=1 || actor(o,d,field(j,extra,"target"),&out->target,e));}
        if(qa_json_string_equal(j,kind,"lightning-reflection")){
            out->contact=3;return vector(d,field(j,extra,"start"),&out->start,e) && vector(d,field(j,extra,"end"),&out->end,e);}
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Unknown Q3 contact variant");
    case BALL_SHOTGUN:{
        extra=field(j,v,"shot");int32_t seed;
        if(!vector(d,field(j,extra,"muzzle"),&out->start,e) || !vector(d,field(j,extra,"direction"),&out->direction,e) ||
            !integer(d,field(j,extra,"seed"),&seed,e))return false;
        out->seed=(uint32_t)seed;return true;}
    case BALL_RAIL:
        extra=field(j,v,"trail");
        if(!vector(d,field(j,extra,"start"),&out->start,e) || !vector(d,field(j,extra,"end"),&out->end,e))return false;
        extra=field(j,extra,"impact");kind=field(j,extra,"kind");out->rail_surface=qa_json_string_equal(j,kind,"surface");
        return out->rail_surface?vector(d,field(j,extra,"normal"),&out->normal,e):
            qa_json_string_equal(j,kind,"none") || frontend_unified_fail(e,QA_ERROR_FORMAT,"Unknown Q3 rail impact variant");
    case BALL_RAIL_AWARD:return integer(d,field(j,v,"count"),&out->count,e) && clock_integer(d,field(j,v,"until"),&out->until,e);
    default:return true;
    }
}
static unified_q3_ballistic *ballistic_find(frontend_unified_q3 *o,unified_q3_bank *b,qa_actor_id id)
{ for(unified_q3_ballistic *v=o->ballistics;v;v=v->next)if(v->bank==b && qa_actor_id_equal(v->actor,id))return v;return NULL; }
static bool ballistic_sound(frontend_unified_q3 *o,unified_q3_bank *b,int32_t handle,qa_actor_id id,
    qa_vec3 origin,int32_t time,int32_t channel,float volume,qa_error *e)
{
    qa_audio_asset *asset=q3p_sound(b->assets,handle);
    return !asset || (o->events && frontend_unified_events_sound_path(o->events,b->content,
        qa_audio_asset_name(asset),id,origin,time,channel,volume,1,0,e) && effect_current(&b->source));
}
static bool ballistic_event_apply(frontend_unified_q3 *o,const qa_unified_document *d,qa_json_id row,qa_error *e)
{
    ballistic_event v;qa_buffer content={0},instance={0};uint64_t generation;unified_q3_bank *b=NULL;
    bool okay=ballistic_read(o,d,row,&v,e) && content_text(d,row,&content,e) && event_owner(d,row,&instance,&generation,e) && enter(o,v.actor,e);
    if(okay && v.kind==BALL_REMOVE){
        for(unified_q3_bank *existing=o->banks;existing;existing=existing->next){
            if(!existing->content || strcmp(existing->content,(const char *)content.data) ||
                (instance.data && (!existing->activation || strcmp(existing->activation,(const char *)instance.data) || existing->generation!=generation)))continue;
            unified_q3_ballistic *state=ballistic_find(o,existing,v.actor);
            if(state){state->projectile=false;state->bolt=false;}
        }
        qa_buffer_free(&content);qa_buffer_free(&instance);return leave(o,true,e);
    }
    if(okay)okay=bank_read(o,(const char *)content.data,(const char *)instance.data,generation,&b,e);
    unified_q3_ballistic *state=okay?ballistic_find(o,b,v.actor):NULL;
    if(okay){
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        okay=backend_read(b,frontend_viewport(o->frontend,domain->physical_seat),e);
        q3n_frame f=effect_frame(o,b,v.time);f.weapon_settings=&weapon_settings;f.event_settings=&effect_settings;
        if(okay)okay=q3n_media_load_unified_effects(b->media,&b->source,e) &&
            q3n_particles_load_unified(b->particles,&f,e) &&
            q3n_media_register_weapon(b->media,(uint32_t)v.weapon,e) && effect_current(&b->source);
        if(okay && !state){state=calloc(1,sizeof(*state));
            if(!state)okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q3 full actor ballistics");
            else{state->actor=v.actor;state->bank=b;state->next=o->ballistics;o->ballistics=state;}}
        const q3n_media_view *m=okay?q3n_media_read(b->media):NULL;
        const q3n_weapon_media *w=okay?&m->weapons[v.weapon]:NULL;
        if(okay)switch(v.kind){
        case BALL_FIRE:{
            bool silent=v.weapon==6 && state->last_fire && state->last_fire_weapon==v.weapon && q3ne_sub(v.time,state->last_fire_time)<=50;
            state->last_fire=true;state->last_fire_time=v.time;state->last_fire_weapon=v.weapon;
            state->flash=true;state->fire_time=v.time;state->flash_origin=v.origin;state->flash_end=v.end;state->weapon=v.weapon;
            int32_t sounds[4];size_t n=0;for(unsigned i=0;i<4;++i)if(w->flash_sounds[i])sounds[n++]=w->flash_sounds[i];
            if(!silent && n)okay=ballistic_sound(o,b,sounds[(uint32_t)q3n_events_rand(b->effects)%n],v.actor,v.origin,v.time,2,v.volume,e);
            break;}
        case BALL_PROJECTILE:{
            bool previous=state->projectile;int32_t prior=state->time;
            state->projectile=true;state->weapon=v.weapon;state->trajectory=v.trajectory;state->origin=v.origin;state->end=v.end;state->time=v.time;
            if(previous && w->trail==Q3N_TRAIL_PLASMA)okay=q3n_weapons_effect_plasma(&f,v.weapon,v.end,e);
            else if(previous && w->trail!=Q3N_TRAIL_NONE && w->trail!=Q3N_TRAIL_GRAPPLE && v.trajectory.type){
                qa_vec3 origin,old;uint32_t contents_now,contents_old;
                okay=q3n_trajectory(&v.trajectory,v.time,&origin,e) && q3n_trajectory(&v.trajectory,prior,&old,e) &&
                    q3n_events_point_contents(&f,origin,-1,&contents_now,e) && q3n_events_point_contents(&f,old,-1,&contents_old,e);
                if(okay && (contents_now&(8|16|32))){if(contents_now&contents_old&32)okay=q3n_effect_bubbles(&f,old,origin,8,e);}
                else if(okay){int32_t tick=q3ne_word((uint32_t)(q3ne_plus(prior,50)/50)*UINT32_C(50));
                    for(;okay && tick<=v.time;){qa_vec3 point;okay=q3n_trajectory(&v.trajectory,tick,&point,e);
                        if(okay){q3n_smoke smoke={.origin=point,.radius=w->trail_radius,.color={1,1,1,.33f},.duration=(float)w->trail_time,
                            .start_time=tick,.shader=m->graphics[w->trail==Q3N_TRAIL_NAIL?Q3N_G_NAIL_PUFF:Q3N_G_SMOKE_PUFF]};
                            q3n_effect_smoke(&f,&smoke)->type=Q3N_LE_SCALE_FADE;}
                        if(tick>INT32_MAX-50)break;
                        tick=q3ne_plus(tick,50);}}
            }break;}
        case BALL_BOUNCE:okay=ballistic_sound(o,b,m->sounds[(q3n_events_rand(b->effects)&1)?Q3N_S_GRENADE_BOUNCE2:Q3N_S_GRENADE_BOUNCE1],(qa_actor_id){0},v.end,v.time,0,1,e);break;
        case BALL_SHOTGUN:okay=q3n_weapons_effect_shotgun(&f,v.start,v.direction,v.seed,e);break;
        case BALL_RAIL:
            okay=q3n_weapons_effect_rail(&f,qa_v3(1,1,1),qa_v3(1,1,1),&v.start,v.end,e);
            if(okay && v.rail_surface){float best=0;int32_t byte=0;for(int32_t i=0;i<162;++i){float dot=qa_vec_dot(v.normal,q3n_events_direction(i));if(dot>best){best=dot;byte=i;}}
                okay=q3n_weapons_impact(&f,7,0,v.end,q3n_events_direction(byte),Q3N_IMPACT_DEFAULT,e);}break;
        case BALL_CONTACT:
            if(v.contact==0)okay=ballistic_sound(o,b,m->sounds[Q3N_S_QUAD],(qa_actor_id){0},v.origin,v.time,4,1,e);
            else if(v.contact==1)bleed(&f,b,v.point,v.target);
            else if(v.contact==2)okay=q3n_weapons_impact(&f,0,0,v.point,v.normal,Q3N_IMPACT_DEFAULT,e);
            else okay=frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Mission reflection requires its genuine compiled Source presentation binding");
            break;
        case BALL_TRAIL:
            if(v.weapon==6 || v.weapon==10){state->bolt=v.weapon!=10 || qa_vec_length(qa_vec_sub(v.end,v.origin))>=64;
                state->bolt_time=v.time;state->bolt_weapon=v.weapon;state->bolt_origin=v.origin;state->bolt_end=v.end;}
            if(v.weapon==7)okay=q3n_weapons_effect_rail(&f,qa_v3(1,1,1),qa_v3(1,1,1),&v.origin,v.end,e);
            break;
        case BALL_IMPACT:
            state->projectile=false;
            if(v.surface&16)break;
            if(v.flesh){if(v.target.registry)bleed(&f,b,v.end,v.target);
                if(v.weapon!=4 && v.weapon!=5 && !(b->source.product==QA_Q3_TEAM_ARENA && (v.weapon==11 || v.weapon==12 || v.weapon==13)))break;}
            okay=q3n_weapons_impact(&f,v.weapon,0,v.end,v.normal,v.flesh?Q3N_IMPACT_FLESH:(v.surface&4096)?Q3N_IMPACT_METAL:Q3N_IMPACT_DEFAULT,e);break;
        case BALL_RAIL_AWARD:okay=frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Rail award requires its genuine compiled Source presentation binding");break;
        default:break;
        }
    }
    qa_buffer_free(&content);qa_buffer_free(&instance);return o->busy?leave(o,okay,e):okay;
}
bool frontend_unified_q3_validate(frontend_unified_q3 *o, bool simulation,
    const qa_unified_document *d, qa_json_id row, qa_error *e)
{
    if (!o || !d || !current(o,e)) return false;
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id kind=field(j,row,"kind"), event=field(j,row,"event");
    if (simulation) return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q3 simulation requires an actual normalized family event");
    qa_buffer content={0}; bool okay=content_text(d,row,&content,e);
    const qa_product *product=NULL;
    qa_catalog *catalog=qa_executable_recipe_catalog(frontend_remote_unified_recipe(o->replica));
    for(size_t i=0;okay && !product && i<qa_catalog_count(catalog);++i){
        const qa_product *p=qa_catalog_at(catalog,i);
        if(!strcmp(p->identity,(const char *)content.data))product=p;
    }
    if(okay)okay=product && product->family==QA_GAME_Q3;
    qa_buffer_free(&content); if (!okay) return false;
    qa_actor_id id; int32_t n; qa_vec3 v;
    if (qa_json_string_equal(j,kind,"q3-character"))
        return actor(o,d,field(j,event,"actor"),&id,e) && integer(d,field(j,event,"event"),&n,e) &&
            integer(d,field(j,event,"parameter"),&n,e) && number(d,field(j,event,"timeMilliseconds"),&(double){0},e);
    if (qa_json_string_equal(j,kind,"q3-ballistics"))return ballistic_read(o,d,row,&(ballistic_event){0},e);
    if (!qa_json_string_equal(j,kind,"q3-source"))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 CLIENT received another event family");
    kind=field(j,event,"kind");
    if(qa_json_string_equal(j,kind,"server-command") || qa_json_string_equal(j,kind,"configstring")) {
        qa_buffer instance={0};
        bool stamped=text(d,field(j,field(j,row,"source"),"provider"),&instance,e);
        qa_buffer_free(&instance);
        if(!stamped)return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 Source event has no actual emitter stamp");
    }
    if (qa_json_string_equal(j,kind,"print") || qa_json_string_equal(j,kind,"log") ||
        qa_json_string_equal(j,kind,"console-command")) {
        qa_buffer b={0}; okay=text(d,field(j,event,"text"),&b,e); qa_buffer_free(&b); return okay;
    }
    if (qa_json_string_equal(j,kind,"server-command")) {
        qa_buffer b={0}; okay=integer(d,field(j,event,"client"),&n,e) && text(d,field(j,event,"text"),&b,e);
        qa_buffer_free(&b); return okay;
    }
    if (qa_json_string_equal(j,kind,"configstring")) {
        qa_buffer b={0}; okay=integer(d,field(j,event,"index"),&n,e) && n>=0 && n<1024 && text(d,field(j,event,"value"),&b,e);
        qa_buffer_free(&b); return okay;
    }
    if (qa_json_string_equal(j,kind,"sound")) {
        qa_buffer b={0}; okay=actor(o,d,field(j,event,"actor"),&id,e) && vector(d,field(j,event,"origin"),&v,e) &&
            vector(d,field(j,event,"velocity"),&v,e) && text(d,field(j,event,"path"),&b,e) &&
            integer(d,field(j,event,"channel"),&n,e) && n>=0 && real(d,field(j,event,"volume"),&(float){0},e) &&
            qa_json_bool(j,field(j,event,"loop"),&(bool){false},e);
        qa_buffer_free(&b); return okay;
    }
    if (qa_json_string_equal(j,kind,"player-event") || qa_json_string_equal(j,kind,"entity-event"))
        return actor(o,d,field(j,event,"actor"),&id,e) && vector(d,field(j,event,"origin"),&v,e) &&
            integer(d,field(j,event,"time"),&n,e);
    if (qa_json_string_equal(j,kind,"drop-client")) {
        qa_buffer b={0}; okay=integer(d,field(j,event,"client"),&n,e) && text(d,field(j,event,"reason"),&b,e);
        qa_buffer_free(&b); return okay;
    }
    return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 CLIENT received an unknown source event variant");
}
static bool character_event(frontend_unified_q3 *o, const qa_unified_document *d, qa_json_id row, qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id v=field(j,row,"event");
    qa_actor_id id; int32_t event,parameter,time; double ms;
    if (!actor(o,d,field(j,v,"actor"),&id,e) || !integer(d,field(j,v,"event"),&event,e) ||
        !integer(d,field(j,v,"parameter"),&parameter,e) || !number(d,field(j,v,"timeMilliseconds"),&ms,e)) return false;
    (void)parameter;
    if (ms<INT32_MIN || ms>INT32_MAX) return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 event clock exceeds Source storage");
    time=(int32_t)ms; event&=~0x300; qa_vec3 origin={0}; bool found=false;
    const qa_json_document *f=qa_unified_document_json(o->frame);
    qa_json_id chars=field(f,field(f,qa_unified_document_root(o->frame),"output"),"characters");
    for (size_t i=0; i<qa_json_size(f,chars); ++i) {
        qa_json_id c=qa_json_at(f,chars,i); qa_actor_id actual;
        if (!actor(o,o->frame,field(f,c,"actor"),&actual,e)) return false;
        if (qa_actor_id_equal(actual,id)) { if (!vector(o->frame,field(f,c,"origin"),&origin,e)) return false; found=true; break; }
    }
    qa_json_id bodies=field(f,field(f,field(f,qa_unified_document_root(o->frame),"output"),"snapshot"),"bodies");
    for(size_t i=0;!found && i<qa_json_size(f,bodies);++i){
        qa_json_id body=qa_json_at(f,bodies,i);qa_actor_id actual;
        if(!actor(o,o->frame,field(f,body,"actor"),&actual,e))return false;
        if(qa_actor_id_equal(actual,id)){
            if(!vector(o->frame,field(f,field(f,body,"body"),"origin"),&origin,e))return false;
            found=true;
        }
    }
    if (!found) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 character event has no current full actor body");
    /* Events with separate sound records do not create a second voice. */
    if ((event>=Q3N_EV_NONE && event<=Q3N_EV_FALL_FAR) || (event>=Q3N_EV_JUMP && event<=Q3N_EV_WATER_CLEAR) ||
        event==Q3N_EV_NOAMMO || event==Q3N_EV_CHANGE_WEAPON || (event>=Q3N_EV_PAIN && event<=Q3N_EV_OBITUARY) ||
        event==Q3N_EV_STOP_LOOP || event==Q3N_EV_TAUNT) return true;
    if (event!=Q3N_EV_JUMP_PAD && event!=Q3N_EV_TELEPORT_IN && event!=Q3N_EV_TELEPORT_OUT && event!=Q3N_EV_GIB)
        return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q3 character event requires actual cgame snapshot context");
    qa_buffer content={0},instance={0};uint64_t generation; unified_q3_bank *b=NULL;
    bool okay=content_text(d,row,&content,e) && event_owner(d,row,&instance,&generation,e) && enter(o,id,e);
    if (okay) okay=bank_read(o,(const char *)content.data,(const char *)instance.data,generation,&b,e);
    if (okay) {
        q3n_frame frame=effect_frame(o,b,time); frame.event_settings=&effect_settings;
        okay=q3n_media_load_unified_effects(b->media,&b->source,e);
        if (okay && event==Q3N_EV_JUMP_PAD) {
            q3n_smoke smoke={.origin=origin,.velocity={0,0,1},.radius=32,.color={1,1,1,.33f},
                .duration=1000,.start_time=time,.flags=1,.shader=q3n_media_read(b->media)->graphics[Q3N_G_SMOKE_PUFF]};
            q3n_effect_smoke(&frame,&smoke);
        } else if (okay && event==Q3N_EV_GIB) q3n_effect_gib_player(&frame,origin);
        else if (okay) q3n_effect_spawn(&frame,origin);
    }
    qa_buffer_free(&content);qa_buffer_free(&instance); return o->busy?leave(o,okay,e):okay;
}
static bool resource_first(unified_q3_bank *b,const char *const *paths,size_t count,
    qa_resource **out,char selected[256],qa_error *e)
{
    for (size_t i=0; i<count; ++i) {
        qa_error local={0}; qa_resource *r=NULL;
        bool okay=qa_vfs_acquire(b->files,paths[i],&r,NULL,&local);
        if (!okay && local.code!=QA_ERROR_NOT_FOUND) { if (e) *e=local; return false; }
        if (r && qa_resource_bytes(r).size) {
            snprintf(selected,256,"%s",paths[i]); *out=r; return current(b->owner,e);
        }
        qa_resource_release(r);
    }
    return frontend_unified_fail(e,QA_ERROR_NOT_FOUND,"Q3 character's selected resource is missing");
}
static bool character_assets(frontend_unified_q3 *o,unified_q3_character *c,qa_error *e)
{
    qa_executable_recipe *recipe=frontend_remote_unified_recipe(o->replica);
    const qa_recipe_choices *choices=qa_executable_recipe_choices(recipe);
    const qa_launch_seat *seat=NULL;
    for (size_t i=0; i<choices->seat_count; ++i)
        if (choices->seats[i].actor.present && choices->seats[i].actor.slot==o->replica->wire_player.slot &&
            choices->seats[i].actor.generation==o->replica->wire_player.generation) seat=&choices->seats[i].selection;
    if (!seat || !seat->character_model || !seat->character_skin || !seat->character_head_model ||
        !seat->character_head_skin)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Unified character lacks its actual offered appearance declaration");
    char model[64],skin[64],head[64],headskin[64];
    snprintf(model,sizeof(model),"%.63s",seat->character_model);
    snprintf(skin,sizeof(skin),"%.63s",seat->character_skin);
    snprintf(head,sizeof(head),"%.63s",*seat->character_head_model?seat->character_head_model:model);
    snprintf(headskin,sizeof(headskin),"%.63s",seat->character_head_skin);
    if (!*model || !*skin || !*headskin || strpbrk(model,"/\\") || strpbrk(skin,"/\\") ||
        strpbrk(head+(head[0]=='*'),"/\\") || strpbrk(headskin,"/\\"))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 character declaration contains a path component");
    char paths[8][256],actual[256]; const char *names[8]; qa_resource *holder=NULL;
    for (unsigned i=0; i<2; ++i) {
        const char *part=i?"upper":"lower";
        snprintf(paths[0],256,"models/players/%s/%s.md3",model,part);
        snprintf(paths[1],256,"models/players/characters/%s/%s.md3",model,part);
        names[0]=paths[0]; names[1]=paths[1];
        if (!resource_first(c->bank,names,2,&holder,actual,e)) return false;
        qa_resource_release(holder); holder=NULL;
        if (!qa_q3_register_model(c->bank->assets,actual,&c->models[i],e) || !c->models[i] || !current(o,e)) return false;
        snprintf(paths[0],256,"models/players/%s/%s_%s_default.skin",model,part,skin);
        snprintf(paths[1],256,"models/players/%s/%s_%s.skin",model,part,skin);
        snprintf(paths[2],256,"models/players/characters/%s/%s_%s_default.skin",model,part,skin);
        snprintf(paths[3],256,"models/players/characters/%s/%s_%s.skin",model,part,skin);
        for (unsigned j=0;j<4;++j) names[j]=paths[j];
        if (!resource_first(c->bank,names,4,&holder,actual,e)) return false;
        qa_resource_release(holder); holder=NULL;
        if (!qa_q3_register_skin(c->bank->assets,actual,&c->skins[i],e) || !c->skins[i] || !current(o,e)) return false;
    }
    if (*head=='*') snprintf(paths[0],256,"models/players/heads/%s/%s.md3",head+1,head+1);
    else snprintf(paths[0],256,"models/players/%s/head.md3",head);
    snprintf(paths[1],256,"models/players/heads/%s/%s.md3",head+(*head=='*'),head+(*head=='*'));
    names[0]=paths[0]; names[1]=paths[1];
    if (!resource_first(c->bank,names,*head=='*'?1:2,&holder,actual,e)) return false;
    qa_resource_release(holder); holder=NULL;
    if (!qa_q3_register_model(c->bank->assets,actual,&c->models[2],e) || !c->models[2] || !current(o,e)) return false;
    size_t n=0;
    for (unsigned folder=*head=='*'?1:0; folder<2; ++folder) {
        const char *base=folder?"heads/":"";
        snprintf(paths[n++],256,"models/players/%s%s/%s/head_default.skin",base,head+(*head=='*'),headskin);
        snprintf(paths[n++],256,"models/players/%s%s/head_%s.skin",base,head+(*head=='*'),headskin);
    }
    for (size_t i=0;i<n;++i) names[i]=paths[i];
    if (!resource_first(c->bank,names,n,&holder,actual,e)) return false;
    qa_resource_release(holder); holder=NULL;
    if (!qa_q3_register_skin(c->bank->assets,actual,&c->skins[2],e) || !c->skins[2] || !current(o,e)) return false;
    snprintf(paths[0],256,"models/players/%s/animation.cfg",model);
    snprintf(paths[1],256,"models/players/characters/%s/animation.cfg",model);
    names[0]=paths[0]; names[1]=paths[1];
    if (!resource_first(c->bank,names,2,&holder,actual,e)) return false;
    bool okay=q3n_selected_animation_parse(qa_resource_bytes(holder),actual,&c->animation,e) && current(o,e);
    if (okay) { qa_resource_release(c->animation_holder); c->animation_holder=holder; }
    else qa_resource_release(holder);
    return okay;
}
static bool character_read(frontend_unified_q3 *o,qa_json_id row,unified_q3_character **out,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(o->frame); qa_actor_id id;
    if (!actor(o,o->frame,field(j,row,"actor"),&id,e)) return false;
    const qa_recipe_provider *p=frontend_remote_unified_provider(o->replica,QA_ROLE_BODY,"");
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    const qa_product *product=p?qa_catalog_product(domain->catalog,p->selection.product):NULL;
    if (!p || !product || product->family!=QA_GAME_Q3)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 character has no real received appearance provider");
    unified_q3_bank *bank=NULL; if (!bank_read(o,product->identity,p->selection.instance,0,&bank,e)) return false;
    unified_q3_character *c=o->characters;
    while (c && !qa_actor_id_equal(c->actor,id)) c=c->next;
    if (!c) {
        c=calloc(1,sizeof(*c));
        if (!c) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining full actor Q3 character pose");
        c->actor=id; c->bank=bank; c->reset=true; c->next=o->characters; o->characters=c;
    }
    if (c->bank!=bank) { qa_resource_release(c->animation_holder); c->animation_holder=NULL; c->bank=bank; c->reset=true; }
    if (!c->animation_holder && !character_assets(o,c,e)) return false;
    qa_json_id animation=field(j,row,"animation");
    if (!vector(o->frame,field(j,row,"origin"),&c->origin,e) || !vector(o->frame,field(j,row,"angles"),&c->angles,e) ||
        !vector(o->frame,field(j,row,"velocity"),&c->velocity,e) ||
        !integer(o->frame,field(j,row,"movementDirection"),&c->movement,e) ||
        !qa_json_string_equal(j,field(j,animation,"kind"),"q3") ||
        !integer(o->frame,field(j,animation,"legs"),&c->legs,e) || !integer(o->frame,field(j,animation,"torso"),&c->torso,e) ||
        !word(o->frame,field(j,row,"sourceFlags"),&c->flags,e) || !word(o->frame,field(j,row,"powerups"),&c->powerups,e)) return false;
    c->scale=c->opacity=1;
    qa_json_id scale=field(j,row,"scale"),opacity=field(j,row,"opacity"),team=field(j,row,"team");
    if((scale!=QA_JSON_NONE && !real(o->frame,scale,&c->scale,e)) ||
        (opacity!=QA_JSON_NONE && !real(o->frame,opacity,&c->opacity,e)))return false;
    c->team=qa_json_string_equal(j,team,"red")?1:qa_json_string_equal(j,team,"blue")?2:0;
    if(!c->team && qa_json_type(j,team)!=QA_JSON_NULL)return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 character has no actual nullable team tag");
    const char *names[]={"x","y","z","w"}; qa_json_id color=field(j,row,"color");
    for (unsigned i=0;i<4;++i) if (!real(o->frame,field(j,color,names[i]),c->color+i,e)) return false;
    c->visible=true; *out=c; return true;
}
static bool character_draw(frontend_unified_q3 *o,unified_q3_character *c,qa_error *e)
{
    qa_actor_id viewer; uint32_t number;
    if (!frontend_remote_unified_player(o->replica,&viewer,&number)) return false;
    c->hidden=(c->flags&128)!=0 || c->opacity<=0 || c->scale==0;
    if (c->hidden) return true;
    if (c->reset) {
        if (!q3n_lerp_clear(&c->animation,&c->pose.legs.animation,c->legs,o->time,e) ||
            !q3n_lerp_clear(&c->animation,&c->pose.torso.animation,c->torso,o->time,e)) return false;
        c->pose=(q3n_player_pose){.legs={.yaw_angle=c->angles.y},
            .torso={.yaw_angle=c->angles.y,.pitch_angle=c->angles.x}}; c->reset=false;
    }
    q3n_pose_axes axes; q3n_pose_entity state={c->flags,c->velocity,c->movement,c->legs,c->torso};
    int32_t elapsed; uint32_t bits=(uint32_t)o->time-(uint32_t)o->previous_time; memcpy(&elapsed,&bits,4);
    if (!q3n_player_angles_pose(&c->pose,&c->animation,&state,c->angles,o->time,elapsed,.3f,&axes,e)) return false;
    int32_t legs=c->pose.legs.yawing && (c->legs&~128)==22?24:c->legs;
    float speed=c->powerups&(1u<<3)?1.5f:1;
    if (!q3n_lerp_run(&c->animation,&c->pose.legs.animation,legs,o->time,speed,false,e) ||
        !q3n_lerp_run(&c->animation,&c->pose.torso.animation,c->torso,o->time,speed,false,e)) return false;
    qa_q3_ref_entity parts[3]={0}; const q3n_lerp_frame *lerps[]={&c->pose.legs.animation,&c->pose.torso.animation,NULL};
    const qa_vec3 *part_axes[]={axes.legs,axes.torso,axes.head};
    for (unsigned i=0;i<3;++i) {
        parts[i]=(qa_q3_ref_entity){.kind=QA_Q3_REF_MODEL,.model=c->models[i],.custom_skin=c->skins[i],
            .flags=128|(qa_actor_id_equal(viewer,c->actor)?2:0),.lighting_origin=c->origin,.shader_time=(float)o->time*.001f};
        memcpy(parts[i].axis,part_axes[i],sizeof(parts[i].axis));
        if (lerps[i]) { parts[i].frame=lerps[i]->frame; parts[i].old_frame=lerps[i]->old_frame; parts[i].back_lerp=lerps[i]->back_lerp; }
        for (unsigned j=0;j<4;++j) parts[i].color[j]=(uint8_t)(uint32_t)(int32_t)truncf(fminf(1,fmaxf(0,c->color[j]))*255);
        parts[i].color[3]=(uint8_t)truncf(fminf(1,fmaxf(0,c->opacity*c->color[3]))*255);
    }
    parts[0].origin=c->origin;
    for (unsigned i=0;i<3;++i) parts[0].axis[i]=qa_vec_scale(parts[0].axis[i],c->scale);
    parts[0].non_normalized_axes=c->scale!=1;
    if (!q3n_attach(c->bank->assets,&parts[1],&parts[0],"tag_torso",true,e) ||
        !q3n_attach(c->bank->assets,&parts[2],&parts[1],"tag_head",true,e)) return false;
    for (unsigned i=0;i<3;++i) parts[i].old_origin=parts[i].origin;
    memcpy(c->submitted_parts,parts,sizeof(parts));
    const char *shaders[4]={NULL}; size_t count=1;
    if (c->powerups&(1u<<4)) shaders[0]="powerups/invisibility";
    else {
        if (c->powerups&(1u<<1)) shaders[count++]=c->team==1?"powerups/blueflag":"powerups/quad";
        if ((c->powerups&(1u<<5)) && (o->time/100)%10==1) shaders[count++]="powerups/regen";
        if (c->powerups&(1u<<2)) shaders[count++]="powerups/battleSuit";
    }
    for (size_t pass=0;pass<count;++pass) {
        int32_t shader=0;
        if (shaders[pass] && (!qa_q3_register_shader(c->bank->assets,shaders[pass],true,&shader,e) || !current(o,e))) return false;
        for (unsigned part=0;part<3;++part) { parts[part].custom_shader=shader;
            if (!qa_q3_presentation_entity(c->bank->backend,&parts[part],e) || !current(o,e)) return false; }
    }
    c->submitted=true;c->time=o->time; return true;
}
static qa_vec3 perpendicular(qa_vec3 n)
{
    qa_vec3 a=fabsf(n.x)<fabsf(n.y)?(fabsf(n.x)<fabsf(n.z)?qa_v3(1,0,0):qa_v3(0,0,1)):
        (fabsf(n.y)<fabsf(n.z)?qa_v3(0,1,0):qa_v3(0,0,1));
    return qa_vec_normalize(qa_vec_sub(a,qa_vec_scale(n,qa_vec_dot(a,n))));
}
static void ballistic_axis(qa_vec3 direction,float rotation,qa_vec3 axes[3])
{
    qa_vec3 n=qa_vec_normalize(direction);if(qa_vec_length(n)==0)n=qa_v3(0,0,1);
    qa_vec3 side=perpendicular(n),cross=qa_vec_cross(n,side);float angle=rotation*.01745329251994329577f;
    axes[0]=n;axes[1]=qa_vec_add(qa_vec_scale(side,cosf(angle)),qa_vec_scale(cross,sinf(angle)));axes[2]=qa_vec_cross(n,axes[1]);
}
static bool ballistic_loop(frontend_unified_q3 *o,unified_q3_bank *b,int32_t handle,
    qa_actor_id id,qa_vec3 origin,qa_vec3 velocity,qa_error *e)
{
    qa_audio_asset *asset=q3p_sound(b->assets,handle);
    return !asset || (o->events && frontend_unified_events_sound_loop_path(o->events,b->content,
        qa_audio_asset_name(asset),id,origin,velocity,o->time,0,1,1,
        (int32_t)(uint32_t)o->frontend->frame_number,false,e) && effect_current(&b->source));
}
static bool ballistic_draw(frontend_unified_q3 *o,unified_q3_bank *b,qa_error *e)
{
    qa_actor_id viewer;uint32_t source_number;
    if(!frontend_remote_unified_player(o->replica,&viewer,&source_number))return false;
    const q3n_media_view *m=q3n_media_read(b->media);
    for(unified_q3_ballistic *v=o->ballistics;v;v=v->next)if(v->bank==b){
        if(v->last_fire && q3ne_sub(o->time,v->last_fire_time)>50)v->last_fire=false;
        const q3n_weapon_media *w=&m->weapons[v->weapon];
        if(v->projectile){
            qa_q3_ref_entity ref={.kind=v->weapon==8?QA_Q3_REF_SPRITE:QA_Q3_REF_MODEL,.model=w->missile_model,
                .origin=v->end,.old_origin=v->end,.flags=w->missile_render_flags|64};
            if(v->weapon==8){ref.radius=16;ref.custom_shader=m->graphics[Q3N_G_PLASMA_BALL];}
            else ballistic_axis(q3ne_array(v->trajectory.delta),v->trajectory.type?(float)(o->time/4):0,ref.axis);
            if(!qa_q3_presentation_entity(b->backend,&ref,e))return false;
            if(w->missile_light && !qa_q3_presentation_light(b->backend,v->end,w->missile_light,w->missile_light_color,false,e))return false;
            qa_vec3 velocity;if(!q3n_trajectory_delta(&v->trajectory,o->time,&velocity,e) || !ballistic_loop(o,b,w->missile_sound,v->actor,v->end,velocity,e))return false;
        }
        if(v->flash){
            if(q3ne_sub(o->time,v->fire_time)>20)v->flash=false;
            else{
                qa_q3_ref_entity ref={.kind=QA_Q3_REF_MODEL,.model=w->flash_model,.origin=v->flash_origin,
                    .old_origin=v->flash_origin,.flags=qa_actor_id_equal(v->actor,viewer)?2:0};
                ballistic_axis(qa_vec_sub(v->flash_end,v->flash_origin),0,ref.axis);
                if(!qa_q3_presentation_entity(b->backend,&ref,e))return false;
                if(!o->preferences.reduced_flashes && qa_vec_length(w->flash_light_color)>0 && !qa_q3_presentation_light(b->backend,v->flash_origin,
                    300+(float)(q3n_events_rand(b->effects)&31),w->flash_light_color,false,e))return false;
            }
        }
        if(v->bolt){
            if(q3ne_sub(o->time,v->bolt_time)>50)v->bolt=false;
            else{
                qa_q3_ref_entity ref={.kind=QA_Q3_REF_LIGHTNING,.origin=v->bolt_origin,.old_origin=v->bolt_end,
                    .custom_shader=m->graphics[Q3N_G_LIGHTNING_SHADER]};
                if(!qa_q3_presentation_entity(b->backend,&ref,e) || !ballistic_loop(o,b,m->weapons[v->bolt_weapon].firing_sound,
                    v->actor,v->bolt_origin,qa_v3(0,0,0),e))return false;
            }
        }
        if(!current(o,e))return false;
    }
    return true;
}
static bool sample(frontend_unified_q3 *o,const qa_scene_view *view,const qa_scene_world_input *world,
    qa_scene_frame *scene,qa_error *e)
{
    if (!view || !world || !scene || !enter(o,(qa_actor_id){0},e)) return false;
    for(unified_q3_bank *b=o->banks;b;b=b->next)qa_q3_presentation_supplement_release(&b->supplement);
    o->sampled=false;o->sampled_scene=NULL;o->world_submitted=false;
    o->view=view; o->world=world; o->scene=scene; o->light_count=0;
    bool okay=true;
    if(!o->scene_bank){
        uint32_t polygons,vertices;bool initialized=false;
        qa_render_controls *controls=o->frontend->cpu?qa_cpu_render_controls(o->frontend->cpu):qa_gl_render_controls(o->frontend->gl);
        okay=frontend_q3_scene_limits_initialize(o->frontend,e) && current(o,e) &&
            qa_render_controls_source_scene_limits_read(controls,&polygons,&vertices,&initialized,e) && initialized &&
            qa_q3_source_scene_bank_create(polygons,vertices,&o->scene_bank,e);
    }
    if(okay){qa_q3_source_scene_bank_frame(o->scene_bank);okay=component_bank_read(o,e);}
    for (unified_q3_character *c=o->characters;c;c=c->next) { c->visible=false;c->submitted=false;c->hidden=false; }
    const qa_json_document *j=qa_unified_document_json(o->frame);
    qa_json_id rows=field(j,field(j,qa_unified_document_root(o->frame),"output"),"characters");
    size_t character_count=qa_json_size(j,rows);
    o->character_count=0;
    if(okay && character_count>o->character_capacity) {
        if(character_count>SIZE_MAX/sizeof(*o->character_order))
            okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Q3 character order exceeds native storage");
        else {
            unified_q3_character **order=realloc(o->character_order,character_count*sizeof(*order));
            if(!order)okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual FRAME character order");
            else { o->character_order=order; o->character_capacity=character_count; }
        }
    }
    for (size_t i=0;okay && i<character_count;++i) {
        unified_q3_character *c;
        okay=character_read(o,qa_json_at(j,rows,i),&c,e);
        if(okay)o->character_order[o->character_count++]=c;
    }
    for (unified_q3_bank *b=o->banks;okay && b;b=b->next)if(!b->retired) {
        q3n_frame frame=effect_frame(o,b,o->time); frame.event_settings=&effect_settings; frame.weapon_settings=&weapon_settings;
        frame.refdef.origin=view->origin; memcpy(frame.refdef.axis,view->axis,sizeof(frame.refdef.axis));
        okay=backend_read(b,view->viewport,e) && qa_q3_presentation_frame(b->backend,scene,view->viewport,e) &&
            qa_q3_presentation_world(b->backend,frontend_unified_media_world(o->media),
                (qa_collision_geometry *)frontend_remote_unified_geometry(o->replica),(qa_bytes){0},e) &&
            qa_q3_presentation_clear(b->backend,e);
        frame.presentation=b->backend;
        if (okay) okay=q3n_media_load_unified_effects(b->media,&b->source,e) && q3n_particles_load_unified(b->particles,&frame,e);
        for(size_t i=0;okay && i<o->character_count;++i) {
            unified_q3_character *c=o->character_order[i];
            if(c->visible && c->bank==b)okay=character_draw(o,c,e);
        }
        if(okay)okay=ballistic_draw(o,b,e);
        if (okay) okay=q3n_marks_submit(&frame,e) && q3n_particles_add(&frame,e);
        qa_bounds bounds;
        if(okay)okay=qa_collision_model_bounds(frontend_remote_unified_geometry(o->replica),0,&bounds,e);
        if(okay){frame.refdef.origin=qa_vec_add(bounds.maxs,qa_v3(65536,65536,65536));okay=q3n_local_submit(&frame,e);}
        const qa_scene_light *lights=NULL; size_t count=0;
        if (okay) okay=qa_q3_presentation_lights_read(b->backend,&lights,&count,e);
        if (okay && count>SIZE_MAX-o->light_count) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Q3 light count overflow");
        size_t needed=o->light_count+count;
        if (okay && needed>o->light_capacity) {
            if (needed>SIZE_MAX/sizeof(*o->lights)) okay=false;
            else { qa_scene_light *next=realloc(o->lights,needed*sizeof(*next));
                if (!next) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining Q3 private CLIENT lights");
                else { o->lights=next; o->light_capacity=needed; } }
        }
        for(size_t i=0;okay && i<count;++i){
            bool admitted=false;
            okay=qa_q3_source_scene_bank_light(o->scene_bank,b->assets,lights+i,&admitted,e) && current(o,e);
            if(okay && admitted)o->lights[o->light_count++]=lights[i];
        }
    }
    for(unified_q3_bank *b=o->banks;okay && b;b=b->next)if(!b->retired)
        okay=qa_q3_presentation_supplement_prepare(b->backend,o->scene_bank,scene,&b->supplement,e) && current(o,e);
    if (okay) { o->sampled_view=*view; o->sampled_scene=scene; o->sampled_frame_number=o->frontend->frame_number; o->sampled=true; }
    o->view=NULL; o->world=NULL; o->scene=NULL; return leave(o,okay,e);
}
bool frontend_unified_q3_lights(frontend_unified_q3 *o,const qa_scene_view *view,const qa_scene_world_input *world,
    const qa_scene_light **out,size_t *count,qa_error *e)
{
    if (!out || !count || !sample(o,view,world,&o->frontend->frame,e)) return false;
    *out=o->lights; *count=o->light_count; return true;
}
bool frontend_unified_q3_scene_bank_read(frontend_unified_q3 *o,const qa_scene_frame *frame,
    qa_q3_source_scene_bank **out,qa_error *e)
{
    if(!o || !out || !frame || frame!=&o->frontend->frame || o->busy || o->prepared || !current(o,e) ||
        !o->scene_bank || o->sampled_scene!=frame || o->sampled_frame_number!=o->frontend->frame_number)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 Source bank requires its actual sampled recipient frame");
    *out=o->scene_bank;return true;
}
bool frontend_unified_q3_hud_recipient_read(frontend_unified_q3 *o,const qa_scene_frame *frame,
    qa_q3_presentation **out,qa_error *e)
{
    qa_q3_source_scene_bank *bank=NULL;qa_q3_presentation_binding binding;
    if(!out || !frontend_unified_q3_scene_bank_read(o,frame,&bank,e))return false;
    if(!o->component_bank){*out=NULL;return true;}
    if(o->component_bank->retired || !o->component_bank->backend ||
        !qa_q3_presentation_idle(o->component_bank->backend) ||
        !qa_q3_presentation_binding_read(o->component_bank->backend,&binding,e) || binding.frame!=frame ||
        binding.options.assets!=o->component_bank->assets || binding.options.context!=o->component_bank ||
        binding.options.owner!=o->audio_owner)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 HUD packets require their real returned recipient backend");
    *out=o->component_bank->backend;return true;
}
static bool character_receipt(frontend_unified_q3 *o,const q3n_frame *f,
    const q3n_compiled_entity *row,unified_q3_character **out,qa_error *e)
{
    const frontend_remote_unified_domain *domain=o?frontend_remote_unified_domain_read(o->replica):NULL;
    qa_q3_presentation_binding binding;
    if(!o || !f || !row || !out || !f->compiled || row->frame!=f->compiled ||
        (!row->published && !row->predicted) || !q3n_frame_current(f) || !q3n_compiled_entity_current(row) ||
        !domain || domain->application!=f->application || f->application!=o->frontend->application ||
        domain->physical_seat!=f->physical_presentation_seat ||
        f->compiled->source.basis.physical_seat!=domain->physical_seat ||
        !qa_q3_presentation_binding_read(f->presentation,&binding,e) || binding.frame!=&o->frontend->frame ||
        binding.options.assets!=f->assets || binding.options.seat!=domain->physical_seat ||
        !current(o,e) || o->busy || o->prepared || !o->sampled || o->sampled_scene!=&o->frontend->frame ||
        o->sampled_frame_number!=o->frontend->frame_number)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Compiled CHARACTER requires the actual returned sampled frame");
    *out=NULL;
    for(unified_q3_character *c=o->characters;c;c=c->next)if(c->visible && qa_actor_id_equal(c->actor,row->actor)) {
        if(!c->bank || c->bank->retired || (!c->submitted && !c->hidden) ||
            !qa_q3_assets_idle(c->bank->assets) || !qa_q3_presentation_idle(c->bank->backend) ||
            !qa_q3_presentation_supplement_current(c->bank->supplement,o->sampled_scene))
            return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Selected CHARACTER submission is not returned");
        *out=c;break;
    }
    return true;
}
bool frontend_unified_q3_body_hidden(void *context,const q3n_frame *f,const q3n_compiled_entity *row,
    bool *hidden,qa_error *e)
{
    unified_q3_character *c=NULL;
    if(!hidden || !character_receipt(context,f,row,&c,e))return false;
    *hidden=c && c->hidden;return true;
}
bool frontend_unified_q3_body_submit(void *context,const q3n_frame *f,const q3n_compiled_entity *row,
    uint32_t part,const qa_q3_ref_entity *ref,bool base,bool *consumed,qa_error *e)
{
    (void)base;unified_q3_character *c=NULL;
    if(!consumed || !ref || part>=3 || !character_receipt(context,f,row,&c,e))return false;
    *consumed=c && (c->submitted || c->hidden);return true;
}
bool frontend_unified_q3_player_weapon(void *context,const q3n_frame *f,const q3n_compiled_entity *row,
    const qa_q3_ref_entity *torso,int32_t team,qa_error *e)
{
    (void)team;unified_q3_character *c=NULL;
    if(!torso || !character_receipt(context,f,row,&c,e))return false;
    if(!c)return q3n_weapons_player_compiled(f,torso,row,e);
    if(c->hidden)return true;
    return q3n_weapons_player_compiled_parent(f,c->bank->assets,&c->submitted_parts[1],row,e) &&
        character_receipt(context,f,row,&c,e);
}
bool frontend_unified_q3_equipment_replacement(frontend_unified_q3 *o,const q3n_frame *f,const qa_q3_player *ps,
    const frontend_unified_render_equipment *equipment,bool present,bool *consumed,qa_error *e)
{
    const frontend_remote_unified_domain *domain=o?frontend_remote_unified_domain_read(o->replica):NULL;
    qa_q3_presentation_binding binding;qa_actor_id viewer;uint32_t source_entity;
    if(!o || !f || !f->compiled || !consumed || ps!=q3n_frame_predicted_player(f) || !q3n_frame_current(f) ||
        !domain || domain->application!=f->application || f->application!=o->frontend->application ||
        domain->physical_seat!=f->physical_presentation_seat ||
        f->compiled->source.basis.physical_seat!=domain->physical_seat ||
        !frontend_remote_unified_player(o->replica,&viewer,&source_entity) ||
        !qa_actor_id_equal(viewer,f->compiled->source.basis.viewer) ||
        !qa_q3_presentation_binding_read(f->presentation,&binding,e) || binding.frame!=&o->frontend->frame ||
        binding.options.assets!=f->assets || binding.options.seat!=domain->physical_seat ||
        !current(o,e) || o->busy || o->prepared)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Selected view equipment lost its actual compiled recipient");
    *consumed=false;
    if(!present)return true;
    if(!equipment || !qa_actor_id_equal(equipment->actor,viewer) || !equipment->provider ||
        !equipment->instance || !equipment->input || !equipment->input->view_model ||
        equipment->source_frame!=o->replica->frame_number || equipment->scene_sequence!=binding.frame->sequence ||
        !f->compiled->source.basis.instance)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Selected view equipment changed its registered frame receipt");
    *consumed=equipment->slot || equipment->provider!=f->compiled->source.basis.provider ||
        strcmp(equipment->instance,f->compiled->source.basis.instance)!=0;
    return true;
}
bool frontend_unified_q3_world(frontend_unified_q3 *o,const qa_scene_view *view,
    const qa_scene_world_input *world,qa_scene_frame *scene,qa_error *e)
{
    if (!o || !view || !world || !scene) return false;
    if(o->world_submitted)return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 primary supplement already belongs to this sampled view");
    if (!o->sampled && !sample(o,view,world,scene,e)) return false;
    if (o->sampled_scene!=scene || o->sampled_frame_number!=o->frontend->frame_number ||
        memcmp(&o->sampled_view.viewport,&view->viewport,sizeof(view->viewport)) ||
        memcmp(o->sampled_view.axis,view->axis,sizeof(view->axis)) ||
        memcmp(o->sampled_view.projection.m,view->projection.m,sizeof(view->projection.m)) ||
        o->sampled_view.clear_color!=view->clear_color || o->sampled_view.clear_depth!=view->clear_depth ||
        o->sampled_view.clear_stencil!=view->clear_stencil || o->sampled_view.clip_enabled!=view->clip_enabled ||
        o->sampled_view.mirror!=view->mirror || o->sampled_view.seat!=view->seat || o->sampled_view.depth!=view->depth ||
        o->sampled_view.color.x!=view->color.x || o->sampled_view.color.y!=view->color.y ||
        o->sampled_view.color.z!=view->color.z || o->sampled_view.color.w!=view->color.w ||
        o->sampled_view.clip_plane.normal.x!=view->clip_plane.normal.x ||
        o->sampled_view.clip_plane.normal.y!=view->clip_plane.normal.y ||
        o->sampled_view.clip_plane.normal.z!=view->clip_plane.normal.z ||
        o->sampled_view.clip_plane.distance!=view->clip_plane.distance ||
        o->sampled_view.origin.x!=view->origin.x || o->sampled_view.origin.y!=view->origin.y ||
        o->sampled_view.origin.z!=view->origin.z || !enter(o,(qa_actor_id){0},e)) return false;
    qa_collision_family family=qa_collision_geometry_family(frontend_remote_unified_geometry(o->replica));
    qa_q3_scene_options options={.world=*world,.world_family=family==QA_COLLISION_Q1?QA_SCENE_Q1:family==QA_COLLISION_Q2?QA_SCENE_Q2:QA_SCENE_Q3,.lod_scale=5,
        .ambient_scale=.6f,.directed_scale=1,.near_clip=4,.rail={6,16,32}};
    qa_scene_state_default(&options.state); bool okay=true;
    for (unified_q3_bank *b=o->banks;okay && b;b=b->next)
        if(!b->retired)okay=qa_q3_presentation_supplement_prepare(b->backend,o->scene_bank,scene,&b->supplement,e) && current(o,e);
    for (unified_q3_bank *b=o->banks;okay && b;b=b->next)
        if(!b->retired)okay=qa_q3_presentation_supplement_draw(b->supplement,&options,scene,e) && current(o,e);
    if(okay){o->sampled=false;o->world_submitted=true;}
    return leave(o,okay,e);
}
bool frontend_unified_q3_reflected_world(frontend_unified_q3 *o,const qa_scene_world_input *world,
    qa_scene_frame *scene,qa_error *e)
{
    if(!o || !world || !scene || o->sampled_scene!=scene ||
        o->sampled_frame_number!=o->frontend->frame_number || scene!=&o->frontend->frame ||
        !qa_scene_world_q1_mirror_scope(frontend_unified_media_world(o->media),world,scene) ||
        !enter(o,(qa_actor_id){0},e))return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 reflection requires its real mirror scope and sampled frame");
    qa_q3_scene_options options={.world=*world,.world_family=QA_SCENE_Q1,.lod_scale=5,
        .ambient_scale=.6f,.directed_scale=1,.near_clip=4,.rail={6,16,32}};
    qa_scene_state_default(&options.state);bool okay=true;
    for(unified_q3_bank *b=o->banks;okay && b;b=b->next)if(!b->retired)
        okay=qa_q3_presentation_supplement_prepare(b->backend,o->scene_bank,scene,&b->supplement,e) && current(o,e);
    if(okay && o->components && o->component_bank)
        okay=frontend_unified_components_prepare_submission(o->components,o->scene_bank,scene,e) && current(o,e);
    for(unified_q3_bank *b=o->banks;okay && b;b=b->next)if(!b->retired)
        okay=qa_q3_presentation_supplement_draw(b->supplement,&options,scene,e) && current(o,e);
    return leave(o,okay,e);
}
static bool source_command_event(frontend_unified_q3 *o,const qa_unified_document *d,
    qa_json_id row,bool command,qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id event=field(j,row,"event");
    qa_buffer content={0},instance={0},value={0};
    double sequence=0;int32_t recipient=-1,index=0;
    qa_json_id source=field(j,row,"source");
    bool okay=content_text(d,row,&content,e) && text(d,field(j,source,"provider"),&instance,e);
    if(okay && command) {
        okay=number(d,field(j,row,"sequence"),&sequence,e);
        if(okay && (sequence<0 || sequence>9007199254740991.0 || trunc(sequence)!=sequence))
            okay=frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 reliable event sequence exceeds its actual source domain");
        if(okay)okay=integer(d,field(j,event,"client"),&recipient,e) && text(d,field(j,event,"text"),&value,e);
    } else if(okay) {
        okay=integer(d,field(j,event,"index"),&index,e) && index>=0 && index<1024 &&
            text(d,field(j,event,"value"),&value,e);
    }
    bool matched=false;
    size_t count=frontend_remote_unified_presentation_q3_client_count(o->replica);
    for(size_t i=0;okay && i<count;++i) {
        frontend_unified_q3_client *client=frontend_remote_unified_presentation_q3_client(o->replica,i);
        if(!frontend_unified_q3_client_event_matches(client,(const char *)instance.data,
            (const char *)content.data,o->epoch))continue;
        matched=true;
        if(command)okay=frontend_unified_q3_client_server_command(client,(uint64_t)sequence,
            recipient,(const char *)value.data,e);
        /* Notification values may be intermediate writes. The real CLIENT
         * compares the committed Source dictionary and reaches its own cs. */
        if(okay)okay=current(o,e);
    }
    if(okay && !matched)okay=frontend_unified_fail(e,QA_ERROR_ARGUMENT,
        "Q3 Source event has no matching retained CLIENT activation");
    qa_buffer_free(&content);qa_buffer_free(&instance);qa_buffer_free(&value);return okay;
}
bool frontend_unified_q3_presentation(frontend_unified_q3 *o, const qa_unified_document *d,
    qa_json_id row, bool *mirrored, qa_error *e)
{
    if (!mirrored || !frontend_unified_q3_validate(o,false,d,row,e)) return false;
    *mirrored=false; const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id kind=field(j,row,"kind"), v=field(j,row,"event");
    if (qa_json_string_equal(j,kind,"q3-character")) return character_event(o,d,row,e);
    if (qa_json_string_equal(j,kind,"q3-ballistics"))return ballistic_event_apply(o,d,row,e);
    kind=field(j,v,"kind"); qa_buffer b={0}; bool okay;
    if (qa_json_string_equal(j,kind,"sound")) {
        qa_buffer content={0},path={0}; qa_actor_id id; qa_vec3 origin,velocity; int32_t channel; float volume; double seconds; bool loop;
        okay=o->events && content_text(d,row,&content,e) && text(d,field(j,v,"path"),&path,e) &&
            actor(o,d,field(j,v,"actor"),&id,e) && vector(d,field(j,v,"origin"),&origin,e) && vector(d,field(j,v,"velocity"),&velocity,e) &&
            integer(d,field(j,v,"channel"),&channel,e) && real(d,field(j,v,"volume"),&volume,e) &&
            number(d,field(j,row,"seconds"),&seconds,e) && qa_json_bool(j,field(j,v,"loop"),&loop,e);
        bool duplicate=false;
        if (okay) okay=frontend_unified_events_sound_mirrored(o->events,d,row,&duplicate,e);
        if (okay && !duplicate) {
            if(loop)okay=frontend_unified_events_sound_loop_path(o->events,(const char *)content.data,
                (const char *)path.data,id,origin,velocity,seconds*1000,channel,volume,1,
                q3ne_word((uint32_t)o->frontend->frame_number),true,e);
            else okay=frontend_unified_events_sound_path(o->events,(const char *)content.data,
                (const char *)path.data,id,origin,seconds*1000,channel,volume,1,0,e);
        }
        qa_buffer_free(&content); qa_buffer_free(&path); return okay && current(o,e);
    }
    if (qa_json_string_equal(j,kind,"print") || qa_json_string_equal(j,kind,"log")) {
        okay=text(d,field(j,v,"text"),&b,e);
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        if (okay) qa_console_emit(domain->console,&domain->command_context,(const char *)b.data);
        qa_buffer_free(&b); return okay && current(o,e);
    }
    if (qa_json_string_equal(j,kind,"server-command")) return source_command_event(o,d,row,true,e);
    if (qa_json_string_equal(j,kind,"configstring")) return source_command_event(o,d,row,false,e);
    return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q3 Source event requires the actual retained cgame command/snapshot owner");
}
bool frontend_unified_q3_simulation(frontend_unified_q3 *o, const qa_unified_document *d, qa_json_id row, qa_error *e)
{ return frontend_unified_q3_validate(o,true,d,row,e); }
static bool q3_returned(const frontend_unified_q3 *o)
{
    if (!o || o->busy) return false;
    for (unified_q3_bank *b=o->banks; b; b=b->next)
        if ((b->effects && !q3n_events_idle(b->effects)) || (b->media && !q3n_media_idle(b->media)) ||
            (b->weapons && !q3n_weapons_idle(b->weapons)) || (b->particles && !q3n_particles_idle(b->particles)) ||
            (b->backend && !qa_q3_presentation_idle(b->backend))) return false;
    return true;
}
bool frontend_unified_q3_checkpoint_ready(const frontend_unified_q3 *o)
{ return o && !o->sampled && q3_returned(o); }
bool frontend_unified_q3_idle(const frontend_unified_q3 *o)
{ return o && !o->prepared && q3_returned(o); }
bool frontend_unified_q3_destroy(frontend_unified_q3 **address, qa_error *e)
{
    if (!address || !*address) return true;
    frontend_unified_q3 *o=*address;
    if (!frontend_unified_q3_idle(o)) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 CLIENT children have not returned");
    for(unified_q3_bank *b=o->banks;b;b=b->next)qa_q3_presentation_supplement_release(&b->supplement);
    if(o->components)frontend_unified_components_submission_release(o->components,o->scene_bank);
    qa_q3_source_scene_bank_destroy(o->scene_bank);o->scene_bank=NULL;
    while (o->banks) {
        unified_q3_bank *b=o->banks;
        if (b->backend && !qa_q3_presentation_destroy(b->backend,e)) return false;
        q3n_weapons_destroy(b->weapons); q3n_particles_destroy(b->particles);
        q3n_events_destroy(b->effects); q3n_media_destroy(b->media);
        o->banks=b->next; free(b->content);free(b->activation); free(b);
    }
    while (o->characters) { unified_q3_character *c=o->characters; o->characters=c->next;
        qa_resource_release(c->animation_holder); free(c); }
    while(o->ballistics){unified_q3_ballistic *v=o->ballistics;o->ballistics=v->next;free(v);}
    qa_unified_document_destroy(o->frame); qa_unified_document_destroy(o->candidate);
    free(o->character_order); free(o->lights); free(o); *address=NULL; return true;
}
bool frontend_unified_q3_visit(const frontend_unified_q3 *o,const qa_application_content_visitor *visitor,qa_error *e)
{
    if(!o || !visitor || !visitor->pool || !visitor->view || !q3_returned(o))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 CLIENT resource inventory requires its returned retained graph");
    for(const unified_q3_bank *b=o->banks;b;b=b->next){
        if(!b->files)return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 CLIENT bank construction has not retained its real VFS");
        if(!visitor->pool(visitor->context,qa_vfs_resources(b->files),e) ||
            !visitor->view(visitor->context,b->files,e))return false;
    }
    return true;
}
static bool saved_blob(qa_source_save_io *io,qa_buffer *b)
{
    size_t n=b->size;
    if(!qa_source_save_count(io,&n,SIZE_MAX))return false;
    if(io->direction==QA_SOURCE_SAVE_READ){
        if(io->offset>io->input.size || n>io->input.size-io->offset)return frontend_unified_fail(io->error,QA_ERROR_FORMAT,"Truncated Q3 CLIENT continuation");
        b->data=n?malloc(n):NULL;b->size=n;
        if(n && !b->data)return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Restoring Q3 CLIENT continuation bytes");
    }
    return qa_source_save_bytes(io,b->data,n);
}
static bool saved_text(qa_source_save_io *io,char **value)
{
    bool present=*value!=NULL;
    if(!qa_source_save_bool(io,&present))return false;
    if(!present){if(io->direction==QA_SOURCE_SAVE_READ)*value=NULL;return true;}
    qa_buffer bytes={0};bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if(!reading){bytes.data=(uint8_t *)*value;bytes.size=strlen(*value)+1;}
    bool okay=saved_blob(io,&bytes) && bytes.size && bytes.data[bytes.size-1]==0 &&
        !memchr(bytes.data,0,bytes.size-1);
    if(reading){if(okay)*value=(char *)bytes.data;else qa_buffer_free(&bytes);}
    return okay;
}
static bool saved_actor(qa_source_save_io *io,frontend_unified_q3 *o,qa_actor_id *id)
{
    bool present=id->registry!=0;qa_saved_actor_id wire={0};
    if(!qa_source_save_bool(io,&present))return false;
    if(!present){if(io->direction==QA_SOURCE_SAVE_READ)*id=(qa_actor_id){0};return true;}
    if(io->direction==QA_SOURCE_SAVE_WRITE && !frontend_remote_unified_wire_actor(o->replica,*id,&wire))return false;
    if(!qa_source_save_u32(io,&wire.slot) || !qa_source_save_u64(io,&wire.generation))return false;
    return io->direction!=QA_SOURCE_SAVE_READ || frontend_remote_unified_actor_retained(o->replica,wire.slot,wire.generation,id,io->error);
}
static bool saved_float(qa_source_save_io *io,float *value)
{return qa_source_save_f32(io,value) && isfinite(*value);}
static bool saved_vector(qa_source_save_io *io,qa_vec3 *value)
{return qa_source_save_vec3(io,value) && qa_vec_finite(*value);}
static bool saved_trajectory(qa_source_save_io *io,qa_q3_trajectory *t)
{
    if(!qa_source_save_i32(io,&t->type) || t->type<0 || t->type>5 ||
        !qa_source_save_i32(io,&t->time) || !qa_source_save_i32(io,&t->duration))return false;
    for(unsigned i=0;i<3;++i)if(!saved_float(io,t->base+i) || !saved_float(io,t->delta+i))return false;
    return true;
}
static bool saved_lerp(qa_source_save_io *io,q3n_lerp_frame *v)
{
    return qa_source_save_i32(io,&v->old_frame) && qa_source_save_i32(io,&v->old_frame_time) &&
        qa_source_save_i32(io,&v->frame) && qa_source_save_i32(io,&v->frame_time) && saved_float(io,&v->back_lerp) &&
        qa_source_save_i32(io,&v->animation_number) && qa_source_save_i32(io,&v->animation_time) && qa_source_save_bool(io,&v->selected);
}
static bool saved_pose_part(qa_source_save_io *io,q3n_pose_frame *v)
{
    return saved_lerp(io,&v->animation) && saved_float(io,&v->yaw_angle) && saved_float(io,&v->pitch_angle) &&
        qa_source_save_bool(io,&v->yawing) && qa_source_save_bool(io,&v->pitching);
}
static bool saved_animation(qa_source_save_io *io,qa_player_animation_config *c)
{
    uint32_t feet=(uint32_t)c->footsteps,gender=(uint32_t)c->gender;
    if(!qa_source_save_u32(io,&feet) || feet>QA_FOOTSTEP_ENERGY || !qa_source_save_u32(io,&gender) || gender>QA_MODEL_NEUTER ||
        !qa_source_save_bool(io,&c->fixed_legs) || !qa_source_save_bool(io,&c->fixed_torso))return false;
    c->footsteps=(qa_model_footstep)feet;c->gender=(qa_model_gender)gender;
    for(unsigned i=0;i<3;++i)if(!saved_float(io,c->head_offset+i))return false;
    for(size_t i=0;i<QA_PLAYER_ANIMATION_COUNT;++i){qa_player_animation *v=c->animations+i;
        if(!qa_source_save_i32(io,&v->first_frame) || !qa_source_save_i32(io,&v->num_frames) ||
            !qa_source_save_i32(io,&v->loop_frames) || !qa_source_save_i32(io,&v->frame_lerp) ||
            !qa_source_save_i32(io,&v->initial_lerp) || !qa_source_save_bool(io,&v->reversed) ||
            !qa_source_save_bool(io,&v->flipflop) || !qa_source_save_bool(io,&v->present))return false;
    }
    return true;
}
static unified_q3_bank *bank_at(frontend_unified_q3 *o,size_t ordinal)
{unified_q3_bank *b=o->banks;while(b && ordinal--)b=b->next;return b;}
static bool bank_number(frontend_unified_q3 *o,unified_q3_bank *b,size_t *ordinal)
{*ordinal=0;for(unified_q3_bank *v=o->banks;v;v=v->next,++*ordinal)if(v==b)return true;return false;}
static bool saved_bank_reference(qa_source_save_io *io,frontend_unified_q3 *o,unified_q3_bank **b,size_t count)
{
    size_t ordinal=0;if(io->direction==QA_SOURCE_SAVE_WRITE && !bank_number(o,*b,&ordinal))return false;
    if(!count || !qa_source_save_count(io,&ordinal,count-1))return false;
    if(io->direction==QA_SOURCE_SAVE_READ)*b=bank_at(o,ordinal);
    return *b!=NULL;
}
static bool bank_fields(qa_source_save_io *io,unified_q3_bank *b)
{
    if(!b->assets || !b->media || !b->effects || !b->particles || !b->weapons)return false;
    qa_q3_presentation_assets *a=b->assets;bool own_capture=!a->capturing;
    if(own_capture){if(!qa_q3_assets_capture_begin(a,io->error))return false;}
    else if(a->busy!=1 || a->codec_busy || (!b->owner->frontend->capture && !b->owner->frontend->source_restoring))return false;
    qa_buffer children[4]={{0}};bool writing=io->direction==QA_SOURCE_SAVE_WRITE;
    bool okay=!writing || (q3n_media_checkpoint(b->media,children,io->error) && q3n_events_checkpoint(b->effects,children+1,io->error) &&
        q3n_particles_checkpoint(b->particles,children+2,io->error) && q3n_weapons_checkpoint(b->weapons,children+3,io->error));
    for(unsigned i=0;okay && i<4;++i)okay=saved_blob(io,children+i);
    if(okay && !writing)okay=q3n_media_restore(b->media,(qa_bytes){children[0].data,children[0].size},io->error) &&
        q3n_events_restore(b->effects,(qa_bytes){children[1].data,children[1].size},io->error) &&
        q3n_particles_restore(b->particles,(qa_bytes){children[2].data,children[2].size},io->error) &&
        q3n_weapons_restore(b->weapons,(qa_bytes){children[3].data,children[3].size},io->error);
    for(unsigned i=0;i<4;++i)qa_buffer_free(children+i);
    for(int32_t i=0;okay && i<Q3N_LOCAL_CAPACITY;++i){
        qa_actor_id owner=b->blood_owners[i];
        if(!b->effects->locals[i].active && io->direction==QA_SOURCE_SAVE_WRITE)owner=(qa_actor_id){0};
        okay=saved_actor(io,b->owner,&owner) &&
            (!owner.registry || (b->effects->locals[i].active &&
                b->effects->locals[i].value.type==Q3N_LE_EXPLOSION &&
                b->effects->locals[i].value.ref.custom_shader==q3n_media_read(b->media)->graphics[Q3N_G_BLOOD_EXPLOSION]));
        if(okay && io->direction==QA_SOURCE_SAVE_READ)b->blood_owners[i]=owner;
    }
    if(own_capture)qa_q3_assets_capture_end(a);
    return okay;
}
static bool imported_bank(frontend_unified_q3 *o,unified_q3_bank *b,qa_error *e)
{
    frontend_unified_bank_view view={0};bool found=false;
    for(size_t i=0;i<frontend_unified_media_bank_count(o->media);++i)
        if(frontend_unified_media_bank_read(o->media,i,&view) && !strcmp(view.content,b->content)){found=true;break;}
    if(!found || !view.q3_assets || !view.files || !view.product || view.product->family!=QA_GAME_Q3)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Saved Q3 CLIENT bank was not imported into its actual media dictionary");
    b->owner=o;b->assets=view.q3_assets;b->files=view.files;b->product=view.product;
    bool matched=false;
    if(b->component_recipient){
        const qa_recipe_provider *p=NULL;qa_executable_recipe *recipe=frontend_remote_unified_recipe(o->replica);
        for(size_t i=0;i<qa_executable_recipe_provider_count(recipe);++i){
            const qa_recipe_provider *candidate=qa_executable_recipe_provider(recipe,i);
            if(candidate->source_owner==b->provider && b->activation && !strcmp(candidate->selection.instance,b->activation))p=candidate;
        }
        matched=p && o->components && frontend_unified_components_recipient_current(o->components,b->content,p);
        if(b->generation || b->retired)return false;
    }else matched=provider(o,b->content,b->activation)==b->provider;
    if(!b->provider || (!b->retired && !matched))
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Saved Q3 CLIENT bank lost its actual provider receipt");
    qa_q3_product product=view.product->campaign && !strcmp(view.product->campaign,"missionpack")?QA_Q3_TEAM_ARENA:QA_Q3_ARENA;
    return bank_children(b,product,e);
}
typedef struct saved_scene_registries {
    qa_q3_presentation_assets **values;
    size_t count;
} saved_scene_registries;
static bool scene_assets_encode(void *context,const qa_q3_presentation_assets *assets,uint64_t *id,qa_error *e)
{
    saved_scene_registries *refs=context;
    for(size_t i=0;i<refs->count;++i)if(refs->values[i]==assets){*id=(uint64_t)i+1;return true;}
    return frontend_unified_fail(e,QA_ERROR_FORMAT,"Source scene cell has no retained registry dictionary row");
}
static bool scene_assets_decode(void *context,uint64_t id,qa_q3_presentation_assets **out,qa_error *e)
{
    saved_scene_registries *refs=context;
    if(!id || id>refs->count)return frontend_unified_fail(e,QA_ERROR_FORMAT,"Source scene registry key is outside its dictionary");
    *out=refs->values[(size_t)id-1];return *out!=NULL;
}
static bool saved_scene_bank(qa_source_save_io *io,frontend_unified_q3 *o,size_t banks)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=o->scene_bank!=NULL;
    if(!qa_source_save_bool(io,&present))return false;
    if(!present)return true;
    saved_scene_registries dictionary={.count=reading?0:qa_q3_source_scene_bank_registry_count(o->scene_bank)};
    if(!qa_source_save_count(io,&dictionary.count,reading?io->input.size/12:SIZE_MAX/sizeof(*dictionary.values)))return false;
    if(dictionary.count>SIZE_MAX/sizeof(*dictionary.values))return false;
    dictionary.values=dictionary.count?calloc(dictionary.count,sizeof(*dictionary.values)):NULL;
    if(dictionary.count && !dictionary.values)return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Retaining Source scene registry dictionary");
    bool okay=true;
    for(size_t i=0;okay && i<dictionary.count;++i){
        uint32_t kind=0;uint64_t identity=0;
        if(!reading){
            dictionary.values[i]=qa_q3_source_scene_bank_registry_at(o->scene_bank,i);
            size_t ordinal=0;
            for(unified_q3_bank *b=o->banks;b;b=b->next,++ordinal)if(b->assets==dictionary.values[i]){
                kind=1;identity=(uint64_t)ordinal+1;break;
            }
            if(!kind){kind=2;okay=o->components && frontend_unified_components_assets_encode(o->components,
                dictionary.values[i],&identity,io->error);}
        }
        if(okay)okay=qa_source_save_u32(io,&kind) && qa_source_save_u64(io,&identity) && identity && (kind==1 || kind==2);
        if(okay && reading){
            if(kind==1){unified_q3_bank *b=identity<=banks?bank_at(o,(size_t)identity-1):NULL;
                okay=b && b->assets;if(okay)dictionary.values[i]=b->assets;
            }else okay=o->components && frontend_unified_components_assets_decode(o->components,identity,dictionary.values+i,io->error);
            for(size_t j=0;okay && j<i;++j)if(dictionary.values[j]==dictionary.values[i])okay=false;
        }
    }
    qa_q3_source_scene_bank_refs refs={&dictionary,scene_assets_encode,scene_assets_decode};
    qa_buffer bytes={0};
    if(okay && !reading)okay=qa_q3_source_scene_bank_checkpoint(o->scene_bank,&refs,&bytes,io->error);
    if(okay)okay=saved_blob(io,&bytes);
    if(okay && reading)okay=qa_q3_source_scene_bank_restore((qa_bytes){bytes.data,bytes.size},&refs,&o->scene_bank,io->error);
    if(okay && reading && o->components)
        okay=frontend_unified_components_submission_restore(o->components,o->scene_bank,io->error);
    qa_buffer_free(&bytes);free(dictionary.values);return okay;
}
static bool capsule_fields(qa_source_save_io *io,frontend_unified_q3 *o,const qa_application_content_graph *content)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;uint8_t magic[5]={'Q','U','Q','3','5'};
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    uint32_t epoch=o->epoch,seat=domain->physical_seat;
    if(!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QUQ35",sizeof(magic)) ||
        !qa_source_save_u32(io,&epoch) || epoch!=o->epoch || !qa_source_save_u32(io,&seat) || seat!=domain->physical_seat ||
        !qa_source_save_u64(io,&o->audio_owner) || !qa_source_save_bool(io,&o->has_frame) ||
        !qa_source_save_i32(io,&o->time) || !qa_source_save_i32(io,&o->previous_time))return false;
    if(o->has_frame){qa_buffer bytes={0};bool okay=reading || qa_unified_document_encode(o->frame,&bytes,io->error);
        if(okay)okay=saved_blob(io,&bytes);
        if(okay && reading)okay=qa_unified_document_decode(QA_UNIFIED_FRAME_DOCUMENT,(qa_bytes){bytes.data,bytes.size},&o->frame,io->error);
        qa_buffer_free(&bytes);int32_t time;if(!okay || !frame_read(o,o->frame,&time,io->error) || time!=o->time)return false;}
    else if(o->time || o->previous_time)return false;
    if(!qa_source_save_bool(io,&o->prepared))return false;
    if(o->prepared){qa_buffer bytes={0};
        if(!qa_source_save_i32(io,&o->candidate_time))return false;
        bool okay=reading || (o->candidate && qa_unified_document_encode(o->candidate,&bytes,io->error));
        if(okay)okay=saved_blob(io,&bytes);
        if(okay && reading)okay=qa_unified_document_decode(QA_UNIFIED_FRAME_DOCUMENT,(qa_bytes){bytes.data,bytes.size},&o->candidate,io->error);
        qa_buffer_free(&bytes);int32_t time;
        if(!okay || !frame_read(o,o->candidate,&time,io->error) || time!=o->candidate_time)return false;
    }
    size_t banks=0;for(unified_q3_bank *b=o->banks;b;b=b->next)++banks;
    if(!qa_source_save_count(io,&banks,SIZE_MAX/sizeof(unified_q3_bank)))return false;
    unified_q3_bank **tail=&o->banks,*b=o->banks;
    for(size_t i=0;i<banks;++i){
        if(reading){b=calloc(1,sizeof(*b));if(!b)return false;*tail=b;tail=&b->next;}
        if(!saved_text(io,&b->content) || !b->content || !*b->content || !saved_text(io,&b->activation) ||
            !qa_source_save_u64(io,&b->generation) || !qa_source_save_bool(io,&b->retired) || !qa_source_save_bool(io,&b->component_recipient) || !qa_source_save_u32(io,&b->provider) ||
            (b->generation && (!b->activation || !*b->activation)) ||
            (reading && !imported_bank(o,b,io->error)) || !bank_fields(io,b))return false;
        b=b->next;
    }
    size_t chars=0;for(unified_q3_character *c=o->characters;c;c=c->next)++chars;
    if(!qa_source_save_count(io,&chars,SIZE_MAX/sizeof(unified_q3_character)))return false;
    unified_q3_character **ctail=&o->characters,*c=o->characters;
    for(size_t i=0;i<chars;++i){
        if(reading){c=calloc(1,sizeof(*c));if(!c)return false;*ctail=c;ctail=&c->next;}
        if(!saved_bank_reference(io,o,&c->bank,banks) || !saved_actor(io,o,&c->actor) || !c->actor.registry ||
            !saved_pose_part(io,&c->pose.legs) || !saved_pose_part(io,&c->pose.torso) || !qa_source_save_i32(io,&c->pose.pain_time) ||
            !qa_source_save_bool(io,&c->pose.pain_direction) || !saved_animation(io,&c->animation))return false;
        uint64_t pool=0,resource=0;
        if(!reading && c->animation_holder && !qa_application_content_resource_id(content,c->animation_holder,&pool,&resource))return false;
        if(!qa_source_save_u64(io,&pool) || !qa_source_save_u64(io,&resource) || (!!pool!=!!resource))return false;
        if(reading && resource){const qa_resource *r=qa_application_content_resource(content,pool,resource);
            if(!r || qa_resource_pool_find(qa_vfs_resources(c->bank->files),qa_resource_id(r))!=r)return false;
            c->animation_holder=(qa_resource *)r;qa_resource_retain(c->animation_holder);}
        for(unsigned k=0;k<3;++k){if(!qa_source_save_i32(io,c->models+k) || !qa_source_save_i32(io,c->skins+k))return false;
            int32_t model=c->models[k],skin=c->skins[k];
            if(model<0 || skin<0 || (model && ((size_t)model>c->bank->assets->model_count || !c->bank->assets->models[model-1])) ||
                (skin && ((size_t)skin>c->bank->assets->skin_count || !c->bank->assets->skins[skin-1])))return false;}
        if(!saved_vector(io,&c->origin) || !saved_vector(io,&c->angles) || !saved_vector(io,&c->velocity) ||
            !qa_source_save_i32(io,&c->movement) || !qa_source_save_i32(io,&c->legs) || !qa_source_save_i32(io,&c->torso) ||
            !qa_source_save_i32(io,&c->time) || !qa_source_save_i32(io,&c->team) || c->team<0 || c->team>2 || !qa_source_save_u32(io,&c->flags) || !qa_source_save_u32(io,&c->powerups) ||
            !saved_float(io,&c->scale) || !saved_float(io,&c->opacity) || !qa_source_save_bool(io,&c->visible) || !qa_source_save_bool(io,&c->reset))return false;
        for(unsigned k=0;k<4;++k)if(!saved_float(io,c->color+k))return false;
        c=c->next;
    }
    size_t shots=0;for(unified_q3_ballistic *v=o->ballistics;v;v=v->next)++shots;
    if(!qa_source_save_count(io,&shots,SIZE_MAX/sizeof(unified_q3_ballistic)))return false;
    unified_q3_ballistic **stail=&o->ballistics,*v=o->ballistics;
    for(size_t i=0;i<shots;++i){
        if(reading){v=calloc(1,sizeof(*v));if(!v)return false;*stail=v;stail=&v->next;}
        if(!saved_bank_reference(io,o,&v->bank,banks) || !saved_actor(io,o,&v->actor) || !v->actor.registry ||
            !qa_source_save_i32(io,&v->weapon) || v->weapon<0 || v->weapon>13 || !qa_source_save_i32(io,&v->time) ||
            !qa_source_save_i32(io,&v->fire_time) || !qa_source_save_i32(io,&v->last_fire_time) || !qa_source_save_i32(io,&v->last_fire_weapon) ||
            !qa_source_save_i32(io,&v->bolt_time) || !qa_source_save_i32(io,&v->bolt_weapon) ||
            !saved_vector(io,&v->origin) || !saved_vector(io,&v->end) || !saved_vector(io,&v->flash_origin) || !saved_vector(io,&v->flash_end) ||
            !saved_vector(io,&v->bolt_origin) || !saved_vector(io,&v->bolt_end) || !saved_trajectory(io,&v->trajectory) ||
            !qa_source_save_bool(io,&v->projectile) || !qa_source_save_bool(io,&v->flash) || !qa_source_save_bool(io,&v->bolt) || !qa_source_save_bool(io,&v->last_fire) ||
            v->last_fire_weapon<0 || v->last_fire_weapon>13 || v->bolt_weapon<0 || v->bolt_weapon>13)return false;
        v=v->next;
    }
    return saved_scene_bank(io,o,banks);
}
bool frontend_unified_q3_checkpoint(frontend_unified_q3 *o,const qa_application_content_graph *content,qa_buffer *out,qa_error *e)
{
    if(!o || !content || !out || out->data || out->size || !frontend_unified_q3_checkpoint_ready(o) || !retained_current(o,e))return false;
    o->busy=true;qa_source_save_io io={0};bool okay=qa_source_save_writer(&io,NULL,e) && capsule_fields(&io,o,content) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);o->busy=false;
    if(!okay && e && e->code==QA_OK)frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 CLIENT cold continuation is inconsistent");
    return okay;
}
bool frontend_unified_q3_restore(qa_frontend *f,frontend_remote_unified *replica,frontend_unified_media *media,
    frontend_unified_components *components,const qa_application_content_graph *content,qa_bytes bytes,frontend_unified_q3 **out,qa_error *e)
{
    if(!f || !content || !f->source_restoring || !out || *out)return false;
    frontend_unified_q3 *o=NULL;if(!frontend_unified_q3_create(f,replica,media,&o,e))return false;
    if(components && !frontend_unified_q3_components(o,components,e)){
        *out=o;return false;
    }
    qa_source_save_io io={0};bool okay=qa_source_save_reader(&io,NULL,bytes,e) && capsule_fields(&io,o,content) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(okay)okay=retained_current(o,e);
    if(!okay){if(e && e->code==QA_OK)frontend_unified_fail(e,QA_ERROR_FORMAT,"Invalid saved Q3 CLIENT continuation");
        qa_error cleanup={0};frontend_unified_q3_frame_abort(o);if(!frontend_unified_q3_destroy(&o,&cleanup))*out=o;
        return false;}
    *out=o;return true;
}

bool frontend_unified_q3_frame_restore_bind(frontend_unified_q3 *o,const qa_unified_document *d,qa_error *e)
{
    if(!o || !o->frontend->source_restoring || !frontend_unified_q3_checkpoint_ready(o) || !retained_current(o,e))return false;
    if(!o->prepared)return d==o->replica->prepared_frame;
    if(!d || !o->candidate || qa_unified_document_type(d)!=QA_UNIFIED_FRAME_DOCUMENT)return false;
    qa_buffer a={0},b={0};int32_t time;
    bool ok=qa_unified_document_encode(o->candidate,&a,e) && qa_unified_document_encode(d,&b,e) &&
        a.size==b.size && (!a.size || !memcmp(a.data,b.data,a.size)) && frame_read(o,d,&time,e) && time==o->candidate_time;
    qa_buffer_free(&a);qa_buffer_free(&b);if(!ok)return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 restored prepared frame differs from its actual parent");
    o->candidate_input=d;return true;
}
