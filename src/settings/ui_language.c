#include "qa/ui_language.h"
#include "qa/input_platform.h"
#include <stdio.h>
bool qa_ui_language_name(uint32_t seat, char out[64], qa_error *error) {
    if (!out || seat >= QA_INPUT_LOCAL_SEATS) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Language requires its physical ENGINE seat");
        return false;
    }
    snprintf(out, 64, "ui_seat%u_language", seat + 1);
    return true;
}
bool qa_ui_language_read(const qa_cvars *cvars, const qa_ui_preference_handles *handles,
    uint32_t seat, const char **out, qa_error *error) {
    if (!cvars || !handles || !out || seat >= QA_INPUT_LOCAL_SEATS) return false;
    const qa_cvar_view *value = qa_cvars_read(cvars, handles->seats[seat][QA_UI_PREF_LANGUAGE]);
    if (!value || !value->value || !*value->value) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Actual physical ENGINE language is not admitted");
        return false;
    }
    *out = value->value;
    return true;
}
