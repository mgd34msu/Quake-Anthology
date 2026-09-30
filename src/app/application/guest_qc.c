#include "guest_qc_profile.h"
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
bool application_qc_named(struct application_qc_state *engine, const char *name,
                           qa_actor_id actor, qa_error *error)
{
    qa_qc_game_global globals[2] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = actor}}},
        {"other", {QA_QC_GAME_ACTOR, {.actor = {0}}}}
    };
    return qa_qc_game_call(engine->provider->state.qc.game, name, NULL, 0,
                            globals, 2, NULL, error);
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
        double model;
        ok = qa_parse_number((qa_bytes){(const uint8_t *)name + 1, strlen(name + 1)}, &model, error);
        if (ok && (!isfinite(model) || model < 0 || model > UINT32_MAX || trunc(model) != model))
            ok = application_fail(error, QA_ERROR_FORMAT, "Invalid QuakeC inline model number");
        if (ok) ok = qa_collision_model_bounds(qa_world_geometry(engine->world), (uint32_t)model, &entry.value.bounds, error);
    } else {
        ok = qa_vfs_acquire(engine->provider->launch->content, name, &entry.source, NULL, error);
        if (ok && kind == QA_QC_RESOURCE_MODEL) {
            qa_bytes bytes = qa_resource_bytes(entry.source);
            if (bytes.size >= 4 && (qa_load_u32le(bytes.data) == 29 ||
                qa_load_u32le(bytes.data) == UINT32_C(0x32505342) ||
                qa_load_u32le(bytes.data) == UINT32_C(0x42535032) ||
                qa_load_u32le(bytes.data) == UINT32_C(0x50534249))) {
                qa_bsp_view map; qa_bsp_model model;
                ok = qa_bsp_open(bytes, &map, error) && qa_bsp_read_model(&map, 0, &model, error);
                if (ok) entry.value.bounds = (qa_bounds){model.bounds.min, model.bounds.max};
            } else {
                qa_model model = {0};
                ok = qa_model_load(bytes, &model, error);
                if (ok) entry.value.bounds = (qa_bounds){
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
    if (!ok) { qa_resource_release(entry.source); free(entry.name); return false; }
    engine->resources[engine->resource_count++] = entry;
    *out = entry.value;
    return true;
}
static qa_command_result server_command(void *opaque, const qa_command_invocation *command, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    qa_string_id text;
    if (!qa_strings_intern_cstr(qa_session_strings(engine->services.session), command->raw, &text, error) ||
        !application_map_server_command(engine->provider, text, error)) return QA_COMMAND_FAILED;
    return QA_COMMAND_HANDLED;
}
static bool read_script(void *opaque, const qa_command_context *context, const char *path,
                         qa_bytes *out, void **lease, qa_error *error)
{
    struct application_qc_state *engine = opaque; (void)context;
    qa_resource *resource;
    if (!qa_vfs_acquire(engine->provider->launch->content, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource); *lease = resource; return true;
}
static void release_script(void *opaque, void *lease) { (void)opaque; qa_resource_release(lease); }
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
qa_console *application_qc_create_console(struct application_qc_state *engine, qa_cvars *cvars, qa_error *error)
{
    qa_console_options options = {.context = engine->command_context, .cvars = cvars,
        .user = engine, .print = console_print, .source_command = server_command, .read_script = read_script,
        .release_script = release_script, .capture_context = capture_context, .context_active = context_active};
    return qa_console_create(&options, error);
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
    if (!scoped_time) {
        const qa_qc_definition *time = qa_qc_program_find_global(engine->provider->state.qc.program, "time");
        uint32_t word; memcpy(&word, &seconds, sizeof(word));
        if (!time || time->type != QA_QC_FLOAT)
            return application_fail(error, QA_ERROR_FORMAT, "QuakeC callback source time is missing");
        if (!qa_qc_stage_globals(engine->provider->state.qc.instance, time->offset, &word, 1, error)) return false;
    }
    qa_qc_game_global globals[3] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = actor}}},
        {"other", {QA_QC_GAME_ACTOR, {.actor = other}}},
        {"time", {QA_QC_GAME_FLOAT, {.number = seconds}}}
    };
    return qa_qc_game_call_index(engine->provider->state.qc.game, (uint32_t)function,
        NULL, 0, globals, scoped_time ? 3 : 2, NULL, error);
}
static bool source_think(void *opaque, qa_actor_id actor, const qa_source_frame *frame, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    int32_t reference;
    if (!application_qc_reference(engine, actor, &reference, error) ||
        !application_qc_set_float(engine, reference, "nextthink", 0, error)) return false;
    if (engine->provider->state.qc.qualified)
        return source_callback(engine, actor, (qa_actor_id){0}, "think", (double)frame->time_ns / 1e9, error);
    if (!qa_qc_game_set_time(engine->provider->state.qc.game, (double)frame->time_ns / 1e9,
                              (double)frame->elapsed_ns / 1e9, error)) return false;
    return source_callback(engine, actor, (qa_actor_id){0}, "think", (double)frame->time_ns / 1e9, error);
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
static bool schedule_think(struct application_qc_state *engine, qa_actor_id actor,
                           const qa_source_frame *frame, qa_error *error)
{
    int32_t reference; float due;
    if (!application_qc_reference(engine, actor, &reference, error) ||
        !application_qc_float(engine, reference, "nextthink", &due, error)) return false;
    if (!isfinite(due)) return application_fail(error, QA_ERROR_FORMAT, "Nonfinite QuakeC think deadline");
    if (!(due > 0)) { qa_scheduler_cancel(qa_session_scheduler(engine->services.session), actor); return true; }
    double ns = (double)due * 1e9;
    uint64_t time = ns >= (double)UINT64_MAX ? UINT64_MAX : (uint64_t)ns;
    qa_think think = {.actor = actor, .execution_provider = engine->provider->owner,
        .callback_id = 1,
        .due_ns = time, .boundary = QA_THINK_DURING_PHYSICS, .callback = source_think, .context = engine};
    (void)frame;
    return qa_session_schedule(engine->services.session, &think, error);
}
static bool stored(void *opaque, qa_qc_instance *vm, const qa_qc_store_event *event, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (!application_qc_store_declared(engine, vm, event, error)) return false;
    if (!engine->provider->state.qc.qualified && !application_qc_project_body_store(engine, vm, event, error)) return false;
    if (!engine->has_frame || event->kind != QA_QC_STORE_ENTITY || event->entity_reference == 0) return true;
    const qa_qc_definition *next = qa_qc_program_find_field(engine->provider->state.qc.program, "nextthink");
    if (next == NULL || next->offset < event->word || next->offset >= event->word + event->count) return true;
    qa_actor_id actor; qa_actor_owner execution;
    if (!qa_qc_reference_actor(vm, event->entity_reference, &actor, error)) return false;
    if (!qa_session_execution(engine->services.session, actor, &execution) || execution != engine->provider->owner) return true;
    return schedule_think(engine, actor, &engine->frame, error);
}
static bool begin_frame(void *opaque, qa_session *session, const qa_source_frame *frame, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (session != engine->services.session)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC frame belongs to another session");
    engine->source_time_ns = frame->time_ns;
    engine->frame = *frame; engine->has_frame = true;
    if (!qa_qc_game_set_time(engine->provider->state.qc.game, (double)frame->time_ns / 1e9,
                              (double)frame->elapsed_ns / 1e9, error)) return false;
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
            if (!application_qc_run_calls(engine, &profile->client_frame, &inputs, error)) return false;
        }
        return true;
    }
    return application_qc_named(engine, "StartFrame", (qa_actor_id){0}, error);
}
static bool actor_frame(void *opaque, qa_session *session, qa_actor_id actor,
                        const qa_source_frame *frame, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    qa_actor_owner execution;
    if (session != engine->services.session || !qa_session_execution(session, actor, &execution) ||
        execution != engine->provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC actor execution owner differs");
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
    bool player = false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot)
        if (engine->clients[slot].connected && qa_actor_id_equal(engine->clients[slot].actor, actor)) { player = true; break; }
    bool spectator = false;
    for (uint32_t slot = 1; slot <= engine->max_clients; ++slot)
        if (engine->clients[slot].connected && qa_actor_id_equal(engine->clients[slot].actor, actor)) {
            spectator = engine->clients[slot].spectator; break;
        }
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
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
            motion != 8 && motion != 9 && motion != 10)
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Unsupported nonclient QuakeC movetype");
    }
    qa_physics_result result;
    if (!player && motion == 7) {
        if (!schedule_think(engine, actor, frame, error)) return false;
        return qa_physics_step_q1_pusher(engine->services.physics, actor, frame, false, &result, error);
    }
    if (!player && motion == 4) {
        if (!qa_physics_step(engine->services.physics, actor, frame, &result, error)) return false;
        if (qa_actors_get(qa_session_actors(session), actor) == NULL) return true;
    }
    if (!schedule_think(engine, actor, frame, error)) return false;
    qa_think_result thought;
    if (!(profile ?
        qa_scheduler_run(qa_session_scheduler(session), actor, frame, QA_THINK_DURING_PHYSICS, &thought, error) :
        qa_scheduler_run_once(qa_session_scheduler(session), actor, frame, QA_THINK_DURING_PHYSICS, &thought, error))) return false;
    if (!thought.alive) return true;
    if (!qa_qc_game_set_time(engine->provider->state.qc.game, (double)frame->time_ns / 1e9,
                              (double)frame->elapsed_ns / 1e9, error)) return false;
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
        return application_qc_named(engine, "PlayerPostThink", actor, error);
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
    bool ok = application_qc_flush(engine, error) && qa_console_drain(engine->console, 0, &commands, error);
    engine->has_frame = false;
    return ok;
}
static uint32_t source_random(void *context)
{
    struct application_qc_state *engine = context;
    return qa_builtin_random_integer(&engine->random);
}
static size_t selected_client_count(const qa_launch_choices *choices, const char *instance)
{
    static const qa_launch_role roles[] = {QA_ROLE_CHARACTER, QA_ROLE_MOVEMENT, QA_ROLE_ARSENAL,
        QA_ROLE_INVENTORY, QA_ROLE_COMBAT, QA_ROLE_EFFECTS, QA_ROLE_EQUIPMENT};
    size_t count = 0;
    for (size_t i = 0; i < choices->seat_count; ++i) {
        const qa_launch_seat *seat = &choices->seats[i]; bool selected = false;
        for (size_t j = 0; j < sizeof(roles) / sizeof(roles[0]) && !selected; ++j) {
            const qa_launch_binding *binding = NULL;
            for (size_t k = 0; k < choices->binding_count; ++k) {
                const qa_launch_binding *candidate = &choices->bindings[k];
                if (candidate->role == roles[j] && candidate->selector[0] == 0 &&
                    candidate->scope.kind == QA_SCOPE_ACTOR && seat->actor.registry &&
                    qa_actor_id_equal(candidate->scope.actor, seat->actor)) { binding = candidate; break; }
            }
            if (!binding) binding = qa_launch_binding_for(choices,
                (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat->id}, roles[j], "");
            selected = binding && strcmp(binding->instance, instance) == 0;
        }
        if (selected) ++count;
    }
    return count;
}
bool application_qc_player_roster_ready(application_provider *provider,
                                         const qa_launch_choices *choices, qa_error *error)
{
    if (!provider || !choices || !provider->launch)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC client roster has no selected provider");
    if (!application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC client roster has unfinished input");
    const struct application_qc_profile *profile = provider->state.qc.qualified;
    size_t count = profile ? selected_client_count(choices, provider->launch->selection.instance) : choices->seat_count;
    uint32_t maximum = profile && profile->clients ? profile->maximum_clients :
        provider->state.qc.engine ? provider->state.qc.engine->max_clients : UINT32_MAX;
    return count <= maximum ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "Selected QC roster exceeds retained source client capacity");
}
bool application_construct_qc(qa_application *app, application_provider *provider, qa_world *world,
                               const qa_product *product, const qa_launch_choices *choices, qa_error *error)
{
    if (app == NULL || provider == NULL || world == NULL || product == NULL || choices == NULL ||
        provider->state.qc.engine != NULL || choices->seat_count >= UINT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid QuakeC application construction");
    struct application_qc_state *engine = calloc(1, sizeof(*engine));
    if (engine == NULL) return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC application state");
    provider->state.qc.engine = engine;
    engine->provider = provider; engine->world = world; engine->loading = true; engine->check_cluster = -1;
    engine->profile = product->edition == QA_EDITION_RERELEASE ? QA_QC_RERELEASE :
        provider->launch->selection.clock.kind == QA_CLOCK_QUAKEWORLD ? QA_QC_QUAKEWORLD : QA_QC_NETQUAKE;
    engine->protocol = (qa_net_protocol_id){engine->profile == QA_QC_QUAKEWORLD ? QA_NET_QW28 : QA_NET_NQ15, 0, 0};
    const struct application_qc_profile *profile = provider->state.qc.qualified;
    engine->max_clients = profile && profile->clients ? profile->maximum_clients :
        choices->seat_count ? (uint32_t)choices->seat_count : 1;
    if (profile && selected_client_count(choices, provider->launch->selection.instance) > engine->max_clients)
        return application_fail(error, QA_ERROR_FORMAT, "QC declared client capacity is below the selected roster");
    engine->clients = calloc((size_t)engine->max_clients + 1, sizeof(*engine->clients));
    engine->actor_capacity = qa_actors_capacity(qa_session_actors(app->session));
    engine->actors = calloc(engine->actor_capacity, sizeof(*engine->actors));
    engine->services = application_builtin_services(app, world, app->physics);
    qa_builtin_random_seed(&engine->random, (uint32_t)(provider->owner * UINT32_C(2654435761)));
    qa_console_dialect dialect = engine->profile == QA_QC_QUAKEWORLD ? QA_CONSOLE_QW : QA_CONSOLE_Q1;
    qa_cvar_options cvars = {.dialect = dialect};
    engine->cvars = qa_cvars_create(&cvars, error);
    engine->command_context = (qa_command_context){.owner = provider->owner, .dialect = dialect, .origin = QA_COMMAND_SERVER};
    if (engine->clients == NULL || engine->actors == NULL || engine->cvars == NULL ||
        (engine->console = application_qc_create_console(engine, engine->cvars, error)) == NULL) return false;
    char maximum[16]; snprintf(maximum, sizeof(maximum), "%u", engine->max_clients);
    static const char *names[] = {"skill", "deathmatch", "coop", "teamplay", "sv_gravity", "sv_aim", "sv_maxspeed",
        "maxclients", "registered", "developer", "sv_cheats", "samelevel", "timelimit", "fraglimit", "gamecfg"};
    const char *values[] = {"1", "0", "0", "0", "800", engine->profile == QA_QC_QUAKEWORLD ? "2" : "0.93", "320",
        maximum, "1", "0", "0", "0", "0", "0", "0"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!qa_cvars_register(engine->cvars, names[i], values[i], 0, provider->owner, NULL, error)) return false;
    if (engine->profile == QA_QC_QUAKEWORLD &&
        !qa_cvars_register(engine->cvars, "sv_phs", "1", 0, provider->owner, NULL, error)) return false;
    if (!qa_cvars_set_number(engine->cvars, "skill", (float)choices->world.skill, error)) return false;
    bool deathmatch = false;
    if (choices->mode_count) {
        size_t selected = 0;
        for (size_t i = 0; i < choices->mode_count; ++i) if (choices->modes[i].primary_score) { selected = i; break; }
        const qa_mode_rules *rules = &choices->modes[selected].rules;
        deathmatch = rules->kind != QA_MODE_SINGLE_PLAYER && rules->kind != QA_MODE_COOPERATIVE;
        if (!qa_cvars_set_number(engine->cvars, "deathmatch", deathmatch ? 1 : 0, error) ||
            !qa_cvars_set_number(engine->cvars, "coop", rules->kind == QA_MODE_COOPERATIVE ? 1 : 0, error) ||
            !qa_cvars_set_number(engine->cvars, "teamplay", (float)rules->teamplay, error)) return false;
    }
    for (size_t i = 0; profile && i < profile->cvar_count; ++i) {
        const application_qc_cvar *entry = &profile->cvars[i];
        if (qa_cvars_find(engine->cvars, entry->name)) {
            if (!qa_cvars_set(engine->cvars, entry->name, entry->value, true, error)) return false;
        } else if (!qa_cvars_register(engine->cvars, entry->name, entry->value, 0, provider->owner, NULL, error)) return false;
    }
    static const qa_qc_builtin imports[] = {
        QA_QC_BUILTIN_CHECKCLIENT, QA_QC_BUILTIN_AIM, QA_QC_BUILTIN_STUFFCMD,
        QA_QC_BUILTIN_COREDUMP, QA_QC_BUILTIN_EPRINT, QA_QC_BUILTIN_LIGHTSTYLE,
        QA_QC_BUILTIN_WRITEBYTE, QA_QC_BUILTIN_WRITECHAR, QA_QC_BUILTIN_WRITESHORT,
        QA_QC_BUILTIN_WRITELONG, QA_QC_BUILTIN_WRITECOORD, QA_QC_BUILTIN_WRITEANGLE,
        QA_QC_BUILTIN_WRITESTRING, QA_QC_BUILTIN_WRITEENTITY, QA_QC_BUILTIN_MAKESTATIC,
        QA_QC_BUILTIN_CHANGELEVEL, QA_QC_BUILTIN_SETSPAWNPARMS,
        QA_QC_BUILTIN_LOGFRAG, QA_QC_BUILTIN_INFOKEY, QA_QC_BUILTIN_MULTICAST
    };
    qa_qc_builtin_binding bindings[sizeof(imports) / sizeof(imports[0])];
    for (size_t i = 0; i < sizeof(imports) / sizeof(imports[0]); ++i)
        bindings[i] = (qa_qc_builtin_binding){imports[i], NULL, engine, application_qc_import};
    qa_actor_definition definition;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), "quakec:authored", &definition, error)) return false;
    qa_qc_game_options options = {
        .vm = {.profile = engine->profile, .entity_capacity = app->options.actor_capacity,
            .observers = {.context = engine, .stored = stored, .entered = application_qc_entered},
            .host = {.owner = provider->owner, .default_definition = definition, .vfs = provider->launch->content,
                     .context = engine, .random_u32 = source_random, .may_move = application_qc_may_move,
                     .declared_projection = profile != NULL, .prepare_entity = application_qc_prepare_entity,
                     .builtins = bindings, .builtin_count = sizeof(imports) / sizeof(imports[0])}},
        .services = engine->services, .cvars = engine->cvars, .console = engine->console,
        .command_context = engine->command_context, .max_clients = engine->max_clients,
        .map_exclusion_flags = deathmatch ? 2048u : choices->world.skill <= 0 ? 256u : choices->world.skill == 1 ? 512u : 1024u,
        .context = engine, .resource = application_qc_resource_lookup,
        .checkpoint = application_qc_capture_engine, .restore = application_qc_restore_engine};
    if (!qa_qc_game_create(provider->state.qc.program, &options, &provider->state.qc.game, error)) return false;
    provider->state.qc.instance = qa_qc_game_instance(provider->state.qc.game);
    provider->component = (qa_component){.owner = provider->owner, .clock = provider->launch->selection.clock,
        .state = engine, .begin_frame = begin_frame, .actor_frame = actor_frame, .end_frame = end_frame};
    if (provider->component.clock.initial_time_ns == 0)
        provider->component.clock.initial_time_ns = UINT64_C(1000000000);
    return true;
}
static bool load_map(application_provider *provider, const qa_bsp_view *bsp,
                       const qa_entities *entities, qa_string_id map_id,
                       qa_string_id spawn_id, bool authored_entities, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    if (!application_qc_input_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "QC map has unfinished input");
    if (provider->state.qc.qualified)
        return application_qc_load_declared_map(engine, bsp, entities, map_id, spawn_id, error);
    const char *map = qa_strings_cstr(qa_session_strings(provider->application->session), map_id);
    (void)bsp; (void)spawn_id;
    if (engine != NULL && !engine->loading) {
        if (!qa_qc_game_reset_level(provider->state.qc.game, error)) return false;
        provider->state.qc.instance = qa_qc_game_instance(provider->state.qc.game);
        engine->loading = true; engine->check_slot = 0; engine->check_time = 0; engine->check_cluster = -1;
        qa_cvars_set_server_active(engine->cvars, false);
        for (size_t i = 0; i < engine->resource_count; ++i) {
            free(engine->resources[i].name); qa_resource_release(engine->resources[i].source);
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
    /* Geometry may come from a different product than this guest's assets. */
    qa_bsp_model world_model;
    if (bsp == NULL || !qa_bsp_read_model(bsp, 0, &world_model, error) ||
        !qa_qc_game_set_time(provider->state.qc.game, 1, 0, error)) return false;
    if (engine->resource_count != 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC world model must precede source precaches");
    if (engine->resource_capacity == 0) {
        engine->resources = calloc(32, sizeof(*engine->resources));
        if (engine->resources == NULL) return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC world precache");
        engine->resource_capacity = 32;
    }
    char *world_name = copy_text(map, error);
    if (world_name == NULL) return false;
    engine->resources[engine->resource_count++] = (application_qc_resource){.name = world_name,
        .kind = QA_QC_RESOURCE_MODEL, .world_model = true, .value = {.index = 1, .bounds = {world_model.bounds.min, world_model.bounds.max}}};
    const qa_qc_definition *mapname = qa_qc_program_find_global(provider->state.qc.program, "mapname");
    const char *base = strncmp(map, "maps/", 5) == 0 ? map + 5 : map;
    char *short_name = copy_text(base, error);
    if (short_name == NULL) return false;
    size_t length = strlen(short_name);
    if (length >= 4 && strcmp(short_name + length - 4, ".bsp") == 0) short_name[length - 4] = 0;
    int32_t name;
    bool ok = mapname != NULL && mapname->type == QA_QC_STRING && qa_qc_string_allocate(vm, short_name, &name, error) &&
              qa_qc_set_global_int(vm, mapname->offset, name, error);
    free(short_name);
    if (!ok) return application_fail(error, QA_ERROR_FORMAT, "QuakeC mapname global is unavailable");
    static const char *globals[] = {"skill", "deathmatch", "coop", "teamplay"};
    for (size_t i = 0; i < sizeof(globals) / sizeof(globals[0]); ++i) {
        const qa_qc_definition *def = qa_qc_program_find_global(provider->state.qc.program, globals[i]);
        const qa_cvar_view *value = qa_cvars_find(engine->cvars, globals[i]);
        if (def != NULL && (def->type != QA_QC_FLOAT || !qa_qc_set_global_float(vm, def->offset, value->number, error))) return false;
    }
    const qa_qc_definition *serverflags = qa_qc_program_find_global(provider->state.qc.program, "serverflags");
    if (serverflags != NULL && (serverflags->type != QA_QC_FLOAT ||
        !qa_qc_set_global_float(vm, serverflags->offset, engine->serverflags, error))) return false;
    const qa_qc_definition *model = application_qc_field(engine, "model", QA_QC_STRING, error);
    if (model == NULL || !qa_qc_string_allocate(vm, map, &name, error) ||
        !qa_qc_set_entity_int(vm, 0, model->offset, name, error) ||
        !application_qc_set_float(engine, 0, "modelindex", 1, error) ||
        !application_qc_set_float(engine, 0, "solid", 4, error) ||
        !application_qc_set_float(engine, 0, "movetype", 7, error)) return false;
    size_t count = authored_entities ? entities->count : 1;
    for (size_t i = 0; i < count; ++i) {
        qa_entity_record record = entities->records[i]; int32_t reference;
        if (record.property_count == 0 && i != 0) continue;
        if (!qa_qc_game_spawn_entity(provider->state.qc.game, i == 0,
            entities->properties + record.first_property, record.property_count, &reference, error)) return false;
    }
    if (!application_qc_flush(engine, error) || !qa_qc_game_loading(provider->state.qc.game, false, error)) return false;
    engine->loading = false; engine->source_time_ns = UINT64_C(1000000000);
    qa_cvars_set_server_active(engine->cvars, true);
    return true;
}
bool application_qc_spawn_map(application_provider *provider, const qa_bsp_view *bsp,
                               const qa_entities *entities, qa_string_id map,
                               qa_string_id spawn, qa_error *error)
{
    if (provider && provider->state.qc.qualified)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Declared QC authored map spawning requires its source entity adapter");
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
    if (engine->input_scope || engine->client_think_time || !qa_world_idle(engine->world) ||
        (provider->state.qc.game && !qa_qc_game_idle(provider->state.qc.game)))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC collision contexts are borrowed by the world");
    for (uint32_t i = 0; engine->actors && i < engine->actor_capacity; ++i) {
        if (!engine->actors[i].collision_bound) continue;
        if (!qa_world_collision_unbind(engine->world, engine->actors[i].actor, &engine->actors[i], error)) return false;
        engine->actors[i].collision_bound = false;
    }
    if (!qa_qc_game_destroy(provider->state.qc.game, error)) return false;
    provider->state.qc.game = NULL; provider->state.qc.instance = NULL;
    qa_console_destroy(engine->console); qa_cvars_destroy(engine->cvars);
    for (size_t i = 0; i < engine->resource_count; ++i) {
        free(engine->resources[i].name); qa_resource_release(engine->resources[i].source);
    }
    for (size_t i = 0; i < engine->message_count; ++i) {
        free(engine->messages[i].data); free(engine->messages[i].references);
    }
    for (size_t i = 0; i < 64; ++i) free(engine->lightstyles[i]);
    qa_builtin_snapshot_free(&engine->observations);
    free(engine->resources); free(engine->messages); free(engine->clients); free(engine->actors); free(engine);
    provider->state.qc.engine = NULL;
    return true;
}
bool application_qc_actor_released(application_provider *provider, qa_actor_record record, qa_error *error)
{
    struct application_qc_state *engine = provider->state.qc.engine;
    (void)error;
    if (engine == NULL) return true;
    qa_qc_game_actor_released(provider->state.qc.game, record);
    if (record.id.slot < engine->actor_capacity && qa_actor_id_equal(engine->actors[record.id.slot].actor, record.id))
        engine->actors[record.id.slot] = (application_qc_actor){0};
    for (uint32_t i = 1; i <= engine->max_clients; ++i)
        if (qa_actor_id_equal(engine->clients[i].actor, record.id)) {
            engine->clients[i].actor = (qa_actor_id){0}; engine->clients[i].connected = false; engine->clients[i].spawned = false;
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
    return source_think(provider->state.qc.engine, actor, frame, error);
}
