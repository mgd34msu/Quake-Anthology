#include "accessibility.h"
#include "qa/text.h"
#include "menu_fonts.h"
#include "ui_features_private.h"
#include <stdio.h>

static bool language_draft(frontend_ui_seat_features *state, const char *text, qa_error *error)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (!copy) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining accessibility language draft");
    memcpy(copy, text, size); free(state->accessibility_language); state->accessibility_language = copy;
    return true;
}
static bool action(void *context, uint32_t id, qa_ui_id control,
    const qa_ui_action *action, qa_error *error)
{
    frontend_seat *seat = context;
    frontend_ui_seat_features *state = frontend_ui_features_seat(seat);
    if (!state || id != seat->id || !control || control > QA_UI_PREF_COUNT + 1)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Accessibility action has no physical seat preference");
    qa_cvars *cvars = qa_application_cvars(seat->frontend->application);
    if (control == QA_UI_PREF_COUNT + 1)
        return action->kind != QA_UI_ACTIVATE ||
            qa_ui_preference_set(cvars, seat->id, QA_UI_PREF_LANGUAGE, state->accessibility_language ? state->accessibility_language : "", error);
    qa_ui_preference preference = (qa_ui_preference)(control - 1);
    const qa_ui_preference_description *description = qa_ui_preference_describe(preference);
    if (description->kind == QA_UI_PREFERENCE_LANGUAGE) {
        if (action->kind == QA_UI_CHANGE_TEXT)
            return language_draft(state, action->value.text ? action->value.text : "", error);
        return action->kind != QA_UI_SUBMIT ||
            qa_ui_preference_set(cvars, seat->id, preference, state->accessibility_language ? state->accessibility_language : "", error);
    }
    if (description->kind == QA_UI_PREFERENCE_CHOICE) {
        if (action->kind != QA_UI_SELECT) return true;
        if (action->value.row >= description->choice_count)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Accessibility choice is outside its authored options");
        return qa_ui_preference_set(cvars, seat->id, preference, description->choices[action->value.row], error);
    }
    if (action->kind != QA_UI_CHANGE_NUMBER) return true;
    char value[32];
    if (!qa_format_number(action->value.number, value, error)) return false;
    return qa_ui_preference_set(cvars, seat->id, preference, value, error);
}
static bool open_menu(void *context, uint32_t id, qa_error *error)
{
    frontend_seat *seat = context;
    frontend_ui_seat_features *state = frontend_ui_features_seat(seat);
    if (!state || id != seat->id) return frontend_fail(error, QA_ERROR_ARGUMENT, "Accessibility draft has no physical seat owner");
    qa_ui_preferences preferences;
    if (!qa_ui_preferences_read(qa_application_cvars(seat->frontend->application), seat->id, &preferences, error)) return false;
    return language_draft(state, preferences.language, error);
}
static bool menu(void *context, uint32_t id, qa_ui_menu *out, qa_error *error)
{
    frontend_seat *seat = context;
    frontend_ui_seat_features *state = frontend_ui_features_seat(seat);
    if (!state || id != seat->id) return frontend_fail(error, QA_ERROR_ARGUMENT, "Accessibility menu has no physical seat owner");
    qa_cvars *cvars = qa_application_cvars(seat->frontend->application);
    for (unsigned key = 0; key < QA_UI_PREF_COUNT; ++key) {
        const qa_ui_preference_description *description = qa_ui_preference_describe((qa_ui_preference)key);
        char name[64];
        if (!qa_ui_preference_name(seat->id, (qa_ui_preference)key, name, error)) return false;
        const qa_cvar_view *value = qa_cvars_find(cvars, name);
        if (!value) return frontend_fail(error, QA_ERROR_ARGUMENT, "Accessibility preferences are not registered");
        qa_ui_control *control = &seat->controls[key];
        *control = (qa_ui_control){.id = key + 1, .label = description->label,
            .rect = {40, 80 + (float)key * 28, 560, 26}, .enabled = true, .visible = true,
            .context = seat, .action = action};
        switch (description->kind) {
        case QA_UI_PREFERENCE_RANGE:
            control->kind = QA_UI_SLIDER;
            control->value.slider.value = value->number;
            control->value.slider.minimum = description->minimum; control->value.slider.maximum = description->maximum;
            control->value.slider.step = description->step; break;
        case QA_UI_PREFERENCE_TOGGLE:
            control->kind = QA_UI_TOGGLE; control->value.checked = value->integer != 0; break;
        case QA_UI_PREFERENCE_CHOICE:
            control->kind = QA_UI_CHOICE; control->value.choice.labels = description->choices;
            control->value.choice.count = description->choice_count;
            for (size_t i = 0; i < description->choice_count; ++i)
                if (!strcmp(value->value, description->choices[i])) { control->value.choice.selected = i; break; }
            break;
        case QA_UI_PREFERENCE_LANGUAGE:
            control->kind = QA_UI_FIELD; control->value.field.text = state->accessibility_language ? state->accessibility_language : "";
            size_t length = strlen(control->value.field.text);
            control->value.field.maximum = length > 63 ? length : 63; break;
        }
    }
    seat->controls[QA_UI_PREF_COUNT] = (qa_ui_control){.id = QA_UI_PREF_COUNT + 1,
        .kind = QA_UI_BUTTON, .label = "Apply language", .rect = {40, 408, 560, 30},
        .visible = true, .enabled = true, .context = seat, .action = action};
    *out = (qa_ui_menu){.id = FRONTEND_ACCESSIBILITY, .title = "Accessibility",
        .controls = seat->controls, .count = QA_UI_PREF_COUNT + 1, .fullscreen = true};
    return true;
}
bool frontend_accessibility_create(frontend_seat *seat, qa_error *error)
{
    return seat && qa_ui_register(seat->ui, &(qa_ui_menu_registration){.id = FRONTEND_ACCESSIBILITY,
        .context = seat, .factory = menu, .open = open_menu}, error);
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
