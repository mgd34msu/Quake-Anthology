#include "guest_qc_profile.h"
#include "guest_qc_items.h"
#include "guest_qc_pickups.h"
#include "guest_qc_item_weapons.h"
#include "guest_qc_combat.h"
#include "guest_qc_protection.h"
#include "guest_qc_objectives.h"
#include "guest_qc_original_save.h"
#include "guest_qc_rerelease.h"
#include "guest_qc_factory.h"
#include "guest_qc_spawn.h"
#include "bots_npc.h"
#include "startup_flow.h"
#include "control_frame.h"
#include <float.h>
#include <stdio.h>

const qa_qc_definition *application_qc_field(struct application_qc_state *engine,
                                            const char *name, qa_qc_value_type type, qa_error *error)
{
    const qa_qc_definition *field = qa_qc_program_find_field(engine->provider->state.qc.program, name);
    if (field == NULL || field->type != type) {
        application_fail(error, QA_ERROR_FORMAT, "QuakeC engine field is missing or has a different type");
        return NULL;
    }
    return field;
}
bool application_qc_float(struct application_qc_state *engine, int32_t reference,
                           const char *name, float *out, qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine, name, QA_QC_FLOAT, error);
    return field != NULL && qa_qc_entity_float(engine->provider->state.qc.instance,
                                               reference, field->offset, out, error);
}
bool application_qc_set_float(struct application_qc_state *engine, int32_t reference,
                               const char *name, float value, qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine, name, QA_QC_FLOAT, error);
    return field != NULL && qa_qc_set_entity_float(engine->provider->state.qc.instance,
                                                   reference, field->offset, value, error);
}
bool application_qc_reference(struct application_qc_state *engine, qa_actor_id actor,
                               int32_t *out, qa_error *error)
{
    return qa_qc_actor_reference(engine->provider->state.qc.instance, actor, true, out, error);
}
static bool source_time(struct application_qc_state *engine, double seconds, qa_error *error)
{
    const qa_qc_definition *time = qa_qc_program_find_global(engine->provider->state.qc.program, "time");
    float value = (float)seconds;
    uint32_t word; memcpy(&word, &value, sizeof(word));
    if (!time || time->type != QA_QC_FLOAT)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC callback source time is missing");
    return qa_qc_stage_globals(engine->provider->state.qc.instance, time->offset, &word, 1, error);
}
bool application_qc_named(struct application_qc_state *engine, const char *name,
                           qa_actor_id actor, qa_error *error)
{
    if (!engine->provider->state.qc.qualified &&
        !source_time(engine, (double)engine->source_time_ns / 1e9, error)) return false;
    qa_qc_game_global globals[2] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = actor}}},
        {"other", {QA_QC_GAME_ACTOR, {.actor = {0}}}}
    };
    return qa_qc_game_call(engine->provider->state.qc.game, name, NULL, 0,
                            globals, 2, NULL, error) && application_qc_publish_client_outputs(engine, error);
}
bool application_qc_spectator_callback(struct application_qc_state *engine, const char *name,
                                       qa_actor_id actor, qa_error *error)
{
    if (engine->profile != QA_QC_QUAKEWORLD)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "QuakeC spectators require QuakeWorld");
    uint32_t index;
    if (!qa_qc_program_find_function(engine->provider->state.qc.program, name, &index) || index == 0) return true;
    return application_qc_named(engine, name, actor, error);
}
static char *copy_text(const char *text, qa_error *error)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    else application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC resource name");
    return copy;
}
void application_qc_resource_dispose(application_qc_resource *entry)
{
    qa_collision_destroy(entry->geometry);
    qa_vfs_acquisition_dispose(&entry->acquisition);
    qa_resource_release(entry->source);
    free(entry->name);
    *entry = (application_qc_resource){0};
}
bool application_qc_resource_resolve_model(application_qc_resource *entry, qa_error *error)
{
    entry->has_inline_model = false; entry->inline_model = 0;
    if (entry->kind != QA_QC_RESOURCE_MODEL) return true;
    if (entry->source) {
        qa_bytes bytes = qa_resource_bytes(entry->source);
        if (!qa_bsp_probe(bytes, NULL, NULL)) return true;
        if (!entry->geometry) {
            qa_bsp_view map;
            if (!qa_bsp_open(bytes, &map, error) ||
                !qa_collision_create(&map, &entry->geometry, error) ||
                !qa_collision_bind_resource(entry->geometry, entry->source, error)) return false;
        }
        return qa_collision_model_bounds(entry->geometry, 0, &entry->value.bounds, error);
    }
    if (!entry->world_model) {
        double model;
        if (!entry->name || *entry->name != '*' ||
            !qa_parse_number((qa_bytes){(const uint8_t *)entry->name + 1, strlen(entry->name + 1)}, &model, error) ||
            !isfinite(model) || model < 0 || model > UINT32_MAX || trunc(model) != model)
            return application_fail(error, QA_ERROR_FORMAT, "Invalid QuakeC inline model number");
        entry->inline_model = (uint32_t)model;
    }
    entry->has_inline_model = true;
    return true;
}
bool application_qc_resource_lookup(void *opaque, qa_qc_resource_kind kind,
                                      const char *name, bool precache,
                                      qa_qc_game_resource *out, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (name == NULL || out == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC resource needs a name and output");
    if (*name == '\0') { *out = (qa_qc_game_resource){0}; return true; }
    uint32_t index = 1;
    for (size_t i = 0; i < engine->resource_count; ++i) {
        application_qc_resource *entry = &engine->resources[i];
        if (entry->kind != kind) continue;
        if (strcmp(entry->name, name) == 0) { *out = entry->value; return true; }
        ++index;
    }
    if (!precache || !engine->loading)
        return application_fail(error, QA_ERROR_NOT_FOUND, "QuakeC resource was not precached");
    if (index >= (engine->profile == QA_QC_RERELEASE ? 65536u : 256u))
        return application_fail(error, QA_ERROR_MEMORY, "QuakeC source precache table is full");
    application_qc_resource entry = {.kind = kind, .value.index = index};
    entry.name = copy_text(name, error);
    if (entry.name == NULL) return false;
    bool ok = true;
    if (kind == QA_QC_RESOURCE_MODEL && *name == '*') {
        ok = application_qc_resource_resolve_model(&entry, error) &&
            qa_collision_model_bounds(qa_world_geometry(engine->world), entry.inline_model, &entry.value.bounds, error);
    } else {
        char *sound_path=NULL;
        if (kind==QA_QC_RESOURCE_SOUND) {
            size_t length=strlen(name);
            if (length>SIZE_MAX-7 || !(sound_path=malloc(length+7)))
                ok=application_fail(error,QA_ERROR_MEMORY,"Allocating source sound asset path");
            else { memcpy(sound_path,"sound/",6); memcpy(sound_path+6,name,length+1); }
        }
        if (ok) ok = qa_vfs_acquire_receipt(engine->provider->launch->content, sound_path?sound_path:name,
            &entry.source,&entry.acquisition,error);
        free(sound_path);
        if (ok && kind == QA_QC_RESOURCE_MODEL) {
            ok = application_qc_resource_resolve_model(&entry, error);
            if (ok && !entry.geometry) {
                qa_model model = {0};
                ok = qa_model_load(qa_resource_bytes(entry.source), &model, error);
                if (ok) entry.value.bounds = model.format==QA_MODEL_MDL?
                    (qa_bounds){qa_v3(-16,-16,-16),qa_v3(16,16,16)}:(qa_bounds){
                    qa_v3(model.bounds.min[0], model.bounds.min[1], model.bounds.min[2]),
                    qa_v3(model.bounds.max[0], model.bounds.max[1], model.bounds.max[2])};
                qa_model_free(&model);
            }
        }
    }
    if (ok && engine->resource_count == engine->resource_capacity) {
        size_t capacity = engine->resource_capacity ? engine->resource_capacity * 2 : 32;
        if (capacity < engine->resource_capacity || capacity > SIZE_MAX / sizeof(*engine->resources))
            ok = application_fail(error, QA_ERROR_MEMORY, "QuakeC precache allocation overflow");
        else {
            application_qc_resource *entries = realloc(engine->resources, capacity * sizeof(*entries));
            if (entries == NULL) ok = application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC precaches");
            else { engine->resources = entries; engine->resource_capacity = capacity; }
        }
    }
    if (!ok) {
        application_qc_resource_dispose(&entry); return false;
    }
    engine->resources[engine->resource_count++] = entry;
    *out = entry.value;
    return true;
}
static qa_command_result server_command(void *opaque, const qa_command_invocation *command, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    for (size_t i = 0; profile && i < profile->command_count; ++i)
        if (command->argc && application_qc_command_name_equal(profile->commands[i].name, command->argv[0]))
            return application_qc_declared_command(engine, command, error) ? QA_COMMAND_HANDLED : QA_COMMAND_FAILED;
    qa_command_result common = application_startup_common_command(engine->provider,
        engine->console, engine->cvars, command, error);
    if (common != QA_COMMAND_UNHANDLED) return common;
    bool handled = false;
    if (!application_qc_host_command(engine->provider, command, &handled, error))
        return QA_COMMAND_FAILED;
    if (handled) return QA_COMMAND_HANDLED;
    if (!command->argc ||
        (!application_qc_command_name_equal(command->argv[0], "map") &&
         !application_qc_command_name_equal(command->argv[0], "gamemap") &&
         !application_qc_command_name_equal(command->argv[0], "changelevel")))
        return QA_COMMAND_UNHANDLED;
    qa_string_id text;
    if (!qa_strings_intern_cstr(qa_session_strings(engine->services.session), command->raw, &text, error) ||
        !application_map_server_command(engine->provider, text, error)) return QA_COMMAND_FAILED;
    return QA_COMMAND_HANDLED;
}
static bool read_script(void *opaque, const qa_command_context *context, const char *path,
                         qa_bytes *out, void **lease, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (application_startup_source_active(engine->provider))
        return application_startup_script_read(engine->provider, context, path, out, lease, error);
    if (application_startup_source_scripts(engine->provider))
        return application_startup_source_script_read(engine->provider, engine->console,
            context, path, out, lease, error);
    qa_resource *resource;
    if (!qa_vfs_acquire(engine->provider->launch->content, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource); *lease = resource; return true;
}
static void release_script(void *opaque, void *lease)
{
    struct application_qc_state *engine = opaque;
    if (application_startup_source_active(engine->provider))
        application_startup_script_release(engine->provider, lease);
    else if (application_startup_source_scripts(engine->provider))
        application_startup_source_script_release(engine->provider, engine->cvars, lease);
    else qa_resource_release(lease);
}
static void script_complete(void *opaque, const qa_command_context *context,
    const char *path, bool success)
{
    struct application_qc_state *engine = opaque;
    application_startup_script_complete(engine->provider, context, path, success);
}
static bool allow_command(void *opaque, const qa_command_invocation *command)
{
    struct application_qc_state *engine = opaque;
    return application_startup_command_allowed(engine->provider, command);
}
static bool capture_context(void *opaque, const qa_command_context *source,
                              qa_command_context *out, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    return qa_application_capture_command_context(engine->provider->application, source, out, error);
}
static bool context_active(void *opaque, const qa_command_context *context)
{
    struct application_qc_state *engine = opaque;
    return qa_application_command_context_active(engine->provider->application, context);
}
static void console_print(void *opaque, const qa_command_context *context, const char *text)
{
    struct application_qc_state *engine = opaque;
    application_console_print(engine->provider->application, context, text);
}
bool application_qc_create_console(struct application_qc_state *engine, qa_cvars *cvars,
    qa_console **out, qa_error *error)
{
    if (!engine || !engine->provider || !cvars || !out || *out)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC console construction requires its owner, registry and empty output");
    qa_console *previous_console = engine->console;
    qa_cvars *previous_cvars = engine->cvars;
    qa_console_options options = {.context = engine->command_context, .cvars = cvars,
        .user = engine, .print = console_print, .source_command = server_command, .read_script = read_script,
        .release_script = release_script, .script_complete = script_complete, .allow_command = allow_command,
        .capture_context = capture_context, .context_active = context_active};
    engine->command_context.cvar_view = qa_cvars_view_identity(cvars);
    options.context = engine->command_context;
    qa_console *console = engine->provider->application->console;
    if (!qa_console_bind_source(console, &options, error)) return false;
    if (!console) return false;
    engine->console = console; engine->cvars = cvars;
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    qa_application *application = engine->provider->application;
    application_provider *previous_provider = application->startup_preinit_provider;
    if (application->operation == APPLICATION_PERSISTING)
        application->startup_preinit_provider = engine->provider;
    bool ok = true;
    for (size_t i = 0; ok && profile && i < profile->command_count; ++i)
        if (!qa_console_register_context(console, &options.context, profile->commands[i].name, "Declared QuakeC command",
            engine->provider->owner, engine->provider->owner, false, application_qc_declared_command, engine, error)) {
            ok = false;
        }
    application->startup_preinit_provider = previous_provider;
    engine->console = previous_console; engine->cvars = previous_cvars;
    if (!ok && qa_console_unbind_source(console, qa_cvars_view_identity(cvars), error)) console = NULL;
    *out = console;
    return ok;
}
static bool source_callback(struct application_qc_state *engine, qa_actor_id actor,
                             qa_actor_id other, const char *name, double time_seconds, qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine, name, QA_QC_FUNCTION, error);
    int32_t reference, function;
    if (!field || !application_qc_reference(engine, actor, &reference, error) ||
        !qa_qc_entity_int(engine->provider->state.qc.instance, reference, field->offset, &function, error)) return false;
    if (!function) return true;
    if (function < 0) return application_fail(error, QA_ERROR_FORMAT, "QuakeC callback function is invalid");
    float seconds = (float)time_seconds;
    bool scoped_time = engine->provider->state.qc.qualified != NULL;
    if (!scoped_time && !source_time(engine, time_seconds, error)) return false;
    qa_qc_game_global globals[3] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = actor}}},
        {"other", {QA_QC_GAME_ACTOR, {.actor = other}}},
        {"time", {QA_QC_GAME_FLOAT, {.number = seconds}}}
    };
    return qa_qc_game_call_index(engine->provider->state.qc.game, (uint32_t)function,
        NULL, 0, globals, scoped_time ? 3 : 2, NULL, error) && application_qc_publish_client_outputs(engine, error);
}
static bool schedule_think_after(struct application_qc_state *, qa_actor_id, uint64_t, qa_error *);
static bool source_think(void *opaque, qa_actor_id actor, const qa_think_scope *scope, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (!scope || scope->kind != QA_THINK_WORLD_FRAME)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC scheduled think requires an actual world frame");
    const qa_source_frame *frame = &scope->source.frame;
    if (frame->provider != engine->provider->owner || frame->kind != engine->provider->component.clock.kind ||
        frame->start_ns > UINT64_MAX - frame->elapsed_ns)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC scheduled think has another source clock");
    int32_t reference; float due;
    if (!application_qc_reference(engine, actor, &reference, error) ||
        !application_qc_float(engine, reference, "nextthink", &due, error)) return false;
    if (!isfinite(due)) return application_fail(error, QA_ERROR_FORMAT, "Nonfinite QuakeC think deadline");
    if (!(due > 0)) return true;
    double current = (double)frame->time_ns / 1e9;
    double end = current + (double)frame->elapsed_ns / 1e9;
    if ((double)due > end) {
        uint64_t end_ns = frame->start_ns + frame->elapsed_ns;
        return end_ns == UINT64_MAX || schedule_think_after(engine, actor, end_ns + 1, error);
    }
    qa_actor_owner execution;
    if (!qa_session_execution(engine->services.session, actor, &execution) || execution != engine->provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC think actor changed execution owner");
    if (!application_qc_set_float(engine, reference, "nextthink", 0, error)) return false;
    return source_callback(engine, actor, (qa_actor_id){0}, "think", fmax(current, (double)due), error);
}
bool application_qc_think_binding(application_provider *provider,qa_actor_id actor,uint32_t callback_id,
                                   qa_think_fn *callback,void **context,qa_error *error)
{
    if(!provider || provider->kind!=APPLICATION_PROVIDER_QC || !provider->state.qc.engine ||
       !callback || !context || callback_id!=1)
        return application_fail(error,QA_ERROR_FORMAT,"saved QuakeC think adapter identity is invalid");
    int32_t reference;
    if(!application_qc_reference(provider->state.qc.engine,actor,&reference,error)) return false;
    const qa_qc_definition *field=application_qc_field(provider->state.qc.engine,"think",QA_QC_FUNCTION,error);
    int32_t function;
    if(!field || !qa_qc_entity_int(provider->state.qc.instance,reference,field->offset,&function,error)) return false;
    qa_qc_program_info info=qa_qc_program_describe(provider->state.qc.program);
    if(function<0 || (uint32_t)function>=info.function_count)
        return application_fail(error,QA_ERROR_FORMAT,"saved QuakeC think field names no original function");
    *callback=source_think;*context=provider->state.qc.engine;return true;
}
static bool schedule_think_after(struct application_qc_state *engine, qa_actor_id actor,
                                   uint64_t minimum_ns, qa_error *error)
{
    int32_t reference; float due;
    if (!application_qc_reference(engine, actor, &reference, error) ||
        !application_qc_float(engine, reference, "nextthink", &due, error)) return false;
    if (!isfinite(due)) return application_fail(error, QA_ERROR_FORMAT, "Nonfinite QuakeC think deadline");
    if (!(due > 0)) { qa_scheduler_cancel(qa_session_scheduler(engine->services.session), actor); return true; }
    double ns = (double)due * 1e9;
    if (ns >= (double)UINT64_MAX) {
        qa_scheduler_cancel(qa_session_scheduler(engine->services.session), actor);
        return true;
    }
    uint64_t time = (uint64_t)ceil(ns);
    if (time < minimum_ns) time = minimum_ns;
    qa_think think = {.actor = actor, .execution_provider = engine->provider->owner,
        .callback_id = 1,
        .due_ns = time, .boundary = QA_THINK_DURING_PHYSICS, .callback = source_think, .context = engine};
    return qa_session_schedule(engine->services.session, &think, error);
}
static bool schedule_think(struct application_qc_state *engine, qa_actor_id actor, qa_error *error)
{
    return schedule_think_after(engine, actor, 0, error);
}
static bool stored(void *opaque, qa_qc_instance *vm, const qa_qc_store_event *event, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (!application_qc_combat_source_stored(engine, vm, event, error)) return false;
    if (!application_qc_items_source_stored(engine, vm, event, error)) return false;
    if (!application_qc_store_declared(engine, vm, event, error)) return false;
    if (!engine->provider->state.qc.qualified && !application_qc_project_body_store(engine, vm, event, error)) return false;
    if (event->kind != QA_QC_STORE_ENTITY || event->entity_reference == 0) return true;
    if (!engine->has_frame) {
        qa_source_command command;
        if (!qa_session_active_command(engine->services.session, engine->provider->owner, &command)) return true;
    }
    const qa_qc_definition *next = qa_qc_program_find_field(engine->provider->state.qc.program, "nextthink");
    if (next == NULL || next->offset < event->word || next->offset >= event->word + event->count) return true;
    qa_actor_id actor; qa_actor_owner execution;
    if (!qa_qc_reference_actor(vm, event->entity_reference, &actor, error)) return false;
    if (!qa_session_execution(engine->services.session, actor, &execution) || execution != engine->provider->owner) return true;
    return schedule_think(engine, actor, error);
}
static bool prepare_frame(void *opaque, qa_session *session, const qa_source_frame *frame, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (session != engine->services.session)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC frame belongs to another session");
    engine->source_time_ns = frame->time_ns;
    engine->frame = *frame; engine->has_frame = true;
    if (!qa_qc_game_set_time(engine->provider->state.qc.game, (double)frame->time_ns / 1e9,
                              (double)frame->elapsed_ns / 1e9, error)) return false;
    return true;
}
static bool begin_frame(void *opaque, qa_session *session, const qa_source_frame *frame, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (session != engine->services.session)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC frame belongs to another session");
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (profile) {
        application_qc_inputs inputs = {.time_ns = frame->time_ns, .elapsed_ns = frame->elapsed_ns};
        if (!application_qc_prepare_markers(engine, error) || !application_qc_run_calls(engine, &profile->frame, &inputs, error)) return false;
        for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
            application_qc_client *client = &engine->clients[slot];
            if (!client->spawned || !qa_actors_get(qa_session_actors(session), client->actor)) continue;
            inputs.self = client->actor;
            if (!application_qc_client_think(engine, client->actor, frame, error)) return false;
            if (!client->spawned || !qa_actors_get(qa_session_actors(session), inputs.self)) continue;
            if (!application_qc_weapon_before_postthink(engine,inputs.self,error) ||
                !application_qc_run_calls(engine, &profile->client_frame, &inputs, error) ||
                !application_qc_weapon_after_postthink(engine,inputs.self,error)) return false;
        }
        return true;
    }
    return application_qc_named(engine, "StartFrame", (qa_actor_id){0}, error);
}
static bool control_current(struct application_qc_state *engine, int32_t reference,
                              qa_actor_id actor, qa_error *error)
{
    qa_actor_id current;
    return qa_qc_reference_actor(engine->provider->state.qc.instance, reference, &current, error) &&
        (qa_actor_id_equal(current, actor) ||
         application_fail(error, QA_ERROR_NOT_FOUND, "QC control actor changed generation"));
}
bool application_qc_control_input_active(const application_provider *provider)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_QC || !provider->state.qc.qualified) return false;
    return provider->state.qc.qualified->input_count != 0 ||
        (provider->state.qc.engine && provider->state.qc.engine->output_channels != 0);
}
bool application_qc_control_receipt_time(const application_provider *provider,
    qa_actor_id actor, uint64_t *out, qa_error *error)
{
    const struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC
        ? provider->state.qc.engine : NULL;
    if (!engine || !out || !engine->initialized || !provider->constructed ||
        !provider->attached || provider->close_pending || engine->profile != QA_QC_QUAKEWORLD ||
        provider->component.clock.kind != QA_CLOCK_QUAKEWORLD ||
        application_world_provider(provider->application, QA_ROLE_ENTITIES, "") != provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW receipt time requires its actual physical source owner");
    bool member;
    if (!application_qc_control_source_client(provider, actor, &member, error)) return false;
    if (!member)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW receipt time requires an admitted physical source client");
    *out = engine->source_time_ns;
    return true;
}
bool application_qc_control_reserved(application_provider *provider, qa_actor_id actor,
                                       bool *reserved, qa_error *error)
{
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ?
        provider->state.qc.engine : NULL;
    if (!engine || !provider->state.qc.instance || !reserved)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC reserved client qualification has no source owner");
    *reserved = false;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), actor);
    if (!record) return true;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
        qa_qc_slot_binding binding;
        if (!qa_qc_slot(provider->state.qc.instance, slot, &binding) ||
            (binding.kind != QA_QC_SLOT_OWNED && binding.kind != QA_QC_SLOT_BORROWED) ||
            !qa_actor_id_equal(binding.actor, actor)) continue;
        const application_qc_client *client = &engine->clients[slot];
        if (binding.owner != record->owner || binding.source_slot != (record->has_source ? record->source_slot : 0) ||
            (binding.kind == QA_QC_SLOT_BORROWED ?
             !client->connected || !qa_actor_id_equal(client->actor, actor) :
             provider->state.qc.qualified || engine->profile != QA_QC_QUAKEWORLD ||
             record->owner != provider->owner || !record->has_source || record->source_slot != slot))
            return application_fail(error, QA_ERROR_FORMAT, "QC reserved client differs from its source slot owner");
        *reserved = true;
        return true;
    }
    return true;
}
static bool control_client(struct application_qc_state *engine, qa_actor_id actor,
                             int32_t *reference, bool *spectator, qa_error *error)
{
    for (uint32_t i = 1; i <= engine->max_clients; ++i) {
        application_qc_client *client = &engine->clients[i];
        if (!client->spawned || !qa_actor_id_equal(client->actor, actor)) continue;
        *spectator = client->spectator;
        return application_qc_reference(engine, actor, reference, error);
    }
    return application_fail(error, QA_ERROR_NOT_FOUND, "QC control requires an admitted source client");
}
static bool control_scalar(struct application_qc_state *engine, int32_t reference, qa_actor_id actor,
                             const char *name, float *value, qa_error *error)
{
    return application_qc_float(engine, reference, name, value, error) &&
        (isfinite(*value) || application_fail(error, QA_ERROR_FORMAT, "QC control scalar is nonfinite")) &&
        control_current(engine, reference, actor, error);
}
static bool control_vector(struct application_qc_state *engine, int32_t reference, qa_actor_id actor,
                             const char *name, qa_vec3 *value, qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine, name, QA_QC_VECTOR, error);
    return field && qa_qc_entity_vector(engine->provider->state.qc.instance, reference, field->offset, value, error) &&
        (qa_vec_finite(*value) || application_fail(error, QA_ERROR_FORMAT, "QC control vector is nonfinite")) &&
        control_current(engine, reference, actor, error);
}
static bool control_store_scalar(struct application_qc_state *engine, int32_t reference, qa_actor_id actor,
                                   const char *name, float value, qa_error *error)
{
    return control_current(engine, reference, actor, error) &&
        application_qc_set_float(engine, reference, name, value, error) &&
        control_current(engine, reference, actor, error);
}
static bool control_store_vector(struct application_qc_state *engine, int32_t reference, qa_actor_id actor,
                                   const char *name, qa_vec3 value, qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine, name, QA_QC_VECTOR, error);
    return field && control_current(engine, reference, actor, error) &&
        qa_qc_set_entity_vector(engine->provider->state.qc.instance, reference, field->offset, value, error) &&
        control_current(engine, reference, actor, error);
}
static bool control_integer(float value, int32_t *out, qa_error *error)
{
    if (!isfinite(value) || (double)value < INT32_MIN || (double)value > INT32_MAX) {
        application_fail(error, QA_ERROR_FORMAT, "QC control integer exceeds source bounds");
        return false;
    }
    *out = (int32_t)value;
    return true;
}
static float control_flags(uint32_t bits)
{
    int32_t signed_bits; memcpy(&signed_bits, &bits, sizeof(bits));
    return (float)signed_bits;
}
static bool control_word(double value, float *out, qa_error *error)
{
    if (!isfinite(value) || fabs(value) >= 0x1.ffffffp127) {
        application_fail(error, QA_ERROR_FORMAT, "QC control value exceeds finite source float bounds");
        return false;
    }
    *out = value > FLT_MAX ? FLT_MAX : value < -FLT_MAX ? -FLT_MAX : (float)value;
    return true;
}
bool application_qc_control_state(application_provider *provider, qa_actor_id actor,
                                    qa_movement_state *state, qa_bounds *bounds,
                                    qa_movement_environment *environment, qa_vec3 *view_angles,
                                    qa_error *error)
{
    struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    if (!engine || !state) return application_fail(error, QA_ERROR_ARGUMENT, "QC control state is absent");
    if (provider->state.qc.qualified) return true;
    int32_t reference; bool spectator;
    if (!control_client(engine, actor, &reference, &spectator, error)) return false;
    qa_vec3 origin, velocity, angles, view, minimum, maximum;
    float health, motion, fix; int32_t move_type;
    if (!control_vector(engine, reference, actor, "origin", &origin, error) ||
        !control_vector(engine, reference, actor, "velocity", &velocity, error) ||
        !control_vector(engine, reference, actor, "angles", &angles, error) ||
        !control_vector(engine, reference, actor, "v_angle", &view, error) ||
        !control_vector(engine, reference, actor, "mins", &minimum, error) ||
        !control_vector(engine, reference, actor, "maxs", &maximum, error) ||
        !control_scalar(engine, reference, actor, "health", &health, error) ||
        !control_scalar(engine, reference, actor, "movetype", &motion, error) ||
        !control_scalar(engine, reference, actor, "fixangle", &fix, error) ||
        !control_integer(motion, &move_type, error)) return false;
    if (!qa_movement_set_origin(state, origin, error) || !qa_movement_set_velocity(state, velocity, error)) return false;
    if (bounds) *bounds = (qa_bounds){minimum, maximum};
    if (view_angles) *view_angles = fix != 0 && engine->profile != QA_QC_QUAKEWORLD ? angles : view;
    if (environment) {
        environment->health = health;
        environment->has_body_bounds = true; environment->body_bounds = (qa_bounds){minimum, maximum};
    }
    if (state->kind == QA_MOVEMENT_NETQUAKE && engine->profile != QA_QC_QUAKEWORLD) {
        qa_nq_movement_state *nq = &state->data.nq;
        float flags, level, type, teleport, ideal; int32_t flag_bits;
        nq->angles = angles; nq->view_angles = fix != 0 ? angles : view;
        nq->move_type = spectator ? 8 : move_type; nq->fix_angle = fix != 0; nq->health = health;
        if (!control_vector(engine, reference, actor, "oldorigin", &nq->old_origin, error) ||
            !control_vector(engine, reference, actor, "avelocity", &nq->angular_velocity, error) ||
            !control_vector(engine, reference, actor, "punchangle", &nq->punch_angles, error) ||
            !control_vector(engine, reference, actor, "movedir", &nq->water_jump_direction, error) ||
            !control_scalar(engine, reference, actor, "flags", &flags, error) ||
            !control_scalar(engine, reference, actor, "waterlevel", &level, error) ||
            !control_scalar(engine, reference, actor, "watertype", &type, error) ||
            !control_scalar(engine, reference, actor, "teleport_time", &teleport, error) ||
            !control_scalar(engine, reference, actor, "idealpitch", &ideal, error) ||
            !control_integer(flags, &flag_bits, error) ||
            !control_integer(level, &nq->water_level, error) ||
            !control_integer(type, &nq->water_type, error)) return false;
        nq->flags = (uint32_t)flag_bits; nq->teleport_time_seconds = teleport; nq->ideal_pitch = ideal;
    } else if (state->kind == QA_MOVEMENT_QUAKEWORLD && engine->profile == QA_QC_QUAKEWORLD) {
        qa_qw_movement_state *qw = &state->data.qw; float teleport;
        qw->origin.x = (double)origin.x + minimum.x + 16;
        qw->origin.y = (double)origin.y + minimum.y + 16;
        qw->origin.z = (double)origin.z + minimum.z + 24;
        qw->angles = view; qw->dead = health <= 0; qw->spectator = spectator ? 1 : 0;
        if (!control_scalar(engine, reference, actor, "teleport_time", &teleport, error)) return false;
        qw->water_jump_time_seconds = teleport;
    }
    if (spectator) {
        if (state->kind == QA_MOVEMENT_NETQUAKE) state->data.nq.move_type = 8;
        else if (state->kind == QA_MOVEMENT_QUAKEWORLD) state->data.qw.spectator = 1;
        else if (state->kind == QA_MOVEMENT_Q2_CLASSIC) state->data.q2.type = 1;
        else if (state->kind == QA_MOVEMENT_Q2_RERELEASE) state->data.q2r.type = 2;
        else if (state->kind == QA_MOVEMENT_Q3) state->data.q3.movement_type = 1;
    }
    return control_current(engine, reference, actor, error);
}
static bool control_ground(struct application_qc_state *engine, int32_t reference, qa_actor_id actor,
                             qa_movement_ground ground, bool only_grounded, qa_error *error)
{
    const qa_qc_game_fields *fields = qa_qc_program_resolved_fields(engine->provider->state.qc.program);
    const qa_qc_definition *flags_field = fields->flags, *ground_field = fields->groundentity;
    float flags; int32_t bits;
    if (!flags_field || flags_field->type != QA_QC_FLOAT ||
        !ground_field || ground_field->type != QA_QC_ENTITY)
        return application_fail(error, QA_ERROR_FORMAT, "QC control ground fields differ");
    if (!qa_qc_entity_float(engine->provider->state.qc.instance, reference, flags_field->offset, &flags, error) ||
        !control_integer(flags, &bits, error) || !control_current(engine, reference, actor, error)) return false;
    uint32_t next = ((uint32_t)bits & ~512u) | (ground.hit != QA_TRACE_HIT_NONE ? 512u : 0u);
    if (next != (uint32_t)bits &&
        (!qa_qc_set_entity_float(engine->provider->state.qc.instance, reference, flags_field->offset,
            control_flags(next), error) || !control_current(engine, reference, actor, error))) return false;
    if (only_grounded && ground.hit == QA_TRACE_HIT_NONE) return true;
    int32_t other = 0;
    if (ground.hit == QA_TRACE_HIT_ACTOR && !application_qc_reference(engine, ground.actor, &other, error)) return false;
    return control_current(engine, reference, actor, error) &&
        qa_qc_set_entity_int(engine->provider->state.qc.instance, reference, ground_field->offset, other, error) &&
        control_current(engine, reference, actor, error);
}
bool application_qc_control_body(application_provider *provider, qa_actor_id actor,
                                   const qa_movement_state *state, qa_movement_ground ground,
                                   qa_body_state *body, qa_error *error)
{
    struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    if (!engine || !state || !body)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC control body projection is absent");
    if (provider->state.qc.qualified) return true;
    int32_t reference; bool spectator; qa_vec3 minimum;
    if (!control_client(engine, actor, &reference, &spectator, error) ||
        !control_ground(engine, reference, actor, ground, true, error)) return false;
    if (ground.hit == QA_TRACE_HIT_WORLD)
        body->ground = qa_actor_reference_source(provider->owner, 0);
    if (engine->profile != QA_QC_QUAKEWORLD || state->kind != QA_MOVEMENT_QUAKEWORLD) return true;
    if (!control_vector(engine, reference, actor, "mins", &minimum, error) ||
        !control_vector(engine, reference, actor, "angles", &body->angles, error) ||
        !control_word((double)state->data.qw.origin.x - minimum.x - 16, &body->origin.x, error) ||
        !control_word((double)state->data.qw.origin.y - minimum.y - 16, &body->origin.y, error) ||
        !control_word((double)state->data.qw.origin.z - minimum.z - 24, &body->origin.z, error)) return false;
    return true;
}
bool application_qc_control_profile(application_provider *provider, qa_actor_id actor,
                                      qa_movement_profile *profile, qa_error *error)
{
    struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    if (!engine || !profile) return application_fail(error, QA_ERROR_ARGUMENT, "QC control profile is absent");
    if (provider->state.qc.qualified || engine->profile != QA_QC_QUAKEWORLD || profile->kind != QA_MOVEMENT_QUAKEWORLD)
        return true;
    int32_t reference; bool spectator;
    if (!control_client(engine, actor, &reference, &spectator, error)) return false;
    qa_q1_movement_parameters *parameters = &profile->data.qw.parameters;
    float *values[] = {&parameters->gravity, &parameters->stop_speed, &parameters->spectator_max_speed,
        &parameters->accelerate, &parameters->air_accelerate, &parameters->water_accelerate,
        &parameters->friction, &parameters->water_friction, &parameters->max_speed};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i) {
        const qa_cvar_view *variable = qa_cvars_read(engine->cvars, engine->cvar_handles.qw_movement[i]);
        *values[i] = variable ? variable->number : 0;
        if (!isfinite(*values[i])) return application_fail(error, QA_ERROR_FORMAT, "QC movement cvar is nonfinite");
    }
    parameters->entity_gravity = 1;
    const char *fields[] = {"maxspeed", "gravity"};
    float *outputs[] = {&parameters->max_speed, &parameters->entity_gravity};
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
        const qa_qc_definition *field = qa_qc_program_find_field(provider->state.qc.program, fields[i]);
        if (field && (!qa_qc_entity_float(provider->state.qc.instance, reference, field->offset, outputs[i], error) ||
            !isfinite(*outputs[i])))
            return application_fail(error, QA_ERROR_FORMAT, "QC movement field is invalid");
    }
    profile->data.qw.shared_controls = false;
    return control_current(engine, reference, actor, error);
}
static bool control_frame_time(struct application_qc_state *engine, float elapsed, qa_error *error)
{
    if (!isfinite(elapsed)) return application_fail(error, QA_ERROR_FORMAT, "QC control interval is nonfinite");
    const qa_qc_definition *field = qa_qc_program_find_global(engine->provider->state.qc.program, "frametime");
    uint32_t word; memcpy(&word, &elapsed, sizeof(word));
    return field && field->type == QA_QC_FLOAT ?
        qa_qc_stage_globals(engine->provider->state.qc.instance, field->offset, &word, 1, error) :
        application_fail(error, QA_ERROR_FORMAT, "QC control frametime is missing");
}
static bool control_store_state(struct application_qc_state *engine, int32_t reference, qa_actor_id actor,
                                  const qa_movement_call *call, qa_error *error)
{
    const qa_movement_state *state = call->state;
    if (state->kind == QA_MOVEMENT_NETQUAKE && engine->profile != QA_QC_QUAKEWORLD) {
        const qa_nq_movement_state *nq = &state->data.nq;
        float teleport;
        if (!control_word(nq->teleport_time_seconds, &teleport, error)) return false;
        const char *names[] = {"oldorigin", "avelocity", "v_angle", "punchangle", "movedir"};
        const qa_vec3 values[] = {nq->old_origin, nq->angular_velocity, nq->view_angles, nq->punch_angles, nq->water_jump_direction};
        for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
            if (!control_store_vector(engine, reference, actor, names[i], values[i], error)) return false;
        return control_store_scalar(engine, reference, actor, "movetype", (float)nq->move_type, error) &&
            control_store_scalar(engine, reference, actor, "flags", control_flags(nq->flags), error) &&
            control_store_scalar(engine, reference, actor, "waterlevel", (float)nq->water_level, error) &&
            control_store_scalar(engine, reference, actor, "watertype", (float)nq->water_type, error) &&
            control_store_scalar(engine, reference, actor, "teleport_time", teleport, error) &&
            control_store_scalar(engine, reference, actor, "idealpitch", nq->ideal_pitch, error) &&
            control_store_scalar(engine, reference, actor, "fixangle", nq->fix_angle ? 1 : 0, error);
    }
    if (state->kind == QA_MOVEMENT_QUAKEWORLD && engine->profile == QA_QC_QUAKEWORLD) {
        const qa_qw_movement_state *qw = &state->data.qw; qa_vec3 minimum;
        if (!control_vector(engine, reference, actor, "mins", &minimum, error)) return false;
        qa_vec3 origin;
        if (!control_word((double)qw->origin.x - minimum.x - 16, &origin.x, error) ||
            !control_word((double)qw->origin.y - minimum.y - 16, &origin.y, error) ||
            !control_word((double)qw->origin.z - minimum.z - 24, &origin.z, error)) return false;
        if (!control_store_vector(engine, reference, actor, "origin", origin, error) ||
            !control_store_vector(engine, reference, actor, "velocity", qw->velocity, error) ||
            !control_store_vector(engine, reference, actor, "v_angle", qw->angles, error) ||
            !control_store_scalar(engine, reference, actor, "teleport_time", qw->water_jump_time_seconds, error) ||
            !control_ground(engine, reference, actor, qw->ground, true, error)) return false;
    }
    return true;
}
static bool control_transition(struct application_qc_state *engine, int32_t reference, qa_actor_id actor,
                                 int32_t *flags, qa_vec3 *velocity, qa_error *error)
{
    float value;
    return control_scalar(engine, reference, actor, "flags", &value, error) &&
        control_integer(value, flags, error) && control_vector(engine, reference, actor, "velocity", velocity, error);
}
static bool control_jump(const qa_movement_command *command)
{
    return command->kind == QA_MOVEMENT_NETQUAKE || command->kind == QA_MOVEMENT_QUAKEWORLD ?
        (command->buttons & 2u) != 0 : command->kind == QA_MOVEMENT_Q2_RERELEASE ?
        (command->buttons & 8u) != 0 : command->up_move >= 10;
}
static void control_consume_jump(qa_movement_command *command)
{
    if (command->kind == QA_MOVEMENT_NETQUAKE || command->kind == QA_MOVEMENT_QUAKEWORLD)
        command->buttons &= ~2u;
    else if (command->kind == QA_MOVEMENT_Q2_RERELEASE) command->buttons &= ~8u;
    else if (command->up_move > 0) command->up_move = 0;
}
static bool control_clear_ground(struct application_qc_state *engine, qa_actor_id actor, qa_error *error)
{
    qa_body_state body;
    if (!qa_world_body_read(engine->world, actor, &body, error)) return false;
    body.ground = (qa_actor_reference){0};
    return qa_world_body_write(engine->world, actor, &body, error);
}
static bool control_qw_input(struct application_qc_state *engine, int32_t reference, qa_actor_id actor,
                               const qa_movement_command *command, qa_error *error)
{
    float fix, health;
    if(command->impulse) application_qc_weapon_command(engine,actor);
    if (!control_scalar(engine, reference, actor, "fixangle", &fix, error) ||
        !control_scalar(engine, reference, actor, "health", &health, error)) return false;
    if ((fix == 0 && !control_store_vector(engine, reference, actor, "v_angle", command->angles, error)) ||
        !control_store_scalar(engine, reference, actor, "button0", (command->buttons & 1u) ? 1.0f : 0.0f, error) ||
        !control_store_scalar(engine, reference, actor, "button2", control_jump(command) ? 1.0f : 0.0f, error) ||
        (command->impulse != 0 && !control_store_scalar(engine, reference, actor, "impulse", (float)command->impulse, error))) return false;
    if (health <= 0) return true;
    qa_vec3 angles, velocity;
    if (!control_vector(engine, reference, actor, "angles", &angles, error) ||
        !control_vector(engine, reference, actor, "velocity", &velocity, error)) return false;
    double pitch = fix == 0 ? -(double)command->angles.x / 3 : angles.x;
    double yaw = fix == 0 ? command->angles.y : angles.y;
    double scale = 3.14159265358979323846 * 2 / 360;
    double sp = sin(pitch * scale), cp = cos(pitch * scale);
    double sy = sin(yaw * scale), cy = cos(yaw * scale);
    double sr = sin((double)angles.z * scale), cr = cos((double)angles.z * scale);
    qa_vec3 right;
    if (!control_word(-sr * sp * cy + -cr * -sy, &right.x, error) ||
        !control_word(-sr * sp * sy + -cr * cy, &right.y, error) ||
        !control_word(-sr * cp, &right.z, error)) return false;
    double side = (double)velocity.x * right.x + (double)velocity.y * right.y + (double)velocity.z * right.z;
    double roll = (fabs(side) < 200 ? fabs(side) * 2 / 200 : 2) * (side < 0 ? -1 : 1) * 4;
    return control_word(pitch, &angles.x, error) && control_word(yaw, &angles.y, error) &&
        control_word(roll, &angles.z, error) && control_store_vector(engine, reference, actor, "angles", angles, error);
}
static bool control_mixed_water(struct application_qc_state *engine, int32_t reference, qa_actor_id actor,
                                  const qa_movement_call *call, qa_error *error)
{
    qa_movement_ground ground;
    if (call->state->kind == QA_MOVEMENT_NETQUAKE) ground = call->state->data.nq.ground;
    else if (call->state->kind == QA_MOVEMENT_QUAKEWORLD) ground = call->state->data.qw.ground;
    else if (call->state->kind == QA_MOVEMENT_Q3) ground = call->state->data.q3.ground;
    else {
        qa_body_state body;
        if (!qa_world_body_read(engine->world, actor, &body, error)) return false;
        ground = qa_actor_reference_present(body.ground) ? (qa_movement_ground){.hit = QA_TRACE_HIT_ACTOR, .actor = qa_actor_reference_resolve(qa_session_actors(engine->provider->application->session), body.ground)}
            : (qa_movement_ground){0};
    }
    if (!call->water_level || !call->water_type)
        return application_fail(error, QA_ERROR_ARGUMENT, "Mixed QC postthink needs actual water outputs");
    int32_t level = *call->water_level, type = *call->water_type;
    int32_t source_type = level == 0 ? -1 :
        call->state->kind == QA_MOVEMENT_NETQUAKE || call->state->kind == QA_MOVEMENT_QUAKEWORLD ? type :
        (type & 16) != 0 ? -4 : (type & 8) != 0 ? -5 : -3;
    return control_ground(engine, reference, actor, ground, false, error) &&
        control_store_scalar(engine, reference, actor, "waterlevel", (float)level, error) &&
        control_store_scalar(engine, reference, actor, "watertype", (float)source_type, error);
}
typedef struct control_think_invocation {
    struct application_qc_state *engine;
    qa_actor_id actor;
    double time;
} control_think_invocation;
static bool control_invoke_think(void *opaque, qa_session *session, qa_error *error)
{
    control_think_invocation *call = opaque;
    if (session != call->engine->services.session)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC think invocation belongs to another session");
    return source_callback(call->engine, call->actor, (qa_actor_id){0}, "think", call->time, error);
}
static bool control_run_think(struct application_qc_state *engine, qa_actor_id actor,
                                const qa_source_frame *frame, qa_error *error)
{
    qa_source_frame active;
    if (!frame || frame->provider != engine->provider->owner ||
        frame->kind != engine->provider->component.clock.kind ||
        !qa_session_active_frame(engine->services.session, frame->provider, &active) ||
        active.number != frame->number || active.start_ns != frame->start_ns ||
        active.time_ns != frame->time_ns || active.elapsed_ns != frame->elapsed_ns)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC control think needs its actual physical source frame");
    qa_actor_owner execution;
    if (!qa_session_execution(engine->services.session, actor, &execution))
        return application_fail(error, QA_ERROR_NOT_FOUND, "QC think actor changed generation");
    if (execution == engine->provider->owner) {
        if (!schedule_think(engine, actor, error)) return false;
        qa_think_result result;
        return qa_scheduler_run_once(qa_session_scheduler(engine->services.session), actor,
            frame, QA_THINK_DURING_PHYSICS, &result, error);
    }
    bool member;
    if (engine->provider->component.clock.kind != QA_CLOCK_NETQUAKE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Foreign QC source think requires a NetQuake client turn");
    if (!application_qc_control_source_client(engine->provider, actor, &member, error)) return false;
    if (!member)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC think actor has no admitted physical source client");
    int32_t reference;
    float due;
    if (!application_qc_reference(engine, actor, &reference, error) ||
        !control_scalar(engine, reference, actor, "nextthink", &due, error)) return false;
    double current = (double)frame->time_ns / 1e9;
    double end = current + (double)frame->elapsed_ns / 1e9;
    if (!(due > 0) || (double)due > end) return true;
    if (!control_store_scalar(engine, reference, actor, "nextthink", 0, error)) return false;
    const qa_think *pending = qa_scheduler_pending(qa_session_scheduler(engine->services.session), actor);
    if (pending && pending->execution_provider == engine->provider->owner)
        qa_scheduler_cancel(qa_session_scheduler(engine->services.session), actor);
    if (!qa_actors_get(qa_session_actors(engine->services.session), actor)) return true;
    if (!application_qc_control_source_client(engine->provider, actor, &member, error)) return false;
    if (!member)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC think actor changed its physical source membership");
    control_think_invocation call = {engine, actor, fmax(current, (double)due)};
    return qa_session_invoke(engine->services.session, actor, QA_INVOKE_THINK,
                             control_invoke_think, &call, error);
}
static bool control_command_think(struct application_qc_state *engine, qa_actor_id actor,
                                    const application_control_context *context, uint64_t elapsed_ns, qa_error *error)
{
    const qa_source_command *command = context ? &context->command : NULL;
    qa_source_command active; bool member;
    if (!command || !qa_session_active_command(engine->services.session, engine->provider->owner, &active) ||
        !qa_actor_id_equal(command->actor, actor) || !qa_actor_id_equal(active.actor, actor) ||
        command->provider != engine->provider->owner || active.provider != command->provider ||
        command->kind != engine->provider->component.clock.kind || active.kind != command->kind ||
        command->phase != QA_CLIENT_COMMAND || active.phase != command->phase ||
        active.completed_frame_number != command->completed_frame_number || active.time_ns != command->time_ns ||
        active.elapsed_ns != command->elapsed_ns || active.host_elapsed_ns != command->host_elapsed_ns)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC think needs its actual admitted source command");
    if (!application_qc_control_source_client(engine->provider, actor, &member, error)) return false;
    if (!member) return application_fail(error, QA_ERROR_ARGUMENT, "QC think actor has no admitted physical source client");
    int32_t reference; float due;
    if (!application_qc_reference(engine, actor, &reference, error) ||
        !control_scalar(engine, reference, actor, "nextthink", &due, error)) return false;
    double current = (double)engine->source_time_ns / 1e9;
    double end = current + (double)elapsed_ns / 1e9;
    if (!(due > 0) || (double)due > end) return true;
    double time = (double)due < current ? current : due;
    if (!control_store_scalar(engine, reference, actor, "nextthink", 0, error)) return false;
    const qa_think *pending = qa_scheduler_pending(qa_session_scheduler(engine->services.session), actor);
    if (pending && pending->execution_provider == command->provider)
        qa_scheduler_cancel(qa_session_scheduler(engine->services.session), actor);
    if (qa_actors_get(qa_session_actors(engine->services.session), actor) == NULL) return true;
    if (!application_qc_control_source_client(engine->provider, actor, &member, error)) return false;
    if (!member) return application_fail(error, QA_ERROR_ARGUMENT, "QC think actor changed its physical source membership");
    control_think_invocation call = {engine, actor, time};
    return qa_session_invoke(engine->services.session, actor, QA_INVOKE_THINK, control_invoke_think, &call, error);
}
bool application_qc_control_before_actor(application_provider *provider, qa_actor_id actor,
                                           const qa_source_frame *frame, qa_error *error)
{
    struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    if (!engine || !frame || frame->provider != provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC actor control needs its actual execution frame");
    if (provider->state.qc.qualified) return true;
    const qa_qc_definition *field = qa_qc_program_find_global(provider->state.qc.program, "force_retouch");
    if (!field) return true;
    float value;
    if (field->type != QA_QC_FLOAT || !qa_qc_global_float(provider->state.qc.instance, field->offset, &value, error)) return false;
    return value == 0 || (qa_world_link(engine->world, actor, NULL, error) &&
        (qa_actors_get(qa_session_actors(engine->services.session), actor) == NULL ||
         qa_physics_touch_triggers(engine->services.physics, actor, error)));
}
bool application_qc_control_phase(application_provider *provider, qa_actor_id actor,
                                    application_control_source_path path, qa_movement_phase phase,
                                    const application_control_context *context, qa_movement_call *call, qa_error *error)
{
    struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    if (!engine || !context || !call || !call->state || !call->command)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC control phase is absent");
    if (provider->state.qc.qualified) return true;
    const application_control_context *actual = application_control_frame_current(provider->application, actor);
    if (!actual || actual->path != path || context->path != path ||
        !qa_actor_id_equal(context->actor, actor) || actual->command_only != context->command_only ||
        application_control_provider(context) != provider->owner ||
        application_control_kind(context) != provider->component.clock.kind ||
        application_control_time(actual) != application_control_time(context) ||
        application_control_start(actual) != application_control_start(context) ||
        application_control_elapsed(actual) != application_control_elapsed(context) ||
        (context->command_only ? actual->command.completed_frame_number != context->command.completed_frame_number ||
            actual->command.elapsed_ns != context->command.elapsed_ns || actual->command.host_elapsed_ns != context->command.host_elapsed_ns :
            actual->frame.number != context->frame.number || actual->frame.elapsed_ns != context->frame.elapsed_ns))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC control callback lost its genuine source admission");
    if (phase != QA_MOVE_PRETHINK && phase != QA_MOVE_THINK && phase != QA_MOVE_POSTTHINK &&
        phase != QA_MOVE_INPUT_END && phase != QA_MOVE_LINK && phase != QA_MOVE_LINK_TRIGGERS) return true;
    int32_t reference; bool spectator;
    if (!control_client(engine, actor, &reference, &spectator, error) ||
        !control_store_state(engine, reference, actor, call, error)) return false;
    if (path == APPLICATION_CONTROL_QW_GROUP && call->state->kind == QA_MOVEMENT_QUAKEWORLD &&
        (phase == QA_MOVE_LINK || phase == QA_MOVE_LINK_TRIGGERS || phase == QA_MOVE_POSTTHINK)) {
        if (call->water_level && !control_store_scalar(engine, reference, actor, "waterlevel", (float)*call->water_level, error)) return false;
        if (call->water_type && !control_store_scalar(engine, reference, actor, "watertype", (float)*call->water_type, error)) return false;
    }
    if (phase == QA_MOVE_PRETHINK) {
        if (context->command_only && context->command.kind == QA_CLOCK_QUAKEWORLD) {
            engine->source_time_ns = context->command.time_ns;
            if (!qa_qc_game_set_time(provider->state.qc.game, (double)engine->source_time_ns / 1e9,
                (double)context->command.elapsed_ns / 1e9, error)) return false;
        }
        int32_t before_flags = 0; qa_vec3 before_velocity = {0};
        bool mixed = path == APPLICATION_CONTROL_MIXED ||
            (path == APPLICATION_CONTROL_QW_GROUP && call->state->kind != QA_MOVEMENT_QUAKEWORLD) ||
            (path == APPLICATION_CONTROL_NQ_TURN && context->source_nqcmd && call->state->kind != QA_MOVEMENT_NETQUAKE);
        if (mixed && !control_transition(engine, reference, actor, &before_flags, &before_velocity, error)) return false;
        if (path == APPLICATION_CONTROL_QW_GROUP) {
            const qa_movement_command *source_command = context->source_qwcmd &&
                call->state->kind != QA_MOVEMENT_QUAKEWORLD ? &context->source_command : call->command;
            if (!control_qw_input(engine, reference, actor, source_command, error)) return false;
            if (spectator) goto refreshed;
        }
        if (path != APPLICATION_CONTROL_NQ_TURN) {
            float elapsed;
            double seconds = path == APPLICATION_CONTROL_QW_GROUP ? (double)call->milliseconds * 0.001 :
                (double)application_control_elapsed(context) / 1e9;
            if (!control_word(seconds, &elapsed, error) || !control_frame_time(engine, elapsed, error)) return false;
        }
        if (!spectator && !application_qc_named(engine, "PlayerPreThink", actor, error)) return false;
        if (qa_actors_get(qa_session_actors(engine->services.session), actor) == NULL) return true;
        if (mixed && (path == APPLICATION_CONTROL_MIXED || path == APPLICATION_CONTROL_NQ_TURN)) {
            int32_t after_flags; qa_vec3 after_velocity;
            if (!control_transition(engine, reference, actor, &after_flags, &after_velocity, error)) return false;
            bool requested = control_jump(call->command);
            bool accepted = requested && ((uint32_t)before_flags & (512u | 4096u)) == (512u | 4096u) &&
                ((uint32_t)after_flags & (512u | 4096u)) == 0 && after_velocity.z > before_velocity.z;
            if (accepted && !control_clear_ground(engine, actor, error)) return false;
            if (accepted || (requested && ((uint32_t)before_flags & 4096u) == 0)) control_consume_jump(call->command);
        }
        if (path != APPLICATION_CONTROL_NQ_TURN) {
            if (!context->command_only)
                return application_fail(error, QA_ERROR_ARGUMENT, "QC mixed input needs an actual source command");
            uint64_t elapsed_ns = path == APPLICATION_CONTROL_QW_GROUP ?
                (uint64_t)call->milliseconds * UINT64_C(1000000) : application_control_elapsed(context);
            if (!control_command_think(engine, actor, context, elapsed_ns, error)) return false;
        }
        if (qa_actors_get(qa_session_actors(engine->services.session), actor) == NULL) return true;
        if (mixed && path == APPLICATION_CONTROL_QW_GROUP) {
            int32_t after_flags; qa_vec3 after_velocity;
            if (!control_transition(engine, reference, actor, &after_flags, &after_velocity, error)) return false;
            bool accepted = control_jump(call->command) && ((uint32_t)before_flags & (512u | 4096u)) == (512u | 4096u) &&
                ((uint32_t)after_flags & 4096u) == 0;
            if (accepted && after_velocity.z > before_velocity.z) {
                if (!control_clear_ground(engine, actor, error)) return false;
                control_consume_jump(call->command);
            }
        }
    } else if (phase == QA_MOVE_THINK) {
        if (path == APPLICATION_CONTROL_NQ_TURN) {
            if (context->command_only)
                return application_fail(error, QA_ERROR_ARGUMENT, "NetQuake actor turn needs an actual source frame");
            if (!control_run_think(engine, actor, &context->frame, error)) return false;
        }
    } else if (phase == QA_MOVE_POSTTHINK) {
        if ((path == APPLICATION_CONTROL_MIXED ||
            (path == APPLICATION_CONTROL_QW_GROUP && call->state->kind != QA_MOVEMENT_QUAKEWORLD) ||
            (path == APPLICATION_CONTROL_NQ_TURN && context->source_nqcmd && call->state->kind != QA_MOVEMENT_NETQUAKE)) &&
            !control_mixed_water(engine, reference, actor, call, error)) return false;
        if (!context->defer_postthink && !(spectator ?
            application_qc_spectator_callback(engine, "SpectatorThink", actor, error) :
            application_qc_client_postthink(engine, actor, error))) return false;
    }
refreshed:
    if (qa_actors_get(qa_session_actors(engine->services.session), actor) == NULL) return true;
    qa_vec3 view;
    return application_qc_control_state(provider, actor, call->state, call->bounds, call->environment, &view, error);
}
static bool actor_frame(void *opaque, qa_session *session, qa_actor_id actor,
                        const qa_source_frame *frame, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    qa_actor_owner execution;
    if (session != engine->services.session || !qa_session_execution(session, actor, &execution) ||
        execution != engine->provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC actor execution owner differs");
    if (engine->provider->state.qc.qualified || engine->profile != QA_QC_QUAKEWORLD)
        for (uint32_t slot = 1; slot <= engine->max_clients; ++slot)
            if (engine->clients[slot].connected && !engine->clients[slot].spawned &&
                qa_actor_id_equal(engine->clients[slot].actor, actor)) return true;
    int32_t reference;
    if (!application_qc_reference(engine, actor, &reference, error)) return false;
    const qa_qc_definition *retouch = qa_qc_program_find_global(engine->provider->state.qc.program, "force_retouch");
    if (retouch != NULL) {
        float value;
        if (retouch->type != QA_QC_FLOAT || !qa_qc_global_float(engine->provider->state.qc.instance, retouch->offset, &value, error)) return false;
        if (value != 0 && (!qa_world_link(engine->world, actor, NULL, error) ||
            !qa_physics_touch_triggers(engine->services.physics, actor, error))) return false;
        if (qa_actors_get(qa_session_actors(session), actor) == NULL) return true;
    }
    bool player = false, spectator = false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot)
        if (engine->clients[slot].connected && qa_actor_id_equal(engine->clients[slot].actor, actor)) {
            player = true;
            spectator = engine->clients[slot].spectator; break;
        }
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    if (!profile && engine->profile == QA_QC_QUAKEWORLD) {
        bool reserved;
        if (!application_qc_control_reserved(engine->provider, actor, &reserved, error)) return false;
        if (reserved) return true;
    }
    if (player && !profile && !(spectator ? application_qc_spectator_callback(engine, "SpectatorThink", actor, error) :
        application_qc_named(engine, "PlayerPreThink", actor, error))) return false;
    if (qa_actors_get(qa_session_actors(session), actor) == NULL) return true;
    if (player && profile) return true;
    float motion = 0;
    if (!player) {
        if (!profile && engine->profile == QA_QC_QUAKEWORLD) {
            float previous, now = (float)((double)frame->time_ns / 1e9);
            if (!application_qc_float(engine, reference, "lastruntime", &previous, error)) return false;
            if (previous == now) return true;
            if (!application_qc_set_float(engine, reference, "lastruntime", now, error)) return false;
        }
        if (!application_qc_float(engine, reference, "movetype", &motion, error)) return false;
        if (motion != 0 && motion != 4 && motion != 5 && motion != 6 && motion != 7 &&
            motion != 8 && motion != 9 && motion != 10 &&
            !(motion==11 && engine->profile==QA_QC_RERELEASE))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Unsupported nonclient QuakeC movetype");
    }
    qa_physics_result result;
    if (!player && motion == 7) {
        if (!schedule_think(engine, actor, error)) return false;
        return qa_physics_step_q1_pusher(engine->services.physics, actor, frame, false, &result, error);
    }
    if (!player && motion == 4) {
        if (!qa_physics_step(engine->services.physics, actor, frame, &result, error)) return false;
        if (qa_actors_get(qa_session_actors(session), actor) == NULL) return true;
    }
    if (!schedule_think(engine, actor, error)) return false;
    qa_think_result thought;
    if (!(profile ?
        qa_scheduler_run(qa_session_scheduler(session), actor, frame, QA_THINK_DURING_PHYSICS, &thought, error) :
        qa_scheduler_run_once(qa_session_scheduler(session), actor, frame, QA_THINK_DURING_PHYSICS, &thought, error))) return false;
    if (!thought.alive) return true;
    if (!player) {
        if (motion == 4) return application_qc_water_transition(engine->provider, actor, error);
        if (motion == 0) return true;
        if (motion != 8) {
            float live_motion;
            if (!application_qc_float(engine, reference, "movetype", &live_motion, error)) return false;
            if (live_motion == 8) return true;
        }
        if (!(motion == 8 ?
            qa_physics_step_source_motion(engine->services.physics, actor, frame, QA_PHYSICS_NOCLIP, &result, error) :
            qa_physics_step(engine->services.physics, actor, frame, &result, error))) return false;
        if (result.status == QA_PHYSICS_REMOVED) return true;
    }
    if (player && !profile && !spectator && qa_actors_get(qa_session_actors(session), actor) != NULL)
        return application_qc_client_postthink(engine, actor, error);
    return true;
}
static bool end_frame(void *opaque, qa_session *session, const qa_source_frame *frame, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    (void)session; (void)frame;
    const qa_qc_definition *retouch = qa_qc_program_find_global(engine->provider->state.qc.program, "force_retouch");
    if (retouch != NULL) {
        float value;
        if (retouch->type != QA_QC_FLOAT || !qa_qc_global_float(engine->provider->state.qc.instance, retouch->offset, &value, error)) return false;
        if (value != 0 && !qa_qc_set_global_float(engine->provider->state.qc.instance, retouch->offset, value - 1, error)) return false;
    }
    size_t commands;
    bool ok = application_qc_rerelease_frame(engine,error) &&
        application_qc_flush(engine, error) &&
        (qa_application_startup_console_queued(engine->provider->application, engine->console) ||
         qa_console_drain(engine->console, 0, &commands, error));
    engine->has_frame = false;
    return ok;
}
static uint32_t source_random(void *context)
{
    struct application_qc_state *engine = context;
    return qa_builtin_random_integer(&engine->random);
}
bool application_qc_player_roster_ready(application_provider *provider,
                                         const qa_launch_choices *choices, qa_error *error)
{
    if (!provider || !choices || !provider->launch)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC client roster has no selected provider");
    if (!application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC client roster has unfinished input");
    const struct application_qc_profile *profile = provider->state.qc.qualified;
    size_t count = choices->seat_count;
    uint32_t maximum = profile && profile->clients ? profile->maximum_clients :
        provider->state.qc.engine ? provider->state.qc.engine->max_clients : UINT32_MAX;
    return count <= maximum ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "Selected QC roster exceeds retained source client capacity");
}
static bool command_actor(void *opaque, qa_session *session, qa_actor_id actor)
{
    struct application_qc_state *engine = opaque;
    application_provider *provider = engine ? engine->provider : NULL;
    if (!provider || provider->state.qc.engine != engine || provider->application->session != session ||
        engine->services.session != session || !provider->constructed || !provider->attached || provider->close_pending ||
        !engine->initialized || engine->loading || !provider->state.qc.instance) return false;
    bool member; qa_error ignored = {0};
    return application_qc_control_source_client(provider, actor, &member, &ignored) && member;
}
static double source_time_seconds(void *opaque)
{
    const struct application_qc_state *engine = opaque;
    return (double)engine->source_time_ns / 1e9;
}
static bool declared_replace(void *opaque,qa_qc_instance *vm,const qa_qc_call_event *event,
    qa_qc_call_next next,qa_error *error)
{
    struct application_qc_state *engine=opaque;bool handled=false;
    if(!application_qc_item_weapons_replace(engine,vm,event,next,&handled,error))return false;
    if(handled)return true;
    if(!application_qc_combat_replace(engine,vm,event,next,&handled,error))return false;
    return handled||application_qc_spawn_call(opaque,vm,event,next,error);
}
static bool declared_inline(void *opaque,qa_qc_instance *vm,const qa_qc_inline_event *event,
    qa_qc_inline_next next,qa_error *error)
{
    struct application_qc_state *engine=opaque;bool handled=false;
    if(!application_qc_item_weapons_inline(engine,vm,event,next,&handled,error))return false;
    if(handled)return true;
    if(!application_qc_combat_inline(engine,vm,event,next,&handled,error))return false;
    return handled||qa_qc_inline_continue(next,error);
}
static bool declared_left(void *opaque,qa_qc_instance *vm,const qa_qc_call_event *event,qa_error *error)
{
    (void)vm;return event->depth!=0||application_qc_objectives_sync(opaque,error);
}
static bool declared_regions(application_provider *provider,qa_qc_inline_region **out,size_t *count,qa_error *error)
{
    const struct application_qc_profile *profile=provider->state.qc.qualified;
    size_t sizes[3]={0},total=0;
    const qa_qc_inline_region *parts[]={
        application_qc_item_weapons_regions(provider,&sizes[0]),
        application_qc_protection_regions(profile?profile->protection:NULL,&sizes[1]),
        application_qc_combat_regions(profile?profile->combat:NULL,&sizes[2])};
    for(size_t i=0;i<sizeof(parts)/sizeof(parts[0]);++i){
        if(sizes[i]>SIZE_MAX-total)return application_fail(error,QA_ERROR_MEMORY,"QC admitted Source region count overflows");
        total+=sizes[i];
    }
    if(total>SIZE_MAX/sizeof(**out))
        return application_fail(error,QA_ERROR_MEMORY,"QC admitted Source region count overflows");
    *count=0;*out=total?malloc(total*sizeof(**out)):NULL;
    if(total&&!*out)return application_fail(error,QA_ERROR_MEMORY,"Composing QC admitted Source regions");
    for(size_t i=0;i<sizeof(parts)/sizeof(parts[0]);++i){
        for(size_t j=0;j<sizes[i];++j){
            const qa_qc_inline_region *region=parts[i]+j;
            size_t existing=0;
            while(existing<*count && ((*out)[existing].function!=region->function ||
                (*out)[existing].entry!=region->entry))++existing;
            if(existing==*count){(*out)[(*count)++]=*region;continue;}
            const qa_qc_inline_region *admitted=*out+existing;
            if(admitted->exit!=region->exit || admitted->replaceable!=region->replaceable ||
                admitted->saved_scope!=region->saved_scope || admitted->saved_word!=region->saved_word){
                free(*out);*out=NULL;*count=0;
                return application_fail(error,QA_ERROR_FORMAT,"QC declarations disagree at one actual Source region entry");
            }
        }
    }
    return true;
}
bool application_construct_qc(qa_application *app, application_provider *provider, qa_world *world,
                               const qa_product *product, const qa_launch_choices *choices, qa_error *error)
{
    qa_console *console; qa_cvars *cvars; qa_command_context command;
    if (!application_qc_console_prepare(app, provider, world, product, choices,
        &console, &cvars, &command, error)) return false;
    struct application_qc_state *engine = provider->state.qc.engine;
    engine->services = application_builtin_services(app, world, app->physics);
    const struct application_qc_profile *profile = provider->state.qc.qualified;
    if (!application_startup_source_preinit(provider, console, cvars, &command, error) ||
        (app->operation != APPLICATION_PERSISTING && !application_startup_apply_latched(provider, cvars, error))) return false;
    const qa_cvar_view *skill = qa_cvars_find(cvars, "skill"), *deathmatch = qa_cvars_find(cvars, "deathmatch");
    const qa_cvar_view *maximum = qa_cvars_find(cvars, "maxclients");
    if (!skill || !deathmatch || !maximum || !isfinite(skill->number) || !isfinite(deathmatch->number) ||
        !isfinite(maximum->number))
        return application_fail(error, QA_ERROR_FORMAT, "QC initialization lost its actual finite source rules");
    if (!profile && engine->profile != QA_QC_QUAKEWORLD) {
        float capacity = truncf(maximum->number);
        float minimum = (float)(choices->seat_count ? choices->seat_count : 1);
        if (capacity < minimum) capacity = minimum;
        if (capacity < 1 || capacity > 64)
            return application_fail(error, QA_ERROR_FORMAT, "QC source client capacity must be between 1 and 64");
        engine->max_clients = (uint32_t)capacity;
        if (!application_publication_source_capacity(provider, engine->max_clients, error)) return false;
    }
    if ((profile || engine->profile == QA_QC_QUAKEWORLD) && maximum->number != (float)engine->max_clients)
        return application_fail(error, QA_ERROR_FORMAT, "QC initialization differs from its actual source client capacity");
    if (engine->max_clients == UINT32_MAX || engine->max_clients + 1 >= engine->actor_capacity)
        return application_fail(error, QA_ERROR_FORMAT, "QC reserved clients exceed the actual source entity capacity");
    if (!application_qc_player_roster_ready(provider, choices, error)) return false;
    engine->clients = calloc((size_t)engine->max_clients + 1, sizeof(*engine->clients));
    engine->actors = calloc(engine->actor_capacity, sizeof(*engine->actors));
    if (!engine->clients || !engine->actors)
        return application_fail(error, QA_ERROR_MEMORY, "Allocating QC physical client and actor rows");
    int32_t difficulty = (int32_t)(fmaxf(0, fminf(3, skill->number)) + 0.5);
    static const qa_qc_builtin imports[] = {
        QA_QC_BUILTIN_CHECKCLIENT, QA_QC_BUILTIN_AIM, QA_QC_BUILTIN_STUFFCMD,
        QA_QC_BUILTIN_COREDUMP, QA_QC_BUILTIN_EPRINT, QA_QC_BUILTIN_LIGHTSTYLE,
        QA_QC_BUILTIN_WRITEBYTE, QA_QC_BUILTIN_WRITECHAR, QA_QC_BUILTIN_WRITESHORT,
        QA_QC_BUILTIN_WRITELONG, QA_QC_BUILTIN_WRITECOORD, QA_QC_BUILTIN_WRITEANGLE,
        QA_QC_BUILTIN_WRITESTRING, QA_QC_BUILTIN_WRITEENTITY, QA_QC_BUILTIN_MAKESTATIC,
        QA_QC_BUILTIN_CHANGELEVEL, QA_QC_BUILTIN_SETSPAWNPARMS,
        QA_QC_BUILTIN_LOGFRAG, QA_QC_BUILTIN_INFOKEY, QA_QC_BUILTIN_MULTICAST
    };
    static const qa_qc_builtin rerelease_imports[] = {
        QA_QC_BUILTIN_SETCOLOR, QA_QC_BUILTIN_EX_BPRINT, QA_QC_BUILTIN_EX_SPRINT,
        QA_QC_BUILTIN_EX_CENTERPRINT, QA_QC_BUILTIN_EX_FINALE_FINISHED, QA_QC_BUILTIN_EX_LOCALSOUND,
        QA_QC_BUILTIN_EX_DRAW_POINT, QA_QC_BUILTIN_EX_DRAW_LINE, QA_QC_BUILTIN_EX_DRAW_ARROW,
        QA_QC_BUILTIN_EX_DRAW_RAY, QA_QC_BUILTIN_EX_DRAW_CIRCLE, QA_QC_BUILTIN_EX_DRAW_BOUNDS,
        QA_QC_BUILTIN_EX_DRAW_WORLDTEXT, QA_QC_BUILTIN_EX_DRAW_SPHERE, QA_QC_BUILTIN_EX_DRAW_CYLINDER,
        QA_QC_BUILTIN_EX_PROMPT, QA_QC_BUILTIN_EX_PROMPTCHOICE, QA_QC_BUILTIN_EX_CLEARPROMPT,
        QA_QC_BUILTIN_EX_CHECK_PLAYER_FLAGS, QA_QC_BUILTIN_EX_WALKPATHTOGOAL, QA_QC_BUILTIN_EX_BOT_MOVETOPOINT,
        QA_QC_BUILTIN_EX_BOT_FOLLOWENTITY
    };
    size_t import_count=sizeof(imports)/sizeof(imports[0]);
    qa_qc_builtin_binding bindings[sizeof(imports)/sizeof(imports[0])+
        sizeof(rerelease_imports)/sizeof(rerelease_imports[0])];
    for (size_t i = 0; i < import_count; ++i)
        bindings[i] = (qa_qc_builtin_binding){imports[i], NULL, engine, application_qc_import};
    if (engine->profile==QA_QC_RERELEASE)
        for (size_t i=0;i<sizeof(rerelease_imports)/sizeof(rerelease_imports[0]);++i)
            bindings[import_count++]=(qa_qc_builtin_binding){rerelease_imports[i],NULL,engine,application_qc_import};
    qa_actor_definition definition;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), "quakec:authored", &definition, error)) return false;
    qa_qc_inline_region *regions=NULL;size_t region_count=0;
    if(!declared_regions(provider,&regions,&region_count,error))return false;
    qa_qc_game_options options = {
        .vm = {.profile = engine->profile, .entity_capacity = engine->actor_capacity,
            .inline_regions=regions,.inline_region_count=region_count,
            .observers = {.context = engine, .stored = stored, .entered = application_qc_entered,
                .replace = declared_replace,.inline_boundary=declared_inline,.left=declared_left,
                .entity_stores_only = true},
            .host = {.owner = provider->owner, .default_definition = definition, .vfs = provider->launch->content,
                     .context = engine, .random_u32 = source_random, .may_move = application_qc_may_move,
                     .declared_projection = profile != NULL, .prepare_entity = application_qc_prepare_entity,
                     .source_time_seconds = source_time_seconds,
                     .builtins = bindings, .builtin_count = import_count}},
        .services = engine->services, .cvars = engine->cvars, .console = engine->console,
        .command_context = engine->command_context, .max_clients = engine->max_clients,
        .map_exclusion_flags = deathmatch->number != 0 ? 2048u : difficulty <= 0 ? 256u : difficulty == 1 ? 512u : 1024u,
        .context = engine, .resource = application_qc_resource_lookup,
        .checkpoint = application_qc_capture_engine, .restore = application_qc_restore_engine};
    bool created=(engine->profile==QA_QC_QUAKEWORLD || app->operation==APPLICATION_PERSISTING ||
        qa_cvars_set_number(cvars,"skill",(float)difficulty,error)) &&
        qa_qc_game_create(provider->state.qc.program,&options,&provider->state.qc.game,error);
    free(regions);if(!created)return false;
    provider->state.qc.instance = qa_qc_game_instance(provider->state.qc.game);
    if(!application_qc_combat_create(engine,error) || !application_qc_protection_create(engine,error) ||
        !application_qc_items_initialize(engine,error))return false;
    provider->component = (qa_component){.owner = provider->owner, .clock = provider->launch->selection.clock,
        .state = engine, .prepare_frame = prepare_frame, .begin_frame = begin_frame,
        .actor_frame = actor_frame, .end_frame = end_frame, .command_actor = command_actor};
    if (application_q1_original_clock(provider,&provider->component.clock.initial_time_ns))
        engine->source_time_ns=provider->component.clock.initial_time_ns;
    else if (provider->component.clock.initial_time_ns == 0)
        provider->component.clock.initial_time_ns = UINT64_C(1000000000);
    return true;
}
bool application_qc_source_globals(application_provider *provider, qa_error *error)
{
    struct application_qc_state *engine=provider->state.qc.engine;
    qa_qc_instance *vm=provider->state.qc.instance;
    static const char *globals[] = {"skill", "deathmatch", "coop", "teamplay"};
    for (size_t i = 0; i < sizeof(globals) / sizeof(globals[0]); ++i) {
        const qa_qc_definition *def = qa_qc_program_find_global(provider->state.qc.program, globals[i]);
        const qa_cvar_view *value = qa_cvars_find(engine->cvars, globals[i]);
        if (def != NULL && (def->type != QA_QC_FLOAT || !qa_qc_set_global_float(vm, def->offset, value->number, error))) return false;
    }
    const qa_qc_definition *serverflags = qa_qc_program_find_global(provider->state.qc.program, "serverflags");
    if (serverflags != NULL && (serverflags->type != QA_QC_FLOAT ||
        !qa_qc_set_global_float(vm, serverflags->offset, engine->serverflags, error))) return false;
    return true;
}

static bool load_map(application_provider *provider, const qa_bsp_view *bsp,
                       const qa_entities *entities, qa_string_id map_id,
                       qa_string_id spawn_id, bool authored_entities, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (!application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC map has unfinished input");
    if(!application_bots_npc_idle(provider))
        return application_fail(error,QA_ERROR_ARGUMENT,"QC map retains an active monster path");
    if (!application_qc_callbacks_suspend(provider, error) ||
        !application_qc_objectives_suspend(engine,error) ||
        !application_qc_pickups_close(engine,error) || !application_qc_items_close(engine,error) ||
        !application_qc_protection_suspend(engine,error) ||
        !application_qc_combat_suspend(engine,error)) return false;
    application_bots_npc_destroy(provider);
    if (provider->state.qc.qualified && !authored_entities)
        return application_qc_load_declared_map(engine, bsp, entities, map_id, spawn_id, error) &&
            application_qc_callbacks_register(provider, error);
    const char *map = qa_strings_cstr(qa_session_strings(provider->application->session), map_id);
    (void)bsp; (void)spawn_id;
    if (engine != NULL && !engine->loading) {
        if (!qa_qc_game_reset_level(provider->state.qc.game, error)) return false;
        engine->initialized = false;
        application_qc_rerelease_reset(engine);
        for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
            engine->clients[slot].receipt_seen = false;
            engine->clients[slot].receipt_sequence = engine->clients[slot].receipt_ordinal = 0;
            engine->clients[slot].pending_weapon=0;
            engine->clients[slot].pending_weapon_following=false;
        }
        provider->state.qc.instance = qa_qc_game_instance(provider->state.qc.game);
        engine->loading = true; engine->check_slot = 0; engine->check_time = 0; engine->check_cluster = -1;
        qa_cvars_set_server_active(engine->cvars, false);
        for (size_t i = 0; i < engine->resource_count; ++i) {
            application_qc_resource_dispose(&engine->resources[i]);
        }
        engine->resource_count = 0;
        for (size_t i = 0; i < engine->message_count; ++i) {
            free(engine->messages[i].data); free(engine->messages[i].references);
        }
        engine->message_count = 0;
        for (size_t i = 0; i < 64; ++i) { free(engine->lightstyles[i]); engine->lightstyles[i] = NULL; }
    }
    if (engine == NULL || entities == NULL || map == NULL || entities->count == 0 || !engine->loading)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC map is missing its world entity");
    qa_qc_instance *vm = provider->state.qc.instance;
    uint64_t initial_ns=UINT64_C(1000000000);
    (void)application_q1_original_clock(provider,&initial_ns);
    engine->source_time_ns = initial_ns;
    if (!application_qc_source_clients_initialize(engine, error)) return false;
    /* Geometry may come from a different product than this guest's assets. */
    qa_bsp_model world_model;
    if (bsp == NULL || !qa_bsp_read_model(bsp, 0, &world_model, error) ||
        !qa_qc_game_set_time(provider->state.qc.game, (double)initial_ns/1e9, 0, error)) return false;
    if (engine->resource_count != 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC world model must precede source precaches");
    if (engine->resource_capacity == 0) {
        engine->resources = calloc(32, sizeof(*engine->resources));
        if (engine->resources == NULL) return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC world precache");
        engine->resource_capacity = 32;
    }
    const qa_launch_snapshot *snapshot=provider->application->routing_snapshot?
        provider->application->routing_snapshot:qa_application_launch(provider->application);
    const qa_launch_choices *choices=snapshot?qa_launch_snapshot_choices(snapshot):NULL;
    const char *world_path=choices?choices->world.map:NULL;
    if (!world_path || !*world_path)
        return application_fail(error,QA_ERROR_FORMAT,"Source world model lacks its actual requested map path");
    char *world_name = copy_text(world_path, error);
    if (world_name == NULL) return false;
    engine->resources[engine->resource_count++] = (application_qc_resource){.name = world_name,
        .kind = QA_QC_RESOURCE_MODEL, .world_model = true, .has_inline_model = true,
        .value = {.index = 1, .bounds = {world_model.bounds.min, world_model.bounds.max}}};
    size_t model_count = qa_bsp_record_count(bsp, QA_BSP_MODELS);
    for (size_t i = 1; i < model_count; ++i) {
        char inline_name[32]; qa_qc_game_resource resource;
        snprintf(inline_name, sizeof(inline_name), "*%zu", i);
        if (!application_qc_resource_lookup(engine, QA_QC_RESOURCE_MODEL,
            inline_name, true, &resource, error)) return false;
    }
    const qa_qc_definition *mapname = qa_qc_program_find_global(provider->state.qc.program, "mapname");
    int32_t name;
    bool ok = mapname != NULL && mapname->type == QA_QC_STRING && qa_qc_string_allocate(vm, map, &name, error) &&
              qa_qc_set_global_int(vm, mapname->offset, name, error);
    if (!ok) return application_fail(error, QA_ERROR_FORMAT, "QuakeC mapname global is unavailable");
    if (!application_qc_source_globals(provider,error)) return false;
    const qa_qc_definition *model = application_qc_field(engine, "model", QA_QC_STRING, error);
    if (model == NULL || !qa_qc_string_allocate(vm, world_path, &name, error) ||
        !qa_qc_set_entity_int(vm, 0, model->offset, name, error) ||
        !application_qc_set_float(engine, 0, "modelindex", 1, error) ||
        !application_qc_set_float(engine, 0, "solid", 4, error) ||
        !application_qc_set_float(engine, 0, "movetype", 7, error)) return false;
    if (!application_qc_initialize_declared(engine, error)) return false;
    size_t count = authored_entities ? entities->count : 1;
    for (size_t i = 0; i < count; ++i) {
        qa_entity_record record = entities->records[i]; int32_t reference;
        if (record.property_count == 0 && i != 0) continue;
        if (!qa_qc_game_spawn_entity(provider->state.qc.game, i == 0,
            entities->properties + record.first_property, record.property_count, &reference, error)) return false;
    }
    if (!application_qc_flush(engine, error) || !qa_qc_game_loading(provider->state.qc.game, false, error)) return false;
    engine->loading = false; engine->initialized = true; engine->source_time_ns = initial_ns;
    qa_cvars_set_server_active(engine->cvars, true);
    return application_qc_objectives_activate(engine,error) && application_qc_callbacks_register(provider, error);
}
bool application_qc_spawn_map(application_provider *provider, const qa_bsp_view *bsp,
                               const qa_entities *entities, qa_string_id map,
                               qa_string_id spawn, qa_error *error)
{
    if (provider && provider->state.qc.qualified &&
        !application_qc_authored_map_ready(provider, error)) return false;
    return load_map(provider, bsp, entities, map, spawn, true, error);
}
bool application_qc_initialize_map(application_provider *provider, const qa_bsp_view *bsp,
                                    const qa_entities *entities, qa_string_id map,
                                    qa_string_id spawn, qa_error *error)
{
    return load_map(provider, bsp, entities, map, spawn, false, error);
}
bool application_qc_deconstruct(application_provider *provider, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (engine == NULL) return true;
    if (application_qc_has_source_admission(engine) || engine->input_scope || engine->parked_inputs ||
        engine->client_think_time || !qa_world_idle(engine->world) ||
        !application_qc_combat_idle(engine) || !application_qc_protection_idle(engine) ||
        !qa_inventory_idle(engine->services.inventory) ||
        !qa_pickups_idle(engine->services.pickups) ||
        (engine->console && !qa_console_idle(engine->console)) ||
        (engine->cvars && !qa_cvars_observer_idle(engine->cvars)) ||
        (provider->state.qc.game && !qa_qc_game_idle(provider->state.qc.game)))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC collision contexts are borrowed by the world");
    if (!application_bots_npc_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC teardown retains an active monster path");
    if (!application_qc_callbacks_suspend(provider, error) ||
        !application_qc_objectives_suspend(engine,error) ||
        !application_qc_pickups_close(engine,error) || !application_qc_items_close(engine,error) ||
        !application_qc_protection_destroy(engine,error) ||
        !application_qc_combat_destroy(engine,error)) return false;
    for (uint32_t i = 0; engine->actors && i < engine->actor_capacity; ++i) {
        if (!engine->actors[i].collision_bound) continue;
        if (!qa_world_collision_unbind(engine->world, engine->actors[i].actor, &engine->actors[i], error)) return false;
        engine->actors[i].collision_bound = false;
    }
    engine->initialized = false;
    application_bots_npc_destroy(provider);
    if (!qa_qc_game_destroy(provider->state.qc.game, error)) return false;
    provider->state.qc.game = NULL; provider->state.qc.instance = NULL;
    engine->output_channels = 0;
    if (engine->console && engine->cvars &&
        !application_startup_source_retire(provider, engine->console, engine->cvars, error)) return false;
    if (!qa_console_unbind_source(engine->console, qa_cvars_view_identity(engine->cvars), error)) return false;
    qa_cvars_remove_owner(engine->cvars, provider->owner);
    qa_cvars_detach_callbacks(engine->cvars); qa_cvars_destroy(engine->cvars);
    for (size_t i = 0; i < engine->resource_count; ++i) {
        application_qc_resource_dispose(&engine->resources[i]);
    }
    for (size_t i = 0; i < engine->message_count; ++i) {
        free(engine->messages[i].data); free(engine->messages[i].references);
    }
    for (size_t i = 0; i < 64; ++i) free(engine->lightstyles[i]);
    qa_buffer_free(&engine->original_extension);
    application_qc_rerelease_destroy(engine);
    qa_builtin_snapshot_free(&engine->observations);
    application_qc_items_destroy(engine);
    free(engine->resources); free(engine->messages); free(engine->clients); free(engine->actors); free(engine);
    provider->state.qc.engine = NULL;
    return true;
}
bool application_qc_actor_released(application_provider *provider, qa_actor_record record, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (engine == NULL) return true;
    if (!application_qc_pickups_release(engine,record.id,error) ||
        !application_qc_items_release(engine,record.id,error) ||
        !application_qc_protection_release(engine,record.id,error) ||
        !application_qc_combat_release(engine,record.id,error) ||
        !application_qc_source_client_released(engine, record, error)) return false;
    application_qc_rerelease_released(engine,record.id);
    application_bots_npc_released(provider,record.id);
    qa_qc_game_actor_released(provider->state.qc.game, record);
    if (record.id.slot < engine->actor_capacity && qa_actor_id_equal(engine->actors[record.id.slot].actor, record.id))
        engine->actors[record.id.slot] = (application_qc_actor){0};
    for (uint32_t i = 1; i <= engine->max_clients; ++i)
        if (qa_actor_id_equal(engine->clients[i].actor, record.id)) {
            engine->clients[i].actor = (qa_actor_id){0}; engine->clients[i].connected = false; engine->clients[i].spawned = false;
            engine->clients[i].prepared = false;
            engine->clients[i].output_published = false;
            engine->clients[i].outputs = (application_client_outputs){0};
            engine->clients[i].receipt_seen = false;
            engine->clients[i].receipt_sequence = engine->clients[i].receipt_ordinal = 0;
            engine->clients[i].pending_weapon=0;
            engine->clients[i].pending_weapon_following=false;
        }
    return true;
}
bool application_qc_physics_read(application_provider *provider, qa_actor_id actor, qa_physics_properties *out)
{
    qa_error ignored = {0};
    return qa_qc_game_read_physics(provider->state.qc.game, actor, out, &ignored);
}
bool application_qc_physics_write(application_provider *provider, qa_actor_id actor,
                                  const qa_physics_properties *value, qa_error *error)
{
    return qa_qc_game_write_physics(provider->state.qc.game, actor, value, error);
}
bool application_qc_water_transition(application_provider *provider, qa_actor_id actor,
                                      qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_QC || !provider->state.qc.engine)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC water transition has no source owner");
    struct application_qc_state *engine = provider->state.qc.engine;
    const qa_actor_registry *actors = qa_session_actors(engine->services.session);
    if (qa_actors_get(actors, actor) == NULL) return true;
    int32_t reference; float previous;
    qa_body_state body;
    if (!application_qc_reference(engine, actor, &reference, error) ||
        !qa_world_body_read(engine->world, actor, &body, error)) return false;
    qa_point_query query = {.point = body.origin, .pass_actor = actor,
        .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(engine->world, &query, &contents, error)) return false;
    if (contents.family != QA_COLLISION_Q1)
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC water transition needs source Q1 contents");
    if (qa_actors_get(actors, actor) == NULL) return true;
    if (!application_qc_float(engine, reference, "watertype", &previous, error)) return false;
    bool splash = previous != 0 && (contents.contents <= -3 ? previous == -1 : previous != -1);
    float water_type = previous == 0 || contents.contents <= -3 ? (float)contents.contents : -1;
    float water_level = previous == 0 || contents.contents <= -3 ? 1 : (float)contents.contents;
    if (splash) {
        const char *path = "misc/h2ohit1.wav";
        bool precached = false;
        for (size_t i = 0; i < engine->resource_count; ++i)
            if (engine->resources[i].kind == QA_QC_RESOURCE_SOUND && strcmp(engine->resources[i].name, path) == 0) {
                precached = true; break;
            }
        if (precached) {
            qa_string_id resource;
            if (!qa_builtin_resource(&engine->services, path, &resource, error)) return false;
            qa_builtin_event sound = {.kind = QA_BUILTIN_SOUND, .family = QA_GAME_Q1,
                .provider = provider->owner, .actor = actor, .time_ns = engine->source_time_ns,
                .resource = resource, .origin = body.origin, .channel = 0, .volume = 1, .attenuation = 1};
            if (!qa_builtin_emit(&engine->services, &sound, error)) return false;
        } else if (!provider->state.qc.qualified) {
            application_console_print(provider->application, &engine->command_context,
                "SV_StartSound: misc/h2ohit1.wav not precacheed\n");
        }
        if (qa_actors_get(actors, actor) == NULL) return true;
    }
    if (!application_qc_set_float(engine, reference, "watertype", water_type, error)) return false;
    return qa_actors_get(actors, actor) == NULL ||
        application_qc_set_float(engine, reference, "waterlevel", water_level, error);
}
bool application_qc_touch(application_provider *provider, const qa_touch_contact *contact, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    double time = provider->state.qc.qualified && engine->client_think_time ?
        *engine->client_think_time : (double)engine->source_time_ns / 1e9;
    return source_callback(engine, contact->self, contact->other, "touch", time, error);
}
bool application_qc_blocked(application_provider *provider, qa_actor_id self, qa_actor_id other, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    double time = provider->state.qc.qualified && engine->client_think_time ?
        *engine->client_think_time : (double)engine->source_time_ns / 1e9;
    return source_callback(engine, self, other, "blocked", time, error);
}
bool application_qc_pusher_think(application_provider *provider, qa_actor_id actor,
                                 const qa_source_frame *frame, qa_error *error)
{
    struct application_qc_state *engine = provider ? provider->state.qc.engine : NULL;
    qa_actor_owner execution;
    if (!engine || !frame || frame->provider != provider->owner ||
        frame->kind != provider->component.clock.kind ||
        !qa_session_execution(engine->services.session, actor, &execution) || execution != provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC pusher think needs its actual source actor frame");
    qa_scheduler_cancel(qa_session_scheduler(engine->services.session), actor);
    return source_callback(engine, actor, (qa_actor_id){0}, "think", (double)engine->source_time_ns / 1e9, error);
}
