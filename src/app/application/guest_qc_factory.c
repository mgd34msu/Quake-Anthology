#include "guest_qc_factory.h"
#include "q3_product.h"
#include "guest_qc_profile.h"
#include "startup_flow.h"
#include <stdio.h>

static void controls_bind(struct application_qc_state *engine)
{
    static const char *const names[] = {"sv_gravity", "sv_stopspeed", "sv_spectatormaxspeed", "sv_accelerate", "sv_airaccelerate",
        "sv_wateraccelerate", "sv_friction", "sv_waterfriction", "sv_maxspeed"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        engine->cvar_handles.qw_movement[i] = qa_cvars_resolve(engine->cvars, names[i]);
    engine->cvar_handles.sv_aim = qa_cvars_resolve(engine->cvars, "sv_aim");
    engine->cvar_handles.teamplay = qa_cvars_resolve(engine->cvars, "teamplay");
}

bool application_qc_console_prepare(qa_application *app, application_provider *provider,
    qa_world *world, const qa_product *product, const qa_launch_choices *choices,
    qa_console **console, qa_cvars **cvars, qa_command_context *command, qa_error *error)
{
    if (!app || !provider || provider->application != app || !provider->launch || !world ||
        !product || !choices || !console || !cvars || !command ||
        provider->kind != APPLICATION_PROVIDER_QC || !provider->state.qc.program ||
        provider->state.qc.game || provider->state.qc.instance || provider->attached ||
        provider->close_pending || choices->seat_count >= UINT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC console preparation requires its actual unconstructed source");
    qa_qc_profile selected = product->edition == QA_EDITION_RERELEASE ? QA_QC_RERELEASE :
        provider->launch->selection.clock.kind == QA_CLOCK_QUAKEWORLD ? QA_QC_QUAKEWORLD : QA_QC_NETQUAKE;
    struct application_qc_state *engine = provider->state.qc.engine;
    if (engine) {
        if (!engine->console_prepared || engine->provider != provider || engine->world != world ||
            engine->services.session != app->session || engine->profile != selected ||
            engine->actor_capacity != qa_actors_capacity(qa_session_actors(app->session)) ||
            !engine->console || !engine->cvars || !qa_console_idle(engine->console) ||
            !qa_cvars_observer_idle(engine->cvars))
            return application_fail(error, QA_ERROR_ARGUMENT, "QC prepared console changed its physical owner");
        *console = engine->console; *cvars = engine->cvars; *command = engine->command_context;
        return true;
    }
    engine = calloc(1, sizeof(*engine));
    if (!engine) return application_fail(error, QA_ERROR_MEMORY, "Allocating prepared QC source owner");
    provider->state.qc.engine = engine;
    engine->provider = provider; engine->world = world; engine->loading = true; engine->check_cluster = -1;
    engine->services = application_builtin_services(app, world, app->physics);
    engine->profile = selected;
    qa_qc_program_info program = qa_qc_program_describe(provider->state.qc.program);
    if ((selected == QA_QC_QUAKEWORLD) != (program.api == QA_QC_API_QUAKEWORLD))
        return application_fail(error, QA_ERROR_FORMAT, "QC selected source profile differs from its loaded program ABI");
    engine->protocol = (qa_net_protocol_id){selected == QA_QC_QUAKEWORLD ? QA_NET_QW28 : QA_NET_NQ15, 0, 0};
    const struct application_qc_profile *profile = provider->state.qc.qualified;
    engine->max_clients = profile && profile->clients ? profile->maximum_clients :
        !profile && program.api == QA_QC_API_QUAKEWORLD ? 32 :
        choices->seat_count ? (uint32_t)choices->seat_count : 1;
    engine->actor_capacity = qa_actors_capacity(qa_session_actors(app->session));
    qa_console_dialect dialect = selected == QA_QC_QUAKEWORLD ? QA_CONSOLE_QW : QA_CONSOLE_Q1;
    qa_cvar_options options = {.dialect = dialect,
        .side = QA_CVAR_SIDE_SERVER, .role = QA_CVAR_ROLE_GAME};
    engine->cvars = qa_cvars_create_view(app->cvars, &options, error);
    if (engine->cvars && !application_startup_seed_source(provider, engine->cvars, error)) return false;
    engine->command_context = (qa_command_context){.owner = provider->owner, .dialect = dialect, .origin = QA_COMMAND_SERVER};
    if (!engine->cvars ||
        !application_qc_create_console(engine, engine->cvars, &engine->console, error)) return false;
    char maximum[16]; snprintf(maximum, sizeof(maximum), "%u", engine->max_clients);
    static const struct { const char *name; qa_cvar_save_policy policy; } names[] = {
        {"skill", QA_CVAR_SAVE_GAMEPLAY},
        {"deathmatch", QA_CVAR_SAVE_GAMEPLAY},
        {"coop", QA_CVAR_SAVE_GAMEPLAY},
        {"teamplay", QA_CVAR_SAVE_GAMEPLAY},
        {"sv_gravity", QA_CVAR_SAVE_GAMEPLAY},
        {"sv_aim", QA_CVAR_SAVE_GAMEPLAY},
        {"sv_maxspeed", QA_CVAR_SAVE_GAMEPLAY},
        {"maxclients", QA_CVAR_SAVE_GAMEPLAY},
        {"registered", QA_CVAR_SAVE_SETTING},
        {"developer", QA_CVAR_SAVE_SETTING},
        {"sv_cheats", QA_CVAR_SAVE_SETTING},
        {"samelevel", QA_CVAR_SAVE_GAMEPLAY},
        {"timelimit", QA_CVAR_SAVE_GAMEPLAY},
        {"fraglimit", QA_CVAR_SAVE_GAMEPLAY},
        {"gamecfg", QA_CVAR_SAVE_GAMEPLAY}};
    bool registered;
    if (!qa_catalog_q1_registered(app->catalog, product->id, &registered, error)) return false;
    const char *values[] = {"1", "0", "0", "0", "800", selected == QA_QC_QUAKEWORLD ? "2" : "0.93", "320",
        maximum, registered ? "1" : "0", "0", "0", "0", "0", "0", "0"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (!qa_cvars_register(engine->cvars, names[i].name, values[i], 0, provider->owner, NULL, error) ||
            !qa_cvars_declare_save_policy(engine->cvars, names[i].name, names[i].policy, error)) return false;
    if (selected == QA_QC_QUAKEWORLD) {
        static const char *const qw_names[] = {"sv_phs", "sv_stopspeed", "sv_spectatormaxspeed", "sv_accelerate",
            "sv_airaccelerate", "sv_wateraccelerate", "sv_friction", "sv_waterfriction"};
        static const char *const qw_values[] = {"1", "100", "500", "10", "0.7", "10", "4", "4"};
        size_t count = profile ? 1 : sizeof(qw_names) / sizeof(*qw_names);
        for (size_t i = 0; i < count; ++i)
            if (!qa_cvars_register(engine->cvars, qw_names[i], qw_values[i], 0, provider->owner, NULL, error) ||
                !qa_cvars_declare_save_policy(engine->cvars, qw_names[i],
                    i ? QA_CVAR_SAVE_GAMEPLAY : QA_CVAR_SAVE_SETTING, error)) return false;
        static const char *const policy_names[] = {"password", "spectator_password", "sv_highchars", "maxspectators"};
        static const char *const policy_values[] = {"", "", "1", "8"};
        for (size_t i = 0; i < sizeof(policy_names) / sizeof(*policy_names); ++i)
            if (!qa_cvars_register(engine->cvars, policy_names[i], policy_values[i],
                i == 3 ? QA_CVAR_SERVERINFO : 0, provider->owner, NULL, error) ||
                !qa_cvars_declare_save_policy(engine->cvars, policy_names[i], QA_CVAR_SAVE_SETTING, error)) return false;
    }

    for (size_t i = 0; profile && i < profile->cvar_count; ++i) {
        const application_qc_cvar *entry = &profile->cvars[i];
        if (!qa_cvars_register(engine->cvars, entry->name, entry->value,
                0, provider->owner, NULL, error)) return false;
        const qa_cvar_view *actual = qa_cvars_find(engine->cvars, entry->name);
        if (actual->save_policy == QA_CVAR_SAVE_UNCLASSIFIED &&
            !qa_cvars_declare_save_policy(engine->cvars, entry->name, QA_CVAR_SAVE_GAMEPLAY, error)) return false;
    }
    controls_bind(engine);
    qa_builtin_random_seed(&engine->random, (uint32_t)(provider->owner * UINT32_C(2654435761)));
    engine->console_prepared = true;
    *console = engine->console; *cvars = engine->cvars; *command = engine->command_context;
    return true;
}

bool application_qc_console_destroy(application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_QC)
        return application_fail(error, QA_ERROR_ARGUMENT, "QC prepared console retirement requires its source provider");
    return application_qc_deconstruct(provider, error);
}
