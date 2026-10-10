#include "source_cinematics.h"
#include "source_renderer_runtime.h"
#include "renderer_materials.h"
#include "component_scene.h"
#include "shared_resource_policy.h"
#include "q3_render_policy.h"
#include "q3_color_policy.h"
#include "material_movies.h"
#include "music_sources.h"
#include "qa/audio_music_prepare.h"
#include "visual_access.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/material_source_scratch.h"
#include "qa/render_controls.h"
#include "qa/binary.h"
#include "qa/q3_presentation_save.h"
#include "qa/q3_assets_custody.h"
#include "qa/application_q3_components.h"
#include "scene_identity.h"
#include "remote_unified_components.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
typedef struct component_scene_packet {
    struct component_scene_packet *next;
    frontend_component_scene_packet view;
    uint8_t *areas;
    qa_scene_light *world_lights,*projected_lights;
    qa_scene_vertex *rail_vertices;
    float *q1_styles;
    qa_vec3 *q2_styles;
    const char *texts[8];
} component_scene_packet;
struct frontend_component_scene_restore {
    struct frontend_component_scene_restore *next;
    uint64_t identity;
    qa_vfs *files;
    qa_resource_pool *pool;
    qa_q3_presentation_options policy;
};
static void constructor_policy(qa_q3_presentation_options *out,const qa_q3_presentation_options *in)
{
    out->viewport=in->viewport; out->near_clip=in->near_clip; out->far_clip=in->far_clip;
    out->identity_light=in->identity_light; out->lod_scale=in->lod_scale; out->lod_bias=in->lod_bias;
    out->shadow_mode=in->shadow_mode; out->rail_core_width=in->rail_core_width;
    out->rail_ring_width=in->rail_ring_width; out->rail_segment_length=in->rail_segment_length;
}
bool frontend_component_scene_restore_prepare(qa_frontend *f,uint64_t identity,qa_vfs **claimed,
    const qa_q3_presentation_options *policy,qa_error *e)
{
    if (!f || !f->source_restoring || f->capture || f->resource_inventory || !claimed || !*claimed || !policy ||
        identity<=QA_FRONTEND_COMMAND_OWNER || identity-QA_FRONTEND_COMMAND_OWNER>f->next_source_id ||
        frontend_source_identity_used(f,identity))
        return frontend_fail(e,QA_ERROR_FORMAT,"Component import prefix lost its genuine graph view or physical identity");
    for (const struct frontend_component_scene_restore *row=f->component_scene_restores;row;row=row->next)
        if (row->identity==identity) return frontend_fail(e,QA_ERROR_FORMAT,"Duplicate component private graph prefix");
    struct frontend_component_scene_restore *row=calloc(1,sizeof(*row));
    if (!row) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining component private graph prefix");
    row->identity=identity; row->files=*claimed; *claimed=NULL;
    row->pool=qa_vfs_resources(row->files); qa_resource_pool_retain(row->pool);
    constructor_policy(&row->policy,policy);
    row->next=f->component_scene_restores; f->component_scene_restores=row; return true;
}
bool frontend_component_scene_restores_destroy(qa_frontend *f,qa_error *e)
{
    if (!f) return true;
    if (f->capture || f->resource_inventory)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component graph prefixes retain an actual inventory borrower");
    while (f->component_scene_restores) {
        struct frontend_component_scene_restore *row=f->component_scene_restores;
        f->component_scene_restores=row->next; qa_vfs_destroy(row->files); qa_resource_pool_destroy(row->pool); free(row);
    }
    return true;
}

struct frontend_component_scene {
    struct frontend_component_scene *next;
    qa_frontend *frontend;
    application_q3_component_scene_preparation request;
    qa_launch_instance_lease *descriptor;
    qa_vfs *files;
    qa_resource_pool *pool;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_media_library *media;
    frontend_material_movies *movies;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    qa_cvars *cvars;
    qa_cvar_handle shadows;
    qa_command_context command;
    qa_q3_presentation_options restored_policy;
    qa_audio_music *music;
    char *music_intro,*music_loop;
    qa_scene_frame frame;
    component_scene_packet *packets,*last_packet;
    qa_q3_picture_receipt *pictures;
    size_t picture_count;
    uint64_t picture_frame;
    size_t picture_cursor;
    bool picture_frame_valid;
    bool pictures_pending;
    size_t pending_picture_cursor;
    uint64_t pending_picture_frame;
    size_t packet_count;
    uint64_t identity,sequence;
    bool host_borrow,ready,begun,music_looping,music_pending,has_restore_policy,retired;
};
static bool retained(const struct frontend_component_scene *owner)
{
    if (!owner || !owner->frontend || !owner->request.retained ||
        !owner->request.retained(owner->request.context)) return false;
    for (const struct frontend_component_scene *row=owner->frontend->component_scenes;row;row=row->next)
        if (row==owner) return true;
    return false;
}
static bool structural_retained(const struct frontend_component_scene *owner)
{
    if (!owner || !owner->frontend) return false;
    bool linked=false;
    for (const struct frontend_component_scene *row=owner->frontend->component_scenes;row;row=row->next)
        if (row==owner) linked=true;
    if (owner->request.origin==APPLICATION_Q3_COMPONENT_SCENE_REMOTE) {
        frontend_unified_component_scene_association remote;
        return linked && frontend_unified_components_scene_association_read(owner->frontend,owner->request.context,owner->identity,&remote) &&
            remote.frontend_owner==owner && remote.frontend_identity==owner->identity && remote.assets==owner->assets &&
            remote.owner==owner->request.owner && remote.service_owner==owner->request.service_owner &&
            remote.generation==owner->request.generation && remote.physical_seat==owner->request.physical_seat &&
            qa_actor_id_equal(remote.viewer,owner->request.viewer) && remote.recipe==owner->request.recipe &&
            remote.provider==owner->request.recipe_provider;
    }
    if (owner->request.origin!=APPLICATION_Q3_COMPONENT_SCENE_LOCAL) return false;
    qa_application_q3_component_scene_association view;
    const qa_launch_instance *descriptor=qa_launch_instance_lease_view(owner->descriptor);
    return linked && descriptor && qa_application_q3_component_scene_association_read(owner->frontend->application,owner->identity,&view) &&
        view.frontend_owner==owner && view.frontend_identity==owner->identity && view.owner==owner->request.owner &&
        view.service_owner==owner->request.service_owner && view.generation==owner->request.generation &&
        view.physical_seat==owner->request.physical_seat && qa_actor_id_equal(view.viewer,owner->request.viewer) &&
        view.assets==owner->assets && view.descriptor && view.descriptor->storage==descriptor->storage;
}
static bool retained_clock(const struct frontend_component_scene *owner,int32_t *out)
{
    if (!structural_retained(owner)) return false;
    if (owner->request.origin==APPLICATION_Q3_COMPONENT_SCENE_REMOTE) {
        frontend_unified_component_scene_association view;
        if (!frontend_unified_components_scene_association_read(owner->frontend,owner->request.context,owner->identity,&view)) return false;
        *out=view.time_ms;
    } else {
        qa_application_q3_component_scene_association view;
        if (!qa_application_q3_component_scene_association_read(owner->frontend->application,owner->identity,&view)) return false;
        *out=view.time_ms;
    }
    return true;
}
static bool publication(struct frontend_component_scene *owner,application_q3_scene_context *view,qa_error *e)
{
    return retained(owner) && !owner->retired && owner->ready && owner->host_borrow &&
        owner->request.publication_read(owner->request.context,view) &&
        owner->request.source.current(owner->request.source.context,view) ? true :
        frontend_fail(e,QA_ERROR_ARGUMENT,"Component renderer lost its genuine entered source publication");
}
static bool movie_current(void *context,const frontend_material_movie_source *view)
{
    struct frontend_component_scene *owner=context;
    return retained(owner) && view && view->frontend==owner->frontend && view->context==owner &&
        view->files==owner->files && view->images==owner->images &&
        view->materials==owner->materials && view->media==owner->media;
}
static bool model_initialize(void *context,const qa_q3_model_opening *opening,
    const qa_model *model,qa_scene_model *root,qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    return publication(owner,&view,e) &&
        frontend_visual_registered_model_initialize(owner->frontend,opening,model,root,e) &&
        owner->request.source.current(owner->request.source.context,&view);
}
static void print(void *context,const char *text)
{
    struct frontend_component_scene *owner=context;
    if (retained(owner)) frontend_console_print(owner->frontend,&owner->command,text);
}
static double milliseconds(void *context)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    return retained(owner) && owner->request.publication_read(owner->request.context,&view)?view.time_ms:0;
}
static int32_t source_time(void *context) { return (int32_t)milliseconds(context); }
static int32_t frame_number(void *context)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    return retained(owner) && owner->request.publication_read(owner->request.context,&view)?
        (int32_t)((uint64_t)view.revision&INT32_MAX):0;
}
static uint64_t audio_bus(void *context) { return ((struct frontend_component_scene *)context)->identity; }
static qa_game_family music_family(const struct frontend_component_scene *owner)
{
    const qa_launch_instance *descriptor=qa_launch_instance_lease_view(owner->descriptor);
    qa_product_id id=descriptor?descriptor->selection.product:owner->request.recipe_provider->selection.product;
    const qa_product *product=qa_catalog_product(owner->request.catalog,id);
    return product && product->family==QA_GAME_Q1?QA_GAME_Q1:
        product && product->family==QA_GAME_Q2?QA_GAME_Q2:QA_GAME_Q3;
}
static bool music_published(const struct frontend_component_scene *owner)
{ return retained(owner) && owner->request.published && owner->request.published(owner->request.context); }
static bool music_stop(void *context,qa_error *e)
{
    struct frontend_component_scene *owner=context;
    qa_audio_music *attached=qa_audio_engine_bus_music(owner->frontend->audio,owner->identity);
    if (attached && attached!=owner->music)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component music bus contains another player");
    qa_audio_engine_remove_music(owner->frontend->audio,owner->identity);
    if (qa_audio_engine_bus_music(owner->frontend->audio,owner->identity))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component music stop retained its exact bus");
    if (owner->music) qa_audio_music_stop(owner->music);
    free(owner->music_intro); free(owner->music_loop);
    owner->music_intro=owner->music_loop=NULL; owner->music_looping=owner->music_pending=false;
    return true;
}
static bool music_current(void *context,const frontend_music_origin *origin)
{
    struct frontend_component_scene *owner=context;
    const qa_launch_instance *descriptor=owner?qa_launch_instance_lease_view(owner->descriptor):NULL;
    bool declaration=owner && (owner->request.origin==APPLICATION_Q3_COMPONENT_SCENE_LOCAL ?
        descriptor && origin && origin->descriptor && origin->descriptor->storage==descriptor->storage &&
            !origin->recipe && !origin->recipe_provider :
        origin && !origin->descriptor && origin->recipe==owner->request.recipe &&
            origin->recipe_provider==owner->request.recipe_provider &&
            qa_executable_recipe_current(owner->request.recipe,owner->request.catalog));
    qa_product_id product=descriptor?descriptor->selection.product:
        owner && owner->request.recipe_provider?owner->request.recipe_provider->selection.product:0;
    return declaration && music_published(owner) && origin->kind==FRONTEND_MUSIC_COMPONENT &&
        origin->context==owner && origin->receiver==owner->request.owner && origin->bus==owner->identity &&
        origin->physical_seat==owner->request.physical_seat && origin->catalog==owner->request.catalog &&
        origin->product==product && origin->files==owner->files && origin->music==owner->music &&
        (!qa_audio_engine_bus_music(owner->frontend->audio,owner->identity) ||
         qa_audio_engine_bus_music(owner->frontend->audio,owner->identity)==owner->music);
}
static frontend_music_origin music_origin(struct frontend_component_scene *owner)
{
    const qa_launch_instance *descriptor=qa_launch_instance_lease_view(owner->descriptor);
    return (frontend_music_origin){.kind=FRONTEND_MUSIC_COMPONENT,.receiver=owner->request.owner,
        .bus=owner->identity,.physical_seat=owner->request.physical_seat,.descriptor=descriptor,
        .recipe=owner->request.recipe,.recipe_provider=owner->request.recipe_provider,
        .catalog=owner->request.catalog,.product=descriptor?descriptor->selection.product:
            owner->request.recipe_provider->selection.product,.files=owner->files,
        .music=owner->music,.context=owner,.current=music_current,.stop=music_stop};
}
static bool music_publish(struct frontend_component_scene *owner,qa_error *e)
{
    if (!owner->music_pending) return true;
    if (!music_published(owner)) return true;
    frontend_music_origin origin=music_origin(owner);
    if (!frontend_music_sources_explicit_begin(owner->frontend->music_sources,&origin,e)) return false;
    qa_audio_music *attached=qa_audio_engine_bus_music(owner->frontend->audio,owner->identity);
    if (attached && attached!=owner->music)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Pending component cue lost its actual player bus");
    if (qa_audio_music_playing(owner->music) && !attached) {
        if (!qa_audio_music_retain(owner->music,e)) return false;
        if (!qa_audio_engine_music(owner->frontend->audio,owner->identity,owner->request.physical_seat,1,owner->music,e)) {
            qa_audio_music_release(owner->music); return false;
        }
    }
    if (!frontend_music_sources_explicit(owner->frontend->music_sources,&origin,
        owner->music_intro?owner->music_intro:"",owner->music_loop?owner->music_loop:"",owner->music_looping,e)) return false;
    owner->music_pending=false; return true;
}
static bool music(void *context,const char *intro_name,const char *loop_name,qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    if (!publication(owner,&view,e)) return false;
    if (!owner->frontend->audio) return frontend_fail(e,QA_ERROR_UNSUPPORTED,"Component music output is disabled");
    qa_audio_music *attached=qa_audio_engine_bus_music(owner->frontend->audio,owner->identity);
    if (attached && attached!=owner->music)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component cue lost its actual player bus");
    if (!owner->music && !qa_audio_music_create(qa_audio_engine_rate(owner->frontend->audio),
        music_family(owner),true,&owner->music,e)) return false;
    frontend_music_origin origin=music_origin(owner);
    bool published=music_published(owner);
    if (published && !frontend_music_sources_explicit_selected(owner->frontend->music_sources,&origin)) {
        if (attached || !qa_audio_music_idle(owner->music))
            return frontend_fail(e,QA_ERROR_ARGUMENT,"Component source selection retains its previous player");
        qa_audio_music *fresh=NULL;
        if (!qa_audio_music_create(qa_audio_engine_rate(owner->frontend->audio),music_family(owner),true,&fresh,e)) return false;
        qa_audio_music_release(owner->music); owner->music=fresh;
        free(owner->music_intro); free(owner->music_loop); owner->music_intro=owner->music_loop=NULL;
        owner->music_looping=false; origin=music_origin(owner);
    }
    qa_audio_music_controls *controls=frontend_music_sources_controls(owner->frontend->music_sources);
    if (!controls || (!qa_audio_music_controls_is(owner->music,controls) &&
        !qa_audio_music_controls_bind(owner->music,controls,e))) return false;
    if (published && !frontend_music_sources_explicit_begin(owner->frontend->music_sources,&origin,e)) return false;
    bool enabled=false;
    if (!qa_audio_music_controls_enabled(controls,&enabled)) return false;
    if (intro_name && *intro_name && !enabled) return true;
    const char *loop_text=loop_name?loop_name:"";
    if (intro_name && owner->music_intro && owner->music_loop && owner->music_looping &&
        !strcmp(intro_name,owner->music_intro) && !strcmp(loop_text,owner->music_loop) &&
        qa_audio_music_playing(owner->music)) return true;
    qa_audio_music_stop(owner->music);
    free(owner->music_intro); free(owner->music_loop); owner->music_intro=owner->music_loop=NULL;
    owner->music_looping=false; owner->music_pending=true;
    if (intro_name && *intro_name) {
        owner->music_intro=malloc(strlen(intro_name)+1); owner->music_loop=malloc(strlen(loop_text)+1);
        if (!owner->music_intro || !owner->music_loop) {
            free(owner->music_intro); free(owner->music_loop); owner->music_intro=owner->music_loop=NULL;
            return frontend_fail(e,QA_ERROR_MEMORY,"Retaining private component cue names");
        }
        strcpy(owner->music_intro,intro_name); strcpy(owner->music_loop,loop_text);
        qa_audio_stream *intro=NULL,*loop=NULL;
        if (!qa_audio_bank_music_cue(owner->sounds,intro_name,music_family(owner),NULL,NULL,&intro,e)) return false;
        if (intro) {
            loop=intro;
            if (*loop_text && strcmp(intro_name,loop_text) &&
                !qa_audio_bank_music_cue(owner->sounds,loop_text,music_family(owner),NULL,NULL,&loop,e)) {
                qa_audio_stream_close(intro); return false;
            }
            owner->music_looping=loop!=NULL; qa_audio_music_start(owner->music,intro,loop);
        }
    }
    return (!published || music_publish(owner,e)) && owner->request.source.current(owner->request.source.context,&view);
}
static bool audio_actor(void *context,int32_t number,uint64_t *out,qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    if (!out || !publication(owner,&view,e)) return false;
    if (number<0) { *out=QA_AUDIO_NO_ACTOR; return true; }
    qa_actor_id actor={0}; bool owned=false,found=false;
    if (!owner->request.source.actor(owner->request.source.context,(uint32_t)number,&actor,&owned,&found,e)) return false;
    if (!found) { *out=QA_AUDIO_NO_ACTOR; return true; }
    *out=frontend_audio_actor(owner->frontend,actor,e);
    return *out!=QA_AUDIO_NO_ACTOR && owner->request.source.current(owner->request.source.context,&view);
}
static bool diagnostics(void *context,qa_scene_source_diagnostics *out,qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    return publication(owner,&view,e) && frontend_q3_material_diagnostics_read(owner->frontend,out,e) &&
        owner->request.source.current(owner->request.source.context,&view);
}
static bool prepare_view(void *context,const qa_q3_refdef *ref,qa_q3_scene_options *options,qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view; (void)ref;
    if (!publication(owner,&view,e) || !frontend_q3_scene_policy_read(owner->frontend,options,e) ||
        !frontend_q3_shadow_mode_read(owner->cvars,owner->shadows,&options->shadow_mode,e)) return false;
    options->world.no_world=true;
    options->world.source_scratch=NULL;
    options->world.source_diagnostics_read=diagnostics;
    options->world.source_diagnostics_context=owner;
    return owner->request.source.current(owner->request.source.context,&view);
}
static bool prepare_picture(void *context,qa_material_context *material,qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    if (!publication(owner,&view,e)) return false;
    material->source_scratch=NULL;
    material->source_diagnostics_read=diagnostics; material->source_diagnostics_context=owner;
    return frontend_q3_material_diagnostics_read(owner->frontend,&material->source_diagnostics,e) &&
        owner->request.source.current(owner->request.source.context,&view);
}
static bool remap(void *context,const char *from,const char *to,float offset,qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    qa_material_source_remap_status status;
    return publication(owner,&view,e) && qa_material_remap_source(owner->materials,from,to,offset,&status,e) &&
        owner->request.source.current(owner->request.source.context,&view);
}
static bool end_registration(void *context,qa_error *error)
{
    struct frontend_component_scene *owner=context;
    if(!owner || !owner->frontend || owner->retired || owner->request.restoring ||
        owner->frontend->source_restoring || owner->frontend->capture || owner->frontend->resource_inventory)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Component registration completion needs its actual fresh constructor");
    return frontend_source_renderer_end_registration(owner->frontend,error);
}
static bool configuration(void *context,uint8_t out[11332],qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    return publication(owner,&view,e) && frontend_q3_configuration(owner->frontend,out,e) &&
        owner->request.source.current(owner->request.source.context,&view);
}
static void packet_destroy(component_scene_packet *packet)
{
    free((void *)packet->view.entities); free((void *)packet->view.polygons);
    free((void *)packet->view.vertices); free((void *)packet->view.lights);
    free(packet->areas); free(packet->world_lights); free(packet->projected_lights);
    free(packet->rail_vertices);
    free(packet->q1_styles); free(packet->q2_styles); free(packet);
}
static void packets_clear(struct frontend_component_scene *owner)
{
    while (owner->packets) {
        component_scene_packet *packet=owner->packets;
        owner->packets=packet->next; packet_destroy(packet);
    }
    owner->last_packet=NULL; owner->packet_count=0;
}
static void pictures_clear(struct frontend_component_scene *owner)
{
    for (size_t i=0;i<owner->picture_count;++i) {
        qa_material_release(owner->pictures[i].material);
        qa_scene_image_release(owner->pictures[i].image);
    }
    free(owner->pictures); owner->pictures=NULL; owner->picture_count=0;
    owner->picture_frame_valid=false; owner->picture_cursor=0;
}
static bool picture_append(struct frontend_component_scene *owner,const qa_q3_picture_receipt *receipt,qa_error *e)
{
    if (!receipt || receipt->assets!=owner->assets || (!receipt->material==!receipt->image) ||
        receipt->seat!=owner->request.physical_seat || !receipt->viewport.width || !receipt->viewport.height ||
        !isfinite(receipt->rect.x) || !isfinite(receipt->rect.y) || !isfinite(receipt->rect.width) || !isfinite(receipt->rect.height) ||
        !isfinite(receipt->uv.x) || !isfinite(receipt->uv.y) || !isfinite(receipt->uv.z) || !isfinite(receipt->uv.w) ||
        !isfinite(receipt->color.x) || !isfinite(receipt->color.y) || !isfinite(receipt->color.z) || !isfinite(receipt->color.w) ||
        !isfinite(receipt->identity_light) || owner->picture_count>=SIZE_MAX/sizeof(*owner->pictures))
        return frontend_fail(e,QA_ERROR_FORMAT,"Component picture leaves its actual registered command namespace");
    qa_q3_picture_receipt *rows=realloc(owner->pictures,(owner->picture_count+1)*sizeof(*rows));
    if (!rows) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining private component picture command");
    owner->pictures=rows;
    if (receipt->material && !qa_material_retain(receipt->material,e)) return false;
    if (receipt->image) qa_scene_image_retain(receipt->image);
    owner->pictures[owner->picture_count++]=*receipt; return true;
}
static bool picture_capture(void *context,const qa_q3_picture_receipt *receipt,qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    return owner->begun && publication(owner,&view,e) && picture_append(owner,receipt,e) &&
        owner->request.source.current(owner->request.source.context,&view);
}
static bool copy_span(const void *span,size_t count,size_t element,void **out,qa_error *e)
{
    if (count && (!span || count>SIZE_MAX/element))
        return frontend_fail(e,QA_ERROR_FORMAT,"Completed component scene span exceeds its real source extent");
    if (!count) { *out=NULL; return true; }
    *out=malloc(count*element);
    if (!*out) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining completed component scene");
    memcpy(*out,span,count*element); return true;
}
static bool packet_copy(struct frontend_component_scene *owner,const qa_q3_refdef *definition,const qa_q3_scene_options *options,
    const qa_q3_ref_entity *entities,size_t entity_count,const qa_q3_scene_polygon *polygons,size_t polygon_count,
    const qa_scene_vertex *vertices,size_t vertex_count,const qa_scene_light *lights,size_t light_count,qa_error *e)
{
    if (!definition || !options ||
        owner->packet_count==SIZE_MAX || options->world.shadow_light_count || options->world.entity_material ||
        options->world.q1_mirror || options->world.q1_sky_environment || options->world.q1_sky)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component scene completion leaves its genuine private render namespace");
    for (size_t i=0;i<polygon_count;++i)
        if (!polygons || polygons[i].first>vertex_count || polygons[i].count>vertex_count-polygons[i].first)
            return frontend_fail(e,QA_ERROR_FORMAT,"Completed component polygon leaves its issued vertices");
    component_scene_packet *packet=calloc(1,sizeof(*packet));
    if (!packet) return frontend_fail(e,QA_ERROR_MEMORY,"Retaining completed component render packet");
    packet->view=(frontend_component_scene_packet){.definition=*definition,.options=*options,
        .entity_count=entity_count,.polygon_count=polygon_count,.vertex_count=vertex_count,.light_count=light_count};
    void *entity_copy=NULL,*polygon_copy=NULL,*vertex_copy=NULL,*light_copy=NULL;
    bool ok=copy_span(entities,entity_count,sizeof(*entities),&entity_copy,e) &&
        copy_span(polygons,polygon_count,sizeof(*polygons),&polygon_copy,e) &&
        copy_span(vertices,vertex_count,sizeof(*vertices),&vertex_copy,e) &&
        copy_span(lights,light_count,sizeof(*lights),&light_copy,e);
    packet->view.entities=entity_copy; packet->view.polygons=polygon_copy;
    packet->view.vertices=vertex_copy; packet->view.lights=light_copy;
    void *areas=NULL,*world_lights=NULL,*projected=NULL;
    if (ok) ok=copy_span(options->world.visible_areas,options->world.visible_area_bytes,1,&areas,e) &&
        copy_span(options->world.lights,options->world.light_count,sizeof(qa_scene_light),&world_lights,e) &&
        copy_span(options->world.projected_lights,options->world.projected_light_count,sizeof(qa_scene_light),&projected,e);
    packet->areas=areas; packet->world_lights=world_lights; packet->projected_lights=projected;
    void *q1=NULL,*q2=NULL;
    if (ok && options->world.q1_styles) ok=copy_span(options->world.q1_styles,options->world.style_count,sizeof(float),&q1,e);
    if (ok && options->world.q2_styles) ok=copy_span(options->world.q2_styles,options->world.style_count,sizeof(qa_vec3),&q2,e);
    packet->q1_styles=q1; packet->q2_styles=q2;
    void *rail_vertices=NULL;
    if (ok) ok=copy_span(options->rail.retained_vertices,options->rail.retained_count,
        sizeof(qa_scene_vertex),&rail_vertices,e);
    packet->rail_vertices=rail_vertices;
    if (ok && options->world.render_text_count>8)
        ok=frontend_fail(e,QA_ERROR_FORMAT,"Completed component scene exceeds its actual refdef text slots");
    if (ok) {
        packet->view.options.world.visible_areas=packet->areas;
        packet->view.options.world.lights=packet->world_lights;
        packet->view.options.world.projected_lights=packet->projected_lights;
        packet->view.options.world.q1_styles=packet->q1_styles;
        packet->view.options.world.q2_styles=packet->q2_styles;
        packet->view.options.rail.retained_vertices=packet->rail_vertices;
        for (size_t i=0;i<options->world.render_text_count;++i) packet->texts[i]=packet->view.definition.text[i];
        packet->view.options.world.render_texts=packet->texts;
    }
    if (!ok) { packet_destroy(packet); return false; }
    if (owner->last_packet) owner->last_packet->next=packet; else owner->packets=packet;
    owner->last_packet=packet; ++owner->packet_count; return true;
}
static bool scene_completed(void *context,const qa_q3_refdef *definition,const qa_q3_scene_options *options,
    const qa_q3_ref_entity *entities,size_t entity_count,const qa_q3_scene_polygon *polygons,size_t polygon_count,
    const qa_scene_vertex *vertices,size_t vertex_count,const qa_scene_light *lights,size_t light_count,qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context source;
    return publication(owner,&source,e) && owner->begun &&
        packet_copy(owner,definition,options,entities,entity_count,polygons,polygon_count,vertices,vertex_count,lights,light_count,e) &&
        owner->request.source.current(owner->request.source.context,&source);
}
static void release_host(void *context) { ((struct frontend_component_scene *)context)->host_borrow=false; }
static bool idle(const void *context)
{
    const struct frontend_component_scene *owner=context;
    return owner && !owner->pictures_pending && !owner->frame.source_pending && (!owner->presentation || qa_q3_presentation_idle(owner->presentation)) &&
        (!owner->movies || frontend_material_movies_idle(owner->movies));
}
static bool retire(void *context,qa_error *e)
{
    struct frontend_component_scene *owner=context;
    if (!owner || owner->host_borrow || !idle(owner) || owner->frontend->capture || owner->frontend->resource_inventory)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component retirement retains its host or output borrower");
    if (owner->retired) return qa_q3_assets_services_retire(owner->assets,e);
    if ((owner->frontend->music_sources &&
        !frontend_music_sources_explicit_retire(owner->frontend->music_sources,owner,e)) ||
        !music_stop(owner,e) ||
        (owner->frontend->audio && !qa_audio_engine_stop_owner(owner->frontend->audio,owner->identity,owner->request.physical_seat,e)) ||
        !qa_q3_assets_services_retire(owner->assets,e)) return false;
    owner->retired=true;
    return true;
}
static bool destroy(void **slot,qa_error *e)
{
    struct frontend_component_scene *owner=slot?*slot:NULL;
    if (!owner) return true;
    if (owner->host_borrow || !idle(owner) || owner->frontend->capture || owner->frontend->resource_inventory)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component renderer retains its real host or output borrower");
    if ((owner->frontend->music_sources &&
        !frontend_music_sources_explicit_retire(owner->frontend->music_sources,owner,e)) || !music_stop(owner,e)) return false;
    qa_audio_music_release(owner->music); owner->music=NULL;
    if (owner->presentation && !qa_q3_presentation_destroy(owner->presentation,e)) return false;
    owner->presentation=NULL;
    if (owner->movies && owner->media && !owner->frontend->source_restoring) {
        frontend_material_movie_source expected={.frontend=owner->frontend,.files=owner->files,.images=owner->images,
            .materials=owner->materials,.media=owner->media,.context=owner,.current=movie_current};
        if (!frontend_renderer_materials_adopt_movies(owner->frontend,&expected,&owner->movies,&owner->media,e)) return false;
    }
    if (!frontend_material_movies_destroy(&owner->movies,e)) return false;
    packets_clear(owner);
    pictures_clear(owner);
    qa_scene_frame_destroy(&owner->frame);
    qa_q3_presentation_assets_destroy(owner->assets);
    qa_font_library_destroy(owner->fonts); qa_audio_bank_destroy(owner->sounds);
    qa_media_library_destroy(owner->media); qa_material_library_destroy(owner->materials);
    qa_scene_resources_destroy(owner->images); qa_vfs_destroy(owner->files); qa_resource_pool_destroy(owner->pool);
    qa_launch_instance_lease_release(owner->descriptor);
    struct frontend_component_scene **link=&owner->frontend->component_scenes;
    while (*link && *link!=owner) link=&(*link)->next;
    if (*link==owner) *link=owner->next;
    free(owner); *slot=NULL; return true;
}
static bool identity_read(const void *context,uint64_t *out)
{
    const struct frontend_component_scene *owner=context;
    if (!out || !retained(owner)) return false;
    *out=owner->identity; return true;
}
static bool begin(void *context,uint64_t sequence,qa_error *e)
{
    struct frontend_component_scene *owner=context;
    if (!retained(owner) || owner->retired || !owner->ready || !owner->host_borrow || !idle(owner))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component output begin needs its real returned private renderer");
    if (!music_publish(owner,e)) return false;
    packets_clear(owner);
    pictures_clear(owner);
    qa_scene_frame_reset(&owner->frame,sequence);
    owner->sequence=sequence; owner->begun=true;
    return qa_q3_presentation_frame(owner->presentation,&owner->frame,frontend_viewport(owner->frontend,owner->request.physical_seat),e);
}
static bool finish(void *context,bool submitted,qa_error *e)
{
    struct frontend_component_scene *owner=context;
    if (!retained(owner) || !owner->ready || !owner->host_borrow)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component output finish lost its retained entered renderer");
    bool ok=!owner->frame.source_pending;
    if (!ok) frontend_fail(e,QA_ERROR_ARGUMENT,"Private component output borrowed the physical Source issue queue");
    if (!submitted || !ok) { packets_clear(owner); pictures_clear(owner); }
    return ok;
}
static bool completed(void *context,uint64_t sequence,const qa_scene_frame **out,qa_error *e)
{
    struct frontend_component_scene *owner=context;
    if (out) *out=NULL;
    if (!out || !retained(owner) || !owner->begun || owner->sequence!=sequence)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component output completion differs from its real Draw sequence");
    if (!idle(owner)) return frontend_fail(e,QA_ERROR_ARGUMENT,"Component output completion retains entered children");
    *out=&owner->frame; return true;
}
bool frontend_component_scenes_idle(const qa_frontend *f)
{
    for (const struct frontend_component_scene *row=f?f->component_scenes:NULL;row;row=row->next) if (!idle(row)) return false;
    return true;
}
size_t frontend_component_scene_count(const qa_frontend *f)
{
    size_t count=0;
    for (const struct frontend_component_scene *row=f?f->component_scenes:NULL;row;row=row->next) ++count;
    return count;
}
bool frontend_component_scene_metadata_read(const qa_frontend *f,size_t ordinal,frontend_component_scene_view *out,qa_error *e)
{
    const struct frontend_component_scene *owner=f?f->component_scenes:NULL;
    while (owner && ordinal--) owner=owner->next;
    qa_q3_presentation_binding binding;
    int32_t clock_ms;
    if (!out || !owner || !owner->ready || !retained_clock(owner,&clock_ms) ||
        !((f->resource_inventory || f->capture || owner->retired)?structural_retained(owner):retained(owner)) ||
        owner->frame.source_pending || owner->frontend!=f ||
        !qa_q3_presentation_binding_read(owner->presentation,&binding,e))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component graph read requires its genuine returned private renderer");
    if (binding.options.assets!=owner->assets || binding.options.context!=owner ||
        (binding.frame && binding.frame!=&owner->frame) || binding.world || binding.geometry ||
        !binding.options.source_scene_membership || binding.options.scene_completed!=scene_completed ||
        binding.options.prepare_view!=prepare_view || binding.options.prepare_picture!=prepare_picture ||
        binding.options.picture_capture!=picture_capture)
        return frontend_fail(e,QA_ERROR_FORMAT,"Component metadata differs from its installed private renderer");
    qa_audio_music *attached=qa_audio_engine_bus_music(f->audio,owner->identity);
    if (attached && attached!=owner->music)
        return frontend_fail(e,QA_ERROR_FORMAT,"Component graph music differs from its actual bus player");
    *out=(frontend_component_scene_view){.owner=owner,.identity=owner->identity,.sequence=owner->sequence,
        .clock_ms=clock_ms,.retired=owner->retired,.packet_count=owner->packet_count,.picture_count=owner->picture_count,
        .picture_frame=owner->picture_frame,.picture_cursor=owner->picture_cursor,
        .picture_frame_valid=owner->picture_frame_valid,.origin=owner->request.origin,
        .recipe=owner->request.recipe,.recipe_provider=owner->request.recipe_provider,
        .generation=owner->request.generation,.service_owner=owner->request.service_owner,
        .receiver=owner->request.owner,.physical_seat=owner->request.physical_seat,.viewer=owner->request.viewer,
        .descriptor=qa_launch_instance_lease_view(owner->descriptor),.catalog=owner->request.catalog,
        .files=owner->files,.pool=owner->pool,.images=owner->images,.materials=owner->materials,.fonts=owner->fonts,
        .sounds=owner->sounds,.media=owner->media,.movies=owner->movies,.assets=owner->assets,
        .presentation=owner->presentation,.policy=binding.options,.frame=&owner->frame,.music=owner->music,
        .music_intro=owner->music_intro,.music_loop=owner->music_loop,.begun=owner->begun,
        .music_attached=attached!=NULL,.music_looping=owner->music_looping,.music_pending=owner->music_pending};
    return true;
}
bool frontend_component_scene_read(const qa_frontend *f,size_t ordinal,frontend_component_scene_view *out,qa_error *e)
{
    const struct frontend_component_scene *owner=f?f->component_scenes:NULL;
    size_t index=ordinal;
    while (owner && index--) owner=owner->next;
    if (!owner || !idle(owner)) return frontend_fail(e,QA_ERROR_ARGUMENT,"Component graph retains active private children");
    return frontend_component_scene_metadata_read(f,ordinal,out,e);
}
bool frontend_component_scene_metadata_current(const qa_frontend *f,const frontend_component_scene_view *view,qa_error *e)
{
    if (!view) return false;
    size_t ordinal=0;
    for (const struct frontend_component_scene *row=f?f->component_scenes:NULL;row;row=row->next,++ordinal) {
        if (row->identity!=view->identity) continue;
        frontend_component_scene_view actual;
        if (!structural_retained(row)) return false;
        return frontend_component_scene_metadata_read(f,ordinal,&actual,e) &&
            actual.owner==view->owner && actual.clock_ms==view->clock_ms && actual.retired==view->retired && actual.sequence==view->sequence && actual.generation==view->generation &&
            actual.service_owner==view->service_owner && actual.receiver==view->receiver &&
            actual.physical_seat==view->physical_seat && qa_actor_id_equal(actual.viewer,view->viewer) &&
            actual.origin==view->origin && actual.descriptor==view->descriptor && actual.recipe==view->recipe &&
            actual.recipe_provider==view->recipe_provider && actual.catalog==view->catalog && actual.files==view->files && actual.pool==view->pool &&
            actual.images==view->images && actual.materials==view->materials && actual.fonts==view->fonts &&
            actual.sounds==view->sounds && actual.media==view->media && actual.movies==view->movies &&
            actual.assets==view->assets && actual.presentation==view->presentation && actual.frame==view->frame &&
            actual.music==view->music && actual.music_intro==view->music_intro && actual.music_loop==view->music_loop &&
            actual.music_attached==view->music_attached &&
            actual.music_looping==view->music_looping && actual.music_pending==view->music_pending &&
            actual.begun==view->begun && actual.packet_count==view->packet_count && actual.picture_count==view->picture_count &&
            actual.picture_frame==view->picture_frame && actual.picture_cursor==view->picture_cursor &&
            actual.picture_frame_valid==view->picture_frame_valid;
    }
    return frontend_fail(e,QA_ERROR_ARGUMENT,"Component metadata leaves its actual private roster");
}
bool frontend_component_scene_packet_count(const qa_frontend *f,uint64_t identity,uint64_t sequence,size_t *out,qa_error *e)
{
    for (const struct frontend_component_scene *row=f?f->component_scenes:NULL;row;row=row->next)
        if (row->identity==identity) {
            if (!out || !((f->resource_inventory || f->capture)?structural_retained(row):retained(row)) ||
                !idle(row) || !row->begun || row->sequence!=sequence)
                return frontend_fail(e,QA_ERROR_ARGUMENT,"Component packet count lost its returned Draw receipt");
            *out=row->packet_count; return true;
        }
    return frontend_fail(e,QA_ERROR_ARGUMENT,"Component packet count leaves its actual renderer roster");
}
bool frontend_component_scene_packet_read(const qa_frontend *f,uint64_t identity,uint64_t sequence,
    size_t ordinal,frontend_component_scene_packet *out,qa_error *e)
{
    const struct frontend_component_scene *owner=f?f->component_scenes:NULL;
    while (owner && owner->identity!=identity) owner=owner->next;
    if (!owner || !out || !((f->resource_inventory || f->capture || owner->retired)?structural_retained(owner):retained(owner)) ||
        !idle(owner) || !owner->begun || owner->sequence!=sequence)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component scene packet lost its returned actual Draw sequence");
    const component_scene_packet *packet=owner->packets;
    while (packet && ordinal--) packet=packet->next;
    if (!packet) return frontend_fail(e,QA_ERROR_ARGUMENT,"Component scene packet ordinal leaves its completed roster");
    *out=packet->view; return true;
}
bool frontend_component_scene_picture_count(const qa_frontend *f,uint64_t identity,uint64_t sequence,size_t *out,qa_error *e)
{
    for (const struct frontend_component_scene *owner=f?f->component_scenes:NULL;owner;owner=owner->next)
        if (owner->identity==identity) {
            if (!out || !owner->begun || owner->sequence!=sequence || !idle(owner) ||
                !((f->capture || f->resource_inventory)?structural_retained(owner):retained(owner)))
                return frontend_fail(e,QA_ERROR_ARGUMENT,"Component pictures lost their returned private output sequence");
            *out=owner->picture_count; return true;
        }
    return frontend_fail(e,QA_ERROR_ARGUMENT,"Component pictures leave the real private renderer roster");
}
bool frontend_component_scene_picture_read(const qa_frontend *f,uint64_t identity,uint64_t sequence,size_t ordinal,
    qa_q3_picture_receipt *out,qa_error *e)
{
    size_t count;
    if (!out || !frontend_component_scene_picture_count(f,identity,sequence,&count,e) || ordinal>=count)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component picture ordinal leaves its actual captured command span");
    for (const struct frontend_component_scene *owner=f->component_scenes;owner;owner=owner->next)
        if (owner->identity==identity) { *out=owner->pictures[ordinal]; return true; }
    return false;
}
bool frontend_component_scene_pictures(qa_frontend *f,uint64_t identity,uint64_t sequence,
    qa_q3_presentation *recipient,qa_scene_frame *frame,qa_error *e)
{
    size_t count=0;
    if (!f || frame!=&f->frame || f->capture || f->resource_inventory || f->source_restoring ||
        !recipient || !frontend_component_scene_picture_count(f,identity,sequence,&count,e)) return false;
    struct frontend_component_scene *owner=f->component_scenes;
    while (owner && owner->identity!=identity) owner=owner->next;
    qa_q3_presentation_binding binding;
    if (!owner || !qa_q3_presentation_binding_read(recipient,&binding,e) || binding.frame!=frame ||
        binding.options.seat!=owner->request.physical_seat)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component pictures differ from their actual recipient frame");
    owner->pending_picture_frame=f->frame_number;
    owner->pending_picture_cursor=owner->picture_frame_valid && owner->picture_frame==f->frame_number?
        owner->picture_cursor:0;
    owner->pictures_pending=true;
    if (owner->pending_picture_cursor>count) return frontend_fail(e,QA_ERROR_FORMAT,"Component picture cursor exceeds its retained output");
    while (owner->pending_picture_cursor<count) {
        if (!retained(owner) || !qa_q3_presentation_completed_picture(recipient,
            owner->pictures+owner->pending_picture_cursor,frame,e)) return false;
        ++owner->pending_picture_cursor;
    }
    return retained(owner);
}
bool frontend_component_scene_pictures_finish(qa_frontend *f,uint32_t physical,bool issued,qa_error *e)
{
    if (!f || physical>=f->options.seats || f->capture || f->resource_inventory || f->frame.source_pending)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component picture publication requires returned Source issue");
    for (struct frontend_component_scene *owner=f->component_scenes;owner;owner=owner->next)
        if (owner->request.physical_seat==physical && owner->pictures_pending &&
            (owner->pending_picture_frame!=f->frame_number || owner->pending_picture_cursor>owner->picture_count))
            return frontend_fail(e,QA_ERROR_FORMAT,"Component picture publication changed its actual retained frame");
    for (struct frontend_component_scene *owner=f->component_scenes;owner;owner=owner->next) {
        if (owner->request.physical_seat!=physical || !owner->pictures_pending) continue;
        if (issued) {
            owner->picture_frame_valid=true; owner->picture_frame=owner->pending_picture_frame;
            owner->picture_cursor=owner->pending_picture_cursor;
        }
        owner->pictures_pending=false;
    }
    return true;
}
bool frontend_component_scene_movie_source_read(const qa_frontend *f,uint64_t identity,
    frontend_material_movie_source *out,qa_error *e)
{
    for (struct frontend_component_scene *owner=f?f->component_scenes:NULL;owner;owner=owner->next)
        if (owner->identity==identity && owner->ready &&
            ((f->capture || f->resource_inventory)?structural_retained(owner):retained(owner))) {
            if (!out) return false;
            *out=(frontend_material_movie_source){.frontend=owner->frontend,.files=owner->files,.images=owner->images,
                .materials=owner->materials,.media=owner->media,.context=owner,.current=movie_current};
            return true;
        }
    return frontend_fail(e,QA_ERROR_ARGUMENT,"Component movie source leaves its genuine private owner roster");
}
bool frontend_component_scene_prepare(void *context,const application_q3_component_scene_preparation *request,qa_error *e)
{
    qa_frontend *f=context;
    if (!f || !request || !request->host || !request->assets || !request->frontend ||
        !request->catalog || !request->content || !request->retained || !request->retained(request->context) ||
        !request->publication_read || !request->source.current || !request->source.actor ||
        request->physical_seat>=f->options.seats || f->options.dedicated || f->capture || f->resource_inventory ||
        request->restoring!=f->source_restoring || request->host->role!=QA_QVM_CGAME ||
        request->host->mounts!=request->content || !request->host->cvars || !request->host->console)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component renderer needs its actual private CGAME constructor receipt");
    if (request->origin==APPLICATION_Q3_COMPONENT_SCENE_LOCAL) {
        if (!request->descriptor || request->recipe || request->recipe_provider)
            return frontend_fail(e,QA_ERROR_ARGUMENT,"Local component renderer needs its actual launch declaration");
    } else if (request->origin==APPLICATION_Q3_COMPONENT_SCENE_REMOTE) {
        if (request->descriptor || !request->recipe || !request->recipe_provider ||
            !qa_executable_recipe_current(request->recipe,request->catalog))
            return frontend_fail(e,QA_ERROR_ARGUMENT,"Remote component renderer needs its actual held recipe");
        bool found=false;
        for (size_t i=0;i<qa_executable_recipe_provider_count(request->recipe);++i)
            if (qa_executable_recipe_provider(request->recipe,i)==request->recipe_provider) found=true;
        if (!found || request->recipe_provider->content!=request->content ||
            !request->artifact || !request->acquisition ||
            request->acquisition->resource_id!=qa_resource_id(request->artifact) ||
            !qa_vfs_acquisition_retained(request->content,request->acquisition,e))
            return frontend_fail(e,QA_ERROR_FORMAT,"Remote component renderer differs from its admitted recipe provider");
    } else return frontend_fail(e,QA_ERROR_FORMAT,"Unknown component renderer origin");
    if (request->retired && !request->restoring)
        return frontend_fail(e,QA_ERROR_FORMAT,"Retired component renderer needs its genuine restored request");
    struct frontend_component_scene *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(e,QA_ERROR_MEMORY,"Allocating component private renderer");
    owner->frontend=f; owner->request=*request; owner->retired=request->retired; owner->cvars=request->host->cvars; owner->command=request->host->command_context;
    owner->shadows=qa_cvars_resolve(owner->cvars,"cg_shadows");
    if (request->restoring) {
        if (request->frontend_identity<=QA_FRONTEND_COMMAND_OWNER ||
            request->frontend_identity-QA_FRONTEND_COMMAND_OWNER>f->next_source_id ||
            frontend_source_identity_used(f,request->frontend_identity)) {
            free(owner); return frontend_fail(e,QA_ERROR_FORMAT,"Component restored renderer identity leaves its saved namespace");
        }
        for (const struct frontend_component_scene *row=f->component_scenes;row;row=row->next)
            if (row->identity==request->frontend_identity) {
                free(owner); return frontend_fail(e,QA_ERROR_FORMAT,"Duplicate restored component renderer identity");
            }
        owner->identity=request->frontend_identity;
    } else if (request->frontend_identity || !frontend_source_identity_allocate(f,&owner->identity,e)) {
        free(owner); return frontend_fail(e,QA_ERROR_ARGUMENT,"Fresh component renderer needs a newly allocated physical identity");
    }
    owner->next=f->component_scenes; f->component_scenes=owner;
    *request->frontend=(application_q3_component_scene_frontend){.owner=owner,.idle=idle,.destroy=destroy,.identity_read=identity_read,.begin=begin,.finish=finish,.completed=completed,.retire=retire};
    qa_scene_frame_init(&owner->frame,owner->identity);
    if (!qa_scene_frame_material_order(&owner->frame,f->order,e) ||
        (request->descriptor && !qa_launch_instance_retain_metadata(request->descriptor,&owner->descriptor,e))) return false;
    struct frontend_component_scene_restore *saved=NULL;
    if (request->restoring) {
        struct frontend_component_scene_restore **link=&f->component_scene_restores;
        while (*link && (*link)->identity!=owner->identity) link=&(*link)->next;
        saved=*link;
        if (!saved) return frontend_fail(e,QA_ERROR_FORMAT,"Restored component renderer has no claimed private graph prefix");
        owner->files=saved->files; saved->files=NULL; owner->pool=saved->pool; saved->pool=NULL; *link=saved->next;
        constructor_policy(&owner->restored_policy,&saved->policy); owner->has_restore_policy=true;
        free(saved); saved=NULL;
    } else {
        owner->files=qa_vfs_clone(request->content,e);
        if (owner->files) { owner->pool=qa_vfs_resources(owner->files); qa_resource_pool_retain(owner->pool); }
    }
    if (!request->restoring && !frontend_q3_source_color_ensure(f,e)) return false;
    owner->images=owner->files?(request->restoring?qa_scene_resources_create_detached(owner->files,e):qa_scene_resources_create(owner->files,e)):NULL;
    if (owner->images && !request->restoring && !frontend_image_policy_initialize(f,owner->images,e)) return false;
    owner->materials=owner->images?(request->restoring?qa_material_library_create_detached(owner->images,e):qa_material_library_create(owner->images,f->order,e)):NULL;
    owner->fonts=owner->images?qa_font_library_create(owner->files,owner->images,e):NULL;
    owner->media=owner->images?qa_media_library_create(owner->images,e):NULL;
    if (!owner->files || !owner->images || !owner->materials || !owner->fonts || !owner->media ||
        !qa_audio_bank_create(owner->files,&owner->sounds,e)) return false;
    qa_scene_image_options images={.family=QA_GAME_Q3,.wrap=QA_SCENE_REPEAT,.filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=-1};
    if (!request->restoring) {
        frontend_material_movie_source movie={.frontend=f,.files=owner->files,.images=owner->images,.materials=owner->materials,
            .media=owner->media,.context=owner,.current=movie_current};
        if (!frontend_q3_material_profile_initialize(f,owner->materials,e) ||
            !frontend_material_movies_create(&movie,&owner->movies,e) ||
            !frontend_source_cinematics_ensure(f,owner->images,e) ||
            !frontend_material_movies_cinematic_attach(owner->movies,f->source_cinematics,request->physical_seat,owner->identity,e) ||
            !qa_material_library_load_scripts(owner->materials,owner->files,&images,e) ||
            !qa_material_library_source_shaders_initialize(owner->materials,&images,e)) return false;
    }
    qa_q3_presentation_asset_options assets={.provider={owner->files,owner->images,owner->materials,QA_GAME_Q3},
        .sounds=owner->sounds,.movies=owner->media,.context=owner,.print=print,.model_initialize=model_initialize};
    if (!qa_q3_presentation_assets_create(&assets,&owner->assets,e)) return false;
    qa_q3_presentation_options options={.assets=owner->assets,.audio=f->audio,.clock={owner,milliseconds},
        .source_scene_membership=true,
        .seat=request->physical_seat,.owner=owner->identity,.viewport=frontend_viewport(f,request->physical_seat),
        .near_clip=4,.far_clip=16384,.identity_light=1,.lod_scale=5,.rail_core_width=6,.rail_ring_width=16,.rail_segment_length=32,
        .context=owner,.audio_actor=audio_actor,.music=music,.frame_number=frame_number,.milliseconds=source_time,.audio_bus=audio_bus,
        .prepare_view=prepare_view,.prepare_picture=prepare_picture,.scene_completed=scene_completed,.remap=remap,.print=print,
        .picture_capture=picture_capture,
        .video_frame=frontend_material_movies_frontend_resolve,.video_context=f};
    if (owner->movies && !frontend_material_movies_cinematic_read(owner->movies,&options.cinematics,e)) return false;
    if (owner->has_restore_policy) constructor_policy(&options,&owner->restored_policy);
    if (!request->restoring && !frontend_q3_renderer_options_read(f,&options,e)) return false;
    if (!qa_q3_presentation_create(&options,&owner->presentation,e)) return false;
    owner->ready=true; owner->host_borrow=true;
    request->host->scene_resources=owner->images; request->host->scene_world=NULL; request->host->scene_frame=&owner->frame;
    request->host->sound_bank=owner->sounds; request->host->sound_mixer=f->audio?qa_audio_engine_seat_mixer(f->audio,request->physical_seat):NULL;
    request->host->presentation.context=owner; request->host->presentation.seat=owner->presentation;
    request->host->presentation.fonts=owner->fonts; request->host->presentation.configuration=configuration;
    request->host->presentation.end_registration=end_registration;
    request->host->frontend_lifetime=owner; request->host->release_frontend=release_host;
    *request->assets=owner->assets; return true;
}
