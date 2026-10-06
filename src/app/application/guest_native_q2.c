#include "guest_native_q2_private.h"
#include "guest_native_q2_input.h"
#include "native_q2_callbacks.h"
#include "native_q2_client_stages.h"
#include "native_q2_source_actors.h"
#include "native_q2_wire_engine.h"
#include "native_q2_visibility.h"
#include "native_q2_console.h"
#include "save_native_q2_record.h"
#include "guest_native_q2_original_save.h"
#include "unified_events.h"
#include "guest_native_q2_attack.h"
#include "guest_native_q2_combat.h"
#include "guest_q2_control.h"
#include "q3_product.h"
#include "startup_flow.h"
#include "qa/console_cvar_observer.h"
#include "native_q2_publication.h"
#include "native_q2_inventory_scanner.h"
#include "native_q2_inventory_rows.h"

static bool load_host(struct application_native_q2 *, qa_error *);

static const uint64_t whole_source_roles = QA_ROLE_BIT(QA_ROLE_ENTITIES) |
    QA_ROLE_BIT(QA_ROLE_MOVEMENT) | QA_ROLE_BIT(QA_ROLE_CHARACTER) |
    QA_ROLE_BIT(QA_ROLE_ARSENAL) |
    QA_ROLE_BIT(QA_ROLE_COMBAT) | QA_ROLE_BIT(QA_ROLE_INVENTORY) |
    QA_ROLE_BIT(QA_ROLE_PICKUPS) | QA_ROLE_BIT(QA_ROLE_MONSTERS);

bool application_native_q2_whole_source(const struct application_native_q2 *engine,
    qa_actor_id actor)
{
    application_provider *provider = engine ? engine->provider : NULL;
    qa_application *app = provider ? provider->application : NULL;
    if (!app || engine->declaration || engine->profile == QA_NATIVE_Q2_CGAME_API2023 ||
        provider->state.native.q2_engine != engine ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != provider) return false;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
    if (!record || record->owner != provider->owner) return false;
    for (unsigned role = 0; role < QA_ROLE_COUNT; ++role)
        if ((whole_source_roles & QA_ROLE_BIT(role)) &&
            application_provider_for(app, actor, (qa_launch_role)role, "") != provider) return false;
    return true;
}

static bool whole_source_choices(const struct application_native_q2 *engine,
    const qa_launch_choices *choices, qa_error *error)
{
    const char *instance = engine->provider->launch->selection.instance;
    const qa_launch_binding *entities = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
    if (!entities || strcmp(entities->instance, instance))
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Original Q2 without a composition declaration must own the whole GAME");
    for (size_t i = 0; i < choices->binding_count; ++i) {
        const qa_launch_binding *binding = choices->bindings + i;
        if ((whole_source_roles & QA_ROLE_BIT(binding->role)) &&
            strcmp(binding->instance, instance))
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "Independent Q2 gameplay providers require a mod-supplied composition declaration");
    }
    for (size_t i = 0; i < choices->mode_count; ++i)
        if (strcmp(choices->modes[i].instance, instance))
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "Independent Q2 modes require a mod-supplied composition declaration");
    return true;
}

static bool command_actor(void *state,qa_session *session,qa_actor_id actor)
{
    struct application_native_q2 *engine=state;
    return engine&&engine->provider&&engine->provider->application->session==session&&
        (application_native_q2_source_client(engine->provider,actor)||
         application_native_q2_declared_source_client(engine->provider,actor));
}

static bool process_current(void *context, const qa_launch_instance *descriptor,
    qa_actor_owner receiver, uint64_t service_owner, qa_error *error)
{
    struct application_native_q2 *engine = context;
    application_provider *provider = engine ? engine->provider : NULL;
    if (!provider || !provider->application || !provider->launch || !descriptor ||
        provider->state.native.q2_engine != engine || !engine->prepared ||
        receiver != provider->owner || service_owner != provider->owner ||
        descriptor->identity != provider->launch->identity ||
        !descriptor->artifact || !provider->launch->artifact ||
        qa_resource_id(descriptor->artifact) != qa_resource_id(provider->launch->artifact))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 process lost its prepared source identity");
    return true;
}

static bool owner_returned(const application_provider *provider)
{
    const struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    return !engine || (!engine->baseline && !engine->calls && !engine->source_invocation &&
        !engine->input_stage && !engine->input_command && !engine->raw_inputs && !engine->movement_stage &&
        !engine->input_arsenal && qa_world_idle(engine->world) &&
        application_native_q2_callbacks_idle(engine->callbacks) &&
        application_native_q2_stages_idle(engine->stages) &&
        application_native_q2_source_actors_returned(engine->source_actors) &&
        application_native_q2_inventory_scanner_returned(engine->inventory_scanner) &&
        application_native_q2_inventory_rows_idle(engine->inventory_rows) &&
        (!engine->console || qa_console_idle(engine->console)) &&
        (!engine->cvars || qa_cvars_observer_idle(engine->cvars)) &&
        (!provider->state.native.host || qa_native_host_destroy_ready(provider->state.native.host)));
}
bool application_native_q2_idle(const application_provider *provider)
{
    const struct application_native_q2 *engine=provider?provider->state.native.q2_engine:NULL;
    return owner_returned(provider)&&(!engine||(application_native_q2_inventory_scanner_idle(engine->inventory_scanner)&&
        application_native_q2_source_actors_idle(engine->source_actors)));
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
    application_native_q2_visibility_invalidate(engine);
    if (engine->callbacks) return true;
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
    application_native_q2_visibility_invalidate(engine);
    if (engine->callbacks) return true;
    ++engine->calls;
    bool ok = qa_native_host_run_frame(engine->provider->state.native.host, true, error);
    --engine->calls;
    return ok && application_native_q2_visibility_complete(engine, error);
}

bool application_native_q2_frames_exit(void *context,qa_session *session,
    const qa_source_frame *frames,size_t count,uint64_t host_ns,qa_error *error)
{
    (void)host_ns;
    qa_application *app=context;
    if(!app||app->session!=session||(count&&!frames))
        return application_fail(error,QA_ERROR_ARGUMENT,"Native source exit lost its actual session boundary");
    const qa_source_frame *frame=NULL;
    for(size_t i=0;i<app->provider_count;++i) {
        application_provider *p=app->providers[i];
        struct application_native_q2 *n=p&&p->kind==APPLICATION_PROVIDER_NATIVE?p->state.native.q2_engine:NULL;
        if(!n||!n->callbacks||!n->initialized||!n->map_ready||!p->constructed||!p->attached||p->close_pending)continue;
        if(!frame) {
            application_provider *primary=application_world_provider(app,QA_ROLE_ENTITIES,"");
            for(size_t j=0;primary&&j<count;++j) if(frames[j].provider==primary->owner) {
                frame=frames+j;break;
            }
            if(!frame)return true;
        }
        n->frame=*frame;n->frame.provider=p->owner;n->frame.kind=p->component.clock.kind;
        if(!application_native_q2_stages_advance(n,frame,error)||
            !application_native_q2_visibility_complete(n,error))return false;
    }
    return true;
}

static void actor_released(void *state, qa_session *session, qa_actor_record actor)
{
    (void)session;
    struct application_native_q2 *engine = state;
    application_native_q2_stages_released(engine, actor.id);
    application_native_q2_source_actors_released(engine, actor.id);
    application_native_q2_wire_released(engine, actor.id);
    application_native_q2_visibility_released(engine, actor.id);
    application_native_q2_attack_released(engine, actor.id);
    application_native_q2_combat_released(engine, actor.id);
    application_native_q2_inventory_scanner_release(engine->inventory_scanner,actor.id);
    qa_error error = {0};
    if (engine->provider->state.native.host &&
        !qa_native_host_actor_released(engine->provider->state.native.host, actor, &error))
        application_fault(engine->provider->application, &error);
    for (size_t i = 1; i < 257; ++i)
        if (qa_actor_id_equal(engine->clients[i].actor, actor.id)) {
            engine->clients[i].actor = (qa_actor_id){0};
            engine->clients[i].connected = engine->clients[i].begun = false;
            engine->clients[i].denied = false;
            engine->clients[i].inventory_bound = false;
            engine->clients[i].protocol_fog = (qa_q2_wire_fog){0};
            engine->clients[i].protocol_fog_actor = (qa_actor_id){0};
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
    struct application_native_q2 *engine = opaque;
    if (application_startup_source_active(engine->provider))
        return application_startup_script_read(engine->provider, context, path, out, lease, error);
    if (application_startup_source_scripts(engine->provider))
        return application_startup_source_script_read(engine->provider, engine->console,
            context, path, out, lease, error);
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(engine->provider->launch->content, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource); *lease = resource;
    return true;
}
static void release_script(void *opaque, void *lease)
{
    struct application_native_q2 *engine = opaque;
    if (application_startup_source_active(engine->provider)) {
        application_startup_script_release(engine->provider, lease);
        return;
    }
    if (application_startup_source_scripts(engine->provider)) {
        application_startup_source_script_release(engine->provider, engine->console, lease);
        return;
    }
    qa_resource_release(lease);
}
static void script_complete(void *opaque, const qa_command_context *context,
    const char *path, bool success)
{
    struct application_native_q2 *engine = opaque;
    application_startup_script_complete(engine->provider, context, path, success);
}
static bool allow_command(void *opaque, const qa_command_invocation *command)
{
    struct application_native_q2 *engine = opaque;
    return application_startup_command_allowed(engine->provider, command);
}
static qa_cvars *cvar_owner(void *opaque, const qa_command_context *command, const char *name)
{
    struct application_native_q2 *engine = opaque;
    qa_cvars *owner = application_startup_cvar_owner(engine->provider, engine->console, command, name);
    return owner ? owner : engine->cvars;
}
static bool cvar_edit(void *opaque, const qa_command_context *command, qa_cvars *registry,
    qa_cvars_edit **out, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    return application_startup_cvar_edit(engine->provider, engine->console, command, registry, out, error);
}
static qa_cvars *visible_cvars(void *opaque, const qa_command_context *command, size_t index)
{
    struct application_native_q2 *engine = opaque;
    qa_cvars *owner = NULL;
    if (application_startup_visible_cvars(engine->provider, engine->console, command, index, &owner))
        return owner;
    return index == 0 ? engine->cvars : NULL;
}
static qa_command_result console_command(void *opaque, const qa_command_invocation *command,
                                            qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    bool handled = false;
    if (application_startup_source_active(engine->provider))
        return application_command_fallback(engine->provider->application, command, error);
    if (!engine->initialized) return QA_COMMAND_UNHANDLED;
    qa_command_result common = application_startup_common_command(engine->provider,
        engine->console, engine->cvars, command, error);
    if (common != QA_COMMAND_UNHANDLED) return common;
    bool ok = application_native_q2_game_command(engine->provider, command, &handled, error);
    if (!ok) return QA_COMMAND_FAILED;
    return handled ? QA_COMMAND_HANDLED : QA_COMMAND_UNHANDLED;
}

static bool prepare_owner(qa_application *app, application_provider *provider,
    qa_world *world, const qa_product *product, const qa_launch_choices *choices, qa_error *error)
{
    if (!app || !provider || !world || !product || !choices || product->family != QA_GAME_Q2 ||
        provider->kind != APPLICATION_PROVIDER_NATIVE || !provider->launch->artifact ||
        choices->seat_count > 256 || provider->state.native.q2_engine)
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
            &provider->state.native.module, error)) return false;
    if (provider->launch->declaration && !qa_native_declaration_load(
            qa_resource_bytes(provider->launch->declaration), provider->launch->selection.artifact,
            provider->state.native.module, &engine->declaration, error)) return false;
    if (!cgame && !engine->declaration && !whole_source_choices(engine, choices, error)) return false;
    if (!application_native_q2_callbacks_prepare(engine, error) ||
        !application_native_q2_declared_input_prepare(engine,error)) return false;
    if (!application_native_q2_publication_create(engine, &engine->publication, error)) return false;
    if (!application_native_q2_inventory_prepare(engine, error) ||
        !application_native_q2_attack_prepare(engine, error)) return false;
    if (!application_native_q2_combat_prepare(engine, error) ||
        !application_q2_control_prepare(engine, error)) return false;
    qa_console_dialect dialect = engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_CONSOLE_Q2 : QA_CONSOLE_Q2_RERELEASE;
    engine->command_context = (qa_command_context){.owner = provider->owner,
        .origin = QA_COMMAND_SERVER, .dialect = dialect};
    qa_cvar_options cvars = {.dialect = dialect};
    engine->cvars = qa_cvars_create(&cvars, error);
    if (!engine->cvars) return false;
    if (!application_startup_seed_source(provider, engine->cvars, error)) return false;
    qa_console_options console = {.context = engine->command_context, .cvars = engine->cvars,
        .user = engine, .print = console_print, .read_script = read_script, .release_script = release_script,
        .script_complete = script_complete, .allow_command = allow_command,
        .cvar_owner = cvar_owner, .visible_cvars = visible_cvars, .cvar_edit = cvar_edit,
        .source_command = console_command, .capture_context = capture_context, .context_active = context_active};
    engine->console = qa_console_create(&console, error);
    if (!engine->console) return false;
    if (!cgame && app->operation != APPLICATION_PERSISTING) {
        qa_application_startup_source source = {.descriptor = provider->launch,
            .scope = {.provider = provider->owner, .kind = QA_APPLICATION_CONSOLE_NATIVE_Q2},
            .console = engine->console, .cvars = engine->cvars,
            .command = engine->command_context, .declaration_owner = provider->owner};
        bool carried = false;
        if (!application_startup_source_carry(provider, &source, &carried, error)) return false;
        if (!carried && !application_native_q2_engine_cvars(engine->cvars, provider->owner, error)) return false;
    }
    uint32_t clients = choices->seat_count ? (uint32_t)choices->seat_count : 1;
    if(engine->callbacks) {
        const qa_json_document *d=application_native_q2_callbacks_document(engine->callbacks);
        qa_json_id declared=qa_json_get(d,qa_json_root(d),"clients");
        if(declared!=QA_JSON_NONE) {
            uint64_t maximum;
            if(!qa_json_u64(d,qa_json_get(d,declared,"maximum"),&maximum,error)||!maximum||maximum>256||maximum<clients)
                return application_fail(error,QA_ERROR_FORMAT,"Native callback clients cannot contain the actual retained seats");
            clients=(uint32_t)maximum;
        }
    }
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
    if (engine->callbacks) {
        const qa_json_document *d = application_native_q2_callbacks_document(engine->callbacks);
        qa_json_id root = qa_json_root(d), declared = qa_json_get(d, root, "cvars");
        for (size_t i = 0; i < qa_json_size(d, declared); ++i) {
            qa_json_id row = qa_json_at(d, declared, i);
            qa_buffer name = {0}, value = {0};
            bool ok = qa_json_string(d, qa_json_get(d, row, "name"), &name, error) &&
                qa_json_string(d, qa_json_get(d, row, "value"), &value, error) &&
                !memchr(name.data, 0, name.size) && !memchr(value.data, 0, value.size);
            if (ok) ok = qa_cvars_find(engine->cvars, (char *)name.data)
                ? qa_cvars_set(engine->cvars, (char *)name.data, (char *)value.data, true, error)
                : qa_cvars_register(engine->cvars, (char *)name.data, (char *)value.data, 0, provider->owner, NULL, error);
            qa_buffer_free(&name); qa_buffer_free(&value);
            if (!ok) return false;
        }
        if(qa_json_get(d,root,"clients")!=QA_JSON_NONE) {
            const qa_cvar_view *client_limit=qa_cvars_find(engine->cvars,"maxclients");
            if(!client_limit||client_limit->number!=(double)clients)
                return application_fail(error,QA_ERROR_FORMAT,"Native callback client capacity differs from actual maxclients");
        }
    }
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
        .command_actor = cgame ? NULL : command_actor,
        .actor_released = cgame ? NULL : actor_released};
    engine->prepared = true;
    return true;
}

bool application_guest_native_q2_console_prepare(qa_application *app, application_provider *provider,
    qa_world *world, const qa_product *product, const qa_launch_choices *choices,
    qa_console **console, qa_cvars **cvars, qa_command_context *command, qa_error *error)
{
    if (!app || !provider || !world || !product || !choices || !console || !cvars || !command ||
        provider->application != app || provider->constructed || provider->attached || provider->close_pending ||
        provider->product != product || application_startup_flow_provider(app, provider->owner) != provider ||
        !qa_session_safe(app->session) || !qa_world_idle(world))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 source preparation lost its actual detached candidate");
    if (!prepare_owner(app, provider, world, product, choices, error) ||
        !load_host(provider->state.native.q2_engine, error)) return false;
    struct application_native_q2 *engine = provider->state.native.q2_engine;
    *console = engine->console; *cvars = engine->cvars; *command = engine->command_context;
    return true;
}

bool application_construct_native_q2(qa_application *app, application_provider *provider,
    qa_world *world, const qa_product *product, const qa_launch_choices *choices, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine) {
        if (!prepare_owner(app, provider, world, product, choices, error)) return false;
        engine = provider->state.native.q2_engine;
        if (app->native_restore_image && engine->profile != QA_NATIVE_Q2_CGAME_API2023 &&
            !load_host(engine, error)) return false;
    } else if (!app || !world || !product || !choices || provider->application != app ||
        provider->product != product || product->family != QA_GAME_Q2 || engine->provider != provider ||
        engine->world != world || !engine->prepared || !provider->constructed || provider->attached ||
        engine->initialized || engine->map_ready || engine->shutting_down || engine->activation_failed ||
        !provider->state.native.host || !application_native_q2_idle(provider) ||
        (qa_native_get_lifecycle(qa_native_host_instance(provider->state.native.host)) != QA_NATIVE_LOADED &&
            !qa_native_process_restore_pending(qa_native_host_instance(provider->state.native.host))))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 construction lost its retained prepared owner");
    if (provider->state.native.host && qa_native_process_restore_pending(qa_native_host_instance(provider->state.native.host)))
        return true;
    return application_startup_source_preinit(provider, engine->console, engine->cvars,
        &engine->command_context, error) && (app->operation == APPLICATION_PERSISTING ||
        qa_cvars_apply_latched(engine->cvars, NULL, error));
}

static bool load_host(struct application_native_q2 *engine, qa_error *error)
{
    application_provider *provider = engine->provider;
    if (engine->activation_failed) {
        if (error) *error = engine->activation_error;
        return false;
    }
    if (provider->state.native.host) return true;
    if (!engine->prepared || engine->calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 loading requires its complete idle source owner");
    uint64_t interval = provider->component.clock.interval_ns;
    if (engine->profile == QA_NATIVE_Q2_CGAME_API2023 && !engine->application &&
        provider->application->native_q2_services &&
        !provider->application->native_q2_services(provider->application->guest_context,
            provider->application, provider->owner, engine->profile, &engine->platform,
            &engine->application, &engine->application_context, error)) return false;
    qa_native_host_instance_options instance = {.declaration = engine->declaration,
        .observe = engine->source_attack != NULL || engine->source_combat != NULL ||
            engine->source_control != NULL || engine->primary_inventory != NULL ||
            application_native_q2_callbacks_observation_required(engine->callbacks),
        .runner = provider->application->native_runner,
        .tick_rate = interval ? (uint32_t)(UINT64_C(1000000000) / interval) : 0,
        .frame_seconds = (float)interval / 1000000000.f,
        .frame_milliseconds = (uint32_t)(interval / UINT64_C(1000000))};
    qa_native_module_info module = qa_native_module_describe(provider->state.native.module);
    qa_native_process_resource_artifact artifact = {.resource = provider->launch->artifact,
        .acquisition = provider->launch->artifact_acquisition, .path = provider->launch->selection.artifact};
    const qa_native_process_resources *capture = NULL;
    qa_bytes lower_recipe = {0};
    if (provider->application->native_restore_image && !engine->process.resources) {
        const qa_application_native_resource_refs *refs = provider->application->native_restore_resources;
        const qa_save_record *saved = qa_save_image_find(provider->application->native_restore_image,
            QA_SAVE_PROVIDER, provider->launch->selection.instance);
        qa_bytes recipe = {0};
        if (!refs || !refs->resolve)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 cold construction requires its retained external graph resolver");
        if (!saved ||
            !application_native_q2_save_resource_recipe(saved, &recipe, error) ||
            !refs->resolve(refs->context, provider->launch->selection.instance,
                qa_resource_id(provider->launch->artifact), recipe, &capture, &lower_recipe, error)) return false;
    }
    if (!application_native_process_prepare(provider->application, provider->launch,
        provider->owner, provider->owner, &artifact, 1, 0, &module.image,
        process_current, engine, capture, lower_recipe, &engine->process, error)) return false;
    instance.process = &engine->process.process;
    bool ok;
    ++engine->calls;
    engine->host_constructing = true;
    if (engine->profile == QA_NATIVE_Q2_CGAME_API2023) {
        qa_native_host_q2_cgame_options options = {.instance = instance,
            .engine = application_native_q2_services(engine), .application = application_native_q2_import,
            .application_context = engine, .cvars = engine->cvars,
            .console = engine->console, .command_context = engine->command_context};
        uint32_t seat = UINT32_MAX;
        for (size_t i = 1; i < 257; ++i) if (engine->clients[i].reserved) {
            if (seat != UINT32_MAX) { engine->host_constructing = false; --engine->calls; return application_fail(error, QA_ERROR_ARGUMENT,
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
                .address_for_actor = application_native_q2_address, .bind_actor = application_native_q2_bind,
                .reserved_source_slot = engine->callbacks ? application_native_q2_callbacks_reserved_slot : NULL},
            .services = {.engine = application_native_q2_services(engine),
                .movement = application_native_q2_movement_services(engine),
                .application = engine->application, .application_context = engine->application_context},
            .cvars = engine->cvars, .console = engine->console, .command_context = engine->command_context};
        ok = qa_native_host_create_q2_game(provider->state.native.module, &options,
            &provider->state.native.host, error);
    }
    engine->host_constructing = false;
    --engine->calls;
    if(ok&&engine->primary_inventory&&!engine->inventory_scanner) {
        ok=application_native_q2_inventory_rows_create(engine,&engine->inventory_rows,error);
        if(ok) {
            application_native_q2_inventory_scanner_options options=application_native_q2_inventory_rows_options(engine->inventory_rows);
            ok=application_native_q2_inventory_scanner_create(engine,&options,&engine->inventory_scanner,error);
        }
    }
    if (!ok) {
        engine->activation_failed = true;
        engine->activation_error = error ? *error : (qa_error){0};
        if (engine->activation_error.code == QA_OK)
            qa_error_set(&engine->activation_error, QA_ERROR_UNSUPPORTED, 0,
                "Native Q2 source owner activation failed");
        if (error) *error = engine->activation_error;
    }
    return ok;
}

bool application_native_q2_activate(struct application_native_q2 *engine, qa_error *error)
{
    if (!engine || !engine->provider->constructed || !engine->provider->attached || engine->calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 loading requires its committed owner");
    return load_host(engine, error);
}

static bool declared_initialize(struct application_native_q2 *engine,qa_error *error)
{
    if(!engine->callbacks||engine->provider->application->operation==APPLICATION_PERSISTING)return true;
    qa_source_frame frame;
    if(!application_native_q2_stages_time_read(engine,&frame,error))return false;
    application_native_callback_value values[]={
        {.name="time",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)frame.time_ns/1e9},
        {.name="elapsed",.kind=APPLICATION_NATIVE_VALUE_NUMBER,.value.number=(double)frame.elapsed_ns/1e9}
    };
    application_native_callback_inputs inputs={values,2,{0}};
    bool accepted;
    return application_native_q2_callbacks_run(engine,"initialize",&inputs,&accepted,error)&&
        (accepted||application_fail(error,QA_ERROR_ARGUMENT,"Declared native source rejected initialization"));
}

bool application_native_q2_initialize_supplemental(application_provider *provider,
    qa_string_id name,qa_string_id spawn,qa_error *error)
{
    struct application_native_q2 *engine=provider?provider->state.native.q2_engine:NULL;
    qa_application *app=provider?provider->application:NULL;
    application_provider *primary=app?application_world_provider(app,QA_ROLE_ENTITIES,""):NULL;
    if(!engine||!app||!primary||primary==provider||!primary->attached||!primary->map_bound||
        !provider->attached||!provider->constructed||!engine->callbacks||engine->map_ready||
        engine->profile==QA_NATIVE_Q2_CGAME_API2023||engine->world!=app->world||
        app->operation==APPLICATION_PERSISTING||!application_native_q2_idle(provider))
        return application_fail(error,QA_ERROR_ARGUMENT,"Supplemental native initialization lost its published primary world");
    qa_source_frame frame;
    if(!application_native_q2_stages_time_read(engine,&frame,error))return false;
    engine->frame=frame;engine->frame.provider=provider->owner;engine->frame.kind=provider->component.clock.kind;
    engine->map_name=name;engine->spawn_point=spawn;
    if(!engine->world_actor.registry&&!qa_session_allocate(app->session,provider->owner,
        engine->definition,true,0,&engine->world_actor,error))return false;
    if(!application_native_q2_activate(engine,error)||
        !qa_native_host_world_actor_bind(provider->state.native.host,engine->world_actor,error)||
        !application_unified_event_registration_clear(app,provider->owner,error))return false;
    for(uint32_t i=0;i<engine->configstring_count;++i){free(engine->configstrings[i]);engine->configstrings[i]=NULL;}
    application_native_q2_wire_destroy(&engine->wire_engine);
    application_native_q2_visibility_destroy(&engine->visibility);
    if(!application_native_q2_combat_load(engine,error))return false;
    ++engine->calls;
    bool ok=application_native_q2_callbacks_validate(engine,error);
    if(ok&&!engine->initialized){
        ok=qa_native_host_initialize(provider->state.native.host,0,0,false,error);
        if(ok)engine->initialized=true;
    }
    if(ok)ok=application_native_q2_callbacks_arrays_validate(engine,error)&&
        application_native_q2_stages_prepare(engine,error)&&
        application_native_q2_wire_begin(engine,error)&&
        application_native_q2_attack_activate(engine,error)&&
        application_native_q2_combat_activate(engine,error)&&
        application_q2_control_activate(engine,error)&&
        qa_native_host_source_reconcile(provider->state.native.host,error)&&
        declared_initialize(engine,error);
    --engine->calls;
    if(!ok)return false;
    engine->map_ready=provider->map_bound=true;
    qa_cvars_set_server_active(engine->cvars,true);
    if(engine->inventory_rows&&(!application_native_q2_inventory_rows_prepare(engine->inventory_rows,error)||
        !application_native_q2_inventory_scanner_activate(engine->inventory_scanner,error)))return false;
    return application_native_q2_callbacks_register(engine,error)&&
        application_native_q2_publication_activate(engine->publication,error);
}

bool application_native_q2_entity_text(const qa_bsp_view *map, char **out, qa_error *error)
{
    qa_bytes text = map->lumps[QA_BSP_ENTITIES].bytes;
    if (text.size == SIZE_MAX) return application_fail(error, QA_ERROR_MEMORY, "Native Q2 entity text extent overflow");
    char *copy = malloc(text.size + 1);
    if (!copy) return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q2 entity text");
    if (text.size) memcpy(copy, text.data, text.size);
    copy[text.size] = 0;
    *out = copy;
    return true;
}

bool application_native_q2_spawn_map(application_provider *provider, const qa_bsp_view *map,
    const qa_entities *entities, qa_string_id name, qa_string_id spawn, qa_error *error)
{
    (void)entities;
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !map || engine->profile == QA_NATIVE_Q2_CGAME_API2023 || !application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 map publication requires an idle game owner");
    char *copy = NULL;
    if (!application_native_q2_entity_text(map, &copy, error)) return false;
    if (!engine->world_actor.registry && !qa_session_allocate(provider->application->session,
            provider->owner, engine->definition, true, 0, &engine->world_actor, error)) { free(copy); return false; }
    free(engine->entity_text); engine->entity_text = copy;
    engine->map_name = name; engine->spawn_point = spawn;
    if (!application_native_q2_activate(engine, error)) return false;
    if (!qa_native_host_world_actor_bind(provider->state.native.host, engine->world_actor, error)) return false;
    if (!application_unified_event_registration_clear(provider->application, provider->owner, error)) return false;
    for (uint32_t i = 0; i < engine->configstring_count; ++i) {
        free(engine->configstrings[i]); engine->configstrings[i] = NULL;
    }
    provider->application->physics->world_actor = engine->world_actor;
    for (size_t i = 1; i < 257; ++i) {
        engine->clients[i].protocol_fog = (qa_q2_wire_fog){0};
        engine->clients[i].protocol_fog_actor = engine->clients[i].actor;
    }
    if (!application_native_q2_combat_load(engine, error)) return false;
    bool original = application_q2_original_source(provider);
    if (original && (!application_q2_original_prepare(provider,error) ||
        !application_native_q2_prepare_restore(provider,error))) return false;
    application_native_q2_wire_destroy(&engine->wire_engine);
    application_native_q2_visibility_destroy(&engine->visibility);
    ++engine->calls;
    bool ok = application_native_q2_callbacks_validate(engine, error) &&
        (provider->application->operation == APPLICATION_PERSISTING ||
         application_native_q2_stages_prepare(engine, error));
    if (ok && !engine->initialized) {
        ok = qa_native_host_initialize(provider->state.native.host, 0, 0, false, error);
        if (ok) engine->initialized = true;
    }
    if (ok && original) ok = application_q2_original_game(provider,error);
    if (ok && !engine->wire_engine) ok = application_native_q2_wire_begin(engine, error);
    if (ok && !original) ok = application_native_q2_attack_activate(engine, error);
    if (ok && !original) ok = application_native_q2_combat_activate(engine, error);
    if (ok && !original) ok = application_q2_control_activate(engine, error);
    if (ok) ok = qa_native_host_source_reconcile(provider->state.native.host, error);
    const char *source_entities = copy;
    qa_buffer declared_entities = {0};
    bool spawn_entities = true;
    if (ok && engine->callbacks) {
        const qa_json_document *d = application_native_q2_callbacks_document(engine->callbacks);
        qa_json_id spawn_entities_id = qa_json_get(d, qa_json_root(d), "spawnEntities");
        spawn_entities = qa_json_type(d, spawn_entities_id) != QA_JSON_NULL;
        if (spawn_entities) {
            ok = qa_json_string(d, spawn_entities_id, &declared_entities, error) &&
                !memchr(declared_entities.data, 0, declared_entities.size);
            source_entities = (char *)declared_entities.data;
        }
    }
    if (ok && spawn_entities) ok = qa_native_host_spawn_entities(provider->state.native.host,
        qa_strings_cstr(qa_session_strings(provider->application->session), name), source_entities,
        spawn ? qa_strings_cstr(qa_session_strings(provider->application->session), spawn) : "", error);
    qa_buffer_free(&declared_entities);
    if (ok && original) ok = application_q2_original_level(provider,error);
    if(ok) ok=application_native_q2_callbacks_arrays_validate(engine,error);
    if(ok)ok=declared_initialize(engine,error);
    --engine->calls;
    if (ok) {
        engine->map_ready = provider->map_bound = true;
        qa_cvars_set_server_active(engine->cvars, true);
        if (original) return application_native_q2_restore_finish(provider,error) &&
            application_native_q2_publication_activate(engine->publication,error);
        if(engine->inventory_rows) ok=application_native_q2_inventory_rows_prepare(engine->inventory_rows,error)&&
            application_native_q2_inventory_scanner_activate(engine->inventory_scanner,error);
        if(ok) ok = application_native_q2_callbacks_register(engine,error) &&
            application_native_q2_publication_activate(engine->publication, error);
    }
    return ok;
}

bool application_native_q2_retire_map(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine) return true;
    if (!application_native_q2_callbacks_drain(engine, error)) return false;
    if (!owner_returned(provider))
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
                if (qa_native_get_lifecycle(qa_native_host_instance(provider->state.native.host)) == QA_NATIVE_SHUT_DOWN)
                    engine->initialized = false;
                if (!ok) return false;
            }
            engine->initialized = false;
        }
        if (!qa_native_host_destroy_ready(provider->state.native.host))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 cgame map shutdown has not drained");
        bool ok = qa_native_host_destroy_owned(&provider->state.native.host, error);
        if (!ok) return false;
        if (!application_native_process_release(&engine->process, error)) return false;
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
    if(!application_native_q2_inventory_scanner_suspend(engine->inventory_scanner,error)) return false;
    for (uint32_t i = 1; i < 257; ++i)
        if (engine->clients[i].actor.registry && !application_native_q2_client_disconnect(provider, i, error)) return false;
    if (!application_q2_control_suspend(engine, error)) return false;
    if (!application_native_q2_callbacks_suspend(engine, error)) return false;
    if (!application_native_q2_source_actors_suspend(engine, error)) return false;
    if (!application_native_q2_publication_retire(engine->publication, error)) return false;
    application_native_q2_visibility_destroy(&engine->visibility);
    engine->map_ready = provider->map_bound = false;
    qa_cvars_set_server_active(engine->cvars, false);
    return true;
}

bool application_native_q2_deconstruct(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine) return true;
    if (!application_native_q2_callbacks_drain(engine, error)) return false;
    if (!owner_returned(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 teardown requires drained callbacks");
    if (!application_native_q2_combat_suspend(engine, error)) return false;
    if (!application_native_q2_stages_close(engine, error)) return false;
    if (!application_native_q2_source_actors_close(engine, error)) return false;
    if(!application_native_q2_inventory_scanner_destroy(engine->inventory_scanner,error)) return false;
    engine->inventory_scanner=NULL;
    if(!application_native_q2_inventory_rows_destroy(engine->inventory_rows,error)) return false;
    engine->inventory_rows=NULL;
    if (!application_native_q2_inventory_close(engine, error)) return false;
    bool terminal = provider->state.native.host && qa_native_terminal(qa_native_host_instance(provider->state.native.host));
    if (terminal && !qa_native_host_terminal_retired(provider->state.native.host))
        return application_fail(error, QA_ERROR_ARGUMENT, "Terminal native Q2 cleanup requires actual canonical actor retirement");
    if (!application_native_q2_callbacks_close(engine, error)) return false;
    qa_error first = {0}; bool ok = true;
    if (provider->state.native.host) {
        if (terminal) engine->initialized = false;
        if (engine->initialized && !engine->shutting_down) {
            engine->shutting_down = true;
            ++engine->calls;
            ok = qa_native_host_shutdown(provider->state.native.host, false, &first);
            --engine->calls;
            if (qa_native_get_lifecycle(qa_native_host_instance(provider->state.native.host)) == QA_NATIVE_SHUT_DOWN)
                engine->initialized = false;
            if (!ok) {
                engine->shutting_down = false;
                if (error) *error = first;
                return false;
            }
            engine->initialized = false;
        }
        if (!application_native_q2_attack_close(engine, error)) return false;
        if (!application_native_q2_combat_close(engine, error)) return false;
        if (!application_q2_control_close(engine, error)) return false;
        if (!application_native_q2_callbacks_close(engine, error)) return false;
        qa_error cleanup = {0};
        if (!qa_native_host_destroy_ready(provider->state.native.host))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 host teardown has not drained");
        ++engine->calls;
        bool closed = qa_native_host_destroy_owned(&provider->state.native.host, &cleanup);
        --engine->calls;
        if (!closed) { if (error) *error = cleanup; return false; }
    }
    if (!application_native_q2_attack_close(engine, error)) return false;
    if (!application_native_q2_combat_close(engine, error)) return false;
    if (!application_q2_control_close(engine, error)) return false;
    if (!application_native_q2_callbacks_close(engine, error)) return false;
    if (!application_native_process_release(&engine->process, error)) return false;
    if (!application_unified_event_registration_clear(provider->application, provider->owner, error)) return false;
    if (engine->console && engine->cvars &&
        !application_startup_source_retire(provider, engine->console, engine->cvars, error)) return false;
    if (engine->platform.release_frontend)
        engine->platform.release_frontend(engine->platform.frontend_lifetime);
    qa_console_destroy(engine->console); qa_cvars_destroy(engine->cvars);
    application_native_q2_publication_destroy(&engine->publication);
    application_native_q2_wire_destroy(&engine->wire_engine);
    application_native_q2_visibility_destroy(&engine->visibility);
    qa_native_declaration_destroy(engine->declaration);
    qa_command_tokens_free(&engine->arguments);
    if (engine->configstrings)
        for (uint32_t i = 0; i < engine->configstring_count; ++i) free(engine->configstrings[i]);
    application_network_q2_retire_bindings(engine);
    free(engine->configstrings); free(engine->entity_text); free(engine);
    provider->state.native.q2_engine = NULL;
    if (!ok && error) *error = first;
    return ok;
}
