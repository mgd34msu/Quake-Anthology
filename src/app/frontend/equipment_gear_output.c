#include "qa/q3_assets_save.h"
#include "equipment_gear_private.h"
#include "equipment_gear_output.h"
#include "equipment_media.h"
#include "equipment_output.h"
#include <math.h>

typedef struct equipment_gear_output {
    frontend_equipment_gear_presenter *presenter;
    application_equipment_gear_presentation source;
    frontend_equipment_output draw;
} equipment_gear_output;
typedef struct gear_draw_call {
    equipment_gear_output *output;
    void *context;
    bool (*current)(void *);
} gear_draw_call;

static bool source_current(const equipment_gear_output *output)
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
static bool collect(void *context,qa_q3_presentation_assets *assets,
    const qa_q3_ref_entity *ref,qa_error *error)
{
    gear_draw_call *call=context;
    if (!call_current(call))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Gear draw lost its actual output lifetime");
    return frontend_equipment_output_collect(&call->output->draw,assets,ref,error);
}
static bool output_current(void *context)
{ return source_current(context); }
static void output_release(void *context)
{ frontend_equipment_gear_release(((equipment_gear_output *)context)->presenter); }
static bool base_group(void *,size_t *,qa_error *);

static bool draw(frontend_equipment_gear_presenter *presenter,
    const application_equipment_gear_presentation *source, const q3n_selected_weapon_view *view,
    float fov, const q3n_selected_weapon_held *held, bool reduced_flashes,
    void *context, bool (*current)(void *), frontend_equipment_output **out,
    bool *submitted, qa_error *error)
{
    if (submitted) *submitted = false;
    if (!presenter || !source || !out || *out || !submitted || !current || !current(context) ||
        !qa_actor_id_equal(presenter->actor, source->actor))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear draw requires its actual presenter and recipient");
    const application_q3_grapple_definition *definition = presenter->owner->view.definition;
    size_t attachments = q3n_selected_authored_attachment_count(presenter->owner->view.media);
    if (attachments > SIZE_MAX - 7)
        return frontend_fail(error, QA_ERROR_MEMORY, "Gear authored output exceeds address space");
    size_t capacity = attachments + (view ? 1 : 7);
    if (capacity > SIZE_MAX / sizeof(qa_q3_ref_entity) ||
        attachments > SIZE_MAX / sizeof(q3n_selected_weapon_attachment))
        return frontend_fail(error, QA_ERROR_MEMORY, "Gear authored output exceeds address space");
    qa_arena *storage=&presenter->owner->frontend->frame.storage;
    equipment_gear_output *output=qa_arena_alloc(storage,sizeof(*output),_Alignof(equipment_gear_output),error);
    if (!output) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual gear output");
    *output=(equipment_gear_output){.draw={.assets=presenter->owner->view.assets,.storage=storage,
        .context=output,.current=output_current,.release=output_release,.base_group=base_group}};
    output->draw.refs=qa_arena_alloc(storage,capacity*sizeof(*output->draw.refs),_Alignof(qa_q3_ref_entity),error);
    if (!output->draw.refs) {
        return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual gear draw references");
    }
    if (!frontend_equipment_gear_retain(presenter, error)) {
        return false;
    }
    output->presenter = presenter; output->source = *source; output->draw.capacity = capacity; output->draw.view = view != NULL;
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
            .anchor_tag = media.anchor_tag,
            .anchor_offset = definition->presentation.anchor_offset, .field_of_view = fov,
            .fov_above = definition->presentation.fov_above, .fov_scale = definition->presentation.fov_scale,
            .attachments = parts, .attachment_count = attachments};
        okay = q3n_weapons_selected_authored_view(presenter->owner->weapons, &media, &presenter->state,
            &request, &actual, submitted, error);
    } else if (okay) {
        q3n_selected_weapon_held actual = *held;
        actual.attachments = parts;
        actual.attachment_count = attachments;
        if (media.gun) okay = frontend_held_carrier_prepare(held->parent_assets, held->torso,
            (double)presenter->owner->frontend->time_ns * 1e-9,
            &output->draw.carrier, &actual.carrier_grip, error);
        if (okay) okay = q3n_weapons_selected_held(presenter->owner->weapons, &media, &presenter->state,
            &request, &actual, submitted, error);
        if (okay && actual.carrier_grip)
            frontend_equipment_output_parent(&output->draw,held->torso,true);
    }
    if (!okay || !*submitted) { frontend_equipment_output_destroy(&output->draw); return okay; }
    *out=&output->draw; return true;
}
bool frontend_equipment_gear_view(frontend_equipment_gear_presenter *presenter,
    const application_equipment_gear_presentation *source, const q3n_selected_weapon_view *view, float fov,
    void *context, bool (*current)(void *), frontend_equipment_output **out, bool *submitted, qa_error *error)
{
    if (!view) return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear view has no actual camera");
    return draw(presenter, source, view, fov, NULL, false, context, current, out, submitted, error);
}
bool frontend_equipment_gear_held(frontend_equipment_gear_presenter *presenter,
    const application_equipment_gear_presentation *source, const q3n_selected_weapon_held *held, bool reduced_flashes,
    void *context, bool (*current)(void *), frontend_equipment_output **out, bool *submitted, qa_error *error)
{
    if (!held) return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear held has no actual torso");
    return draw(presenter, source, NULL, 0, held, reduced_flashes, context, current, out, submitted, error);
}
static bool base_group(void *context,size_t *count,qa_error *error)
{
    equipment_gear_output *output=context;
    q3n_selected_weapon_media media;
    if (!q3n_selected_authored_read(output->presenter->owner->view.media,false,&media,error)) return false;
    if (!output->draw.count || output->draw.refs[0].model!=media.gun || output->draw.refs[0].custom_shader)
        return frontend_fail(error,QA_ERROR_FORMAT,"Gear source style has no genuine base gun");
    *count=output->draw.count;
    if (*count>1 && media.flash && output->draw.refs[*count-1].model==media.flash &&
        !output->draw.refs[*count-1].custom_shader) --*count;
    return true;
}
