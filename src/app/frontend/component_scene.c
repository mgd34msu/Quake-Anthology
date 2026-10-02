#include "component_scene.h"
#include "shared_resource_policy.h"
#include "q3_render_policy.h"
#include "q3_color_policy.h"
#include "material_movies.h"
#include "visual_access.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/material_source_scratch.h"
#include "qa/render_controls.h"
#include "qa/binary.h"
#include <limits.h>
#include <stdio.h>

struct frontend_component_scene {
    struct frontend_component_scene *next;
    qa_frontend *frontend;
    application_q3_component_scene_preparation request;
    qa_launch_instance_lease *descriptor;
    qa_vfs *files;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_media_library *media;
    frontend_material_movies *movies;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    qa_cvars *cvars;
    qa_command_context command;
    qa_scene_frame frame;
    uint64_t identity,sequence;
    bool host_borrow,ready,begun;
};
static bool retained(const struct frontend_component_scene *owner)
{
    if (!owner || !owner->frontend || !owner->request.retained ||
        !owner->request.retained(owner->request.context)) return false;
    for (const struct frontend_component_scene *row=owner->frontend->component_scenes;row;row=row->next)
        if (row==owner) return true;
    return false;
}
static bool publication(struct frontend_component_scene *owner,application_q3_scene_context *view,qa_error *e)
{
    return retained(owner) && owner->ready && owner->host_borrow &&
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
static qa_material_source_scratch *scratch(struct frontend_component_scene *owner,qa_error *e)
{
    qa_frontend *f=owner->frontend;
    return qa_render_controls_source_scratch(f->cpu?qa_cpu_render_controls(f->cpu):qa_gl_render_controls(f->gl),e);
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
        !frontend_q3_shadow_mode_read(owner->cvars,&options->shadow_mode,e)) return false;
    options->world.no_world=true;
    options->world.source_scratch=scratch(owner,e);
    options->world.source_diagnostics_read=diagnostics;
    options->world.source_diagnostics_context=owner;
    return options->world.source_scratch && owner->request.source.current(owner->request.source.context,&view);
}
static bool prepare_picture(void *context,qa_material_context *material,qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    if (!publication(owner,&view,e)) return false;
    material->source_scratch=scratch(owner,e);
    material->source_diagnostics_read=diagnostics; material->source_diagnostics_context=owner;
    return material->source_scratch && frontend_q3_material_diagnostics_read(owner->frontend,&material->source_diagnostics,e);
}
static bool remap(void *context,const char *from,const char *to,float offset,qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    qa_material_source_remap_status status;
    return publication(owner,&view,e) && qa_material_remap_source(owner->materials,from,to,offset,&status,e) &&
        owner->request.source.current(owner->request.source.context,&view);
}
static bool configuration(void *context,uint8_t out[11332],qa_error *e)
{
    struct frontend_component_scene *owner=context; application_q3_scene_context view;
    return publication(owner,&view,e) && frontend_q3_configuration(owner->frontend,out,e) &&
        owner->request.source.current(owner->request.source.context,&view);
}
static void release_host(void *context) { ((struct frontend_component_scene *)context)->host_borrow=false; }
static bool idle(const void *context)
{
    const struct frontend_component_scene *owner=context;
    return owner && !owner->frame.source_pending && (!owner->presentation || qa_q3_presentation_idle(owner->presentation)) &&
        (!owner->movies || frontend_material_movies_idle(owner->movies));
}
static bool destroy(void **slot,qa_error *e)
{
    struct frontend_component_scene *owner=slot?*slot:NULL;
    if (!owner) return true;
    if (owner->host_borrow || !idle(owner) || owner->frontend->capture || owner->frontend->resource_inventory)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component renderer retains its real host or output borrower");
    if (owner->presentation && !qa_q3_presentation_destroy(owner->presentation,e)) return false;
    owner->presentation=NULL;
    if (!frontend_material_movies_destroy(&owner->movies,e)) return false;
    qa_scene_frame_destroy(&owner->frame);
    qa_q3_presentation_assets_destroy(owner->assets);
    qa_font_library_destroy(owner->fonts); qa_audio_bank_destroy(owner->sounds);
    qa_media_library_destroy(owner->media); qa_material_library_destroy(owner->materials);
    qa_scene_resources_destroy(owner->images); qa_vfs_destroy(owner->files);
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
    if (!retained(owner) || !owner->ready || !owner->host_borrow || !idle(owner))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component output begin needs its real returned private renderer");
    qa_scene_frame_reset(&owner->frame,sequence);
    owner->sequence=sequence; owner->begun=true;
    return qa_q3_presentation_frame(owner->presentation,&owner->frame,frontend_viewport(owner->frontend,owner->request.physical_seat),e);
}
static bool finish(void *context,bool submitted,qa_error *e)
{
    struct frontend_component_scene *owner=context;
    if (!retained(owner) || !owner->ready || !owner->host_borrow)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component output finish lost its retained entered renderer");
    return !owner->frame.source_pending ||
        qa_material_source_frame_end(owner->frame.source_pending,&owner->frame,submitted,e);
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
bool frontend_component_scene_prepare(void *context,const application_q3_component_scene_preparation *request,qa_error *e)
{
    qa_frontend *f=context;
    if (!f || !request || !request->host || !request->assets || !request->frontend || !request->descriptor ||
        !request->catalog || !request->content || !request->retained || !request->retained(request->context) ||
        !request->publication_read || !request->source.current || !request->source.actor ||
        request->physical_seat>=f->options.seats || f->options.dedicated || f->capture || f->resource_inventory ||
        request->restoring!=f->source_restoring || request->host->role!=QA_QVM_CGAME ||
        request->host->mounts!=request->content || !request->host->cvars || !request->host->console)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Component renderer needs its actual private CGAME constructor receipt");
    struct frontend_component_scene *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(e,QA_ERROR_MEMORY,"Allocating component private renderer");
    owner->frontend=f; owner->request=*request; owner->cvars=request->host->cvars; owner->command=request->host->command_context;
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
    *request->frontend=(application_q3_component_scene_frontend){.owner=owner,.idle=idle,.destroy=destroy,.identity_read=identity_read,.begin=begin,.finish=finish,.completed=completed};
    qa_scene_frame_init(&owner->frame,owner->identity);
    if (!qa_scene_frame_material_order(&owner->frame,f->order,e) ||
        !qa_launch_instance_retain_metadata(request->descriptor,&owner->descriptor,e)) return false;
    owner->files=qa_vfs_clone(request->content,e);
    if (!request->restoring && !frontend_q3_source_color_ensure(f,e)) return false;
    owner->images=owner->files?(request->restoring?qa_scene_resources_create_detached(owner->files,e):qa_scene_resources_create(owner->files,e)):NULL;
    if (owner->images && !request->restoring && !frontend_image_policy_initialize(f,owner->images,e)) return false;
    owner->materials=owner->images?(request->restoring?qa_material_library_create_detached(owner->images,e):qa_material_library_create(owner->images,f->order,e)):NULL;
    owner->fonts=owner->images?qa_font_library_create(owner->files,owner->images,e):NULL;
    owner->media=owner->images?qa_media_library_create(owner->images,e):NULL;
    if (!owner->files || !owner->images || !owner->materials || !owner->fonts || !owner->media ||
        !qa_audio_bank_create(owner->files,&owner->sounds,e)) return false;
    qa_scene_image_options images={.family=QA_SCENE_Q3,.wrap=QA_SCENE_REPEAT,.filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=-1};
    if (!request->restoring) {
        frontend_material_movie_source movie={.frontend=f,.files=owner->files,.images=owner->images,.materials=owner->materials,
            .media=owner->media,.context=owner,.current=movie_current};
        if (!frontend_q3_material_profile_initialize(f,owner->materials,e) ||
            !frontend_material_movies_create(&movie,&owner->movies,e) ||
            !qa_material_library_load_scripts(owner->materials,owner->files,&images,e)) return false;
    }
    qa_q3_presentation_asset_options assets={.provider={owner->files,owner->images,owner->materials,QA_SCENE_Q3},
        .sounds=owner->sounds,.movies=owner->media,.context=owner,.print=print,.model_initialize=model_initialize};
    if (!qa_q3_presentation_assets_create(&assets,&owner->assets,e)) return false;
    qa_q3_presentation_options options={.assets=owner->assets,.audio=f->audio,.clock={owner,milliseconds},
        .seat=request->physical_seat,.owner=owner->identity,.viewport=frontend_viewport(f,request->physical_seat),
        .near_clip=4,.far_clip=16384,.identity_light=1,.lod_scale=5,.rail_core_width=6,.rail_ring_width=16,.rail_segment_length=32,
        .context=owner,.audio_actor=audio_actor,.frame_number=frame_number,.milliseconds=source_time,.audio_bus=audio_bus,
        .prepare_view=prepare_view,.prepare_picture=prepare_picture,.remap=remap,.print=print,
        .video_frame=frontend_material_movies_frontend_resolve,.video_context=f};
    if (!request->restoring && !frontend_q3_renderer_options_read(f,&options,e)) return false;
    if (!qa_q3_presentation_create(&options,&owner->presentation,e)) return false;
    owner->ready=true; owner->host_borrow=true;
    request->host->scene_resources=owner->images; request->host->scene_world=NULL; request->host->scene_frame=&owner->frame;
    request->host->sound_bank=owner->sounds; request->host->sound_mixer=f->audio?qa_audio_engine_seat_mixer(f->audio,request->physical_seat):NULL;
    request->host->presentation.context=owner; request->host->presentation.seat=owner->presentation;
    request->host->presentation.fonts=owner->fonts; request->host->presentation.configuration=configuration;
    request->host->frontend_lifetime=owner; request->host->release_frontend=release_host;
    *request->assets=owner->assets; return true;
}
