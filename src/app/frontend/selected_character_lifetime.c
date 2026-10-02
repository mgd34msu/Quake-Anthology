#include "selected_character_lifetime.h"
#include "selected_character_private.h"

bool frontend_selected_character_refresh(qa_frontend *frontend, qa_error *error)
{
    if (!frontend || frontend->capture || !frontend_selected_character_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character publication refresh retains an actual output or registry lease");
    for (frontend_selected_character *owner = frontend->selected_characters; owner; owner = owner->next)
        if (owner->restoring || !owner->view.selection.current || !owner->view.selection.release || !owner->appearance_lease)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character publication refresh precedes genuine private import");
    frontend_selected_character **link = &frontend->selected_characters;
    while (*link) {
        frontend_selected_character *owner = *link;
        bool current = owner->view.selection.current(owner->view.selection.lifetime, &owner->view.selection) &&
            qa_application_q3_asset_selection_current(frontend->application, &owner->view.appearance);
        if (current) { link = &owner->next; continue; }
        *link = owner->next; frontend_selected_character_dispose(owner);
    }
    return true;
}
