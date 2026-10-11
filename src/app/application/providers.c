#include "internal.h"
#include "qa/source_frame_time.h"
#include "qa/tools.h"
#include "q3_campaign_launch.h"
#include "network_q1_signon.h"
#include "network_q2_private.h"
#include "guest_native_q2_private.h"
#include "q1_weapon_rules.h"
#include "native_q3_console.h"
#include "native_q3_remote_role.h"
#include "native_client_roles.h"
#include "native_q1_console.h"
#include "native_q1_wire.h"
#include "native_q1_respawn.h"
#include "native_q1_powers.h"
#include "native_q1_composition_flags.h"
#include "native_q1_composition_death.h"
#include "native_q1_composition_rogue.h"
#include "native_q1_composition_birth.h"
#include "native_q2_console.h"
#include "native_q2_arsenal.h"
#include "native_q2_combat_policy.h"
#include "bots_npc.h"
#include "startup_flow.h"
#include "save_private.h"
#include "engine_shutdown.h"
#include "supplies.h"
#include "equipment_actions.h"
#include "guest_q3_save.h"
#include "guest_q3_factory.h"
#include "guest_q3_weapons_services.h"
#include "guest_qc_factory.h"
#include "guest_qc_internal.h"
#include "guest_qc_original_save.h"
#include "native_q3_wire_state.h"
#include "native_q3_wire.h"
#include "native_q3_settings.h"
#include "native_q3_session.h"
#include "native_q3_votes.h"
#include "native_q3_clients.h"
#include "native_q3_objectives.h"
#include "native_q3_team_status.h"
#include "native_q3_end_frame.h"
#include "native_q3_ipfilters.h"
#include "native_q3_log.h"
#include "native_q3_postgame.h"
#include "unified_q1_events.h"
#include "unified_q2_native_events.h"
#include "unified_q3_events.h"
#include "unified_events.h"
#include "native_q3_team_combat.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "qa/game_q2_bots.h"
#include "qa/game_q2_wire.h"
#include "native_maps.h"
#include "rankings.h"
#include "q3_world_restart.h"
#include "control_frame.h"
#include "arsenal_profile.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool kind_for(const qa_launch_instance *launch,
                     application_provider_kind *out, qa_error *error)
{
    switch (launch->selection.runtime) {
    case QA_PROGRAM_QUAKEC:
        *out = APPLICATION_PROVIDER_QC;
        return true;
    case QA_PROGRAM_QVM:
        *out = APPLICATION_PROVIDER_QVM;
        return true;
    case QA_PROGRAM_NATIVE:
        *out = APPLICATION_PROVIDER_NATIVE;
        return true;
    case QA_PROGRAM_BUILTIN: {
        const qa_ruleset_descriptor *rules = qa_ruleset_read(launch->selection.clock.kind);
        if (rules) {
            switch (rules->family) {
            case QA_GAME_Q1: *out = APPLICATION_PROVIDER_Q1; return true;
            case QA_GAME_Q2: *out = APPLICATION_PROVIDER_Q2; return true;
            case QA_GAME_Q3: *out = APPLICATION_PROVIDER_Q3; return true;
            }
        }
        break;
    }
    }
    return application_fail(error, QA_ERROR_ARGUMENT,
                            "provider has no executable runtime family");
}

bool application_provider_prepare(qa_application *application,
                                  const qa_launch_instance *launch,
                                  application_provider **out,
                                  qa_error *error)
{
    if (application == NULL || launch == NULL || out == NULL ||
        launch->selection.instance == NULL ||
        launch->selection.instance[0] == '\0')
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "invalid application provider selection");
    *out = NULL;
    qa_actor_owner owner;
    qa_string_id trigger_teleport;
    if (!qa_strings_intern_cstr(qa_session_strings(application->session),
                                launch->selection.instance, &owner, error) ||
        !qa_strings_intern_cstr(qa_session_strings(application->session),
                                "trigger_teleport", &trigger_teleport, error))
        return false;
    application_provider *provider = calloc(1, sizeof(*provider));
    if (provider == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot allocate application provider");
    provider->application = application;
    provider->owner = owner;
    provider->trigger_teleport_class = trigger_teleport;
    if (!kind_for(launch, &provider->kind, error)) {
        free(provider);
        return false;
    }

    if (!qa_launch_instance_retain_metadata(launch, &provider->launch_lease, error)) {
        free(provider);
        return false;
    }
    provider->launch = qa_launch_instance_lease_view(provider->launch_lease);

    if (provider->kind == APPLICATION_PROVIDER_QC) {
        if (launch->artifact == NULL ||
            !qa_qc_program_load(qa_resource_bytes(launch->artifact),
                                launch->selection.artifact,
                                &provider->state.qc.program, error)) {
            qa_launch_instance_lease_release(provider->launch_lease);
            free(provider);
            return false;
        }
        if (!application_qc_qualify(provider, error)) {
            application_qc_release_qualification(provider);
            qa_qc_program_destroy(provider->state.qc.program);
            qa_launch_instance_lease_release(provider->launch_lease);
            free(provider);
            return false;
        }
    } else if (provider->kind == APPLICATION_PROVIDER_QVM) {
        if (launch->artifact == NULL ||
            !qa_qvm_image_load(qa_resource_bytes(launch->artifact),
                               &provider->state.qvm.image, error)) {
            qa_launch_instance_lease_release(provider->launch_lease);
            free(provider);
            return false;
        }
    }
    provider->next_live = application->live_providers;
    if (provider->next_live != NULL)
        provider->next_live->previous_live = provider;
    application->live_providers = provider;
    ++application->provider_states;
    *out = provider;
    return true;
}

static bool selected_mode(const qa_launch_choices *choices,
                          qa_mode_rules *rules)
{
    if (choices == NULL || choices->mode_count == 0)
        return false;
    size_t selected = 0;
    for (size_t index = 0; index < choices->mode_count; ++index)
        if (choices->modes[index].primary_score) {
            selected = index;
            break;
        }
    *rules = choices->modes[selected].rules;
    return true;
}

typedef struct application_native_profile {
    qa_game_family family;
    qa_mode_kind mode_kind;
    int32_t skill, teamplay;
    uint32_t maximum_clients, gamecfg;
    bool cooperative, deathmatch, friendly_fire;
} application_native_profile;

static bool native_profile(const qa_launch_instance *launch,
                           const qa_launch_choices *choices,
                           application_native_profile *out,
                           qa_error *error)
{
    if (launch == NULL || choices == NULL || out == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "native construction profile needs launch choices");
    const qa_ruleset_descriptor *ruleset = qa_ruleset_read(launch->selection.clock.kind);
    if (!ruleset)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "provider clock has no native game family");
    qa_game_family family = ruleset->family;

    qa_mode_rules mode = family == QA_GAME_Q3
                             ? qa_mode_defaults(QA_MODE_Q3, QA_MODE_FFA)
                             : (qa_mode_rules){0};
    bool has_mode = selected_mode(choices, &mode);
    bool cooperative = has_mode && mode.kind == QA_MODE_COOPERATIVE;
    bool deathmatch = has_mode && mode.kind != QA_MODE_COOPERATIVE &&
                      mode.kind != QA_MODE_SINGLE_PLAYER;
    application_native_profile profile = {
        .family = family,
        .mode_kind = mode.kind,
        .cooperative = cooperative,
        .deathmatch = deathmatch,
        .friendly_fire = mode.friendly_fire,
    };
    if (family == QA_GAME_Q1) {
        if (choices->seat_count > UINT32_MAX)
            return application_fail(error, QA_ERROR_MEMORY,
                                    "Q1 client roster exceeds native capacity");
        profile.skill = choices->world.skill < 0
                            ? 0
                            : choices->world.skill > 3
                                  ? 3
                                  : choices->world.skill;
        profile.teamplay = has_mode ? mode.teamplay : 0;
        profile.maximum_clients = launch->selection.clock.kind == QA_RULESET_QUAKEWORLD
            ? 32u : cooperative || deathmatch ? 16u : 1u;
        for (size_t index = 0; index < choices->mode_count; ++index)
            if (choices->modes[index].rules.enabled &&
                choices->modes[index].rules.source == QA_MODE_ROGUE &&
                choices->modes[index].rules.relics &&
                strcmp(choices->modes[index].instance,
                       launch->selection.instance) == 0)
                profile.gamecfg |= 1;
    } else if (family == QA_GAME_Q2) {
        profile.skill = choices->world.skill;
    }
    *out = profile;
    return true;
}

static void profile_word(uint8_t *encoded, size_t *size, uint64_t value)
{
    for (size_t index = 0; index < 8; ++index)
        encoded[(*size)++] = (uint8_t)(value >> (index * 8));
}

static bool q2_selected_options(const qa_launch_provider *, uint64_t, const qa_product *,
    const qa_launch_choices *, qa_q2_options *, qa_error *);

bool application_instance_configuration(void *opaque,
                                        const qa_launch_instance *launch,
                                        const qa_launch_choices *choices,
                                        qa_buffer *out,
                                        qa_error *error)
{
    (void)opaque;
    if (out == NULL || out->data || out->size)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "provider configuration needs empty byte output");
    application_native_profile profile;
    if (!native_profile(launch, choices, &profile, error))
        return false;
    uint8_t encoded[9 * 8];
    size_t size = 0;
    profile_word(encoded, &size, profile.family);
    switch (profile.family) {
    case QA_GAME_Q1:
        profile_word(encoded, &size, (uint32_t)profile.skill);
        profile_word(encoded, &size, (uint32_t)profile.teamplay);
        profile_word(encoded, &size, profile.cooperative);
        profile_word(encoded, &size, profile.deathmatch);
        profile_word(encoded, &size, profile.gamecfg);
        break;
    case QA_GAME_Q2:
        profile_word(encoded, &size, (uint32_t)profile.skill);
        profile_word(encoded, &size, profile.cooperative);
        profile_word(encoded, &size, profile.deathmatch);
        if (launch->selection.runtime == QA_PROGRAM_BUILTIN) {
            qa_catalog *catalog = qa_launch_instance_catalog(launch);
            const qa_product *product = catalog
                ? qa_catalog_product(catalog, launch->selection.product) : NULL;
            qa_q2_options selected = {0};
            if (!q2_selected_options(&launch->selection, launch->roles, product, choices, &selected, error))
                return false;
            profile_word(encoded, &size, selected.arsenal_rules);
            profile_word(encoded, &size, selected.native_hook);
            profile_word(encoded, &size, selected.hook_edition);
            profile_word(encoded, &size, selected.equipment_hook_rules);
            profile_word(encoded, &size, selected.equipment_hook_edition);
        }
        break;
    case QA_GAME_Q3:
        profile_word(encoded, &size, profile.mode_kind);
        profile_word(encoded, &size, profile.friendly_fire);
        break;
    }
    uint8_t *copy = malloc(size);
    if (!copy) return application_fail(error, QA_ERROR_MEMORY, "Retaining provider configuration bytes");
    memcpy(copy, encoded, size);
    *out = (qa_buffer){copy, size};
    return true;
}

qa_q1_program application_q1_program(const char *campaign)
{
    if (strcmp(campaign, "hipnotic") == 0)
        return QA_Q1_HIPNOTIC;
    if (strcmp(campaign, "rogue") == 0)
        return QA_Q1_ROGUE;
    if (strcmp(campaign, "dopa") == 0)
        return QA_Q1_DOPA;
    if (strcmp(campaign, "mg1") == 0)
        return QA_Q1_MG1;
    if (strcmp(campaign, "mg3") == 0)
        return QA_Q1_MG3;
    if (strcmp(campaign, "ctf") == 0)
        return QA_Q1_CTF;
    return QA_Q1_ID1;
}

static qa_actor_owner q1_combat_provider(void *opaque, qa_actor_id actor)
{
    application_provider *provider = opaque;
    application_provider *selected = application_provider_for(
        provider->application, actor, QA_ROLE_COMBAT, "");
    return selected == NULL ? provider->owner : selected->owner;
}

static bool q1_source_damage(void *opaque, qa_damage_request *request,
                              qa_error *error)
{
    application_provider *provider = opaque;
    if (!application_q3_weapons_services_q1_damage(opaque, request, error))
        return false;
    application_provider *movement = application_provider_for(
        provider->application, request->target, QA_ROLE_MOVEMENT, "");
    request->attack.movement_provider = movement == NULL ? 0 : movement->owner;
    return true;
}

static bool q1_force_retouch(void *opaque, uint32_t source_frames, qa_error *error)
{
    application_provider *provider = opaque;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1 ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->state.q1)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 retouch lost its native Source owner");
    return qa_q1_game_force_retouch(provider->state.q1, source_frames, error);
}

static bool q1_find_target(void *opaque, qa_string_id target,
                           qa_actor_id *out)
{
    application_provider *provider = opaque;
    return qa_targets_first(provider->application->targets, target, out);
}

static bool q1_find_targets(void *opaque, qa_string_id target,
                            qa_actor_id *actors, size_t capacity,
                            size_t *count, qa_error *error)
{
    application_provider *provider = opaque;
    if (count == NULL || (capacity != 0 && actors == NULL))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q1 target query needs output storage");
    qa_target_cursor cursor = {0};
    size_t written = 0;
    qa_actor_id actor;
    while (qa_targets_next(provider->application->targets, target, &cursor,
                           &actor)) {
        if (written == capacity)
            return application_fail(error, QA_ERROR_MEMORY,
                                    "Q1 target query output is too small");
        actors[written++] = actor;
    }
    *count = written;
    return true;
}

static qa_q1_options q1_initial_options(application_provider *provider,
    const qa_product *product, application_native_profile profile)
{
    return (qa_q1_options){
        .provider = provider->owner,
        .combat_provider = provider->owner,
        .movement_provider = provider->owner,
        .inventory_provider = provider->owner,
        .program = application_q1_program(product->campaign),
        .edition = strcmp(product->campaign, "quake64") == 0
                       ? QA_Q1_QUAKE64
                       : product->edition == QA_EDITION_RERELEASE
                             ? QA_Q1_RERELEASE
                             : QA_Q1_CLASSIC,
        .quakeworld = product->edition == QA_EDITION_QUAKEWORLD,
        .coop = profile.cooperative,
        .skill = (uint8_t)profile.skill,
        .deathmatch = profile.deathmatch ? 1 : 0,
        .teamplay = profile.teamplay,
        .gamecfg = profile.gamecfg,
        .gravity = 800.0f,
        .aim_threshold = product->edition == QA_EDITION_QUAKEWORLD ? 2.0f : 0.93f,
        .max_clients = profile.maximum_clients,
        .random_seed = (uint32_t)(provider->owner * UINT32_C(2654435761)),
    };
}

static bool q1_final_options(application_provider *provider, const qa_launch_choices *choices,
    qa_q1_options *options, qa_error *error)
{
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    if (!application_startup_apply_latched(provider, cvars, error)) return false;
    static const char *const names[] = {"skill", "deathmatch", "coop", "teamplay", "gamecfg",
        "sv_gravity", "sv_aim", "maxclients"};
    qa_cvar_view value[8];
    for (size_t i = 0; i < 8; ++i) {
        if (!qa_cvars_effective_view(cvars, names[i], value + i, error) ||
            !isfinite(value[i].number) || (value[i].owner && value[i].owner != provider->owner))
            return application_fail(error, QA_ERROR_FORMAT, "Q1 configuration lost a finite actual source value");
    }
    options->skill = (uint8_t)(int32_t)(fmaxf(0, fminf(3, value[0].number)) + 0.5);
    options->deathmatch = value[1].integer;
    options->coop = value[2].number != 0;
    options->teamplay = value[3].integer;
    options->gamecfg = (uint32_t)value[4].integer;
    options->gravity = value[5].number;
    options->aim_threshold = value[6].number;
    float capacity = truncf(value[7].number);
    if (!options->quakeworld && provider->application->operation != APPLICATION_PERSISTING &&
        !qa_cvars_set_number(cvars, "skill", (float)options->skill, error)) return false;
    if (options->quakeworld) {
        if (choices->seat_count > 32)
            return application_fail(error, QA_ERROR_FORMAT, "QuakeWorld has 32 physical client rows");
        options->max_clients = 32;
        return true;
    }
    float minimum = (float)(choices->seat_count ? choices->seat_count : 1);
    if (capacity < minimum) capacity = minimum;
    if (capacity < 1 || capacity > 64)
        return application_fail(error, QA_ERROR_FORMAT, "Q1 source client capacity must be between 1 and 64");
    options->max_clients = (uint32_t)capacity;
    return application_publication_source_capacity(provider, options->max_clients, error);
}

static bool native_console_preinit(application_provider *provider, qa_error *error)
{
    qa_console *console = NULL;
    qa_cvars *cvars = NULL;
    qa_command_context command;
    bool found = provider->kind == APPLICATION_PROVIDER_Q1
        ? application_native_q1_console_at(provider, &console, &cvars, &command)
        : provider->kind == APPLICATION_PROVIDER_Q2
            ? application_native_q2_console_at(provider, &console, &cvars, &command)
            : application_native_q3_console_at(provider, &console, &cvars, &command);
    return found ? application_startup_source_preinit(provider, console, cvars, &command, error)
        : application_fail(error, QA_ERROR_ARGUMENT, "Native source lost its actual preinitialization console");
}

static bool q1_selected_fired(void *context, qa_actor_id actor, qa_item_id weapon, qa_error *error)
{
    return application_q3_weapons_services_selected_fired(context, actor, weapon, error) &&
        application_native_q1_source_fired(context, actor, weapon, error);
}
static bool q1_selected_attack_delay(void *context, qa_actor_id actor, qa_q1_weapon weapon,
    float *seconds, qa_error *error)
{
    if (!application_q1_attack_delay(context, actor, weapon, seconds, error)) return false;
    float milliseconds = truncf(*seconds * 1000.0f);
    if (!isfinite(*seconds) || *seconds < 0 || !isfinite(milliseconds) ||
        (double)milliseconds > (double)(UINT64_MAX / UINT64_C(1000000)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected Q1 delay is outside its native interval");
    uint64_t delay; bool handled;
    if (!application_q3_weapons_services_selected_delay(context, actor,
        (uint64_t)milliseconds * UINT64_C(1000000), &delay, &handled, error)) return false;
    if (handled) *seconds = (float)((double)delay / 1e9);
    return true;
}
static bool q1_character_drop_inventory(void *context, qa_actor_id actor, qa_error *error)
{
    application_provider *character = context;
    qa_application *app = character ? character->application : NULL;
    uint32_t seat;
    if (!app || app->destroy_requested || app->finalizing || !app->session || !app->world ||
        !character->constructed || !character->attached || character->close_pending ||
        character->kind != APPLICATION_PROVIDER_Q1 || !character->state.q1 ||
        !qa_actors_get(qa_session_actors(app->session), actor) ||
        !qa_application_player_seat(app, actor, &seat) ||
        application_provider_for(app, actor, QA_ROLE_CHARACTER, "") != character)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 death inventory lost its actual character and player");
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!source || source->application != app || !source->constructed ||
        !source->attached || source->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 death inventory lost its published game source");
    if (source->kind != APPLICATION_PROVIDER_Q1) return true;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(source->state.q1, &operation, error)) return false;
    qa_item_id weapon;
    qa_actor_id dropped;
    bool okay = qa_application_weapon_read(app, actor, &weapon, error);
    if (okay && (!qa_q1_game_operation_live(&operation) ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != source ||
        application_provider_for(app, actor, QA_ROLE_CHARACTER, "") != character))
        okay = application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 death inventory replaced its actual source owners");
    if (okay) okay = qa_q1_drop_backpack(operation.game, actor, weapon, &dropped, error);
    if (okay && !qa_q1_game_operation_live(&operation))
        okay = application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 death inventory retired its actual game source");
    qa_q1_game_operation_end(&operation);
    return okay;
}
static bool construct_q1(qa_application *application,
                         application_provider *provider, qa_world *world,
                         const qa_product *product,
                         const qa_launch_choices *choices, qa_error *error)
{
    application_native_profile profile;
    if (!native_profile(provider->launch, choices, &profile, error)) return false;
    qa_q1_options options = q1_initial_options(provider, product, profile);
    bool console_created = provider->native_q1_console ||
        application_native_q1_console_create(provider, &options, error);
    if (!console_created || !native_console_preinit(provider, error)) return false;
    for (size_t i = 0; i < choices->mode_count; ++i)
        if (choices->modes[i].rules.enabled && choices->modes[i].rules.source == QA_MODE_Q1_HORDE &&
            !strcmp(choices->modes[i].instance, provider->launch->selection.instance)) {
            qa_cvars *cvars = application_native_q1_console_registry(provider);
            if (!qa_cvars_register(cvars, "horde", "0", 0, provider->owner, NULL, error) ||
                !qa_cvars_declare_save_policy(cvars, "horde", QA_CVAR_SAVE_GAMEPLAY, error) ||
                !qa_cvars_set(cvars, "horde", "1", true, error)) return false;
        }
    if ((application->operation == APPLICATION_PERSISTING &&
         !application_native_q1_checkpoint_prepare(provider, error)) ||
        !q1_final_options(provider, choices, &options, error)) return false;
    if (!application_native_q1_wire_create(provider, error)) return false;
    qa_targets_monsters_configure(application->targets, application, application_monster_mission);
    qa_q1_host host = {.context = provider,
                       .cvars = application_native_q1_console_registry(provider),
                       .source_console_print = application_native_q1_source_console_print,
                       .source_logfrag_write = application_native_q1_source_logfrag_write,
                       .world_info = application_native_q1_world_info,
                       .source_info_map_reset = application_native_q1_source_info_map_reset,
                       .client_attack = application_native_q1_client_attack,
                       .check_client = application_native_q1_check_client,
                       .find_target = q1_find_target,
                       .find_targets = q1_find_targets,
                       .combat_provider = q1_combat_provider,
                       .supply = application_supplies_source_for,
                       .drop_inventory = q1_character_drop_inventory,
                       .request_respawn = application_native_q1_request_respawn,
                       .console_suicide = application_native_q1_suicide,
                       .console_cheat = application_native_q1_console_cheat,
                       .powerup = application_native_q1_powerup,
                       .set_gravity = application_native_q1_set_gravity,
                       .console_power = application_native_q1_console_power,
                       .cheat_arsenal = application_supplies_cheat_arsenal,
                       .weapon_changed = application_native_q1_weapon_changed,
                       .fired = q1_selected_fired,
                       .base_team_health = application_native_q1_base_team_health,
                       .sound_precache = application_unified_q1_sound_precache,
                       .precache_reset = application_unified_q1_precache_reset,
                       .grapple_weapon_frame = application_q3_weapons_services_grapple_frame,
                       .source_damage = q1_source_damage,
                       .force_retouch = q1_force_retouch,
                       .weapon_parameters = application_q1_weapon_parameters,
                       .weapon_observation = application_q1_weapon_observation,
                       .before_fire = application_q1_before_fire,
                       .attack_delay = q1_selected_attack_delay,
                       .nail_fire = application_q1_nail_fire,
                       .missions = {.context = application->targets, .lookup = qa_targets_monster_lookup},
                       .monster_admit = application_monster_admit,
                       .monster_path = application_bots_npc_walk,
                       .horde = application_bots_npc_horde,
                       .monster_path_clone = application_bots_npc_clone,
                       .monster_path_release = application_bots_npc_released};
    qa_builtin_services services = application_builtin_services(
        application, world, application->physics);
    if (!qa_q1_game_create(&services, &options, &host,
                           &provider->state.q1, error) ||
        !qa_q1_game_retain(provider->state.q1, &provider->q1_lifetime, error) ||
        !application_native_q1_ctf_flags_configure(provider, error) ||
        !application_native_q1_rogue_world_configure(provider, error) ||
        !qa_q1_source_clients_configure(provider->state.q1,
            &(qa_q1_source_client_services){.context = provider,
                .publish = application_native_q1_wire_client_publish,
                .observer = application_native_q1_wire_client_observer}, error) ||
        !qa_q1_game_component(provider->state.q1, &provider->component,
                              error) ||
        !qa_q1_game_combat_policy(provider->state.q1, &provider->policy,
                                  error))
        return false;
    uint64_t initial_time_ns = provider->component.clock.initial_time_ns;
    (void)application_q1_original_clock(provider, &initial_time_ns);
    provider->component.clock = provider->launch->selection.clock;
    provider->component.clock.initial_time_ns = initial_time_ns;
    return true;
}

static qa_q2_product q2_product(const char *campaign)
{
    if (strcmp(campaign, "xatrix") == 0)
        return QA_Q2_XATRIX;
    if (strcmp(campaign, "rogue") == 0)
        return QA_Q2_ROGUE;
    if (strcmp(campaign, "n64") == 0)
        return QA_Q2_N64;
    return QA_Q2_BASE;
}

static bool q2_selected_options(const qa_launch_provider *selection, uint64_t roles, const qa_product *product,
    const qa_launch_choices *choices, qa_q2_options *options, qa_error *error)
{
    if (!selection || !options || !product || product->family != QA_GAME_Q2 || !choices)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 arsenal registration needs its actual native source");
    qa_q2_weapon_rules selected = QA_Q2_WEAPON_RULES_BASE;
    if (roles & QA_ROLE_BIT(QA_ROLE_ARSENAL))
        for (size_t i = 0; i < choices->mode_count; ++i) {
            const qa_launch_mode *mode = choices->modes + i;
            if (!mode->rules.enabled || strcmp(mode->instance, selection->instance)) continue;
            qa_q2_weapon_rules rules = mode->rules.source == QA_MODE_Q2_CTF ? QA_Q2_WEAPON_RULES_CTF
                : mode->rules.source == QA_MODE_LMCTF ? QA_Q2_WEAPON_RULES_LMCTF : QA_Q2_WEAPON_RULES_BASE;
            if (rules == QA_Q2_WEAPON_RULES_BASE) continue;
            if (selected != QA_Q2_WEAPON_RULES_BASE && selected != rules)
                return application_fail(error, QA_ERROR_UNSUPPORTED, "One Q2 arsenal selects conflicting native mode modules");
            selected = rules;
        }
    bool native_hook = false;
    qa_q2_edition hook_edition = QA_Q2_CLASSIC;
    qa_q2_weapon_rules equipment_rules = QA_Q2_WEAPON_RULES_BASE;
    qa_q2_edition equipment_edition = QA_Q2_CLASSIC;
    for (size_t i = 0; i < choices->equipment_count; ++i) {
        const qa_launch_equipment *equipment = choices->equipment + i;
        qa_q2_weapon_rules rules = equipment->selection.grapple == QA_GRAPPLE_Q2_CTF
            ? QA_Q2_WEAPON_RULES_CTF : equipment->selection.grapple == QA_GRAPPLE_LMCTF
                ? QA_Q2_WEAPON_RULES_LMCTF : QA_Q2_WEAPON_RULES_BASE;
        if (equipment->selection.binding != QA_EQUIPMENT_WEAPON_SLOT ||
            rules == QA_Q2_WEAPON_RULES_BASE ||
            strcmp(equipment->grapple_source, selection->instance)) continue;
        qa_q2_edition edition = rules == QA_Q2_WEAPON_RULES_CTF &&
            product->edition == QA_EDITION_RERELEASE ? QA_Q2_RERELEASE : QA_Q2_CLASSIC;
        if (equipment_rules != QA_Q2_WEAPON_RULES_BASE &&
            (equipment_rules != rules || equipment_edition != edition))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "One Q2 source selects conflicting hook equipment definitions");
        equipment_rules = rules;
        equipment_edition = edition;
        if (selected != rules) continue;
        const qa_launch_binding *arsenal = qa_launch_binding_for(choices, equipment->scope, QA_ROLE_ARSENAL, "");
        if (!arsenal || strcmp(arsenal->instance, selection->instance)) continue;
        native_hook = true;
        hook_edition = edition;
    }
    options->arsenal_rules = selected;
    options->native_hook = native_hook;
    options->hook_edition = hook_edition;
    options->equipment_hook_rules = equipment_rules;
    options->equipment_hook_edition = equipment_edition;
    return true;
}

static bool q2_arsenal_options(application_provider *provider,
    const qa_launch_choices *choices, qa_q2_options *options, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q2)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 arsenal registration needs its actual native source");
    qa_application *app = provider->application;
    const qa_launch_snapshot *snapshot = app->routing_snapshot ? app->routing_snapshot : qa_application_launch(app);
    const qa_launch_instance *instance = qa_launch_snapshot_find(snapshot, provider->launch->selection.instance);
    if (!instance || instance->state != provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 arsenal registration lost its actual selected source");
    return q2_selected_options(&instance->selection, instance->roles, provider->product, choices, options, error);
}

static bool arsenal_profile(const qa_launch_provider *selection, uint64_t roles,
    const qa_product *product, const qa_launch_choices *choices,
    application_arsenal_profile *out, qa_error *error)
{
    *out = (application_arsenal_profile){.family = product->family};
    switch (product->family) {
    case QA_GAME_Q1:
        out->source.q1 = application_q1_program(product->campaign);
        return true;
    case QA_GAME_Q2:
        out->source.q2 = (qa_q2_options){
            .edition = product->edition == QA_EDITION_RERELEASE ? QA_Q2_RERELEASE : QA_Q2_CLASSIC,
            .product = q2_product(product->campaign)};
        return q2_selected_options(selection, roles, product, choices, &out->source.q2, error);
    case QA_GAME_Q3:
        out->source.q3 = !strcmp(product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
        return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Selected arsenal has no product family");
}

static bool arsenal_scope(const qa_launch_choices *choices, qa_launch_scope scope, qa_error *error)
{
    if (!choices ||
        (scope.kind != QA_SCOPE_DEFAULT_PLAYER && scope.kind != QA_SCOPE_SEAT))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected arsenal requires its default or authored seat scope");
    if (scope.kind == QA_SCOPE_SEAT) {
        bool authored = false;
        for (size_t i = 0; i < choices->seat_count; ++i)
            if (choices->seats[i].id == scope.seat) { authored = true; break; }
        if (!authored)
            return application_fail(error, QA_ERROR_ARGUMENT, "Selected arsenal has no authored seat");
    }
    return true;
}

bool application_draft_arsenal_profile(const qa_launch_draft *draft, qa_launch_scope scope,
    application_arsenal_profile *out, bool *found, qa_error *error)
{
    if (!draft || !out || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Draft arsenal needs its selection and output");
    *out = (application_arsenal_profile){0}; *found = false;
    const qa_launch_choices *choices = qa_launch_draft_choices(draft);
    if (!arsenal_scope(choices, scope, error)) return false;
    const qa_launch_binding *binding = qa_launch_binding_for(choices, scope, QA_ROLE_ARSENAL, "");
    if (!binding) return true;
    const qa_launch_provider *selected = NULL;
    for (size_t i = 0; i < choices->provider_count; ++i)
        if (!strcmp(choices->providers[i].instance, binding->instance)) {
            selected = choices->providers + i; break;
        }
    const qa_product *product = selected ?
        qa_catalog_product(qa_launch_draft_catalog(draft), selected->product) : NULL;
    if (!product)
        return application_fail(error, QA_ERROR_ARGUMENT, "Draft arsenal lost its selected product");
    if (!arsenal_profile(selected, QA_ROLE_BIT(QA_ROLE_ARSENAL), product, choices, out, error)) return false;
    *found = true;
    return true;
}

bool application_startup_arsenal_profile(qa_application *app, const qa_launch_snapshot *snapshot,
    qa_launch_scope scope, application_arsenal_profile *out, qa_actor_owner *owner,
    bool *found, qa_error *error)
{
    if (!app || !snapshot || !out || !owner || !found ||
        (snapshot != qa_configuration_current(app->configuration) && snapshot != app->routing_snapshot &&
         snapshot != qa_application_startup_candidate(app)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Boot arsenal requires its retained launch snapshot");
    *out = (application_arsenal_profile){0}; *owner = 0; *found = false;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    if (!arsenal_scope(choices, scope, error)) return false;
    const qa_launch_binding *binding = qa_launch_binding_for(choices, scope, QA_ROLE_ARSENAL, "");
    const qa_launch_instance *selected = binding ? qa_launch_snapshot_find(snapshot, binding->instance) : NULL;
    application_provider *provider = selected ? selected->state : NULL;
    if (!provider) return true;
    qa_game_family family;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1: family = QA_GAME_Q1; break;
    case APPLICATION_PROVIDER_Q2: family = QA_GAME_Q2; break;
    case APPLICATION_PROVIDER_Q3: family = QA_GAME_Q3; break;
    default: return true;
    }
    if (provider->application != app || provider->owner == 0 || !provider->launch ||
        provider->launch->storage != selected->storage)
        return application_fail(error, QA_ERROR_ARGUMENT, "Boot arsenal lost its actual native provider descriptor");
    qa_catalog *catalog = qa_launch_instance_catalog(selected);
    if (!catalog) catalog = qa_launch_snapshot_catalog(snapshot);
    const qa_product *product = qa_catalog_product(catalog, selected->selection.product);
    if (!product || product->family != family)
        return application_fail(error, QA_ERROR_ARGUMENT, "Boot arsenal lost its physical native product");
    if (!arsenal_profile(&selected->selection, selected->roles, product, choices, out, error)) return false;
    *owner = provider->owner;
    if (family == QA_GAME_Q2) out->source.q2.owner = *owner;
    *found = true;
    return true;
}

bool application_native_q2_arsenal_prepare(application_provider *provider,
    const qa_launch_choices *choices, qa_error *error)
{
    qa_q2_options options = {0};
    return q2_arsenal_options(provider, choices, &options, error) &&
        qa_q2_bot_arsenal_register_multiplayer(provider->state.q2, options.arsenal_rules,
            options.native_hook, options.hook_edition, error) &&
        qa_q2_bot_equipment_register_hook(provider->state.q2, options.equipment_hook_rules,
            options.equipment_hook_edition, error);
}

static bool q2_equipment_animation(void *context, qa_actor_id actor, bool reverse,
    bool *handled, qa_error *error)
{
    return application_q3_weapons_services_equipment_animation(context, actor, reverse, false,
        handled, error);
}
static bool construct_q2(qa_application *application,
                         application_provider *provider, qa_world *world,
                         const qa_product *product,
                         const qa_launch_choices *choices, qa_error *error)
{
    application_native_profile profile;
    if (!native_profile(provider->launch, choices, &profile, error))
        return false;
    qa_q2_options options = {
        .owner = provider->owner,
        .edition = product->edition == QA_EDITION_RERELEASE
                       ? QA_Q2_RERELEASE
                       : QA_Q2_CLASSIC,
        .product = q2_product(product->campaign),
        .deathmatch = profile.deathmatch,
        .cooperative = profile.cooperative,
        .skill = profile.skill,
        .seed = (uint64_t)provider->owner * UINT64_C(11400714819323198485),
        .frame_ns = provider->launch->selection.clock.interval_ns,
    };
    qa_builtin_services services = application_builtin_services(
        application, world, application->physics);
    bool console_ready = provider->native_q2_console ||
        application_native_q2_console_prepare(provider, &options, choices, error);
    if (!console_ready || !native_console_preinit(provider, error) ||
        (application->operation != APPLICATION_PERSISTING &&
        !application_native_q2_console_finalize(provider, &options, choices, error))) return false;
    services.cvar_context = provider;
    services.cvar = application_native_q2_cvar;
    if (!q2_arsenal_options(provider, choices, &options, error)) return false;
    qa_targets_monsters_configure(application->targets, application, application_monster_mission);
    qa_q2_hooks hooks = {.context = provider,
        .equipment_animation = q2_equipment_animation,
        .selected_firing_interval = application_q3_weapons_services_selected_delay,
        .source_damage_factor = application_q3_weapons_services_q2_damage,
        .source_weapon_powerups = application_q3_weapons_services_q2_powerups,
        .selected_weapon_input = application_q2_weapon_input,
        .prepare_damage = application_native_q2_damage_prepare,
        .inventory_provider = application_native_q2_attack_inventory,
        .rotation_changed = application_native_q2_rotation_changed,
        .fired = application_native_q1_source_fired};
    if (!qa_q2_create(&services, &options, &hooks, &provider->state.q2,
                      error))
        return false;
    int32_t clients, capacity;
    if (!application_native_q2_source_integer(provider, "maxclients", &clients, error) ||
        !application_native_q2_source_integer(provider, "maxentities", &capacity, error)) return false;
    if (clients < 1 || clients > 256 || capacity <= clients || capacity > 65536 ||
        choices->seat_count > (uint32_t)clients)
        return application_fail(error, QA_ERROR_FORMAT,
                                "Q2 source edict policy cannot reserve its real client rows");
    if (!qa_q2_wire_configure(provider->state.q2, (uint32_t)capacity, (uint32_t)clients, error))
        return false;
    qa_q2_item_options items = {.context = provider, .supply_for = application_supplies_source_for,
        .visibility = application_unified_q2_native_item_visibility,
        .instanced_coop = options.edition == QA_Q2_RERELEASE, .weapon_respawn_seconds = 30};
    if (!qa_q2_monsters_bind_missions(provider->state.q2,
        &(qa_monster_missions){.context = application->targets,
            .lookup = qa_targets_monster_lookup}, error) ||
        !qa_q2_items_configure(provider->state.q2, &items, error)) return false;
    if (!application_native_q2_arsenal_prepare(provider, choices, error)) return false;
    if (application->operation != APPLICATION_PERSISTING &&
        !application_native_q2_console_refresh(provider, error)) return false;
    if (!application_native_q2_combat_policy(provider, &provider->policy, error)) return false;
    provider->component = qa_q2_component(provider->state.q2);
    provider->component.clock = provider->launch->selection.clock;
    return true;
}

static int32_t q3_game_type(qa_mode_kind kind)
{
    switch (kind) {
    case QA_MODE_DUEL:
        return 1;
    case QA_MODE_SINGLE_PLAYER:
        return 2;
    case QA_MODE_TEAM_DEATHMATCH:
        return 3;
    case QA_MODE_CTF:
        return 4;
    case QA_MODE_ONE_FLAG:
        return 5;
    case QA_MODE_OVERLOAD:
        return 6;
    case QA_MODE_HARVESTER:
        return 7;
    default:
        return 0;
    }
}

static bool seed_number(qa_cvars *cvars, const char *name, float number,
    qa_error *error)
{
    if (qa_cvars_is_set(cvars, name)) return true;
    qa_cvar_view effective;
    return qa_cvars_effective_view(cvars, name, &effective, error) &&
        (effective.number == number || qa_cvars_set_number(cvars, name, number, error));
}

bool application_provider_seed_cvars(application_provider *provider, qa_cvars *cvars,
    const qa_launch_choices *choices, qa_error *error)
{
    application_native_profile profile;
    if (!native_profile(provider->launch, choices, &profile, error)) return false;
    qa_mode_rules mode = {0};
    bool found = selected_mode(choices, &mode);
    bool mode_unset = !qa_cvars_is_set(cvars, "g_gametype");
    if (profile.family != QA_GAME_Q3) {
        if (!seed_number(cvars, "skill", (float)profile.skill, error) ||
            !seed_number(cvars, "gamecfg", (float)profile.gamecfg, error)) return false;
        if (mode_unset && profile.family == QA_GAME_Q1 &&
            !seed_number(cvars, "teamplay", (float)profile.teamplay, error)) return false;
    }
    if (mode_unset) {
        int32_t type = profile.family == QA_GAME_Q3 ? q3_game_type(profile.mode_kind) :
            profile.cooperative ? 9 : profile.deathmatch ? q3_game_type(profile.mode_kind) : 8;
        qa_cvar_view effective;
        if (!qa_cvars_effective_view(cvars, "g_gametype", &effective, error) ||
            (effective.integer != type && !qa_cvars_set_number(cvars, "g_gametype", (float)type, error))) return false;
    }
    if (profile.family != QA_GAME_Q3) return true;
    return seed_number(cvars, "g_friendlyFire", profile.friendly_fire ? 1 : 0, error) &&
        seed_number(cvars, "fraglimit", (float)(found ? mode.frag_limit : 20), error) &&
        seed_number(cvars, "timelimit", found ? mode.time_limit_minutes : 0, error) &&
        seed_number(cvars, "capturelimit", (float)(found ? mode.capture_limit : 8), error) &&
        seed_number(cvars, "g_warmup", (float)(found && mode.warmup_seconds ? mode.warmup_seconds : 20), error) &&
        seed_number(cvars, "g_doWarmup", found && mode.warmup_seconds != 0 ? 1 : 0, error);
}

static bool q3_console_prepare(application_provider *provider,
    const qa_launch_choices *choices, qa_error *error)
{
    if (!application_native_q3_console_create(provider, choices->world.map, error)) return false;
    qa_q3_product product = !strcmp(provider->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    return application_native_q3_settings_prepare_definitions(provider, product, error);
}

static bool provider_product_bind(application_provider *provider, qa_catalog *catalog,
    const qa_product *product, qa_error *error)
{
    if (!qa_application_content_names_bind(provider->application,catalog,error)) return false;
    provider->content_name=qa_application_content_name(provider->application,catalog,product->id);
    qa_catalog_retain(catalog);
    qa_catalog_release(provider->product_catalog);
    provider->product_catalog = catalog;
    provider->product = product;
    return true;
}

static bool provider_console_prepare(qa_application *application,
    application_provider *provider, qa_world *world, qa_catalog *catalog,
    const qa_product *product, const qa_launch_choices *choices, qa_console **console,
    qa_cvars **cvars, qa_command_context *command, qa_error *error)
{
    if (!application || !provider || !world || !catalog || !product || !choices ||
        !console || !cvars || !command || provider->constructed || provider->attached ||
        qa_catalog_product(catalog, product->id) != product)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source preparation needs its actual detached provider and world");
    *console = NULL; *cvars = NULL;
    if (provider->native_q1_console)
        return application_native_q1_console_at(provider, console, cvars, command);
    if (provider->native_q2_console)
        return application_native_q2_console_at(provider, console, cvars, command);
    if (provider->native_q3_console)
        return application_native_q3_remote_roles_prepare(provider, choices, error) &&
            application_native_q3_console_at(provider, console, cvars, command);
    if (!provider_product_bind(provider, catalog, product, error)) return false;
    if (provider->client_only_owned || application_native_client_only(provider))
        return true;
    application_native_profile profile;
    if (!native_profile(provider->launch, choices, &profile, error)) return false;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1: {
        qa_q1_options options = q1_initial_options(provider, product, profile);
        return application_native_q1_console_create(provider, &options, error) &&
            application_native_q1_console_at(provider, console, cvars, command);
    }
    case APPLICATION_PROVIDER_Q2: {
        qa_q2_options options = {.owner = provider->owner,
            .edition = product->edition == QA_EDITION_RERELEASE ? QA_Q2_RERELEASE : QA_Q2_CLASSIC,
            .product = q2_product(product->campaign), .skill = profile.skill,
            .deathmatch = profile.deathmatch, .cooperative = profile.cooperative};
        return application_native_q2_console_prepare(provider, &options, choices, error) &&
            application_native_q2_console_at(provider, console, cvars, command);
    }
    case APPLICATION_PROVIDER_Q3: {
        bool client_only = application_native_q3_remote_client_only(provider);
        if ((!client_only && !q3_console_prepare(provider, choices, error)) ||
            !application_native_q3_remote_roles_prepare(provider, choices, error)) return false;
        if (!client_only) return application_native_q3_console_at(provider, console, cvars, command);
        qa_application_startup_source source;
        bool found;
        if (!application_native_q3_remote_role_source_at(provider, 0, &source, &found, error)) return false;
        if (found) { *console = source.console; *cvars = source.cvars; *command = source.command; }
        return true;
    }
    case APPLICATION_PROVIDER_QC:
        return application_qc_console_prepare(application, provider, world, product,
            choices, console, cvars, command, error);
    case APPLICATION_PROVIDER_QVM:
    case APPLICATION_PROVIDER_NATIVE:
        return product->family == QA_GAME_Q3 ?
            application_guest_q3_console_prepare(application, provider, world, product,
                choices, console, cvars, command, error) :
            product->family == QA_GAME_Q2 ?
                application_guest_native_q2_console_prepare(application, provider, world, product,
                    choices, console, cvars, command, error) :
                application_fail(error, QA_ERROR_UNSUPPORTED,
                    "Selected original GAME requires its retained private console preparation");
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Unknown startup source kind");
}

static bool provider_preparation_enter(qa_application *application,
    application_provider *provider, const qa_launch_choices *choices,
    application_provider **previous, qa_cvars_edit **values, qa_error *error)
{
    const qa_launch_snapshot *snapshot = application ? application->routing_snapshot : NULL;
    const qa_launch_instance *selected = provider && provider->launch && snapshot
        ? qa_launch_snapshot_find(snapshot, provider->launch->selection.instance) : NULL;
    if (!application || !provider || provider->application != application ||
        !selected || selected->state != provider || selected->storage != provider->launch->storage ||
        qa_launch_snapshot_choices(snapshot) != choices ||
        (application->startup_preinit_provider && application->startup_preinit_provider != provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source constructor lost its actual candidate descriptor");
    if (!application_startup_values_enter(application, values, error)) return false;
    *previous = application->startup_preinit_provider;
    application->startup_preinit_provider = provider;
    return true;
}

static bool provider_preparation_leave(qa_application *application,
    application_provider *previous, qa_cvars_edit *values, qa_error *error)
{
    application->startup_preinit_provider = previous;
    return application_startup_values_leave(values, error);
}

bool application_provider_console_prepare(qa_application *application,
    application_provider *provider, qa_world *world, qa_catalog *catalog,
    const qa_product *product, const qa_launch_choices *choices, qa_console **console,
    qa_cvars **cvars, qa_command_context *command, qa_error *error)
{
    qa_cvars_edit *values = NULL;
    application_provider *previous = NULL;
    if (!provider_preparation_enter(application, provider, choices, &previous, &values, error)) return false;
    bool ok = provider_console_prepare(application, provider, world, catalog,
        product, choices, console, cvars, command, error);
    qa_error returned = {0};
    bool left = provider_preparation_leave(application, previous, values, &returned);
    if (!left && ok && error) *error = returned;
    return ok && left;
}

bool application_provider_startup_source_at(application_provider *provider, size_t index,
    qa_application_startup_source *out, bool *found, qa_error *error)
{
    if (!provider || !provider->launch || !out || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup enumeration requires its physical source owner");
    *found = false;
    if (provider->client_only_owned || application_native_client_only(provider))
        return application_native_client_role_source_at(provider, index, out, found, error);
    if ((provider->kind == APPLICATION_PROVIDER_QVM || provider->kind == APPLICATION_PROVIDER_NATIVE) &&
        provider->product && provider->product->family == QA_GAME_Q3) {
        if (provider->kind == APPLICATION_PROVIDER_QVM ? !provider->state.qvm.engine : !provider->state.native.engine)
            return true;
        return application_guest_q3_startup_source_at(provider, index, out, found, error);
    }
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        qa_application_startup_source game = {.descriptor = provider->launch,
            .scope = {.provider = provider->owner, .kind = QA_APPLICATION_CONSOLE_Q3_GAME},
            .declaration_owner = provider->owner};
        bool has_game = application_native_q3_console_at(provider, &game.console, &game.cvars, &game.command);
        if (has_game && !index) { *out = game; *found = true; return true; }
        return application_native_q3_remote_role_source_at(provider, index - (has_game ? 1 : 0), out, found, error);
    }
    if (index) return true;
    qa_application_startup_source source = {.descriptor = provider->launch,
        .scope = {.provider = provider->owner}, .declaration_owner = provider->owner};
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1:
        source.scope.kind = QA_APPLICATION_CONSOLE_Q1_GAME;
        *found = application_native_q1_console_at(provider, &source.console, &source.cvars, &source.command);
        break;
    case APPLICATION_PROVIDER_Q2:
        source.scope.kind = QA_APPLICATION_CONSOLE_Q2_GAME;
        *found = application_native_q2_console_at(provider, &source.console, &source.cvars, &source.command);
        break;
    case APPLICATION_PROVIDER_Q3: break;
    case APPLICATION_PROVIDER_QC:
        source.scope.kind = QA_APPLICATION_CONSOLE_QC;
        if (provider->state.qc.engine) {
            struct application_qc_state *engine = provider->state.qc.engine;
            source.console = engine->console; source.cvars = engine->cvars; source.command = engine->command_context;
            *found = source.console != NULL;
        }
        break;
    case APPLICATION_PROVIDER_NATIVE:
        source.scope.kind = QA_APPLICATION_CONSOLE_NATIVE_Q2;
        if (provider->state.native.q2_engine) {
            struct application_native_q2 *engine = provider->state.native.q2_engine;
            source.console = engine->console; source.cvars = engine->cvars; source.command = engine->command_context;
            *found = source.console != NULL;
        }
        break;
    case APPLICATION_PROVIDER_QVM: break;
    }
    if (*found) *out = source;
    return true;
}

static qa_actor_owner q3_combat_provider(void *opaque, qa_actor_id target,
                                         qa_actor_owner fallback)
{
    application_provider *provider = opaque;
    application_provider *selected = application_provider_for(
        provider->application, target, QA_ROLE_COMBAT, "");
    return selected == NULL ? fallback : selected->owner;
}

static bool q3_primary_attack_allowed(void *opaque, qa_actor_id actor)
{
    application_provider *provider = opaque;
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor) ||
        application_provider_for(provider->application, actor, QA_ROLE_ARSENAL, "") != provider)
        return false;
    return !provider->application->equipment ||
        qa_equipment_primary_selected(provider->application->equipment, actor);
}

static int32_t q3_source_team(void *opaque, qa_actor_id actor)
{
    application_provider *provider = opaque;
    qa_q3_client_session session;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor) ||
        !qa_q3_client_session_read(provider->state.q3, actor, &session, NULL))
        return 0;
    return session.team;
}

static bool q3_memory_debug_integer(void *opaque, int32_t *out, qa_error *error)
{
    return application_native_q3_settings_integer_at(opaque, APPLICATION_Q3_SETTING_G_DEBUG_ALLOC, out, error);
}

static qa_trajectory q3_mover_trajectory(const qa_q3_trajectory *source)
{
    return (qa_trajectory){.type = (qa_trajectory_type)source->type,
        .time_ms = source->time, .duration_ms = source->duration,
        .base = qa_v3(source->base[0], source->base[1], source->base[2]),
        .delta = qa_v3(source->delta[0], source->delta[1], source->delta[2])};
}

static bool q3_native_client_mover_read(void *opaque, qa_actor_id actor,
    qa_q3_mover_state *out)
{
    application_provider *provider = opaque;
    uint32_t slot;
    qa_q3_source_binding binding;
    qa_q3_entity entity;
    qa_q3_wire_visibility visibility;
    qa_q3_source_client_motion motion;
    bool physics_object;
    if (!provider || !out || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->state.q3 || !provider->constructed || !provider->attached ||
        provider->close_pending ||
        !qa_q3_source_actor_slot(provider->state.q3, actor, &slot, NULL) ||
        !qa_q3_source_binding_read(provider->state.q3, slot, &binding, NULL) ||
        binding.client_slot < 0 || !binding.body_attached ||
        !qa_actor_id_equal(binding.actor, actor) ||
        !qa_q3_wire_entity_read(provider->state.q3, slot, &entity, &visibility, NULL) ||
        !qa_q3_wire_borrowed_client_motion_read(provider->state.q3, actor, &motion, NULL) ||
        !qa_q3_source_physics_object_read(provider->state.q3, slot, &physics_object, NULL))
        return false;
    *out = (qa_q3_mover_state){.kind = entity.eType == 1 || entity.eType == 2 || physics_object
            ? QA_Q3_MOVER_NATIVE_PLAYER : QA_Q3_MOVER_NATIVE_FIXED,
        .position = q3_mover_trajectory(&entity.pos),
        .angular = q3_mover_trajectory(&entity.apos),
        .has_client = true, .client_origin = motion.origin,
        .delta_yaw_word = motion.delta_yaw_word,
        .ground_entity_number = entity.groundEntityNum};
    return true;
}

static bool q3_native_client_mover_write(void *opaque, qa_actor_id actor,
    const qa_q3_mover_state *state, qa_error *error)
{
    application_provider *provider = opaque;
    qa_q3_mover_state current;
    if (!state || !q3_native_client_mover_read(provider, actor, &current) ||
        state->kind != current.kind || !state->has_client)
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "Q3 mover lost its actual source client pointer");
    if (!qa_q3_wire_entity_motion_write(provider->state.q3, actor,
        &state->position, &state->angular, state->ground_entity_number, error)) return false;
    if (!state->write_client_motion) return true;
    qa_q3_source_client_motion motion = {.origin = state->client_origin,
        .delta_yaw_word = state->delta_yaw_word};
    return qa_q3_wire_borrowed_client_motion_write(provider->state.q3, actor, &motion, error);
}

static bool q3_selected_objective_drop(void *context, qa_actor_id actor, qa_error *error)
{
    bool handled;
    if (!application_q3_weapons_services_selected_drop(context, actor, &handled, error)) return false;
    return handled || application_native_q3_objective_drop(context, actor, error);
}
static bool q3_selected_teleport_destination(void *context, qa_actor_id actor,
    qa_vec3 *origin, qa_vec3 *angles, qa_error *error)
{
    bool handled;
    if (!application_q3_weapons_services_selected_spawn(context, actor, origin, angles, &handled, error))
        return false;
    return handled || application_q3_native_deathmatch_destination(context, actor, origin, angles, error);
}
static bool q3_selected_client_effects(void *context, qa_actor_id actor,
    const qa_q3_selected_client_effects *before, const qa_q3_selected_client_effects *after,
    qa_error *error)
{
    bool handled;
    return application_q3_weapons_services_selected_client_effects(context, actor, before, after,
        &handled, error);
}
static bool q3_selected_fired(void *context, qa_actor_id actor, qa_q3_weapon weapon, qa_error *error)
{
    application_provider *provider = context;
    if (!provider || !provider->state.q3)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Selected Q3 shot lost its native weapon owner");
    return application_q3_weapons_services_selected_fired(provider, actor,
        qa_q3_weapon_item(provider->state.q3, weapon, false), error);
}
static bool construct_q3(qa_application *application,
                         application_provider *provider, qa_world *world,
                         const qa_product *product,
                         const qa_launch_choices *choices, qa_error *error)
{
    if (!application_native_q3_remote_roles_prepare(provider, choices, error) ||
        !application_native_q3_remote_roles_preinit(provider, error)) return false;
    if (application_native_q3_remote_client_only(provider)) return true;
    application_native_profile profile;
    if (!native_profile(provider->launch, choices, &profile, error))
        return false;
    qa_q3_rules rules = qa_q3_default_rules();
    rules.game_type = q3_game_type(profile.mode_kind);
    rules.friendly_fire = profile.friendly_fire;
    if (!provider->native_q3_console &&
        !(application->operation == APPLICATION_PERSISTING
            ? (application_native_q3_console_create(provider, choices->world.map, error) &&
               application_native_q3_settings_prepare_definitions(provider,
                   !strcmp(provider->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA, error))
            : q3_console_prepare(provider, choices, error)))
        return false;
    if (!native_console_preinit(provider, error)) return false;
    qa_cvars *cvars = application_native_q3_console_registry(provider);
    application_q3_world_startup replacement;
    bool replacing = application_q3_world_restart_source(application, provider,
                                                          &replacement);
    if (!application_q3_world_restart_cvars(application, provider, cvars, error) ||
        !application_q3_campaign_launch_cvars(provider, cvars, provider->owner, error))
        return false;
    if (replacing)
        rules.game_type = replacement.game_type;
    if (application->operation != APPLICATION_PERSISTING) {
        if (!application_startup_apply_latched(provider, cvars, error)) return false;
        const qa_cvar_view *game_type = qa_cvars_find(cvars, "g_gametype");
        const qa_cvar_view *friendly = qa_cvars_find(cvars, "g_friendlyFire");
        if (game_type && !replacing) rules.game_type = game_type->integer;
        if (friendly) rules.friendly_fire = friendly->number != 0;
    }
    qa_cvar_view capacity;
    if (!qa_cvars_effective_view(cvars, "sv_maxclients", &capacity, error) ||
        !capacity.declared || capacity.owner != provider->owner || choices->seat_count > 64)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 startup has no actual scoped client capacity");
    float requested = truncf(capacity.number);
    const qa_cvar_view *dedicated = qa_cvars_find(cvars, "dedicated");
    if (!dedicated || dedicated->owner != provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 startup has no actual scoped dedicated setting");
    float minimum = dedicated->number != 0 ? 1.0f
        : (float)(choices->seat_count ? choices->seat_count : 1);
    if (requested < minimum)
        requested = minimum;
    if (requested > 64) requested = 64;
    if (!isfinite(requested) || requested < 1 || requested > 64)
        return application_fail(error, QA_ERROR_FORMAT,
                                "Q3 source client capacity must be between 1 and 64");
    uint32_t max_clients = (uint32_t)requested;
    if (!application_publication_source_capacity(provider, max_clients, error)) return false;
    qa_q3_options options = {
        .services = application_builtin_services(application, world,
                                                 application->physics),
        .owner = provider->owner,
        .product = strcmp(product->campaign, "missionpack") == 0
                       ? QA_Q3_TEAM_ARENA
                       : QA_Q3_ARENA,
        .rules = rules,
        .max_clients = max_clients,
        .hooks = {.context = provider,
                  .source_participant_event = application_unified_q3_participant,
                  .attack_providers = application_unified_q3_attack_providers,
                  .source_settings_update = application_native_q3_settings_source_frame,
                  .source_world_init = application_native_q3_source_world_initialize,
                  .source_team_items = application_native_q3_source_team_items,
                  .source_team = q3_source_team,
                  .source_hurt_carrier = application_native_q3_team_check_hurt_carrier,
                  .source_frag_bonuses = application_native_q3_team_frag_bonuses,
                  .source_death_score = application_native_q3_source_death_score,
                  .source_client_death = application_native_q3_client_death_items,
                  .source_client_run = application_native_q3_source_client_run,
                  .source_client_end = application_native_q3_source_client_end,
                  .source_supply_take = application_supplies_q3_take,
                  .source_ammo_regeneration = application_supplies_q3_ammo_regeneration,
                  .source_ammo_timer_stored = application_supplies_q3_ammo_stored,
                  .primary_attack_allowed = q3_primary_attack_allowed,
                  .selected_damage_factor = application_q3_weapons_services_selected_damage,
                  .selected_client_effects = q3_selected_client_effects,
                  .selected_weapon_fired = q3_selected_fired,
                  .inventory_weapon_request = application_equipment_q3_weapon_request,
                  .postgame_cvar_integer = application_native_q3_postgame_cvar_integer,
                  .memory_debug_integer = q3_memory_debug_integer,
                  .foreign_mover_read = q3_native_client_mover_read,
                  .foreign_mover_write = q3_native_client_mover_write,
                  .source_end_frame = application_native_q3_source_end_frame,
                  .objective_pickup = application_native_q3_objective_pickup,
                  .objective_admitted = application_native_q3_objective_admitted,
                  .objective_drop = q3_selected_objective_drop,
                  .teleport_destination = q3_selected_teleport_destination,
                  .objective_dropped = application_native_q3_objective_dropped,
                  .objective_expired = application_native_q3_objective_expired,
                  .objective_nodrop = application_native_q3_objective_nodrop,
                  .source_flags_cleared = application_native_q3_source_flags_cleared,
                  .source_obelisk_settings = application_native_q3_obelisk_settings,
                  .objective_obelisk_admitted = application_native_q3_obelisk_admitted,
                  .objective_obelisk_touch = application_native_q3_obelisk_touch,
                  .objective_obelisk_die = application_native_q3_obelisk_die,
                  .objective_obelisk_pain = application_native_q3_obelisk_pain,
                  .respawn = application_native_q3_client_respawn,
                  .combat_provider = q3_combat_provider,
                  .cheats_enabled = application_native_cheats_enabled,
                  .console_motion = application_native_console_motion,
                  .console_print = application_native_q3_console_print,
                  .source_log = application_native_q3_source_log,
                  .client_print = application_native_q3_client_print,
                  .server_command = application_native_q3_server_command,
                  .configstring_changed = application_native_q3_configstring_changed,
                  .ranking_report = application_rankings_report,
                  .ranking_warmup = application_rankings_warmup,
                  .grant_arsenal = application_native_grant_arsenal,
                  .give_item = application_native_give_item,
                  .suicide = application_native_suicide,
                  .award = application_native_q3_award},
        .random_seed = replacing ? replacement.random_seed
            : (uint32_t)(provider->owner * UINT32_C(2246822519)),
    };
    if (!qa_q3_create(&options, &provider->state.q3, error))
        return false;
    provider->component = qa_q3_component(provider->state.q3);
    provider->component.clock = provider->launch->selection.clock;
    application_native_q3_settings_options settings = {
        .context = provider,
        .send_server_command = application_native_q3_server_command,
        .remap_teams = application_native_q3_settings_source_remap,
        .print = application_native_q3_console_print,
    };
    return application_native_q3_wire_create(provider, world,
               application->operation == APPLICATION_PERSISTING, error) &&
           (provider->native_q3_settings ||
            application_native_q3_settings_create(provider, options.product, &settings, error)) &&
           application_native_q3_votes_create(provider, error) &&
           application_native_q3_team_status_create(provider, error) &&
           application_native_q3_ipfilters_create(provider, error) &&
           qa_q3_combat_policy(provider->state.q3, &provider->policy,
                               error);
}

static bool provider_clock_admit(void *context, uint64_t host_ns, uint64_t pending_ns,
    uint64_t *pending_after_ns, uint64_t *frame_ns, qa_error *error)
{
    application_provider *provider = context;
    const qa_cvars *cvars = provider->frame_time.cvars;
    if (!cvars)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source clock lost its actual GAME registry");
    const qa_cvar_view *setting = qa_cvars_read(cvars, provider->frame_time.dedicated);
    bool dedicated = setting ? setting->number != 0 : provider->application->dedicated;
    qa_ruleset_id dialect = qa_cvars_dialect(cvars);
    *frame_ns = 0;
    if (dialect == QA_RULESET_NETQUAKE || dialect == QA_RULESET_QUAKEWORLD) {
        if (pending_ns > UINT64_MAX - host_ns)
            return application_fail(error, QA_ERROR_ARGUMENT, "Source host interval exhausted");
        *pending_after_ns = pending_ns + host_ns;
        bool accepted;
        return qa_source_frame_time_admit(&provider->frame_time, *pending_after_ns, dialect == QA_RULESET_QUAKEWORLD, &accepted, frame_ns, error);
    }
    if (!host_ns) { *pending_after_ns = pending_ns; return true; }
    qa_source_frame_time_controls controls;
    double milliseconds;
    if (!qa_source_frame_time_controls_read(&provider->frame_time, &controls, error) ||
        !qa_source_frame_time_transform(dialect, (double)host_ns / 1000000,
            &controls, dedicated, true, &milliseconds, error)) return false;
    const qa_cvar_view *fps = qa_cvars_read(cvars, provider->frame_time.capture_fps);
    if (dialect == QA_RULESET_Q3 && !dedicated && fps && fps->number > 0 && milliseconds > 0 &&
        application_world_provider(provider->application, QA_ROLE_ENTITIES, "") == provider) {
        qa_capture_clock capture;
        if (!qa_capture_frame_time(milliseconds, fps->number, (float)controls.timescale,
            provider->application->state == QA_APPLICATION_RUNNING, false, &capture, error)) return false;
        milliseconds = capture.milliseconds;
    }
    uint64_t prior = pending_ns;
    double ns;
    if (dialect == QA_RULESET_Q2_RERELEASE && controls.fixedtime == 0 && controls.timescale > 0) {
        uint64_t fraction_ns = pending_ns % UINT64_C(1000000);
        float fraction = (float)((double)fraction_ns / 1000000);
        float product = (float)milliseconds;
        float total = fraction + product;
        double whole = trunc((double)total);
        float remainder = total - (float)whole;
        ns = whole * 1000000 + (double)remainder * 1000000;
        prior -= fraction_ns;
    } else ns = milliseconds * 1000000;
    if (!isfinite(ns) || ns < 0 || ns >= 18446744073709551616.0 || prior > UINT64_MAX - (uint64_t)ns)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source frame duration exceeds the native elapsed range");
    *pending_after_ns = prior + (uint64_t)ns;
    return true;
}

static bool provider_clock_bind(application_provider *provider, qa_error *error)
{
    if (provider->component.clock.kind == QA_RULESET_QUAKEWORLD) {
        qa_console *console; qa_cvars *cvars;
        if (!application_guest_console_at(provider, 0, &console, &cvars, NULL) || !cvars)
            return application_fail(error, QA_ERROR_ARGUMENT, "QW clock requires its actual GAME registry");
        static const char *const names[] = {"sv_mintic", "sv_maxtic"};
        static const char *const values[] = {"0.03", "0.1"};
        for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
            if (!qa_cvars_register(cvars, names[i], values[i], 0, provider->owner, NULL, error)) return false;
    }
    qa_console *console; qa_cvars *cvars = NULL;
    (void)application_guest_console_at(provider, 0, &console, &cvars, NULL);
    qa_source_frame_time_bind(cvars, &provider->frame_time);
    provider->sv_novis = qa_cvars_resolve(cvars, "sv_novis");
    provider->q2_maxclients = qa_cvars_resolve(cvars, "maxclients");
    provider->q2_airaccelerate = qa_cvars_resolve(cvars, "sv_airaccelerate");
    provider->teamplay = qa_cvars_resolve(cvars, "teamplay");
    provider->sv_phs = qa_cvars_resolve(cvars, "sv_phs");
    provider->component.clock_admit = provider_clock_admit;
    provider->component.clock_context = provider;
    return true;
}

static bool construct_provider(qa_application *application,
                                    application_provider *provider,
                                    qa_world *world,
                                    qa_catalog *catalog,
                                    const qa_product *product,
                                    const qa_launch_choices *choices,
                                    qa_error *error)
{
    if (application == NULL || provider == NULL || world == NULL ||
        catalog == NULL || product == NULL || choices == NULL || provider->constructed ||
        qa_catalog_product(catalog, product->id) != product)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "invalid detached provider construction");
    provider->constructed = true;
    if (!provider_product_bind(provider, catalog, product, error)) return false;
    if (provider->client_only_owned || application_native_client_only(provider))
        return true;
    bool ok;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1:
        ok = construct_q1(application, provider, world, product, choices,
                          error);
        break;
    case APPLICATION_PROVIDER_Q2:
        ok = construct_q2(application, provider, world, product, choices,
                          error);
        break;
    case APPLICATION_PROVIDER_Q3:
        ok = construct_q3(application, provider, world, product, choices,
                          error);
        break;
    case APPLICATION_PROVIDER_QC:
        ok = application_construct_qc(application, provider, world, product,
                                       choices, error);
        break;
    case APPLICATION_PROVIDER_QVM:
        ok = application_construct_q3_guest(application, provider, world, product,
                                             choices, error);
        break;
    case APPLICATION_PROVIDER_NATIVE:
        ok = product->family == QA_GAME_Q2
            ? application_construct_native_q2(application, provider, world,
                                               product, choices, error)
            : application_construct_q3_guest(application, provider, world,
                                               product, choices, error);
        break;
    default:
        provider->constructed = false;
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "unknown application provider runtime");
    }
    if (!ok) {
        qa_error ignored = {0};
        (void)application_provider_deconstruct(provider, &ignored);
        return false;
    }
    if (!provider_clock_bind(provider, error)) return false;
    return true;
}

bool application_provider_construct(qa_application *application,
    application_provider *provider, qa_world *world, qa_catalog *catalog,
    const qa_product *product, const qa_launch_choices *choices, qa_error *error)
{
    qa_cvars_edit *values = NULL;
    application_provider *previous = NULL;
    if (!provider_preparation_enter(application, provider, choices, &previous, &values, error)) return false;
    bool ok = construct_provider(application, provider, world, catalog, product, choices, error);
    qa_error returned = {0};
    bool left = provider_preparation_leave(application, previous, values, &returned);
    if (!left && ok && error) *error = returned;
    return ok && left;
}

bool application_provider_construct_q3_restored(qa_application *application,
    application_provider *provider, qa_world *world, qa_catalog *catalog,
    const qa_product *product, const qa_launch_choices *choices,
    const qa_save_record *record, qa_error *error)
{
    if (application == NULL || provider == NULL || world == NULL ||
        catalog == NULL || product == NULL || choices == NULL || record == NULL ||
        provider->application != application || provider->constructed ||
        (provider->kind != APPLICATION_PROVIDER_QVM && provider->kind != APPLICATION_PROVIDER_NATIVE) ||
        application->operation != APPLICATION_PERSISTING ||
        qa_catalog_product(catalog, product->id) != product)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Invalid detached saved original Q3 provider construction");
    provider->constructed = true;
    if (!provider_product_bind(provider, catalog, product, error)) return false;
    if (!application_guest_q3_save_prepare(provider, world, product, choices, record, error)) {
        qa_error ignored = {0};
        (void)application_provider_deconstruct(provider, &ignored);
        return false;
    }
    if (!provider_clock_bind(provider, error)) return false;
    return true;
}

static bool deconstruct_provider(application_provider *provider, qa_error *error)
{
    if (provider == NULL) return true;
    if (!application_bots_npc_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Monster source navigation is entered");
    if (provider->attached || provider->component_attached ||
        provider->policy_attached)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "cannot destroy an attached provider instance");
    if (!application_startup_source_deconstruct(provider, error)) return false;
    if (provider->client_only_owned || application_native_client_only(provider)) {
        if (!application_native_client_roles_destroy(provider, error)) return false;
        provider->constructed = false;
        provider->map_bound = false;
        provider->component = (qa_component){0};
        provider->policy = (qa_combat_policy){0};
        return true;
    }
    if (!provider->constructed) {
        if (provider->kind == APPLICATION_PROVIDER_Q1) {
            application_native_q1_wire_destroy(provider);
            return application_native_q1_console_destroy(provider, error);
        }
        if (provider->kind == APPLICATION_PROVIDER_Q2)
            return application_native_q2_console_destroy(provider, error);
        if (provider->kind == APPLICATION_PROVIDER_Q3)
            return application_native_q3_remote_roles_destroy(provider, error) &&
                application_native_q3_settings_destroy(provider, error) &&
                application_native_q3_console_destroy(provider, error);
        if (provider->kind == APPLICATION_PROVIDER_QC)
            return application_qc_console_destroy(provider, error);
        if (provider->kind == APPLICATION_PROVIDER_QVM ||
            (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.engine))
            return application_q3_guest_deconstruct(provider, error);
        if (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.q2_engine)
            return application_native_q2_deconstruct(provider, error);
        return true;
    }
    bool ok = true;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1:
        if (provider->state.q1 == NULL) {
            application_native_q1_wire_destroy(provider);
            ok = application_native_q1_console_destroy(provider, error);
            break;
        }
        if (!application_native_q1_console_idle(provider) || !application_native_q1_wire_idle(provider) ||
            !application_bots_npc_idle(provider))
            return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 source console or catalog is borrowed");
        application_bots_npc_destroy(provider);
        qa_q1_game_destroy(provider->state.q1);
        qa_q1_game_operation_end(&provider->q1_lifetime);
        provider->state.q1 = NULL;
        qa_q1_level_destroy(provider->q1_level);
        provider->q1_level = NULL;
        qa_q1_campaign_source_destroy(provider->q1_campaign);
        provider->q1_campaign = NULL;
        application_native_q1_wire_destroy(provider);
        ok = application_native_q1_console_destroy(provider, error);
        break;
    case APPLICATION_PROVIDER_Q2:
        if (provider->state.q2 == NULL) {
            ok = application_native_q2_console_destroy(provider, error);
            break;
        }
        if (!application_native_q2_console_idle(provider))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 source console is borrowed");
        ok = qa_q2_destroy(provider->state.q2, error);
        if (ok) {
            provider->state.q2 = NULL;
            ok = application_native_q2_console_destroy(provider, error);
        }
        break;
    case APPLICATION_PROVIDER_Q3:
        if (!application_native_q3_remote_roles_destroy(provider, error)) return false;
        if (provider->state.q3 == NULL) {
            ok = application_native_q3_console_destroy(provider, error);
            break;
        }
        if (!application_native_q3_console_idle(provider) ||
            !application_native_q3_settings_idle(provider) ||
            !application_native_q3_ipfilters_idle(provider) ||
            !application_native_q3_votes_idle(provider) ||
            !application_native_q3_team_status_idle(provider) ||
            !application_native_q3_wire_idle(provider) ||
            !application_native_q3_wire_destroy_ready(provider) ||
            !qa_q3_destroy_ready(provider->state.q3)) {
            ok = application_fail(error, QA_ERROR_ARGUMENT, "native Q3 source owners are borrowed");
            break;
        }
        ok = application_native_q3_votes_destroy(provider, error) &&
             application_native_q3_team_status_destroy(provider, error) &&
             application_native_q3_ipfilters_destroy(provider, error) &&
             application_native_q3_settings_destroy(provider, error) &&
             application_native_q3_wire_destroy(provider, error) &&
             qa_q3_destroy(provider->state.q3, error);
        if (ok) {
            provider->state.q3 = NULL;
            ok = application_native_q3_console_destroy(provider, error);
        }
        break;
    case APPLICATION_PROVIDER_QC:
        ok = application_qc_deconstruct(provider, error);
        if (ok) application_bots_npc_destroy(provider);
        break;
    case APPLICATION_PROVIDER_QVM:
        ok = application_q3_guest_deconstruct(provider, error);
        break;
    case APPLICATION_PROVIDER_NATIVE:
        if (provider->state.native.q2_engine != NULL) {
            ok = application_native_q2_deconstruct(provider, error);
            break;
        }
        if (provider->state.native.engine != NULL) {
            ok = application_q3_guest_deconstruct(provider, error);
            break;
        }
        if (provider->state.native.host != NULL) {
            if (!qa_native_host_destroy_ready(provider->state.native.host) ||
                (provider->state.native.q3_host &&
                 !qa_q3_host_destroy_ready(provider->state.native.q3_host))) {
                ok = application_fail(error, QA_ERROR_ARGUMENT,
                                      "native provider still has active calls or bindings");
                break;
            }
            ok = qa_native_host_destroy_owned(&provider->state.native.host, error);
            if (!provider->state.native.host && provider->state.native.q3_host)
                qa_q3_host_native_consumed(provider->state.native.q3_host);
            if (!ok)
                break;
        }
        if (provider->state.native.q3_host != NULL) {
            ok = qa_q3_host_destroy(provider->state.native.q3_host, error);
            if (!ok)
                break;
            provider->state.native.q3_host = NULL;
        }
        break;
    }
    if (ok) {
        application_q1_signon_drop(provider->application, provider->owner);
        provider->constructed = false;
        provider->map_bound = false;
        provider->component = (qa_component){0};
        provider->policy = (qa_combat_policy){0};
    }
    return ok;
}

bool application_provider_deconstruct(application_provider *provider, qa_error *error)
{
    if (provider && provider->hosted_video_leases)
        return application_fail(error, QA_ERROR_ARGUMENT, "Provider retains an actual hosted video restart recipe");
    qa_application *app = provider ? provider->application : NULL;
    application_provider *previous = app ? app->engine_shutdown_provider : NULL;
    if (app && app->engine_shutdown) app->engine_shutdown_provider = provider;
    if (provider && !provider->event_activation_bound &&
        application_unified_event_owner_bound_is(app, provider))
        provider->event_activation_bound = true;
    bool ok = deconstruct_provider(provider, error);
    if (ok && provider) {
        provider->native_q1_restore_game = (qa_bytes){0};
        provider->native_q1_restore_npc = (qa_bytes){0};
    }
    if (ok && provider) application_network_q2_capture_dispose(provider);
    if (ok && provider) application_q3_wire_capture_dispose(provider);
    if (ok) application_network_q2_retire_source_bindings(provider);
    if(ok && provider && provider->event_activation_bound)
        ok=application_unified_event_registration_clear(app,provider->owner,error);
    if(ok && provider && provider->event_activation_bound) {
        ok = provider->event_retirement_frame_present ?
            application_unified_event_owner_retire(app, provider->owner, &provider->event_retirement_frame, error) :
            application_unified_persistent_retire(app,provider->owner,(qa_actor_id){0},error);
        if (ok) {
            provider->event_retirement_frame_present = false;
            provider->event_activation_bound = false;
        }
    }
    if (app) app->engine_shutdown_provider = previous;
    return ok;
}

bool application_guest_spawn_map(application_provider *provider,
                                  const qa_bsp_view *map, const qa_entities *entities,
                                  qa_string_id name, qa_string_id spawn_point,
                                  qa_error *error)
{
    if (provider == NULL || !provider->constructed)
        return application_fail(error, QA_ERROR_ARGUMENT, "guest map owner is not constructed");
    bool ok;
    if (provider->kind == APPLICATION_PROVIDER_QC)
        ok = application_qc_spawn_map(provider, map, entities, name, spawn_point, error);
    else if (provider->kind == APPLICATION_PROVIDER_NATIVE &&
        provider->state.native.q2_engine != NULL)
        ok = application_native_q2_spawn_map(provider, map, entities, name, spawn_point, error);
    else if (provider->kind == APPLICATION_PROVIDER_QVM ||
        (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.engine != NULL))
        ok = application_q3_guest_spawn_map(provider, map, entities, name, spawn_point, error);
    else
        return application_fail(error, QA_ERROR_UNSUPPORTED, "selected provider has no guest map adapter");
    if (ok) provider->map_bound = true;
    return ok;
}
