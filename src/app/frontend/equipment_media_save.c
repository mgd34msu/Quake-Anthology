#include "equipment_media_save.h"
#include "equipment_media_private.h"
#include "save_private.h"
#include "qa/material_library_save.h"
#include "qa/scene_resource_save.h"

#include <limits.h>

static bool resource_fields(qa_source_save_io *io, qa_application_content_graph *graph,
    const qa_resource **resource)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t pool = 0, version = 0;
    if (!reading && *resource &&
        !qa_application_content_resource_id(graph, *resource, &pool, &version)) return false;
    if (!qa_source_save_u64(io, &pool) || !qa_source_save_u64(io, &version) ||
        (!pool != !version)) return false;
    if (reading) {
        const qa_resource *found = pool ? qa_application_content_resource(graph, pool, version) : NULL;
        if (pool && !found) return false;
        *resource = found;
        qa_resource_retain((qa_resource *)found);
    }
    return true;
}

static bool owner_fields(qa_source_save_io *io, qa_actor_owner *owner)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_strings *strings = qa_session_strings(io->session);
    char *name = !reading && *owner <= UINT32_MAX ?
        (char *)qa_strings_cstr(strings, (qa_string_id)*owner) : NULL;
    if (!reading && (!*owner || !name || !*name)) return false;
    bool ok = frontend_save_text(io, &name);
    if (reading) {
        *owner = name ? qa_strings_find(strings, (qa_bytes){(const uint8_t *)name, strlen(name)}) : 0;
        free(name);
    }
    return ok && *owner;
}

static bool transform_fields(qa_source_save_io *io, qa_model_transform *transform)
{
    for (size_t i = 0; i < 3; ++i) {
        if (!qa_source_save_f32(io, transform->origin + i) || !isfinite(transform->origin[i]) ||
            !qa_source_save_f32(io, transform->scale + i) || !isfinite(transform->scale[i]) ||
            transform->scale[i] == 0) return false;
        for (size_t j = 0; j < 3; ++j)
            if (!qa_source_save_f32(io, transform->axes[i] + j) ||
                !isfinite(transform->axes[i][j])) return false;
    }
    return true;
}

static bool declaration_fields(qa_source_save_io *io, qa_application_content_graph *graph,
    frontend_held_declaration *declaration)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    const qa_resource *source = declaration->source;
    bool ok = resource_fields(io, graph, &source);
    if (reading) declaration->source = (qa_resource *)source;
    if (!ok || !qa_source_save_bool(io, &declaration->none)) return false;
    if (declaration->none) return true;
    if (!frontend_save_text(io, &declaration->path) || !declaration->path || !*declaration->path ||
        !frontend_save_text(io, &declaration->fallback) ||
        (declaration->fallback && !*declaration->fallback) ||
        !qa_source_save_bool(io, &declaration->has_digest) ||
        !qa_source_save_bytes(io, &declaration->digest, sizeof(declaration->digest)) ||
        !qa_source_save_u32(io, &declaration->reference_frame) ||
        declaration->reference_frame > INT32_MAX || !transform_fields(io, &declaration->grip)) return false;
    for (size_t i = 0; i < 3; ++i) {
        double length = 0;
        for (size_t k = 0; k < 3; ++k) length += (double)declaration->grip.axes[i][k] * declaration->grip.axes[i][k];
        if (fabs(length - 1) > .001) return false;
        for (size_t j = 0; j < i; ++j) {
            double dot = 0;
            for (size_t k = 0; k < 3; ++k) dot += (double)declaration->grip.axes[i][k] * declaration->grip.axes[j][k];
            if (fabs(dot) > .001) return false;
        }
    }
    double determinant =
        ((double)declaration->grip.axes[0][1] * declaration->grip.axes[1][2] - (double)declaration->grip.axes[0][2] * declaration->grip.axes[1][1]) * declaration->grip.axes[2][0] +
        ((double)declaration->grip.axes[0][2] * declaration->grip.axes[1][0] - (double)declaration->grip.axes[0][0] * declaration->grip.axes[1][2]) * declaration->grip.axes[2][1] +
        ((double)declaration->grip.axes[0][0] * declaration->grip.axes[1][1] - (double)declaration->grip.axes[0][1] * declaration->grip.axes[1][0]) * declaration->grip.axes[2][2];
    if (determinant < .999) return false;
    size_t maximum = reading ? (io->input.size - io->offset) / sizeof(qa_sha256_digest) :
        SIZE_MAX / sizeof(*declaration->part_digests);
    if (!qa_source_save_count(io, &declaration->part_digest_count, maximum)) return false;
    if (reading && declaration->part_digest_count) {
        declaration->part_digests = calloc(declaration->part_digest_count, sizeof(*declaration->part_digests));
        if (!declaration->part_digests) return frontend_fail(io->error, QA_ERROR_MEMORY, "Retaining restored held digest inventory");
    }
    if (declaration->part_digest_count && (!declaration->part_digests ||
        !qa_source_save_bytes(io, declaration->part_digests,
            declaration->part_digest_count * sizeof(*declaration->part_digests)))) return false;
    maximum = reading ? (io->input.size - io->offset) / sizeof(uint32_t) : SIZE_MAX / sizeof(*declaration->vertices);
    if (!qa_source_save_count(io, &declaration->vertex_count, maximum) ||
        (!declaration->part_digest_count != !declaration->vertex_count)) return false;
    if (reading && declaration->vertex_count) {
        declaration->vertices = calloc(declaration->vertex_count, sizeof(*declaration->vertices));
        if (!declaration->vertices) return frontend_fail(io->error, QA_ERROR_MEMORY, "Retaining restored held vertex qualification");
    }
    if (declaration->vertex_count && !declaration->vertices) return false;
    for (size_t i = 0; i < declaration->vertex_count; ++i) {
        if (!qa_source_save_u32(io, declaration->vertices + i)) return false;
        for (size_t j = 0; j < i; ++j) if (declaration->vertices[i] == declaration->vertices[j]) return false;
    }
    return true;
}

static bool row_fields(qa_source_save_io *io, qa_application_content_graph *graph,
    frontend_equipment_media *row)
{
    uint32_t family = row->family;
    if (!owner_fields(io, &row->provider) || !qa_source_save_u32(io, &family) || family > QA_GAME_Q3 ||
        !qa_source_save_string(io, &row->gear_namespace) ||
        !qa_source_save_u64(io, &row->gear_service_owner) ||
        (!row->gear_namespace != !row->gear_service_owner) ||
        (row->gear_namespace && family != QA_GAME_Q3) ||
        !qa_source_save_string(io, &row->item) || !qa_source_save_bool(io,&row->source_slot) ||
        (row->source_slot&&(!row->item||!qa_source_save_u64(io,&row->source_generation)||row->gear_namespace||row->gear_service_owner)) ||
        !frontend_save_text(io, &row->view_path) || !row->view_path ||
        (row->source_slot?*row->view_path!=0:!*row->view_path))return false;
    if(row->source_slot) {
        uint64_t view=io->direction==QA_SOURCE_SAVE_WRITE?qa_application_content_view_id(graph,row->owner.mounts):0;
        size_t bytes=row->source_held.size;
        if(!qa_source_save_u64(io,&view)||!view||!qa_source_save_count(io,&bytes,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX))return false;
        if(io->direction==QA_SOURCE_SAVE_READ){
            row->source_held.data=bytes?malloc(bytes):NULL;row->source_held.size=bytes;
            if(bytes&&!row->source_held.data)return frontend_fail(io->error,QA_ERROR_MEMORY,"Restoring actual item held declaration");
            if(!qa_application_content_claim_view(graph,view,&row->owner.mounts,io->error))return false;
        }
        if(!qa_source_save_bytes(io,row->source_held.data,bytes))return false;
        bytes=row->source_icon.size;
        if(!qa_source_save_count(io,&bytes,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX))return false;
        if(io->direction==QA_SOURCE_SAVE_READ){row->source_icon.data=bytes?malloc(bytes):NULL;row->source_icon.size=bytes;
            if(bytes&&!row->source_icon.data)return frontend_fail(io->error,QA_ERROR_MEMORY,"Restoring actual item icon declaration");}
        if(!qa_source_save_bytes(io,row->source_icon.data,bytes)||(!bytes&&!row->source_held.size))return false;
        if(bytes) {
            if(io->direction==QA_SOURCE_SAVE_WRITE){
                bool found=false;
                for(size_t i=0;i<qa_material_library_record_count(row->owner.materials);++i)
                    if(qa_material_library_record_at(row->owner.materials,i)==row->icon){row->saved_icon=i;found=true;break;}
                if(!found)return false;
            }
            const qa_resource *source=row->icon_source;
            if(!qa_source_save_count(io,&row->saved_icon,SIZE_MAX)||!resource_fields(io,graph,&source))return false;
            if(io->direction==QA_SOURCE_SAVE_READ)row->icon_source=(qa_resource *)source;
        }
    } else if(!qa_source_save_count(io,&row->saved_owner,SIZE_MAX))return false;
    if (!row->source_slot && family != QA_GAME_Q3 && (!qa_source_save_count(io, &row->saved_view, SIZE_MAX) ||
        !resource_fields(io, graph, &row->view.resource) || !row->view.resource)) return false;
    if (!declaration_fields(io, graph, &row->declaration) ||
        (family == QA_GAME_Q3 && !row->source_slot && !row->declaration.source) ||
        (row->source_slot&&(row->declaration.source||(!row->source_held.size&&!row->declaration.none)))) return false;
    row->family = (qa_game_family)family;
    if (row->declaration.none) return true;
    return (row->source_slot||qa_source_save_count(io, &row->saved_parent, SIZE_MAX)) &&
        frontend_save_text(io, &row->saved_parent_path) && row->saved_parent_path && *row->saved_parent_path &&
        resource_fields(io, graph, &row->held_parent.resource) && row->held_parent.resource &&
        transform_fields(io, &row->held.alignment) && qa_source_save_u32(io, &row->held.reference_frame) &&
        row->held.reference_frame == row->declaration.reference_frame;
}

static bool physical_refs(const qa_frontend *frontend, frontend_equipment_media *row)
{
    if(row->source_slot)return !row->view.resource&&!row->view.model&&!row->view.scene&&
        row->owner.mounts&&row->owner.images&&row->owner.materials&&
        (row->declaration.none||(row->held_parent.resource&&row->held_parent.model&&row->held.model));
    for (size_t i = 0; i < frontend_visual_owner_count(frontend); ++i) {
        frontend_visual_owner_view owner;
        if (!frontend_visual_owner_read(frontend, i, &owner)) return false;
        if (owner.owner != row->provider || owner.mounts != row->owner.mounts ||
            owner.images != row->owner.images || owner.materials != row->owner.materials) continue;
        bool view = row->family == QA_GAME_Q3, parent = row->declaration.none;
        if (view && (row->view.resource || row->view.model || row->view.scene)) return false;
        for (size_t j = 0; j < frontend_visual_model_count(frontend, i); ++j) {
            frontend_visual_model_view model;
            if (!frontend_visual_model_read(frontend, i, j, &model)) return false;
            if (model.scene == row->view.scene && model.model == row->view.model && model.resource == row->view.resource) {
                row->saved_view = j; view = true;
            }
            if (!row->declaration.none && model.scene == row->held_parent.scene &&
                model.model == row->held_parent.model && model.resource == row->held_parent.resource) {
                row->saved_parent = j; parent = true;
            }
        }
        row->saved_owner = i;
        return view && parent;
    }
    return false;
}

bool frontend_equipment_topology_checkpoint(const qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !out || out->data || out->size ||
        (frontend->equipment && frontend->equipment->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment capture requires its actual idle media owner");
    qa_application_content_graph *graph = qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment capture requires the leased actual content graph");
    qa_source_save_io io = {0}; size_t count = frontend_equipment_media_count(frontend);
    uint8_t magic[4] = {'Q','F','E','T'}; uint32_t schema = 5;
    bool ok = qa_source_save_writer(&io, qa_application_session(frontend->application), error) &&
        qa_source_save_bytes(&io, magic, sizeof(magic)) && qa_source_save_u32(&io, &schema) &&
        qa_source_save_count(&io, &count, SIZE_MAX);
    for (const frontend_equipment_media *row = frontend->equipment ? frontend->equipment->media : NULL;
            ok && row; row = row->next) {
        frontend_equipment_media saved = *row;
        saved.saved_parent_path = (char *)row->held_parent.path;
        ok = row->bound && !row->users && (!row->held_scene || qa_scene_model_observation_ready(row->held_scene)) &&
            (row->declaration.none || (row->held_scene && row->held.model)) &&
            frontend_equipment_media_namespace_current(frontend, row) &&
            physical_refs(frontend, &saved) && row_fields(&io, graph, &saved);
    }
    ok = ok && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Equipment media leaves its actual physical content inventory");
    return ok;
}

bool frontend_equipment_prepare_restored(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->equipment)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment import requires an empty isolated media owner");
    qa_application_content_graph *graph = qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment import requires the restored content graph");
    frontend_equipment *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Preparing actual equipment media owner");
    qa_source_save_io io = {0}; size_t count = 0; uint8_t magic[4]; uint32_t schema = 0;
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        qa_source_save_bytes(&io, magic, sizeof(magic)) && !memcmp(magic, "QFET", sizeof(magic)) &&
        qa_source_save_u32(&io, &schema) && schema == 5 && qa_source_save_count(&io, &count, bytes.size / 32);
    for (size_t i = 0; ok && i < count; ++i) {
        frontend_equipment_media *row = calloc(1, sizeof(*row));
        if (!row) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Preparing restored physical equipment media row"); break; }
        if (owner->tail) owner->tail->next = row; else owner->media = row;
        owner->tail = row; row->restoring = true;
        ok = row_fields(&io, graph, row);
        if(ok&&row->source_slot) {
            row->frontend=frontend;
            row->owner.owner=row->provider;
            row->owner.family=row->family==QA_GAME_Q1?QA_SCENE_Q1:row->family==QA_GAME_Q2?QA_SCENE_Q2:QA_SCENE_Q3;
            row->owner.images=qa_scene_resources_create_detached(row->owner.mounts,error);
            row->owner.materials=row->owner.images?qa_material_library_create_detached(row->owner.images,error):NULL;
            row->media=row->owner.materials?qa_media_library_create(row->owner.images,error):NULL;
            ok=row->owner.images&&row->owner.materials&&row->media&&
                (!row->icon_source||qa_resource_pool_find(qa_vfs_resources(row->owner.mounts),qa_resource_id(row->icon_source))==row->icon_source)&&
                (row->declaration.none||qa_resource_pool_find(qa_vfs_resources(row->owner.mounts),
                    qa_resource_id(row->held_parent.resource))==row->held_parent.resource);
        }
        if (ok&&!row->source_slot) ok = frontend_visual_owner_read(frontend, row->saved_owner, &row->owner) &&
            row->owner.owner == row->provider && row->owner.family ==
                (row->family == QA_GAME_Q3 ? QA_SCENE_Q3 : row->family == QA_GAME_Q2 ? QA_SCENE_Q2 : QA_SCENE_Q1) &&
            frontend_equipment_media_namespace_current(frontend, row) &&
            (row->family == QA_GAME_Q3 || qa_resource_pool_find(qa_vfs_resources(row->owner.mounts),
                qa_resource_id(row->view.resource)) == row->view.resource) &&
            (!row->declaration.source || qa_resource_pool_find(qa_vfs_resources(row->owner.mounts),
                qa_resource_id(row->declaration.source)) == row->declaration.source) &&
            (row->declaration.none || qa_resource_pool_find(qa_vfs_resources(row->owner.mounts),
                qa_resource_id(row->held_parent.resource)) == row->held_parent.resource);
        for (const frontend_equipment_media *prior = owner->media; ok && prior != row; prior = prior->next)
            if (prior->provider == row->provider && prior->family == row->family && prior->item == row->item &&
                prior->gear_namespace == row->gear_namespace && prior->gear_service_owner == row->gear_service_owner &&
                prior->view.resource == row->view.resource && !strcmp(prior->view_path, row->view_path) &&
                prior->source_slot==row->source_slot && (!row->source_slot||
                    (prior->source_generation==row->source_generation&&
                     prior->source_held.size==row->source_held.size&&prior->source_icon.size==row->source_icon.size&&
                     (!row->source_held.size||!memcmp(prior->source_held.data,row->source_held.data,row->source_held.size))&&
                     (!row->source_icon.size||!memcmp(prior->source_icon.data,row->source_icon.data,row->source_icon.size))))) ok = false;
    }
    ok = ok && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) {
        while (owner->media) {
            frontend_equipment_media *row = owner->media,*next=row->next;
            if(!frontend_equipment_media_dispose(row,error)){frontend->equipment=owner;return false;}
            owner->media=next;
        }
        free(owner);
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid saved physical equipment media topology");
        return false;
    }
    frontend->equipment = owner;
    return true;
}

static bool restored_refs(const qa_frontend *frontend, const frontend_equipment_media *row,
    frontend_visual_model_view *view, frontend_visual_model_view *parent)
{
    if(row->source_slot){
        if(row->source_icon.size){qa_material_library_record_view icon;
            if(!qa_material_library_record_read(row->owner.materials,row->saved_icon,&icon)||
                icon.kind!=QA_MATERIAL_PICTURE||!icon.material)return false;}
        return row->owner.mounts&&row->owner.images&&row->owner.materials&&
        (row->declaration.none||(row->held_parent.resource&&row->saved_parent_path&&
        (!strcmp(row->saved_parent_path,row->declaration.path)||(row->declaration.fallback&&!strcmp(row->saved_parent_path,row->declaration.fallback)))));}
    if (row->family != QA_GAME_Q3 && (!frontend_visual_model_read(frontend, row->saved_owner, row->saved_view, view) ||
        view->resource != row->view.resource || strcmp(view->path, row->view_path))) return false;
    if (row->declaration.none) return true;
    if (!frontend_visual_model_read(frontend, row->saved_owner, row->saved_parent, parent) ||
        parent->resource != row->held_parent.resource || strcmp(parent->path, row->saved_parent_path) ||
        (strcmp(parent->path, row->declaration.path) &&
            (!row->declaration.fallback || strcmp(parent->path, row->declaration.fallback))) ||
        row->held.reference_frame >= parent->model->frame_count) return false;
    const qa_sha256_digest *digest = qa_resource_digest(parent->resource);
    if (row->declaration.has_digest && !qa_sha256_equal(&row->declaration.digest, digest)) return false;
    if (row->declaration.part_digest_count) {
        bool matched = false;
        for (size_t i = 0; i < row->declaration.part_digest_count; ++i)
            if (qa_sha256_equal(row->declaration.part_digests + i, digest)) matched = true;
        if (!matched || parent->model->format != QA_MODEL_MDL || parent->model->mesh_count != 1) return false;
    }
    return true;
}

bool frontend_equipment_media_bind_restored(qa_frontend *frontend, qa_error *error)
{
    if (!frontend || frontend->stepping || !frontend->equipment || !frontend_equipment_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment binding requires its idle restored media owner");
    for (frontend_equipment_media *row = frontend->equipment->media; row; row = row->next) {
        frontend_visual_model_view view = {0}, parent = {0};
        if (!row->restoring || row->bound || !restored_refs(frontend, row, &view, &parent))
            return frontend_fail(error, QA_ERROR_FORMAT, "Equipment cache ordinals differ from the actual restored holders");
    }
    for (frontend_equipment_media *row = frontend->equipment->media; row; row = row->next) {
        frontend_visual_model_view view = {0}, parent = {0};
        if (!restored_refs(frontend, row, &view, &parent)) return false;
        if(!row->source_slot){row->view = view; row->held_parent = parent;}
        else {row->held_parent.path=row->saved_parent_path;
            if(row->source_icon.size)row->icon=qa_material_library_record_at(row->owner.materials,row->saved_icon);}
        row->bound = true;
    }
    return true;
}

static bool subset_matches(const frontend_equipment_media *row, const qa_model *model)
{
    const qa_model *parent = row->held_parent.model;
    if (model == parent || model->format != QA_MODEL_MDL || model->mesh_count != 1 ||
        model->frame_count != parent->frame_count || model->skin_count != parent->skin_count ||
        !model->meshes || !parent->meshes || model->meshes == parent->meshes ||
        model->frames != parent->frames || model->frame_groups != parent->frame_groups ||
        model->skin_groups != parent->skin_groups || model->skins != parent->skins ||
        model->tags != parent->tags || model->sprites != parent->sprites ||
        model->lods != parent->lods || model->bones != parent->bones ||
        model->bone_matrices != parent->bone_matrices || model->bind_pose != parent->bind_pose ||
        model->gl_commands != parent->gl_commands ||
        model->command_line.data != parent->command_line.data ||
        model->command_line.size != parent->command_line.size ||
        model->source.data != parent->source.data || model->source.size != parent->source.size) return false;
    const qa_model_mesh *actual = model->meshes, *source = parent->meshes;
    if (actual->vertex_count != source->vertex_count || actual->texcoord_count != source->texcoord_count ||
        actual->frame_count != source->frame_count || actual->shader_count != source->shader_count ||
        actual->weight_count != source->weight_count || actual->bone_reference_count != source->bone_reference_count ||
        actual->vertices != source->vertices || actual->texcoords != source->texcoords ||
        actual->shaders != source->shaders || actual->weights != source->weights ||
        actual->vertex_weights != source->vertex_weights || actual->bone_references != source->bone_references ||
        !actual->triangle_count || !actual->triangles || !source->triangles ||
        actual->triangles == source->triangles) return false;
    size_t at = 0;
    for (uint32_t i = 0; i < source->triangle_count; ++i) {
        const qa_model_triangle *triangle = source->triangles + i;
        bool included = true;
        for (size_t corner = 0; corner < 3; ++corner) {
            bool found = false;
            for (size_t j = 0; j < row->declaration.vertex_count; ++j)
                if (row->declaration.vertices[j] == triangle->vertex[corner]) found = true;
            included &= found;
        }
        if (!included) continue;
        if (at >= actual->triangle_count || actual->triangles[at].front != triangle->front) return false;
        for (size_t corner = 0; corner < 3; ++corner)
            if (actual->triangles[at].vertex[corner] != triangle->vertex[corner] ||
                actual->triangles[at].texcoord[corner] != triangle->texcoord[corner]) return false;
        ++at;
    }
    return at == actual->triangle_count;
}

bool frontend_equipment_media_attach_restored(qa_frontend *frontend, size_t ordinal,
    const qa_model *model, qa_scene_model *scene, frontend_model_inventory *models, qa_error *error)
{
    frontend_equipment_media *row = frontend && frontend->equipment ? frontend->equipment->media : NULL;
    while (row && ordinal) { row = row->next; --ordinal; }
    if (!frontend || frontend->stepping || !row || !row->restoring || !row->bound || row->users ||
        row->declaration.none || row->held.model || row->held_scene || row->held_lease || !model || !scene || !models ||
        !qa_scene_model_idle(scene) || qa_scene_model_source(scene) != model ||
        qa_scene_model_resource_owner(scene) != row->owner.images ||
        qa_scene_model_material_owner(scene) != row->owner.materials ||
        row->held.reference_frame >= model->frame_count)
        return frontend_fail(error, QA_ERROR_FORMAT, "Equipment root differs from its actual prepared destructor and media owners");
    frontend_model_lease *lease=NULL;frontend_model_source source={0};
    if(!frontend_model_retain(models,model,&lease,error))return false;
    if(!frontend_model_lease_source(lease,&source)){frontend_model_release(lease);return false;}
    if(row->source_slot)row->held_parent.model=source.parent?source.parent:model;
    const qa_sha256_digest *digest=qa_resource_digest(row->held_parent.resource);
    bool digest_valid=!row->declaration.has_digest||qa_sha256_equal(&row->declaration.digest,digest);
    if(row->declaration.part_digest_count) {
        bool matched=false;
        for(size_t i=0;i<row->declaration.part_digest_count;++i)
            if(qa_sha256_equal(row->declaration.part_digests+i,digest))matched=true;
        digest_valid=digest_valid&&matched&&row->held_parent.model->format==QA_MODEL_MDL&&row->held_parent.model->mesh_count==1;
    }
    if(!digest_valid){frontend_model_release(lease);return frontend_fail(error,QA_ERROR_FORMAT,"Restored held model differs from its actual declared digest");}
    if ((!row->declaration.part_digest_count && model != row->held_parent.model) ||
        (row->declaration.part_digest_count && !subset_matches(row, model)))
        {frontend_model_release(lease);return frontend_fail(error, QA_ERROR_FORMAT, "Equipment root loses its real full or subset parsed-holder identity");}
    if (source.resource != row->held_parent.resource ||
        source.files != row->owner.mounts || source.parent !=
            (row->declaration.part_digest_count ? row->held_parent.model : NULL)) {
        frontend_model_release(lease);
        return frontend_fail(error, QA_ERROR_FORMAT, "Equipment parsed holder leaves its actual retained resource provenance");
    }
    row->held.model = model; row->held_scene = scene; row->held_lease = lease;
    return true;
}

bool frontend_equipment_topology_ready(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !frontend_equipment_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment topology qualification requires idle actual media");
    for (const frontend_equipment_media *row = frontend->equipment ? frontend->equipment->media : NULL;
            row; row = row->next) {
        if (!row->bound || (!row->source_slot&&!qa_application_provider_instance(frontend->application, row->provider)) ||
            !frontend_equipment_media_namespace_current(frontend, row) ||
            (row->source_slot&&(!row->media||
                (!(row->restoring&&frontend->source_restoring&&!row->movies)&&
                 (!row->movies||!frontend_material_movies_current(row->movies))))) ||
            (row->source_slot&&((row->source_icon.size!=0)!=(row->icon!=NULL)||
                (row->icon&&row->icon->library!=row->owner.materials))) ||
            (!row->source_slot && row->family != QA_GAME_Q3 && (!row->view.model || !row->view.scene)) ||
            (row->family == QA_GAME_Q3 && ((!row->source_slot&&!row->declaration.source) || row->view.resource ||
                row->view.model || row->view.scene)) || (!row->declaration.none &&
                (!row->held.model || !row->held_scene || (row->restoring && !row->held_lease))))
            return frontend_fail(error, QA_ERROR_FORMAT, "Equipment topology omits an actual source cache or held root");
    }
    return true;
}
