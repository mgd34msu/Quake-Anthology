#include "equipment_actions.h"
#include "equipment_runtime.h"

bool application_equipment_q3_weapon_request(void *context, qa_actor_id actor,
    qa_item_id item, bool *handled, qa_error *error)
{
    application_provider *provider = context;
    if (!provider || !handled || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->application || !provider->constructed || !provider->attached ||
        provider->close_pending || !provider->state.q3 ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q3 inventory weapon request lost its actual native owner");
    *handled = false;
    qa_application *app = provider->application;
    if (application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q3 inventory weapon request is not owned by the selected arsenal");
    qa_equipment_state state;
    if (!app->equipment || !qa_equipment_read(app->equipment, actor, &state)) return true;
    if (state.selection.grapple != QA_GRAPPLE_Q3 ||
        state.selection.binding != QA_EQUIPMENT_WEAPON_SLOT) return true;
    if (!application_equipment_runtime_owner_current(app->equipment_runtime, state.sources.grapple))
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "Q3 inventory request lost its retained grapple source");
    qa_item_id grapple = qa_q3_weapon_item(provider->state.q3, QA_Q3_W_GRAPPLE, false);
    if (item == grapple) {
        if (!qa_equipment_select_grapple(app->equipment, actor, true, error)) return false;
        *handled = true;
        return true;
    }
    return qa_equipment_select_grapple(app->equipment, actor, false, error);
}
