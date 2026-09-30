#include "guest_native_q2_private.h"
#include "guest_native_q2_attack.h"
#include "guest_native_q2_combat.h"

bool application_native_q2_idle(const application_provider *provider)
{
    const struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    return !engine || (!engine->baseline && !engine->calls && qa_world_idle(engine->world) &&
        (!engine->console || qa_console_idle(engine->console)) &&
        (!provider->state.native.host || qa_native_host_destroy_ready(provider->state.native.host)));
}

static bool frontend_owner_idle(void *context)
{
    struct application_native_q2 *engine = context;
    return engine && application_native_q2_idle(engine->provider);
}

static bool begin_frame(void *state, qa_session *session, const qa_source_frame *frame, qa_error *error)
{
    (void)session;
    struct application_native_q2 *engine = state;
    engine->frame = *frame;
    if (!engine->map_ready) return true;
    ++engine->calls;
    bool ok = engine->profile != QA_NATIVE_Q2_GAME_API2023 ||
        qa_native_host_prep_frame(engine->provider->state.native.host, error);
    --engine->calls;
    return ok;
}

static bool end_frame(void *state, qa_session *session, const qa_source_frame *frame, qa_error *error)
{
    (void)session; (void)frame;
    struct application_native_q2 *engine = state;
    if (!engine->map_ready) return true;
    ++engine->calls;
    bool ok = qa_native_host_run_frame(engine->provider->state.native.host, true, error);
    --engine->calls;
    return ok;
}

static void actor_released(void *state, qa_session *session, qa_actor_record actor)
{
    (void)session;
    struct application_native_q2 *engine = state;
    application_native_q2_attack_released(engine, actor.id);
    application_native_q2_combat_released(engine, actor.id);
    qa_error error = {0};
    if (engine->provider->state.native.host &&
        !qa_native_host_actor_released(engine->provider->state.native.host, actor, &error))
        application_fault(engine->provider->application, &error);
    for (size_t i = 1; i < 257; ++i)
        if (qa_actor_id_equal(engine->clients[i].actor, actor.id)) {
            engine->clients[i].actor = (qa_actor_id){0};
            engine->clients[i].connected = engine->clients[i].begun = false;
            engine->clients[i].inventory_bound = false;
        }
    if (qa_actor_id_equal(engine->world_actor, actor.id)) engine->world_actor = (qa_actor_id){0};
}

static bool capture_context(void *opaque, const qa_command_context *source,
                              qa_command_context *out, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    return qa_application_capture_command_context(engine->provider->application, source, out, error);
}
static bool context_active(void *opaque, const qa_command_context *context)
{
    struct application_native_q2 *engine = opaque;
    return qa_application_command_context_active(engine->provider->application, context);
}
static void console_print(void *opaque, const qa_command_context *context, const char *text)
{
    struct application_native_q2 *engine = opaque;
    application_console_print(engine->provider->application, context, text);
}
static bool read_script(void *opaque, const qa_command_context *context, const char *path,
                         qa_bytes *out, void **lease, qa_error *error)
{
    (void)context;
    struct application_native_q2 *engine = opaque;
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(engine->provider->launch->content, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource); *lease = resource;
    return true;
}
static void release_script(void *opaque, void *lease)
{
    (void)opaque; qa_resource_release(lease);
}
static qa_command_result console_command(void *opaque, const qa_command_invocation *command,
                                            qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    bool handled = false;
    if (!engine->initialized) return QA_COMMAND_UNHANDLED;
    if (!application_native_q2_console_command(engine->provider, command->context.actor,
            command->raw, &handled, error)) return QA_COMMAND_FAILED;
    return handled ? QA_COMMAND_HANDLED : QA_COMMAND_UNHANDLED;
}

bool application_construct_native_q2(qa_application *app, application_provider *provider,
    qa_world *world, const qa_product *product, const qa_launch_choices *choices, qa_error *error)
{
    if (!app || !provider || !world || !product || !choices || product->family != QA_GAME_Q2 ||
        provider->kind != APPLICATION_PROVIDER_NATIVE || !provider->launch->artifact ||
        choices->seat_count > 256)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid native Q2 owner admission");
    struct application_native_q2 *engine = calloc(1, sizeof(*engine));
    if (!engine) return application_fail(error, QA_ERROR_MEMORY, "Allocating native Q2 owner");
    provider->state.native.q2_engine = engine;
    engine->provider = provider; engine->world = world;
    bool cgame = (provider->launch->roles & QA_ROLE_BIT(QA_ROLE_HUD)) &&
        !(provider->launch->roles & (QA_ROLE_BIT(QA_ROLE_ENTITIES) |
          QA_ROLE_BIT(QA_ROLE_CHARACTER) | QA_ROLE_BIT(QA_ROLE_ARSENAL)));
    engine->profile = cgame ? QA_NATIVE_Q2_CGAME_API2023 :
        provider->launch->selection.clock.kind == QA_CLOCK_Q2_RERELEASE ?
            QA_NATIVE_Q2_GAME_API2023 : QA_NATIVE_Q2_GAME_API3;
    if (cgame && provider->launch->selection.clock.kind != QA_CLOCK_Q2_RERELEASE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 has no native cgame API");
    if (!qa_native_module_load(qa_resource_bytes(provider->launch->artifact),
            provider->launch->selection.artifact, engine->profile,
            qa_resource_digest(provider->launch->artifact), &provider->state.native.module, error)) return false;
    if (provider->launch->declaration && !qa_native_declaration_load(
            qa_resource_bytes(provider->launch->declaration), provider->launch->selection.artifact,
            provider->state.native.module, &engine->declaration, error)) return false;
    if (!application_native_q2_inventory_prepare(engine, error) ||
        !application_native_q2_attack_prepare(engine, error)) return false;
    if (!application_native_q2_combat_prepare(engine, error)) return false;
    qa_console_dialect dialect = engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_CONSOLE_Q2 : QA_CONSOLE_Q2_RERELEASE;
    engine->command_context = (qa_command_context){.owner = provider->owner,
        .origin = QA_COMMAND_SERVER, .dialect = dialect};
    qa_cvar_options cvars = {.dialect = dialect};
    engine->cvars = qa_cvars_create(&cvars, error);
    if (!engine->cvars) return false;
    qa_console_options console = {.context = engine->command_context, .cvars = engine->cvars,
        .user = engine, .print = console_print, .read_script = read_script, .release_script = release_script,
        .source_command = console_command, .capture_context = capture_context, .context_active = context_active};
    engine->console = qa_console_create(&console, error);
    if (!engine->console) return false;
    uint32_t clients = choices->seat_count ? (uint32_t)choices->seat_count : 1;
    char maximum[16], skill[32];
    snprintf(maximum, sizeof(maximum), "%u", clients);
    snprintf(skill, sizeof(skill), "%d", choices->world.skill);
    bool deathmatch = false, coop = false;
    if (choices->mode_count) {
        size_t index = 0;
        for (size_t i = 0; i < choices->mode_count; ++i) if (choices->modes[i].primary_score) { index = i; break; }
        qa_mode_kind kind = choices->modes[index].rules.kind;
        coop = kind == QA_MODE_COOPERATIVE;
        deathmatch = kind != QA_MODE_SINGLE_PLAYER && !coop;
    }
    const char *names[] = {"maxclients", "skill", "deathmatch", "coop"};
    const char *values[] = {maximum, skill, deathmatch ? "1" : "0", coop ? "1" : "0"};
    for (size_t i = 0; i < 4; ++i)
        if (!qa_cvars_register(engine->cvars, names[i], values[i], 0, provider->owner, NULL, error)) return false;
    for (size_t i = 0; i < choices->seat_count; ++i) {
        engine->clients[i + 1].reserved = true;
        engine->clients[i + 1].seat = choices->seats[i].id;
    }
    bool rerelease = engine->profile != QA_NATIVE_Q2_GAME_API3;
    engine->resource_base[0] = rerelease ? 62 : 32;
    engine->resource_limit[0] = rerelease ? 8192 : 256;
    engine->resource_limit[1] = rerelease ? 2048 : 256;
    engine->resource_limit[2] = rerelease ? 512 : 256;
    engine->resource_base[1] = engine->resource_base[0] + engine->resource_limit[0];
    engine->resource_base[2] = engine->resource_base[1] + engine->resource_limit[1];
    engine->configstring_count = rerelease ? 12448 : 2080;
    engine->configstrings = calloc(engine->configstring_count, sizeof(*engine->configstrings));
    if (!engine->configstrings)
        return application_fail(error, QA_ERROR_MEMORY, "Allocating native Q2 source configstring table");
    if (!qa_strings_intern_cstr(qa_session_strings(app->session),
            "native-q2:entity", &engine->definition, error)) return false;
    engine->platform.content_files = provider->launch->content;
    engine->platform.cvars = engine->cvars;
    engine->platform.owner_context = engine;
    engine->platform.owner_idle = frontend_owner_idle;
    if (app->native_q2_services && !app->native_q2_services(app->guest_context, app,
            provider->owner, engine->profile, &engine->platform, &engine->application,
            &engine->application_context, error)) return false;
    if ((engine->platform.frontend_lifetime != NULL) != (engine->platform.release_frontend != NULL))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 platform lease requires a paired release callback");
    provider->component = (qa_component){.owner = provider->owner,
        .clock = provider->launch->selection.clock, .state = engine,
        .begin_frame = cgame ? NULL : begin_frame, .end_frame = cgame ? NULL : end_frame,
        .actor_released = cgame ? NULL : actor_released};
    return true;
}

bool application_native_q2_activate(struct application_native_q2 *engine, qa_error *error)
{
    application_provider *provider = engine->provider;
    if (provider->state.native.host) return true;
    if (!provider->constructed || !provider->attached || engine->calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 loading requires its committed owner");
    uint64_t interval = provider->component.clock.interval_ns;
    if (engine->profile == QA_NATIVE_Q2_CGAME_API2023 && !engine->application &&
        provider->application->native_q2_services &&
        !provider->application->native_q2_services(provider->application->guest_context,
            provider->application, provider->owner, engine->profile, &engine->platform,
            &engine->application, &engine->application_context, error)) return false;
    qa_native_host_instance_options instance = {.declaration = engine->declaration,
        .observe = engine->source_attack != NULL || engine->source_combat != NULL,
        .declaration_digest = qa_native_declaration_digest(engine->declaration),
        .runner = provider->application->native_runner,
        .tick_rate = interval ? (uint32_t)(UINT64_C(1000000000) / interval) : 0,
        .frame_seconds = (float)interval / 1000000000.f,
        .frame_milliseconds = (uint32_t)(interval / UINT64_C(1000000))};
    bool ok;
    ++engine->calls;
    if (engine->profile == QA_NATIVE_Q2_CGAME_API2023) {
        qa_native_host_q2_cgame_options options = {.instance = instance,
            .engine = application_native_q2_services(engine), .application = application_native_q2_import,
            .application_context = engine, .cvars = engine->cvars,
            .console = engine->console, .command_context = engine->command_context};
        uint32_t seat = UINT32_MAX;
        for (size_t i = 1; i < 257; ++i) if (engine->clients[i].reserved) {
            if (seat != UINT32_MAX) { --engine->calls; return application_fail(error, QA_ERROR_ARGUMENT,
                "Native Q2 cgame requires an explicitly admitted seat instance"); }
            seat = engine->clients[i].seat;
        }
        options.seat = seat; options.seat_bound = seat != UINT32_MAX;
        ok = qa_native_host_create_q2_cgame(provider->state.native.module, &options,
            &provider->state.native.host, error);
    } else {
        qa_native_host_q2_game_options options = {.instance = instance,
            .world = {.session = provider->application->session, .world = engine->world,
                .physics = provider->application->physics, .combat = provider->application->combat,
                .inventory = provider->application->inventory, .targets = provider->application->targets,
                .owner = provider->owner, .definition = engine->definition, .world_actor = engine->world_actor,
                .binding_context = engine, .project_actor = application_native_q2_project,
                .address_for_actor = application_native_q2_address, .bind_actor = application_native_q2_bind},
            .services = {.engine = application_native_q2_services(engine),
                .movement = application_native_q2_movement_services(engine),
                .application = engine->application, .application_context = engine->application_context},
            .cvars = engine->cvars, .console = engine->console, .command_context = engine->command_context};
        ok = qa_native_host_create_q2_game(provider->state.native.module, &options,
            &provider->state.native.host, error);
    }
    --engine->calls;
    return ok;
}

bool application_native_q2_spawn_map(application_provider *provider, const qa_bsp_view *map,
    const qa_entities *entities, qa_string_id name, qa_string_id spawn, qa_error *error)
{
    (void)entities;
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !map || engine->profile == QA_NATIVE_Q2_CGAME_API2023 || !application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 map publication requires an idle game owner");
    qa_bytes text = map->lumps[QA_BSP_ENTITIES].bytes;
    if (text.size == SIZE_MAX) return application_fail(error, QA_ERROR_MEMORY, "Native Q2 entity text extent overflow");
    char *copy = malloc(text.size + 1);
    if (!copy) return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q2 entity text");
    if (text.size) memcpy(copy, text.data, text.size);
    copy[text.size] = 0;
    if (!engine->world_actor.registry && !qa_session_allocate(provider->application->session,
            provider->owner, engine->definition, true, 0, &engine->world_actor, error)) { free(copy); return false; }
    free(engine->entity_text); engine->entity_text = copy;
    engine->map_name = name; engine->spawn_point = spawn;
    if (!application_native_q2_activate(engine, error)) return false;
    if (engine->initialized && !qa_native_host_world_actor_bind(provider->state.native.host, engine->world_actor, error)) return false;
    for (uint32_t i = 0; i < engine->configstring_count; ++i) {
        free(engine->configstrings[i]); engine->configstrings[i] = NULL;
    }
    provider->application->physics->world_actor = engine->world_actor;
    if (!application_native_q2_combat_load(engine, error)) return false;
    ++engine->calls;
    bool ok = true;
    if (!engine->initialized) {
        ok = qa_native_host_initialize(provider->state.native.host, 0, 0, false, error);
        if (ok) engine->initialized = true;
    }
    if (ok) ok = application_native_q2_attack_activate(engine, error);
    if (ok) ok = application_native_q2_combat_activate(engine, error);
    if (ok) ok = qa_native_host_spawn_entities(provider->state.native.host,
        qa_strings_cstr(qa_session_strings(provider->application->session), name), copy,
        spawn ? qa_strings_cstr(qa_session_strings(provider->application->session), spawn) : "", error);
    --engine->calls;
    if (ok) { engine->map_ready = provider->map_bound = true; qa_cvars_set_server_active(engine->cvars, true); }
    return ok;
}

bool application_native_q2_retire_map(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine) return true;
    if (!application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 map retirement requires drained source callbacks");
    if (engine->profile == QA_NATIVE_Q2_CGAME_API2023 && provider->state.native.host) {
        if (engine->initialized) {
            if (qa_native_terminal(qa_native_host_instance(provider->state.native.host))) {
                if (!qa_native_host_terminal_retired(provider->state.native.host))
                    return application_fail(error, QA_ERROR_ARGUMENT,
                        "Terminal native Q2 cgame retirement requires drained retired ownership");
            } else {
                ++engine->calls;
                bool ok = qa_native_host_shutdown(provider->state.native.host, false, error);
                --engine->calls;
                if (!ok) return false;
            }
            engine->initialized = false;
        }
        if (!qa_native_host_destroy_ready(provider->state.native.host))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 cgame map shutdown has not drained");
        bool ok = qa_native_host_destroy(provider->state.native.host, error);
        provider->state.native.host = NULL;
        if (!ok) return false;
    }
    if (engine->profile == QA_NATIVE_Q2_CGAME_API2023) {
        if (engine->platform.release_frontend)
            engine->platform.release_frontend(engine->platform.frontend_lifetime);
        engine->platform = (qa_native_host_engine_services){.content_files = provider->launch->content,
            .cvars = engine->cvars, .owner_context = engine, .owner_idle = frontend_owner_idle};
        engine->application = NULL; engine->application_context = NULL;
        engine->hud_source_owner = 0;
        for (uint32_t i = 0; i < engine->configstring_count; ++i) {
            free(engine->configstrings[i]); engine->configstrings[i] = NULL;
        }
    }
    for (uint32_t i = 1; i < 257; ++i)
        if (engine->clients[i].actor.registry && !application_native_q2_client_disconnect(provider, i, error)) return false;
    engine->map_ready = provider->map_bound = false;
    qa_cvars_set_server_active(engine->cvars, false);
    return true;
}

bool application_native_q2_deconstruct(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine) return true;
    if (!application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 teardown requires drained callbacks");
    if (!application_native_q2_combat_suspend(engine, error)) return false;
    if (!application_native_q2_inventory_close(engine, error)) return false;
    bool terminal = provider->state.native.host && qa_native_terminal(qa_native_host_instance(provider->state.native.host));
    if (terminal && !qa_native_host_terminal_retired(provider->state.native.host))
        return application_fail(error, QA_ERROR_ARGUMENT, "Terminal native Q2 cleanup requires actual canonical actor retirement");
    qa_error first = {0}; bool ok = true;
    if (provider->state.native.host) {
        if (terminal) engine->initialized = false;
        if (engine->initialized && !engine->shutting_down) {
            engine->shutting_down = true;
            ++engine->calls;
            ok = qa_native_host_shutdown(provider->state.native.host, false, &first);
            --engine->calls;
            if (!ok) {
                engine->shutting_down = false;
                if (error) *error = first;
                return false;
            }
            engine->initialized = false;
        }
        if (!application_native_q2_attack_close(engine, error)) return false;
        if (!application_native_q2_combat_close(engine, error)) return false;
        qa_error cleanup = {0};
        if (!qa_native_host_destroy_ready(provider->state.native.host))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 host teardown has not drained");
        ++engine->calls;
        bool closed = qa_native_host_destroy(provider->state.native.host, &cleanup);
        --engine->calls; provider->state.native.host = NULL;
        if (!closed) { if (error) *error = cleanup; return false; }
    }
    if (!application_native_q2_attack_close(engine, error)) return false;
    if (!application_native_q2_combat_close(engine, error)) return false;
    if (engine->platform.release_frontend)
        engine->platform.release_frontend(engine->platform.frontend_lifetime);
    qa_console_destroy(engine->console); qa_cvars_destroy(engine->cvars);
    qa_native_declaration_destroy(engine->declaration);
    qa_command_tokens_free(&engine->arguments);
    if (engine->configstrings)
        for (uint32_t i = 0; i < engine->configstring_count; ++i) free(engine->configstrings[i]);
    free(engine->configstrings); free(engine->entity_text); free(engine);
    provider->state.native.q2_engine = NULL;
    if (!ok && error) *error = first;
    return ok;
}
