#include "model_inventory.h"
#include "qa/source_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include <stdlib.h>
#include <string.h>

typedef struct model_holder {
    frontend_model_source source;
    qa_model owned;
    uint64_t pool, resource, view;
    uint64_t parent;
    size_t references, dependents;
} model_holder;
typedef struct animation_holder {
    frontend_animation_source source;
    qa_model_animation owned;
    uint64_t pool, resource, view, scale_pool, scale_resource;
    size_t references;
} animation_holder;
struct frontend_model_inventory {
    qa_application_content_graph *graph;
    model_holder *models;
    animation_holder *animations;
    size_t model_count, animation_count;
    size_t references;
    bool owns_holders, staging, owner_retired;
};
struct frontend_model_lease { frontend_model_inventory *inventory; size_t index; };
struct frontend_animation_lease { frontend_model_inventory *inventory; size_t index; };
static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
static bool subset_aliases(const qa_model *a, const qa_model *b)
{
    if (!a || !b || a==b || a->format!=QA_MODEL_MDL || b->format!=QA_MODEL_MDL ||
        a->mesh_count!=1 || b->mesh_count!=1 || !a->meshes || !b->meshes || a->meshes==b->meshes ||
        memcmp(a->name,b->name,sizeof(a->name)) || a->flags!=b->flags || a->sync!=b->sync ||
        a->orientation!=b->orientation || memcmp(&a->radius,&b->radius,sizeof(a->radius)) ||
        memcmp(&a->size,&b->size,sizeof(a->size)) || memcmp(&a->beam_length,&b->beam_length,sizeof(a->beam_length)) ||
        memcmp(a->scale,b->scale,sizeof(a->scale)) || memcmp(a->translation,b->translation,sizeof(a->translation)) ||
        memcmp(a->eye_position,b->eye_position,sizeof(a->eye_position)) ||
        a->skin_width!=b->skin_width || a->skin_height!=b->skin_height || a->declared_skin_count!=b->declared_skin_count ||
        memcmp(&a->bounds,&b->bounds,sizeof(a->bounds)) ||
        a->frame_count!=b->frame_count || a->frame_group_count!=b->frame_group_count ||
        a->skin_count!=b->skin_count || a->skin_group_count!=b->skin_group_count ||
        a->tag_count!=b->tag_count || a->sprite_count!=b->sprite_count || a->lod_count!=b->lod_count ||
        a->bone_count!=b->bone_count || a->gl_command_count!=b->gl_command_count ||
        a->frames!=b->frames || a->frame_groups!=b->frame_groups || a->skin_groups!=b->skin_groups ||
        a->skins!=b->skins || a->tags!=b->tags || a->sprites!=b->sprites || a->lods!=b->lods ||
        a->bones!=b->bones || a->bone_matrices!=b->bone_matrices || a->bind_pose!=b->bind_pose ||
        a->gl_commands!=b->gl_commands || a->command_line.data!=b->command_line.data ||
        a->command_line.size!=b->command_line.size || a->source.data!=b->source.data || a->source.size!=b->source.size)
        return false;
    const qa_model_mesh *x=a->meshes,*y=b->meshes;
    return !memcmp(x->name,y->name,sizeof(x->name)) && x->flags==y->flags &&
        x->vertex_count==y->vertex_count && x->texcoord_count==y->texcoord_count &&
        x->frame_count==y->frame_count && x->shader_count==y->shader_count &&
        x->weight_count==y->weight_count && x->bone_reference_count==y->bone_reference_count &&
        x->vertices==y->vertices && x->texcoords==y->texcoords && x->shaders==y->shaders &&
        x->weights==y->weights && x->vertex_weights==y->vertex_weights && x->bone_references==y->bone_references &&
        x->triangles && x->triangles!=y->triangles && x->triangle_count && x->triangle_count<=y->triangle_count;
}
static bool subset_fields(qa_source_save_io *io, model_holder *holder, const model_holder *parent)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    const qa_model *base=parent->source.model;
    const qa_model_mesh *source=base?base->meshes:NULL;
    uint32_t count=reading?0:holder->source.model->meshes->triangle_count;
    if (!base || base->format!=QA_MODEL_MDL || base->mesh_count!=1 || !source ||
        !qa_source_save_u32(io,&count) || !count || count>source->triangle_count ||
        (reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/4))) return false;
    if (reading) {
        qa_model_mesh *owned=calloc(1,sizeof(*owned));
        qa_model_triangle *triangles=calloc(count,sizeof(*triangles));
        if (!owned || !triangles) {
            free(owned); free(triangles);
            return fail(io->error,QA_ERROR_MEMORY,"Retaining actual borrowed held-model subset");
        }
        holder->owned=*base; *owned=*source;
        owned->triangles=triangles; owned->triangle_count=count;
        holder->owned.meshes=owned;
        holder->source.parent=base;
    } else if (!subset_aliases(holder->source.model,base)) return false;
    qa_model_triangle *triangles=reading?holder->owned.meshes->triangles:holder->source.model->meshes->triangles;
    uint32_t source_at=0;
    for (size_t i=0;i<count;++i) {
        uint32_t ordinal=source_at;
        if (!reading) {
            const qa_model_triangle *triangle=triangles+i;
            while (ordinal<source->triangle_count) {
                const qa_model_triangle *actual=source->triangles+ordinal;
                if (actual->front==triangle->front && !memcmp(actual->vertex,triangle->vertex,sizeof(actual->vertex)) &&
                    !memcmp(actual->texcoord,triangle->texcoord,sizeof(actual->texcoord))) break;
                ++ordinal;
            }
        }
        if (!qa_source_save_u32(io,&ordinal) || ordinal<source_at || ordinal>=source->triangle_count) return false;
        if (reading) triangles[i]=source->triangles[ordinal];
        source_at=ordinal+1;
    }
    return true;
}
static bool source_identity(qa_application_content_graph *graph, const qa_resource *resource,
    const qa_vfs *files, uint64_t *pool, uint64_t *version, uint64_t *view, qa_error *error)
{
    if (!graph || !resource || !files || !qa_application_content_resource_id(graph, resource, pool, version) ||
        !(*view = qa_application_content_view_id(graph, files)) ||
        qa_vfs_resources(files) != qa_application_content_pool(graph, *pool))
        return fail(error, QA_ERROR_FORMAT, "Parsed model source is outside the retained content graph");
    return true;
}
static void dispose(frontend_model_inventory *inventory)
{
    if (!inventory) return;
    for (size_t i = 0; inventory->models && i < inventory->model_count; ++i) {
        if (inventory->owns_holders) {
            model_holder *holder=&inventory->models[i];
            if (holder->parent) {
                if (holder->owned.meshes) free(holder->owned.meshes->triangles);
                free(holder->owned.meshes);
            } else qa_model_free(&holder->owned);
        }
        qa_resource_release((qa_resource *)inventory->models[i].source.resource);
    }
    for (size_t i = 0; inventory->animations && i < inventory->animation_count; ++i) {
        if (inventory->owns_holders) qa_model_animation_free(&inventory->animations[i].owned);
        qa_resource_release((qa_resource *)inventory->animations[i].source.resource);
        qa_resource_release((qa_resource *)inventory->animations[i].source.scale_resource);
    }
    free(inventory->models); free(inventory->animations); free(inventory);
}
static void model_retire(frontend_model_inventory *inventory, size_t index)
{
    model_holder *holder = &inventory->models[index];
    if (!inventory->owns_holders || inventory->staging || holder->references || holder->dependents || !holder->source.model) return;
    uint64_t parent=holder->parent;
    if (parent) {
        if (holder->owned.meshes) free(holder->owned.meshes->triangles);
        free(holder->owned.meshes); holder->owned=(qa_model){0};
    } else qa_model_free(&holder->owned);
    qa_resource_release((qa_resource *)holder->source.resource);
    holder->source = (frontend_model_source){0};
    if (parent) {
        --inventory->models[parent-1].dependents;
        model_retire(inventory,(size_t)parent-1);
    }
}
static void animation_retire(frontend_model_inventory *inventory, size_t index)
{
    animation_holder *holder = &inventory->animations[index];
    if (!inventory->owns_holders || inventory->staging || holder->references || !holder->source.animation) return;
    qa_model_animation_free(&holder->owned);
    qa_resource_release((qa_resource *)holder->source.resource);
    qa_resource_release((qa_resource *)holder->source.scale_resource);
    holder->source = (frontend_animation_source){0};
}
void frontend_models_destroy(frontend_model_inventory *inventory)
{
    if (!inventory || inventory->owner_retired) return;
    inventory->owner_retired = true;
    inventory->staging = false;
    if (inventory->models) for (size_t i = 0; i < inventory->model_count; ++i) model_retire(inventory, i);
    if (inventory->animations) for (size_t i = 0; i < inventory->animation_count; ++i) animation_retire(inventory, i);
    if (--inventory->references == 0) dispose(inventory);
}
bool frontend_model_retain(frontend_model_inventory *inventory, const qa_model *source,
    frontend_model_lease **out, qa_error *error)
{
    if (!inventory || !inventory->owns_holders || inventory->owner_retired || !source || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Model claim requires an actual decoded holder and empty token output");
    for (size_t i = 0; i < inventory->model_count; ++i) if (inventory->models[i].source.model == source) {
        if (inventory->references == SIZE_MAX || inventory->models[i].references == SIZE_MAX)
            return fail(error, QA_ERROR_MEMORY, "Immutable model claims overflow their actual counters");
        frontend_model_lease *lease = malloc(sizeof(*lease));
        if (!lease) return fail(error, QA_ERROR_MEMORY, "Retaining actual immutable model consumer");
        *lease = (frontend_model_lease){inventory, i};
        ++inventory->references; ++inventory->models[i].references;
        *out = lease; return true;
    }
    return fail(error, QA_ERROR_FORMAT, "Model claim does not identify a live decoded holder");
}
bool frontend_model_lease_clone(const frontend_model_lease *held,frontend_model_lease **out,qa_error *error)
{
    frontend_model_source source;
    frontend_model_inventory *inventory=held?held->inventory:NULL;
    if(!out || *out || !inventory || !inventory->owns_holders || !inventory->references ||
        !frontend_model_lease_source(held,&source))
        return fail(error,QA_ERROR_ARGUMENT,"Model clone requires its actual live owning token and empty output");
    model_holder *row=&inventory->models[held->index];
    if(inventory->references==SIZE_MAX || row->references==SIZE_MAX)
        return fail(error,QA_ERROR_MEMORY,"Cloned immutable model claims overflow their actual counters");
    frontend_model_lease *lease=malloc(sizeof(*lease));
    if(!lease) return fail(error,QA_ERROR_MEMORY,"Cloning actual immutable model consumer");
    *lease=*held; ++inventory->references; ++row->references; *out=lease; return true;
}
void frontend_model_release(frontend_model_lease *lease)
{
    if (!lease) return;
    frontend_model_inventory *inventory = lease->inventory;
    --inventory->models[lease->index].references;
    model_retire(inventory, lease->index);
    free(lease);
    if (--inventory->references == 0) dispose(inventory);
}
bool frontend_animation_retain(frontend_model_inventory *inventory, const qa_model_animation *source,
    frontend_animation_lease **out, qa_error *error)
{
    if (!inventory || !inventory->owns_holders || inventory->owner_retired || !source || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Animation claim requires an actual decoded holder and empty token output");
    for (size_t i = 0; i < inventory->animation_count; ++i) if (inventory->animations[i].source.animation == source) {
        if (inventory->references == SIZE_MAX || inventory->animations[i].references == SIZE_MAX)
            return fail(error, QA_ERROR_MEMORY, "Immutable animation claims overflow their actual counters");
        frontend_animation_lease *lease = malloc(sizeof(*lease));
        if (!lease) return fail(error, QA_ERROR_MEMORY, "Retaining actual immutable animation consumer");
        *lease = (frontend_animation_lease){inventory, i};
        ++inventory->references; ++inventory->animations[i].references;
        *out = lease; return true;
    }
    return fail(error, QA_ERROR_FORMAT, "Animation claim does not identify a live decoded holder");
}
bool frontend_animation_lease_clone(const frontend_animation_lease *held,frontend_animation_lease **out,qa_error *error)
{
    frontend_animation_source source;
    frontend_model_inventory *inventory=held?held->inventory:NULL;
    if(!out || *out || !inventory || !inventory->owns_holders || !inventory->references ||
        !frontend_animation_lease_source(held,&source))
        return fail(error,QA_ERROR_ARGUMENT,"Animation clone requires its actual live owning token and empty output");
    animation_holder *row=&inventory->animations[held->index];
    if(inventory->references==SIZE_MAX || row->references==SIZE_MAX)
        return fail(error,QA_ERROR_MEMORY,"Cloned immutable animation claims overflow their actual counters");
    frontend_animation_lease *lease=malloc(sizeof(*lease));
    if(!lease) return fail(error,QA_ERROR_MEMORY,"Cloning actual immutable animation consumer");
    *lease=*held; ++inventory->references; ++row->references; *out=lease; return true;
}
void frontend_animation_release(frontend_animation_lease *lease)
{
    if (!lease) return;
    frontend_model_inventory *inventory = lease->inventory;
    --inventory->animations[lease->index].references;
    animation_retire(inventory, lease->index);
    free(lease);
    if (--inventory->references == 0) dispose(inventory);
}
bool frontend_model_lease_source(const frontend_model_lease *lease, frontend_model_source *out)
{
    if (!lease || !out || !lease->inventory || lease->index >= lease->inventory->model_count) return false;
    const model_holder *holder = &lease->inventory->models[lease->index];
    if (!holder->references || !holder->source.model || !holder->source.resource || !holder->source.files) return false;
    *out = holder->source; return true;
}
bool frontend_animation_lease_source(const frontend_animation_lease *lease, frontend_animation_source *out)
{
    if (!lease || !out || !lease->inventory || lease->index >= lease->inventory->animation_count) return false;
    const animation_holder *holder = &lease->inventory->animations[lease->index];
    if (!holder->references || !holder->source.animation || !holder->source.resource || !holder->source.files) return false;
    *out = holder->source; return true;
}
bool frontend_models_install(frontend_model_inventory *inventory, qa_error *error)
{
    if (!inventory || !inventory->owns_holders || inventory->owner_retired || !inventory->staging || inventory->graph)
        return fail(error, QA_ERROR_ARGUMENT, "Model install requires the actual staged inventory after pure decode");
    size_t references = 1;
    for (size_t i = 0; i < inventory->model_count; ++i) {
        if (!inventory->models[i].source.model || inventory->models[i].references > SIZE_MAX - references)
            return fail(error, QA_ERROR_FORMAT, "Staged immutable model claims have inconsistent topology");
        references += inventory->models[i].references;
    }
    for (size_t i = 0; i < inventory->animation_count; ++i) {
        if (!inventory->animations[i].source.animation || inventory->animations[i].references > SIZE_MAX - references)
            return fail(error, QA_ERROR_FORMAT, "Staged immutable animation claims have inconsistent topology");
        references += inventory->animations[i].references;
    }
    if (references != inventory->references)
        return fail(error, QA_ERROR_FORMAT, "Immutable holder claims disagree with the real owning tokens");
    inventory->staging = false;
    for (size_t i = 0; i < inventory->model_count; ++i) model_retire(inventory, i);
    for (size_t i = 0; i < inventory->animation_count; ++i) animation_retire(inventory, i);
    return true;
}
bool frontend_models_capture(qa_application_content_graph *graph,
    const frontend_model_source *models, size_t model_count,
    const frontend_animation_source *animations, size_t animation_count,
    frontend_model_inventory **out, qa_error *error)
{
    if (!graph || !out || *out || (model_count && !models) || (animation_count && !animations) ||
        model_count > SIZE_MAX / sizeof(model_holder) || animation_count > SIZE_MAX / sizeof(animation_holder))
        return fail(error, QA_ERROR_ARGUMENT, "Model inventory requires actual graph, source rows and empty output");
    frontend_model_inventory *inventory = calloc(1, sizeof(*inventory));
    if (!inventory) return fail(error, QA_ERROR_MEMORY, "Allocating immutable model inventory");
    inventory->graph = graph; inventory->references = 1;
    inventory->models = model_count ? calloc(model_count, sizeof(*inventory->models)) : NULL;
    inventory->animations = animation_count ? calloc(animation_count, sizeof(*inventory->animations)) : NULL;
    bool ok = (!model_count || inventory->models) && (!animation_count || inventory->animations);
    if (!ok) fail(error, QA_ERROR_MEMORY, "Allocating parsed model source rows");
    for (size_t i = 0; ok && i < model_count; ++i) {
        const frontend_model_source *source = &models[i]; bool alias = false;
        if (!source->model || !source->model->source.data ||
            source->model->source.size != qa_resource_bytes(source->resource).size) {
            ok = fail(error, QA_ERROR_FORMAT, "Parsed model lacks its exact original resource version"); break;
        }
        for (size_t j = 0; j < inventory->model_count; ++j) if (inventory->models[j].source.model == source->model) {
            alias = true;
            if (inventory->models[j].source.resource != source->resource || inventory->models[j].source.files != source->files ||
                inventory->models[j].source.parent!=source->parent)
                ok = fail(error, QA_ERROR_FORMAT, "Aliased model holder has conflicting source provenance");
            break;
        }
        if (!ok || alias) continue;
        model_holder *holder = &inventory->models[inventory->model_count];
        ok = source_identity(graph, source->resource, source->files, &holder->pool, &holder->resource, &holder->view, error);
        if (ok && source->parent) {
            for (size_t j=0;j<inventory->model_count;++j)
                if (inventory->models[j].source.model==source->parent) { holder->parent=j+1; break; }
            if (!holder->parent || inventory->models[holder->parent-1].source.resource!=source->resource ||
                inventory->models[holder->parent-1].source.files!=source->files || !subset_aliases(source->model,source->parent))
                ok=fail(error,QA_ERROR_FORMAT,"Held subset lacks its actual prior parsed parent and shared allocations");
        }
        if (ok) { holder->source = *source; qa_resource_retain((qa_resource *)source->resource); ++inventory->model_count; }
    }
    for (size_t i = 0; ok && i < animation_count; ++i) {
        const frontend_animation_source *source = &animations[i]; bool alias = false;
        if (!source->animation || !source->animation->source.data ||
            source->animation->source.size != qa_resource_bytes(source->resource).size) {
            ok = fail(error, QA_ERROR_FORMAT, "Parsed animation lacks its exact original resource version"); break;
        }
        for (size_t j = 0; j < inventory->animation_count; ++j) if (inventory->animations[j].source.animation == source->animation) {
            alias = true;
            const frontend_animation_source *prior = &inventory->animations[j].source;
            if (prior->resource != source->resource || prior->files != source->files || prior->scale_resource != source->scale_resource)
                ok = fail(error, QA_ERROR_FORMAT, "Aliased animation holder has conflicting scale/source provenance");
            break;
        }
        if (!ok || alias) continue;
        animation_holder *holder = &inventory->animations[inventory->animation_count];
        ok = source_identity(graph, source->resource, source->files, &holder->pool, &holder->resource, &holder->view, error);
        if (ok && source->scale_resource && !qa_application_content_resource_id(graph, source->scale_resource,
            &holder->scale_pool, &holder->scale_resource)) ok = fail(error, QA_ERROR_FORMAT, "Animation scale policy is outside the actual content graph");
        if (ok && source->scale_resource && holder->scale_pool != holder->pool)
            ok = fail(error, QA_ERROR_FORMAT, "Animation scale policy does not belong to its source content view");
        if (ok) {
            holder->source = *source; qa_resource_retain((qa_resource *)source->resource);
            if (source->scale_resource) qa_resource_retain((qa_resource *)source->scale_resource);
            ++inventory->animation_count;
        }
    }
    if (!ok) { frontend_models_destroy(inventory); return false; }
    *out = inventory; return true;
}
static bool identity_fields(qa_source_save_io *io, uint64_t *pool, uint64_t *resource, uint64_t *view)
{ return qa_source_save_u64(io, pool) && *pool && qa_source_save_u64(io, resource) && *resource && qa_source_save_u64(io, view) && *view; }
static bool inventory_fields(qa_source_save_io *io, frontend_model_inventory *inventory)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; uint8_t magic[4] = {'Q','F','M','I'}; if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFMI", 4) || !qa_source_save_count(io, &inventory->model_count, reading ? io->input.size / 32 : SIZE_MAX / sizeof(model_holder)) ||
        !qa_source_save_count(io, &inventory->animation_count, reading ? io->input.size / 40 : SIZE_MAX / sizeof(animation_holder)) ||
        inventory->model_count > SIZE_MAX / sizeof(model_holder) || inventory->animation_count > SIZE_MAX / sizeof(animation_holder)) return false;
    if (reading) {
        inventory->models = inventory->model_count ? calloc(inventory->model_count, sizeof(*inventory->models)) : NULL;
        inventory->animations = inventory->animation_count ? calloc(inventory->animation_count, sizeof(*inventory->animations)) : NULL;
        if ((inventory->model_count && !inventory->models) || (inventory->animation_count && !inventory->animations))
            return fail(io->error, QA_ERROR_MEMORY, "Restoring immutable model owner rows");
    }
    for (size_t i = 0; i < inventory->model_count; ++i) {
        model_holder *holder = &inventory->models[i];
        if (!identity_fields(io, &holder->pool, &holder->resource, &holder->view)) return false;
        if (reading) {
            const qa_resource *resource = qa_application_content_resource(inventory->graph, holder->pool, holder->resource);
            const qa_vfs *files = qa_application_content_view(inventory->graph, holder->view);
            if (!resource || !files || qa_vfs_resources(files) != qa_application_content_pool(inventory->graph, holder->pool)) return false;
            qa_resource_retain((qa_resource *)resource);
            holder->source = (frontend_model_source){.model=&holder->owned,.resource=resource,.files=files};
        }
        uint64_t parent_key=holder->parent;
        if (!qa_source_save_u64(io,&parent_key) || parent_key>i) return false;
        if (parent_key) {
            model_holder *parent=&inventory->models[parent_key-1];
            if (parent->pool!=holder->pool || parent->resource!=holder->resource || parent->view!=holder->view ||
                parent->dependents==SIZE_MAX) return false;
            if (reading) { holder->parent=parent_key; ++parent->dependents; }
            if (!subset_fields(io,holder,parent)) return false;
        } else if (reading && !qa_model_load(qa_resource_bytes(holder->source.resource),&holder->owned,io->error)) return false;
    }
    for (size_t i = 0; i < inventory->animation_count; ++i) {
        animation_holder *holder = &inventory->animations[i];
        if (!identity_fields(io, &holder->pool, &holder->resource, &holder->view) ||
            !qa_source_save_u64(io, &holder->scale_pool) || !qa_source_save_u64(io, &holder->scale_resource) ||
            ((!holder->scale_pool) != (!holder->scale_resource))) return false;
        if (reading) {
            const qa_resource *resource = qa_application_content_resource(inventory->graph, holder->pool, holder->resource);
            const qa_vfs *files = qa_application_content_view(inventory->graph, holder->view);
            const qa_resource *scale_resource = holder->scale_pool ? qa_application_content_resource(inventory->graph,
                holder->scale_pool, holder->scale_resource) : NULL;
            if (!resource || !files || (holder->scale_pool && !scale_resource) ||
                qa_vfs_resources(files) != qa_application_content_pool(inventory->graph, holder->pool) ||
                (scale_resource && holder->scale_pool != holder->pool)) return false;
            qa_resource_retain((qa_resource *)resource);
            if (scale_resource) qa_resource_retain((qa_resource *)scale_resource);
            holder->source = (frontend_animation_source){&holder->owned, resource, files, scale_resource};
        }
        if (reading && (!qa_model_animation_load(qa_resource_bytes(holder->source.resource),&holder->owned,io->error) ||
            (holder->source.scale_resource && !qa_model_animation_scale_json(&holder->owned,
                qa_resource_bytes(holder->source.scale_resource),NULL,NULL,io->error)))) return false;
    }
    return true;
}
bool frontend_models_checkpoint(const frontend_model_inventory *inventory, qa_buffer *out, qa_error *error)
{
    if (!inventory || inventory->owns_holders || inventory->owner_retired || !inventory->graph || !out || out->data || out->size)
        return fail(error, QA_ERROR_ARGUMENT, "Model checkpoint requires actual inventory and empty output");
    frontend_model_inventory local = *inventory; qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && inventory_fields(&io, &local) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Parsed model graph is not completely qualified");
    return ok;
}
bool frontend_models_restore(qa_application_content_graph *graph, qa_bytes bytes,
    frontend_model_inventory **out, qa_error *error)
{
    if (!graph || !out || *out || !bytes.data)
        return fail(error, QA_ERROR_ARGUMENT, "Model restore requires actual graph, saved inventory and empty output");
    frontend_model_inventory *inventory = calloc(1, sizeof(*inventory));
    if (!inventory) return fail(error, QA_ERROR_MEMORY, "Allocating restored model inventory");
    inventory->graph = graph; inventory->owns_holders = true; inventory->staging = true;
    inventory->references = 1; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && inventory_fields(&io, inventory) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) {
        frontend_models_destroy(inventory);
        if (error && error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Saved parsed model graph differs from qualified immutable content");
        return false;
    }
    inventory->graph = NULL;
    *out = inventory; return true;
}
size_t frontend_model_count(const frontend_model_inventory *inventory)
{ return inventory ? inventory->model_count : 0; }
size_t frontend_animation_count(const frontend_model_inventory *inventory)
{ return inventory ? inventory->animation_count : 0; }
bool frontend_model_source_at(const frontend_model_inventory *inventory, size_t index, frontend_model_source *out)
{ if (!inventory || inventory->owner_retired || !out || index >= inventory->model_count || !inventory->models[index].source.model) return false; *out = inventory->models[index].source; return true; }
bool frontend_animation_source_at(const frontend_model_inventory *inventory, size_t index, frontend_animation_source *out)
{ if (!inventory || inventory->owner_retired || !out || index >= inventory->animation_count || !inventory->animations[index].source.animation) return false; *out = inventory->animations[index].source; return true; }
bool frontend_model_encode(void *context, const qa_model *model, uint64_t *out, qa_error *error)
{
    const frontend_model_inventory *inventory = context;
    if (inventory && !inventory->owner_retired && model && out) for (size_t i = 0; i < inventory->model_count; ++i)
        if (inventory->models[i].source.model == model) { *out = i; return true; }
    return fail(error, QA_ERROR_FORMAT, "Scene model is absent from the actual parsed holder inventory");
}
bool frontend_model_decode(void *context, uint64_t id, const qa_model **out, qa_error *error)
{
    const frontend_model_inventory *inventory = context;
    if (inventory && !inventory->owner_retired && out && id < inventory->model_count) {
        const qa_model *model = inventory->models[id].source.model;
        if (model) { *out = model; return true; }
    }
    return fail(error, QA_ERROR_FORMAT, "Scene model reference differs from the restored source-qualified holder");
}
bool frontend_animation_encode(void *context, const qa_model_animation *animation, uint64_t *out, qa_error *error)
{
    const frontend_model_inventory *inventory = context;
    if (inventory && !inventory->owner_retired && animation && out) for (size_t i = 0; i < inventory->animation_count; ++i)
        if (inventory->animations[i].source.animation == animation) { *out = i; return true; }
    return fail(error, QA_ERROR_FORMAT, "Replacement animation is absent from the actual installed holder inventory");
}
bool frontend_animation_decode(void *context, uint64_t id,
    const qa_model_animation **out, qa_error *error)
{
    const frontend_model_inventory *inventory = context;
    if (inventory && !inventory->owner_retired && out && id < inventory->animation_count) {
        const qa_model_animation *animation = inventory->animations[id].source.animation;
        if (animation) { *out = animation; return true; }
    }
    return fail(error, QA_ERROR_FORMAT, "Replacement animation reference differs from the installed immutable holder");
}
bool frontend_model_source_qualify(void *context, const qa_model *model, qa_scene_resources *images,
    qa_material_library *materials, const qa_scene_image_options *options, qa_error *error)
{
    const frontend_model_inventory *inventory = context;
    if (!inventory || inventory->owner_retired || !model || !images || !options || options->family > QA_GAME_Q3 ||
        (materials && qa_material_library_resource_owner(materials) != images))
        return fail(error, QA_ERROR_ARGUMENT, "Scene model qualification requires actual content and resource owners");
    for (size_t i = 0; i < inventory->model_count; ++i) if (inventory->models[i].source.model == model) {
        if (qa_scene_resources_files(images) != inventory->models[i].source.files)
            return fail(error, QA_ERROR_FORMAT, "Model image owner does not use its original source content view");
        return true;
    }
    return fail(error, QA_ERROR_FORMAT, "Scene model source is outside the genuine parsed holder inventory");
}
