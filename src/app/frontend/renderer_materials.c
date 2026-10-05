#include "renderer_materials.h"
#include "renderer_registries.h"
#include "visual_restore.h"
#include "native_q3_client.h"
#include "native_q3_video.h"
#include "video_guests.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "remote_q1_client.h"
#include "remote_q2_client.h"
#include "unified_media_inventory.h"
#include "component_scene.h"
#include "equipment_media.h"
#include "source_cinematics.h"
#include "qa/material_library_save.h"
#include "qa/scene_resource_save.h"
#include "qa/scene_world_save.h"
#include "qa/material_source_scratch.h"
#include "qa/render_controls.h"
#include "qa/source_save.h"
#include "q3_render_policy.h"

struct frontend_renderer_materials {
    struct frontend_renderer_materials *next;
    qa_frontend *frontend;
    qa_application *application;
    frontend_renderer_materials_view view;
    const qa_material_library *target;
    qa_resource_pool *pool,*lightmap_pool;
};
static bool movie_current(void *context,const frontend_material_movie_source *source)
{
    frontend_renderer_materials *owner=context;
    bool linked=false;
    for(frontend_renderer_materials *row=owner?owner->frontend->renderer_materials:NULL;row;row=row->next)
        linked=linked || row==owner;
    return linked && source &&
        owner->frontend->application==owner->application && source->frontend==owner->frontend &&
        source->files==owner->view.mounts && source->images==owner->view.images &&
        source->materials==owner->view.library && source->media==owner->view.media &&
        source->context==owner && source->current==movie_current && owner->view.media &&
        (owner->view.movies || owner->frontend->source_restoring);
}
static frontend_material_movie_source movie_source(frontend_renderer_materials *owner)
{
    return (frontend_material_movie_source){.frontend=owner->frontend,.files=owner->view.mounts,
        .images=owner->view.images,.materials=owner->view.library,.media=owner->view.media,
        .context=owner,.current=movie_current};
}
static bool renderer_library(const qa_frontend *f,qa_material_library **out,qa_error *error)
{
    if(f->cpu && f->gl) return frontend_fail(error,QA_ERROR_FORMAT,"Retained shader has two physical renderers");
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    if(!controls) { *out=NULL; return true; }
    const qa_material_source_scratch *scratch=qa_render_controls_source_metadata(controls,error);
    const qa_material *material=NULL;
    if(!scratch || !qa_material_source_material_metadata(scratch,&material,error)) return false;
    *out=material?material->library:NULL; return true;
}
static bool renderer_holds_library(const qa_frontend *f,const qa_material_library *library,bool *held,qa_error *error)
{
    if(f->cpu && f->gl) return false;
    const qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    *held=false;
    if(!controls || !library) return true;
    const qa_material_source_scratch *source=qa_render_controls_source_metadata(controls,error);
    if(!source) return false;
    *held=qa_material_source_holds_library(source,library); return true;
}
static bool parent_library(const qa_frontend *f,const qa_material_library *library,bool *found,qa_error *error)
{
    *found=library==f->materials;
    for(size_t i=0;!*found && i<frontend_source_group_count(f);++i) {
        frontend_source_group_view row;
        if(!(frontend_video_guests_resources_associated(f,frontend_video_guests_read(f))?
            frontend_source_group_video_read(f,i,&row,frontend_video_guests_read(f)):frontend_source_group_read(f,i,&row))) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_visual_owner_count(f);++i) {
        frontend_visual_owner_view row;
        if(!frontend_visual_owner_read(f,i,&row)) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_equipment_media_count(f);++i) {
        frontend_equipment_media_view row;
        if(!frontend_equipment_media_at(f,i,&row)) return false;
        *found=library==row.owner.materials;
    }
    for(size_t i=0;!*found && i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        if(!frontend_component_scene_metadata_read(f,i,&row,error)) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view row;
        if(!(frontend_video_guests_resources_associated(f,frontend_video_guests_read(f))?
            frontend_native_q3_video_read(f,i,&row,error):frontend_native_q3_read(f,i,&row,error))) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources row;
        if(!(f->resource_inventory?frontend_remote_q3_resources_metadata_read(frontend_remote_q3_at(f,i),&row,error):
            frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&row,error))) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_remote_q3_initial_count(f);++i) {
        frontend_remote_q3_initial_view row;
        if(!(f->resource_inventory?frontend_remote_q3_initial_metadata_read(frontend_remote_q3_initial_at(f,i),&row,error):
            frontend_remote_q3_initial_read(frontend_remote_q3_initial_at(f,i),&row,error))) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1_media row;
        if(!frontend_remote_q1_media_read(frontend_remote_q1_at(f,i),&row,error)) return false;
        *found=library==row.materials;
    }
    for(size_t i=0;!*found && i<frontend_remote_q2_count(f);++i) {
        frontend_remote_q2_view row;
        if(!frontend_remote_q2_metadata_read(frontend_remote_q2_at(f,i),&row,error)) return false;
        *found=library==row.materials;
    }
    size_t unified_count=0;
    if(!frontend_unified_media_inventory_count(f,&unified_count,error)) return false;
    for(size_t i=0;!*found && i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(f,i,&media,error)) return false;
        for(size_t j=0;!*found && media && j<frontend_unified_media_bank_count(media);++j) {
            frontend_unified_bank_view bank;
            if(!frontend_unified_media_bank_read(media,j,&bank)) return false;
            *found=library==bank.materials;
        }
    }
    return true;
}
static bool renderer_lightmap(const qa_frontend *f,const qa_scene_image **out,qa_error *error)
{
    if(f->cpu && f->gl) return frontend_fail(error,QA_ERROR_FORMAT,"Retained lightmap has two physical renderers");
    const qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    if(!controls) { *out=NULL; return true; }
    const qa_material_source_scratch *source=qa_render_controls_source_metadata(controls,error);
    return source && qa_material_source_lightmap_metadata(source,out,error);
}
static bool parent_images(const qa_frontend *f,const qa_scene_resources *images,bool *found,qa_error *error)
{
    *found=images==f->images || images==f->ui_images;
    const qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    if(!*found && controls) {
        const qa_material_source_scratch *source=qa_render_controls_source_metadata(controls,error);
        if(!source) return false;
        for(size_t i=0;!*found && i<qa_material_source_world_count(source);++i) {
            const qa_scene_world *world=qa_material_source_world_at(source,i);
            if(!world) return false;
            *found=qa_scene_world_resource_owner(world)==images;
        }
    }
    for(size_t i=0;!*found && i<frontend_renderer_registries_count(f);++i) {
        qa_q3_presentation_assets *assets=NULL;
        if(!frontend_renderer_registries_at(f,i,&assets,error)) return false;
        for(size_t j=0;!*found && j<qa_q3_assets_map_count(assets);++j) {
            qa_q3_asset_map_custody map={0};
            if(!qa_q3_assets_map_at(assets,j,&map) || !map.world) return false;
            *found=qa_scene_world_resource_owner(map.world)==images;
        }
    }
    for(size_t i=0;!*found && i<frontend_source_group_count(f);++i) {
        frontend_source_group_view row;
        if(!(frontend_video_guests_resources_associated(f,frontend_video_guests_read(f))?
            frontend_source_group_video_read(f,i,&row,frontend_video_guests_read(f)):frontend_source_group_read(f,i,&row))) return false;
        *found=images==row.images;
    }
    for(unsigned kind=0;!*found && kind<3;++kind) for(size_t i=0;!*found;++i) {
        const qa_scene_resources *row=kind==0?frontend_event_images_at((qa_frontend *)f,i):
            kind==1?frontend_visual_images_at((qa_frontend *)f,i):frontend_native_q2_images_at((qa_frontend *)f,i);
        if(!row) break;
        *found=images==row;
    }
    for(size_t i=0;!*found && i<frontend_equipment_media_count(f);++i) {
        frontend_equipment_media_view row;
        if(!frontend_equipment_media_at(f,i,&row)) return false;
        *found=images==row.owner.images;
    }
    for(size_t i=0;!*found && i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        if(!frontend_component_scene_metadata_read(f,i,&row,error)) return false;
        *found=images==row.images;
    }
    for(size_t i=0;!*found && i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view row;
        if(!(frontend_video_guests_resources_associated(f,frontend_video_guests_read(f))?
            frontend_native_q3_video_read(f,i,&row,error):frontend_native_q3_read(f,i,&row,error))) return false;
        *found=images==row.images;
    }
    for(size_t i=0;!*found && i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources row;
        if(!(f->resource_inventory?frontend_remote_q3_resources_metadata_read(frontend_remote_q3_at(f,i),&row,error):
            frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&row,error))) return false;
        *found=images==row.images;
    }
    for(size_t i=0;!*found && i<frontend_remote_q3_initial_count(f);++i) {
        frontend_remote_q3_initial_view row;
        if(!(f->resource_inventory?frontend_remote_q3_initial_metadata_read(frontend_remote_q3_initial_at(f,i),&row,error):
            frontend_remote_q3_initial_read(frontend_remote_q3_initial_at(f,i),&row,error))) return false;
        *found=images==row.images;
    }
    for(size_t i=0;!*found && i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1_media row;
        if(!frontend_remote_q1_media_read(frontend_remote_q1_at(f,i),&row,error)) return false;
        *found=images==row.images;
    }
    for(size_t i=0;!*found && i<frontend_remote_q2_count(f);++i) {
        frontend_remote_q2_view row;
        if(!frontend_remote_q2_metadata_read(frontend_remote_q2_at(f,i),&row,error)) return false;
        *found=images==row.images;
    }
    size_t unified_count=0;
    if(!frontend_unified_media_inventory_count(f,&unified_count,error)) return false;
    for(size_t i=0;!*found && i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(f,i,&media,error)) return false;
        for(size_t j=0;!*found && media && j<frontend_unified_media_bank_count(media);++j) {
            frontend_unified_bank_view bank;
            if(!frontend_unified_media_bank_read(media,j,&bank)) return false;
            *found=images==bank.images;
        }
    }
    return true;
}
typedef struct retained_rows {
    frontend_renderer_materials_view *rows;
    size_t count;
} retained_rows;
static bool rows_append(retained_rows *rows,frontend_renderer_materials_view value,qa_error *error)
{
    if(rows->count==SIZE_MAX/sizeof(*rows->rows)) return false;
    void *next=realloc(rows->rows,(rows->count+1)*sizeof(*rows->rows));
    if(!next) return frontend_fail(error,QA_ERROR_MEMORY,"Reading retained renderer custody rows");
    rows->rows=next; rows->rows[rows->count++]=value; return true;
}
static bool rows_library(const qa_frontend *f,retained_rows *rows,qa_material_library *library,qa_error *error)
{
    if(!library) return true;
    for(size_t i=0;i<rows->count;++i) if(rows->rows[i].library==library) return true;
    bool known=false;
    if(!parent_library(f,library,&known,error)) return false;
    if(known) return true;
    qa_scene_resources *images=qa_material_library_resource_owner(library);
    qa_vfs *mounts=images?qa_scene_resources_files(images):NULL;
    if(!images || !mounts) return frontend_fail(error,QA_ERROR_FORMAT,"Retained shader lost its actual bank and VFS");
    frontend_renderer_materials_view value={.library=library,.images=images,.mounts=mounts};
    for(frontend_renderer_materials *owner=f->renderer_materials;owner;owner=owner->next)
        if(owner->view.library==library) { value=owner->view; break; }
    if (value.movies && !frontend_material_movies_cinematic_namespace_read(value.movies,
        &value.cinematic_seat,&value.cinematic_bus,&value.has_cinematics,error)) return false;
    return rows_append(rows,value,error);
}
static bool rows_bank(const qa_frontend *f,retained_rows *rows,qa_scene_resources *images,qa_error *error)
{
    if (!images) return true;
    for (size_t i=0;i<rows->count;++i)
        if (rows->rows[i].images==images || rows->rows[i].lightmap_images==images) return true;
    bool known=false;
    if (!parent_images(f,images,&known,error)) return false;
    if (known) return true;
    qa_vfs *files=qa_scene_resources_files(images);
    return files && rows_append(rows,(frontend_renderer_materials_view){.lightmap_images=images,.lightmap_mounts=files},error);
}
static bool renderer_bank_held(const qa_frontend *f,const qa_scene_resources *images,bool *held,qa_error *error)
{
    *held=false;
    const qa_scene_image *lightmap=NULL;
    if (!renderer_lightmap(f,&lightmap,error)) return false;
    *held=lightmap && qa_scene_image_resource_owner(lightmap)==images;
    if (f->source_cinematics) {
        qa_q3_cinematic_handles_options pool;
        if (!frontend_source_cinematics_read(f,&pool,error)) return false;
        *held=*held || pool.images==images;
    }
    const qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    for (unsigned kind=0;controls && kind<2 && !*held;++kind) {
        size_t count=0;
        if (!(kind?qa_render_controls_source_texture_metadata(controls,&count,error):
            qa_render_controls_source_images_metadata(controls,&count,error))) return false;
        for (size_t i=0;i<count && !*held;++i) {
            const qa_scene_image *image=NULL;
            if (!(kind?qa_render_controls_source_texture_level_metadata(controls,i,&image,error):
                qa_render_controls_source_image_metadata(controls,i,&image,error))) return false;
            *held=image && qa_scene_image_resource_owner(image)==images;
        }
    }
    return true;
}
static bool rows_read(const qa_frontend *f,retained_rows *rows,qa_error *error)
{
    if(!f) return false;
    if(f->source_restoring) {
        for(frontend_renderer_materials *owner=f->renderer_materials;owner;owner=owner->next) {
            const frontend_renderer_materials_view *v=&owner->view;
            if(owner->frontend!=f || owner->application!=f->application ||
                (!!v->library!=!!v->images) || (!!v->images!=!!v->mounts) ||
                (!!v->lightmap_images!=!!v->lightmap_mounts) || (!v->library && !v->lightmap_images) ||
                !rows_append(rows,*v,error)) return false;
        }
        return true;
    }
    /* Existing capsules keep chronological custody; uncapsulated physical
     * roots follow the renderer's genuine registry enumeration. */
    for(frontend_renderer_materials *owner=f->renderer_materials;owner;owner=owner->next) {
        bool held=false;
        if(!renderer_holds_library(f,owner->view.library,&held,error)) return false;
        if((held || frontend_material_movies_cinematic_retained(owner->view.movies)) &&
            !rows_library(f,rows,owner->view.library,error)) return false;
    }
    qa_material_library *library=NULL;
    if(!renderer_library(f,&library,error) || !rows_library(f,rows,library,error)) return false;
    const qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    if(controls) {
        const qa_material_source_scratch *source=qa_render_controls_source_metadata(controls,error);
        if(!source) return false;
        for(size_t i=0;i<qa_material_source_library_count(source);++i) {
            const qa_material_library *held=qa_material_source_library_at(source,i);
            if(!held || !rows_library(f,rows,(qa_material_library *)held,error)) return false;
        }
    }
    const qa_scene_image *lightmap=NULL;
    if(!renderer_lightmap(f,&lightmap,error) ||
        !rows_bank(f,rows,lightmap?qa_scene_image_resource_owner(lightmap):NULL,error)) return false;
    for(unsigned kind=0;controls && kind<2;++kind) {
        size_t count=0;
        if(!(kind?qa_render_controls_source_texture_metadata(controls,&count,error):
            qa_render_controls_source_images_metadata(controls,&count,error))) return false;
        for(size_t i=0;i<count;++i) {
            const qa_scene_image *image=NULL;
            if(!(kind?qa_render_controls_source_texture_level_metadata(controls,i,&image,error):
                qa_render_controls_source_image_metadata(controls,i,&image,error)) ||
                !rows_bank(f,rows,image?qa_scene_image_resource_owner(image):NULL,error)) return false;
        }
    }
    if(f->source_cinematics) {
        qa_q3_cinematic_handles_options pool;
        if(!frontend_source_cinematics_read(f,&pool,error) || !rows_bank(f,rows,pool.images,error)) return false;
    }
    return true;
}
bool frontend_renderer_materials_count(const qa_frontend *f,size_t *out,qa_error *error)
{
    retained_rows rows={0}; bool okay=out && rows_read(f,&rows,error);
    if(okay) *out=rows.count;
    free(rows.rows); return okay;
}
bool frontend_renderer_materials_read_at(const qa_frontend *f,size_t ordinal,frontend_renderer_materials_view *out,qa_error *error)
{
    retained_rows rows={0}; bool okay=out && rows_read(f,&rows,error) && ordinal<rows.count;
    if(okay) *out=rows.rows[ordinal];
    free(rows.rows); return okay;
}
bool frontend_renderer_materials_adopt_movies(qa_frontend *f,const frontend_material_movie_source *expected,
    frontend_material_movies **movies,qa_media_library **media,qa_error *error)
{
    if(!f || !expected || !movies || !media || expected->frontend!=f || expected->media!=*media ||
        !*movies || !*media || f->source_restoring || f->capture || f->resource_inventory) return false;
    qa_material_library *library=expected->materials; bool held=false,cinematic=false;
    uint32_t cinematic_seat=0; uint64_t cinematic_bus=0;
    if (!frontend_material_movies_cinematic_namespace_read(*movies,&cinematic_seat,&cinematic_bus,&cinematic,error)) return false;
    if(!renderer_holds_library(f,library,&held,error)) return false;
    if(!held && !frontend_material_movies_cinematic_retained(*movies)) return true;
    if(!expected->images || !expected->files || qa_material_library_resource_owner(library)!=expected->images ||
        qa_scene_resources_files(expected->images)!=expected->files) return false;
    frontend_renderer_materials *owner=f->renderer_materials;
    while(owner && owner->view.library!=library && owner->target!=library) owner=owner->next;
    if(owner && (owner->view.media || owner->view.movies ||
        (owner->view.images && owner->view.images!=expected->images) ||
        (owner->view.mounts && owner->view.mounts!=expected->files)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained shader already has another media custody owner");
    if(!owner) {
        owner=calloc(1,sizeof(*owner));
        if(!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining retiring shader movie custody");
        owner->frontend=f; owner->application=f->application; owner->target=library;
        frontend_renderer_materials **tail=&f->renderer_materials;
        while(*tail) tail=&(*tail)->next;
        *tail=owner;
    }
    if(!owner->view.library) {
        if(!qa_material_library_retain(library,error)) return false;
        owner->view.library=library;
    }
    if(!owner->view.images) {
        if(!qa_scene_resources_retain(expected->images,error)) return false;
        owner->view.images=expected->images;
    }
    if(!owner->view.mounts) {
        if(!qa_vfs_retain(expected->files,error)) return false;
        owner->view.mounts=expected->files;
    }
    if(!owner->pool) {
        owner->pool=qa_vfs_resources(expected->files); qa_resource_pool_retain(owner->pool);
    }
    owner->view.media=*media; owner->view.movies=*movies;
    owner->view.has_cinematics=cinematic; owner->view.cinematic_seat=cinematic_seat; owner->view.cinematic_bus=cinematic_bus;
    frontend_material_movie_source destination=movie_source(owner);
    if(!frontend_material_movies_transfer(movies,expected,&destination,&owner->view.movies,error)) {
        owner->view.media=NULL; owner->view.movies=NULL; return false;
    }
    *media=NULL; return true;
}
static frontend_renderer_materials *movie_at(qa_frontend *f,size_t ordinal,qa_error *error)
{
    retained_rows rows={0}; frontend_renderer_materials *result=NULL;
    if(rows_read(f,&rows,error)) for(size_t i=0;i<rows.count;++i) {
        if(!rows.rows[i].media) continue;
        if(ordinal) { --ordinal; continue; }
        for(frontend_renderer_materials *owner=f->renderer_materials;owner;owner=owner->next)
            if(owner->view.library==rows.rows[i].library && owner->view.media==rows.rows[i].media) {
                result=owner; break;
            }
        break;
    }
    free(rows.rows); return result;
}
bool frontend_renderer_materials_movie_count(const qa_frontend *f,size_t *out,qa_error *error)
{
    retained_rows rows={0}; bool okay=out && rows_read(f,&rows,error); size_t count=0;
    for(size_t i=0;okay && i<rows.count;++i) count+=rows.rows[i].media!=NULL;
    if(okay) *out=count;
    free(rows.rows); return okay;
}
bool frontend_renderer_materials_movie_source_at(qa_frontend *f,size_t ordinal,frontend_material_movie_source *out,qa_error *error)
{
    frontend_renderer_materials *owner=movie_at(f,ordinal,error);
    if(!owner || !out || !owner->view.library)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained shader movies have no actual custody capsule");
    *out=movie_source(owner);
    return owner->frontend==f && owner->application==f->application && out->files && out->images &&
        ((f->source_restoring && !owner->view.movies) || movie_current(owner,out));
}
static bool row_destroy(frontend_renderer_materials **slot,qa_error *error)
{
    frontend_renderer_materials *owner=*slot;
    if(owner->frontend->capture || owner->frontend->resource_inventory ||
        (owner->view.library && !qa_material_library_idle(owner->view.library)) ||
        (owner->view.images && !qa_scene_resources_idle(owner->view.images)) ||
        (owner->view.lightmap_images && !qa_scene_resources_idle(owner->view.lightmap_images)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained renderer material owners remain borrowed");
    if(!frontend_material_movies_destroy(&owner->view.movies,error)) return false;
    qa_media_library_destroy(owner->view.media); owner->view.media=NULL;
    qa_material_library_destroy(owner->view.library);
    qa_scene_resources_destroy(owner->view.images);
    qa_vfs_destroy(owner->view.mounts); qa_resource_pool_destroy(owner->pool);
    qa_scene_resources_destroy(owner->view.lightmap_images);
    qa_vfs_destroy(owner->view.lightmap_mounts); qa_resource_pool_destroy(owner->lightmap_pool);
    *slot=owner->next; free(owner); return true;
}
bool frontend_renderer_materials_destroy(frontend_renderer_materials **out,qa_error *error)
{
    if(!out) return true;
    while(*out) if(!row_destroy(out,error)) return false;
    return true;
}
bool frontend_renderer_materials_idle(const frontend_renderer_materials *owner)
{
    for(;owner;owner=owner->next)
        if((owner->view.library && !qa_material_library_idle(owner->view.library)) ||
            (owner->view.images && !qa_scene_resources_idle(owner->view.images)) ||
            (owner->view.lightmap_images && !qa_scene_resources_idle(owner->view.lightmap_images)) ||
            (owner->view.movies && !frontend_material_movies_idle(owner->view.movies))) return false;
    return true;
}
bool frontend_renderer_materials_prune(qa_frontend *f,qa_error *error)
{
    if(!f || f->source_restoring) return f!=NULL;
    frontend_renderer_materials **slot=&f->renderer_materials;
    while(*slot) {
        bool held=false;
        if(!renderer_holds_library(f,(*slot)->view.library,&held,error)) return false;
        bool bank_held=false;
        if((*slot)->view.lightmap_images && !renderer_bank_held(f,(*slot)->view.lightmap_images,&bank_held,error)) return false;
        if(held || bank_held || frontend_material_movies_cinematic_retained((*slot)->view.movies)) slot=&(*slot)->next;
        else if(!row_destroy(slot,error)) return false;
    }
    return true;
}
typedef struct saved_row {
    bool shader,lightmap,movies,cinematics;
    uint32_t seat;
    uint64_t view,pool,lightmap_view,lightmap_pool;
} saved_row;
static bool row_fields(qa_source_save_io *io,saved_row *row)
{
    return qa_source_save_bool(io,&row->shader) && qa_source_save_u64(io,&row->view) &&
        qa_source_save_u64(io,&row->pool) && qa_source_save_bool(io,&row->lightmap) &&
        qa_source_save_u64(io,&row->lightmap_view) && qa_source_save_u64(io,&row->lightmap_pool) &&
        qa_source_save_bool(io,&row->movies) && qa_source_save_bool(io,&row->cinematics) &&
        qa_source_save_u32(io,&row->seat);
}
bool frontend_renderer_materials_checkpoint(qa_frontend *f,qa_buffer *out,qa_error *error)
{
    retained_rows rows={0}; qa_source_save_io io={0};
    uint8_t magic[4]={'Q','F','R','M'}; uint64_t count=0;
    qa_application_content_graph *graph=f?qa_application_content_graph_read(f->application):NULL;
    bool okay=f && f->capture && graph && rows_read(f,&rows,error);
    count=rows.count;
    okay=okay && qa_source_save_writer(&io,NULL,error) && qa_source_save_bytes(&io,magic,4) &&
        qa_source_save_u64(&io,&count);
    for(size_t i=0;okay && i<rows.count;++i) {
        const frontend_renderer_materials_view *v=&rows.rows[i];
        saved_row row={.shader=v->library!=NULL,.lightmap=v->lightmap_images!=NULL,.movies=v->media!=NULL,
            .cinematics=v->has_cinematics,.seat=v->cinematic_seat};
        if(row.shader) {
            row.view=qa_application_content_view_id(graph,v->mounts);
            row.pool=qa_application_content_pool_id(graph,qa_vfs_resources(v->mounts));
            okay=row.view && row.pool;
        }
        if(row.lightmap) {
            row.lightmap_view=qa_application_content_view_id(graph,v->lightmap_mounts);
            row.lightmap_pool=qa_application_content_pool_id(graph,qa_vfs_resources(v->lightmap_mounts));
            okay=okay && row.lightmap_view && row.lightmap_pool;
        }
        okay=okay && (!row.movies || (row.shader && v->movies)) &&
            (!row.cinematics || (row.movies && v->cinematic_bus && row.seat<f->options.seats)) && row_fields(&io,&row);
    }
    okay=okay && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); free(rows.rows); return okay;
}
static bool graph_pair(qa_application_content_graph *graph,bool present,uint64_t view,uint64_t pool)
{
    return present==(view!=0) && present==(pool!=0) && (!present ||
        (qa_application_content_view(graph,view) && qa_application_content_pool(graph,pool) &&
         qa_vfs_resources(qa_application_content_view(graph,view))==qa_application_content_pool(graph,pool)));
}
static bool restore_bank(qa_frontend *f,qa_application_content_graph *graph,uint64_t view,uint64_t pool,
    qa_vfs **mounts,qa_resource_pool **resources,qa_scene_resources **images,qa_error *error)
{
    qa_vfs *actual=qa_application_content_view(graph,view);
    for(frontend_renderer_materials *owner=f->renderer_materials;owner;owner=owner->next) {
        qa_scene_resources *prior=owner->view.mounts==actual?owner->view.images:
            owner->view.lightmap_mounts==actual?owner->view.lightmap_images:NULL;
        if(prior) {
            if(!qa_application_content_retain_pool(graph,pool,resources,error) || !qa_vfs_retain(actual,error)) return false;
            *mounts=actual;
            if(!qa_scene_resources_retain(prior,error)) return false;
            *images=prior; return true;
        }
    }
    if(!qa_application_content_retain_pool(graph,pool,resources,error) ||
        !qa_application_content_claim_view(graph,view,mounts,error)) return false;
    *images=qa_scene_resources_create_detached(*mounts,error); return *images!=NULL;
}
bool frontend_renderer_materials_prepare_restored(qa_frontend *f,qa_bytes bytes,qa_error *error)
{
    qa_source_save_io io={0}; uint8_t magic[4]={0}; uint64_t count=0;
    if(!f || !f->source_restoring || f->capture || f->resource_inventory || f->renderer_materials) return false;
    qa_application_content_graph *graph=qa_application_content_graph_read(f->application);
    bool okay=graph && qa_source_save_reader(&io,NULL,bytes,error) && qa_source_save_bytes(&io,magic,4) &&
        !memcmp(magic,"QFRM",4) && qa_source_save_u64(&io,&count) && count<=SIZE_MAX/sizeof(saved_row) && count<=bytes.size/40;
    saved_row *rows=okay && count?calloc((size_t)count,sizeof(*rows)):NULL;
    if(okay && count && !rows) okay=frontend_fail(error,QA_ERROR_MEMORY,"Decoding retained renderer parent roster");
    for(size_t i=0;okay && i<(size_t)count;++i) {
        saved_row *row=&rows[i];
        okay=row_fields(&io,row) && (row->shader || row->lightmap) && (!row->movies || row->shader) &&
            (!row->cinematics || (row->movies && row->seat<f->options.seats)) &&
            (row->cinematics || !row->seat) &&
            graph_pair(graph,row->shader,row->view,row->pool) &&
            graph_pair(graph,row->lightmap,row->lightmap_view,row->lightmap_pool) &&
            (!row->shader || !row->lightmap || row->view!=row->lightmap_view);
    }
    okay=okay && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    /* All topology bytes and graph references validate before the first claim. */
    frontend_renderer_materials **tail=&f->renderer_materials;
    for(size_t i=0;okay && i<(size_t)count;++i) {
        saved_row *row=&rows[i]; frontend_renderer_materials *owner=calloc(1,sizeof(*owner));
        if(!owner) { okay=frontend_fail(error,QA_ERROR_MEMORY,"Restoring renderer parent custody"); break; }
        owner->frontend=f; owner->application=f->application; *tail=owner; tail=&owner->next;
        owner->view.has_cinematics=row->cinematics; owner->view.cinematic_seat=row->seat;
        if (row->cinematics) {
            if (f->next_source_id>=UINT64_MAX-QA_FRONTEND_COMMAND_OWNER-1)
                okay=frontend_fail(error,QA_ERROR_FORMAT,"Retained cinematic namespace exceeds the candidate's actual owner range");
            else owner->view.cinematic_bus=QA_FRONTEND_COMMAND_OWNER+(++f->next_source_id);
        }
        if(row->shader) {
            okay=okay && restore_bank(f,graph,row->view,row->pool,&owner->view.mounts,&owner->pool,&owner->view.images,error);
            if(okay) {
                owner->view.library=qa_material_library_create_detached(owner->view.images,error);
                owner->target=owner->view.library; okay=owner->view.library!=NULL;
            }
            if(okay && row->movies) { owner->view.media=qa_media_library_create(owner->view.images,error); okay=owner->view.media!=NULL; }
        }
        if(okay && row->lightmap) okay=restore_bank(f,graph,row->lightmap_view,row->lightmap_pool,
            &owner->view.lightmap_mounts,&owner->lightmap_pool,&owner->view.lightmap_images,error);
    }
    free(rows); return okay;
}
bool frontend_renderer_materials_bind_restored(qa_frontend *f,qa_error *error)
{
    if(!f || !f->source_restoring || f->capture || f->resource_inventory) return false;
    for(frontend_renderer_materials *owner=f->renderer_materials;owner;owner=owner->next)
        if(owner->view.library && qa_material_library_has_source_profile(owner->view.library) &&
            !frontend_q3_material_source_bind(f,owner->view.library,error)) return false;
    return true;
}
