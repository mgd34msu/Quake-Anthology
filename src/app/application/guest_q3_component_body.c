#include "guest_q3_component_body_private.h"
#include "internal.h"
#include "qa/binary.h"
#include "qa/q3_assets_save.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void output_free(component_body_output *output)
{
    for (size_t i = 0; i < output->count; ++i) free((void *)output->parts[i].passes);
    free(output->parts); *output = (component_body_output){0};
}
static void outputs_clear(application_q3_component_body *owner)
{
    for (size_t i = 0; i < owner->count; ++i) output_free(owner->outputs + i);
    free(owner->outputs); owner->outputs = NULL; owner->count = 0;
}
static bool source_current(const application_q3_component_body *owner)
{
    return owner && owner->options.source.current(owner->options.source.context,
        owner->sequence, owner->time_ms);
}
static bool source_actor(application_q3_component_body *owner, uint32_t physical,
    qa_actor_id *actor, bool *owned, bool *found, qa_error *error)
{
    *actor = (qa_actor_id){0}; *owned = *found = false;
    if (!source_current(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body lost its reached scene");
    owner->busy = true;
    bool okay = owner->options.source.actor(owner->options.source.context, physical, actor, owned, found, error);
    owner->busy = false;
    return okay && (source_current(owner) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Component body scene changed during actor admission"));
}
static bool actor_current(application_q3_component_body *owner, const component_player_scope *player,
    bool *live, qa_error *error)
{
    qa_actor_id actor; bool owned, found;
    *live = false;
    if (!source_actor(owner, player->physical, &actor, &owned, &found, error)) return false;
    *live = found && !owned && qa_actor_id_equal(actor, player->output.actor) &&
        owner->options.source.live(owner->options.source.context, actor);
    return source_current(owner) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Component body scene changed during liveness admission");
}
static bool part_append(component_body_output *output, qa_application_q3_body_part part,
    uint32_t helper, size_t *index, qa_error *error)
{
    if (output->count == SIZE_MAX / sizeof(*output->parts))
        return application_fail(error, QA_ERROR_MEMORY, "Component body helper extent is exhausted");
    qa_application_q3_component_part *parts = realloc(output->parts,
        (output->count + 1) * sizeof(*parts));
    if (!parts) return application_fail(error, QA_ERROR_MEMORY, "Retaining component body helper");
    output->parts = parts; *index = output->count++;
    parts[*index] = (qa_application_q3_component_part){.part = part, .helper = helper};
    return true;
}
static bool player_hook(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_component_body *owner = context;
    if (owner->busy)
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body reentered actor admission");
    if (!owner->entered) return qa_qvm_proceed(call, result, error);
    const application_q3_body_submission *row = owner->options.profile->submissions;
    int32_t pointer; qa_bytes number;
    if (!qa_qvm_call_argument(call, row->actor_argument, &pointer, error)) return false;
    int64_t state_pointer = (int64_t)pointer + (int64_t)row->entity_number_offset;
    if (state_pointer < INT32_MIN || state_pointer > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Component entity-state pointer exceeds its source word");
    if (!qa_qvm_span(call->vm, (int32_t)state_pointer, 0, 4, &number, error)) return false;
    int32_t physical = qa_load_i32le(number.data);
    component_player_scope scope = {.previous = owner->player,
        .state = state_pointer};
    bool owned = false, found = false;
    if (physical >= 0) {
        scope.physical = (uint32_t)physical;
        if (!source_actor(owner, scope.physical, &scope.output.actor, &owned, &found, error)) return false;
        scope.admitted = found && !owned && scope.output.actor.registry &&
            owner->options.source.live(owner->options.source.context, scope.output.actor);
    }
    if (!source_current(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body lost its reached player scene");
    if (scope.admitted) {
        scope.pending = calloc(row->call_count, sizeof(*scope.pending));
        if (!scope.pending) return application_fail(error, QA_ERROR_MEMORY, "Retaining pending original body helpers");
        for (size_t i = 0; i < row->call_count; ++i) {
            size_t index;
            if (!part_append(&scope.output, row->calls[i].part, row->calls[i].instruction, &index, error)) {
                free(scope.pending); output_free(&scope.output); return false;
            }
            scope.pending[i] = true;
        }
    }
    owner->player = &scope;
    bool okay = qa_qvm_proceed(call, result, error);
    bool ordered = owner->player == &scope;
    owner->player = scope.previous;
    /* The donor publishes at scope exit only while that exact actor lives.
     * Failed component execution invalidates the enclosing completed receipt. */
    bool live = false;
    if (okay && ordered && scope.admitted) okay = actor_current(owner, &scope, &live, error);
    if (okay && live) {
        if (owner->count == SIZE_MAX / sizeof(*owner->outputs))
            okay = application_fail(error, QA_ERROR_MEMORY, "Component body actor extent is exhausted");
        else {
            component_body_output *outputs = realloc(owner->outputs, (owner->count + 1) * sizeof(*outputs));
            if (!outputs) okay = application_fail(error, QA_ERROR_MEMORY, "Retaining completed component body");
            else { owner->outputs = outputs; outputs[owner->count++] = scope.output; scope.output = (component_body_output){0}; }
        }
    }
    free(scope.pending); output_free(&scope.output);
    if (!ordered) return application_fail(error, QA_ERROR_ARGUMENT, "Component player scopes unwound out of order");
    return okay;
}
static bool mesh_hook(void *context, const qa_qvm_call *call, int32_t *result, qa_error *error)
{
    application_q3_component_body *owner = context;
    if (owner->busy)
        return application_fail(error, QA_ERROR_ARGUMENT, "Component mesh reentered actor admission");
    const application_q3_body_submission *row = owner->options.profile->submissions;
    component_player_scope *player = owner->player;
    component_mesh_scope scope = {.previous = owner->mesh};
    const application_q3_body_call *part = NULL; size_t part_index = 0;
    for (size_t i = 0; i < row->call_count; ++i)
        if (row->calls[i].instruction == call->caller_instruction) { part = row->calls + i; part_index = i; break; }
    int32_t state;
    if (!qa_qvm_call_argument(call, row->mesh_state_argument, &state, error)) return false;
    if (player && player->admitted && part && (int64_t)state == player->state) {
        scope.player = player;
        if (!qa_qvm_call_argument(call, row->mesh_entity_argument, &scope.pointer, error)) return false;
        int64_t shader_pointer = (int64_t)scope.pointer + row->mesh_shader_offset;
        if (shader_pointer < INT32_MIN || shader_pointer > INT32_MAX)
            return application_fail(error, QA_ERROR_FORMAT, "Component body shader pointer exceeds its source word");
        qa_bytes shader;
        if (!qa_qvm_span(call->vm, (int32_t)shader_pointer, 0, 4, &shader, error)) return false;
        scope.shader = qa_load_i32le(shader.data);
        if (player->pending[part_index]) { scope.part = part_index; player->pending[part_index] = false; }
        else if (!part_append(&player->output, part->part, part->instruction, &scope.part, error)) return false;
    }
    owner->mesh = &scope;
    bool okay = qa_qvm_proceed(call, result, error);
    bool ordered = owner->mesh == &scope; owner->mesh = scope.previous;
    return ordered ? okay : application_fail(error, QA_ERROR_ARGUMENT, "Component mesh scopes unwound out of order");
}

bool application_q3_component_body_source_entity(void *context, const qa_qvm_call *call,
    int32_t pointer, const qa_q3_ref_entity *entity, bool *suppress, qa_error *error)
{
    application_q3_component_body *owner = context;
    if (!owner || !call || call->vm != owner->options.vm || !entity || !suppress || owner->busy)
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body trap lost its original executor");
    *suppress = false;
    component_mesh_scope *mesh = owner->mesh;
    if (!mesh || !mesh->player || mesh->player != owner->player || pointer != mesh->pointer ||
        entity->kind != QA_Q3_REF_MODEL) return true;
    bool cancelled, live;
    if (!qa_qvm_call_cancelled(call, &cancelled, error)) return false;
    if (cancelled) return true;
    if (!actor_current(owner, mesh->player, &live, error)) return false;
    if (!live) return true;
    qa_application_q3_component_part *part = mesh->player->output.parts + mesh->part;
    if (entity->custom_shader == mesh->shader) part->base = true;
    else {
        if (part->count == SIZE_MAX / sizeof(*part->passes))
            return application_fail(error, QA_ERROR_MEMORY, "Component body pass extent is exhausted");
        qa_q3_ref_entity *passes = realloc((void *)part->passes, (part->count + 1) * sizeof(*passes));
        if (!passes) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual component material pass");
        part->passes = passes; passes[part->count++] = *entity;
    }
    *suppress = true; return true;
}
bool application_q3_component_body_idle(const application_q3_component_body *owner)
{ return !owner || (!owner->entered && !owner->player && !owner->mesh && !owner->busy && !owner->leases); }
bool application_q3_component_body_create(const application_q3_component_body_options *options,
    application_q3_component_body **out, qa_error *error)
{
    if (!options || !options->vm || !options->image || !options->profile || !options->owner ||
        !options->assets || !options->source.context || !options->source.actor || !options->source.live ||
        !options->source.current || !out || *out || qa_qvm_get_role(options->vm) != QA_QVM_CGAME ||
        (qa_qvm_image_of(options->vm) != options->image) ||
        !application_q3_body_profile_qualify(options->image, qa_qvm_get_abi(options->vm),
            options->profile->artifact_path, options->profile, error) ||
        !options->profile->present || options->profile->count != 1 ||
        !options->profile->submissions[0].mesh || options->profile->submissions[0].conditional ||
        options->profile->submissions[0].argument_reference || !qa_qvm_can_destroy(options->vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body requires its actual scene declaration and owners");
    application_q3_component_body *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "Retaining component body source owner");
    owner->options = *options; *out = owner;
    const application_q3_body_submission *row = options->profile->submissions;
    return qa_qvm_bind_function(options->vm, row->entry, true, player_hook, owner, &owner->bindings[0], error) &&
        qa_qvm_bind_function(options->vm, row->mesh_entry, true, mesh_hook, owner, &owner->bindings[1], error);
}
bool application_q3_component_body_destroy(application_q3_component_body *owner, qa_error *error)
{
    if (!owner) return true;
    if (!application_q3_component_body_idle(owner) || !qa_qvm_can_destroy(owner->options.vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body destruction requires returned source calls");
    for (size_t i = 2; i; --i) if (owner->bindings[i - 1]) {
        if (!qa_qvm_unbind(owner->options.vm, owner->bindings[i - 1], error)) return false;
        owner->bindings[i - 1] = 0;
    }
    outputs_clear(owner); free(owner); return true;
}
bool application_q3_component_body_begin(application_q3_component_body *owner,
    uint64_t sequence, int32_t time_ms, qa_error *error)
{
    if (!owner || !application_q3_component_body_idle(owner) || !owner->bindings[0] || !owner->bindings[1] ||
        !qa_qvm_can_destroy(owner->options.vm) || sequence > UINT64_C(9007199254740991) ||
        owner->generation == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body frame needs its completed original owner");
    if (!owner->options.source.current(owner->options.source.context, sequence, time_ms))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body frame has no reached source context");
    outputs_clear(owner); owner->sequence = sequence; owner->time_ms = time_ms; ++owner->generation;
    owner->completed = false; owner->entered = true; return true;
}
bool application_q3_component_body_end(application_q3_component_body *owner, bool success, qa_error *error)
{
    if (!owner || !owner->entered || owner->player || owner->mesh || owner->busy ||
        !qa_qvm_can_destroy(owner->options.vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body frame retains an entered source helper");
    owner->entered = false;
    owner->completed = success && source_current(owner);
    if (!owner->completed) outputs_clear(owner);
    return !success || owner->completed ||
        application_fail(error, QA_ERROR_ARGUMENT, "Component body completion lost its reached source scene");
}
bool qa_application_q3_component_bodies_read(const application_q3_component_body *owner,
    qa_application_q3_component_bodies *out)
{
    if (!out || !owner || !owner->completed || owner->entered || owner->player || owner->mesh ||
        owner->busy || !source_current(owner)) return false;
    *out = (qa_application_q3_component_bodies){.producer = owner, .owner = owner->options.owner,
        .assets = owner->options.assets, .sequence = owner->sequence, .generation = owner->generation,
        .time_ms = owner->time_ms, .count = owner->count}; return true;
}
bool qa_application_q3_component_bodies_current(const qa_application_q3_component_bodies *view)
{
    qa_application_q3_component_bodies actual;
    return view && qa_application_q3_component_bodies_read(view->producer, &actual) &&
        view->owner == actual.owner && view->assets == actual.assets && view->sequence == actual.sequence &&
        view->generation == actual.generation &&
        view->time_ms == actual.time_ms && view->count == actual.count;
}
bool qa_application_q3_component_body_at(const qa_application_q3_component_bodies *view,
    size_t index, qa_application_q3_component_actor *out)
{
    if (!out || !qa_application_q3_component_bodies_current(view) || index >= view->count) return false;
    const component_body_output *output = view->producer->outputs + index;
    *out = (qa_application_q3_component_actor){output->actor, output->parts, output->count}; return true;
}
bool qa_application_q3_component_bodies_borrow(application_q3_component_body *owner,
    qa_application_q3_component_body_lease **out, qa_application_q3_component_bodies *view, qa_error *error)
{
    qa_application_q3_component_bodies actual;
    if (!out || *out || !view || !qa_application_q3_component_bodies_read(owner, &actual))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body borrow needs its actual completed source output");
    qa_application_q3_component_body_lease *lease = calloc(1, sizeof(*lease));
    if (!lease) return application_fail(error, QA_ERROR_MEMORY, "Retaining component body Draw lease");
    lease->owner = owner; lease->next = owner->leases; owner->leases = lease;
    *out = lease; *view = actual; return true;
}
void qa_application_q3_component_bodies_return(qa_application_q3_component_body_lease **slot)
{
    if (!slot || !*slot) return;
    qa_application_q3_component_body_lease *lease = *slot;
    qa_application_q3_component_body_lease **link = &lease->owner->leases;
    while (*link && *link != lease) link = &(*link)->next;
    if (*link != lease) return;
    *link = lease->next; *slot = NULL; free(lease);
}
bool application_q3_component_body_descriptors(const application_q3_component_body *owner,
    qa_qvm_saved_function out[2], qa_error *error)
{
    if (!owner || !out || !application_q3_component_body_idle(owner) || !owner->bindings[0] || !owner->bindings[1])
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body descriptors need their complete idle owner");
    const application_q3_body_submission *row = owner->options.profile->submissions;
    out[0] = (qa_qvm_saved_function){owner->bindings[0], row->entry, true, player_hook, (void *)owner};
    out[1] = (qa_qvm_saved_function){owner->bindings[1], row->mesh_entry, true, mesh_hook, (void *)owner};
    return true;
}
void application_q3_component_body_adopt(application_q3_component_body *owner, const qa_qvm_binding bindings[2])
{ owner->bindings[0] = bindings[0]; owner->bindings[1] = bindings[1]; }
