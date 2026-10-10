#include "equipment_held_output.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_asset_shader.h"
#include <limits.h>
#include <math.h>

struct frontend_equipment_held_output {
    qa_frontend *frontend;
    qa_application_equipment_view source;
    frontend_equipment_media_view media;
    frontend_equipment_media *media_owner;
    qa_q3_presentation_assets *assets;
    qa_q3_ref_entity parent;
    qa_model_transform transform;
    qa_q3_ref_entity *passes;
    size_t count, capacity;
};

static bool current(const frontend_equipment_held_output *output)
{
    return output && qa_application_equipment_current(output->frontend->application,
        &output->source);
}

static void parent_transform(const qa_q3_ref_entity *parent, qa_model_transform *out)
{
    qa_model_transform_identity(out);
    out->origin[0] = parent->origin.x;
    out->origin[1] = parent->origin.y;
    out->origin[2] = parent->origin.z;
    for (size_t i = 0; i < 3; ++i) {
        out->axes[i][0] = parent->axis[i].x;
        out->axes[i][1] = parent->axis[i].y;
        out->axes[i][2] = parent->axis[i].z;
    }
}

bool frontend_equipment_held_output_create(qa_frontend *frontend,
    const qa_application_equipment_view *source, frontend_equipment_media *media,
    qa_q3_presentation_assets *assets, const qa_q3_ref_entity *parent,
    frontend_equipment_held_output **out, qa_error *error)
{
    return frontend_equipment_held_output_create_from(frontend, source, media,
        assets, assets, parent, out, error);
}

bool frontend_equipment_held_output_create_from(qa_frontend *frontend,
    const qa_application_equipment_view *source, frontend_equipment_media *media,
    const qa_q3_presentation_assets *parent_assets, qa_q3_presentation_assets *assets,
    const qa_q3_ref_entity *parent, frontend_equipment_held_output **out, qa_error *error)
{
    frontend_equipment_media_view retained;
    if (!frontend || !source || !source->selected || !parent || !parent_assets || !assets || !out ||
        !qa_application_equipment_current(frontend->application, source) ||
        !frontend_equipment_media_read(media, &retained) ||
        retained.provider != source->provider || retained.family != source->family ||
        retained.gear_namespace != source->gear_namespace ||
        retained.gear_service_owner != source->gear_service_owner ||
        retained.item != source->item || retained.source_slot!=source->source_slot ||
        (source->source_slot ? retained.source_generation!=source->source_generation :
            (!source->view_model || retained.strings != qa_session_strings(qa_application_session(frontend->application)) ||
                retained.view_path != source->view_model)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Held replacement requires its actual selected media and source actor");
    if (parent->kind != QA_Q3_REF_MODEL || !qa_vec_finite(parent->origin) ||
        !qa_vec_finite(parent->lighting_origin) || !qa_vec_finite(parent->axis[0]) ||
        !qa_vec_finite(parent->axis[1]) || !qa_vec_finite(parent->axis[2]) ||
        !isfinite(parent->back_lerp) || !isfinite(parent->shadow_plane))
        return frontend_fail(error, QA_ERROR_FORMAT, "Held replacement parent is not a finite authored model");
    frontend_equipment_held_output *output = calloc(1, sizeof(*output));
    if (!output) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual held replacement output");
    output->frontend = frontend; output->source = *source;
    output->media = retained; output->assets = assets; output->parent = *parent;
    if (!frontend_equipment_media_retain(media, error)) {
        free(output); return false;
    }
    output->media_owner = media;
    if (retained.declaration->none) { *out = output; return true; }
    qa_q3_asset_model_holder holder;
    qa_model_tag tag;
    bool found = false;
    bool ok = parent->model > 0 &&
        qa_q3_assets_model_holder(parent_assets, (size_t)parent->model - 1, &holder, error);
    if (ok && (!holder.present || !holder.sources[0]))
        ok = frontend_fail(error, QA_ERROR_FORMAT, "Held replacement parent has no actual registered model holder");
    if (ok) ok = qa_q3_presentation_tag(parent_assets, parent->model, "tag_weapon",
        parent->old_frame, parent->frame, 1 - parent->back_lerp, &tag, &found, error);
    if (ok && !found)
        ok = frontend_fail(error, QA_ERROR_FORMAT, "Original source torso has no weapon attachment");
    if (ok && (!retained.held || !retained.held->model || !retained.held_scene ||
            retained.held->reference_frame > INT32_MAX))
        ok = frontend_fail(error, QA_ERROR_FORMAT, "Held replacement has no real prepared model and scene");
    if (ok) {
        qa_model_transform parent_pose, socket, tagged;
        parent_transform(parent, &parent_pose);
        qa_model_transform_identity(&socket);
        memcpy(socket.origin, tag.origin, sizeof(socket.origin));
        memcpy(socket.axes, tag.axes, sizeof(socket.axes));
        qa_model_transform_compose(&parent_pose, &socket, &tagged);
        qa_model_transform_compose(&tagged, &retained.held->alignment, &output->transform);
        for (size_t i = 0; ok && i < 3; ++i) {
            ok = isfinite(output->transform.origin[i]) && isfinite(output->transform.scale[i]);
            for (size_t j = 0; ok && j < 3; ++j)
                ok = isfinite(output->transform.axes[i][j]);
        }
        if (!ok) frontend_fail(error, QA_ERROR_FORMAT, "Actual held attachment exceeds finite transform coordinates");
    } else if (parent->model <= 0)
        frontend_fail(error, QA_ERROR_FORMAT, "Held replacement parent has no physical model handle");
    if (!ok) { frontend_equipment_held_output_destroy(output); return false; }
    *out = output;
    return true;
}

bool frontend_equipment_held_output_pass(frontend_equipment_held_output *output,
    const qa_q3_ref_entity *source, qa_error *error)
{
    if (!current(output) || !source || !isfinite(source->shader_time))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Held pass lost its selected source or shader time");
    if (output->media.declaration->none) return true;
    const qa_material *material;
    if (!qa_q3_assets_shader_read(output->assets, source->custom_shader, &material, error)) return false;
    if (output->count == 1022)
        return frontend_fail(error, QA_ERROR_FORMAT, "Held replacement passes exceed the actual source scene extent");
    if (output->count == output->capacity) {
        size_t capacity = output->capacity ? output->capacity * 2 : 4;
        if (capacity > 1022) capacity = 1022;
        qa_q3_ref_entity *passes = realloc(output->passes, capacity * sizeof(*passes));
        if (!passes) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual held shader passes");
        output->passes = passes; output->capacity = capacity;
    }
    qa_q3_ref_entity pass = output->parent;
    pass.frame = pass.old_frame = (int32_t)output->media.held->reference_frame;
    pass.back_lerp = 0; pass.skin = pass.custom_skin = 0;
    pass.custom_shader = source->custom_shader; pass.shader_time = source->shader_time;
    pass.shader_texcoord = (qa_vec2){0};
    pass.non_normalized_axes = false;
    memset(pass.color, 255, sizeof(pass.color));
    if (material) memcpy(pass.color, source->color, sizeof(pass.color));
    pass.origin = qa_v3(output->transform.origin[0], output->transform.origin[1], output->transform.origin[2]);
    pass.old_origin = pass.origin;
    output->passes[output->count++] = pass;
    return true;
}

size_t frontend_equipment_held_output_count(const frontend_equipment_held_output *output)
{
    return output ? output->count : 0;
}

bool frontend_equipment_held_output_submit(frontend_equipment_held_output *output,
    qa_q3_presentation *presentation, const qa_q3_scene_options *options,
    uint32_t first_order, qa_scene_frame *frame, qa_error *error)
{
    if (!current(output) || !presentation || !options || !frame ||
        first_order > 1022 || output->count > 1022 - first_order ||
        qa_q3_presentation_resources(presentation) != output->assets)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Held output requires its actual source renderer and reserved scene order");
    for (size_t i = 0; i < output->count; ++i)
        if (!qa_q3_presentation_selected_model(presentation, output->media.held_scene,
                output->media.held->model, output->media.held_parent.path,
                &output->transform, output->passes + i, options,
                first_order + (uint32_t)i, frame, error)) return false;
    return true;
}

void frontend_equipment_held_output_destroy(frontend_equipment_held_output *output)
{
    if (!output) return;
    free(output->passes);
    frontend_equipment_media_release(output->media_owner);
    free(output);
}
