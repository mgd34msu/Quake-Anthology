#include "guest_qc_profile.h"
#include "guest_qc_items.h"
#include "guest_qc_combat.h"
#include "guest_qc_protection.h"
#include "guest_qc_objectives.h"
#include "guest_q3_component_clients.h"
#include <stdio.h>
#include <float.h>
#include "qa/text.h"

static bool resolve(const application_qc_value *value, const application_qc_inputs *inputs,
                    qa_qc_game_value *result, qa_error *error)
{
    if (value->kind == QC_VALUE_CONSTANT) { *result = value->constant; return true; }
    qa_qc_game_value out = {.kind = QA_QC_GAME_FLOAT};
    if (value->kind != QC_VALUE_INPUT) {
        const qa_command_invocation *console = inputs->console;
        if (!console || !qa_console_invocation_current(console->console, console))
            return application_fail(error, QA_ERROR_ARGUMENT, "QC console value requires its entered command");
        if (value->kind == QC_VALUE_ARGUMENT_COUNT) out.value.number = (float)console->argc;
        else if (value->kind == QC_VALUE_ARGUMENTS_TEXT) {
            out.kind = QA_QC_GAME_STRING; out.value.string = console->args_text;
        } else {
            const char *text = value->argument < console->argc ? console->argv[value->argument] : "";
            out.kind = value->constant.kind;
            if (out.kind == QA_QC_GAME_STRING) out.value.string = text;
            else {
                double number;
                if (!qa_parse_atof(text, &number, error)) return false;
                out.value.number = (float)number;
            }
        }
        *result = out; return true;
    }
    const qa_movement_command *command = inputs->command;
    switch (value->source) {
    case QC_INPUT_SELF: out.kind = QA_QC_GAME_ACTOR; out.value.actor = inputs->self; break;
    case QC_INPUT_OTHER: out.kind = QA_QC_GAME_ACTOR; out.value.actor = inputs->other; break;
    case QC_INPUT_ACTIVATOR: out.kind = QA_QC_GAME_ACTOR; out.value.actor = inputs->activator; break;
    case QC_INPUT_ATTACKER: out.kind = QA_QC_GAME_ACTOR; out.value.actor = inputs->attacker; break;
    case QC_INPUT_INFLICTOR: out.kind = QA_QC_GAME_ACTOR; out.value.actor = inputs->inflictor; break;
    case QC_INPUT_AMOUNT: out.value.number = inputs->amount; break;
    case QC_INPUT_KNOCKBACK: out.value.number = inputs->knockback; break;
    case QC_INPUT_POINT: out.kind = QA_QC_GAME_VECTOR; out.value.vector = inputs->point; break;
    case QC_INPUT_DIRECTION: out.kind = QA_QC_GAME_VECTOR; out.value.vector = inputs->direction; break;
    case QC_INPUT_NORMAL: out.kind = QA_QC_GAME_VECTOR; out.value.vector = inputs->normal; break;
    case QC_INPUT_ITEM: out.kind = QA_QC_GAME_STRING; out.value.string = inputs->item; break;
    case QC_INPUT_DAMAGE_FLAGS: out.value.number = inputs->damage_flags; break;
    case QC_INPUT_PROTECTION_SCALE: out.value.number = inputs->protection_scale; break;
    case QC_INPUT_PICKUP_COUNT: out.value.number = inputs->pickup_count; break;
    case QC_INPUT_PICKUP_HAS_COUNT: out.value.number = inputs->pickup_has_count ? 1 : 0; break;
    case QC_INPUT_PICKUP_DROPPED: out.value.number = inputs->pickup_dropped ? 1 : 0; break;
    case QC_INPUT_TIME: out.value.number = (float)((double)inputs->time_ns / 1e9); break;
    case QC_INPUT_ELAPSED: out.value.number = (float)((double)inputs->elapsed_ns / 1e9); break;
    case QC_INPUT_RESULT: out.value.number = inputs->result; break;
    case QC_INPUT_ANGLES: out.kind = QA_QC_GAME_VECTOR; if (command) out.value.vector = command->angles; break;
    case QC_INPUT_ATTACK: case QC_INPUT_JUMP: case QC_INPUT_IMPULSE:
    case QC_INPUT_FORWARD: case QC_INPUT_SIDE: case QC_INPUT_UP:
        if (command) out.value.number = application_qc_input_scalar(command, value->source);
        break;
    case QC_INPUT_COUNT: break;
    }
    *result = out; return true;
}
static bool run_call_staged(struct application_qc_state *engine, const application_qc_call *call,
                     const qa_qc_inline_region *region, const application_qc_inputs *inputs, uint32_t result[3], qa_error *error)
{
    if (!application_qc_objectives_sync(engine,error)) return false;
    qa_qc_game_value arguments[8];
    for (size_t j = 0; j < call->argument_count; ++j)
        if (!resolve(&call->arguments[j], inputs, &arguments[j], error)) return false;
    qa_qc_game_global local[16];
    qa_qc_game_global *globals = call->global_count <= 16 ? local : calloc(call->global_count, sizeof(*globals));
    if (!globals) return application_fail(error, QA_ERROR_MEMORY, "Allocating qualified QC globals");
    bool ok = true;
    for (size_t j = 0; ok && j < call->global_count; ++j) {
        globals[j].name = call->globals[j].definition->name;
        ok = resolve(&call->globals[j].value, inputs, &globals[j].value, error);
    }
    if (ok) ok = region ? qa_qc_game_call_region(engine->provider->state.qc.game,region,arguments,
        call->argument_count,globals,call->global_count,result,error) :
        qa_qc_game_call_index(engine->provider->state.qc.game,call->function,arguments,
        call->argument_count,globals,call->global_count,result,error);
    if (globals != local) free(globals);
    return ok && application_qc_objectives_sync(engine,error) && application_qc_publish_client_outputs(engine, error);
}
bool application_qc_run_call(struct application_qc_state *engine,const application_qc_call *call,
    const application_qc_inputs *inputs,uint32_t result[3],qa_error *error)
{return run_call_staged(engine,call,NULL,inputs,result,error);}
bool application_qc_run_call_region(struct application_qc_state *engine,const application_qc_call *call,
    const qa_qc_inline_region *region,const application_qc_inputs *inputs,uint32_t result[3],qa_error *error)
{return run_call_staged(engine,call,region,inputs,result,error);}
bool application_qc_run_calls(struct application_qc_state *engine, const application_qc_calls *calls,
                                const application_qc_inputs *inputs, qa_error *error)
{
    if (!calls->count) return application_qc_objectives_sync(engine,error);
    for (size_t i = 0; i < calls->count; ++i)
        if (!application_qc_run_call(engine, calls->values + i, inputs, NULL, error)) return false;
    return true;
}
bool application_qc_command_name_equal(const char *left, const char *right)
{
    while (*left && *right) {
        unsigned char a = (unsigned char)*left++, b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
    }
    return *left == *right;
}
bool application_qc_declared_command(void *opaque, const qa_command_invocation *command, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (!profile || !engine->provider->state.qc.game || !command->argc ||
        !qa_console_invocation_current(command->console, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC declared command requires its live source console");
    for (size_t i = 0; i < profile->command_count; ++i) {
        if (!application_qc_command_name_equal(profile->commands[i].name, command->argv[0])) continue;
        application_qc_calls calls = {.values = &profile->commands[i].call, .count = 1};
        application_qc_inputs inputs = {.console = command};
        return application_qc_run_calls(engine, &calls, &inputs, error);
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QC declared command lost its compiled declaration");
}
static bool callback_current(const application_qc_callback *callback, qa_error *error)
{
    const struct application_qc_state *engine = callback->engine;
    const application_provider *provider = engine ? engine->provider : NULL;
    return (provider && provider->state.qc.engine == engine && provider->state.qc.game &&
        provider->constructed && provider->attached && !provider->close_pending &&
        !provider->application->destroy_requested && engine->initialized && !engine->loading) ||
        application_fail(error, QA_ERROR_ARGUMENT, "QC callback lost its admitted source owner");
}
static bool callback_inputs(application_qc_callback *callback, const void *request, const void *result,
                            application_qc_inputs *inputs, qa_error *error)
{
    application_q3_mod_inputs values;
    if (!callback_current(callback, error) ||
        !callback->services.inputs(callback->services.context, request, result, &values, error) ||
        !callback_current(callback, error)) return false;
    const application_q3_mod_value *self = values.values + Q3_MOD_SELF;
    if (self->kind != Q3_MOD_VALUE_ACTOR)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC callback requires its actual canonical actor");
    *inputs = (application_qc_inputs){.self = self->as.actor, .time_ns = callback->engine->source_time_ns};
    if (callback->operation == Q3_MOD_THINK) {
        const application_q3_mod_actor_request *actor = request;
        inputs->time_ns = actor->source.think.time_ns;
        inputs->elapsed_ns = actor->source.think.elapsed_ns;
    } else if (callback->operation == Q3_MOD_TOUCH || callback->operation == Q3_MOD_USE) {
        const application_q3_mod_value *other = values.values + Q3_MOD_OTHER;
        if (other->kind != Q3_MOD_VALUE_ACTOR)
            return application_fail(error, QA_ERROR_ARGUMENT, "QC contact callback requires its actual other actor");
        inputs->other = other->as.actor;
        if (callback->operation == Q3_MOD_USE) {
            const application_q3_mod_value *activator = values.values + Q3_MOD_ACTIVATOR;
            if (activator->kind != Q3_MOD_VALUE_ACTOR)
                return application_fail(error, QA_ERROR_ARGUMENT, "QC use callback requires its actual activator actor");
            inputs->activator = activator->as.actor;
        }
    } else if (callback->operation == Q3_MOD_GIVE || callback->operation == Q3_MOD_CONSUME) {
        const application_q3_mod_value *amount = values.values + Q3_MOD_AMOUNT;
        const application_q3_mod_value *item = values.values + Q3_MOD_ITEM;
        if (amount->kind != Q3_MOD_VALUE_SCALAR || item->kind != Q3_MOD_VALUE_STRING)
            return application_fail(error, QA_ERROR_ARGUMENT, "QC inventory callback requires its actual amount and item identity");
        inputs->amount = (float)amount->as.scalar;
        inputs->item = item->as.string;
    } else {
        const application_q3_mod_value *attacker = values.values + Q3_MOD_ATTACKER;
        const application_q3_mod_value *amount = values.values + Q3_MOD_AMOUNT;
        const application_q3_mod_value *knockback = values.values + Q3_MOD_KNOCKBACK;
        if (attacker->kind != Q3_MOD_VALUE_ACTOR || amount->kind != Q3_MOD_VALUE_SCALAR ||
            knockback->kind != Q3_MOD_VALUE_SCALAR)
            return application_fail(error, QA_ERROR_ARGUMENT, "QC reaction callback requires its actual damage values");
        inputs->attacker = attacker->as.actor;
        inputs->amount = (float)amount->as.scalar;
        inputs->knockback = (float)knockback->as.scalar;
        if (callback->operation == Q3_MOD_DAMAGE || callback->operation == Q3_MOD_DIE) {
            const application_q3_mod_value *inflictor = values.values + Q3_MOD_INFLICTOR;
            const application_q3_mod_value *point = values.values + Q3_MOD_POINT;
            if (inflictor->kind != Q3_MOD_VALUE_ACTOR || point->kind != Q3_MOD_VALUE_VECTOR)
                return application_fail(error, QA_ERROR_ARGUMENT, "QC callback requires its actual source inflictor and point");
            inputs->inflictor = inflictor->as.actor;
            inputs->point = point->as.vector;
        }
        if (callback->operation == Q3_MOD_DAMAGE) {
            const application_q3_mod_value *direction = values.values + Q3_MOD_DIRECTION;
            const application_q3_mod_value *normal = values.values + Q3_MOD_NORMAL;
            if (direction->kind != Q3_MOD_VALUE_VECTOR || normal->kind != Q3_MOD_VALUE_VECTOR)
                return application_fail(error, QA_ERROR_ARGUMENT, "QC damage callback requires its actual source geometry");
            inputs->direction = direction->as.vector;
            inputs->normal = normal->as.vector;
        }
    }
    if (result) {
        const application_q3_mod_value *observed = values.values + Q3_MOD_RESULT;
        if (observed->kind != Q3_MOD_VALUE_SCALAR)
            return application_fail(error, QA_ERROR_ARGUMENT, "QC observer requires its actual canonical result");
        inputs->result = (float)observed->as.scalar;
    }
    return true;
}
static bool callback_observe(void *opaque, const void *request, const void *result, qa_error *error)
{
    application_qc_callback *callback = opaque;
    application_qc_inputs inputs;
    return callback_inputs(callback, request, result, &inputs, error) &&
        application_qc_run_call(callback->engine, &callback->call, &inputs, NULL, error) && callback_current(callback, error);
}
static bool callback_replace(void *opaque, const void *request, qa_operation_next next, void *result, qa_error *error)
{
    (void)next;
    application_qc_callback *callback = opaque;
    application_qc_inputs inputs;
    uint32_t returned[3];
    if (!callback_inputs(callback, request, NULL, &inputs, error) ||
        !application_qc_run_call(callback->engine, &callback->call, &inputs, returned, error) ||
        !callback_current(callback, error)) return false;
    float value;
    memcpy(&value, returned, sizeof(value));
    return callback->services.replace(callback->services.context, result, value != 0, error);
}
static bool callback_transform(void *opaque, void *request, qa_error *error)
{
    application_qc_callback *callback = opaque;
    application_qc_inputs inputs;
    uint32_t returned[3];
    if (!callback_inputs(callback, request, NULL, &inputs, error) ||
        !application_qc_run_call(callback->engine, &callback->call, &inputs, returned, error) ||
        !callback_current(callback, error)) return false;
    float value;
    memcpy(&value, returned, sizeof(value));
    return callback->services.transform(callback->services.context, request, callback->knockback,
        value, error) && callback_current(callback, error);
}
bool application_qc_callbacks_register(application_provider *provider, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC
        ? provider->state.qc.engine : NULL;
    struct application_qc_profile *profile = provider && provider->kind == APPLICATION_PROVIDER_QC
        ? provider->state.qc.qualified : NULL;
    if (!engine || !engine->initialized || engine->loading) return true;
    /* Publication may retain the VM while replacing its selected services;
     * map travel also suspends combat bindings without clearing borrowed rows. */
    qa_qc_instance *vm = provider->state.qc.instance;
    for (uint32_t slot = 1; slot < qa_qc_entity_count(vm); ++slot) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(vm, slot, &binding) || (binding.kind != QA_QC_SLOT_OWNED &&
            binding.kind != QA_QC_SLOT_BORROWED)) continue;
        int32_t reference; qa_actor_id actor;
        if (!qa_qc_slot_reference(vm, slot, &reference, error) ||
            !qa_qc_reference_actor(vm, reference, &actor, error)) return false;
        qa_qc_entity_access access = {.kind = QA_QC_ENTITY_BIND,
            .binding = binding, .reference = reference};
        if (!application_qc_prepare_entity(engine, vm, &access, error)) return false;
    }
    if (!profile || !profile->callback_count) return true;
    application_q3_mod_operation_services services[Q3_MOD_OPERATION_COUNT];
    if (!application_q3_mod_operations_read(provider->application->mod_operations, services, error)) return false;
    for (size_t i = 0; i < profile->callback_count; ++i) {
        application_qc_callback *callback = profile->callbacks + i;
        callback->engine = engine;
        if (!callback_current(callback, error)) return false;
        if (callback->registration) continue;
        callback->services = services[callback->operation];
        qa_operation_hook hook = {.owner = provider->owner, .name = callback->id,
            .kind = callback->stage, .context = callback};
        if (callback->stage == QA_OPERATION_TRANSFORM) hook.call.transform = callback_transform;
        else if (callback->stage == QA_OPERATION_OBSERVE) hook.call.observe = callback_observe;
        else hook.call.replace = callback_replace;
        if (!qa_operation_register(callback->services.operation, &hook, &callback->registration, error)) return false;
    }
    engine->callbacks_active = true;
    return true;
}
bool application_qc_callbacks_suspend(application_provider *provider, qa_error *error)
{
    struct application_qc_profile *profile = provider && provider->kind == APPLICATION_PROVIDER_QC
        ? provider->state.qc.qualified : NULL;
    if (!profile) return true;
    for (size_t i = 0; i < profile->callback_count; ++i) {
        const application_qc_callback *callback = profile->callbacks + i;
        if (callback->registration && !qa_operation_destroy_validate(callback->services.operation, error)) return false;
    }
    for (size_t i = 0; i < profile->callback_count; ++i) {
        application_qc_callback *callback = profile->callbacks + i;
        if (callback->registration && !qa_operation_unregister(callback->services.operation, callback->registration))
            return application_fail(error, QA_ERROR_ARGUMENT, "QC callback lost its actual operation registration");
        callback->registration = 0;
    }
    if (provider->state.qc.engine) provider->state.qc.engine->callbacks_active = false;
    return true;
}
bool application_qc_callbacks_ready(const struct application_qc_state *engine, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (engine->callbacks_active && (!profile || !profile->callback_count))
        return application_fail(error, QA_ERROR_FORMAT, "QC callback continuation has no declared hooks");
    for (size_t i = 0; profile && i < profile->callback_count; ++i) {
        const application_qc_callback *callback = profile->callbacks + i;
        if ((callback->registration != 0) != engine->callbacks_active ||
            (callback->registration && (callback->engine != engine ||
             !qa_operation_destroy_validate(callback->services.operation, error))))
            return application_fail(error, QA_ERROR_FORMAT, "QC callback continuation differs from its actual hooks");
    }
    return application_qc_objectives_ready(engine,error) && application_qc_combat_ready(engine,error) &&
        application_qc_protection_ready(engine,error);
}
static bool project_value(qa_qc_instance *vm, int32_t reference, const qa_qc_definition *field,
                            qa_qc_game_value value, qa_error *error)
{
    switch (value.kind) {
    case QA_QC_GAME_FLOAT: return qa_qc_project_entity_float(vm, reference, field->offset, value.value.number, error);
    case QA_QC_GAME_VECTOR: return qa_qc_project_entity_vector(vm, reference, field->offset, value.value.vector, error);
    case QA_QC_GAME_STRING: {
        static const char prefix[] = "qc-projection:";
        size_t length = strlen(value.value.string);
        if (length > SIZE_MAX - sizeof(prefix)) return application_fail(error, QA_ERROR_MEMORY, "QC projection string name overflow");
        char *name = malloc(length + sizeof(prefix));
        if (!name) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC projection string name");
        memcpy(name, prefix, sizeof(prefix) - 1);
        memcpy(name + sizeof(prefix) - 1, value.value.string, length + 1);
        int32_t text;
        bool ok = qa_qc_engine_string(vm, name, value.value.string, length + 1, &text, error);
        free(name);
        return ok && qa_qc_project_entity_int(vm, reference, field->offset, text, error);
    }
    default: return application_fail(error, QA_ERROR_ARGUMENT, "QC declared field projection has an invalid type");
    }
}
bool application_qc_seed_fields(struct application_qc_state *engine, qa_actor_id actor, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (!profile) return true;
    int32_t reference;
    if (!qa_qc_actor_reference(engine->provider->state.qc.instance, actor, false, &reference, error)) return false;
    bool client = false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot)
        if (engine->clients[slot].connected && qa_actor_id_equal(engine->clients[slot].actor, actor)) { client = true; break; }
    for (size_t i = 0; i < profile->field_count; ++i) {
        if (profile->fields[i].kind == QC_FIELD_CONSTANT &&
            !project_value(engine->provider->state.qc.instance, reference, profile->fields[i].definition,
                profile->fields[i].constant.constant, error)) return false;
        if (client && (profile->fields[i].kind == QC_FIELD_MIN || profile->fields[i].kind == QC_FIELD_MAX)) {
            qa_body_state body;
            if (!qa_world_body_read(engine->world, actor, &body, error) ||
                !qa_qc_project_entity_vector(engine->provider->state.qc.instance, reference, profile->fields[i].definition->offset,
                    profile->fields[i].kind == QC_FIELD_MIN ? body.bounds.mins : body.bounds.maxs, error)) return false;
        }
    }
    return true;
}
bool application_qc_prepare_markers(struct application_qc_state *engine, qa_error *error)
{
    if (!engine->provider->state.qc.qualified) return true;
    if (!qa_builtin_observations(&engine->services, &engine->observations, error)) return false;
    for (size_t i = 0; i < engine->observations.count; ++i) {
        qa_actor_id actor = engine->observations.ids[i];
        if (!qa_actors_get(qa_session_actors(engine->services.session), actor)) continue;
        qa_builtin_player_info player;
        if (engine->services.player_info && engine->services.player_info(engine->services.context, actor, &player) && player.connected) continue;
        int32_t reference;
        qa_error absent = {0};
        if (qa_qc_actor_reference(engine->provider->state.qc.instance, actor, false, &reference, &absent)) continue;
        if (absent.code != QA_ERROR_NOT_FOUND) { if (error) *error = absent; return false; }
        if (!application_qc_reference(engine, actor, &reference, error) || !application_qc_seed_fields(engine, actor, error)) return false;
    }
    return true;
}
static bool actor_current(struct application_qc_state *engine, qa_qc_instance *vm,
                            int32_t reference, qa_actor_id expected, qa_error *error)
{
    (void)engine;
    qa_actor_id actor;
    return qa_qc_reference_actor(vm, reference, &actor, error) &&
        (qa_actor_id_equal(actor, expected) || application_fail(error, QA_ERROR_NOT_FOUND, "QC declared actor projection changed generation"));
}
static bool declared_client(const struct application_qc_state *engine,qa_actor_id actor)
{
    for(uint32_t slot=1;slot<=engine->max_clients;++slot)
        if(engine->clients[slot].connected&&qa_actor_id_equal(engine->clients[slot].actor,actor)) return true;
    return false;
}
static bool project_userinfo(qa_qc_instance *vm,int32_t reference,
    const application_qc_bound_field *field,const qa_buffer *value,qa_error *error)
{
    size_t length=strlen(field->key);
    if(length>SIZE_MAX-48) return application_fail(error,QA_ERROR_MEMORY,"QC userinfo buffer name overflows");
    char *name=malloc(length+48);
    if(!name) return application_fail(error,QA_ERROR_MEMORY,"Retaining QC userinfo buffer identity");
    snprintf(name,length+48,"mod-userinfo:%d:%s",reference,field->key);
    size_t capacity=value->size>=128?value->size+1:128;
    int32_t string_id;
    bool ok=qa_qc_engine_string(vm,name,(const char *)value->data,capacity,&string_id,error);
    free(name);
    return ok&&qa_qc_project_entity_int(vm,reference,field->definition->offset,string_id,error);
}
bool application_qc_project_declared(struct application_qc_state *engine, qa_qc_instance *vm,
                                      const qa_qc_entity_access *access, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (!profile || access->binding.kind != QA_QC_SLOT_BORROWED || engine->projecting) return true;
    engine->projecting = true; bool ok = true;
    for (size_t i = 0; ok && i < profile->field_count; ++i) {
        const application_qc_bound_field *field = &profile->fields[i]; const qa_qc_definition *def = field->definition;
        uint32_t width = def->type == QA_QC_VECTOR ? 3u : 1u;
        if (def->offset >= access->word + access->count || def->offset + width <= access->word) continue;
        qa_qc_game_value value = {.kind = QA_QC_GAME_FLOAT}; qa_body_state body;
        switch (field->kind) {
        case QC_FIELD_PRIVATE: case QC_FIELD_CONSTANT: case QC_FIELD_INPUT: case QC_FIELD_THINK: case QC_FIELD_NEXTTHINK: continue;
        case QC_FIELD_USERINFO: {
            if(!declared_client(engine,access->binding.actor)) continue;
            qa_buffer text={0};
            ok=application_client_userinfo_key_read(engine->provider->application,engine->world,
                access->binding.actor,field->key,&text,error);
            if(ok) ok=actor_current(engine,vm,access->reference,access->binding.actor,error)&&
                project_userinfo(vm,access->reference,field,&text,error);
            qa_buffer_free(&text);
            continue;
        }
        case QC_FIELD_HEALTH: {
            qa_combat_state combat;
            ok = qa_combat_read_traits(engine->services.combat, access->binding.actor, &combat, error);
            if (ok) value.value.number = combat.health;
            break;
        }
        case QC_FIELD_INVENTORY: {
            qa_inventory_entry entry; qa_error absent = {0};
            if (qa_inventory_entry_read(engine->services.inventory, access->binding.actor, field->item, &entry, &absent)) {
                ok = isfinite(entry.count) && fabs(entry.count) <= FLT_MAX;
                if (ok) value.value.number = (float)entry.count;
                else application_fail(error, QA_ERROR_FORMAT, "Canonical inventory count exceeds QC binary32");
            }
            else if (absent.code != QA_ERROR_NOT_FOUND) { if (error) *error = absent; ok = false; }
            break;
        }
        case QC_FIELD_CLASSNAME: {
            qa_builtin_actor_traits traits = {0};
            if (engine->services.actor_traits) engine->services.actor_traits(engine->services.context, access->binding.actor, &traits);
            const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), access->binding.actor);
            qa_string_id classname = traits.classname ? traits.classname : record ? record->definition : 0;
            value.kind = QA_QC_GAME_STRING;
            value.value.string = qa_strings_cstr(qa_session_strings(engine->services.session), classname);
            ok = classname != 0 && value.value.string != NULL;
            if (!ok) application_fail(error, QA_ERROR_UNSUPPORTED, "QC marker classname has no canonical metadata");
            break;
        }
        case QC_FIELD_CLIENT_FLAGS: {
            qa_builtin_player_info player; qa_builtin_actor_traits traits = {0}; float previous;
            bool client = engine->services.player_info && engine->services.player_info(engine->services.context, access->binding.actor, &player) && player.connected;
            if (client && engine->services.actor_traits) engine->services.actor_traits(engine->services.context, access->binding.actor, &traits);
            ok = qa_qc_entity_float(vm, access->reference, def->offset, &previous, error) && isfinite(previous);
            uint32_t bits = 0;
            if (ok) {
                bits = (uint32_t)qa_source_float_to_i32(previous) & field->private_mask;
                if (client) bits |= 8u | (traits.no_target ? 128u : 0u);
                if (client && field->grounded) {
                    ok = qa_world_body_read(engine->world, access->binding.actor, &body, error);
                    if (ok && body.ground.registry) bits |= 512u;
                }
                int32_t integer; memcpy(&integer, &bits, sizeof(integer));
                value.value.number = (float)integer;
            }
            break;
        }
        case QC_FIELD_VIEW: {
            if (application_qc_output_field_owned(engine, access->binding.actor, field)) continue;
            qa_builtin_player_info player;
            bool client = engine->services.player_info && engine->services.player_info(engine->services.context, access->binding.actor, &player) && player.connected;
            value.kind = QA_QC_GAME_VECTOR; value.value.vector = qa_v3(0, 0, client ? player.view_height : 0); break;
        }
        case QC_FIELD_ORIGIN: case QC_FIELD_VELOCITY: case QC_FIELD_ANGLES: case QC_FIELD_MIN: case QC_FIELD_MAX:
            if (application_qc_output_field_owned(engine, access->binding.actor, field)) continue;
            ok = qa_world_body_read(engine->world, access->binding.actor, &body, error);
            value.kind = QA_QC_GAME_VECTOR;
            if (ok) value.value.vector = field->kind == QC_FIELD_ORIGIN ? body.origin : field->kind == QC_FIELD_VELOCITY ? body.velocity :
                field->kind == QC_FIELD_ANGLES ? body.angles : field->kind == QC_FIELD_MIN ? body.bounds.mins : body.bounds.maxs;
            break;
        }
        if (ok) ok = actor_current(engine, vm, access->reference, access->binding.actor, error) &&
            project_value(vm, access->reference, def, value, error);
    }
    engine->projecting = false; return ok;
}
bool application_qc_store_declared(struct application_qc_state *engine, qa_qc_instance *vm,
                                    const qa_qc_store_event *event, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (!profile || engine->projecting || event->kind != QA_QC_STORE_ENTITY || !event->entity_reference) return true;
    qa_actor_id actor; qa_qc_slot_binding binding;
    qa_qc_entity_layout layout = qa_qc_default_entity_layout(engine->provider->state.qc.program, engine->profile);
    if (!qa_qc_reference_actor(vm, event->entity_reference, &actor, error) ||
        !qa_qc_slot(vm, (uint32_t)event->entity_reference / layout.stride_bytes, &binding)) return false;
    if (binding.kind != QA_QC_SLOT_BORROWED) return true;
    if (!application_qc_items_source_stored(engine,vm,event,error) ||
        !application_qc_protection_source_stored(engine,vm,event,error)) return false;
    engine->projecting = true; bool ok = true;
    for (size_t i = 0; ok && i < profile->field_count; ++i) {
        const application_qc_bound_field *field = &profile->fields[i]; const qa_qc_definition *def = field->definition;
        uint32_t width = def->type == QA_QC_VECTOR ? 3u : 1u;
        if (def->offset >= event->word + event->count || def->offset + width <= event->word) continue;
        float scalar; qa_vec3 vector; qa_body_state body;
        switch (field->kind) {
        case QC_FIELD_PRIVATE: case QC_FIELD_CONSTANT: case QC_FIELD_INPUT: case QC_FIELD_THINK: case QC_FIELD_NEXTTHINK: break;
        case QC_FIELD_USERINFO: {
            if(!declared_client(engine,actor)) break;
            int32_t string_id;const char *text;
            ok=qa_qc_entity_int(vm,event->entity_reference,def->offset,&string_id,error)&&
                qa_qc_string(vm,string_id,&text,error)&&
                application_client_userinfo_key_write(engine->provider->application,engine->world,
                    actor,field->key,text,error);
            break;
        }
        case QC_FIELD_VIEW: {
            bool client = false, declared = false;
            for (uint32_t slot = 1; slot <= engine->max_clients; ++slot)
                if (engine->clients[slot].connected && qa_actor_id_equal(engine->clients[slot].actor, actor)) { client = true; break; }
            for (size_t j = 0; j < profile->client_output_count; ++j) {
                const application_qc_client_output *output = profile->client_outputs + j;
                if (output->channel == APPLICATION_CLIENT_VIEW_OFFSET && !output->height && output->field == field) { declared = true; break; }
            }
            if (client && declared) break;
            ok = application_fail(error, QA_ERROR_ARGUMENT, "QC view offset store lacks its declared client output owner"); break;
        }
        case QC_FIELD_CLASSNAME:
            ok = application_fail(error, QA_ERROR_ARGUMENT, "QC store requires a declared canonical output owner"); break;
        case QC_FIELD_HEALTH:
            if (application_qc_combat_health_owned(engine,actor,def)) break;
            ok = qa_qc_entity_float(vm, event->entity_reference, def->offset, &scalar, error) &&
                qa_combat_set_health(engine->services.combat, actor, scalar, error); break;
        case QC_FIELD_INVENTORY: {
            qa_inventory_entry entry;
            ok = qa_qc_entity_float(vm, event->entity_reference, def->offset, &scalar, error) &&
                qa_inventory_entry_read(engine->services.inventory, actor, field->item, &entry, error);
            if (ok) ok = application_qc_items_field_permission(engine,actor,field->item,entry.count!=scalar,false,error);
            if (ok) { entry.count = scalar; ok = qa_inventory_configure(engine->services.inventory, actor, &entry, NULL, NULL, error); }
            break;
        }
        case QC_FIELD_CLIENT_FLAGS: {
            float before; memcpy(&before, &event->before[def->offset - event->word], sizeof(before));
            ok = qa_qc_entity_float(vm, event->entity_reference, def->offset, &scalar, error) && isfinite(before) && isfinite(scalar) &&
                (double)before >= INT32_MIN && (double)before <= INT32_MAX && (double)scalar >= INT32_MIN && (double)scalar <= INT32_MAX;
            if (ok) {
                uint32_t next = (uint32_t)(int32_t)scalar, changed = ((uint32_t)(int32_t)before ^ next) & ~field->private_mask;
                if (changed) {
                    ok = field->grounded && changed == 512u && !(next & 512u);
                    if (ok) { ok = qa_world_body_read(engine->world, actor, &body, error); if (ok) { body.ground = (qa_actor_id){0}; ok = qa_world_body_write(engine->world, actor, &body, error); } }
                    if (!ok && error && error->code == QA_OK) application_fail(error, QA_ERROR_ARGUMENT, "QC changed canonical client flags without ownership");
                }
            }
            break;
        }
        case QC_FIELD_ORIGIN: case QC_FIELD_VELOCITY: case QC_FIELD_ANGLES: case QC_FIELD_MIN: case QC_FIELD_MAX:
            if (application_qc_output_field_owned(engine, actor, field)) break;
            ok = qa_world_body_read(engine->world, actor, &body, error) && qa_qc_entity_vector(vm, event->entity_reference, def->offset, &vector, error);
            if (ok) {
                if (field->kind == QC_FIELD_ORIGIN) body.origin = vector;
                else if (field->kind == QC_FIELD_VELOCITY) body.velocity = vector;
                else if (field->kind == QC_FIELD_ANGLES) body.angles = vector;
                else if (field->kind == QC_FIELD_MIN) body.bounds.mins = vector;
                else body.bounds.maxs = vector;
                ok = qa_world_body_write(engine->world, actor, &body, error);
            }
            break;
        }
        if (ok) ok = actor_current(engine, vm, event->entity_reference, actor, error);
    }
    engine->projecting = false;
    if (!ok && error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Invalid QC declared source store");
    return ok;
}
bool application_qc_initialize_declared(struct application_qc_state *engine, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (!profile) return true;
    if (!application_qc_prepare_markers(engine, error)) return false;
    if (!engine->initialized) {
        application_qc_inputs inputs = {.time_ns = engine->source_time_ns};
        if (!application_qc_run_calls(engine, &profile->initialize, &inputs, error)) return false;
        engine->initialized = true;
    }
    return true;
}

static bool initialize_declared_map_context(struct application_qc_state *engine,
                                              qa_string_id map, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    const char *path = qa_strings_cstr(qa_session_strings(engine->services.session), map);
    if (!profile || !path || !qa_qc_game_loading(engine->provider->state.qc.game, true, error)) return false;
    engine->loading = true; qa_cvars_set_server_active(engine->cvars, false);
    engine->check_slot = 0; engine->check_time = 0; engine->check_cluster = -1;
    if (!engine->source_time_ns) engine->source_time_ns = UINT64_C(1000000000);
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    if (!qa_qc_game_set_time(engine->provider->state.qc.game, (double)engine->source_time_ns / 1e9, 0, error)) return false;
    static const char *names[] = {"skill", "deathmatch", "coop", "teamplay"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        const qa_qc_definition *global = qa_qc_program_find_global(engine->provider->state.qc.program, names[i]);
        const qa_cvar_view *cvar = qa_cvars_find(engine->cvars, names[i]);
        if (global && global->type == QA_QC_FLOAT && !qa_qc_set_global_float(vm, global->offset, cvar->number, error)) return false;
    }
    const qa_qc_definition *mapname = qa_qc_program_find_global(engine->provider->state.qc.program, "mapname");
    if (mapname && mapname->type == QA_QC_STRING) {
        const char *base = strncmp(path, "maps/", 5) == 0 ? path + 5 : path;
        size_t length = strlen(base); char *short_name = malloc(length + 1);
        if (!short_name) return application_fail(error, QA_ERROR_MEMORY, "Allocating QC component map name");
        memcpy(short_name, base, length + 1);
        if (length >= 4 && strcmp(short_name + length - 4, ".bsp") == 0) short_name[length - 4] = 0;
        int32_t string; size_t capacity = length + 1; if (capacity < 128) capacity = 128;
        bool ok = qa_qc_engine_string(vm, "component-mapname", short_name, capacity, &string, error) &&
            qa_qc_set_global_int(vm, mapname->offset, string, error);
        free(short_name); if (!ok) return false;
    }
    if (!application_qc_initialize_declared(engine, error)) return false;
    if (!application_qc_flush(engine, error) || !qa_qc_game_loading(engine->provider->state.qc.game, false, error)) return false;
    engine->loading = false; qa_cvars_set_server_active(engine->cvars, true);
    return application_qc_objectives_activate(engine,error);
}
bool application_qc_load_declared_map(struct application_qc_state *engine, const qa_bsp_view *bsp,
                                        const qa_entities *entities, qa_string_id map, qa_string_id spawn, qa_error *error)
{
    (void)spawn;
    if (!engine || !bsp || !entities || !entities->count || engine->has_frame)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC component map initialization requires an idle shared map");
    return initialize_declared_map_context(engine, map, error);
}
bool application_qc_initialize_addition(application_provider *provider, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC
        ? provider->state.qc.engine : NULL;
    qa_application *app = provider ? provider->application : NULL;
    if (!engine || !provider->state.qc.qualified || !provider->state.qc.game ||
        !provider->state.qc.instance || !provider->constructed || !provider->attached ||
        provider->close_pending || !app || app->destroy_requested || !app->map_view_ready ||
        !app->map_resource || !app->geometry || !app->current_map ||
        engine->world != app->world || engine->services.session != app->session || engine->has_frame)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "QC addition initialization requires its actual published map owner");
    return (engine->initialized && !engine->loading) ||
        initialize_declared_map_context(engine, app->current_map, error);
}
bool application_qc_client_think(struct application_qc_state *engine, qa_actor_id actor,
                                  const qa_source_frame *frame, qa_error *error)
{
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    const qa_qc_definition *think = NULL, *deadline = NULL;
    for (size_t i = 0; i < profile->field_count; ++i) {
        if (profile->fields[i].kind == QC_FIELD_THINK) think = profile->fields[i].definition;
        if (profile->fields[i].kind == QC_FIELD_NEXTTHINK) deadline = profile->fields[i].definition;
    }
    if (!think) {
        think = qa_qc_program_find_field(engine->provider->state.qc.program, "think");
        deadline = qa_qc_program_find_field(engine->provider->state.qc.program, "nextthink");
    }
    if (!think && !deadline) return true;
    if (!think || !deadline || think->type != QA_QC_FUNCTION || deadline->type != QA_QC_FLOAT)
        return application_fail(error, QA_ERROR_FORMAT, "QC component think fields have invalid types");
    qa_qc_instance *vm = engine->provider->state.qc.instance;
    int32_t reference;
    if (!application_qc_reference(engine, actor, &reference, error)) return false;
    double now = (double)frame->time_ns / 1e9, end = now + (double)frame->elapsed_ns / 1e9;
    while (qa_actors_get(qa_session_actors(engine->services.session), actor)) {
        bool admitted = false;
        for (uint32_t i = 1; i <= engine->max_clients; ++i)
            admitted |= engine->clients[i].spawned && qa_actor_id_equal(engine->clients[i].actor, actor);
        if (!admitted) return true;
        float due; int32_t function;
        if (!qa_qc_entity_float(vm, reference, deadline->offset, &due, error) ||
            !qa_qc_entity_int(vm, reference, think->offset, &function, error)) return false;
        if (!isfinite(due)) return application_fail(error, QA_ERROR_FORMAT, "QC component think deadline is nonfinite");
        if (!(due > 0) || (double)due > end) return true;
        float callback_time = (float)fmax((double)due, now);
        qa_qc_game_global globals[3] = {
            {"self", {QA_QC_GAME_ACTOR, {.actor = actor}}},
            {"other", {QA_QC_GAME_ACTOR, {.actor = {0}}}},
            {"time", {QA_QC_GAME_FLOAT, {.number = callback_time}}}
        };
        if (!qa_qc_project_entity_float(vm, reference, deadline->offset, 0, error) || function <= 0) {
            if (error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "QC component think callback is absent");
            return false;
        }
        const float *previous_time = engine->client_think_time;
        engine->client_think_time = &callback_time;
        bool invoked = qa_qc_game_call_index(engine->provider->state.qc.game, (uint32_t)function,
            NULL, 0, globals, 3, NULL, error);
        engine->client_think_time = previous_time;
        if (!invoked || !application_qc_publish_client_outputs(engine, error)) return false;
        if (engine->profile != QA_QC_QUAKEWORLD) return true;
    }
    return true;
}
