#include "scene_inventory.h"
#include "visual_restore.h"
#include "equipment_media.h"
#include "remote_q3_client.h"
#include "remote_q1_client.h"
#include "remote_q2_client.h"
#include "renderer_worlds.h"
#include "unified_media_inventory.h"
#include "qa/q3_assets_save.h"
#include "qa/scene_resource_save.h"
#include "qa/scene_world_save.h"
#include "qa/material_library_save.h"

struct frontend_scene_inventory {
    qa_frontend *frontend;
    const frontend_capture *capture;
    frontend_model_inventory *models;
    frontend_world_source *worlds;
    const qa_scene_model **roots;
    size_t world_count, root_count;
};
typedef struct sources {
    frontend_model_source *models;
    frontend_animation_source *animations;
    size_t model_count, animation_count;
} sources;
static bool model_add(sources *rows, frontend_model_source source, qa_error *error)
{
    if (!source.model || !source.resource || !source.files)
        return frontend_fail(error,QA_ERROR_FORMAT,"Scene parsed holder has no actual source producer");
    for (size_t i=0;i<rows->model_count;++i) if (rows->models[i].model==source.model) {
        return (rows->models[i].resource==source.resource && rows->models[i].files==source.files &&
            rows->models[i].parent==source.parent) ||
            frontend_fail(error,QA_ERROR_FORMAT,"Aliased scene parsed holder has conflicting actual provenance");
    }
    if (rows->model_count==SIZE_MAX/sizeof(*rows->models))
        return frontend_fail(error,QA_ERROR_MEMORY,"Scene source inventory exceeds address space");
    frontend_model_source *models=realloc(rows->models,(rows->model_count+1)*sizeof(*models));
    if (!models) return frontend_fail(error,QA_ERROR_MEMORY,"Collecting actual immutable scene sources");
    rows->models=models; models[rows->model_count++]=source; return true;
}
static bool animation_add(sources *rows, frontend_animation_source source, qa_error *error)
{
    if (!source.animation || !source.resource || !source.files)
        return frontend_fail(error,QA_ERROR_FORMAT,"Scene animation has no actual installed source producer");
    for (size_t i=0;i<rows->animation_count;++i) if (rows->animations[i].animation==source.animation) {
        return (rows->animations[i].resource==source.resource && rows->animations[i].files==source.files &&
            rows->animations[i].scale_resource==source.scale_resource) ||
            frontend_fail(error,QA_ERROR_FORMAT,"Aliased installed animation has conflicting source policy");
    }
    if (rows->animation_count==SIZE_MAX/sizeof(*rows->animations))
        return frontend_fail(error,QA_ERROR_MEMORY,"Scene animation inventory exceeds address space");
    frontend_animation_source *animations=realloc(rows->animations,(rows->animation_count+1)*sizeof(*animations));
    if (!animations) return frontend_fail(error,QA_ERROR_MEMORY,"Collecting actual installed animations");
    rows->animations=animations; animations[rows->animation_count++]=source; return true;
}
static bool known_model(const sources *rows,const qa_model *model)
{
    for (size_t i=0;i<rows->model_count;++i) if (rows->models[i].model==model) return true;
    return false;
}
static bool world_bind(frontend_scene_inventory *inventory,const qa_scene_world *world,
    const qa_resource *resource,const qa_vfs *files,qa_error *error)
{
    if (!world) return true;
    for (size_t i=0;i<inventory->world_count;++i) if (inventory->worlds[i].world==world) {
        frontend_world_source *row=inventory->worlds+i;
        if (!resource || !files || (row->resource && (row->resource!=resource || row->files!=files)) ||
            qa_scene_resources_files(row->images)!=files)
            return frontend_fail(error,QA_ERROR_FORMAT,"Scene world has conflicting actual map provenance");
        row->resource=resource; row->files=files; return true;
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Scene world producer is outside the held actual roots");
}
static const qa_resource *lod_resource(const qa_q3_asset_model_holder *model,unsigned slot)
{
    if (!model->has_lods) return slot==0?model->resource:NULL;
    if (!model->lods || slot>=3) return NULL;
    for (unsigned depth=0;depth<3;++depth) {
        if (model->lods->states[slot]==QA_MODEL_LOD_LOADED) return model->lod_resources[slot];
        if (model->lods->states[slot]!=QA_MODEL_LOD_ALIAS || model->lods->aliases[slot]!=slot+1 || slot>=2) return NULL;
        slot=model->lods->aliases[slot];
    }
    return NULL;
}
static bool producers(frontend_scene_inventory *inventory,sources *rows,qa_error *error)
{
    qa_frontend *f=inventory->frontend;
    if (f->scene_world && !world_bind(inventory,f->scene_world,f->map_resource,f->mounts,error)) return false;
    frontend_renderer_worlds_view retained; bool present=false;
    if(!frontend_renderer_worlds_read(f,&retained,&present,error) || (present && retained.world &&
        !world_bind(inventory,retained.world,retained.resource,retained.files,error))) return false;
    size_t unified_count=0;
    if(!frontend_unified_media_inventory_count(f,&unified_count,error)) return false;
    for(size_t i=0;i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        if(!frontend_unified_media_inventory_at(f,i,&media,error)) return false;
        if(!media) continue;
        qa_scene_world *world=frontend_unified_media_world(media);
        if(world && !world_bind(inventory,world,qa_executable_recipe_map(frontend_unified_media_recipe(media)),
            qa_scene_resources_files(qa_scene_world_resource_owner(world)),error)) return false;
        for(size_t j=0;j<frontend_unified_media_model_count(media);++j) {
            frontend_unified_model_view model; frontend_unified_bank_view bank;
            if(!frontend_unified_media_model_read(media,j,&model) ||
                !frontend_unified_media_bank_read(media,model.bank,&bank) ||
                (model.world?!world_bind(inventory,model.world,model.resource,bank.files,error):
                    !model_add(rows,(frontend_model_source){.model=model.model,.resource=model.resource,.files=bank.files},error))) return false;
        }
    }
    for (size_t i=0;i<frontend_source_group_count(f);++i) {
        frontend_source_group_view group;
        if (!frontend_source_group_read(f,i,&group) ||
            (!group.map_resource!=!group.geometry) || (!group.map_resource!=!group.world) ||
            (group.world && !world_bind(inventory,group.world,group.map_resource,group.mounts,error))) return false;
    }
    for(size_t i=0;i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources owner;
        if(!frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&owner,error) ||
            !owner.map || !owner.geometry || !owner.world ||
            !world_bind(inventory,owner.world,owner.map,owner.mounts,error)) return false;
    }
    for(size_t i=0;i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1 *owner=frontend_remote_q1_at(f,i); frontend_remote_q1_media media;
        if(!frontend_remote_q1_media_read(owner,&media,error) ||
            !world_bind(inventory,media.world,media.map,media.mounts,error)) return false;
        for(size_t j=0;j<frontend_remote_q1_model_count(owner);++j) {
            frontend_remote_q1_model_view model;
            if(!frontend_remote_q1_model_at(owner,j,&model,error) ||
                (model.world?!world_bind(inventory,model.world,model.resource,media.mounts,error):
                    !model_add(rows,(frontend_model_source){.model=model.model,.resource=model.resource,.files=media.mounts},error))) return false;
        }
    }
    for(size_t i=0;i<frontend_remote_q2_count(f);++i) {
        frontend_remote_q2 *owner=frontend_remote_q2_at(f,i); frontend_remote_q2_view media;
        if(!frontend_remote_q2_metadata_read(owner,&media,error) ||
            !world_bind(inventory,media.world,media.map,media.content.mounts,error)) return false;
        for(size_t j=0;j<frontend_remote_q2_model_count(owner);++j) {
            frontend_remote_q2_model_view model;
            if(!frontend_remote_q2_model_at(owner,j,&model,error) ||
                !model_add(rows,(frontend_model_source){.model=model.model,.resource=model.resource,.files=media.content.mounts},error)) return false;
        }
    }
    for (size_t i=0;i<frontend_visual_owner_count(f);++i) {
        frontend_visual_owner_view owner;
        if (!frontend_visual_owner_read(f,i,&owner)) return false;
        for (size_t j=0;j<frontend_visual_model_count(f,i);++j) {
            frontend_visual_model_view model;
            if (!frontend_visual_model_read(f,i,j,&model) ||
                !model_add(rows,(frontend_model_source){.model=model.model,.resource=model.resource,.files=owner.mounts},error)) return false;
        }
    }
    for (size_t i=0;i<frontend_equipment_media_count(f);++i) {
        frontend_equipment_media_view media;
        if (!frontend_equipment_media_at(f,i,&media) || !media.declaration || !media.held) return false;
        if (media.declaration->none) continue;
        if (!media.held_scene || !model_add(rows,(frontend_model_source){media.held->model,
            media.held_parent.resource,media.owner.mounts,
            media.held->model!=media.held_parent.model?media.held_parent.model:NULL},error)) return false;
    }
    for (size_t i=0;;++i) {
        const qa_q3_presentation_assets *assets=frontend_capture_assets_at(inventory->capture,i);
        if (!assets) break;
        size_t count=0;
        if (!qa_q3_assets_model_count(assets,&count,error)) return false;
        for (size_t j=0;j<count;++j) {
            qa_q3_asset_model_holder model;
            if (!qa_q3_assets_model_holder(assets,j,&model,error)) return false;
            if (!model.present) continue;
            if (model.owns_world && !world_bind(inventory,model.world,model.resource,model.provider.mounts,error)) return false;
            for (unsigned k=0;k<3;++k) if (model.sources[k] &&
                !model_add(rows,(frontend_model_source){.model=model.sources[k],.resource=lod_resource(&model,k),.files=model.provider.mounts},error)) return false;
        }
    }
    for (size_t i=0;i<inventory->world_count;++i) {
        const frontend_world_source *world=inventory->worlds+i;
        if (!world->resource || !world->files || !world->images || !world->materials ||
            qa_material_library_resource_owner(world->materials)!=world->images ||
            qa_scene_resources_files(world->images)!=world->files)
            return frontend_fail(error,QA_ERROR_FORMAT,"Held scene world lacks its genuine immutable map and resource owners");
    }
    return true;
}
static bool node_sources(const qa_scene_model *model,sources *rows,qa_error *error)
{
    frontend_model_source source;
    if (frontend_scene_model_source_read(model,QA_SCENE_MODEL_CONTENT_SOURCE,&source)) {
        if (!model_add(rows,source,error)) return false;
    } else if (!known_model(rows,qa_scene_model_source(model)))
        return frontend_fail(error,QA_ERROR_FORMAT,"Held scene node has no real parsed source producer");
    const qa_model_replacement *replacement=qa_scene_model_replacement_description(model);
    if (!replacement) return true;
    if (replacement->mesh!=qa_scene_model_source(model))
        return frontend_fail(error,QA_ERROR_FORMAT,"Replacement mesh differs from its actual scene node source");
    if (frontend_scene_model_source_read(model,QA_SCENE_MODEL_CONTENT_REPLACEMENT_SOURCE,&source)) {
        if (!model_add(rows,source,error)) return false;
    } else if (!known_model(rows,replacement->source))
        return frontend_fail(error,QA_ERROR_FORMAT,"Replacement source has no actual retained producer");
    frontend_animation_source animation;
    return frontend_scene_animation_source_read(model,&animation) ? animation_add(rows,animation,error) :
        frontend_fail(error,QA_ERROR_FORMAT,"Replacement animation has no actual retained source and installed-policy producer");
}
static bool nodes(frontend_scene_inventory *inventory,sources *rows,qa_error *error)
{
    for (size_t i=0;i<inventory->root_count;++i) {
        const qa_scene_model **queue=malloc(sizeof(*queue)); size_t count=1;
        if (!queue) return frontend_fail(error,QA_ERROR_MEMORY,"Collecting the actual scene replacement tree");
        queue[0]=inventory->roots[i]; bool ok=true;
        for (size_t j=0;ok && j<count;++j) {
            ok=node_sources(queue[j],rows,error);
            for (const qa_scene_model *child=ok?qa_scene_model_replacement_first(queue[j]):NULL;child && ok;
                child=qa_scene_model_replacement_next(child)) {
                for (size_t k=0;k<count;++k) if (queue[k]==child) {
                    ok=frontend_fail(error,QA_ERROR_FORMAT,"Actual replacement tree aliases or cycles"); break;
                }
                if (!ok) break;
                if (count==SIZE_MAX/sizeof(*queue)) { ok=false; break; }
                const qa_scene_model **grown=realloc(queue,(count+1)*sizeof(*grown));
                if (!grown) { ok=frontend_fail(error,QA_ERROR_MEMORY,"Collecting actual replacement children"); break; }
                queue=grown; queue[count++]=child;
            }
        }
        free(queue); if (!ok) return false;
    }
    return true;
}
void frontend_scene_inventory_destroy(frontend_scene_inventory *inventory)
{
    if (!inventory) return;
    frontend_models_destroy(inventory->models); free(inventory->worlds); free(inventory->roots); free(inventory);
}
bool frontend_scene_inventory_capture(qa_frontend *f,frontend_scene_inventory **out,qa_error *error)
{
    if (!f || !f->application || !f->capture || !out || *out || !qa_application_content_graph_read(f->application))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Scene inventory requires the actual frontend and current content leases");
    frontend_scene_inventory *inventory=calloc(1,sizeof(*inventory)); sources rows={0};
    if (!inventory) return frontend_fail(error,QA_ERROR_MEMORY,"Collecting genuine scene owner roots");
    inventory->frontend=f; inventory->capture=f->capture; bool ok=true;
    for (size_t i=0;ok;++i) {
        const qa_scene_world *world=frontend_capture_world_at(f->capture,i);
        if (!world) break;
        if (inventory->world_count==SIZE_MAX/sizeof(*inventory->worlds)) { ok=false; break; }
        frontend_world_source *worlds=realloc(inventory->worlds,(inventory->world_count+1)*sizeof(*worlds));
        if (!worlds) { ok=frontend_fail(error,QA_ERROR_MEMORY,"Collecting actual scene world roots"); break; }
        inventory->worlds=worlds;
        worlds[inventory->world_count++]=(frontend_world_source){.world=world,
            .images=qa_scene_world_resource_owner(world),.materials=qa_scene_world_material_owner(world)};
    }
    for (size_t i=0;ok;++i) {
        const qa_scene_model *model=frontend_capture_model_at(f->capture,i);
        if (!model) break;
        if (inventory->root_count==SIZE_MAX/sizeof(*inventory->roots)) { ok=false; break; }
        const qa_scene_model **roots=realloc(inventory->roots,(inventory->root_count+1)*sizeof(*roots));
        if (!roots) { ok=frontend_fail(error,QA_ERROR_MEMORY,"Collecting actual scene model roots"); break; }
        inventory->roots=roots; roots[inventory->root_count++]=model;
    }
    ok=ok && producers(inventory,&rows,error) && nodes(inventory,&rows,error) &&
        frontend_models_capture(qa_application_content_graph_read(f->application),rows.models,rows.model_count,
            rows.animations,rows.animation_count,&inventory->models,error);
    free(rows.models); free(rows.animations);
    if (!ok) {
        frontend_scene_inventory_destroy(inventory);
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Actual scene source inventory is incomplete");
        return false;
    }
    *out=inventory; return true;
}
frontend_model_inventory *frontend_scene_inventory_models(const frontend_scene_inventory *inventory)
{ return inventory?inventory->models:NULL; }
size_t frontend_scene_inventory_world_count(const frontend_scene_inventory *inventory)
{ return inventory?inventory->world_count:0; }
bool frontend_scene_inventory_world_at(const frontend_scene_inventory *inventory,size_t i,frontend_world_source *out)
{
    if (!inventory || !out || i>=inventory->world_count) return false;
    *out=inventory->worlds[i]; return true;
}
size_t frontend_scene_inventory_model_count(const frontend_scene_inventory *inventory)
{ return inventory?inventory->root_count:0; }
const qa_scene_model *frontend_scene_inventory_model_at(const frontend_scene_inventory *inventory,size_t i)
{ return inventory && i<inventory->root_count?inventory->roots[i]:NULL; }
bool frontend_scene_inventory_namespace(const frontend_scene_inventory *inventory,frontend_scene_namespace *space,qa_error *error)
{
    if (!inventory || inventory->frontend->capture!=inventory->capture || !space)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Scene namespace collection requires its actual held owner roots");
    for (size_t i=0;i<inventory->world_count;++i)
        if (!frontend_scene_namespace_capture_world(space,i+1,inventory->worlds[i].world,error)) return false;
    for (size_t i=0;i<inventory->root_count;++i)
        if (!frontend_scene_namespace_capture_model(space,i+1,inventory->roots[i],error)) return false;
    return true;
}
