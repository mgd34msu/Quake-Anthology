#include "internal.h"

typedef struct frontend_model {
    struct frontend_model *next;
    qa_resource *resource;
    qa_model model;
    qa_scene_model *scene;
    char path[];
} frontend_model;
struct frontend_visual_owner {
    frontend_visual_owner *next;
    qa_actor_owner owner;
    qa_scene_family family;
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    frontend_model *models;
};
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
void frontend_visuals_destroy(qa_frontend *frontend)
{
    while (frontend->visuals) {
        frontend_visual_owner *owner = frontend->visuals;
        frontend->visuals = owner->next;
        while (owner->models) {
            frontend_model *model = owner->models; owner->models = model->next;
            qa_scene_model_destroy(model->scene); qa_model_free(&model->model);
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
static bool model_read(frontend_visual_owner *owner, const char *path, frontend_model **out, qa_error *error)
{
    for (frontend_model *model = owner->models; model; model = model->next)
        if (!strcmp(model->path, path)) { *out = model; return true; }
    size_t length = strlen(path);
    if (length > SIZE_MAX - sizeof(frontend_model) - 1)
        return frontend_fail(error, QA_ERROR_MEMORY, "appearance path exceeds native storage");
    frontend_model *model = calloc(1, sizeof(*model) + length + 1);
    if (!model) return frontend_fail(error, QA_ERROR_MEMORY, "allocating decoded appearance");
    memcpy(model->path, path, length + 1);
    qa_scene_image_options images = {.family = owner->family, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .usage = QA_IMAGE_USAGE_SKIN,
        .transparent_index = owner->family == QA_SCENE_Q1 ? 255 : -1};
    bool ok = qa_vfs_acquire(owner->mounts, path, &model->resource, NULL, error) &&
        qa_model_load(qa_resource_bytes(model->resource), &model->model, error) &&
        qa_scene_model_create(&model->model, owner->images, owner->materials, &images, &model->scene, error);
    if (!ok) {
        qa_scene_model_destroy(model->scene); qa_model_free(&model->model);
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
            if (!model_read(owner, path, &model, error)) return false;
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
