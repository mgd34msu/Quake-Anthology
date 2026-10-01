#include "internal.h"
#include "visual_restore.h"
#include "save_private.h"
#include "qa/persistence_content.h"
#include "qa/material_library_save.h"
#include "qa/scene_model_save.h"
#include "qa/scene_resource_save.h"

typedef struct frontend_model {
    struct frontend_model *next;
    qa_resource *resource;
    qa_model owned_model;
    const qa_model *model;
    frontend_model_lease *lease;
    qa_scene_model *scene;
    char path[];
} frontend_model;
static bool model_translation_equal(const qa_scene_model *first, const qa_scene_model *second)
{
    const qa_scene_image_options *a = qa_scene_model_image_options(first);
    const qa_scene_image_options *b = qa_scene_model_image_options(second);
    return a && b && a->translation.size == b->translation.size &&
        (!a->translation.size || !memcmp(a->translation.data, b->translation.data, a->translation.size));
}
struct frontend_visual_owner {
    frontend_visual_owner *next;
    qa_actor_owner owner;
    qa_scene_family family;
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    frontend_model *models;
};
static const frontend_visual_owner *owner_at(const qa_frontend *frontend, size_t index)
{
    if (!frontend || !frontend->application || frontend->stepping) return NULL;
    const frontend_visual_owner *owner=frontend->visuals;
    while (owner && index) { owner=owner->next; --index; }
    return owner;
}
size_t frontend_visual_owner_count(const qa_frontend *frontend)
{
    if (!frontend || !frontend->application || frontend->stepping) return 0;
    size_t count=0;
    for (const frontend_visual_owner *owner=frontend->visuals;owner;owner=owner->next) ++count;
    return count;
}
bool frontend_visual_owner_read(const qa_frontend *frontend, size_t index, frontend_visual_owner_view *out)
{
    const frontend_visual_owner *owner=owner_at(frontend,index);
    if (!owner || !out || !owner->owner || !owner->mounts || !owner->images || !owner->materials ||
        qa_scene_resources_files(owner->images)!=owner->mounts || qa_material_library_resource_owner(owner->materials)!=owner->images)
        return false;
    *out=(frontend_visual_owner_view){owner->owner,owner->family,owner->mounts,owner->images,owner->materials}; return true;
}
size_t frontend_visual_model_count(const qa_frontend *frontend, size_t index)
{
    const frontend_visual_owner *owner=owner_at(frontend,index); size_t count=0;
    if (owner) for (const frontend_model *model=owner->models;model;model=model->next) ++count;
    return count;
}
bool frontend_visual_model_read(const qa_frontend *frontend, size_t index, size_t ordinal, frontend_visual_model_view *out)
{
    const frontend_visual_owner *owner=owner_at(frontend,index);
    const frontend_model *model=owner?owner->models:NULL;
    while (model && ordinal) { model=model->next; --ordinal; }
    if (!model || !out || !model->resource || !model->path[0]) return false;
    *out=(frontend_visual_model_view){model->path,model->resource,model->model,model->scene}; return true;
}
bool frontend_visual_model_attach_restored(qa_frontend *frontend, size_t owner_index, const char *path,
    const qa_resource *resource, const qa_model *parsed, qa_scene_model *scene,
    frontend_model_inventory *inventory, qa_error *error)
{
    frontend_visual_owner *owner=(frontend_visual_owner *)owner_at(frontend,owner_index);
    if (!owner || !path || !*path || !resource || !parsed || !scene || !inventory || !qa_scene_model_idle(scene) ||
        qa_scene_model_source(scene)!=parsed || qa_scene_model_resource_owner(scene)!=owner->images ||
        qa_scene_model_material_owner(scene)!=owner->materials ||
        qa_resource_pool_find(qa_vfs_resources(owner->mounts),qa_resource_id(resource))!=resource)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Restored appearance model lacks its actual content and scene owners");
    qa_bytes bytes=qa_resource_bytes(resource);
    if (parsed->source.size!=bytes.size || (bytes.size && memcmp(parsed->source.data,bytes.data,bytes.size)))
        return frontend_fail(error,QA_ERROR_FORMAT,"Restored appearance parsed holder differs from its retained source");
    for (frontend_visual_owner *other=frontend->visuals;other;other=other->next)
        for (const frontend_model *model=other->models;model;model=model->next)
            if (model->scene==scene || (other==owner && model->resource==resource &&
                !strcmp(model->path,path) && model_translation_equal(model->scene,scene)))
                return frontend_fail(error,QA_ERROR_FORMAT,"Restored appearance repeats a scene owner or path");
    size_t length=strlen(path);
    if (length>SIZE_MAX-sizeof(frontend_model)-1)
        return frontend_fail(error,QA_ERROR_MEMORY,"Restored appearance path exceeds address space");
    frontend_model *model=calloc(1,sizeof(*model)+length+1);
    if (!model) return frontend_fail(error,QA_ERROR_MEMORY,"Attaching restored appearance holder");
    if (!frontend_model_retain(inventory,parsed,&model->lease,error)) { free(model); return false; }
    memcpy(model->path,path,length+1); model->model=parsed; model->scene=scene;
    model->resource=(qa_resource *)resource; qa_resource_retain(model->resource);
    frontend_model **tail=&owner->models;
    while (*tail) tail=&(*tail)->next;
    *tail=model; return true;
}
typedef struct visual_owner_plan { qa_actor_owner owner; qa_scene_family family; uint64_t view; } visual_owner_plan;
static bool topology_fields(qa_source_save_io *io, qa_application *application, visual_owner_plan **plans, size_t *count)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; uint8_t magic[4]={'Q','F','V','T'}; uint32_t schema=1;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFVT",4) || !qa_source_save_u32(io,&schema) || schema!=1 ||
        !qa_source_save_count(io,count,reading?io->input.size/22:SIZE_MAX/sizeof(**plans))) return false;
    if (reading && *count) {
        *plans=calloc(*count,sizeof(**plans));
        if (!*plans) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining actual appearance owner topology");
    }
    qa_strings *strings=qa_session_strings(qa_application_session(application));
    for (size_t i=0;i<*count;++i) {
        visual_owner_plan *plan=&(*plans)[i]; uint32_t family=plan->family;
        char *key=!reading && plan->owner<=UINT32_MAX?(char *)qa_strings_cstr(strings,(qa_string_id)plan->owner):NULL;
        if (!reading && (!plan->owner || !key)) return false;
        bool ok=frontend_save_text(io,&key);
        if (reading) {
            plan->owner=key?qa_strings_find(strings,(qa_bytes){(const uint8_t *)key,strlen(key)}):0;
            free(key);
        }
        if (!ok || !plan->owner || !qa_source_save_u32(io,&family) || family>QA_SCENE_Q3 ||
            !qa_source_save_u64(io,&plan->view) || !plan->view) return false;
        plan->family=(qa_scene_family)family;
        for (size_t j=0;j<i;++j) if (plan->owner==(*plans)[j].owner || plan->view==(*plans)[j].view) return false;
    }
    return true;
}
bool frontend_visual_topology_checkpoint(const qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Appearance topology capture requires idle actual owners and empty output");
    qa_application_content_graph *graph=qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error,QA_ERROR_ARGUMENT,"Appearance topology requires actual content graph lease");
    size_t count=frontend_visual_owner_count(frontend);
    visual_owner_plan *plans=count?calloc(count,sizeof(*plans)):NULL;
    if (count && !plans) return frontend_fail(error,QA_ERROR_MEMORY,"Capturing appearance owner topology");
    bool ok=true;
    for (size_t i=0;ok && i<count;++i) {
        frontend_visual_owner_view view;
        ok=frontend_visual_owner_read(frontend,i,&view) && qa_application_provider_instance(frontend->application,view.owner);
        if (ok) {
            uint64_t id=qa_application_content_view_id(graph,view.mounts);
            ok=id!=0; plans[i]=(visual_owner_plan){view.owner,view.family,id};
        }
    }
    qa_source_save_io io={0};
    ok=ok && qa_source_save_writer(&io,qa_application_session(frontend->application),error) &&
        topology_fields(&io,frontend->application,&plans,&count) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); free(plans);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Unqualified appearance owner topology");
    return ok;
}
bool frontend_visual_prepare_restored(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->visuals)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Appearance preparation requires empty isolated owner list");
    qa_application_content_graph *graph=qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error,QA_ERROR_ARGUMENT,"Appearance preparation requires preloaded content graph");
    visual_owner_plan *plans=NULL; size_t count=0; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,qa_application_session(frontend->application),bytes,error) &&
        topology_fields(&io,frontend->application,&plans,&count) && qa_source_save_finish(&io,NULL);
    for (size_t i=0;ok && i<count;++i) ok=frontend->order && qa_application_content_view(graph,plans[i].view);
    frontend_visual_owner **tail=&frontend->visuals;
    for (size_t i=0;ok && i<count;++i) {
        frontend_visual_owner *owner=calloc(1,sizeof(*owner));
        if (!owner) { ok=frontend_fail(error,QA_ERROR_MEMORY,"Preparing genuine appearance owner"); break; }
        owner->owner=plans[i].owner; owner->family=plans[i].family; *tail=owner; tail=&owner->next;
        ok=qa_application_content_claim_view(graph,plans[i].view,&owner->mounts,error);
        if (ok) ok=(owner->images=qa_scene_resources_create_detached(owner->mounts,error))!=NULL;
        if (ok) ok=(owner->materials=qa_material_library_create_detached(owner->images,error))!=NULL;
    }
    free(plans); qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Invalid saved appearance owner topology");
    return ok;
}
bool frontend_visual_topology_ready(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Appearance qualification requires idle actual owners");
    size_t count=frontend_visual_owner_count(frontend);
    for (size_t i=0;i<count;++i) {
        frontend_visual_owner_view view;
        if (!frontend_visual_owner_read(frontend,i,&view) || !qa_application_provider_instance(frontend->application,view.owner))
            return frontend_fail(error,QA_ERROR_FORMAT,"Appearance owner was not bound to actual prepared source");
    }
    return true;
}
const qa_scene_resources *frontend_visual_images_at(qa_frontend *frontend, size_t index)
{
    if (!frontend) return NULL;
    frontend_visual_owner *owner = frontend->visuals;
    while (owner && index) { owner = owner->next; --index; }
    return owner ? owner->images : NULL;
}
bool frontend_visuals_remap(qa_frontend *frontend, const char *original, const char *replacement, float offset, qa_error *error)
{
    for (frontend_visual_owner *owner = frontend->visuals; owner; owner = owner->next)
        if (!qa_material_remap(owner->materials, original, replacement, offset, error)) return false;
    return true;
}
bool frontend_visuals_idle(const qa_frontend *frontend)
{
    if (!frontend) return false;
    for (const frontend_visual_owner *owner=frontend->visuals;owner;owner=owner->next) {
        if ((owner->materials && !qa_material_library_idle(owner->materials)) ||
            (owner->images && !qa_scene_resources_idle(owner->images))) return false;
        for (const frontend_model *model=owner->models;model;model=model->next)
            if (!qa_scene_model_idle(model->scene)) return false;
    }
    return true;
}
void frontend_visuals_destroy(qa_frontend *frontend)
{
    if (!frontend_visuals_idle(frontend)) return;
    while (frontend->visuals) {
        frontend_visual_owner *owner = frontend->visuals;
        frontend->visuals = owner->next;
        while (owner->models) {
            frontend_model *model = owner->models; owner->models = model->next;
            qa_scene_model_destroy(model->scene); qa_model_free(&model->owned_model);
            frontend_model_release(model->lease);
            qa_resource_release(model->resource); free(model);
        }
        qa_material_library_destroy(owner->materials);
        qa_scene_resources_destroy(owner->images); qa_vfs_destroy(owner->mounts); free(owner);
    }
}
static bool visual_owner(qa_frontend *frontend, const qa_application_visual_view *view,
    frontend_visual_owner **out, qa_error *error)
{
    for (frontend_visual_owner *owner = frontend->visuals; owner; owner = owner->next)
        if (owner->owner == view->provider) { *out = owner; return true; }
    qa_command_context source = {.owner = view->provider, .origin = QA_COMMAND_SERVER,
        .dialect = view->family == QA_GAME_Q2 ? QA_CONSOLE_Q2 : view->family == QA_GAME_Q3 ? QA_CONSOLE_Q3 : QA_CONSOLE_Q1};
    qa_command_context captured;
    if (!qa_application_capture_command_context(frontend->application, &source, &captured, error)) return false;
    qa_vfs *files = qa_application_context_files(frontend->application, &captured, NULL);
    if (!files) return frontend_fail(error, QA_ERROR_NOT_FOUND, "appearance owner has no active content view");
    frontend_visual_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "allocating appearance resources");
    owner->owner = view->provider;
    owner->family = view->family == QA_GAME_Q2 ? QA_SCENE_Q2 : view->family == QA_GAME_Q3 ? QA_SCENE_Q3 : QA_SCENE_Q1;
    owner->mounts = qa_vfs_clone(files, error);
    owner->images = owner->mounts ? qa_scene_resources_create(owner->mounts, error) : NULL;
    owner->materials = owner->images ? qa_material_library_create(owner->images, frontend->order, error) : NULL;
    qa_scene_image_options images = {.family = owner->family, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .transparent_index = -1};
    bool ok = owner->mounts && owner->images && owner->materials &&
        qa_material_library_load_scripts(owner->materials, owner->mounts, &images, error) &&
        frontend_material_remaps(frontend, owner->materials, error);
    if (!ok) {
        qa_material_library_destroy(owner->materials); qa_scene_resources_destroy(owner->images);
        qa_vfs_destroy(owner->mounts); free(owner); return false;
    }
    owner->next = frontend->visuals; frontend->visuals = owner; *out = owner;
    return true;
}
static void player_translation(uint8_t colors, uint8_t out[256])
{
    for (unsigned i = 0; i < 256; ++i) out[i] = (uint8_t)i;
    unsigned top = colors & 240u, bottom = (colors & 15u) << 4;
    for (unsigned i = 0; i < 16; ++i) {
        out[16 + i] = (uint8_t)(top < 128 ? top + i : top + 15 - i);
        out[96 + i] = (uint8_t)(bottom < 128 ? bottom + i : bottom + 15 - i);
    }
}
static bool model_read(frontend_visual_owner *owner, const char *path, const qa_resource *source,
    bool colored, uint8_t colors, frontend_model **out, qa_error *error)
{
    uint8_t translation[256];
    if (colored) player_translation(colors, translation);
    for (frontend_model *model = owner->models; model; model = model->next) {
        if (strcmp(model->path, path) || (source && model->resource != source)) continue;
        const qa_scene_image_options *options = qa_scene_model_image_options(model->scene);
        bool translated = colored && model->model->format == QA_MODEL_MDL;
        if (options && options->translation.size == (translated ? sizeof(translation) : 0) &&
            (!translated || !memcmp(options->translation.data, translation, sizeof(translation)))) {
            *out = model;
            return true;
        }
    }
    size_t length = strlen(path);
    if (length > SIZE_MAX - sizeof(frontend_model) - 1)
        return frontend_fail(error, QA_ERROR_MEMORY, "appearance path exceeds native storage");
    frontend_model *model = calloc(1, sizeof(*model) + length + 1);
    if (!model) return frontend_fail(error, QA_ERROR_MEMORY, "allocating decoded appearance");
    memcpy(model->path, path, length + 1);
    qa_scene_image_options images = {.family = owner->family, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .usage = QA_IMAGE_USAGE_SKIN,
        .transparent_index = owner->family == QA_SCENE_Q1 ? 255 : -1};
    bool ok;
    if (source) {
        ok = qa_resource_pool_find(qa_vfs_resources(owner->mounts), qa_resource_id(source)) == source;
        if (!ok) frontend_fail(error, QA_ERROR_NOT_FOUND, "Appearance model lost its actual retained content owner");
        else { model->resource = (qa_resource *)source; qa_resource_retain(model->resource); }
    } else ok = qa_vfs_acquire(owner->mounts, path, &model->resource, NULL, error);
    if (ok) ok = qa_model_load(qa_resource_bytes(model->resource), &model->owned_model, error);
    model->model=&model->owned_model;
    if (colored && model->model->format == QA_MODEL_MDL)
        images.translation = (qa_bytes){translation, sizeof(translation)};
    if (ok) ok=qa_scene_model_create(model->model, owner->images, owner->materials, &images, &model->scene, error);
    if (!ok) {
        qa_scene_model_destroy(model->scene); qa_model_free(&model->owned_model);
        qa_resource_release(model->resource); free(model); return false;
    }
    model->next = owner->models; owner->models = model; *out = model; return true;
}
static qa_model_transform transform(const qa_application_visual_view *view)
{
    qa_model_transform value;
    qa_model_transform_identity(&value);
    qa_vec3 axes[3]; frontend_camera_axes(view->body.angles, axes);
    value.origin[0] = view->body.origin.x; value.origin[1] = view->body.origin.y; value.origin[2] = view->body.origin.z;
    for (unsigned i = 0; i < 3; ++i) {
        value.axes[i][0] = axes[i].x; value.axes[i][1] = axes[i].y; value.axes[i][2] = axes[i].z;
        value.scale[i] = view->scale;
    }
    return value;
}
bool frontend_visuals_submit(qa_frontend *frontend, uint32_t seat, qa_actor_owner exclude,
    const qa_scene_world_input *world, qa_scene_frame *frame, qa_error *error)
{
    qa_actor_id local = {0};
    (void)qa_application_player_actor(frontend->application, seat, &local);
    const qa_actor_record *record; uint32_t cursor = 0;
    qa_actor_registry *actors = qa_world_actors(qa_application_world(frontend->application));
    while (qa_actors_next(actors, &cursor, &record)) {
        qa_actor_id actor = record->id;
        if (qa_actor_id_equal(actor, local) && !world->view.clip_enabled) continue;
        qa_application_visual_view view; qa_error observed = {0};
        if (!qa_application_visual_read(frontend->application, actor, &view, &observed)) {
            if (observed.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = observed;
            return false;
        }
        if (!view.visible || view.alpha <= 0 || view.scale <= 0) continue;
        if (exclude && view.provider == exclude) continue;
        qa_model_transform placement = transform(&view);
        qa_scene_vec4 color = {1, 1, 1, view.alpha};
        if (view.has_inline_model) {
            if (!qa_scene_world_submit_model(frontend->scene_world, view.inline_model,
                &placement, world, actor.slot, color, frame, error)) return false;
            continue;
        }
        frontend_visual_owner *owner = NULL;
        for (unsigned part = 0; part < 4; ++part) {
            const char *path = view.models[part];
            if (!path || !*path) continue;
            if (!owner && !visual_owner(frontend, &view, &owner, error)) return false;
            frontend_model *model;
            if (!model_read(owner, path, view.model_resources[part],
                view.family == QA_GAME_Q1 && view.has_player_colors, view.player_colors, &model, error)) return false;
            qa_scene_model_input input = {.view = world->view, .transform = placement,
                .previous_origin = view.body.origin, .color = color, .family = owner->family,
                .frame = view.frame >= 0 ? (uint32_t)view.frame : 0,
                .old_frame = view.old_frame >= 0 ? (uint32_t)view.old_frame : view.frame >= 0 ? (uint32_t)view.frame : 0,
                .skin = view.skin >= 0 ? (uint32_t)view.skin : 0, .flags = view.render_flags,
                .entity = actor.slot, .identity_light = world->identity_light, .seconds = world->seconds,
                .ambient = {1, 1, 1}, .fog = world->fog, .source_path = path};
            qa_scene_world_sample_light(frontend->scene_world, view.body.origin,
                &input.ambient, &input.directed, &input.light_direction);
            if (!qa_scene_model_submit(model->scene, &input, frame, error)) return false;
        }
    }
    return true;
}
