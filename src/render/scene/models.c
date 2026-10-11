#include "models/internal.h"
#include "resources_internal.h"
#include "../controls_private.h"
#include "qa/scene_model_save.h"
#include <fenv.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>

static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
struct qa_scene_model_capture { qa_scene_model *root; };
static bool model_idle(const qa_scene_model *model, bool include_capture)
{
    if (!model) return false;
    while (model->replacement_parent) model = model->replacement_parent;
    const qa_scene_model *root = model;
    for (;;) {
        if (model->active_submissions || model->checkpoint_active || (include_capture && model->capture)) return false;
        if (model->replacement) { model = model->replacement; continue; }
        while (model != root && !model->replacement_next) model = model->replacement_parent;
        if (model == root) return true;
        model = model->replacement_next;
    }
}
bool qa_scene_model_idle(const qa_scene_model *model) { return model_idle(model,true); }
bool qa_scene_model_observation_ready(const qa_scene_model *model) { return model_idle(model,false); }
bool qa_scene_model_capture_begin(const qa_scene_model *model, qa_scene_model_capture **out, qa_error *error)
{
    if (!model || !out || *out || model->replacement_parent || model->replacement_next || !qa_scene_model_idle(model))
        return fail(error,QA_ERROR_ARGUMENT,"Model aggregate capture requires an idle owned root and empty token");
    qa_scene_model_capture *capture=malloc(sizeof(*capture));
    if (!capture) return fail(error,QA_ERROR_MEMORY,"Retaining the model owner capture lease");
    capture->root=(qa_scene_model *)model; capture->root->capture=capture; *out=capture; return true;
}
void qa_scene_model_capture_end(qa_scene_model_capture *capture)
{
    if (!capture) return;
    if (capture->root->capture==capture) capture->root->capture=NULL;
    free(capture);
}
const qa_model *qa_scene_model_source(const qa_scene_model *model) { return model ? model->source : NULL; }
const qa_scene_image_options *qa_scene_model_image_options(const qa_scene_model *model) { return model ? &model->options : NULL; }
qa_scene_resources *qa_scene_model_resource_owner(const qa_scene_model *model) { return model ? model->resources : NULL; }
qa_material_library *qa_scene_model_material_owner(const qa_scene_model *model) { return model ? model->materials : NULL; }
bool qa_scene_model_content_read(const qa_scene_model *model, qa_scene_model_content_kind kind,
    qa_scene_model_content_lease *out)
{
    if (!model || !out || !qa_scene_model_observation_ready(model)) return false;
    qa_scene_model_content_lease lease;
    switch (kind) {
    case QA_SCENE_MODEL_CONTENT_SOURCE: lease=model->source_lease; break;
    case QA_SCENE_MODEL_CONTENT_REPLACEMENT_SOURCE: lease=model->replacement_source_lease; break;
    case QA_SCENE_MODEL_CONTENT_ANIMATION: lease=model->animation_lease; break;
    default: return false;
    }
    if ((lease.context!=NULL)!=(lease.release!=NULL)) return false;
    *out=lease; return true;
}
const qa_scene_mesh *qa_scene_model_mesh_at(const qa_scene_model *model, size_t index)
{ return model && index < model->source->mesh_count ? &model->meshes[index].retained : NULL; }
const qa_scene_model *qa_scene_model_replacement_first(const qa_scene_model *model) { return model ? model->replacement : NULL; }
const qa_scene_model *qa_scene_model_replacement_next(const qa_scene_model *model) { return model ? model->replacement_next : NULL; }
const qa_model_replacement *qa_scene_model_replacement_description(const qa_scene_model *model) { return model ? model->replacement_source : NULL; }
uint64_t qa_scene_model_identity(const qa_scene_model *model) { return model ? model->identity : 0; }
size_t qa_scene_model_shadow_identity_count(const qa_scene_model *model)
{
    size_t count = 0;
    if (model) for (const scene_model_shadow_identity *entry = model->shadow_identities; entry; entry = entry->next) ++count;
    return count;
}
bool qa_scene_model_shadow_identity_at(const qa_scene_model *model, size_t index, uint32_t *entity, uint64_t *identity)
{
    if (!model || !entity || !identity) return false;
    const scene_model_shadow_identity *entry = model->shadow_identities;
    while (entry && index) { entry = entry->next; --index; }
    if (!entry) return false;
    *entity = entry->entity; *identity = entry->identity; return true;
}
static bool model_array(size_t count, size_t size, void **out, qa_error *error) {
    if (count > SIZE_MAX / size) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "model array exceeds addressable storage"); return false;
    }
    *out = calloc(count ? count : 1, size);
    if (!*out) { qa_error_set(error, QA_ERROR_MEMORY, 0, "retained model array allocation failed"); return false; }
    return true;
}

bool qa_scene_model_create(const qa_model *source, qa_scene_resources *resources,
                           qa_material_library *materials, const qa_scene_image_options *options,
                           qa_strings *strings, qa_scene_model **out, qa_error *error) {
    if (!source || !resources || !options || !strings || !out ||
        (options->family == QA_GAME_Q3 && !materials) ||
        (options->palette_rgb.size && (options->palette_rgb.size != 768 || !options->palette_rgb.data)) ||
        (options->translation.size && (options->translation.size != 256 || !options->translation.data))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid retained model services or palette options"); return false;
    }
    qa_scene_model *model = calloc(1, sizeof(*model));
    if (!model) { qa_error_set(error, QA_ERROR_MEMORY, 0, "retained model allocation failed"); return false; }
    model->source = source; model->resources = resources; model->materials = materials;
    model->strings = strings; qa_strings_retain(strings);
    model->identity = qa_scene_identity(); model->options = *options;
    model->options.usage = source->format == QA_MODEL_SPR || source->format == QA_MODEL_SP2 ?
        QA_IMAGE_USAGE_SPRITE : QA_IMAGE_USAGE_SKIN;
    if (options->palette_rgb.size) {
        memcpy(model->palette, options->palette_rgb.data, sizeof(model->palette));
        model->options.palette_rgb = (qa_bytes){model->palette, sizeof(model->palette)};
    } else if (source->format == QA_MODEL_MDL || source->format == QA_MODEL_SPR || options->family == QA_GAME_Q2) {
        qa_bytes palette;
        qa_game_family palette_family = source->format == QA_MODEL_MDL || source->format == QA_MODEL_SPR ? QA_GAME_Q1 : options->family;
        if (!qa_scene_resources_palette(resources, palette_family, &palette, error)) goto fail;
        if (palette.size != sizeof(model->palette)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "model palette has the wrong size"); goto fail;
        }
        memcpy(model->palette, palette.data, sizeof(model->palette));
        model->options.palette_rgb = (qa_bytes){model->palette, sizeof(model->palette)};
    }
    if (options->translation.size) {
        memcpy(model->translation, options->translation.data, sizeof(model->translation));
        model->options.translation = (qa_bytes){model->translation, sizeof(model->translation)};
    }
    if (source->format == QA_MODEL_SP2) {
        model->options.transparent = true; model->options.transparent_index = 255;
        model->options.mipmap = false;
    }
    void *allocation;
    if (source->format == QA_MODEL_MD5) {
        size_t joints = source->bone_count;
        if (joints > SIZE_MAX / (2 * SCENE_MODEL_POSE_VARIANTS * sizeof(*model->sampled_pose_frames))) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "model pose storage exceeds addressable extent"); goto fail;
        }
        if (!model_array((size_t)source->bone_count * SCENE_MODEL_POSE_VARIANTS, sizeof(*model->sampled_pose), &allocation, error)) goto fail;
        model->sampled_pose = allocation;
        if (!model_array((size_t)source->bone_count * 2 * SCENE_MODEL_POSE_VARIANTS, sizeof(*model->sampled_pose_frames), &allocation, error)) goto fail;
        model->sampled_pose_frames = allocation;
        for (unsigned i = 0; i < SCENE_MODEL_POSE_VARIANTS; ++i) {
            model->poses[i].pose = model->sampled_pose + (size_t)i * source->bone_count;
            model->poses[i].frames = model->sampled_pose_frames + (size_t)i * source->bone_count * 2;
        }
    }
    if (!model_array(source->mesh_count, sizeof(*model->meshes), &allocation, error)) goto fail;
    model->meshes = allocation;
    for (uint32_t i = 0; i < source->mesh_count; ++i) {
        char generated[32];
        const char *name = source->meshes[i].name;
        if (source->format == QA_MODEL_MD5) { snprintf(generated, sizeof(generated), "mesh%u", i); name = generated; }
        else if (source->format == QA_MODEL_MD2 || source->format == QA_MODEL_MDL) name = "alias";
        if (!scene_model_topology(model, i, error) ||
            !qa_strings_intern_cstr(strings, name, &model->meshes[i].surface, error)) goto fail;
    }
    if (!model_array(source->skin_count, sizeof(*model->skins), &allocation, error)) goto fail;
    model->skins = allocation;
    for (uint32_t i = 0; i < source->skin_count; ++i) {
        if (source->format == QA_MODEL_MDL) {
            char name[96];
            snprintf(name, sizeof(name), "*model:%llu:skin:%u", (unsigned long long)model->identity, i);
            if (!scene_model_indexed(model, name, source->skins[i].pixels, source->skin_width,
                                      source->skin_height, false, &model->skins[i], error)) goto fail;
        } else if (source->skins[i].name[0] && !scene_model_external(model, source->skins[i].name, NULL, &model->skins[i], error)) goto fail;
    }
    if (!model_array(source->sprite_count, sizeof(*model->sprites), &allocation, error)) goto fail;
    model->sprites = allocation;
    for (uint32_t i = 0; i < source->sprite_count; ++i) {
        const qa_model_sprite *sprite = &source->sprites[i];
        if (source->format == QA_MODEL_SPR) {
            char name[96];
            snprintf(name, sizeof(name), "*model:%llu:sprite:%u", (unsigned long long)model->identity, i);
            if (!scene_model_indexed(model, name, sprite->pixels, sprite->width, sprite->height,
                                      true, &model->sprites[i], error)) goto fail;
        } else if (!scene_model_external(model, sprite->image, NULL, &model->sprites[i], error)) goto fail;
    }
    *out = model;
    return true;
fail:
    qa_scene_model_destroy(model); return false;
}

static void replacement_destroy(qa_scene_model *model) {
    if (model->replacement_skins) {
        for (uint32_t i = 0; i < model->source->mesh_count; ++i) free(model->replacement_skins[i]);
        free(model->replacement_skins); model->replacement_skins = NULL;
    }
    qa_scene_model *replacement = model->replacement;
    model->replacement = NULL;
    while (replacement) {
        qa_scene_model *next = replacement->replacement_next;
        replacement->replacement_next = NULL;
        replacement->replacement_parent = NULL;
        qa_scene_model_destroy(replacement); replacement = next;
    }
    model->replacement = NULL;
    model->replacement_source = NULL;
}

void qa_scene_model_destroy(qa_scene_model *model) {
    if (!model) return;
    if (model->replacement_parent || !qa_scene_model_idle(model)) return;
    if (model->frame_references) { model->destroy_pending = true; return; }
    replacement_destroy(model);
    scene_model_topology_destroy(model);
    free(model->sampled_pose);
    free(model->sampled_pose_frames);
    scene_model_images_destroy(model);
    scene_model_shadow_identity *identity = model->shadow_identities;
    while (identity) { scene_model_shadow_identity *next = identity->next; free(identity); identity = next; }
    if (model->animation_lease.release) model->animation_lease.release(model->animation_lease.context);
    if (model->replacement_source_lease.release) model->replacement_source_lease.release(model->replacement_source_lease.context);
    if (model->source_lease.release) model->source_lease.release(model->source_lease.context);
    qa_strings_destroy(model->strings);
    free(model);
}

bool scene_model_frame_retain(qa_scene_model_pin *pin, qa_scene_model *model,
    const qa_model_pose *pose, qa_error *error)
{
    unsigned index = SCENE_MODEL_TRANSIENT_SAMPLE;
    if (pose && pose == model->source->bind_pose) index = SCENE_MODEL_BIND_SAMPLE;
    else for (unsigned i = 0; i < SCENE_MODEL_POSE_VARIANTS; ++i)
        if (model->poses[i].ready && pose == model->poses[i].pose) { index = i; break; }
    if (index == SCENE_MODEL_TRANSIENT_SAMPLE || model->destroy_pending || pin->model ||
        !model->source_lease.context || !model->source_lease.release)
        return fail(error, QA_ERROR_ARGUMENT, "scene model pin requires its retained pose slot");
    unsigned *references = index == SCENE_MODEL_BIND_SAMPLE ?
        &model->bind_frame_references : &model->poses[index].frame_references;
    if (*references == UINT_MAX || model->frame_references == UINT_MAX)
        return fail(error, QA_ERROR_MEMORY, "scene model frame reference count overflow");
    *pin = (qa_scene_model_pin){.model = model, .pose = pose};
    ++model->frame_references;
    ++*references;
    return true;
}

void scene_model_frame_release(qa_scene_model_pin *pin)
{
    qa_scene_model *model = pin->model;
    if (!model) return;
    if (pin->pose == model->source->bind_pose) --model->bind_frame_references;
    else for (unsigned i = 0; i < SCENE_MODEL_POSE_VARIANTS; ++i)
        if (pin->pose == model->poses[i].pose) { --model->poses[i].frame_references; break; }
    --model->frame_references;
    *pin = (qa_scene_model_pin){0};
    if (!model->frame_references && model->destroy_pending) qa_scene_model_destroy(model);
}

uint32_t qa_scene_model_effect_flags(const qa_scene_model *model) {
    if (!model) return 0;
    if (model->source->format == QA_MODEL_MDL) return (uint32_t)model->source->flags;
    if (model->replacement_source) return (uint32_t)model->replacement_source->flags;
    return model->replacement ? (uint32_t)model->replacement->replacement_source->flags : 0;
}

uint32_t qa_scene_model_select_lod(const qa_scene_model_input *input, uint32_t count,
                                   float radius, float lod_scale, float lod_bias) {
    if (!input || !count || !isfinite(radius) || !isfinite(lod_scale) || !isfinite(lod_bias)) return 0;
    int64_t lod = 0;
    if (count > 1) {
        float distance = qa_vec_dot(input->view.axis[0], model_origin(input)) - qa_vec_dot(input->view.axis[0], input->view.origin);
        const float *matrix = input->view.projection.m;
        float projected = 0;
        if (distance > 0) {
            float numerator = radius * matrix[5] - distance * matrix[9] + matrix[13];
            float denominator = radius * matrix[7] - distance * matrix[11] + matrix[15];
            projected = fminf(numerator / denominator, 1);
        }
        float fraction = projected != 0 ? 1 - projected * fminf(lod_scale, 20) : 0;
        float selected = fraction * (float)count;
        if (!isfinite(selected)) selected = 0;
        if (selected >= (float)(count - 1)) lod = count - 1;
        else if (selected > 0) lod = (int64_t)selected;
    }
    double biased = (double)lod + lod_bias;
    if (biased <= 0) return 0;
    if (biased >= count - 1) return count - 1;
    return (uint32_t)biased;
}

static bool replacement_skin_path(const qa_model_replacement *replacement, uint32_t mesh,
                                   uint32_t skin, uint32_t frame, char **out, qa_error *error) {
    if (replacement->source->format == QA_MODEL_MD2)
        return qa_model_md5_skin_path(replacement->source->skins[skin].name, out, error);
    const qa_model_mesh *surface = &replacement->mesh->meshes[mesh];
    if (!surface->shader_count) { *out = NULL; return true; }
    qa_bytes shader = qa_model_shader_name(&surface->shaders[0]);
    if (shader.size == SIZE_MAX || (shader.size && (!shader.data || memchr(shader.data, 0, shader.size)))) {
        qa_error_set(error, QA_ERROR_FORMAT, mesh, "invalid replacement shader name"); return false;
    }
    char *name = malloc(shader.size + 1);
    if (!name) { qa_error_set(error, QA_ERROR_MEMORY, mesh, "replacement shader name allocation failed"); return false; }
    memcpy(name, shader.data, shader.size); name[shader.size] = 0;
    char *base = NULL;
    bool ok = qa_model_q1_skin_path(name, skin, frame, &base, error);
    free(name);
    if (!ok) return false;
    size_t length = strlen(base);
    char *path = realloc(base, length + 5);
    if (!path) { free(base); qa_error_set(error, QA_ERROR_MEMORY, 0, "replacement skin extension allocation failed"); return false; }
    memcpy(path + length, ".lmp", 5); *out = path; return true;
}

static bool replacement_build(const qa_model *source, qa_scene_resources *resources,
    qa_material_library *materials, const qa_scene_image_options *options,
    qa_strings *strings, const qa_model_replacement *replacement, qa_scene_model **out, qa_error *error) {
    if (!replacement || !replacement->source || !replacement->mesh || !replacement->animation ||
        (replacement->source->format != QA_MODEL_MDL && replacement->source->format != QA_MODEL_MD2) ||
        (replacement->source != source && replacement->mesh != source) ||
        replacement->mesh->format != QA_MODEL_MD5 ||
        replacement->mesh->bone_count != replacement->animation->joint_count || !replacement->animation->frame_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "replacement does not belong to this retained model"); return false;
    }
    qa_scene_model *next = NULL;
    if (!qa_scene_model_create(replacement->mesh, resources, materials, options, strings, &next, error)) return false;
    next->replacement_description = *replacement;
    next->replacement_source = &next->replacement_description;
    next->replacement_skin_count = replacement->source->skin_count;
    void *allocation;
    if (!model_array(next->source->mesh_count, sizeof(*next->replacement_skins), &allocation, error)) goto fail;
    next->replacement_skins = allocation;
    for (uint32_t mesh = 0; mesh < next->source->mesh_count; ++mesh) {
        if (!model_array(next->replacement_skin_count, sizeof(*next->replacement_skins[mesh]), &allocation, error)) goto fail;
        next->replacement_skins[mesh] = allocation;
        const qa_model *skin_source = replacement->source;
        uint32_t groups = skin_source->format == QA_MODEL_MDL ? skin_source->skin_group_count : skin_source->skin_count;
        for (uint32_t skin = 0; skin < groups; ++skin) {
            uint32_t count = skin_source->format == QA_MODEL_MDL ? skin_source->skin_groups[skin].count : 1;
            uint32_t first = skin_source->format == QA_MODEL_MDL ? skin_source->skin_groups[skin].first : skin;
            for (uint32_t frame = 0; frame < count; ++frame) {
                char *path = NULL;
                if (!replacement_skin_path(replacement, mesh, skin, frame, &path, error)) goto fail;
                if (!path) continue;
                bool ok = scene_model_external(next, path, NULL, &next->replacement_skins[mesh][first + frame], error);
                free(path);
                if (!ok) goto fail;
            }
        }
    }
    *out = next; return true;
fail:
    qa_scene_model_destroy(next); return false;
}
static bool prepare_replacement(qa_scene_model *model, const qa_model_replacement *replacement,
                                 qa_error *error) {
    if (!replacement || (replacement->source != model->source && replacement->mesh != model->source)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Replacement differs from the actual retained source"); return false;
    }
    qa_scene_model **link = &model->replacement;
    while (*link) {
        qa_scene_model *retained = *link;
        const qa_model_replacement *description = retained->replacement_source;
        if (description->mesh == replacement->mesh && description->animation == replacement->animation &&
            description->flags == replacement->flags && description->elapsed_animation == replacement->elapsed_animation) {
            *link = retained->replacement_next;
            retained->replacement_next = model->replacement;
            model->replacement = retained;
            return true;
        }
        link = &retained->replacement_next;
    }
    qa_scene_model *next = NULL;
    if (!replacement_build(model->source, model->resources, model->materials, &model->options,
        model->strings, replacement, &next, error)) return false;
    next->replacement_next = model->replacement;
    next->replacement_parent = model;
    model->replacement = next;
    return true;
}
static bool content_leases_valid(const qa_scene_model_content_lease *mesh,
    const qa_scene_model_content_lease *source, const qa_scene_model_content_lease *animation)
{
    return mesh && mesh->context && mesh->release && source && source->context && source->release &&
        animation && animation->context && animation->release && mesh != source && mesh != animation && source != animation;
}
static bool model_image_sources_bind(qa_scene_model *model, const qa_resource *resource, qa_error *error)
{
    if (!resource) return true;
    for (scene_model_image *image = model->images; image; image = image->next)
        if (!scene_image_asset_source_bind((qa_scene_image *)image->base, resource, error) ||
            !scene_image_asset_source_bind((qa_scene_image *)image->fullbright, resource, error)) return false;
    return true;
}
bool qa_scene_model_source_resource_bind(qa_scene_model *model, const qa_resource *resource, qa_error *error)
{
    if (!model || !qa_scene_model_idle(model) || !resource) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Model source binding requires its actual idle owner and resource"); return false;
    }
    qa_bytes bytes = qa_resource_bytes(resource);
    if (bytes.size != model->source->source.size || (bytes.size &&
        memcmp(bytes.data, model->source->source.data, bytes.size))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Model source resource differs from its immutable source bytes"); return false;
    }
    return model_image_sources_bind(model, resource, error);
}
static void replacement_leases(qa_scene_model *model, qa_scene_model_content_lease *mesh,
    qa_scene_model_content_lease *source, qa_scene_model_content_lease *animation)
{
    model->source_lease = *mesh; *mesh = (qa_scene_model_content_lease){0};
    model->replacement_source_lease = *source; *source = (qa_scene_model_content_lease){0};
    model->animation_lease = *animation; *animation = (qa_scene_model_content_lease){0};
}
static bool replacement_matches(const qa_scene_model *child, const qa_model_replacement *description)
{
    const qa_model_replacement *actual = child ? child->replacement_source : NULL;
    return actual && description && actual->source == description->source &&
        actual->mesh == description->mesh && actual->animation == description->animation &&
        actual->flags == description->flags && actual->elapsed_animation == description->elapsed_animation;
}
static bool replacement_select(qa_scene_model *model, qa_scene_model *prepared,
    const qa_model_replacement *description, qa_scene_model **out, qa_error *error)
{
    *out = NULL;
    if (model->source->format != QA_MODEL_MDL && model->source->format != QA_MODEL_MD2) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Replacement policy requires its actual native alias source"); return false;
    }
    if (!description) return true;
    if (replacement_matches(prepared, description)) { *out = prepared; return true; }
    for (qa_scene_model *child = model->replacement; child; child = child->replacement_next)
        if (replacement_matches(child, description)) { *out = child; return true; }
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Replacement policy lacks its genuine prepared child"); return false;
}
bool qa_scene_model_replacement_policy_bind(qa_scene_model *model, bool enabled, double distance,
    const qa_model_replacement *description, qa_error *error)
{
    if (!model || !qa_scene_model_idle(model) || model->replacement_parent || model->replacement_next) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Replacement policy requires its actual idle root"); return false;
    }
    qa_scene_model *selected = NULL;
    if (!replacement_select(model, NULL, description, &selected, error)) return false;
    model->replacement_policy_set = true; model->replacement_policy_enabled = enabled;
    model->replacement_distance = distance; model->selected_replacement = selected; return true;
}
bool qa_scene_model_replacement_policy_read(const qa_scene_model *model, bool *configured,
    bool *enabled, double *distance, const qa_scene_model **selected)
{
    if (!model || !configured || !enabled || !distance || !selected || model->replacement_parent ||
        model->replacement_next || !qa_scene_model_observation_ready(model)) return false;
    *configured = model->replacement_policy_set; *enabled = model->replacement_policy_enabled;
    *distance = model->replacement_distance; *selected = model->selected_replacement; return true;
}
bool qa_scene_model_replacement_policy_update(qa_scene_model *model, bool enabled, double distance,
    qa_error *error)
{
    if (!model || !qa_scene_model_idle(model) || model->replacement_parent || model->replacement_next ||
        !model->replacement_policy_set || !model->selected_replacement ||
        (model->source->format != QA_MODEL_MDL && model->source->format != QA_MODEL_MD2)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Replacement use requires its actual idle admitted root"); return false;
    }
    model->replacement_policy_enabled = enabled; model->replacement_distance = distance; return true;
}
bool qa_scene_model_source_bind(qa_scene_model *model, qa_scene_model_content_lease *lease, qa_error *error)
{
    if (!model || !qa_scene_model_idle(model) || model->source_lease.context || model->source_lease.release ||
        !lease || !lease->context || !lease->release) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source binding requires its actual idle model and owning lease"); return false;
    }
    if (lease->resource && !qa_scene_model_source_resource_bind(model, lease->resource, error)) return false;
    model->source_lease = *lease; *lease = (qa_scene_model_content_lease){0}; return true;
}
bool qa_scene_model_replacement_prepare(qa_scene_model *model, const qa_model_replacement *description,
    qa_scene_model_content_lease *mesh, qa_scene_model_content_lease *source,
    qa_scene_model_content_lease *animation, qa_error *error)
{
    if (!model || !qa_scene_model_idle(model) || model->replacement_parent ||
        !content_leases_valid(mesh, source, animation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Replacement admission requires its actual idle root and content leases"); return false;
    }
    qa_scene_model *next = NULL;
    if (!replacement_build(model->source, model->resources, model->materials, &model->options,
        model->strings, description, &next, error)) return false;
    replacement_leases(next, mesh, source, animation);
    next->replacement_parent = model; next->replacement_next = model->replacement; model->replacement = next;
    return true;
}
bool qa_scene_model_replacement_prepare_parent(qa_scene_model *model, const qa_model_replacement *description,
    qa_scene_model_content_lease *mesh, qa_scene_model_content_lease *animation, qa_error *error)
{
    if (!model || !qa_scene_model_idle(model) || model->replacement_parent || model->replacement_next ||
        !description || description->source != model->source || !mesh || !animation || mesh == animation ||
        !mesh->context || !mesh->release || !animation->context || !animation->release) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Replacement admission requires its genuine registered parent and mesh/animation owners");
        return false;
    }
    qa_scene_model *next = NULL;
    if (!replacement_build(model->source, model->resources, model->materials, &model->options,
        model->strings, description, &next, error)) return false;
    next->source_lease = *mesh; *mesh = (qa_scene_model_content_lease){0};
    next->animation_lease = *animation; *animation = (qa_scene_model_content_lease){0};
    next->replacement_parent = model; next->replacement_next = model->replacement; model->replacement = next;
    return true;
}

typedef struct model_policy_image {
    const scene_model_image *current;
    scene_model_image *destination;
} model_policy_image;
typedef struct model_policy_node {
    qa_scene_model *owner;
    qa_scene_model images;
    model_policy_image *bindings;
    size_t count;
    scene_model_image ***shaders;
    bool prepared;
} model_policy_node;
struct qa_scene_model_image_policy {
    qa_scene_model *owner;
    qa_scene_resource_policy *resources;
    qa_scene_material_image_policy *materials;
    qa_scene_model_capture *capture;
    model_policy_node *nodes;
    size_t count;
    qa_scene_model *replacement;
    qa_scene_model *selected;
    bool selection_changed, selection_enabled;
    double distance;
    bool sealed, published;
};
static bool model_policy_current(const qa_scene_model_image_policy *ticket)
{
    if (!ticket || ticket->owner->image_policy != ticket || ticket->owner->capture != ticket->capture || ticket->owner->replacement_parent ||
        ticket->owner->replacement_next ||
        ticket->owner->resources != qa_scene_resource_policy_source(ticket->resources)) return false;
    for (size_t i = 0; i < ticket->count; ++i) {
        const model_policy_node *node = &ticket->nodes[i];
        if (node->owner->active_submissions || node->owner->checkpoint_active ||
            node->owner->source != node->images.source || node->owner->resources != ticket->owner->resources ||
            node->owner->materials != ticket->owner->materials) return false;
    }
    return true;
}
static void model_policy_skin_arrays(qa_scene_model *model)
{
    if (!model->replacement_skins) return;
    for (size_t i = 0; i < model->source->mesh_count; ++i) free(model->replacement_skins[i]);
    free(model->replacement_skins); model->replacement_skins = NULL;
}
static void model_policy_dispose(qa_scene_model_image_policy *ticket)
{
    qa_scene_model_destroy(ticket->replacement);
    for (size_t i = 0; i < ticket->count; ++i) {
        model_policy_node *node = &ticket->nodes[i];
        scene_model_images_destroy(&node->images);
        model_policy_skin_arrays(&node->images);
        if (node->shaders) {
            for (size_t j = 0; j < node->owner->source->mesh_count; ++j) free(node->shaders[j]);
            free(node->shaders);
        }
        free(node->bindings);
    }
    ticket->owner->image_policy = NULL;
    qa_scene_model_capture_end(ticket->capture);
    free(ticket->nodes); free(ticket);
}
static scene_model_image *model_policy_binding(const model_policy_node *node, const scene_model_image *image)
{
    if (!image) return NULL;
    for (size_t i = 0; i < node->count; ++i)
        if (node->bindings[i].current == image) return node->bindings[i].destination;
    return NULL;
}
static bool model_policy_node_prepare(model_policy_node *node, qa_scene_resources *resources, qa_error *error)
{
    qa_scene_model *owner = node->owner;
    node->images.source = owner->source;
    node->images.options = owner->options;
    memcpy(node->images.palette, owner->palette, sizeof(owner->palette));
    memcpy(node->images.translation, owner->translation, sizeof(owner->translation));
    node->images.resources = resources; node->images.materials = owner->materials;
    node->images.identity = owner->identity;
    if (owner->options.family == QA_GAME_Q3) return true;
    for (const scene_model_image *image = owner->images; image; image = image->next) {
        if (node->count == SIZE_MAX / sizeof(*node->bindings)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Model image binding inventory overflows"); return false;
        }
        ++node->count;
    }
    node->bindings = node->count ? calloc(node->count, sizeof(*node->bindings)) : NULL;
    if (node->count && !node->bindings) goto memory;
    size_t at = 0;
    for (const scene_model_image *image = owner->images; image; image = image->next, ++at) {
        scene_model_image *prepared = NULL; bool indexed = false;
        if (image->indexed_override) {
            qa_scene_model_indexed_skin skin = {image->name, image->indexed_width, image->indexed_height,
                {image->indexed_pixels.data, image->indexed_pixels.size}};
            indexed = true;
            if (!scene_model_indexed_override(&node->images, &skin, &prepared, error)) return false;
        } else if (owner->source->format == QA_MODEL_MDL) {
            for (uint32_t i = 0; !indexed && i < owner->source->skin_count; ++i) {
                if (owner->skins[i] != image) continue;
                indexed = true;
                if (!scene_model_indexed(&node->images, image->name, owner->source->skins[i].pixels,
                    owner->source->skin_width, owner->source->skin_height, false, &prepared, error)) return false;
            }
        } else if (owner->source->format == QA_MODEL_SPR) {
            for (uint32_t i = 0; !indexed && i < owner->source->sprite_count; ++i) {
                if (owner->sprites[i] != image) continue;
                const qa_model_sprite *sprite = &owner->source->sprites[i]; indexed = true;
                if (!scene_model_indexed(&node->images, image->name, sprite->pixels,
                    sprite->width, sprite->height, true, &prepared, error)) return false;
            }
        }
        if (!indexed && !scene_model_external(&node->images, image->name, NULL, &prepared, error)) return false;
        node->bindings[at] = (model_policy_image){image, prepared};
    }
    void *allocation;
    if (!model_array(owner->source->skin_count, sizeof(*owner->skins), &allocation, error)) return false;
    node->images.skins = allocation;
    if (!model_array(owner->source->sprite_count, sizeof(*owner->sprites), &allocation, error)) return false;
    node->images.sprites = allocation;
    node->shaders = owner->source->mesh_count ? calloc(owner->source->mesh_count, sizeof(*node->shaders)) : NULL;
    if (owner->source->mesh_count && !node->shaders) goto memory;
    for (uint32_t i = 0; i < owner->source->skin_count; ++i)
        node->images.skins[i] = model_policy_binding(node, owner->skins[i]);
    for (uint32_t i = 0; i < owner->source->sprite_count; ++i)
        node->images.sprites[i] = model_policy_binding(node, owner->sprites[i]);
    for (uint32_t i = 0; i < owner->source->mesh_count; ++i) {
        size_t count = owner->source->meshes[i].shader_count;
        if (!owner->meshes[i].shaders) continue;
        if (!model_array(count, sizeof(*node->shaders[i]), &allocation, error)) return false;
        node->shaders[i] = allocation;
        for (size_t j = 0; j < count; ++j)
            node->shaders[i][j] = model_policy_binding(node, owner->meshes[i].shaders[j]);
    }
    if (owner->replacement_skins) {
        if (!model_array(owner->source->mesh_count, sizeof(*owner->replacement_skins), &allocation, error)) return false;
        node->images.replacement_skins = allocation;
        for (size_t i = 0; i < owner->source->mesh_count; ++i) {
            if (!owner->replacement_skins[i]) continue;
            size_t count = owner->replacement_skin_count;
            if (!model_array(count, sizeof(*node->images.replacement_skins[i]), &allocation, error)) return false;
            node->images.replacement_skins[i] = allocation;
            for (size_t j = 0; j < count; ++j)
                node->images.replacement_skins[i][j] = model_policy_binding(node, owner->replacement_skins[i][j]);
        }
    }
    node->prepared = true; return true;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Preparing retained model image bindings"); return false;
}
bool qa_scene_model_image_policy_prepare(qa_scene_model *model, qa_scene_resource_policy *resources,
    qa_scene_model_image_policy **out, qa_error *error)
{
    qa_scene_resources *destination = qa_scene_resource_policy_destination(resources);
    if (!out || *out || !model || !destination || model->replacement_parent || model->replacement_next ||
        !qa_scene_model_idle(model) || model->resources != qa_scene_resource_policy_source(resources)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Model image preparation requires its actual retained resource bank"); return false;
    }
    qa_scene_model_image_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining model image preparation"); return false; }
    ticket->owner = model; ticket->resources = resources;
    if (!qa_scene_model_capture_begin(model, &ticket->capture, error)) { free(ticket); return false; }
    model->image_policy = ticket;
    qa_scene_model *node = model;
    for (;;) {
        if (ticket->count == SIZE_MAX / sizeof(*ticket->nodes)) {
            model_policy_dispose(ticket);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Model replacement image inventory overflows"); return false;
        }
        model_policy_node *nodes = realloc(ticket->nodes, (ticket->count + 1) * sizeof(*nodes));
        if (!nodes) {
            model_policy_dispose(ticket);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining actual model replacement image owners"); return false;
        }
        ticket->nodes = nodes; nodes[ticket->count++] = (model_policy_node){.owner = node};
        if (!model_policy_node_prepare(&nodes[ticket->count - 1], destination, error)) {
            model_policy_dispose(ticket); return false;
        }
        if (node->replacement) { node = node->replacement; continue; }
        while (node != model && !node->replacement_next) node = node->replacement_parent;
        if (node == model) break;
        node = node->replacement_next;
    }
    *out = ticket; return true;
}
bool qa_scene_model_image_policy_ready(qa_scene_model_image_policy *ticket, qa_error *error)
{
    if (!model_policy_current(ticket) || ticket->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Prepared model images lost their actual source owners"); return false;
    }
    ticket->sealed = true; return true;
}
bool qa_scene_model_image_policy_materials(qa_scene_model_image_policy *ticket,
    qa_scene_material_image_policy *materials, qa_error *error)
{
    if (!model_policy_current(ticket) || ticket->sealed || ticket->published || ticket->replacement ||
        qa_scene_material_image_policy_source(materials) != ticket->owner->materials ||
        !qa_scene_material_image_policy_destination(materials)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Model material preparation requires its genuine destination library"); return false;
    }
    ticket->materials = materials; return true;
}
bool qa_scene_model_image_policy_replacement(qa_scene_model_image_policy *ticket,
    const qa_model_replacement *description, qa_scene_model_content_lease *mesh,
    qa_scene_model_content_lease *source, qa_scene_model_content_lease *animation, qa_error *error)
{
    if (!model_policy_current(ticket) || ticket->sealed || ticket->published || ticket->replacement ||
        !content_leases_valid(mesh, source, animation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Replacement preparation requires its actual open model/image ticket"); return false;
    }
    qa_scene_model *next = NULL;
    if (!replacement_build(ticket->owner->source, qa_scene_resource_policy_destination(ticket->resources),
        ticket->materials ? qa_scene_material_image_policy_destination(ticket->materials) : ticket->owner->materials,
        &ticket->owner->options, ticket->owner->strings, description, &next, error)) return false;
    replacement_leases(next, mesh, source, animation); ticket->replacement = next; return true;
}
bool qa_scene_model_image_policy_replacement_parent(qa_scene_model_image_policy *ticket,
    const qa_model_replacement *description, qa_scene_model_content_lease *mesh,
    qa_scene_model_content_lease *animation, qa_error *error)
{
    if (!model_policy_current(ticket) || ticket->sealed || ticket->published || ticket->replacement ||
        !description || description->source != ticket->owner->source || !mesh || !animation || mesh == animation ||
        !mesh->context || !mesh->release || !animation->context || !animation->release) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Replacement requires its genuine captured parent and owning mesh/animation"); return false;
    }
    qa_scene_model *next = NULL;
    if (!replacement_build(ticket->owner->source, qa_scene_resource_policy_destination(ticket->resources),
        ticket->materials ? qa_scene_material_image_policy_destination(ticket->materials) : ticket->owner->materials,
        &ticket->owner->options, ticket->owner->strings, description, &next, error)) return false;
    next->source_lease = *mesh; *mesh = (qa_scene_model_content_lease){0};
    next->animation_lease = *animation; *animation = (qa_scene_model_content_lease){0};
    ticket->replacement = next; return true;
}
bool qa_scene_model_image_policy_ready_is(const qa_scene_model_image_policy *ticket)
{ return model_policy_current(ticket) && ticket->sealed && !ticket->published &&
    qa_scene_resource_policy_ready_is(ticket->resources); }
bool qa_scene_model_image_policy_select(qa_scene_model_image_policy *ticket, bool enabled, double distance,
    const qa_model_replacement *description, qa_error *error)
{
    if (!model_policy_current(ticket) || ticket->sealed || ticket->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Replacement selection requires its actual open model preparation"); return false;
    }
    qa_scene_model *selected = NULL;
    if (!replacement_select(ticket->owner, ticket->replacement, description, &selected, error)) return false;
    ticket->selected = selected; ticket->selection_enabled = enabled;
    ticket->distance = distance; ticket->selection_changed = true; return true;
}
void qa_scene_model_image_policy_publish(qa_scene_model_image_policy *ticket)
{
    if (!model_policy_current(ticket) || !ticket->sealed || ticket->published) return;
    for (size_t i = 0; i < ticket->count; ++i) {
        model_policy_node *node = &ticket->nodes[i]; qa_scene_model *owner = node->owner;
        if (!node->prepared) continue;
        scene_model_image *images = owner->images; owner->images = node->images.images; node->images.images = images;
        scene_model_image **skins = owner->skins; owner->skins = node->images.skins; node->images.skins = skins;
        scene_model_image **sprites = owner->sprites; owner->sprites = node->images.sprites; node->images.sprites = sprites;
        for (size_t j = 0; j < owner->source->mesh_count; ++j) {
            scene_model_image **shaders = owner->meshes[j].shaders;
            owner->meshes[j].shaders = node->shaders[j]; node->shaders[j] = shaders;
        }
        scene_model_image ***replacement = owner->replacement_skins;
        owner->replacement_skins = node->images.replacement_skins; node->images.replacement_skins = replacement;
    }
    if (ticket->replacement) {
        qa_scene_model *next = ticket->replacement;
        next->resources = ticket->owner->resources;
        next->materials = ticket->owner->materials;
        next->replacement_parent = ticket->owner;
        next->replacement_next = ticket->owner->replacement;
        ticket->owner->replacement = next; ticket->replacement = NULL;
    }
    if (ticket->selection_changed) {
        ticket->owner->replacement_policy_set = true;
        ticket->owner->replacement_policy_enabled = ticket->selection_enabled;
        ticket->owner->replacement_distance = ticket->distance;
        ticket->owner->selected_replacement = ticket->selected;
    }
    ticket->published = true;
}
bool qa_scene_model_image_policy_finish(qa_scene_model_image_policy **owner, qa_error *error)
{
    if (!owner || !model_policy_current(*owner) || !(*owner)->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Model image retirement requires its published preparation"); return false;
    }
    model_policy_dispose(*owner); *owner = NULL; return true;
}
bool qa_scene_model_image_policy_abort(qa_scene_model_image_policy **owner, qa_error *error)
{
    if (!owner || !model_policy_current(*owner) || (*owner)->published) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Model image abort requires its unpublished preparation"); return false;
    }
    model_policy_dispose(*owner); *owner = NULL; return true;
}

static void repair_frames(const qa_scene_model *model, qa_scene_model_input *input) {
    const qa_model *source = input->replacement ? input->replacement->source : model->source;
    uint32_t count = source->frame_group_count ? source->frame_group_count : source->frame_count;
    if (source->format == QA_MODEL_MD5) count = input->animation ? input->animation->frame_count : 1;
    if (!count) count = 1;
    if (source->format == QA_MODEL_SP2 || (source->format == QA_MODEL_MD5 && !input->replacement) ||
        (input->family == QA_GAME_Q3 && (input->flags & 512))) {
        input->frame %= count; input->old_frame %= count;
    }
    bool bad = input->frame >= count, old_bad = input->old_frame >= count;
    if ((input->family == QA_GAME_Q3 || source->format == QA_MODEL_MD2) && (bad || old_bad))
        input->frame = input->old_frame = 0;
    else { if (bad) input->frame = 0; if (old_bad) input->old_frame = 0; }
    if (input->frame == input->old_frame && source->format != QA_MODEL_MD2) input->back_lerp = 0;
    if (input->replacement && input->replacement->elapsed_animation) {
        input->frame = qa_model_replacement_frame(input->replacement, input->frame, input->seconds, input->sync_base);
        input->old_frame = input->frame; input->back_lerp = 0;
    } else if (input->replacement) {
        input->frame = qa_model_replacement_frame(input->replacement, input->frame, input->seconds, input->sync_base);
        input->old_frame = qa_model_replacement_frame(input->replacement, input->old_frame, input->seconds, input->sync_base);
    } else if (source->frame_group_count) {
        input->frame = qa_model_group_sample(&source->frame_groups[input->frame], input->seconds, input->sync_base);
        input->old_frame = qa_model_group_sample(&source->frame_groups[input->old_frame], input->seconds, input->sync_base);
    }
}

static bool select_image(qa_scene_model *model, const qa_scene_model_input *input, uint32_t index,
                          scene_model_image *external, qa_scene_frame *frame, scene_model_image **out, qa_error *error) {
    const qa_model_mesh *mesh = &model->source->meshes[index];
    *out = NULL;
    if (input->indexed_skin && model->source->format == QA_MODEL_MDL)
        return scene_model_indexed_override(model, input->indexed_skin, out, error);
    bool custom_allowed = model->source->format != QA_MODEL_MDL;
    bool shell_image = scene_model_has_shell(input) &&
        (model->source->format == QA_MODEL_MD2 || model->source->format == QA_MODEL_MD5);
    if ((input->custom_material && custom_allowed) || shell_image) return true;
    if (input->custom_skin && custom_allowed) {
        qa_string_id name = model->meshes[index].surface;
        for (size_t i = 0; i < input->custom_skin->count; ++i)
            if (input->custom_skin->mappings[i].surface == name) {
                if (input->custom_skin_materials) {
                    external->material = input->custom_skin_materials[i];
                    *out = external; return true;
                }
                if (input->material_library) {
                    if (!scene_model_external_material(model, input->material_library,
                        input->custom_skin->mappings[i].shader, frame, &external->material, error)) return false;
                    *out = external; return true;
                }
                return scene_model_external(model, input->custom_skin->mappings[i].shader, frame, out, error);
            }
        return true;
    }
    const qa_model *skin_source = input->replacement ? input->replacement->source : model->source;
    if (skin_source->format == QA_MODEL_MDL) {
        uint32_t skin = input->skin < skin_source->skin_group_count ? input->skin : 0;
        if (!skin_source->skin_group_count) return true;
        skin = qa_model_group_sample(&skin_source->skin_groups[skin], input->seconds, input->sync_base);
        *out = input->replacement ? model->replacement_skins[index][skin] : model->skins[skin];
    } else if (skin_source->format == QA_MODEL_MD2) {
        if (!skin_source->skin_count) return true;
        uint32_t skin = input->skin < skin_source->skin_count ? input->skin : 0;
        *out = input->replacement ? model->replacement_skins[index][skin] : model->skins[skin];
    } else if (mesh->shader_count) {
        uint32_t skin = model->source->format == QA_MODEL_MD3 ? input->skin % mesh->shader_count :
            input->skin < mesh->shader_count ? input->skin : 0;
        if (input->material_library) {
            scene_model_image *registered = model->meshes[index].shaders[skin];
            if (!registered) return true;
            if (!qa_material_register(input->material_library, registered->name, &model->options,
                false, &external->material, error)) return false;
            if (qa_material_library_has_source_profile(input->material_library) && external->material->default_shader &&
                (model->source->format == QA_MODEL_MD3 || model->source->format == QA_MODEL_MD4))
                external->material = qa_material_find(input->material_library, "*default");
            *out = external; return true;
        }
        *out = model->meshes[index].shaders[skin];
    }
    return true;
}

static bool casts_shadow(const qa_scene_model *model, const qa_scene_model_input *input) {
    if (input->view_model || model->source->format == QA_MODEL_SPR || model->source->format == QA_MODEL_SP2) return false;
    if (input->family == QA_GAME_Q2) return !(input->flags & (4u | 16u | 32u | 128u | 8192u | 0x00200000u));
    if (input->family == QA_GAME_Q3 && (input->flags & (4u | 8u | 64u))) return false;
    return input->color.w >= 1;
}

static qa_model_bounds cull_bounds(const qa_scene_model *model, const qa_scene_model_input *original,
                                    const qa_scene_mesh *mesh) {
    const qa_model *source = original->replacement ? original->replacement->source : model->source;
    if (source->format == QA_MODEL_MDL) return source->bounds;
    if (source->format == QA_MODEL_MD2 && source->frame_count) {
        uint32_t current = original->frame, previous = original->old_frame;
        if (current >= source->frame_count || previous >= source->frame_count) current = previous = 0;
        qa_bounds bounds = model_bounds_empty();
        const uint32_t frames[2] = {current, previous};
        for (unsigned i = 0; i < 2; ++i) {
            const qa_model_frame *frame = &source->frames[frames[i]];
            qa_vec3 translation = model_vec(frame->translation);
            model_bounds_add(&bounds, translation);
            model_bounds_add(&bounds, qa_vec_add(translation, qa_vec_scale(model_vec(frame->scale), 255)));
        }
        return (qa_model_bounds){{bounds.mins.x, bounds.mins.y, bounds.mins.z}, {bounds.maxs.x, bounds.maxs.y, bounds.maxs.z}};
    }
    return (qa_model_bounds){{mesh->bounds.mins.x, mesh->bounds.mins.y, mesh->bounds.mins.z},
                            {mesh->bounds.maxs.x, mesh->bounds.maxs.y, mesh->bounds.maxs.z}};
}

static int source_md3_sphere(const qa_scene_model_input *input, const qa_model_frame *frame,
    const qa_scene_plane planes[6])
{
    float point[3]; qa_model_transform_point(&input->transform, frame->origin, point);
    bool inside = true;
    for (unsigned i = 0; i < 4; ++i) {
        float distance = qa_vec_dot(model_vec(point), planes[i].normal) - planes[i].distance;
        if (distance < -frame->radius) return -1;
        if (distance <= frame->radius) inside = false;
    }
    return inside ? 1 : 0;
}
static bool source_md3_visible(const qa_scene_model *model, const qa_scene_model_input *input)
{
    if (!model->source->frame_count || input->no_cull) return true;
    const qa_model_frame *current = &model->source->frames[input->frame];
    const qa_model_frame *previous = &model->source->frames[input->old_frame];
    qa_scene_plane planes[6]; (void)qa_scene_frustum(&input->view, planes);
    if (!input->non_normalized_axis) {
        int a = source_md3_sphere(input, current, planes), b = current == previous ? a :
            source_md3_sphere(input, previous, planes);
        if (input->source_scratch) ++input->source_scratch->owner->counters.md3_sphere[a==b && a? a>0?0:2:1];
        if (a == b && a) return a > 0;
    }
    qa_model_bounds merged;
    for (unsigned axis = 0; axis < 3; ++axis) {
        merged.min[axis] = fminf(current->bounds.min[axis], previous->bounds.min[axis]);
        merged.max[axis] = fmaxf(current->bounds.max[axis], previous->bounds.max[axis]);
    }
    bool front[4] = {false},back[4]={false};
    for (unsigned corner = 0; corner < 8; ++corner) {
        float local[3], point[3];
        for (unsigned axis = 0; axis < 3; ++axis)
            local[axis] = corner & (1u << axis) ? merged.max[axis] : merged.min[axis];
        qa_model_transform_point(&input->transform, local, point);
        for (unsigned plane = 0; plane < 4; ++plane)
            if (qa_vec_dot(model_vec(point), planes[plane].normal) > planes[plane].distance) front[plane] = true;
            else back[plane]=true;
    }
    bool visible=front[0] && front[1] && front[2] && front[3];
    bool inside=!(back[0] || back[1] || back[2] || back[3]);
    if (input->source_scratch) ++input->source_scratch->owner->counters.md3_box[!visible?2:inside?0:1];
    return visible;
}
bool qa_scene_model_source_admission(const qa_scene_model *model, const qa_scene_model_input *original,
    bool *visible, qa_error *error)
{
    if (!model || !model->source || !original || !visible || !original->source_order ||
        (model->source->format != QA_MODEL_MD3 && model->source->format != QA_MODEL_MD4) ||
        !isfinite(original->back_lerp)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source admission requires the actual selected model and input");
        return false;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!isfinite(original->transform.origin[i]) || !isfinite(original->transform.scale[i]) ||
            !qa_vec_finite(model_vec(original->transform.axes[i]))) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "Source admission transform is nonfinite");
            return false;
        }
    }
    qa_scene_model_input input = *original;
    input.replacement = NULL;
    repair_frames(model, &input);
    *visible = model->source->format == QA_MODEL_MD4 || source_md3_visible(model, &input);
    return true;
}

static bool alias_diffuse(const qa_scene_model_input *input, const float normal[3],
    qa_vec3 *out, qa_error *error)
{
    float incoming = qa_vec_dot(model_vec(normal), input->light_direction);
    const float ambient[3] = {input->ambient.x * 255, input->ambient.y * 255, input->ambient.z * 255};
    const float directed[3] = {input->directed.x * 255, input->directed.y * 255, input->directed.z * 255};
    float color[3];
    for (unsigned i = 0; i < 3; ++i) {
        float value = incoming <= 0 ? ambient[i] : fminf(255, ambient[i] + incoming * directed[i]);
        if (!isfinite(value) || (double)value < -2147483648.0 || (double)value >= 2147483648.0) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Alias diffuse color exceeds source integer range");
            return false;
        }
        color[i] = (float)((uint32_t)(int32_t)value & 255u) / 255;
    }
    *out = qa_v3(color[0], color[1], color[2]);
    return true;
}

static bool md5_influence_bounds(const qa_scene_model *model, uint32_t index,
    const qa_scene_model_input *input, bool shell, qa_bounds *bounds)
{
    const scene_model_mesh *mesh = &model->meshes[index];
    if (!mesh->influence_bounds_ready || !input->pose || input->pose_count < model->source->bone_count) return false;
    double lower[3] = {INFINITY, INFINITY, INFINITY}, upper[3] = {-INFINITY, -INFINITY, -INFINITY};
    double magnitude[3] = {0, 0, 0};
    bool used = false;
    for (uint32_t joint = 0; joint < model->source->bone_count; ++joint) {
        const qa_model_pose *pose = &input->pose[joint];
        if (!qa_vec_finite(model_vec(pose->position)) || !isfinite(pose->scale)) return false;
        for (unsigned k = 0; k < 4; ++k) if (!isfinite(pose->orientation[k])) return false;
        const scene_model_influence *influence = &mesh->influences[joint];
        if (!influence->used) continue;
        used = true;
        const float *q = pose->orientation;
        for (unsigned k = 0; k < 4; ++k) if ((double)q[k] * q[k] > FLT_MAX / 8.0) return false;
        double rotation[3][3] = {
            {2 * (q[3] * q[3] + q[0] * q[0]) - 1, 2.0 * (q[0] * q[1] - q[3] * q[2]), 2.0 * (q[0] * q[2] + q[3] * q[1])},
            {2.0 * (q[0] * q[1] + q[3] * q[2]), 2 * (q[3] * q[3] + q[1] * q[1]) - 1, 2.0 * (q[1] * q[2] - q[3] * q[0])},
            {2.0 * (q[0] * q[2] - q[3] * q[1]), 2.0 * (q[1] * q[2] + q[3] * q[0]), 2 * (q[3] * q[3] + q[2] * q[2]) - 1}
        };
        float offset_min[3], offset_max[3], normal_min[3], normal_max[3];
        model_store(offset_min, influence->offsets.mins); model_store(offset_max, influence->offsets.maxs);
        model_store(normal_min, influence->normals.mins); model_store(normal_max, influence->normals.maxs);
        for (unsigned axis = 0; axis < 3; ++axis) {
            double low = 0, high = 0, normal_low = 0, normal_high = 0, absolute = 0, normal_absolute = 0;
            for (unsigned k = 0; k < 3; ++k) {
                if (fabs(offset_min[k]) > FLT_MAX / 2.0 || fabs(offset_max[k]) > FLT_MAX / 2.0 ||
                    fabs(normal_min[k]) > FLT_MAX / 2.0 || fabs(normal_max[k]) > FLT_MAX / 2.0) return false;
                double a = rotation[axis][k] * offset_min[k], b = rotation[axis][k] * offset_max[k];
                low += fmin(a, b); high += fmax(a, b); absolute += fmax(fabs(a), fabs(b));
                if (shell) {
                    a = rotation[axis][k] * normal_min[k]; b = rotation[axis][k] * normal_max[k];
                    normal_low += fmin(a, b); normal_high += fmax(a, b); normal_absolute += fmax(fabs(a), fabs(b));
                }
            }
            double scaled_low = pose->scale * low, scaled_high = pose->scale * high;
            double point_low = pose->position[axis] + fmin(scaled_low, scaled_high) + 4 * normal_low;
            double point_high = pose->position[axis] + fmax(scaled_low, scaled_high) + 4 * normal_high;
            double point_absolute = fabs(pose->position[axis]) + fabs(pose->scale) * absolute + 4 * normal_absolute;
            if (absolute > FLT_MAX / 8.0 || normal_absolute > FLT_MAX / 8.0 || point_absolute > FLT_MAX / 8.0) return false;
            lower[axis] = fmin(lower[axis], point_low); upper[axis] = fmax(upper[axis], point_high);
            magnitude[axis] = fmax(magnitude[axis], point_absolute);
        }
    }
    double operations = ((double)mesh->max_weights + 32) * FLT_EPSILON;
    if (operations >= 0.5) return false;
    double gamma = operations / (1 - operations);
    float mins[3], maxs[3];
    /* Nonnegative weights form a convex combination scaled by their actual
     * sum. The interval also covers ordered float accumulation and shells. */
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!used) lower[axis] = upper[axis] = 0;
        double absolute = magnitude[axis] * mesh->max_bias_sum;
        if (!isfinite(absolute) || absolute > FLT_MAX / 8.0) return false;
        double error = gamma * absolute + ((double)mesh->max_weights + 32) * 64 * FLT_TRUE_MIN * (mesh->max_bias_sum + 1);
        double lo = lower[axis] * (lower[axis] < 0 ? mesh->max_bias_sum : mesh->min_bias_sum) - error;
        double hi = upper[axis] * (upper[axis] > 0 ? mesh->max_bias_sum : mesh->min_bias_sum) + error;
        mins[axis] = nextafterf((float)lo, -INFINITY); maxs[axis] = nextafterf((float)hi, INFINITY);
        if (!isfinite(mins[axis]) || !isfinite(maxs[axis])) return false;
    }
    *bounds = (qa_bounds){model_vec(mins), model_vec(maxs)};
    return true;
}

static bool md5_bounds_visible(const qa_scene_model_input *input, qa_bounds bounds)
{
    qa_model_bounds local = {{bounds.mins.x, bounds.mins.y, bounds.mins.z},
                            {bounds.maxs.x, bounds.maxs.y, bounds.maxs.z}}, world;
    qa_model_transform_bounds(&input->transform, &local, &world);
    qa_bounds transformed = {model_vec(world.min), model_vec(world.max)};
    if (!qa_vec_finite(transformed.mins) || !qa_vec_finite(transformed.maxs)) return true;
    qa_scene_plane planes[6];
    size_t count = qa_scene_frustum(&input->view, planes);
    return qa_scene_bounds_visible(transformed, planes, count);
}

static unsigned md5_sample_index(const qa_scene_model *model, const qa_scene_model_input *input,
    uint32_t mesh, bool *reusable)
{
    *reusable = false;
    if (!input->pose || input->pose_count != model->source->bone_count) return SCENE_MODEL_TRANSIENT_SAMPLE;
    unsigned index = SCENE_MODEL_TRANSIENT_SAMPLE, references = 0;
    if (input->pose == model->source->bind_pose) {
        index = SCENE_MODEL_BIND_SAMPLE; references = model->bind_frame_references;
    } else for (unsigned i = 0; i < SCENE_MODEL_POSE_VARIANTS; ++i)
        if (model->poses[i].ready && input->pose == model->poses[i].pose) {
            index = i; references = model->poses[i].frame_references; break;
        }
    if (index == SCENE_MODEL_TRANSIENT_SAMPLE) return index;
    const scene_model_sample *sample = &model->meshes[mesh].samples[index];
    if (references && sample->pose && sample->skin.rounding != fegetround()) return SCENE_MODEL_TRANSIENT_SAMPLE;
    *reusable = true;
    return index;
}

static void md5_sample_prepare(scene_model_sample *sample, const qa_scene_model_input *input, bool reusable)
{
    int rounding = fegetround();
    if (!reusable || sample->pose != input->pose || sample->skin.rounding != rounding) {
        sample->pose = reusable ? input->pose : NULL;
        sample->skin.rounding = rounding;
        sample->skin.error = (qa_error){0};
        sample->skin.ready = sample->bounds_ready = sample->shell_ready = false;
    }
}

static bool md5_deferred_allowed(const qa_scene_model *model, const qa_scene_model_input *input,
    const qa_scene_model_input *original, unsigned depth)
{
    if (model->source->format != QA_MODEL_MD5 || depth || model->active_submissions > 1 || original->pose ||
        (input->family != QA_GAME_Q1 && input->family != QA_GAME_Q2) ||
        !model->source_lease.context || !model->source_lease.release ||
        (input->animation && (!model->animation_lease.context || !model->animation_lease.release ||
            !model->replacement_source || input->animation != model->replacement_source->animation)) ||
        input->alias_lighting == QA_ALIAS_Q3_DIFFUSE || input->custom_material || input->planar_shadow ||
        input->shadow_only || input->shadow_light_count || input->source_order || input->source_scratch ||
        input->source_model_owner || input->source_model_retain || input->source_model_release ||
        input->source_recipient_image) return false;
    return true;
}

static bool md5_deferred_mesh(qa_scene_model *model, const qa_scene_model_input *input, uint32_t index,
    bool cull, qa_scene_mesh *mesh, unsigned *sample_index, bool *visible)
{
    bool reusable;
    *sample_index = md5_sample_index(model, input, index, &reusable);
    if (!reusable) return false;
    const scene_model_mesh *retained = &model->meshes[index];
    if (!retained->retained.geometry || !retained->retained.vertex_count || !retained->retained.index_count) return false;
    bool shell = scene_model_has_shell(input);
    const scene_model_sample *sample = &retained->samples[*sample_index];
    bool same = sample->pose == input->pose && sample->skin.rounding == fegetround();
    qa_bounds bounds;
    if (same && sample->skin.ready && (shell ? sample->shell_ready : sample->bounds_ready))
        bounds = shell ? sample->shell_bounds : sample->bounds;
    else if (!md5_influence_bounds(model, index, input, shell, &bounds)) return false;
    *mesh = retained->retained;
    mesh->bounds = bounds;
    *visible = !cull || md5_bounds_visible(input, bounds);
    return true;
}

static bool md5_deferred_skin(qa_scene_model *model, const qa_scene_model_input *input, uint32_t index,
    unsigned sample_index, qa_scene_frame *frame,
    const qa_scene_skinning **out, qa_error *error)
{
    const qa_scene_skin_pose *pose;
    if (!qa_scene_frame_model(frame, model, input->pose, input->pose_count, &pose, error)) return false;
    scene_model_sample *sample = &model->meshes[index].samples[sample_index];
    md5_sample_prepare(sample, input, true);
    qa_scene_skinning *skin = qa_arena_alloc(&frame->storage, sizeof(*skin), _Alignof(qa_scene_skinning), error);
    if (!skin) return false;
    bool shell = scene_model_has_shell(input);
    qa_vec3 light = input->alias_lighting == QA_ALIAS_PREPARED_LIGHT ? input->alias_light : scene_model_alias_light(input);
    scene_model_shading shading = {0};
    if (!shell) shading = scene_model_shade_prepare(input);
    *skin = (qa_scene_skinning){.pose = pose, .sample = &sample->skin,
        .shade_direction = shading.direction, .light = light, .tint = input->color,
        .shell = shell ? 4 : 0, .shade = !shell};
    *out = skin;
    return true;
}

static bool mesh_geometry(qa_scene_model *model, const qa_scene_model_input *input, uint32_t index,
                           bool cull, qa_scene_frame *frame, qa_scene_mesh *out, bool *visible,
                           qa_error *error) {
    const qa_model_mesh *source = &model->source->meshes[index];
    scene_model_mesh *retained = &model->meshes[index];
    *out = retained->retained;
    *visible = true;
    if (model->source_topology && out->vertex_count != source->vertex_count) {
        qa_error_set(error, QA_ERROR_FORMAT, index, "Source model lost its physical vertex extent"); return false;
    }
    if (!out->vertex_count) return true;
    size_t source_vertex_count = source->vertex_count;
    if (source_vertex_count > SIZE_MAX / sizeof(qa_model_vertex) || out->vertex_count > SIZE_MAX / sizeof(qa_scene_vertex)) {
        qa_error_set(error, QA_ERROR_MEMORY, index, "model frame geometry exceeds addressable storage"); return false;
    }
    unsigned sample_index = 0;
    bool reusable = false;
    if (model->source->format == QA_MODEL_MD5) sample_index = md5_sample_index(model, input, index, &reusable);
    scene_model_sample *sample = &retained->samples[sample_index];
    qa_model_vertex *sampled = sample->skin.vertices;
    if (!sampled) return false;
    bool shell = scene_model_has_shell(input);
    bool alias = model->source->format == QA_MODEL_MDL || model->source->format == QA_MODEL_MD2;
    if (alias) {
        qa_vec3 delta = qa_v3(0, 0, 0);
        if (model->source->format == QA_MODEL_MD2 && input->back_lerp != 0) {
            qa_model_transform inverse;
            if (!qa_model_transform_inverse(&input->transform, &inverse)) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "MD2 origin compensation requires a nonsingular transform"); return false;
            }
            float world_delta[3], local_delta[3];
            model_store(world_delta, qa_vec_sub(input->previous_origin, model_origin(input)));
            qa_model_transform_direction(&inverse, world_delta, local_delta); delta = model_vec(local_delta);
        }
        if (!qa_model_sample_alias(model->source, input->frame, input->old_frame, input->back_lerp,
                                    delta, sampled, source->vertex_count, error)) return false;
    } else if (model->source->format == QA_MODEL_MD5) {
        md5_sample_prepare(sample, input, reusable);
        if (!sample->skin.ready) {
            qa_bounds bounds;
            if (cull && md5_influence_bounds(model, index, input, shell, &bounds) && !md5_bounds_visible(input, bounds)) {
                *visible = false; return true;
            }
            if (!qa_model_skin_md5(model->source, index, input->pose, input->pose_count,
                                    sampled, source->vertex_count, error)) return false;
            sample->skin.error = (qa_error){0};
            sample->skin.ready = true;
        }
        if (sample->skin.error.code != QA_OK) {
            if (error) *error = sample->skin.error;
            return false;
        }
        if (!sample->bounds_ready) {
            sample->bounds = model_bounds_empty();
            for (size_t i = 0; i < out->vertex_count; ++i) {
                uint32_t source_index = retained->sources[i];
                if (source_index >= source->vertex_count) {
                    qa_error_set(error, QA_ERROR_FORMAT, index, "Source model lost its physical vertex order"); return false;
                }
                model_bounds_add(&sample->bounds, model_vec(sampled[source_index].position));
            }
            sample->bounds_ready = true;
        }
    } else if (!qa_model_sample_mesh(model->source, index, input->frame, input->old_frame,
                                      input->back_lerp, sampled, source->vertex_count, error)) return false;
    if (shell && model->source->format == QA_MODEL_MD5 && !sample->shell_ready) {
        sample->shell_bounds = model_bounds_empty();
        for (size_t i = 0; i < out->vertex_count; ++i) {
            const qa_model_vertex *point = &sampled[retained->sources[i]];
            model_bounds_add(&sample->shell_bounds,
                qa_vec_add(model_vec(point->position), qa_vec_scale(model_vec(point->normal), 4)));
        }
        sample->shell_ready = true;
    }
    if (cull && model->source->format == QA_MODEL_MD5) {
        qa_bounds bounds = shell ? sample->shell_bounds : sample->bounds;
        qa_model_bounds local = {{bounds.mins.x, bounds.mins.y, bounds.mins.z},
                                {bounds.maxs.x, bounds.maxs.y, bounds.maxs.z}}, world;
        qa_model_transform_bounds(&input->transform, &local, &world);
        qa_scene_plane planes[6];
        size_t count = qa_scene_frustum(&input->view, planes);
        if (!qa_scene_bounds_visible((qa_bounds){model_vec(world.min), model_vec(world.max)}, planes, count)) {
            *visible = false; return true;
        }
    }
    qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage, out->vertex_count * sizeof(*vertices),
        _Alignof(qa_scene_vertex), error);
    if (!vertices) return false;
    qa_scene_model_input lighting = *input;
    if (input->family == QA_GAME_Q3 && model->options.family != QA_GAME_Q3) {
        lighting.family = model->options.family;
        lighting.flags = 0;
    }
    qa_vec3 light = input->alias_lighting == QA_ALIAS_PREPARED_LIGHT ?
        input->alias_light : scene_model_alias_light(&lighting);
    scene_model_shading shading = {0};
    if (!input->shadow_only && input->alias_lighting != QA_ALIAS_Q3_DIFFUSE &&
        lighting.family != QA_GAME_Q3 && !shell)
        shading = scene_model_shade_prepare(&lighting);
    out->bounds = model->source->format == QA_MODEL_MD5 ?
        shell ? sample->shell_bounds : sample->bounds : model_bounds_empty();
    for (size_t i = 0; i < out->vertex_count; ++i) {
        uint32_t source_index = retained->sources[i];
        if (source_index >= source->vertex_count || (model->source_topology && source_index != i)) {
            qa_error_set(error, QA_ERROR_FORMAT, index, "Source model lost its physical vertex order"); return false;
        }
        const qa_model_vertex *point = &sampled[source_index];
        vertices[i] = retained->vertices[i];
        vertices[i].position = model_vec(point->position); vertices[i].normal = model_vec(point->normal);
        if (shell && (model->source->format == QA_MODEL_MD2 || model->source->format == QA_MODEL_MD5))
            vertices[i].position = qa_vec_add(vertices[i].position, qa_vec_scale(vertices[i].normal, 4));
        vertices[i].color = input->color;
        if (!input->shadow_only && input->alias_lighting == QA_ALIAS_Q3_DIFFUSE) {
            qa_vec3 color;
            if (!alias_diffuse(input, point->normal, &color, error)) return false;
            if (model->source->format == QA_MODEL_MDL && input->back_lerp != 0) {
                qa_vec3 old;
                size_t old_index = (size_t)input->old_frame * source->vertex_count + source_index;
                if (!alias_diffuse(input, source->vertices[old_index].normal, &old, error)) return false;
                color = qa_vec_add(qa_vec_scale(color, 1 - input->back_lerp), qa_vec_scale(old, input->back_lerp));
            }
            vertices[i].color.x *= color.x;
            vertices[i].color.y *= color.y;
            vertices[i].color.z *= color.z;
        } else if (!input->shadow_only && lighting.family != QA_GAME_Q3) {
            uint8_t normal = retained->normal_indices ? retained->normal_indices[(size_t)input->frame * source->vertex_count + source_index] : 255;
            float shade = shell ? 1 : scene_model_shade(&shading, point->normal, normal);
            if (!shell && lighting.family == QA_GAME_Q1 && model->source->format == QA_MODEL_MDL && input->back_lerp != 0) {
                size_t old_index = (size_t)input->old_frame * source->vertex_count + source_index;
                shade = shade * (1 - input->back_lerp) + scene_model_shade(&shading,
                    source->vertices[old_index].normal, retained->normal_indices[old_index]) * input->back_lerp;
            }
            vertices[i].color.x *= light.x * shade;
            vertices[i].color.y *= light.y * shade;
            vertices[i].color.z *= light.z * shade;
        }
        if (model->source->format != QA_MODEL_MD5) model_bounds_add(&out->bounds, vertices[i].position);
    }
    out->identity = 0; out->revision = frame->sequence; out->vertices = vertices;
    return true;
}

bool scene_model_source_pose_retain(void *context, qa_error *error)
{
    scene_model_source_pose *pose = context;
    return pose && pose->input.source_model_owner && pose->input.source_model_retain &&
        pose->input.source_model_release &&
        pose->input.source_model_retain(pose->input.source_model_owner, error);
}
void scene_model_source_pose_release(void *context)
{
    scene_model_source_pose *pose = context;
    if (pose) pose->input.source_model_release(pose->input.source_model_owner);
}
bool scene_model_source_pose_read(void *context, int32_t current, int32_t previous, float back,
    qa_scene_frame *frame, qa_scene_mesh *out, qa_error *error)
{
    scene_model_source_pose *pose = context;
    qa_scene_model *model = pose ? pose->model : NULL;
    if (!model || !model->source_topology || pose->surface >= model->source->mesh_count ||
        !frame || !out || current < 0 || previous < 0 || !isfinite(back) ||
        model->checkpoint_active || model->capture || model->image_policy) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source pose requires its retained physical model surface");
        return false;
    }
    const qa_model_mesh *surface = &model->source->meshes[pose->surface];
    const scene_model_mesh *retained = &model->meshes[pose->surface];
    size_t count = surface->vertex_count;
    if (retained->retained.vertex_count != surface->vertex_count ||
        count > SIZE_MAX / sizeof(qa_model_vertex) || count > SIZE_MAX / sizeof(qa_scene_vertex)) {
        qa_error_set(error, QA_ERROR_FORMAT, pose->surface, "Source pose lost its physical vertex extent");
        return false;
    }
    *out = retained->retained;
    if (!surface->vertex_count) return true;
    qa_model_vertex *sampled = qa_arena_alloc(&frame->storage,
        surface->vertex_count * sizeof(*sampled), _Alignof(qa_model_vertex), error);
    qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage,
        surface->vertex_count * sizeof(*vertices), _Alignof(qa_scene_vertex), error);
    if (!sampled || !vertices || !qa_model_sample_mesh(model->source, pose->surface,
        (uint32_t)current, (uint32_t)previous, current == previous ? 0 : back,
        sampled, surface->vertex_count, error)) return false;
    out->bounds = model_bounds_empty();
    for (size_t i = 0; i < surface->vertex_count; ++i) {
        if (retained->sources[i] != i) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Source pose lost its physical vertex order");
            return false;
        }
        vertices[i] = retained->vertices[i];
        vertices[i].position = model_vec(sampled[i].position);
        vertices[i].normal = model_vec(sampled[i].normal);
        model_bounds_add(&out->bounds, vertices[i].position);
    }
    out->vertices = vertices; out->identity = 0; out->revision = frame->sequence;
    return true;
}

static bool model_submit(qa_scene_model *, const qa_scene_model_input *, qa_scene_frame *, unsigned, qa_error *);

static void beam_axes(qa_vec3 direction, uint32_t roll_degrees, qa_model_transform *transform) {
    const float pi = 3.14159265358979323846f;
    float yaw = 0, pitch;
    if (direction.x == 0 && direction.y == 0) pitch = direction.z > 0 ? 90 : 270;
    else {
        yaw = direction.x != 0 ? atan2f(direction.y, direction.x) * 180 / pi : direction.y > 0 ? 90 : 270;
        if (yaw < 0) yaw += 360;
        float forward = sqrtf(direction.x * direction.x + direction.y * direction.y);
        pitch = atan2f(direction.z, forward) * 180 / pi;
        if (pitch < 0) pitch += 360;
    }
    float yaw_radians = (float)(yaw * 0.01745329251994329577);
    float pitch_radians = (float)(-pitch * 0.01745329251994329577);
    float roll_radians = (float)(roll_degrees * 0.01745329251994329577);
    float sy = sinf(yaw_radians), cy = cosf(yaw_radians), sp = sinf(pitch_radians), cp = cosf(pitch_radians);
    float sr = sinf(roll_radians), cr = cosf(roll_radians);
    model_store(transform->axes[0], qa_v3(cp * cy, cp * sy, -sp));
    model_store(transform->axes[1], qa_v3(-((-sr * sp) * cy + (-cr * -sy)),
        -((-sr * sp) * sy + (-cr * cy)), sr * cp));
    model_store(transform->axes[2], qa_v3((cr * sp) * cy + (-sr * -sy), (cr * sp) * sy + (-sr * cy), cr * cp));
}

static bool model_beam(qa_scene_model *model, const qa_scene_model_input *input,
                        qa_scene_frame *frame, unsigned depth, qa_error *error) {
    double segment_length = input->beam_segment_length == 0 ? 30 : input->beam_segment_length;
    if (!isfinite(segment_length) || segment_length <= 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "model beam segment length must be positive"); return false;
    }
    qa_vec3 difference = qa_vec_sub(input->previous_origin, model_origin(input));
    double distance = sqrt((double)difference.x * difference.x + (double)difference.y * difference.y + (double)difference.z * difference.z);
    if (!isfinite(distance)) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "model beam distance is nonfinite"); return false; }
    if (distance == 0) return true;
    if (ceil(distance / segment_length) > (double)(SIZE_MAX / sizeof(qa_scene_draw))) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "model beam has too many segments"); return false;
    }
    qa_vec3 direction = qa_v3((float)(difference.x / distance), (float)(difference.y / distance), (float)(difference.z / distance));
    double seed_number = trunc(input->seconds * 1000) + input->entity;
    if (!isfinite(seed_number) || fabs(seed_number) > 9007199254740991.0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "model beam random seed is outside source integer range"); return false;
    }
    uint32_t random = (uint32_t)(int64_t)seed_number;
    uint32_t count = model->source->frame_group_count ? model->source->frame_group_count : model->source->frame_count;
    if (!count) count = input->animation ? input->animation->frame_count : 1;
    if (!count) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "model beam has no source frames"); return false; }
    for (double offset = 0; offset < distance; offset += segment_length) {
        double length = fmin(distance - offset, segment_length);
        qa_scene_model_input segment = *input;
        segment.model_beam = false; segment.attachments = NULL; segment.attachment_count = 0;
        segment.pose = NULL; segment.pose_count = 0;
        random = random * UINT32_C(69069) + 1;
        segment.frame = segment.old_frame = (random & 32767u) % count; segment.back_lerp = 0;
        random = random * UINT32_C(69069) + 1;
        beam_axes(direction, (random & 32767u) % 360u, &segment.transform);
        segment.transform.scale[0] = (float)(length / segment_length);
        segment.transform.scale[1] = segment.transform.scale[2] = 1;
        qa_vec3 origin = model_origin(input);
        model_store(segment.transform.origin, qa_v3((float)(origin.x + direction.x * (offset + length * 0.5)),
            (float)(origin.y + direction.y * (offset + length * 0.5)), (float)(origin.z + direction.z * (offset + length * 0.5))));
        segment.flags = 8192;
        if (!model_submit(model, &segment, frame, depth, error)) return false;
    }
    return true;
}

static bool submit_attachments(qa_scene_model *model, const qa_scene_model_input *input,
                                qa_scene_frame *frame, unsigned depth, qa_error *error) {
    for (size_t i = 0; i < input->attachment_count; ++i) {
        const qa_scene_model_attachment *attachment = &input->attachments[i];
        if (!attachment->tag || !attachment->model || !attachment->input) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "model attachment requires tag, model and input"); return false;
        }
        qa_model_tag tag;
        float tag_scale = 1;
        if (model->source->format == QA_MODEL_MD3) {
            if (!qa_model_lerp_tag(model->source, attachment->tag, input->old_frame, input->frame,
                                   1 - input->back_lerp, &tag)) continue;
        } else if (model->source->format == QA_MODEL_MD5) {
            size_t length = strlen(attachment->tag);
            uint32_t joint;
            for (joint = 0; joint < model->source->bone_count; ++joint) {
                qa_bytes name = qa_model_bone_name(&model->source->bones[joint]);
                if (name.size == length && !memcmp(name.data, attachment->tag, length)) break;
            }
            if (joint >= model->source->bone_count || joint >= input->pose_count) continue;
            qa_model_joint_tag(&input->pose[joint], &tag); tag_scale = input->pose[joint].scale;
        } else continue;
        qa_model_transform local_tag = {0}, world_tag;
        memcpy(local_tag.origin, tag.origin, sizeof(local_tag.origin));
        memcpy(local_tag.axes, tag.axes, sizeof(local_tag.axes));
        local_tag.scale[0] = local_tag.scale[1] = local_tag.scale[2] = tag_scale;
        qa_model_transform_compose(&input->transform, &local_tag, &world_tag);
        qa_scene_model_input child = *attachment->input;
        qa_model_transform_compose(&world_tag, &attachment->input->transform, &child.transform);
        float delta[3], moved[3];
        model_store(delta, qa_vec_sub(child.previous_origin, model_origin(attachment->input)));
        qa_model_transform_direction(&world_tag, delta, moved);
        child.previous_origin = qa_vec_add(model_origin(&child), model_vec(moved));
        child.view = input->view; child.seconds = input->seconds;
        child.milliseconds = input->milliseconds; child.has_milliseconds = input->has_milliseconds;
        child.ambient = input->ambient; child.directed = input->directed; child.light_direction = input->light_direction;
        child.shadow_only = input->shadow_only;
        if (!model_submit(attachment->model, &child, frame, depth + 1, error)) return false;
    }
    return true;
}

static bool sample_animation_pose(qa_scene_model *model, qa_scene_model_input *input,
    qa_scene_frame *frame, qa_error *error)
{
    const qa_model_animation *animation = input->animation;
    if (!animation->frame_count || animation->joint_count != model->source->bone_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "MD5 animation does not match retained mesh"); return false;
    }
    size_t count = animation->joint_count, bytes = count * sizeof(*model->sampled_pose);
    const qa_model_pose *current = animation->poses + (size_t)(input->frame % animation->frame_count) * count;
    const qa_model_pose *old = animation->poses + (size_t)(input->old_frame % animation->frame_count) * count;
    bool frames_equal = input->frame == input->old_frame;
    int rounding = fegetround();
    for (unsigned i = 0; i < SCENE_MODEL_POSE_VARIANTS; ++i) {
        scene_model_pose_variant *pose = &model->poses[i];
        if (pose->ready && pose->rounding == rounding && pose->frames_equal == frames_equal &&
            (frames_equal || !memcmp(&pose->back_lerp, &input->back_lerp, sizeof(input->back_lerp))) &&
            !memcmp(pose->frames, current, bytes) && !memcmp(pose->frames + count, old, bytes)) {
            input->pose = pose->pose; input->pose_count = count;
            pose->frame_sequence = frame->sequence;
            return true;
        }
    }
    unsigned index = model->next_pose;
    unsigned scanned = 0;
    while (scanned < SCENE_MODEL_POSE_VARIANTS && (model->poses[index].frame_references ||
        (model->poses[index].ready && model->poses[index].frame_sequence == frame->sequence))) {
        index = (index + 1) % SCENE_MODEL_POSE_VARIANTS;
        ++scanned;
    }
    if (scanned == SCENE_MODEL_POSE_VARIANTS) {
        qa_model_pose *transient = qa_arena_alloc(&frame->storage, bytes, _Alignof(qa_model_pose), error);
        if (!transient || !qa_model_animation_sample(animation, input->frame, input->old_frame,
            input->back_lerp, transient, count, error)) return false;
        input->pose = transient; input->pose_count = count;
        return true;
    }
    model->next_pose = (index + 1) % SCENE_MODEL_POSE_VARIANTS;
    scene_model_pose_variant *pose = &model->poses[index];
    pose->ready = false;
    for (uint32_t i = 0; i < model->source->mesh_count; ++i) model->meshes[i].samples[index].pose = NULL;
    if (!qa_model_animation_sample(animation, input->frame, input->old_frame,
        input->back_lerp, pose->pose, count, error)) return false;
    memcpy(pose->frames, current, bytes);
    memcpy(pose->frames + count, old, bytes);
    pose->back_lerp = input->back_lerp;
    pose->frames_equal = frames_equal;
    pose->rounding = rounding;
    pose->frame_sequence = frame->sequence;
    pose->ready = true;
    input->pose = pose->pose;
    input->pose_count = count;
    return true;
}

static bool model_submit_body(qa_scene_model *model, const qa_scene_model_input *original,
                          qa_scene_frame *frame, unsigned depth, qa_error *error) {
    if (depth >= 64) { qa_error_set(error, QA_ERROR_ARGUMENT, depth, "model attachment graph is cyclic or too deep"); return false; }
    qa_scene_model_input input = *original;
    if (!isfinite(input.back_lerp) ||
        !isfinite(input.seconds) || !isfinite(input.sync_base) || !isfinite(input.color.w) ||
        (input.attachment_count && !input.attachments) || (input.pose_count && !input.pose) ||
        (input.render_text_count && !input.render_texts) ||
        (input.shadow_light_count && !input.shadow_lights) ||
        (input.custom_skin && input.custom_skin->count && !input.custom_skin->mappings) ||
        (input.custom_skin_materials && (!input.custom_skin ||
            input.custom_skin_material_count != input.custom_skin->count)) ||
        (input.material_library && model->options.family != QA_GAME_Q3) ||
        !qa_vec_finite(input.previous_origin) || !qa_vec_finite(input.ambient) ||
        !qa_vec_finite(input.directed) || !qa_vec_finite(input.light_direction) ||
        input.alias_lighting < QA_ALIAS_CONTENT_LIGHTING || input.alias_lighting > QA_ALIAS_PREPARED_LIGHT ||
        (input.alias_lighting == QA_ALIAS_PREPARED_LIGHT && !qa_vec_finite(input.alias_light)) ||
        !isfinite(input.color.x) || !isfinite(input.color.y) || !isfinite(input.color.z) ||
        !isfinite(input.shadow_plane) || !isfinite(input.shader_time) || !isfinite(input.rotation)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene model pose or optional span"); return false;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!isfinite(input.transform.origin[i]) || !isfinite(input.transform.scale[i]) ||
            !qa_vec_finite(model_vec(input.transform.axes[i]))) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "scene model transform is nonfinite"); return false;
        }
    }
    if (input.model_beam) return model_beam(model, &input, frame, depth, error);
    if (input.shadow_only && !casts_shadow(model, &input)) return true;
    if (input.shadow_only && input.family == QA_GAME_Q2) input.flags &= ~(1024u | 2048u | 4096u | 65536u | 131072u);
    if (input.family == QA_GAME_Q2 && (input.flags & 128)) {
        if (model->options.palette_rgb.size != 768) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 beam requires its source palette"); return false;
        }
        unsigned color = (input.skin & 255u) * 3;
        qa_vec4 tint = {model->palette[color] / 255.0f, model->palette[color + 1] / 255.0f,
                              model->palette[color + 2] / 255.0f, input.color.w};
        return qa_scene_beam(frame, &input.view, model_origin(&input), input.previous_origin,
                              (float)input.frame, tint, qa_scene_white(model->resources), error);
    }
    if (model->source->format == QA_MODEL_MDL && input.indexed_skin) {
        input.replacement = NULL;
    } else if (model->replacement_policy_set) {
        input.replacement = NULL;
        qa_scene_model *selected = model->selected_replacement;
        double dx = (double)input.transform.origin[0] - input.view.origin.x;
        double dy = (double)input.transform.origin[1] - input.view.origin.y;
        double dz = (double)input.transform.origin[2] - input.view.origin.z;
        double distance = sqrt(dx * dx + dy * dy + dz * dz);
        if (selected && model->replacement_policy_enabled &&
            (input.shadow_only || model->replacement_distance <= 0 || !(distance > model->replacement_distance))) {
            input.replacement = selected->replacement_source;
            model = selected; input.animation = input.replacement->animation;
        }
    } else if (input.replacement) {
        if (!prepare_replacement(model, input.replacement, error)) return false;
        model = model->replacement; input.animation = input.replacement->animation;
    }
    repair_frames(model, &input);
    bool source_md4 = input.source_order && model->source_topology && model->source->format == QA_MODEL_MD4;
    if (source_md4) {
        input.custom_material = NULL; input.custom_skin = NULL;
        input.custom_skin_materials = NULL; input.custom_skin_material_count = 0;
        input.fog_index = 0; input.fog = (qa_scene_fog){0}; input.fog_has_surface = false;
        input.fog_tc_scale = 0; input.fog_surface = (qa_scene_plane){0};
        input.no_cull = true;
    }
    if (model->source->format == QA_MODEL_MD5 && !input.pose) {
        if (input.animation) {
            if (depth != 0 || model->active_submissions > 1) {
                size_t count = input.animation->joint_count;
                if (count != model->source->bone_count || count > SIZE_MAX / sizeof(qa_model_pose)) {
                    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "MD5 animation does not match retained mesh"); return false;
                }
                qa_model_pose *pose = qa_arena_alloc(&frame->storage, count * sizeof(*pose),
                    _Alignof(qa_model_pose), error);
                if (!pose || !qa_model_animation_sample(input.animation, input.frame, input.old_frame,
                    input.back_lerp, pose, count, error)) return false;
                input.pose = pose; input.pose_count = count;
            } else if (!sample_animation_pose(model, &input, frame, error)) return false;
        } else { input.pose = model->source->bind_pose; input.pose_count = model->source->bone_count; }
    }
    bool visible = true;
    bool source_md3 = input.source_order && model->source->format == QA_MODEL_MD3;
    if (!input.shadow_only) {
        if (input.family == QA_GAME_Q3 && !source_md4) {
            if ((input.flags & 2) && !input.view.clip_enabled && input.shadow_mode != 2 && input.shadow_mode != 3) visible = false;
            if ((input.flags & 4) && input.view.clip_enabled) visible = false;
        }
        if (input.family == QA_GAME_Q2 && (input.flags & 4) && input.left_hand == 2) visible = false;
    }
    if (source_md3 && !input.shadow_only && !source_md3_visible(model, &input)) visible = false;
    if (visible && (model->source->format == QA_MODEL_SPR || model->source->format == QA_MODEL_SP2)) {
        if (!scene_model_sprite_submit(model, &input, input.frame, frame, error)) return false;
    } else if (visible) {
        uint32_t first = 0, count = model->source->mesh_count;
        if (model->source->lod_count) {
            uint32_t lod = input.lod < model->source->lod_count ? input.lod : model->source->lod_count - 1;
            first = model->source->lods[lod].first_mesh; count = model->source->lods[lod].mesh_count;
        }
        bool defer = md5_deferred_allowed(model, &input, original, depth);
        for (uint32_t i = first; i < first + count; ++i) {
            qa_scene_mesh mesh;
            bool mesh_visible = true;
            bool weapon = input.family == QA_GAME_Q2 && (input.view_model || (input.flags & 4));
            bool cull = !source_md3 && !input.no_cull && !input.shadow_only && !weapon;
            scene_model_source_pose *pose = NULL;
            const qa_scene_skinning *skinning = NULL;
            unsigned sample_index = SCENE_MODEL_TRANSIENT_SAMPLE;
            bool deferred_mesh = false;
            scene_model_image *image;
            scene_model_image external = {0};
            if (model->source_topology && input.source_scratch && input.source_model_owner && !input.shadow_only) {
                pose = qa_arena_alloc(&frame->storage, sizeof(*pose), _Alignof(scene_model_source_pose), error);
                if (!pose) return false;
                *pose = (scene_model_source_pose){.model = model, .surface = i, .input = *original};
                mesh = model->meshes[i].retained;
            } else {
                deferred_mesh = defer && md5_deferred_mesh(model, &input, i, cull,
                    &mesh, &sample_index, &mesh_visible);
                if (!deferred_mesh && !mesh_geometry(model, &input, i, cull, frame, &mesh, &mesh_visible, error)) return false;
            }
            if (!mesh_visible) continue;
            if (!select_image(model, &input, i, &external, frame, &image, error)) return false;
            if (deferred_mesh) {
                if (image && image->material && !scene_model_has_shell(&input)) {
                    if (!mesh_geometry(model, &input, i, cull, frame, &mesh, &mesh_visible, error)) return false;
                    if (!mesh_visible) continue;
                } else if (!md5_deferred_skin(model, &input, i, sample_index,
                    frame, &skinning, error)) return false;
            }
            if (model->source->format != QA_MODEL_MD5 && cull && mesh.vertex_count) {
                qa_model_bounds local = cull_bounds(model, original, &mesh), world;
                qa_model_transform_bounds(&input.transform, &local, &world);
                qa_bounds bounds = {model_vec(world.min), model_vec(world.max)};
                qa_scene_plane planes[6];
                size_t plane_count = qa_scene_frustum(&input.view, planes);
                if (!qa_scene_bounds_visible(bounds, planes, plane_count)) continue;
            }
            if (!scene_model_emit(model, &input, &mesh, image, scene_model_has_shell(&input), false,
                pose, skinning, frame, error)) return false;
        }
    }
    return submit_attachments(model, &input, frame, depth, error);
}

static bool model_submit(qa_scene_model *model, const qa_scene_model_input *input,
                         qa_scene_frame *frame, unsigned depth, qa_error *error) {
    for (const qa_scene_model *owner = model; owner; owner = owner->replacement_parent)
        if (owner->checkpoint_active || owner->capture || owner->destroy_pending) {
            qa_error_set(error, QA_ERROR_ARGUMENT, depth, "model owner checkpoint is active");
            return false;
        }
    if (model->active_submissions == UINT_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, depth, "model submission nesting exceeds its owner counter");
        return false;
    }
    ++model->active_submissions;
    bool ok = model_submit_body(model, input, frame, depth, error);
    --model->active_submissions;
    return ok;
}

bool qa_scene_model_submit(qa_scene_model *model, const qa_scene_model_input *input,
                           qa_scene_frame *frame, qa_error *error) {
    if (!model || !input || !frame) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "model, input and frame are required"); return false; }
    size_t commands = frame->command_count;
    size_t groups = frame->group_count;
    if (model_submit(model, input, frame, 0, error)) return true;
    frame->command_count = commands;
    frame->group_count = groups;
    return false;
}
