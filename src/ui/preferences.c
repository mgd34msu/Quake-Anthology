#include "qa/ui_preferences.h"
#include "qa/ui_language.h"
#include "qa/console_cvars_prepare.h"
#include "qa/text.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *const typefaces[] = {"standard", "bold"};
static const char *const color_modes[] = {"standard", "blue-yellow", "monochrome"};
static const qa_ui_preference_description descriptions[QA_UI_PREF_COUNT] = {
    {"hudScale", "HUD scale", "1", QA_UI_PREFERENCE_RANGE, .5f, 1.5f, .05f, NULL, 0},
    {"textScale", "Text scale", "1", QA_UI_PREFERENCE_RANGE, .75f, 2, .05f, NULL, 0},
    {"menuScale", "Menu scale", "1", QA_UI_PREFERENCE_RANGE, .75f, 1, .05f, NULL, 0},
    {"highContrast", "High contrast", "0", QA_UI_PREFERENCE_TOGGLE, 0, 0, 0, NULL, 0},
    {"reducedFlashes", "Reduced flashes", "0", QA_UI_PREFERENCE_TOGGLE, 0, 0, 0, NULL, 0},
    {"captions", "Captions", "1", QA_UI_PREFERENCE_TOGGLE, 0, 0, 0, NULL, 0},
    {"crosshair", "Crosshair", "1", QA_UI_PREFERENCE_TOGGLE, 0, 0, 0, NULL, 0},
    {"crosshairSize", "Crosshair size", "8", QA_UI_PREFERENCE_RANGE, 2, 32, 1, NULL, 0},
    {"typeface", "Typeface", "standard", QA_UI_PREFERENCE_CHOICE, 0, 0, 0, typefaces, 2},
    {"colorMode", "Color mode", "standard", QA_UI_PREFERENCE_CHOICE, 0, 0, 0, color_modes, 3},
    {"language", "Language", "english", QA_UI_PREFERENCE_LANGUAGE, 0, 0, 0, NULL, 0}
};
static bool fail(qa_error *error, const char *message)
{
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message); return false;
}
const qa_ui_preference_description *qa_ui_preference_describe(qa_ui_preference preference)
{
    return (unsigned)preference < QA_UI_PREF_COUNT ? &descriptions[preference] : NULL;
}
bool qa_ui_preference_name(uint32_t seat, qa_ui_preference preference, char out[64], qa_error *error)
{
    const qa_ui_preference_description *description = qa_ui_preference_describe(preference);
    if (seat >= QA_INPUT_LOCAL_SEATS || !description || !out)
        return fail(error, "UI preference requires a physical seat and known key");
    if (preference == QA_UI_PREF_LANGUAGE) return qa_ui_language_name(seat, out, error);
    snprintf(out, 64, "ui_seat%u_%s", seat + 1, description->key); return true;
}
static bool validate(void *context, const char *value, qa_error *error)
{
    const qa_ui_preference_description *description = context;
    if (!value) return fail(error, "Missing UI preference value");
    switch (description->kind) {
    case QA_UI_PREFERENCE_RANGE: {
        double number;
        if (!qa_parse_number((qa_bytes){(const uint8_t *)value, strlen(value)}, &number, error)) return false;
        if (!isfinite(number) || number < description->minimum || number > description->maximum) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s expects %g through %g",
                description->label, (double)description->minimum, (double)description->maximum); return false;
        }
        return true;
    }
    case QA_UI_PREFERENCE_TOGGLE:
        return !strcmp(value, "0") || !strcmp(value, "1") || fail(error, "Expected 0 or 1");
    case QA_UI_PREFERENCE_CHOICE:
        for (size_t i = 0; i < description->choice_count; ++i)
            if (!strcmp(value, description->choices[i])) return true;
        return fail(error, "Unknown UI preference choice");
    case QA_UI_PREFERENCE_LANGUAGE:
        if (!*value) return fail(error, "Expected a lowercase language name");
        for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
            if (*p < 'a' || *p > 'z') return fail(error, "Expected a lowercase language name");
        return true;
    }
    return fail(error, "Unknown UI preference kind");
}
bool qa_ui_preferences_register(qa_cvars *cvars, uint64_t owner, qa_error *error)
{
    if (!cvars) return fail(error, "UI preferences require their actual registry");
    for (uint32_t seat = 0; seat < QA_INPUT_LOCAL_SEATS; ++seat) {
        for (unsigned key = 0; key < QA_UI_PREF_COUNT; ++key) {
            const qa_ui_preference_description *description = &descriptions[key]; char name[64];
            if (!qa_ui_preference_name(seat, (qa_ui_preference)key, name, error) ||
                !qa_cvars_register(cvars, name, description->initial, QA_CVAR_ARCHIVE, owner, description->label, error) ||
                !qa_cvars_bind(cvars, name, &(qa_cvar_binding){.owner = owner,
                    .user = (void *)description, .validate = validate}, error)) return false;
        }
    }
    return true;
}
bool qa_ui_preference_set(qa_cvars *cvars, uint32_t seat, qa_ui_preference preference,
    const char *value, qa_error *error)
{
    char name[64];
    return cvars && qa_ui_preference_name(seat, preference, name, error) &&
        validate((void *)qa_ui_preference_describe(preference), value, error) &&
        qa_cvars_set_console(cvars, name, value, error);
}
static const qa_cvar_view *canonical(const qa_cvars_edit *edit,const char *name)
{
    bool folded=qa_cvars_dialect(qa_cvars_edit_registry(edit))==QA_CONSOLE_Q3;
    for (size_t i=0;i<qa_cvars_edit_count(edit);++i) {
        const qa_cvar_view *row=qa_cvars_edit_at(edit,i);
        if (!row) continue;
        const unsigned char *a=(const unsigned char *)row->name,*b=(const unsigned char *)name;
        while (*a && *b) {
            unsigned char left=*a++,right=*b++;
            if (folded && left>='A' && left<='Z') left+='a'-'A';
            if (folded && right>='A' && right<='Z') right+='a'-'A';
            if (left!=right) break;
            if (!*a && !*b) return row;
        }
    }
    return NULL;
}
static bool preferences_read(const qa_cvars *cvars, const qa_cvars_edit *edit,
    uint32_t seat, qa_ui_preferences *out, bool canonical_only,qa_error *error)
{
    if (!cvars || !out || seat >= QA_INPUT_LOCAL_SEATS) return fail(error, "UI preferences require their actual physical seat");
    const qa_cvar_view *values[QA_UI_PREF_COUNT];
    for (unsigned key = 0; key < QA_UI_PREF_COUNT; ++key) {
        char name[64];
        if (!qa_ui_preference_name(seat, (qa_ui_preference)key, name, error)) return false;
        values[key] = edit ? canonical_only?canonical(edit,name):qa_cvars_edit_find(edit, name) : qa_cvars_find(cvars, name);
        if (!values[key]) return fail(error, "UI preference is not registered on this candidate");
    }
    *out = (qa_ui_preferences){.hud_scale = values[QA_UI_PREF_HUD_SCALE]->number,
        .text_scale = values[QA_UI_PREF_TEXT_SCALE]->number, .menu_scale = values[QA_UI_PREF_MENU_SCALE]->number,
        .crosshair_size = values[QA_UI_PREF_CROSSHAIR_SIZE]->number,
        .high_contrast = values[QA_UI_PREF_HIGH_CONTRAST]->integer != 0,
        .reduced_flashes = values[QA_UI_PREF_REDUCED_FLASHES]->integer != 0,
        .captions = values[QA_UI_PREF_CAPTIONS]->integer != 0, .crosshair = values[QA_UI_PREF_CROSSHAIR]->integer != 0,
        .typeface = !strcmp(values[QA_UI_PREF_TYPEFACE]->value, "bold") ? QA_UI_TYPEFACE_BOLD : QA_UI_TYPEFACE_STANDARD,
        .color_mode = !strcmp(values[QA_UI_PREF_COLOR_MODE]->value, "blue-yellow") ? QA_UI_COLOR_BLUE_YELLOW :
            !strcmp(values[QA_UI_PREF_COLOR_MODE]->value, "monochrome") ? QA_UI_COLOR_MONOCHROME : QA_UI_COLOR_STANDARD,
        .language = values[QA_UI_PREF_LANGUAGE]->value};
    return true;
}
bool qa_ui_preferences_read(const qa_cvars *cvars, uint32_t seat, qa_ui_preferences *out, qa_error *error)
{ return preferences_read(cvars, NULL, seat, out, false,error); }
bool qa_ui_preferences_edit_read(const qa_cvars_edit *edit, uint32_t seat,
    qa_ui_preferences *out, qa_error *error)
{ return preferences_read(qa_cvars_edit_registry(edit), edit, seat, out, false,error); }
bool qa_ui_preferences_edit_ready_is(const qa_cvars_edit *edit,uint32_t seat,
    const qa_ui_preferences *expected)
{
    qa_ui_preferences actual;
    return expected && expected->language && qa_cvars_edit_ready_is(edit) &&
        preferences_read(qa_cvars_edit_registry(edit),edit,seat,&actual,true,NULL) &&
        actual.hud_scale==expected->hud_scale && actual.text_scale==expected->text_scale &&
        actual.menu_scale==expected->menu_scale && actual.crosshair_size==expected->crosshair_size &&
        actual.high_contrast==expected->high_contrast && actual.reduced_flashes==expected->reduced_flashes &&
        actual.captions==expected->captions && actual.crosshair==expected->crosshair &&
        actual.typeface==expected->typeface && actual.color_mode==expected->color_mode &&
        !strcmp(actual.language,expected->language);
}
