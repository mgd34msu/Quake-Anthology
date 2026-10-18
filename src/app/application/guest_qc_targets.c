#include "guest_qc_targets.h"
#include "guest_qc_profile.h"
#include "qa/qc_observation.h"

static bool owned_slot(struct application_qc_state *engine, qa_actor_id actor,
    uint32_t *slot, qa_error *error)
{
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    qa_qc_slot_binding binding;
    return qa_qc_actor_observation_slot(vm, actor, slot, error) &&
        qa_qc_slot(vm, *slot, &binding) && binding.kind == QA_QC_SLOT_OWNED;
}

static bool text(struct application_qc_state *engine, uint32_t slot,
    qa_actor_id actor, const qa_qc_definition *field, qa_string_id *out,
    qa_error *error)
{
    *out = QA_STRING_NONE;
    if (!field) return true;
    int32_t value;
    return application_qc_field(field, QA_QC_STRING, error) &&
        qa_qc_actor_observation_int(engine->provider->state.qc.instance, slot,
            actor, field->offset, &value, error) &&
        qa_qc_string_identity(engine->provider->state.qc.instance, value, out, error);
}

static bool number(struct application_qc_state *engine, uint32_t slot,
    qa_actor_id actor, const qa_qc_definition *field, float *out,
    qa_error *error)
{
    *out = 0;
    return !field || (application_qc_field(field, QA_QC_FLOAT, error) &&
        qa_qc_actor_observation_float(engine->provider->state.qc.instance, slot,
            actor, field->offset, out, error));
}

static bool read(void *opaque, qa_actor_id actor, qa_authored_target *out)
{
    struct application_qc_state *engine = opaque;
    const qa_qc_game_fields *fields = engine->field_bindings;
    uint32_t slot;
    qa_error error = {0};
    qa_authored_target value = {0};
    if (!owned_slot(engine, actor, &slot, &error) ||
        !text(engine, slot, actor, fields->classname, &value.classname, &error) ||
        !text(engine, slot, actor, fields->targetname, &value.targetname, &error) ||
        !text(engine, slot, actor, fields->target, &value.target, &error) ||
        !text(engine, slot, actor, fields->killtarget, &value.killtarget, &error) ||
        !text(engine, slot, actor, fields->message, &value.message, &error) ||
        !number(engine, slot, actor, fields->delay, &value.delay_seconds, &error) ||
        !number(engine, slot, actor, fields->wait, &value.wait_seconds, &error))
        return false;
    *out = value;
    return true;
}

static bool use(void *opaque, qa_actor_id actor, qa_actor_id other,
    qa_actor_id activator, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    const qa_qc_definition *field = engine->field_bindings->use;
    if (!field) return true;
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    uint32_t slot;
    int32_t function;
    if (!owned_slot(engine, actor, &slot, error) ||
        !application_qc_field(field, QA_QC_FUNCTION, error) ||
        !qa_qc_actor_observation_int(vm, slot, actor, field->offset, &function, error))
        return false;
    if (!function) return true;
    const qa_qc_definition *time = application_qc_field(
        engine->global_bindings->time, QA_QC_FLOAT, error);
    float seconds;
    if (!time || !qa_qc_global_float(vm, time->offset, &seconds, error)) return false;
    qa_qc_game_global globals[] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = actor}}},
        {"other", {QA_QC_GAME_ACTOR, {.actor = other}}},
        {"activator", {QA_QC_GAME_ACTOR, {.actor = activator}}},
        {"time", {QA_QC_GAME_FLOAT, {.number = seconds}}}
    };
    return qa_qc_game_call_index(engine->provider->state.qc.game,
        (uint32_t)function, NULL, 0, globals, sizeof(globals) / sizeof(*globals),
        NULL, error) && application_qc_publish_client_outputs(engine, error);
}

bool application_qc_acquire_enemy(application_provider *provider, qa_actor_id actor,
    qa_actor_id enemy, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC
        ? provider->state.qc.engine : NULL;
    const qa_qc_definition *field = engine ? engine->field_bindings->enemy : NULL;
    if (!engine || !engine->found_target.function || !field || field->type != QA_QC_ENTITY)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "QuakeC enemy acquisition requires an enemy field and FoundTarget export");
    const qa_actor_registry *actors = qa_session_actors(engine->services.session);
    if (!qa_actors_get(actors, actor) || !qa_actors_get(actors, enemy)) return true;
    qa_qc_instance *vm = provider->state.qc.instance;
    int32_t enemy_reference, self_reference;
    bool ok = application_qc_reference(engine, enemy, &enemy_reference, error);
    if (!qa_actors_get(actors, actor) || !qa_actors_get(actors, enemy)) {
        if (error) *error = (qa_error){0};
        return true;
    }
    if (!ok) return false;
    ok = application_qc_reference(engine, actor, &self_reference, error);
    if (!qa_actors_get(actors, actor) || !qa_actors_get(actors, enemy)) {
        if (error) *error = (qa_error){0};
        return true;
    }
    if (!ok) return false;
    const qa_qc_definition *time = application_qc_field(
        engine->global_bindings->time, QA_QC_FLOAT, error);
    float seconds;
    if (!time || !qa_qc_global_float(vm, time->offset, &seconds, error)) return false;
    ok = qa_qc_set_entity_int(vm, self_reference, field->offset, enemy_reference, error);
    if (!qa_actors_get(actors, actor) || !qa_actors_get(actors, enemy)) {
        if (error) *error = (qa_error){0};
        return true;
    }
    if (!ok) return false;
    qa_qc_game_global globals[] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = actor}}},
        {"time", {QA_QC_GAME_FLOAT, {.number = seconds}}}
    };
    return qa_qc_game_call_index(provider->state.qc.game, engine->found_target.index,
        NULL, 0, globals, sizeof(globals) / sizeof(*globals), NULL, error) &&
        application_qc_publish_client_outputs(engine, error);
}

bool application_qc_mark_monster_target(application_provider *provider, qa_actor_id actor,
    qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC
        ? provider->state.qc.engine : NULL;
    const qa_qc_definition *field = engine ? engine->field_bindings->flags : NULL;
    if (!engine || !field || field->type != QA_QC_FLOAT)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "QuakeC monster classification requires a flags field");
    const qa_actor_registry *actors = qa_session_actors(engine->services.session);
    if (!qa_actors_get(actors, actor)) return true;
    uint32_t slot;
    if (!owned_slot(engine, actor, &slot, NULL))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "QuakeC monster classification requires an owned entity");
    qa_qc_instance *vm = provider->state.qc.instance;
    float flags;
    int32_t reference;
    if (!qa_qc_actor_observation_float(vm, slot, actor, field->offset, &flags, error) ||
        !qa_qc_slot_reference(vm, slot, &reference, error)) return false;
    uint32_t bits = (uint32_t)qa_source_to_i32(flags) | 32u;
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    if (flags == (float)value) return true;
    bool ok = qa_qc_set_entity_float(vm, reference, field->offset, (float)value, error);
    if (!qa_actors_get(actors, actor)) {
        if (error) *error = (qa_error){0};
        return true;
    }
    return ok;
}

static bool set_name(struct application_qc_state *engine, qa_actor_id actor,
    const qa_qc_definition *definition, qa_string_id name, qa_error *error)
{
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    const qa_qc_definition *field = application_qc_field(definition, QA_QC_STRING, error);
    uint32_t slot;
    int32_t reference, value = 0;
    qa_string_id before;
    if (!field || !owned_slot(engine, actor, &slot, error) ||
        !text(engine, slot, actor, field, &before, error)) return false;
    qa_strings *strings = qa_session_strings(engine->services.session);
    if (!qa_strings_text(strings, before).size) before = QA_STRING_NONE;
    if (before == name) return true;
    if (name && !qa_qc_string_allocate(vm, qa_strings_cstr(strings, name), &value, error))
        return false;
    return qa_qc_slot_reference(vm, slot, &reference, error) &&
        qa_qc_set_entity_int(vm, reference, field->offset, value, error);
}

static bool set_targetname(void *opaque, qa_actor_id actor, qa_string_id name,
    qa_error *error)
{
    struct application_qc_state *engine = opaque;
    return set_name(engine, actor, engine->field_bindings->targetname, name, error);
}

static bool set_target(void *opaque, qa_actor_id actor, qa_string_id name,
    qa_error *error)
{
    struct application_qc_state *engine = opaque;
    return set_name(engine, actor, engine->field_bindings->target, name, error);
}

static bool set_delay(void *opaque, qa_actor_id actor, float seconds, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    const qa_qc_definition *field = application_qc_field(engine->field_bindings->delay,
        QA_QC_FLOAT, error);
    uint32_t slot;
    int32_t reference;
    return field && owned_slot(engine, actor, &slot, error) &&
        qa_qc_slot_reference(vm, slot, &reference, error) &&
        qa_qc_set_entity_float(vm, reference, field->offset, seconds, error);
}

bool application_qc_target_binding(application_provider *provider,
    qa_actor_id actor, qa_target_binding *out, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    uint32_t slot;
    if (!owned_slot(engine, actor, &slot, error)) return false;
    *out = (qa_target_binding){.actor = actor,
        .source = provider->component.clock.kind,
        .context = engine, .read = read, .use = use, .set_targetname = set_targetname,
        .set_target = set_target, .set_delay = set_delay};
    return true;
}

bool application_qc_targets_bind(struct application_qc_state *engine,
    qa_qc_instance *vm, const qa_qc_entity_access *access, qa_error *error)
{
    (void)vm;
    if (access->binding.kind != QA_QC_SLOT_OWNED) return true;
    qa_targets *targets = engine->provider->application->targets;
    qa_target_binding current;
    if (qa_targets_binding(targets, access->binding.actor, &current) &&
        current.context == engine && current.read == read && current.use == use)
        return true;
    qa_target_binding binding;
    if (!application_qc_target_binding(engine->provider, access->binding.actor,
            &binding, error) ||
        !qa_targets_bind(targets, &binding, error))
        return false;
    return true;
}

bool application_qc_targets_stored(struct application_qc_state *engine,
    qa_qc_instance *vm, const qa_qc_store_event *event, qa_error *error)
{
    if (event->kind != QA_QC_STORE_ENTITY || !event->entity_reference) return true;
    const qa_qc_definition *fields[] = {
        engine->field_bindings->targetname, engine->field_bindings->classname
    };
    bool changed = false;
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
        const qa_qc_definition *field = fields[i];
        if (field && field->offset >= event->word &&
            field->offset - event->word < event->count) {
            size_t word = field->offset - event->word;
            changed |= event->before[word] != event->after[word];
        }
    }
    if (!changed) return true;
    qa_actor_id actor;
    if (!qa_qc_reference_actor(vm, event->entity_reference, &actor, error)) return false;
    qa_qc_slot_binding binding;
    uint32_t slot;
    if (qa_qc_actor_observation_slot(vm, actor, &slot, error) &&
        qa_qc_slot(vm, slot, &binding) && binding.kind == QA_QC_SLOT_OWNED)
        qa_targets_changed(engine->provider->application->targets, actor);
    return true;
}

void application_qc_targets_close(struct application_qc_state *engine)
{
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    for (uint32_t i = 1; i < qa_qc_entity_count(vm); ++i) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(vm, i, &binding) || binding.kind != QA_QC_SLOT_OWNED) continue;
        qa_targets_unbind_context(engine->provider->application->targets,
            binding.actor, engine);
    }
}
