#include "qa/q3_assets_save.h"
#include "equipment_gear_private.h"
#include "equipment_gear_output.h"
#include "qa/q3_asset_shader.h"
#include <math.h>

struct frontend_equipment_gear_output {
    frontend_equipment_gear_presenter *presenter;
    application_equipment_gear_presentation source;
    qa_q3_ref_entity *refs, *passes;
    qa_q3_presentation_assets *primary_assets;
    size_t count, capacity, base_count, pass_count, pass_capacity;
    bool view, source_style;
};
typedef struct gear_draw_call {
    frontend_equipment_gear_output *output;
    void *context;
    bool (*current)(void *);
} gear_draw_call;

static bool source_current(const frontend_equipment_gear_output *output)
{
    const equipment_gear_content *owner = output ? output->presenter->owner : NULL;
    return owner && qa_actor_id_equal(output->source.actor, output->presenter->actor) &&
        output->source.source.gear_owner == owner->view.source.owner &&
        output->source.source.definition == owner->view.definition &&
        output->source.gear.player.product == owner->view.product &&
        qa_application_equipment_content_current(owner->frontend->application, &owner->view.source) &&
        application_equipment_gear_presentation_current(owner->frontend->application, &output->source);
}
static bool call_current(void *context)
{
    gear_draw_call *call = context;
    return call->current(call->context) && source_current(call->output);
}
static bool collect(void *context, qa_q3_presentation_assets *assets,
    const qa_q3_ref_entity *ref, qa_error *error)
{
    gear_draw_call *call = context; frontend_equipment_gear_output *output = call->output;
    if (!call_current(call) || !ref || assets != output->presenter->owner->view.assets ||
        output->count == output->capacity)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear draw exceeds its actual registry or output lifetime");
    output->refs[output->count++] = *ref; return true;
}
void frontend_equipment_gear_output_destroy(frontend_equipment_gear_output *output)
{
    if (!output) return;
    frontend_equipment_gear_release(output->presenter);
}

static bool draw(frontend_equipment_gear_presenter *presenter,
    const application_equipment_gear_presentation *source, const q3n_selected_weapon_view *view,
    float fov, const q3n_selected_weapon_held *held, bool reduced_flashes,
    void *context, bool (*current)(void *), frontend_equipment_gear_output **out,
    bool *submitted, qa_error *error)
{
    if (submitted) *submitted = false;
    if (!presenter || !source || !out || *out || !submitted || !current || !current(context) ||
        !qa_actor_id_equal(presenter->actor, source->actor))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear draw requires its actual presenter and recipient");
    const application_q3_grapple_definition *definition = presenter->owner->view.definition;
    size_t attachments = view ? q3n_selected_authored_attachment_count(presenter->owner->view.media) : 0;
    size_t capacity = view ? attachments + 1 : 7;
    if ((view && attachments == SIZE_MAX) || capacity > SIZE_MAX / sizeof(qa_q3_ref_entity) ||
        attachments > SIZE_MAX / sizeof(q3n_selected_weapon_attachment))
        return frontend_fail(error, QA_ERROR_MEMORY, "Gear authored output exceeds address space");
    qa_arena *storage=&presenter->owner->frontend->frame.storage;
    frontend_equipment_gear_output *output=qa_arena_alloc(storage,sizeof(*output),_Alignof(frontend_equipment_gear_output),error);
    if (!output) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual gear output");
    *output=(frontend_equipment_gear_output){0};
    output->refs=qa_arena_alloc(storage,capacity*sizeof(*output->refs),_Alignof(qa_q3_ref_entity),error);
    if (!output->refs) {
        return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual gear draw references");
    }
    if (!frontend_equipment_gear_retain(presenter, error)) {
        return false;
    }
    output->presenter = presenter; output->source = *source; output->capacity = capacity; output->view = view != NULL;
    gear_draw_call call = {output, context, current}; q3n_selected_weapon_media media;
    bool okay = source_current(output);
    if (!okay) frontend_fail(error, QA_ERROR_ARGUMENT, "Gear draw changed its actual private source tuple");
    if (okay) okay = q3n_selected_authored_read(presenter->owner->view.media, view != NULL, &media, error);
    q3n_selected_weapon_draw request = {.player = &output->source.gear.player,
        .presentation_weapon = definition->presentation.weapon_index, .time = source->gear.time_ms,
        .firing = source->gear.tether.registry != 0, .reduced_flashes = reduced_flashes,
        .context = &call, .current = call_current, .submit = collect};
    q3n_selected_weapon_attachment *parts=attachments?qa_arena_alloc(storage,attachments*sizeof(*parts),
        _Alignof(q3n_selected_weapon_attachment),error):NULL;
    if (okay && attachments && !parts)
        okay = frontend_fail(error, QA_ERROR_MEMORY, "Reading retained gear attachment handles");
    for (size_t i = 0; okay && i < attachments; ++i) {
        q3n_selected_authored_attachment_view actual;
        okay = q3n_selected_authored_attachment_read(presenter->owner->view.media, i, &actual, error);
        if (okay) parts[i] = (q3n_selected_weapon_attachment){actual.model, actual.tag};
    }
    if (okay && view) {
        q3n_selected_weapon_authored_view actual = {.camera = *view,
            .anchor_tag = definition->presentation.anchor_tag,
            .anchor_offset = definition->presentation.anchor_offset, .field_of_view = fov,
            .fov_above = definition->presentation.fov_above, .fov_scale = definition->presentation.fov_scale,
            .attachments = parts, .attachment_count = attachments};
        okay = q3n_weapons_selected_authored_view(presenter->owner->weapons, &media, &presenter->state,
            &request, &actual, submitted, error);
    } else if (okay) okay = q3n_weapons_selected_held(presenter->owner->weapons, &media, &presenter->state,
        &request, held, submitted, error);
    if (!okay || !*submitted) { frontend_equipment_gear_output_destroy(output); return okay; }
    *out = output; return true;
}
bool frontend_equipment_gear_view(frontend_equipment_gear_presenter *presenter,
    const application_equipment_gear_presentation *source, const q3n_selected_weapon_view *view, float fov,
    void *context, bool (*current)(void *), frontend_equipment_gear_output **out, bool *submitted, qa_error *error)
{
    if (!view) return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear view has no actual camera");
    return draw(presenter, source, view, fov, NULL, false, context, current, out, submitted, error);
}
bool frontend_equipment_gear_held(frontend_equipment_gear_presenter *presenter,
    const application_equipment_gear_presentation *source, const q3n_selected_weapon_held *held, bool reduced_flashes,
    void *context, bool (*current)(void *), frontend_equipment_gear_output **out, bool *submitted, qa_error *error)
{
    if (!held) return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear held has no actual torso");
    return draw(presenter, source, NULL, 0, held, reduced_flashes, context, current, out, submitted, error);
}
size_t frontend_equipment_gear_output_count(const frontend_equipment_gear_output *output)
{
    return !output ? 0 : !output->source_style ? output->count :
        output->base_count * output->pass_count + output->count - output->base_count;
}
bool frontend_equipment_gear_output_source_style(frontend_equipment_gear_output *output,
    qa_q3_presentation_assets *assets, const qa_q3_ref_entity *parent, qa_error *error)
{
    q3n_selected_weapon_media media;
    if (!output || output->view || output->source_style || !assets || !parent || !source_current(output) ||
        !qa_q3_assets_idle(assets) || parent->kind != QA_Q3_REF_MODEL ||
        !qa_vec_finite(parent->lighting_origin) || !isfinite(parent->shadow_plane))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear source style requires its actual primary torso");
    if (!q3n_selected_authored_read(output->presenter->owner->view.media, false, &media, error)) return false;
    if (!output->count || output->refs[0].model != media.gun || output->refs[0].custom_shader)
        return frontend_fail(error, QA_ERROR_FORMAT, "Gear source style has no genuine base gun");
    size_t base = 1;
    if (output->count > base && media.barrel && output->refs[base].model == media.barrel &&
        !output->refs[base].custom_shader) ++base;
    if (output->count > base && (output->count != base + 1 ||
        output->refs[base].model != media.flash || output->refs[base].custom_shader))
        return frontend_fail(error, QA_ERROR_FORMAT, "Gear source style contains unrelated native passes");
    for (size_t i = 0; i < output->count; ++i) {
        output->refs[i].flags = parent->flags; output->refs[i].lighting_origin = parent->lighting_origin;
        output->refs[i].shadow_plane = parent->shadow_plane;
    }
    output->primary_assets = assets; output->base_count = base; output->source_style = true; return true;
}
bool frontend_equipment_gear_output_source_pass(frontend_equipment_gear_output *output,
    const qa_q3_ref_entity *pass, qa_error *error)
{
    const qa_material *material;
    if (!output || !output->source_style || !pass || !isfinite(pass->shader_time) || !source_current(output))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear source pass has no actual captured primary style");
    if (!qa_q3_assets_shader_read(output->primary_assets, pass->custom_shader, &material, error)) return false;
    size_t remaining = output->count - output->base_count;
    if (output->pass_count >= (1022 - remaining) / output->base_count)
        return frontend_fail(error, QA_ERROR_FORMAT, "Gear held output exceeds its actual source orders");
    if (output->pass_count == output->pass_capacity) {
        size_t capacity = output->pass_capacity ? output->pass_capacity * 2 : 4;
        if (capacity > 1022) capacity = 1022;
        qa_q3_ref_entity *passes=qa_arena_grow(&output->presenter->owner->frontend->frame.storage,output->passes,
            output->pass_count*sizeof(*passes),capacity*sizeof(*passes),_Alignof(qa_q3_ref_entity),error);
        if (!passes) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual gear source passes");
        output->passes = passes; output->pass_capacity = capacity;
    }
    output->passes[output->pass_count++] = *pass; return true;
}
static qa_q3_ref_entity shifted(const qa_q3_ref_entity *source, qa_vec3 offset)
{
    qa_q3_ref_entity ref = *source;
    ref.origin = qa_vec_add(ref.origin, offset); ref.old_origin = qa_vec_add(ref.old_origin, offset);
    ref.lighting_origin = qa_vec_add(ref.lighting_origin, offset); ref.shadow_plane += offset.z;
    return ref;
}
bool frontend_equipment_gear_output_submit(frontend_equipment_gear_output *output,
    qa_q3_presentation *presentation, qa_vec3 offset, const qa_q3_scene_options *options,
    uint32_t order, qa_scene_frame *frame, qa_error *error)
{
    if (!output || !source_current(output) || !presentation || !options || !frame || !qa_vec_finite(offset) ||
        order > 1022 || frontend_equipment_gear_output_count(output) > 1022 - order)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear submission lost its actual registry, lease or orders");
    for (size_t i = 0; i < output->count; ++i) {
        qa_q3_ref_entity ref = shifted(output->refs + i, offset);
        if (!qa_vec_finite(ref.origin) || !qa_vec_finite(ref.old_origin) ||
            !qa_vec_finite(ref.lighting_origin) || !isfinite(ref.shadow_plane))
            return frontend_fail(error, QA_ERROR_FORMAT, "Gear offset exceeds finite scene coordinates");
    }
    size_t first = 0;
    if (output->source_style) {
        for (size_t pass = 0; pass < output->pass_count; ++pass)
            for (size_t part = 0; part < output->base_count; ++part) {
                qa_q3_ref_entity ref = shifted(output->refs + part, offset);
                if (!qa_q3_presentation_selected_registered_pass(presentation, output->presenter->owner->view.assets,
                    &ref, output->primary_assets, output->passes + pass, options, order++, frame, error)) return false;
            }
        first = output->base_count;
    }
    for (size_t i = first; i < output->count; ++i) {
        qa_q3_ref_entity ref = shifted(output->refs + i, offset);
        if (!qa_q3_presentation_selected_registered(presentation, output->presenter->owner->view.assets,
            &ref, options, order++, frame, error)) return false;
    }
    return true;
}
