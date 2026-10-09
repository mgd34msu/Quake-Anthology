#include "qa/input.h"
#include "guest_qc_profile.h"
#include "guest_qc_objectives.h"
#include <float.h>

typedef struct saved_input saved_input;
struct application_qc_input_scope {
    struct application_qc_input_scope *previous;
    qa_actor_id actor;
    int32_t reference;
    const application_qc_input_binding *binding;
    bool *entered;
    saved_input *saved;
    size_t saved_count;
    bool nested, movement_slice;
};
struct saved_input {
    const application_qc_bound_field *field;
    uint32_t words[3];
    bool valid;
};
struct application_qc_parked_input {
    struct application_qc_parked_input *next;
    struct application_qc_input_scope *head, *tail, *parent;
};
float application_qc_input_scalar(const qa_movement_command *command, application_qc_input_id input)
{
    switch (input) {
    case QC_INPUT_ATTACK: return (command->buttons & 1u) ? 1.0f : 0.0f;
    case QC_INPUT_JUMP: return command->kind == QA_MOVEMENT_NETQUAKE || command->kind == QA_MOVEMENT_QUAKEWORLD ?
        (command->buttons & 2u) ? 1.0f : 0.0f : command->kind == QA_MOVEMENT_Q2_RERELEASE ?
        (command->buttons & 8u) ? 1.0f : 0.0f : command->up_move >= 10 ? 1.0f : 0.0f;
    case QC_INPUT_IMPULSE: return command->impulse;
    case QC_INPUT_FORWARD: return command->forward_move / qa_input_command_units(command->kind);
    case QC_INPUT_SIDE: return command->side_move / qa_input_command_units(command->kind);
    case QC_INPUT_UP: return command->kind == QA_MOVEMENT_Q2_RERELEASE ?
        (command->buttons & 8u) ? 1 : (command->buttons & 16u) ? -1 : 0 : command->up_move / qa_input_command_units(command->kind);
    default: return 0;
    }
}
static bool set_input(qa_movement_command *command, application_qc_input_id input, float value, qa_error *error)
{
    if (!isfinite(value)) return application_fail(error, QA_ERROR_FORMAT, "QC input output is nonfinite");
    switch (input) {
    case QC_INPUT_ATTACK: command->buttons = value != 0 ? command->buttons | 1u : command->buttons & ~1u; break;
    case QC_INPUT_JUMP:
        if (command->kind == QA_MOVEMENT_NETQUAKE || command->kind == QA_MOVEMENT_QUAKEWORLD)
            command->buttons = value != 0 ? command->buttons | 2u : command->buttons & ~2u;
        else if (command->kind == QA_MOVEMENT_Q2_RERELEASE)
            command->buttons = value != 0 ? command->buttons | 8u : command->buttons & ~8u;
        else if (value == 0) { if (command->up_move > 0) command->up_move = 0; }
        else command->up_move = fmaxf(command->up_move, qa_input_command_units(command->kind));
        break;
    case QC_INPUT_IMPULSE: {
        if (value < 0 || value > 255 || truncf(value) != value)
            return application_fail(error, QA_ERROR_FORMAT, "QC input impulse output does not fit one byte");
        command->impulse = (uint8_t)value; break;
    }
    case QC_INPUT_FORWARD: case QC_INPUT_SIDE: case QC_INPUT_UP: {
        if (input == QC_INPUT_UP && command->kind == QA_MOVEMENT_Q2_RERELEASE) {
            command->buttons = (command->buttons & ~24u) | (value > 0 ? 8u : value < 0 ? 16u : 0u);
            break;
        }
        double scaled = (double)value * qa_input_command_units(command->kind);
        if (command->kind == QA_MOVEMENT_Q2_RERELEASE) {
            if (fabs(scaled) > FLT_MAX) return application_fail(error, QA_ERROR_FORMAT, "QC movement output exceeds command range");
        } else {
            double maximum = command->kind == QA_MOVEMENT_Q3 ? 127 : 32767;
            double minimum = command->kind == QA_MOVEMENT_Q3 ? -127 : -32768;
            if (scaled < minimum || scaled > maximum)
                return application_fail(error, QA_ERROR_FORMAT, "QC movement output exceeds command range");
            scaled = trunc(scaled);
        }
        if (input == QC_INPUT_FORWARD) command->forward_move = (float)scaled;
        else if (input == QC_INPUT_SIDE) command->side_move = (float)scaled;
        else command->up_move = (float)scaled;
        break;
    }
    default: return application_fail(error, QA_ERROR_ARGUMENT, "QC scalar output names a nonscalar control");
    }
    return true;
}
bool application_qc_entered(void *opaque, qa_qc_instance *vm, const qa_qc_call_event *event, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (!application_qc_objectives_sync(engine, error)) return false;
    struct application_qc_input_scope *scope = engine->input_scope;
    if (!scope || !scope->binding) return true;
    const qa_qc_definition *self = qa_qc_program_find_global(engine->provider->state.qc.program, "self"); int32_t reference;
    if (!self || self->type != QA_QC_ENTITY || !qa_qc_global_int(vm, self->offset, &reference, error)) return false;
    if (reference != scope->reference) return true;
    for (size_t i = 0; i < scope->binding->output_count; ++i)
        if (scope->binding->outputs[i].function == event->function) scope->entered[i] = true;
    return true;
}
static bool read_words(qa_qc_instance *vm, int32_t reference, const qa_qc_definition *field,
                         uint32_t words[3], qa_error *error)
{
    uint32_t count = field->type == QA_QC_VECTOR ? 3u : 1u;
    for (uint32_t i = 0; i < count; ++i) {
        int32_t value;
        if (!qa_qc_entity_int(vm, reference, field->offset + i, &value, error)) return false;
        memcpy(&words[i], &value, sizeof(value));
    }
    return true;
}
static bool restore_input(qa_qc_instance *vm, int32_t reference, const saved_input *saved, qa_error *error)
{
    if (!saved->valid) return true;
    if (saved->field->definition->type == QA_QC_VECTOR) {
        float v[3]; memcpy(v, saved->words, sizeof(v));
        return qa_qc_project_entity_vector(vm, reference, saved->field->definition->offset, qa_v3(v[0], v[1], v[2]), error);
    }
    int32_t word; memcpy(&word, saved->words, sizeof(word));
    return qa_qc_project_entity_int(vm, reference, saved->field->definition->offset, word, error);
}
bool application_qc_input_idle(const application_provider *provider)
{
    const struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    return !engine || (!engine->input_scope && !engine->parked_inputs);
}
static bool dispose_scope(struct application_qc_state *engine, struct application_qc_input_scope *scope,
                            bool failed, qa_error *error)
{
    bool ok = true;
    if ((scope->nested || failed) && qa_actors_get(qa_session_actors(engine->services.session), scope->actor)) {
        qa_error restoration = {0};
        qa_actor_id current;
        ok = qa_qc_reference_actor(engine->provider->state.qc.instance, scope->reference, &current, &restoration) &&
            qa_actor_id_equal(current, scope->actor);
        if (!ok) {
            if (restoration.code == QA_OK) application_fail(&restoration, QA_ERROR_NOT_FOUND, "QC input actor changed generation");
            if (error) *error = restoration;
        }
        bool current_actor = ok;
        for (size_t i = 0; current_actor && i < scope->saved_count; ++i)
            if (!restore_input(engine->provider->state.qc.instance, scope->reference, &scope->saved[i], &restoration)) {
                if (error) *error = restoration;
                ok = false;
            }
    }
    free(scope->saved); free(scope);
    return ok;
}
static bool close_scope(struct application_qc_state *engine, bool failed, qa_error *error)
{
    struct application_qc_input_scope *scope = engine->input_scope;
    for (struct application_qc_parked_input *parked = engine->parked_inputs; parked; parked = parked->next)
        if (parked->parent == scope)
            return application_fail(error, QA_ERROR_ARGUMENT, "QC input scope still owns a parked child");
    engine->input_scope = scope->previous;
    return dispose_scope(engine, scope, failed, error);
}
bool application_qc_input_park(application_provider *provider, qa_actor_id actor,
                                 struct application_qc_parked_input **out, qa_error *error)
{
    struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    if (!engine || !out) return application_fail(error, QA_ERROR_ARGUMENT, "QC input parking owner is absent");
    *out = NULL;
    const struct application_qc_profile *profile = provider->state.qc.qualified;
    bool command = false, slice = false;
    for (size_t i = 0; profile && i < profile->input_count; ++i) {
        if (profile->input[i].movement_slice) slice = true;
        else command = true;
    }
    if (!command && !slice) return true;
    struct application_qc_input_scope *head = engine->input_scope, *tail = head;
    if (!head || !qa_actor_id_equal(head->actor, actor) || head->movement_slice != slice ||
        head->binding || head->entered)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC input parking has no completed client scope");
    if (command && slice) {
        tail = head->previous;
        if (!tail || !qa_actor_id_equal(tail->actor, actor) || tail->movement_slice || tail->binding || tail->entered)
            return application_fail(error, QA_ERROR_ARGUMENT, "QC input parking has no completed command scope");
    }
    for (struct application_qc_input_scope *scope = head;; scope = scope->previous) {
        for (struct application_qc_parked_input *child = engine->parked_inputs; child; child = child->next)
            if (child->parent == scope)
                return application_fail(error, QA_ERROR_ARGUMENT, "QC input scope still owns a parked child");
        if (scope == tail) break;
    }
    struct application_qc_parked_input *parked = calloc(1, sizeof(*parked));
    if (!parked) return application_fail(error, QA_ERROR_MEMORY, "Allocating parked QC input ownership");
    parked->head = head; parked->tail = tail; parked->parent = tail->previous;
    parked->next = engine->parked_inputs; engine->parked_inputs = parked;
    tail->previous = NULL; engine->input_scope = parked->parent;
    *out = parked;
    return true;
}
static struct application_qc_parked_input **parked_owner(struct application_qc_state *engine,
                                                         struct application_qc_parked_input *parked)
{
    struct application_qc_parked_input **owner = &engine->parked_inputs;
    while (*owner && *owner != parked) owner = &(*owner)->next;
    return owner;
}
bool application_qc_input_resume(application_provider *provider, struct application_qc_parked_input *parked,
                                   qa_error *error)
{
    if (!parked) return true;
    struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    if (!engine) return application_fail(error, QA_ERROR_ARGUMENT, "Parked QC input owner is absent");
    struct application_qc_parked_input **owner = parked_owner(engine, parked);
    if (!*owner || engine->input_scope != parked->parent)
        return application_fail(error, QA_ERROR_ARGUMENT, "Parked QC input parent is no longer current");
    for (struct application_qc_input_scope *scope = parked->head; scope; scope = scope->previous) {
        qa_actor_id current;
        if (!qa_actors_get(qa_session_actors(engine->services.session), scope->actor) ||
            !qa_qc_reference_actor(provider->state.qc.instance, scope->reference, &current, error) ||
            !qa_actor_id_equal(current, scope->actor))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Parked QC input actor changed generation");
    }
    parked->tail->previous = parked->parent; engine->input_scope = parked->head;
    *owner = parked->next; free(parked);
    return true;
}
bool application_qc_input_parked_abort(application_provider *provider, struct application_qc_parked_input *parked,
                                         qa_error *error)
{
    if (!parked) return true;
    struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    if (!engine) return application_fail(error, QA_ERROR_ARGUMENT, "Parked QC input owner is absent");
    struct application_qc_parked_input **owner = parked_owner(engine, parked);
    if (!*owner) return application_fail(error, QA_ERROR_ARGUMENT, "Parked QC input handle is no longer owned");
    *owner = parked->next;
    bool ok = true;
    struct application_qc_input_scope *scope = parked->head;
    while (scope) {
        struct application_qc_input_scope *previous = scope->previous;
        if (!dispose_scope(engine, scope, true, error)) ok = false;
        scope = previous;
    }
    free(parked);
    return ok;
}
static bool close_through(struct application_qc_state *engine, struct application_qc_input_scope *scope,
                            bool failed, qa_error *error)
{
    bool found = false;
    for (struct application_qc_input_scope *entry = engine->input_scope; entry; entry = entry->previous) {
        if (entry->binding) return application_fail(error, QA_ERROR_ARGUMENT, "QC input callback is still executing");
        for (struct application_qc_parked_input *parked = engine->parked_inputs; parked; parked = parked->next)
            if (parked->parent == entry)
                return application_fail(error, QA_ERROR_ARGUMENT, "QC input scope still owns a parked child");
        if (entry == scope) { found = true; break; }
    }
    if (!found) return application_fail(error, QA_ERROR_ARGUMENT, "QC input scope is no longer active");
    bool ok = true;
    while (engine->input_scope != scope) if (!close_scope(engine, true, error)) ok = false;
    if (!close_scope(engine, failed, error)) ok = false;
    return ok;
}
bool application_qc_input_abort(application_provider *provider, qa_actor_id actor, bool movement_slice, qa_error *error)
{
    struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    if (!engine) return true;
    struct application_qc_input_scope *scope = engine->input_scope;
    while (scope && (!qa_actor_id_equal(scope->actor, actor) || scope->movement_slice != movement_slice)) scope = scope->previous;
    if (!scope) return true;
    return close_through(engine, scope, true, error);
}
bool application_qc_input(application_provider *provider, qa_actor_id actor, qa_movement_command *command,
                            bool before, bool movement_slice, uint64_t elapsed_ns, qa_error *error)
{
    struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    const struct application_qc_profile *profile = provider ? provider->state.qc.qualified : NULL;
    if (!engine || !command) return application_fail(error, QA_ERROR_ARGUMENT, "QC input application is absent");
    if (!profile) {
        if (!before) return true;
        qa_movement_command source = *command;
        bool jump = application_qc_input_scalar(command, QC_INPUT_JUMP) != 0;
        source.buttons = (command->buttons & 1u) | (jump ? 2u : 0u);
        if (command->kind == QA_MOVEMENT_Q3 || command->kind == QA_MOVEMENT_Q2_CLASSIC) {
            source.angles = qa_v3((float)((double)command->angle_words[0] * 360 / 65536),
                (float)((double)command->angle_words[1] * 360 / 65536),
                (float)((double)command->angle_words[2] * 360 / 65536));
        }
        if (command->kind == QA_MOVEMENT_Q3 || command->kind == QA_MOVEMENT_Q2_RERELEASE) source.impulse = 0;
        return application_qc_player_command(provider, actor, &source, error);
    }
    bool subscribed = false;
    for (size_t i = 0; i < profile->input_count; ++i) subscribed |= profile->input[i].movement_slice == movement_slice;
    if (!subscribed) return true;
    struct application_qc_input_scope *scope = engine->input_scope;
    if (!before && (!scope || !qa_actor_id_equal(scope->actor, actor) || scope->movement_slice != movement_slice))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC input completion has no matching begun scope");
    if (!before && !qa_actors_get(qa_session_actors(engine->services.session), actor)) return close_scope(engine, true, error);
    bool admitted = false;
    for (uint32_t i = 1; i <= engine->max_clients; ++i)
        admitted |= engine->clients[i].spawned && qa_actor_id_equal(engine->clients[i].actor, actor);
    if (!admitted) {
        if (!before && !close_through(engine, scope, true, error)) return false;
        return application_fail(error, QA_ERROR_ARGUMENT, "QC input requires a qualified admitted client");
    }
    int32_t reference;
    if (!application_qc_reference(engine, actor, &reference, error)) {
        if (!before) { qa_error cleanup = {0}; if (!close_through(engine, scope, true, &cleanup) && error) *error = cleanup; }
        return false;
    }
    if (before) {
        scope = calloc(1, sizeof(*scope));
        if (!scope) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC input scope");
        scope->saved = profile->field_count ? calloc(profile->field_count, sizeof(*scope->saved)) : NULL;
        if (profile->field_count && !scope->saved) { free(scope); return application_fail(error, QA_ERROR_MEMORY, "Allocating QC nested input storage"); }
        scope->saved_count = profile->field_count; scope->actor = actor; scope->reference = reference;
        scope->movement_slice = movement_slice; scope->previous = engine->input_scope;
        for (struct application_qc_input_scope *outer = scope->previous; outer; outer = outer->previous)
            scope->nested |= qa_actor_id_equal(outer->actor, actor);
        for (struct application_qc_parked_input *parked = engine->parked_inputs; parked; parked = parked->next)
            for (struct application_qc_input_scope *outer = parked->head; outer; outer = outer->previous)
                scope->nested |= qa_actor_id_equal(outer->actor, actor);
        engine->input_scope = scope;
    }
    saved_input *saved = scope->saved;
    qa_qc_instance *vm = provider->state.qc.instance; bool ok = true;
    bool received = false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        const application_qc_client *client = &engine->clients[slot];
        received |= engine->profile == QA_QC_RERELEASE && client->connected && client->spawned &&
            qa_actor_id_equal(client->actor, actor) && client->receipt_seen &&
            command->sequence <= client->receipt_sequence;
    }
    const qa_qc_definition *source_impulse = received ?
        qa_qc_program_find_field(provider->state.qc.program, "impulse") : NULL;
    for (size_t i = 0; before && ok && i < profile->field_count; ++i) {
        const application_qc_bound_field *field = &profile->fields[i]; if (field->kind != QC_FIELD_INPUT) continue;
        saved[i].field = field;
        ok = read_words(vm, reference, field->definition, saved[i].words, error);
        saved[i].valid = ok;
        if (!ok) break;
        if (source_impulse && source_impulse->type == QA_QC_FLOAT &&
            field->input == QC_INPUT_IMPULSE && field->definition == source_impulse) continue;
        if (field->input == QC_INPUT_ANGLES) ok = qa_vec_finite(command->angles) &&
            qa_qc_project_entity_vector(vm, reference, field->definition->offset, command->angles, error);
        else {
            float value = application_qc_input_scalar(command, field->input);
            if (!field->nonzero || value != 0) {
                value *= field->scale;
                if(field->input==QC_INPUT_IMPULSE && value!=0 && !received)
                    application_qc_weapon_command(engine,actor);
                ok = isfinite(value) && qa_qc_project_entity_float(vm, reference, field->definition->offset, value, error);
            }
        }
    }
    application_qc_inputs inputs = {.self = actor, .command = command, .time_ns = engine->source_time_ns, .elapsed_ns = elapsed_ns};
    for (size_t i = 0; ok && i < profile->input_count; ++i) {
        const application_qc_input_binding *binding = &profile->input[i];
        if (binding->before != before || binding->movement_slice != movement_slice) continue;
        bool local_entered[16] = {0}; uint32_t local_before[16][3];
        bool *entered = binding->output_count <= 16 ? local_entered : calloc(binding->output_count, sizeof(*entered));
        uint32_t (*previous)[3] = binding->output_count <= 16 ? local_before : calloc(binding->output_count, sizeof(*previous));
        if (!entered || !previous) { ok = application_fail(error, QA_ERROR_MEMORY, "Allocating QC input capture"); }
        scope->binding = binding; scope->entered = entered;
        for (size_t j = 0; ok && j < binding->output_count; ++j)
            if (binding->outputs[j].field) ok = read_words(vm, reference, binding->outputs[j].field->definition, previous[j], error);
        if (ok) ok = application_qc_run_calls(engine, &binding->calls, &inputs, error);
        bool live = qa_actors_get(qa_session_actors(engine->services.session), actor) != NULL;
        for (size_t j = 0; ok && live && j < binding->output_count; ++j) {
            const application_qc_output *output = &binding->outputs[j];
            if (output->field) {
                uint32_t after[3]; const application_qc_bound_field *field = output->field;
                ok = read_words(vm, reference, field->definition, after, error);
                uint32_t count = field->input == QC_INPUT_ANGLES ? 3u : 1u;
                if (ok && memcmp(previous[j], after, count * sizeof(*after)) != 0) {
                    if (field->input == QC_INPUT_ANGLES) {
                        float v[3]; memcpy(v, after, sizeof(v)); qa_vec3 angles = qa_v3(v[0], v[1], v[2]);
                        ok = qa_vec_finite(angles);
                        if (ok) command->angles = angles;
                    } else {
                        float value; memcpy(&value, after, sizeof(value));
                        ok = set_input(command, field->input, value / field->scale, error);
                    }
                }
            } else if (entered[j]) {
                for (unsigned id = QC_INPUT_ATTACK; ok && id < QC_INPUT_COUNT; ++id)
                    if (output->consume & (UINT64_C(1) << id)) ok = set_input(command, (application_qc_input_id)id, 0, error);
            }
        }
        scope->binding = NULL; scope->entered = NULL;
        if (entered != local_entered) free(entered);
        if (previous != local_before) free(previous);
        if (!live) break;
    }
    if (engine->input_scope != scope) ok = application_fail(error, QA_ERROR_ARGUMENT, "QC input callback left an unmatched nested scope");
    bool live = qa_actors_get(qa_session_actors(engine->services.session), actor) != NULL;
    if (before && ok && !live) ok = application_fail(error, QA_ERROR_ARGUMENT, "QC input callback retired its client");
    if (!ok || !live) {
        qa_error cleanup = {0};
        if (!close_through(engine, scope, true, &cleanup)) { if (error) *error = cleanup; ok = false; }
    } else if (!before && !close_scope(engine, false, error)) ok = false;
    if (!ok && error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Invalid QC source input output");
    return ok;
}
