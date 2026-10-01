#ifndef QA_UI_PREFERENCES_H
#define QA_UI_PREFERENCES_H
#include "qa/ui.h"
#include "qa/input_platform.h"

struct qa_cvars_edit;

typedef enum qa_ui_typeface { QA_UI_TYPEFACE_STANDARD, QA_UI_TYPEFACE_BOLD } qa_ui_typeface;
typedef struct qa_ui_preferences {
    float hud_scale, text_scale, menu_scale, crosshair_size;
    bool high_contrast, reduced_flashes, captions, crosshair;
    qa_ui_typeface typeface;
    qa_ui_color_mode color_mode;
    const char *language; /* Borrowed until the cvar registry is mutated. */
} qa_ui_preferences;
typedef enum qa_ui_preference {
    QA_UI_PREF_HUD_SCALE, QA_UI_PREF_TEXT_SCALE, QA_UI_PREF_MENU_SCALE,
    QA_UI_PREF_HIGH_CONTRAST, QA_UI_PREF_REDUCED_FLASHES, QA_UI_PREF_CAPTIONS,
    QA_UI_PREF_CROSSHAIR, QA_UI_PREF_CROSSHAIR_SIZE, QA_UI_PREF_TYPEFACE,
    QA_UI_PREF_COLOR_MODE, QA_UI_PREF_LANGUAGE, QA_UI_PREF_COUNT
} qa_ui_preference;
typedef enum qa_ui_preference_kind {
    QA_UI_PREFERENCE_RANGE, QA_UI_PREFERENCE_TOGGLE, QA_UI_PREFERENCE_CHOICE,
    QA_UI_PREFERENCE_LANGUAGE
} qa_ui_preference_kind;
typedef struct qa_ui_preference_description {
    const char *key, *label, *initial;
    qa_ui_preference_kind kind;
    float minimum, maximum, step;
    const char *const *choices;
    size_t choice_count;
} qa_ui_preference_description;
const qa_ui_preference_description *qa_ui_preference_describe(qa_ui_preference);
/* Register before archive application and before private cvar restoration so
 * validation binds to the candidate's actual registry. All four physical seat
 * namespaces remain available independently of the current local roster. */
bool qa_ui_preferences_register(qa_cvars *, uint64_t owner, qa_error *);
bool qa_ui_preference_name(uint32_t seat, qa_ui_preference, char out[64], qa_error *);
bool qa_ui_preferences_read(const qa_cvars *, uint32_t seat, qa_ui_preferences *, qa_error *);
/* Reads the same actual proposed scalar records from a retained canonical
 * publication ticket. Language borrows that ticket until publish or abort. */
bool qa_ui_preferences_edit_read(const struct qa_cvars_edit *, uint32_t seat,
    qa_ui_preferences *, qa_error *);
bool qa_ui_preference_set(qa_cvars *, uint32_t seat, qa_ui_preference, const char *, qa_error *);
#endif
