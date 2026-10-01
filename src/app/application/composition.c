#include "internal.h"
#include "q3_product.h"
#include "startup_flow.h"

#include <stdlib.h>

static bool configuration_safe(void *opaque)
{
    qa_application *application = opaque;
    return application != NULL && !application->publication_started &&
           application_guests_idle(application) &&
           application_bots_can_destroy(application) &&
           application->session != NULL &&
           qa_session_safe(application->session) &&
           (application->world == NULL || qa_world_idle(application->world)) &&
           application->combat != NULL && qa_combat_idle(application->combat);
}

static bool prepare_instance(void *opaque, const qa_launch_instance *launch,
                             void **state, qa_error *error)
{
    if (state == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "provider preparation needs state output");
    application_provider *provider = NULL;
    bool ok = application_provider_prepare(opaque, launch, &provider, error);
    *state = provider;
    return ok;
}

static void close_instance(void *opaque, void *state)
{
    application_provider_release(opaque, state);
}

static bool prepare_publication(void *opaque,
                                const qa_launch_snapshot *previous,
                                const qa_launch_snapshot *candidate,
                                void **ticket, qa_error *error)
{
    return application_publication_prepare(opaque, previous, candidate, ticket,
                                           error);
}

static void rollback_publication(void *opaque, void *ticket)
{
    application_publication_rollback(opaque, ticket);
}

static void publish(void *opaque, const qa_launch_snapshot *previous,
                    const qa_launch_snapshot *candidate, void *ticket)
{
    application_publication_publish(opaque, previous, candidate, ticket);
}

static bool retire(void *opaque, const qa_launch_snapshot *previous,
                    qa_error *error)
{
    (void)previous;
    return application_publication_retire(opaque, error);
}

bool application_composition_create(qa_application *application,
                                    qa_error *error)
{
    if (application == NULL || application->session == NULL ||
        application->combat == NULL || application->inventory == NULL ||
        application->pickups == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "application composition needs shared authorities");

    uint32_t actor_capacity =
        qa_actors_capacity(qa_session_actors(application->session));
    application->physics = calloc(1, sizeof(*application->physics));
    application->motion = calloc(actor_capacity, sizeof(*application->motion));
    application->controls = calloc(actor_capacity,
                                   sizeof(*application->controls));
    application->q2_visuals = calloc(actor_capacity,
                                     sizeof(*application->q2_visuals));
    if (application->physics == NULL || application->motion == NULL ||
        application->controls == NULL || application->q2_visuals == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot allocate application movement state");
    application->motion_capacity = actor_capacity;
    application->control_capacity = actor_capacity;
    application->q2_visual_capacity = actor_capacity;
    qa_arena_init(&application->event_arena, 16384);

    qa_target_options targets = application_target_options(application);
    application->targets = qa_targets_create(&targets, error);
    if (application->targets == NULL)
        return false;

    qa_configuration_hooks hooks = {
        .context = application,
        .safe = configuration_safe,
        .instance_configuration = application_instance_configuration,
        .prepare_instance = prepare_instance,
        .close_instance = close_instance,
        .prepare_publication = prepare_publication,
        .rollback_publication = rollback_publication,
        .publish = publish,
        .retire = retire,
    };
    return qa_configuration_create(&hooks, &application->configuration, error);
}

bool application_composition_destroy(qa_application *application,
                                     qa_error *error)
{
    if (application == NULL || application->configuration == NULL)
        return true;
    if (!qa_application_startup_abort(application, error)) return false;
    qa_configuration *configuration = application->configuration;
    if (!qa_configuration_destroy(configuration, error))
        return false;
    application->configuration = NULL;
    return true;
}

bool application_apply(qa_application *application,
                       const qa_launch_draft *draft, qa_error *error)
{
    if (qa_application_startup_pending(application))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup configuration already retains a candidate");
    if (application->startup_hooks)
        return application_startup_flow_begin(application, draft, error);
    application_q3_product_preparation prepared = {0};
    if (!application_q3_product_prepare_draft(application, draft, &prepared, error))
        return false;
    qa_configuration_transaction *transaction = NULL;
    if (!qa_configuration_prepare(application->configuration, prepared.draft,
                                  &transaction, error)) {
        application_q3_product_finish(application, &prepared, false);
        return false;
    }
    if (!qa_configuration_validate(transaction, error)) {
        qa_error ignored = {0};
        (void)qa_configuration_abort(transaction, &ignored);
        application_q3_product_finish(application, &prepared, false);
        return false;
    }
    if (!qa_configuration_commit(transaction, error)) {
        qa_error ignored = {0};
        (void)qa_configuration_abort(transaction, &ignored);
        application_q3_product_finish(application, &prepared, false);
        return false;
    }
    application_q3_product_finish(application, &prepared, true);
    if (application->state == QA_APPLICATION_FAULTED) {
        if (error != NULL)
            *error = application->publication_error;
        return false;
    }
    return true;
}
