#include "internal.h"

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
    case QA_PROGRAM_BUILTIN:
        switch (launch->selection.clock.kind) {
        case QA_CLOCK_NETQUAKE:
        case QA_CLOCK_QUAKEWORLD:
            *out = APPLICATION_PROVIDER_Q1;
            return true;
        case QA_CLOCK_Q2_CLASSIC:
        case QA_CLOCK_Q2_RERELEASE:
            *out = APPLICATION_PROVIDER_Q2;
            return true;
        case QA_CLOCK_Q3:
            *out = APPLICATION_PROVIDER_Q3;
            return true;
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
    if (!qa_strings_intern_cstr(qa_session_strings(application->session),
                                launch->selection.instance, &owner, error))
        return false;
    application_provider *provider = calloc(1, sizeof(*provider));
    if (provider == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot allocate application provider");
    provider->application = application;
    provider->owner = owner;
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
    uint32_t maximum_clients;
    bool cooperative, deathmatch, friendly_fire;
} application_native_profile;

static bool provider_family(const qa_launch_instance *launch,
                            qa_game_family *out)
{
    switch (launch->selection.clock.kind) {
    case QA_CLOCK_NETQUAKE:
    case QA_CLOCK_QUAKEWORLD:
        *out = QA_GAME_Q1;
        return true;
    case QA_CLOCK_Q2_CLASSIC:
    case QA_CLOCK_Q2_RERELEASE:
        *out = QA_GAME_Q2;
        return true;
    case QA_CLOCK_Q3:
        *out = QA_GAME_Q3;
        return true;
    }
    return false;
}

static bool native_profile(const qa_launch_instance *launch,
                           const qa_launch_choices *choices,
                           application_native_profile *out,
                           qa_error *error)
{
    if (launch == NULL || choices == NULL || out == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "native construction profile needs launch choices");
    qa_game_family family;
    if (!provider_family(launch, &family))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "provider clock has no native game family");

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
        profile.maximum_clients = (uint32_t)choices->seat_count;
    } else if (family == QA_GAME_Q2) {
        profile.skill = choices->world.skill;
    }
    *out = profile;
    return true;
}

static void profile_word(qa_sha256_context *hash, uint64_t value)
{
    uint8_t encoded[8];
    for (size_t index = 0; index < sizeof(encoded); ++index)
        encoded[index] = (uint8_t)(value >> (index * 8));
    qa_sha256_update(hash, (qa_bytes){encoded, sizeof(encoded)});
}

bool application_instance_configuration(void *opaque,
                                        const qa_launch_instance *launch,
                                        const qa_launch_choices *choices,
                                        qa_sha256_digest *out,
                                        qa_error *error)
{
    (void)opaque;
    if (out == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "provider configuration needs digest output");
    application_native_profile profile;
    if (!native_profile(launch, choices, &profile, error))
        return false;
    qa_sha256_context hash;
    qa_sha256_init(&hash);
    static const uint8_t domain[] = "application-native-profile-v1";
    qa_sha256_update(&hash, (qa_bytes){domain, sizeof(domain) - 1});
    profile_word(&hash, profile.family);
    switch (profile.family) {
    case QA_GAME_Q1:
        profile_word(&hash, (uint32_t)profile.skill);
        profile_word(&hash, (uint32_t)profile.teamplay);
        profile_word(&hash, profile.maximum_clients);
        profile_word(&hash, profile.cooperative);
        profile_word(&hash, profile.deathmatch);
        break;
    case QA_GAME_Q2:
        profile_word(&hash, (uint32_t)profile.skill);
        profile_word(&hash, profile.cooperative);
        profile_word(&hash, profile.deathmatch);
        break;
    case QA_GAME_Q3:
        profile_word(&hash, profile.mode_kind);
        profile_word(&hash, profile.friendly_fire);
        break;
    }
    qa_sha256_final(&hash, out);
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

static bool construct_q1(qa_application *application,
                         application_provider *provider, qa_world *world,
                         const qa_product *product,
                         const qa_launch_choices *choices, qa_error *error)
{
    application_native_profile profile;
    if (!native_profile(provider->launch, choices, &profile, error))
        return false;
    qa_q1_options options = {
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
        .gravity = 800.0f,
        .aim_threshold = 0.93f,
        .max_clients = profile.maximum_clients,
        .random_seed = (uint32_t)(provider->owner * UINT32_C(2654435761)),
    };
    qa_q1_host host = {.context = provider,
                       .find_target = q1_find_target,
                       .find_targets = q1_find_targets,
                       .combat_provider = q1_combat_provider};
    qa_builtin_services services = application_builtin_services(
        application, world, application->physics);
    if (!qa_q1_game_create(&services, &options, &host,
                           &provider->state.q1, error) ||
        !qa_q1_game_retain(provider->state.q1, &provider->q1_lifetime, error) ||
        !qa_q1_game_component(provider->state.q1, &provider->component,
                              error) ||
        !qa_q1_game_combat_policy(provider->state.q1, &provider->policy,
                                  error))
        return false;
    provider->component.clock = provider->launch->selection.clock;
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
    if (!qa_q2_create(&services, &options, NULL, &provider->state.q2,
                      error))
        return false;
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

static qa_actor_owner q3_combat_provider(void *opaque, qa_actor_id target,
                                         qa_actor_owner fallback)
{
    application_provider *provider = opaque;
    application_provider *selected = application_provider_for(
        provider->application, target, QA_ROLE_COMBAT, "");
    return selected == NULL ? fallback : selected->owner;
}

static bool construct_q3(qa_application *application,
                         application_provider *provider, qa_world *world,
                         const qa_product *product,
                         const qa_launch_choices *choices, qa_error *error)
{
    application_native_profile profile;
    if (!native_profile(provider->launch, choices, &profile, error))
        return false;
    qa_q3_rules rules = qa_q3_default_rules();
    rules.game_type = q3_game_type(profile.mode_kind);
    rules.friendly_fire = profile.friendly_fire;
    qa_q3_options options = {
        .services = application_builtin_services(application, world,
                                                 application->physics),
        .owner = provider->owner,
        .product = strcmp(product->campaign, "missionpack") == 0
                       ? QA_Q3_TEAM_ARENA
                       : QA_Q3_ARENA,
        .rules = rules,
        .hooks = {.context = provider,
                  .combat_provider = q3_combat_provider},
        .random_seed = (uint32_t)(provider->owner * UINT32_C(2246822519)),
    };
    if (!qa_q3_create(&options, &provider->state.q3, error))
        return false;
    provider->component = qa_q3_component(provider->state.q3);
    provider->component.clock = provider->launch->selection.clock;
    return qa_q3_combat_policy(provider->state.q3, &provider->policy,
                               error);
}

bool application_provider_construct(qa_application *application,
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
    qa_catalog_retain(catalog);
    qa_catalog_release(provider->product_catalog);
    provider->product_catalog = catalog;
    provider->product = product;
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
        if (product->family != QA_GAME_Q3) {
            provider->constructed = false;
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                                    "native Q2 application module owner is not installed");
        }
        ok = application_construct_q3_guest(application, provider, world, product,
                                             choices, error);
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
    return true;
}

bool application_provider_deconstruct(application_provider *provider,
                                      qa_error *error)
{
    if (provider == NULL || !provider->constructed)
        return true;
    if (provider->attached || provider->component_attached ||
        provider->policy_attached)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "cannot destroy an attached provider instance");
    bool ok = true;
    switch (provider->kind) {
    case APPLICATION_PROVIDER_Q1:
        qa_q1_game_destroy(provider->state.q1);
        qa_q1_game_operation_end(&provider->q1_lifetime);
        provider->state.q1 = NULL;
        qa_q1_level_destroy(provider->q1_level);
        provider->q1_level = NULL;
        qa_q1_campaign_source_destroy(provider->q1_campaign);
        provider->q1_campaign = NULL;
        break;
    case APPLICATION_PROVIDER_Q2:
        ok = qa_q2_destroy(provider->state.q2, error);
        if (ok)
            provider->state.q2 = NULL;
        break;
    case APPLICATION_PROVIDER_Q3:
        ok = qa_q3_destroy(provider->state.q3, error);
        if (ok)
            provider->state.q3 = NULL;
        break;
    case APPLICATION_PROVIDER_QC:
        ok = application_qc_deconstruct(provider, error);
        break;
    case APPLICATION_PROVIDER_QVM:
        ok = application_q3_guest_deconstruct(provider, error);
        break;
    case APPLICATION_PROVIDER_NATIVE:
        if (provider->state.native.engine != NULL) {
            ok = application_q3_guest_deconstruct(provider, error);
            break;
        }
        if (provider->state.native.host != NULL) {
            if (!qa_native_host_destroy_ready(provider->state.native.host)) {
                ok = application_fail(error, QA_ERROR_ARGUMENT,
                                      "native provider still has active calls or bindings");
                break;
            }
            ok = qa_native_host_destroy(provider->state.native.host, error);
            provider->state.native.host = NULL;
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
        provider->constructed = false;
        provider->map_bound = false;
        provider->component = (qa_component){0};
        provider->policy = (qa_combat_policy){0};
    }
    return ok;
}

bool application_guest_spawn_map(application_provider *provider,
                                  const qa_bsp_view *map, const qa_entities *entities,
                                  qa_string_id name, qa_string_id spawn_point,
                                  qa_error *error)
{
    if (provider == NULL || !provider->constructed)
        return application_fail(error, QA_ERROR_ARGUMENT, "guest map owner is not constructed");
    if (provider->kind == APPLICATION_PROVIDER_QC)
        return application_qc_spawn_map(provider, map, entities, name, spawn_point, error);
    if (provider->kind == APPLICATION_PROVIDER_QVM ||
        (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.engine != NULL))
        return application_q3_guest_spawn_map(provider, map, entities, name, spawn_point, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED, "selected provider has no guest map adapter");
}
