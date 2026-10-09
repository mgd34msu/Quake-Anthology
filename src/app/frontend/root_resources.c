#include "root_resources.h"
#include "renderer_materials.h"
#include "qa/scene_world_save.h"
#include "shared_resource_policy.h"
#include "q3_render_policy.h"
#include "q1_sky.h"
#include "native_q3_client.h"
#include "qa/map_sidecars.h"
#include "source_acoustics.h"

bool frontend_world_scratch_create(const qa_scene_world *world, frontend_world_scratch *out, qa_error *error)
{
    frontend_world_scratch next = {0};
    if (!qa_scene_world_scratch_create(world, &next.view, error) ||
        !qa_scene_world_scratch_create(world, &next.child, error)) {
        frontend_world_scratch_destroy(&next);
        return false;
    }
    frontend_world_scratch_destroy(out);
    *out = next;
    return true;
}
void frontend_world_scratch_destroy(frontend_world_scratch *scratch)
{
    qa_scene_world_scratch_destroy(scratch->child);
    qa_scene_world_scratch_destroy(scratch->view);
    *scratch = (frontend_world_scratch){0};
}

struct frontend_root_resources {
    qa_frontend *frontend;
    qa_application *application;
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_media_library *media;
    frontend_material_movies *movies;
    qa_scene_world *world;
    frontend_world_scratch scratch[4];
    qa_audio_bank *sounds;
    qa_resource *map;
    qa_map_sidecars *sidecars;
    char *name;
    uint64_t configuration,revision;
    bool installed;
};
static bool current(void *context,const frontend_material_movie_source *source)
{
    frontend_root_resources *owner=context;
    qa_frontend *f=owner?owner->frontend:NULL;
    if (!f || f->application!=owner->application || !source || source->frontend!=f ||
        source->context!=owner || source->files!=owner->mounts || source->images!=owner->images ||
        source->materials!=owner->materials || source->media!=owner->media) return false;
    if (owner->installed)
        return f->root_resources==owner && f->mounts==owner->mounts &&
            f->images==owner->images && f->materials==owner->materials;
    qa_application_map_view map;
    return f->root_resources_pending==owner && owner->map &&
        qa_application_map_read(f->application,&map) && map.resource==owner->map &&
        map.revision==owner->revision &&
        qa_application_configuration_generation(f->application)==owner->configuration;
}
static frontend_material_movie_source source_view(frontend_root_resources *owner)
{
    return (frontend_material_movie_source){.frontend=owner->frontend,.files=owner->mounts,
        .images=owner->images,.materials=owner->materials,.media=owner->media,
        .context=owner,.current=current};
}
static bool dispose(frontend_root_resources **slot,qa_error *error)
{
    frontend_root_resources *owner=*slot;
    if (!owner) return true;
    qa_frontend *f=owner->frontend;
    if (owner->installed && (f->root_resources!=owner || f->mounts!=owner->mounts ||
        f->images!=owner->images || f->materials!=owner->materials))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Root resources lost their actual installed destructor association");
    if (owner->movies && owner->media && !f->source_restoring) {
        frontend_material_movie_source expected=source_view(owner);
        if (!frontend_renderer_materials_adopt_movies(f,&expected,&owner->movies,&owner->media,error)) return false;
    }
    if (!frontend_material_movies_destroy(&owner->movies,error)) return false;
    qa_media_library_destroy(owner->media); owner->media=NULL;
    for (unsigned i=0;i<4;++i) frontend_world_scratch_destroy(owner->scratch+i);
    if (owner->installed) {
        if (f->root_resources!=owner || f->mounts!=owner->mounts ||
            f->images!=owner->images || f->materials!=owner->materials)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Root resources lost their actual installed destructor association");
        qa_scene_world_destroy(f->scene_world); f->scene_world=NULL; f->world_scratch=NULL;
        qa_audio_bank_destroy(f->sounds); f->sounds=NULL;
        qa_resource_release(f->map_resource); f->map_resource=NULL;
        free(f->map_name); f->map_name=NULL;
        f->mounts=NULL; f->images=NULL; f->materials=NULL;
    } else {
        qa_scene_world_destroy(owner->world);
        qa_audio_bank_destroy(owner->sounds);
        qa_resource_release(owner->map);
        free(owner->name);
    }
    qa_material_library_destroy(owner->materials);
    qa_scene_resources_destroy(owner->images);
    qa_vfs_destroy(owner->mounts);
    qa_map_sidecars_release(owner->sidecars);
    *slot=NULL; free(owner); return true;
}
bool frontend_root_resources_destroy(qa_frontend *f,qa_error *error)
{
    if (!f || f->capture || f->resource_inventory)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Root resource retirement overlaps actual capture");
    return dispose(&f->root_resources_pending,error) && dispose(&f->root_resources,error);
}
bool frontend_root_movie_source_read(qa_frontend *f,frontend_material_movie_source *out,qa_error *error)
{
    frontend_root_resources *owner=f?f->root_resources:NULL;
    if (!owner || !out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Root movies require their genuine retained provider");
    *out=source_view(owner);
    return current(owner,out) || frontend_fail(error,QA_ERROR_ARGUMENT,"Root movie provider left its actual namespace");
}
bool frontend_root_resources_prepare_restored(qa_frontend *f,qa_error *error)
{
    if (!f || !f->source_restoring || f->root_resources || f->root_resources_pending ||
        !f->mounts || !f->images || !f->materials)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Root media import requires the actual detached provider banks");
    frontend_root_resources *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining imported root media provider");
    owner->frontend=f; owner->application=f->application; owner->mounts=f->mounts;
    owner->images=f->images; owner->materials=f->materials; owner->installed=true;
    if (f->scene_world) {
        for (unsigned i=0;i<f->options.seats;++i)
            if (!frontend_world_scratch_create(f->scene_world,owner->scratch+i,error)) {
                for (unsigned j=0;j<i;++j) frontend_world_scratch_destroy(owner->scratch+j);
                free(owner); return false;
            }
        f->world_scratch=owner->scratch;
    }
    const qa_map_sidecars *sidecars=qa_application_map_sidecars(f->application);
    if (sidecars) {
        owner->sidecars=(qa_map_sidecars *)sidecars; qa_map_sidecars_retain(owner->sidecars);
    }
    f->root_resources=owner;
    owner->media=qa_media_library_create(owner->images,error);
    return owner->media!=NULL;
}
bool frontend_root_sidecars_bind_restored(qa_frontend *f,qa_error *error)
{
    frontend_root_resources *owner=f?f->root_resources:NULL;
    if (!f || !f->source_restoring || !owner || owner->frontend!=f || owner->application!=f->application)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Restored root sidecars lost their actual detached parent");
    const qa_map_sidecars *sidecars=qa_application_map_sidecars(f->application);
    if (!f->map_resource) return !sidecars && !owner->sidecars;
    if (!sidecars || !qa_map_sidecars_current(sidecars) || qa_map_sidecars_map(sidecars)!=f->map_resource ||
        (owner->sidecars && owner->sidecars!=sidecars))
        return frontend_fail(error,QA_ERROR_FORMAT,"Restored root sidecars differ from the prepared application map");
    if (!owner->sidecars) {
        owner->sidecars=(qa_map_sidecars *)sidecars; qa_map_sidecars_retain(owner->sidecars);
    }
    return true;
}
bool frontend_root_resources_sync(qa_frontend *f,qa_error *error)
{
    qa_application_map_view map;
    if (!qa_application_map_read(f->application,&map)) return true;
    uint64_t configuration=qa_application_configuration_generation(f->application);
    if (f->configuration==configuration && f->map_revision==map.revision) return true;
    if (f->root_resources_pending)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Root world construction retains an unfinished provider");
    const qa_launch_snapshot *snapshot=qa_application_launch(f->application);
    const qa_product *product=qa_catalog_product(qa_launch_snapshot_catalog(snapshot),map.geometry);
    if (!product) return frontend_fail(error,QA_ERROR_ARGUMENT,"Root world has no actual geometry product");
    frontend_root_resources *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining root world construction");
    owner->frontend=f; owner->application=f->application; owner->configuration=configuration;
    owner->revision=map.revision; owner->map=map.resource; qa_resource_retain(owner->map);
    f->root_resources_pending=owner;
    const qa_map_sidecars *sidecars=qa_application_map_sidecars(f->application);
    if (!sidecars || !qa_map_sidecars_current(sidecars) || qa_map_sidecars_map(sidecars)!=map.resource ||
        qa_map_sidecars_product(sidecars)!=map.geometry) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"Root world lost its actual admitted map sidecars"); goto fail;
    }
    owner->sidecars=(qa_map_sidecars *)sidecars; qa_map_sidecars_retain(owner->sidecars);
    owner->mounts=qa_vfs_clone(qa_launch_snapshot_mounts(snapshot),error);
    owner->images=owner->mounts?qa_scene_resources_create(owner->mounts,error):NULL;
    if (!owner->images || !frontend_image_policy_initialize(f,owner->images,error)) goto fail;
    owner->materials=qa_material_library_create(owner->images,f->order,error);
    owner->media=qa_media_library_create(owner->images,error);
    frontend_material_movie_source source=source_view(owner);
    if (!owner->materials || !owner->media || !frontend_material_movies_create(&source,&owner->movies,error)) goto fail;
    qa_scene_family family=product->family==QA_GAME_Q3?QA_SCENE_Q3:
        product->family==QA_GAME_Q2?QA_SCENE_Q2:QA_SCENE_Q1;
    qa_scene_world_options options={.images={.family=family,.wrap=QA_SCENE_REPEAT,
        .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=255},
        .subdivisions=64,.q1_water_alpha=1,.q2_light_modulate=1,.q3_overbright=1,
        .external_lit=qa_map_sidecars_external_lit(owner->sidecars)};
    qa_bsp_view bsp;
    options.has_external_entities=qa_map_sidecars_external_entities(owner->sidecars,&options.external_entities);
    if (!qa_bsp_open(qa_resource_bytes(owner->map),&bsp,error) ||
        !qa_map_sidecars_apply_entities(owner->sidecars,&bsp,error) ||
        !qa_material_library_load_scripts(owner->materials,owner->mounts,&options.images,error) ||
        !qa_scene_world_create(&bsp,owner->images,owner->materials,&options,&owner->world,error) ||
        !qa_scene_world_source_resource_bind(owner->world,owner->map,error) ||
        (f->audio && !qa_audio_bank_create(owner->mounts,&owner->sounds,error))) goto fail;
    for (unsigned i=0;i<f->options.seats;++i)
        if (!frontend_world_scratch_create(owner->world,owner->scratch+i,error)) goto fail;
    size_t length=strlen(map.name);
    owner->name=malloc(length+1);
    if (!owner->name) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining render map identity"); goto fail; }
    memcpy(owner->name,map.name,length+1);
    if (!frontend_native_q3_retire_ready(f,error) || !frontend_source_retire_world(f,error) ||
        !frontend_native_q3_retire_world(f,error)) goto fail;
    qa_scene_frame_reset(&f->frame,f->frame_number);
    if (!dispose(&f->root_resources,error)) goto fail;
    /* Older imported graphs without a media child still own this exact tuple. */
    qa_scene_world_destroy(f->scene_world); qa_audio_bank_destroy(f->sounds);
    qa_resource_release(f->map_resource); free(f->map_name);
    qa_material_library_destroy(f->materials); qa_scene_resources_destroy(f->images); qa_vfs_destroy(f->mounts);
    f->mounts=owner->mounts; f->images=owner->images; f->materials=owner->materials;
    f->scene_world=owner->world; f->sounds=owner->sounds; f->map_resource=owner->map; f->map_name=owner->name;
    f->world_scratch=owner->scratch;
    f->configuration=configuration; f->map_revision=map.revision;
    owner->installed=true; f->root_resources=owner; f->root_resources_pending=NULL;
    return (!f->q1_sky || frontend_q1_sky_map(f->q1_sky,error)) &&
        frontend_material_remaps(f,f->materials,error) && frontend_source_publish_world(f,error) &&
        frontend_native_q3_publish_world(f,error) && frontend_acoustics_source_bind(f,error);
fail: {
    qa_error cleanup={0};
    (void)dispose(&f->root_resources_pending,&cleanup);
    return false;
}
}
