#include "internal.h"
#include "qa/application_weapon_availability.h"
#include "qa/game_q1_inventory.h"

bool qa_application_weapon_availability_read(qa_application *app, qa_actor_id actor,
    const qa_item_definition *definition, bool *available, bool *found, qa_error *error)
{
    if (!app || !app->session || app->destroy_requested ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING) ||
        !definition || !definition->item || !available || !found ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Weapon availability requires its actual actor and item definition");
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    bool present = false, ready = false;
    if (definition->weapon && provider && provider->constructed && provider->attached &&
        !provider->close_pending && provider->owner == definition->owner &&
        provider->kind == APPLICATION_PROVIDER_Q1) {
        application_operation prior = app->operation;
        uint64_t generation = app->publication_generation, map = app->map_revision;
        app->operation = APPLICATION_ADVANCING;
        bool ok = qa_q1_game_weapon_item_available(provider->state.q1, actor, definition->item,
            &ready, &present, error);
        if (ok && (app->publication_generation != generation || app->map_revision != map ||
            application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider ||
            !provider->constructed || !provider->attached || provider->close_pending ||
            !qa_actors_get(qa_session_actors(app->session), actor)))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Weapon availability lost its actual arsenal owner");
        app->operation = prior;
        if (!ok) return false;
    }
    *available = ready; *found = present; return true;
}
