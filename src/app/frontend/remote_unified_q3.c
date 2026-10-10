#include "remote_unified_private.h"
#include "remote_unified_metadata.h"
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
#include "../../presentation/q3_native/body.h"
#include "../../presentation/q3_native/attachments.h"
#include "../../presentation/q3_native/particles.h"
#include "../../presentation/q3_native/events_internal.h"
#include "../../presentation/q3_native/trajectory.h"
#include "../../presentation/q3_native/selected_media.h"
#include "../../presentation/q3/internal.h"
#include "qa/hud.h"
#include "qa/pool.h"
#include "qa/vfs_view_save.h"
#include "qa/scene_marks.h"
#include "qa/scene_world_save.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_presentation_save.h"
#include "qa/unified_frame_visuals.h"
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
    q3n_selected_media *selected_media;
    q3n_clients *clients;
    uint64_t character_publication, character_map_revision;
    q3n_particles *particles;
    q3n_unified_effect_source source;
    qa_actor_id blood_owners[Q3N_LOCAL_CAPACITY];
} unified_q3_bank;
typedef struct unified_q3_character {
    struct unified_q3_character *next;
    qa_actor_id actor;
    unified_q3_bank *bank;
    q3n_player_pose pose;
    q3n_client_info client;
    qa_q3_entity source;
    uint64_t media_revision;
    qa_vec3 origin, angles, velocity;
    int32_t movement, legs, torso, time, team;
    uint32_t flags, powerups;
    float color[4], scale, opacity;
    bool visible, reset;
    qa_q3_ref_entity submitted_parts[3];
    bool submitted, hidden, weapon_submitted;
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
    q3n_selected_weapon_state view_state;
    q3n_selected_weapon_attachment *attachments;
    size_t attachment_capacity;
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
    qa_arena character_storage;
    qa_pool character_records;
    unified_q3_character **characters_by_slot;
    unified_q3_character **character_order;
    size_t character_count;
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
    qa_scene_light lights[QA_Q3_SOURCE_LIGHT_CAPACITY];
    size_t light_count;
    qa_scene_view sampled_view;
    qa_scene_frame *sampled_scene;
    uint64_t sampled_frame_number;
    bool sampled;
    bool world_submitted;
    qa_ui_preferences preferences;
    bool busy, prepared, has_frame;
};
static bool actor(frontend_unified_q3 *o,qa_actor_id wire,qa_actor_id *out,qa_error *e)
{
    return frontend_remote_unified_source_actor(o->replica,
        qa_unified_document_frame(frontend_remote_unified_frame(o->replica)),wire,false,out,e);
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
        .policy={.family=QA_COLLISION_Q3,.contents_mask=qa_collision_contents_mask(mask,QA_COLLISION_Q3),.curves=true},.pass_actor=b->source.actor};
    qa_collision_geometry *g = (qa_collision_geometry *)frontend_remote_unified_geometry(b->owner->replica);
    return g && qa_collision_trace(g,frontend_remote_unified_presentation_trace_scratch(b->owner->replica),&q,out,e) && effect_current(&b->source);
}
static bool contents(void *context, const q3n_frame *f, qa_vec3 point, int32_t pass, uint32_t *out, qa_error *e)
{
    unified_q3_bank *b = context; (void)pass;
    if (f->unified_effects != &b->source || !effect_current(&b->source)) return false;
    qa_point_contents result; qa_point_query q = {.point=point,
        .policy={.family=QA_COLLISION_Q3,.curves=true},.pass_actor=b->source.actor};
    qa_collision_geometry *g = (qa_collision_geometry *)frontend_remote_unified_geometry(b->owner->replica);
    if (!g || !qa_collision_point_contents(g,frontend_remote_unified_presentation_trace_scratch(b->owner->replica),&q,&result,e) || !effect_current(&b->source)) return false;
    *out=(uint32_t)qa_collision_point_contents_export(result.contents,QA_COLLISION_Q3,result.q1_opaque_token); return true;
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
    qa_scene_world_input world={0}; frontend_unified_media_world_scratch(o->media,&world);
    options.world_scratch=world.scratch; options.world_child_scratch=world.child_scratch;
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
    const qa_unified_frame *frame=qa_unified_document_frame(d);
    if(!frame||!frame->world||frame->epoch!=o->epoch) {
        frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified Q3 clock lost its actual typed Source frame");
        return false;
    }
    uint32_t word=(uint32_t)qa_unified_world_frame_milliseconds(frame->world);
    memcpy(time,&word,sizeof(word)); return true;
}
bool frontend_unified_q3_create(qa_frontend *f, frontend_remote_unified *r,
    frontend_unified_media *m, frontend_unified_q3 **out, qa_error *e)
{
    if (!f || !r || !m || !out || *out || !frontend_remote_unified_domain_read(r))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 CLIENT requires its actual replica and media owners");
    frontend_unified_q3 *o=calloc(1,sizeof(*o));
    if (!o) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Allocating persistent Unified Q3 CLIENT");
    o->frontend=f; o->replica=r; o->media=m; o->epoch=frontend_remote_unified_epoch(r);
    size_t capacity=qa_actors_capacity(frontend_remote_unified_registry(r));
    size_t stride=sizeof(unified_q3_character)+2*sizeof(size_t);
    if(capacity>(SIZE_MAX-32)/stride){free(o);return frontend_unified_fail(e,QA_ERROR_MEMORY,"Q3 character reservation overflows storage");}
    qa_arena_init(&o->character_storage,0);
    bool okay=qa_arena_reserve(&o->character_storage,capacity*stride+32,e) &&
        qa_pool_prepare(&o->character_records,&o->character_storage,capacity,sizeof(unified_q3_character),_Alignof(unified_q3_character),e);
    if(okay){
        o->characters_by_slot=qa_arena_alloc(&o->character_storage,capacity*sizeof(*o->characters_by_slot),_Alignof(unified_q3_character *),e);
        okay=o->characters_by_slot!=NULL;
    }
    if(okay){memset(o->characters_by_slot,0,capacity*sizeof(*o->characters_by_slot));qa_arena_seal(&o->character_storage);}
    if(!okay || !retained_current(o,e)){qa_arena_destroy(&o->character_storage);free(o);return false;}
    *out=o;return true;
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
bool frontend_unified_q3_owner_validate(frontend_unified_q3 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    return o&&row&&current(o,e)&&(row->payload.kind==QA_UNIFIED_PRESENTATION_OWNER||
        frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 owner retirement lacks its actual tagged record"));
}
bool frontend_unified_q3_owner_retire(frontend_unified_q3 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    bool okay=frontend_unified_q3_owner_validate(o,row,e)&&frontend_unified_q3_idle(o);
    const qa_unified_owner_event *v=row?&row->payload.value.owner:NULL;
    if(okay&&v->kind==QA_UNIFIED_OWNER_RETIRED)for(unified_q3_bank *b=o->banks;b;b=b->next)
        if(b->activation&&!strcmp(b->activation,v->owner.provider)&&b->generation==v->owner.generation&&!b->retired){
            for(unified_q3_ballistic *state=o->ballistics;state;state=state->next)if(state->bank==b){
                if(o->events&&(state->bolt||state->projectile)&&!frontend_unified_events_sound_stop_loop(o->events,state->actor,e)){okay=false;break;}
                state->projectile=state->flash=state->bolt=state->last_fire=false;}
            if(!okay)break;
            q3n_events_round(b->effects);q3n_particles_round(b->particles,o->time);b->retired=true;
        }
    return okay;
}
bool frontend_unified_q3_frame_prepare(frontend_unified_q3 *o, const qa_unified_document *d, qa_error *e)
{
    if (!o || o->busy || o->prepared || !current(o,e)) return false;
    if (!frame_read(o,d,&o->candidate_time,e) || !qa_unified_document_retain(d,&o->candidate,e)) return false;
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
    if(!qa_ui_preferences_read(qa_application_cvars(o->frontend->application),qa_application_ui_preference_handles(o->frontend->application),d->physical_seat,&o->preferences,e) ||
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
static unified_q3_ballistic *ballistic_find(frontend_unified_q3 *o,unified_q3_bank *b,qa_actor_id id)
{ for(unified_q3_ballistic *v=o->ballistics;v;v=v->next)if(v->bank==b && qa_actor_id_equal(v->actor,id))return v;return NULL; }
typedef struct selected_weapon_call {
    frontend_unified_q3 *owner;
    unified_q3_bank *bank;
    qa_q3_scene_options options;
    qa_scene_frame *frame;
    int32_t time;
    uint32_t order;
} selected_weapon_call;
static bool selected_weapon_current(void *context)
{ return frontend_unified_q3_current(((selected_weapon_call *)context)->owner); }
static bool selected_weapon_submit(void *context,qa_q3_presentation_assets *assets,
    const qa_q3_ref_entity *ref,qa_error *e)
{
    selected_weapon_call *call=context;
    return qa_q3_presentation_completed_entity(call->bank->backend,assets,
        frontend_unified_media_world(call->owner->media),ref,call->time,&call->options,
        call->order++,call->frame,e);
}
bool frontend_unified_q3_selected_weapon(frontend_unified_q3 *o,const qa_unified_model_state *model,
    const qa_scene_world_input *world,qa_scene_frame *scene,bool *submitted,qa_error *e)
{
    *submitted=false;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    const qa_cvar_view *draw_gun=qa_cvars_read(qa_application_cvars(o->frontend->application),
        o->frontend->engine_cvars.hud.draw_gun);
    if ((draw_gun && !draw_gun->integer) || world->view.clip_enabled ||
        o->frontend->seats[domain->physical_seat].q1_chase) return true;
    qa_actor_id id;
    if (!actor(o,model->actor,&id,e) || !enter(o,id,e)) return false;
    unified_q3_bank *b=NULL;
    bool okay=bank_read(o,model->content,model->render_equipment->instance,0,&b,e);
    qa_q3_product product=okay?b->source.product:QA_Q3_ARENA;
    if (okay && !b->selected_media) {
        q3n_selected_media_options options={.content=b->files,.assets=b->assets,.product=product};
        okay=q3n_selected_media_create(&options,&b->selected_media,e);
    }
    unified_q3_ballistic *state=okay?ballistic_find(o,b,id):NULL;
    if (okay && !state) {
        state=calloc(1,sizeof(*state));
        if (!state) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining selected weapon animation");
        else {state->actor=id;state->bank=b;state->next=o->ballistics;o->ballistics=state;}
    }
    const qa_unified_q3_weapon_view *wire=model->q3_weapon;
    selected_weapon_call call={.owner=o,.bank=b,.frame=scene,.time=wire->time_ms};
    qa_scene_world_options recipient;
    if (okay) okay=qa_scene_world_options_read(frontend_unified_media_world(o->media),&recipient) &&
        backend_read(b,world->view.viewport,e) && qa_q3_presentation_frame(b->backend,scene,world->view.viewport,e);
    if (okay) {
        call.options=(qa_q3_scene_options){.world=*world,.world_family=recipient.images.family,
            .ambient_scale=.6f,.directed_scale=1,.near_clip=4,.lod_scale=5,
            .split_screen=o->frontend->options.seats>1};
        qa_scene_state_default(&call.options.state);
        qa_q3_player player={.product=product,.weapon=wire->weapon,.torsoAnim=wire->torso_animation};
        q3n_selected_weapon_draw request={.player=&player,.time=wire->time_ms,
            .presentation_weapon=wire->weapon,.has_last_fire=wire->has_last_fire_ms,
            .last_fire=wire->last_fire_ms,.firing=wire->firing,
            .reduced_flashes=o->preferences.reduced_flashes,.context=&call,
            .current=selected_weapon_current,.submit=selected_weapon_submit};
        q3n_selected_weapon_view camera={.origin=world->view.origin,
            .angles=qa_axes_angles(world->view.axis),.horizontal_speed=wire->horizontal_speed,
            .field_of_view=atanf(1/world->view.projection.m[0])*114.59155902616464f,
            .bob_cycle=wire->bob_cycle,.draw_gun=true};
        qa_cvars *cvars=qa_application_cvars(o->frontend->application);
        const frontend_engine_cvar_handles *refs=&o->frontend->engine_cvars;
        const qa_cvar_view *x=qa_cvars_read(cvars,refs->cg_gunX),*y=qa_cvars_read(cvars,refs->cg_gunY),
            *z=qa_cvars_read(cvars,refs->cg_gunZ),*gun_frame=qa_cvars_read(cvars,refs->cg_gun_frame);
        camera.gun_offset=qa_v3(x?(float)x->number:0,y?(float)y->number:0,z?(float)z->number:0);
        camera.gun_frame=gun_frame?gun_frame->integer:0;
        q3n_selected_weapon_media media;
        if (!model->anchor) {
            q3n_selected_media_request load={.weapon=wire->weapon,.view_required=true,
                .context=&call,.current=selected_weapon_current};
            q3n_selected_animation animation;bool character;
            okay=q3n_selected_media_prepare(b->selected_media,&load,&media,e) &&
                q3n_selected_media_animation(b->selected_media,&animation,&character,e);
            if (okay) {camera.animations=animation.config;
                okay=q3n_weapons_selected_view(b->weapons,&media,&state->view_state,&request,&camera,submitted,e);}
        } else {
            media=(q3n_selected_weapon_media){.assets=b->assets};
            okay=qa_q3_register_model(b->assets,model->path,&media.gun,e) &&
                qa_q3_register_model(b->assets,model->anchor->path,&media.hands,e);
            if (okay && model->attachment_count>state->attachment_capacity) {
                q3n_selected_weapon_attachment *attachments=realloc(state->attachments,
                    model->attachment_count*sizeof(*attachments));
                if (!attachments) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining selected weapon attachments");
                else {state->attachments=attachments;state->attachment_capacity=model->attachment_count;}
            }
            for (size_t i=0;okay && i<model->attachment_count;++i) {
                state->attachments[i].tag=model->attachments[i].tag;
                okay=qa_q3_register_model(b->assets,model->attachments[i].path,&state->attachments[i].model,e);
            }
            q3n_selected_weapon_authored_view authored={.camera=camera,.anchor_tag=model->anchor->tag,
                .anchor_offset=model->anchor->offset,
                .field_of_view=camera.field_of_view,
                .fov_above=model->anchor->fov_above,.fov_scale=model->anchor->fov_scale,
                .frame=(int32_t)model->visual.frame,.old_frame=(int32_t)model->visual.old_frame,.back_lerp=model->back_lerp,
                .attachments=state->attachments,.attachment_count=model->attachment_count};
            if (okay) okay=q3n_weapons_selected_authored_view(b->weapons,&media,&state->view_state,
                &request,&authored,submitted,e);
        }
    }
    return leave(o,okay,e);
}
static bool ballistic_sound(frontend_unified_q3 *o,unified_q3_bank *b,int32_t handle,qa_actor_id id,
    qa_vec3 origin,int32_t time,int32_t channel,float volume,qa_error *e)
{
    qa_audio_asset *asset=q3p_sound(b->assets,handle);
    return !asset || (o->events && frontend_unified_events_sound_path(o->events,b->content,
        qa_audio_asset_name(asset),id,origin,time,channel,volume,1,0,e) && effect_current(&b->source));
}
static bool ballistic_event_apply(frontend_unified_q3 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    qa_unified_q3_ballistic_event v=row->payload.value.q3_ballistic; unified_q3_bank *b=NULL;
    const char *content=row->content,*instance=row->owner.provider; uint64_t generation=row->owner.generation;
    bool okay=actor(o,v.actor,&v.actor,e)&&actor(o,v.target,&v.target,e)&&enter(o,v.actor,e);
    if(okay && v.kind==QA_UNIFIED_Q3_REMOVE){
        for(unified_q3_bank *existing=o->banks;existing;existing=existing->next){
            if(!existing->content || strcmp(existing->content,content) ||
                (instance && (!existing->activation || strcmp(existing->activation,instance) || existing->generation!=generation)))continue;
            unified_q3_ballistic *state=ballistic_find(o,existing,v.actor);
            if(state){state->projectile=false;state->bolt=false;}
        }
        return leave(o,true,e);
    }
    if(okay)okay=bank_read(o,content,instance,generation,&b,e);
    unified_q3_ballistic *state=okay?ballistic_find(o,b,v.actor):NULL;
    if(okay){
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        okay=backend_read(b,frontend_viewport(o->frontend,domain->physical_seat),e);
        q3n_frame f=effect_frame(o,b,v.time_ms);f.weapon_settings=&weapon_settings;f.event_settings=&effect_settings;
        if(okay)okay=q3n_media_load_unified_effects(b->media,&b->source,e) &&
            q3n_particles_load_unified(b->particles,&f,e) &&
            q3n_media_register_weapon(b->media,(uint32_t)v.weapon,e) && effect_current(&b->source);
        if(okay && !state){state=calloc(1,sizeof(*state));
            if(!state)okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual Q3 full actor ballistics");
            else{state->actor=v.actor;state->bank=b;state->next=o->ballistics;o->ballistics=state;}}
        const q3n_media_view *m=okay?q3n_media_read(b->media):NULL;
        const q3n_weapon_media *w=okay?&m->weapons[v.weapon]:NULL;
        if(okay)switch(v.kind){
        case QA_UNIFIED_Q3_FIRE:{
            bool silent=v.weapon==6 && state->last_fire && state->last_fire_weapon==v.weapon && q3ne_sub(v.time_ms,state->last_fire_time)<=50;
            state->last_fire=true;state->last_fire_time=v.time_ms;state->last_fire_weapon=v.weapon;
            state->flash=true;state->fire_time=v.time_ms;state->flash_origin=v.origin;state->flash_end=v.end;state->weapon=v.weapon;
            int32_t sounds[4];size_t n=0;for(unsigned i=0;i<4;++i)if(w->flash_sounds[i])sounds[n++]=w->flash_sounds[i];
            if(!silent && n)okay=ballistic_sound(o,b,sounds[(uint32_t)q3n_events_rand(b->effects)%n],v.actor,v.origin,v.time_ms,2,v.volume,e);
            break;}
        case QA_UNIFIED_Q3_PROJECTILE:{
            bool previous=state->projectile;int32_t prior=state->time;
            state->projectile=true;state->weapon=v.weapon;state->trajectory=v.trajectory;state->origin=v.origin;state->end=v.end;state->time=v.time_ms;
            if(previous && w->trail==Q3N_TRAIL_PLASMA)okay=q3n_weapons_effect_plasma(&f,v.weapon,v.end,e);
            else if(previous && w->trail!=Q3N_TRAIL_NONE && w->trail!=Q3N_TRAIL_GRAPPLE && v.trajectory.type){
                qa_vec3 origin,old;uint32_t contents_now,contents_old;
                okay=q3n_trajectory(&v.trajectory,v.time_ms,&origin,e) && q3n_trajectory(&v.trajectory,prior,&old,e) &&
                    q3n_events_point_contents(&f,origin,-1,&contents_now,e) && q3n_events_point_contents(&f,old,-1,&contents_old,e);
                if(okay && (contents_now&(8|16|32))){if(contents_now&contents_old&32)okay=q3n_effect_bubbles(&f,old,origin,8,e);}
                else if(okay){int32_t tick=q3ne_word((uint32_t)(q3ne_plus(prior,50)/50)*UINT32_C(50));
                    for(;okay && tick<=v.time_ms;){qa_vec3 point;okay=q3n_trajectory(&v.trajectory,tick,&point,e);
                        if(okay){q3n_smoke smoke={.origin=point,.radius=w->trail_radius,.color={1,1,1,.33f},.duration=(float)w->trail_time,
                            .start_time=tick,.shader=m->graphics[w->trail==Q3N_TRAIL_NAIL?Q3N_G_NAIL_PUFF:Q3N_G_SMOKE_PUFF]};
                            q3n_effect_smoke(&f,&smoke)->type=Q3N_LE_SCALE_FADE;}
                        if(tick>INT32_MAX-50)break;
                        tick=q3ne_plus(tick,50);}}
            }break;}
        case QA_UNIFIED_Q3_BOUNCE:okay=ballistic_sound(o,b,m->sounds[(q3n_events_rand(b->effects)&1)?Q3N_S_GRENADE_BOUNCE2:Q3N_S_GRENADE_BOUNCE1],(qa_actor_id){0},v.end,v.time_ms,0,1,e);break;
        case QA_UNIFIED_Q3_SHOTGUN:okay=q3n_weapons_effect_shotgun(&f,v.start,v.direction,v.seed,e);break;
        case QA_UNIFIED_Q3_RAIL:
            okay=q3n_weapons_effect_rail(&f,qa_v3(1,1,1),qa_v3(1,1,1),&v.start,v.end,e);
            if(okay && v.rail_surface){float best=0;int32_t byte=0;for(int32_t i=0;i<162;++i){float dot=qa_vec_dot(v.normal,q3n_events_direction(i));if(dot>best){best=dot;byte=i;}}
                okay=q3n_weapons_impact(&f,7,0,v.end,q3n_events_direction(byte),Q3N_IMPACT_DEFAULT,e);}break;
        case QA_UNIFIED_Q3_CONTACT:
            if(v.contact==0)okay=ballistic_sound(o,b,m->sounds[Q3N_S_QUAD],(qa_actor_id){0},v.origin,v.time_ms,4,1,e);
            else if(v.contact==1)bleed(&f,b,v.point,v.target);
            else if(v.contact==2)okay=q3n_weapons_impact(&f,0,0,v.point,v.normal,Q3N_IMPACT_DEFAULT,e);
            else okay=frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Mission reflection requires its genuine compiled Source presentation binding");
            break;
        case QA_UNIFIED_Q3_TRAIL:
            if(v.weapon==6 || v.weapon==10){state->bolt=v.weapon!=10 || qa_vec_length(qa_vec_sub(v.end,v.origin))>=64;
                state->bolt_time=v.time_ms;state->bolt_weapon=v.weapon;state->bolt_origin=v.origin;state->bolt_end=v.end;}
            if(v.weapon==7)okay=q3n_weapons_effect_rail(&f,qa_v3(1,1,1),qa_v3(1,1,1),&v.origin,v.end,e);
            break;
        case QA_UNIFIED_Q3_IMPACT:
            state->projectile=false;
            if(v.surface&16)break;
            if(v.flesh){if(v.target.registry)bleed(&f,b,v.end,v.target);
                if(v.weapon!=4 && v.weapon!=5 && !(b->source.product==QA_Q3_TEAM_ARENA && (v.weapon==11 || v.weapon==12 || v.weapon==13)))break;}
            okay=q3n_weapons_impact(&f,v.weapon,0,v.end,v.normal,v.flesh?Q3N_IMPACT_FLESH:(v.surface&4096)?Q3N_IMPACT_METAL:Q3N_IMPACT_DEFAULT,e);break;
        case QA_UNIFIED_Q3_RAIL_AWARD:okay=frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Rail award requires its genuine compiled Source presentation binding");break;
        default:break;
        }
    }
    return o->busy?leave(o,okay,e):okay;
}
bool frontend_unified_q3_presentation_validate(frontend_unified_q3 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    if(!o||!row||!current(o,e))return false;
    const qa_product *product=NULL;
    qa_catalog *catalog=qa_executable_recipe_catalog(frontend_remote_unified_recipe(o->replica));
    for(size_t i=0;!product&&i<qa_catalog_count(catalog);++i){
        const qa_product *p=qa_catalog_at(catalog,i);if(!strcmp(p->identity,row->content))product=p;
    }
    if(!product||product->family!=QA_GAME_Q3)return false;
    qa_actor_id id;
    if(row->payload.kind==QA_UNIFIED_PRESENTATION_Q3_CHARACTER)
        return actor(o,row->payload.value.q3_character.actor,&id,e);
    if(row->payload.kind==QA_UNIFIED_PRESENTATION_Q3_BALLISTIC){
        const qa_unified_q3_ballistic_event *v=&row->payload.value.q3_ballistic;
        return actor(o,v->actor,&id,e)&&actor(o,v->target,&id,e);
    }
    if(row->payload.kind!=QA_UNIFIED_PRESENTATION_Q3)
        return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 CLIENT received another event family");
    const qa_unified_q3_event *v=&row->payload.value.q3;
    switch(v->kind){
    case QA_UNIFIED_Q3_SOUND: case QA_UNIFIED_Q3_PLAYER_EVENT: case QA_UNIFIED_Q3_ENTITY_EVENT:
        return actor(o,v->actor,&id,e);
    case QA_UNIFIED_Q3_SERVER_COMMAND: case QA_UNIFIED_Q3_CONFIGSTRING:
        return row->provider||frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 Source event has no actual emitter stamp");
    case QA_UNIFIED_Q3_PRINT: case QA_UNIFIED_Q3_LOG: case QA_UNIFIED_Q3_CONSOLE_COMMAND: case QA_UNIFIED_Q3_DROP_CLIENT:
        return true;
    }
    return frontend_unified_fail(e,QA_ERROR_FORMAT,"Q3 CLIENT received an unknown source event variant");
}
static bool character_event(frontend_unified_q3 *o,const qa_unified_presentation_event *row,qa_error *e)
{
    const qa_unified_q3_character_event *v=&row->payload.value.q3_character;
    qa_actor_id id;
    if(!actor(o,v->actor,&id,e))return false;
    int32_t time=v->time_ms,event=v->event&~0x300; qa_vec3 origin={0}; bool found=false;
    const qa_unified_frame *received=qa_unified_document_frame(o->frame);
    const qa_unified_frame_visuals *visuals=received->visuals;
    for (size_t i=0;visuals && i<visuals->character_count;++i) {
        const qa_unified_character_state *c=visuals->characters+i; qa_actor_id actual;
        if (!frontend_remote_unified_source_actor(o->replica,received,c->actor,false,&actual,e)) return false;
        if (qa_actor_id_equal(actual,id)) { origin=c->origin; found=true; break; }
    }
    for(size_t i=0;!found && i<received->world->body_count;++i){
        const qa_unified_body_state *body=received->world->bodies+i; qa_actor_id actual;
        if(!frontend_remote_unified_source_actor(o->replica,received,body->actor,false,&actual,e))return false;
        if(qa_actor_id_equal(actual,id)){ origin=body->body.origin; found=true; }
    }
    if (!found) return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"Q3 character event has no current full actor body");
    /* Events with separate sound records do not create a second voice. */
    if ((event>=Q3N_EV_NONE && event<=Q3N_EV_FALL_FAR) || (event>=Q3N_EV_JUMP && event<=Q3N_EV_WATER_CLEAR) ||
        event==Q3N_EV_NOAMMO || event==Q3N_EV_CHANGE_WEAPON || (event>=Q3N_EV_PAIN && event<=Q3N_EV_OBITUARY) ||
        event==Q3N_EV_STOP_LOOP || event==Q3N_EV_TAUNT) return true;
    if (event!=Q3N_EV_JUMP_PAD && event!=Q3N_EV_TELEPORT_IN && event!=Q3N_EV_TELEPORT_OUT && event!=Q3N_EV_GIB)
        return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q3 character event requires actual cgame snapshot context");
    unified_q3_bank *b=NULL;
    bool okay=enter(o,id,e);
    if (okay) okay=bank_read(o,row->content,row->owner.provider,row->owner.generation,&b,e);
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
     return o->busy?leave(o,okay,e):okay;
}
static bool character_source(frontend_unified_q3 *o, const qa_unified_character_state *row,
    qa_actor_id id, frontend_unified_q3_source_view *out, const frontend_unified_q3_source_player **player,
    qa_error *e)
{
    const qa_unified_frame *frame=qa_unified_document_frame(o->frame);
    const qa_unified_frame_metadata *metadata=qa_unified_document_metadata(
        frontend_remote_unified_metadata_document(o->replica,frame));
    const qa_unified_provider_state *selected=NULL;
    *player=NULL;
    for(size_t i=0;metadata && i<metadata->configuration_count;++i)
        if(qa_actor_id_equal(metadata->configurations[i].actor,row->actor)) {
            selected=&metadata->configurations[i].character;break;
        }
    frontend_unified_q3_sources *sources=frontend_remote_unified_presentation_q3_sources(o->replica);
    if(!selected)return true;
    for(size_t i=0;i<frontend_unified_q3_sources_count(sources);++i) {
        frontend_unified_q3_source_view source;
        if(!frontend_unified_q3_sources_read(sources,i,&source,e))return false;
        if(strcmp(source.instance,selected->provider) || strcmp(source.content,selected->content))continue;
        for(size_t j=0;j<source.player_count;++j)
            if(qa_actor_id_equal(source.players[j].actor,id)) {
                *out=source;*player=source.players+j;return true;
            }
    }
    return true;
}
static bool character_read(frontend_unified_q3 *o,const qa_unified_character_state *row,unified_q3_character **out,qa_error *e)
{
    const qa_unified_frame *frame=qa_unified_document_frame(o->frame);qa_actor_id id;
    *out=NULL;
    if(!frontend_remote_unified_source_actor(o->replica,frame,row->actor,false,&id,e))return false;
    frontend_unified_q3_source_view source;const frontend_unified_q3_source_player *player;
    if(!character_source(o,row,id,&source,&player,e))return false;
    if(!player)return true;
    unified_q3_bank *bank=NULL;
    if(!bank_read(o,source.content,source.instance,0,&bank,e))return false;
    if(bank->clients && (bank->character_publication!=source.publication ||
        bank->character_map_revision!=source.map_revision)) {
        q3n_clients_destroy(bank->clients);bank->clients=NULL;
        for(unified_q3_character *c=o->characters;c;c=c->next)if(c->bank==bank)c->reset=true;
    }
    if(!bank->clients) {
        q3n_client_options options={.content=bank->files,.assets=bank->assets,.product=source.product};
        if(!q3n_clients_create_received(&options,&bank->clients,e))return false;
        bank->character_publication=source.publication;bank->character_map_revision=source.map_revision;
    }
    uint32_t index=player->client_slot;
    q3n_client_settings settings={.memory_remaining=SIZE_MAX,.loading=true};
    if(!q3n_clients_received_register(bank->clients,index,
        qa_q3_configstring(source.game_state,544u+index),source.configstring_revisions[544u+index],
        source.max_clients,source.game_type,&settings,e))return false;
    const q3n_client_info *client=q3n_clients_get(bank->clients,index);
    unified_q3_character *c=o->characters_by_slot[id.slot];
    if(!c){
        size_t slot;c=qa_pool_take(&o->character_records,&slot);
        if(!c)return frontend_unified_fail(e,QA_ERROR_MEMORY,"Loaded Q3 character capacity exhausted");
        *c=(unified_q3_character){.actor=id,.reset=true,.next=o->characters};
        o->characters=c;o->characters_by_slot[id.slot]=c;
    }else if(!qa_actor_id_equal(c->actor,id)){
        *c=(unified_q3_character){.actor=id,.reset=true,.next=c->next};
    }
    if(c->bank!=bank || c->media_revision!=client->media_revision)c->reset=true;
    c->bank=bank;c->client=*client;c->media_revision=client->media_revision;
    c->source=source.entities[player->source_number].state;
    c->origin=row->origin;c->angles=row->angles;c->velocity=row->velocity;
    c->movement=row->movement_direction;c->legs=row->legs;c->torso=row->torso;
    c->flags=row->source_flags;c->powerups=(uint32_t)c->source.powerups;
    c->scale=row->scale;c->opacity=row->opacity;c->team=client->team;
    memcpy(c->color,row->color,sizeof(c->color));
    c->visible=true;*out=c;return true;
}
static bool character_part(void *context,uint32_t part,qa_q3_ref_entity *ref,bool base,qa_error *e)
{
    (void)part;(void)base;unified_q3_character *c=context;
    return qa_q3_presentation_entity(c->bank->backend,ref,e) && current(c->bank->owner,e);
}
static bool character_submit(frontend_unified_q3 *o,unified_q3_character *c,qa_error *e)
{
    qa_actor_id viewer;uint32_t number;
    if(!frontend_remote_unified_player(o->replica,&viewer,&number))return false;
    c->hidden=(c->flags&128)!=0 || c->opacity<=0 || c->scale==0 || !c->client.info_valid;
    if(c->hidden)return true;
    if(c->reset){q3n_player_reset(&c->pose,c->angles);c->reset=false;}
    qa_q3_entity state=c->source;
    state.eFlags=q3ne_word(c->flags);state.legsAnim=c->legs;state.torsoAnim=c->torso;
    state.angles2[1]=(float)c->movement;
    state.pos.delta[0]=c->velocity.x;state.pos.delta[1]=c->velocity.y;state.pos.delta[2]=c->velocity.z;
    q3n_body_visual visual={.scale=c->scale,.opacity=c->opacity};
    memcpy(visual.color,c->color,sizeof(visual.color));
    q3n_body_options options={.time=o->time,
        .frame_milliseconds=q3ne_word((uint32_t)o->time-(uint32_t)o->previous_time),
        .local_view_client=qa_actor_id_equal(viewer,c->actor)?state.number:-1,.swing_speed=.3f,.visual=&visual};
    q3n_player_body body;
    if(!q3n_player_body_build(c->bank->assets,&c->pose,&c->client,&state,c->origin,c->angles,
        &options,&body,e))return false;
    memcpy(c->submitted_parts,body.parts,sizeof(body.parts));
    q3n_body_powerup_media media={0};
    if((c->powerups&(1u<<4)) &&
        !qa_q3_register_shader(c->bank->assets,"powerups/invisibility",true,&media.invisibility,e))return false;
    if(c->powerups&(1u<<1)) {
        if(!qa_q3_register_shader(c->bank->assets,c->team==1?"powerups/blueflag":"powerups/quad",
            true,c->team==1?&media.red_quad:&media.quad,e))return false;
    }
    if((c->powerups&(1u<<5)) &&
        !qa_q3_register_shader(c->bank->assets,"powerups/regen",true,&media.regeneration,e))return false;
    if((c->powerups&(1u<<2)) &&
        !qa_q3_register_shader(c->bank->assets,"powerups/battleSuit",true,&media.battle_suit,e))return false;
    for(uint32_t part=0;part<body.count;++part)
        if(!q3n_player_body_powerups(&media,o->time,c->powerups,c->team,part,&body.parts[part],
            c,character_part,e))return false;
    c->submitted=true;c->time=o->time;return true;
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
            if(w->missile_light != 0 && !qa_q3_presentation_light(b->backend,v->end,w->missile_light,w->missile_light_color,false,e))return false;
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
    for (unified_q3_character *c=o->characters;c;c=c->next) { c->visible=false;c->submitted=false;c->hidden=false;c->weapon_submitted=false; }
    const qa_unified_frame *received=qa_unified_document_frame(o->frame);
    const qa_unified_frame_visuals *visuals=received->visuals;
    size_t character_count=visuals?visuals->character_count:0;
    o->character_count=0;
    o->character_order=okay && character_count?qa_arena_alloc(&scene->storage,
        character_count*sizeof(*o->character_order),_Alignof(unified_q3_character *),e):NULL;
    if(okay && character_count && !o->character_order)okay=false;
    for (size_t i=0;okay && i<character_count;++i) {
        unified_q3_character *c;
        okay=character_read(o,visuals->characters+i,&c,e);
        if(okay && c)o->character_order[o->character_count++]=c;
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
            if(c->visible && c->bank==b)okay=character_submit(o,c,e);
        }
        if(okay)okay=ballistic_draw(o,b,e);
        if (okay) okay=q3n_marks_submit(&frame,e) && q3n_particles_add(&frame,e);
        qa_bounds bounds;
        if(okay)okay=qa_collision_model_bounds(frontend_remote_unified_geometry(o->replica),0,&bounds,e);
        if(okay){frame.refdef.origin=qa_vec_add(bounds.maxs,qa_v3(65536,65536,65536));okay=q3n_local_submit(&frame,e);}
        const qa_scene_light *lights=NULL; size_t count=0;
        if (okay) okay=qa_q3_presentation_lights_read(b->backend,&lights,&count,e);
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
    if(c->hidden || c->weapon_submitted)return true;
    if(!q3n_weapons_player_compiled_parent(f,c->bank->assets,&c->submitted_parts[1],row,e) ||
        !character_receipt(context,f,row,&c,e))return false;
    c->weapon_submitted=true;return true;
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
    *consumed=equipment->input->family==QA_GAME_Q3 || equipment->slot ||
        equipment->provider!=f->compiled->source.basis.provider ||
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
    qa_q3_scene_options options={.world=*world,.world_family=family==QA_COLLISION_Q1?QA_GAME_Q1:family==QA_COLLISION_Q2?QA_GAME_Q2:QA_GAME_Q3,.lod_scale=5,
        .ambient_scale=.6f,.directed_scale=1,.near_clip=4,.rail={.core_width=6,.ring_width=16,.segment_length=32}};
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
    qa_q3_scene_options options={.world=*world,.world_family=QA_GAME_Q1,.lod_scale=5,
        .ambient_scale=.6f,.directed_scale=1,.near_clip=4,.rail={.core_width=6,.ring_width=16,.segment_length=32}};
    qa_scene_state_default(&options.state);bool okay=true;
    for(unified_q3_bank *b=o->banks;okay && b;b=b->next)if(!b->retired)
        okay=qa_q3_presentation_supplement_prepare(b->backend,o->scene_bank,scene,&b->supplement,e) && current(o,e);
    if(okay && o->components && o->component_bank)
        okay=frontend_unified_components_prepare_submission(o->components,o->scene_bank,scene,e) && current(o,e);
    for(unified_q3_bank *b=o->banks;okay && b;b=b->next)if(!b->retired)
        okay=qa_q3_presentation_supplement_draw(b->supplement,&options,scene,e) && current(o,e);
    return leave(o,okay,e);
}
static bool source_command_event(frontend_unified_q3 *o,const qa_unified_presentation_event *row,bool command,qa_error *e)
{
    const qa_unified_q3_event *v=&row->payload.value.q3;
    bool okay=true;
    size_t count=frontend_remote_unified_presentation_q3_client_count(o->replica);
    for(size_t i=0;okay&&i<count;++i){
        frontend_unified_q3_client *client=frontend_remote_unified_presentation_q3_client(o->replica,i);
        if(!frontend_unified_q3_client_event_matches(client,row->provider,row->content,o->epoch))continue;
        if(command)okay=frontend_unified_q3_client_server_command(client,row->sequence,v->client,v->text,e);
        /* Configstring notifications can be intermediate writes. The CLIENT
         * compares the committed Source dictionary and reaches its own cs. */
        if(okay)okay=current(o,e);
    }
    return okay;
}
bool frontend_unified_q3_presentation(frontend_unified_q3 *o,const qa_unified_presentation_event *row,
    bool *mirrored,qa_error *e)
{
    if(!mirrored||!frontend_unified_q3_presentation_validate(o,row,e))return false;
    *mirrored=false;
    if(row->payload.kind==QA_UNIFIED_PRESENTATION_Q3_CHARACTER)return character_event(o,row,e);
    if(row->payload.kind==QA_UNIFIED_PRESENTATION_Q3_BALLISTIC)return ballistic_event_apply(o,row,e);
    const qa_unified_q3_event *v=&row->payload.value.q3;
    if(v->kind==QA_UNIFIED_Q3_SOUND){
        qa_actor_id id; bool duplicate=false;
        bool okay=o->events&&actor(o,v->actor,&id,e)&&frontend_unified_events_sound_mirrored(o->events,row,&duplicate,e);
        if(okay&&!duplicate){
            if(v->loop)okay=frontend_unified_events_sound_loop_path(o->events,row->content,v->resource,id,
                v->origin,v->velocity,row->seconds*1000,v->channel,v->volume,1,
                q3ne_word((uint32_t)o->frontend->frame_number),true,e);
            else okay=frontend_unified_events_sound_path(o->events,row->content,v->resource,id,v->origin,
                row->seconds*1000,v->channel,v->volume,1,0,e);
        }
        return okay&&current(o,e);
    }
    if(v->kind==QA_UNIFIED_Q3_PRINT||v->kind==QA_UNIFIED_Q3_LOG){
        const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
        qa_console_emit(domain->console,&domain->command_context,v->text);return current(o,e);
    }
    if(v->kind==QA_UNIFIED_Q3_SERVER_COMMAND)return source_command_event(o,row,true,e);
    if(v->kind==QA_UNIFIED_Q3_CONFIGSTRING)return source_command_event(o,row,false,e);
    return frontend_unified_fail(e,QA_ERROR_UNSUPPORTED,"Q3 Source event requires the actual retained cgame command/snapshot owner");
}
static bool q3_returned(const frontend_unified_q3 *o)
{
    if (!o || o->busy) return false;
    for (unified_q3_bank *b=o->banks; b; b=b->next)
        if ((b->effects && !q3n_events_idle(b->effects)) || (b->media && !q3n_media_idle(b->media)) ||
            (b->weapons && !q3n_weapons_idle(b->weapons)) || (b->particles && !q3n_particles_idle(b->particles)) ||
            (b->selected_media && !q3n_selected_media_idle(b->selected_media)) ||
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
        q3n_selected_media_destroy(b->selected_media);
        q3n_events_destroy(b->effects); q3n_media_destroy(b->media); q3n_clients_destroy(b->clients);
        o->banks=b->next; free(b->content);free(b->activation); free(b);
    }
    qa_arena_destroy(&o->character_storage);
    while(o->ballistics){unified_q3_ballistic *v=o->ballistics;o->ballistics=v->next;free(v->attachments);free(v);}
    qa_unified_document_destroy(o->frame); qa_unified_document_destroy(o->candidate);
    free(o); *address=NULL; return true;
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
