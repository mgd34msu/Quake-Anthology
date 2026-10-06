#include "guest_q3_body_private.h"
#include "internal.h"
#include "qa/binary.h"
#include "qa/q3_presentation.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool current(application_q3_body *owner, qa_error *error)
{
    return (owner->drawing && owner->services.current(owner->services.context, &owner->draw)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "CGAME body lost its actual entered Draw lease");
}
static bool physical_actor(application_q3_body *owner, uint32_t physical,
    qa_actor_id *actor, bool *present, qa_error *error)
{
    owner->busy = true;
    bool okay = owner->module.client.source_actor(owner->module.client.context, physical, actor, present, error);
    owner->busy = false;
    return okay;
}
static bool selection(application_q3_body *owner, qa_actor_id actor,
    bool *hidden, bool *selected, qa_error *error)
{
    owner->busy = true;
    bool okay = owner->services.actor(owner->services.context, &owner->draw, actor, hidden, selected, error);
    owner->busy = false;
    return okay;
}
static bool actor_current(application_q3_body *owner, uint32_t physical,
    qa_actor_id actor, qa_error *error)
{
    qa_actor_id actual = {0}; bool present = false;
    return current(owner, error) &&
        physical_actor(owner, physical, &actual, &present, error) &&
        current(owner, error) && ((present && qa_actor_id_equal(actual, actor)) ||
            application_fail(error, QA_ERROR_ARGUMENT, "CGAME body source actor generation changed"));
}
static bool resolve(application_q3_body *owner, uint32_t physical, qa_actor_id *actor,
    bool *present, bool *hidden, bool *selected, qa_error *error)
{
    *present = *hidden = *selected = false; *actor = (qa_actor_id){0};
    if (!current(owner, error) ||
        !physical_actor(owner, physical, actor, present, error) ||
        !current(owner, error)) return false;
    if (!*present) return true;
    if (!actor->registry || !selection(owner, *actor, hidden, selected, error) ||
        !actor_current(owner, physical, *actor, error)) return false;
    if ((!owner->draw.hide_active && *hidden) || (!owner->draw.capture_active && *selected))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body selection leaves its prepared Draw admission");
    return true;
}

static bool submission(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    body_hook *hook = context;
    application_q3_body *owner = hook->owner;
    const application_q3_body_submission *row = hook->declaration;
    if (owner->busy)
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body callback reentered its frontend admission");
    if (!owner->drawing) return qa_qvm_proceed(call, result, error);
    if (!current(owner, error)) return false;
    if (row->conditional) {
        int32_t condition;
        if (!qa_qvm_call_argument(call, row->condition_argument, &condition, error)) return false;
        if ((int64_t)condition != row->condition_value) return qa_qvm_proceed(call, result, error);
    }
    int32_t pointer;
    qa_bytes number;
    if (!qa_qvm_call_argument(call, row->actor_argument, &pointer, error) ||
        !qa_qvm_span(call->vm, pointer, (int64_t)row->entity_number_offset, 4, &number, error)) return false;
    int32_t physical = qa_load_i32le(number.data);
    /* Unbound authored physical entities have no host actor receipt. */
    if (physical < 0) return qa_qvm_proceed(call, result, error);
    qa_actor_id actor; bool present, hidden, selected;
    if (!resolve(owner, (uint32_t)physical, &actor, &present, &hidden, &selected, error)) return false;
    if (!present || (!hidden && !selected) || (selected && !row->mesh && !hidden))
        return qa_qvm_proceed(call, result, error);
    body_range range = {.previous = owner->ranges, .declaration = row,
        .physical = (uint32_t)physical, .actor = actor, .hidden = hidden,
        .state = (int64_t)pointer + (int64_t)row->entity_number_offset};
    if (row->argument_reference) {
        int32_t reference; qa_bytes bytes;
        if (!qa_qvm_call_argument(call, row->reference_argument, &reference, error) ||
            !qa_qvm_span(call->vm, reference, 0, 140, &bytes, error)) return false;
        range.start = qa_qvm_mask_address(call->vm, reference); range.end = range.start + 140;
    } else {
        qa_qvm_source_frame frame;
        if (!qa_qvm_call_source_frame(call, owner->module.image, &frame, error)) return false;
        range.start = frame.start; range.end = frame.end;
    }
    owner->ranges = &range;
    bool okay = qa_qvm_proceed(call, result, error);
    bool ordered = owner->ranges == &range;
    owner->ranges = range.previous;
    if (!ordered) return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body scopes unwound out of order");
    return okay;
}

static bool mesh(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    body_hook *hook = context;
    application_q3_body *owner = hook->owner;
    if (owner->busy)
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body mesh reentered its frontend admission");
    body_range *range = owner->ranges;
    if (!range || range->declaration->mesh_entry != hook->instruction)
        return qa_qvm_proceed(call, result, error);
    const application_q3_body_submission *row = range->declaration;
    const application_q3_body_call *part = NULL;
    for (size_t i = 0; i < row->call_count; ++i)
        if (row->calls[i].instruction == call->caller_instruction) { part = row->calls + i; break; }
    if (!part) return qa_qvm_proceed(call, result, error);
    int32_t state, pointer;
    /* One real mesh hook is retained per entry, as in the source donor. */
    const application_q3_body_submission *binding = hook->declaration;
    if (!qa_qvm_call_argument(call, binding->mesh_state_argument, &state, error)) return false;
    if ((int64_t)state != range->state) return qa_qvm_proceed(call, result, error);
    if (!actor_current(owner, range->physical, range->actor, error) ||
        !qa_qvm_call_argument(call, binding->mesh_entity_argument, &pointer, error)) return false;
    int64_t shader_pointer = (int64_t)pointer + binding->mesh_shader_offset;
    if (shader_pointer < INT32_MIN || shader_pointer > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "CGAME body shader pointer exceeds its source word");
    qa_bytes shader;
    if (!qa_qvm_span(call->vm, (int32_t)shader_pointer, 0, 4, &shader, error)) return false;
    body_mesh active = {.previous = owner->meshes, .range = range, .pointer = pointer,
        .shader = qa_load_i32le(shader.data), .part = part->part, .helper = part->instruction};
    owner->meshes = &active;
    bool okay = qa_qvm_proceed(call, result, error);
    bool ordered = owner->meshes == &active;
    owner->meshes = active.previous;
    if (!ordered) return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body mesh scopes unwound out of order");
    return okay;
}

bool application_q3_body_source_entity(void *context, const qa_qvm_call *call,
    int32_t pointer, const qa_q3_ref_entity *entity, bool *suppress, qa_error *error)
{
    application_q3_body *owner = context;
    if (!owner || !call || !entity || !suppress || call->vm != owner->module.vm || owner->busy)
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body submission requires its actual executor");
    *suppress = false;
    if (!owner->ranges) return true;
    bool cancelled;
    if (!qa_qvm_call_cancelled(call, &cancelled, error)) return false;
    if (cancelled) return true;
    qa_bytes bytes;
    if (!qa_qvm_span(call->vm, pointer, 0, 140, &bytes, error)) return false;
    uint32_t start = qa_qvm_mask_address(call->vm, pointer);
    body_range *range = owner->ranges;
    while (range && (start < range->start || (uint64_t)start + 140 > range->end)) range = range->previous;
    if (!range) return true;
    if (!actor_current(owner, range->physical, range->actor, error)) return false;
    if (range->hidden) { *suppress = true; return true; }
    body_mesh *active = owner->meshes;
    if (!active || active->range != range || active->pointer != pointer || entity->kind != QA_Q3_REF_MODEL) return true;
    bool hidden = false, selected = false;
    if (!selection(owner, range->actor, &hidden, &selected, error) ||
        !actor_current(owner, range->physical, range->actor, error)) return false;
    if (!selected) return true;
    if (!owner->draw.capture_active)
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body capture leaves its prepared Draw admission");
    bool handled = false;
    owner->busy = true;
    bool okay = owner->services.submit(owner->services.context, &owner->draw, range->actor,
        active->part, active->helper, entity, entity->custom_shader == active->shader, &handled, error);
    owner->busy = false;
    if (okay) okay = actor_current(owner, range->physical, range->actor, error);
    if (okay) *suppress = handled;
    return okay;
}

size_t application_q3_body_profile_hooks(const application_q3_body_profile *profile)
{
    if (!profile || !profile->present) return 0;
    size_t count = profile->count;
    for (size_t i = 0; i < profile->count; ++i) {
        const application_q3_body_submission *row = profile->submissions + i;
        if (!row->mesh) continue;
        size_t j = 0;
        while (j < i && (!profile->submissions[j].mesh || profile->submissions[j].mesh_entry != row->mesh_entry)) ++j;
        if (j == i) ++count;
    }
    return count;
}
bool application_q3_body_bindings_complete(const application_q3_body *owner)
{
    if (!owner) return true;
    for (size_t i = 0; i < owner->hook_count; ++i)
        if ((owner->hooks[i].binding != 0) != owner->enabled) return false;
    return !owner->enabled || owner->module.profile->present;
}
static bool enable(application_q3_body *owner, bool active, qa_error *error)
{
    if (owner->ranges || owner->meshes || owner->busy || !qa_qvm_can_destroy(owner->module.vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body bindings require a completed source call");
    if (active && owner->enabled) return true;
    if (!active) {
        for (size_t i = owner->hook_count; i; --i) {
            body_hook *hook = owner->hooks + i - 1;
            if (hook->binding && !qa_qvm_unbind(owner->module.vm, hook->binding, error)) return false;
            hook->binding = 0;
        }
        owner->enabled = false; return true;
    }
    if (!application_q3_body_bindings_complete(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body retains a partial failed binding set");
    for (size_t i = 0; i < owner->hook_count; ++i) {
        body_hook *hook = owner->hooks + i;
        if (!qa_qvm_bind_function(owner->module.vm, hook->instruction, false,
            hook->function, hook, &hook->binding, error)) {
            qa_error first = error ? *error : (qa_error){0};
            (void)enable(owner, false, NULL);
            if (error) *error = first;
            return false;
        }
    }
    owner->enabled = true; return true;
}

bool application_q3_body_idle(const application_q3_body *owner)
{ return !owner || (!owner->drawing && !owner->ranges && !owner->meshes && !owner->busy); }
bool application_q3_body_executor(const application_q3_body *owner, const qa_session *session, const qa_qvm *vm)
{ return !owner || (owner->module.session == session && owner->module.vm == vm); }
bool application_q3_body_destroy(application_q3_body *owner, qa_error *error)
{
    if (!owner) return true;
    if (!application_q3_body_idle(owner) || !enable(owner, false, error)) return false;
    free(owner->hooks); free(owner); return true;
}
bool application_q3_body_create_module(const application_q3_body_module *module,
    const qa_application_q3_body_services *services, const application_q3_body_saved *saved,
    application_q3_body **out, qa_error *error)
{
    if (!module || !module->session || !module->vm || !module->image || !module->profile || !module->receiver ||
        !out || *out || qa_qvm_get_role(module->vm) != QA_QVM_CGAME ||
        (qa_qvm_image_of(module->vm) != module->image) ||
        !application_q3_body_profile_qualify(module->image, qa_qvm_get_abi(module->vm),
            module->profile->artifact_path, module->profile, error) ||
        (module->profile->present && (!module->client.context || !module->client.source_actor)) ||
        !services || !services->context || !services->prepare || !services->current || !services->actor ||
        !services->submit || !services->release_draw || !qa_qvm_can_destroy(module->vm) ||
        (saved && (!saved->present || (saved->enabled && !module->profile->present))))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body constructor requires its real source owners");
    size_t count = application_q3_body_profile_hooks(module->profile);
    if (count > SIZE_MAX / sizeof(body_hook) || (saved &&
        (saved->count != (saved->enabled ? count : 0) ||
            ((saved->count != 0) != (saved->bindings != NULL)))))
        return application_fail(error, QA_ERROR_FORMAT, "CGAME body continuation has a different callback inventory");
    for (size_t i = 0; saved && i < saved->count; ++i) {
        if (!saved->bindings[i])
            return application_fail(error, QA_ERROR_FORMAT, "CGAME body continuation has an absent callback identity");
        for (size_t j = 0; j < i; ++j) if (saved->bindings[i] == saved->bindings[j])
            return application_fail(error, QA_ERROR_FORMAT, "CGAME body continuation repeats a callback identity");
    }
    application_q3_body *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Retaining original CGAME body owner");
    owner->module = *module; owner->services = *services; *out = owner;
    owner->hooks = count ? calloc(count, sizeof(*owner->hooks)) : NULL;
    if (count && !owner->hooks) return application_fail(error, QA_ERROR_MEMORY, "Retaining body callback inventory");
    owner->hook_count = count;
    size_t at = 0;
    for (size_t i = 0; i < module->profile->count; ++i) {
        const application_q3_body_submission *row = module->profile->submissions + i;
        owner->hooks[at++] = (body_hook){.owner = owner, .declaration = row,
            .instruction = row->entry, .function = submission};
        if (!row->mesh) continue;
        size_t j = 0;
        while (j < i && (!module->profile->submissions[j].mesh ||
            module->profile->submissions[j].mesh_entry != row->mesh_entry)) ++j;
        if (j == i) owner->hooks[at++] = (body_hook){.owner = owner, .declaration = row,
            .instruction = row->mesh_entry, .function = mesh};
    }
    return !saved || !saved->enabled || enable(owner, true, error);
}

bool application_q3_body_draw_begin(application_q3_body *owner, qa_error *error)
{
    if (!owner || !application_q3_body_idle(owner) || !application_q3_body_bindings_complete(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "CGAME body Draw requires its idle complete owner");
    qa_application_q3_body_draw draw = {0};
    owner->busy = true;
    bool okay = owner->services.prepare(owner->services.context, owner->module.receiver, owner->module.seat, &draw, error);
    owner->busy = false;
    if (okay && (!draw.token || !owner->services.current(owner->services.context, &draw)))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "CGAME body Draw lost its retained source token");
    bool active = draw.hide_active || draw.capture_active;
    if (okay && active && !owner->module.profile->present)
        okay = application_fail(error, QA_ERROR_FORMAT, "Body replacements require artifact-matched cgame-presentation.json");
    bool meshes = false;
    for (size_t i = 0; i < owner->module.profile->count; ++i) meshes |= owner->module.profile->submissions[i].mesh;
    if (okay && draw.capture_active && !meshes)
        okay = application_fail(error, QA_ERROR_FORMAT, "Body materials require original player-to-mesh call sites");
    if (okay) okay = enable(owner, active, error);
    if (!okay) {
        if (draw.token) {
            owner->busy = true;
            owner->services.release_draw(owner->services.context, &draw);
            owner->busy = false;
        }
        return false;
    }
    owner->draw = draw; owner->drawing = true; return true;
}
void application_q3_body_draw_end(application_q3_body *owner)
{
    if (!owner || !owner->drawing || owner->ranges || owner->meshes || owner->busy) return;
    owner->drawing = false; owner->busy = true;
    owner->services.release_draw(owner->services.context, &owner->draw);
    owner->draw = (qa_application_q3_body_draw){0}; owner->busy = false;
}
size_t application_q3_body_descriptor_count(const application_q3_body *owner)
{ return owner && owner->enabled ? owner->hook_count : 0; }
bool application_q3_body_descriptors(const application_q3_body *owner,
    qa_qvm_saved_function *out, size_t count, qa_error *error)
{
    if (!application_q3_body_idle(owner) || !application_q3_body_bindings_complete(owner) ||
        count != application_q3_body_descriptor_count(owner) || (count && !out))
        return application_fail(error, QA_ERROR_ARGUMENT, "Body descriptors require their complete idle source inventory");
    for (size_t i = 0; i < count; ++i) {
        const body_hook *hook = owner->hooks + i;
        out[i] = (qa_qvm_saved_function){hook->binding, hook->instruction, false, hook->function, (void *)hook};
    }
    return true;
}
void application_q3_body_adopt(application_q3_body *owner, const qa_qvm_binding *bindings)
{
    for (size_t i = 0; owner && owner->enabled && i < owner->hook_count; ++i) owner->hooks[i].binding = bindings[i];
}
