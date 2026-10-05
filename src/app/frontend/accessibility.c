#include "accessibility.h"
#include "qa/text.h"
#include "menu_fonts.h"
#include "ui_features_private.h"
#include "settings_menu.h"
#include <stdio.h>

bool frontend_accessibility_create(frontend_seat *seat, qa_error *error)
{
    return seat && qa_ui_register(seat->ui, &(qa_ui_menu_registration){.id = FRONTEND_ACCESSIBILITY,
        .context = seat, .factory = frontend_settings_accessibility_menu}, error);
}
bool frontend_accessibility_sync(qa_frontend *frontend, qa_error *error)
{
    if (!frontend || !frontend->application)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Accessibility requires its actual application");
    if (frontend->options.dedicated) return true;
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i]; qa_ui_preferences preferences;
        qa_font_selection selection;
        if (!qa_ui_preferences_read(qa_application_cvars(frontend->application), i, &preferences, error) ||
            !frontend_menu_font_selection(frontend, i, preferences.typeface == QA_UI_TYPEFACE_BOLD, &selection, error) ||
            !qa_ui_set_presentation(seat->ui, &selection, preferences.text_scale, preferences.color_mode, error)) return false;
        seat->fonts = selection;
    }
    return true;
}
