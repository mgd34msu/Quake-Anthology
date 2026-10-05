#ifndef QA_UI_LIBRARY_H
#define QA_UI_LIBRARY_H
#include "qa/ui.h"
#include "qa/arena_progress_catalog.h"

enum {
    QA_UI_LIBRARY_CAMPAIGN = 230, QA_UI_LIBRARY_DIFFICULTY,
    QA_UI_LIBRARY_CUSTOM, QA_UI_LIBRARY_CATEGORY, QA_UI_LIBRARY_CHOICES,
    QA_UI_LIBRARY_ROSTER, QA_UI_LIBRARY_ARENAS = 237
};
typedef enum qa_ui_library_field {
    QA_UI_LIBRARY_PRODUCT, QA_UI_LIBRARY_MAP_PRODUCT, QA_UI_LIBRARY_MAP,
    QA_UI_LIBRARY_MOVEMENT, QA_UI_LIBRARY_CHARACTER, QA_UI_LIBRARY_MODEL,
    QA_UI_LIBRARY_SEATS, QA_UI_LIBRARY_WEAPONS, QA_UI_LIBRARY_ENEMIES,
    QA_UI_LIBRARY_SKILL, QA_UI_LIBRARY_MODE, QA_UI_LIBRARY_RULES,
    QA_UI_LIBRARY_GRAPPLE, QA_UI_LIBRARY_GRAPPLE_STYLE, QA_UI_LIBRARY_GRENADES,
    QA_UI_LIBRARY_MONSTER_SOURCE, QA_UI_LIBRARY_MONSTER_CLASS,
    QA_UI_LIBRARY_TEAM_PLAYER, QA_UI_LIBRARY_TEAM_OPPONENT,
    QA_UI_LIBRARY_ENVIRONMENT, QA_UI_LIBRARY_DOPPLER
} qa_ui_library_field;
typedef struct qa_ui_library_choice {
    const char *id, *label, *unavailable;
} qa_ui_library_choice;
typedef struct qa_ui_library_roster_row {
    const char *classname, *label, *effective_label;
} qa_ui_library_roster_row;
/* Metadata callbacks borrow the same draft; select edits that owner directly.
 * Returned rows remain borrowed until the next metadata callback. */
typedef struct qa_ui_library_services {
    void *context;
    qa_ui_id hosting_menu, browser_menu, lobby_menu, mods_menu;
    const char *(*hosting_label)(void *);
    bool (*play)(void *, qa_ui_library *, qa_launch_draft *, bool authored,
        const char *arena_map, int32_t bot_skill, qa_error *);
    bool (*choices)(void *, qa_ui_library *, qa_ui_library_field, const char *classname,
        const qa_ui_library_choice **, size_t *, const char **selected, qa_error *);
    bool (*select)(void *, qa_ui_library *, qa_ui_library_field, const char *classname,
        const char *choice, qa_error *);
    bool (*roster)(void *, qa_ui_library *, const qa_ui_library_roster_row **,
        size_t *, const char **source_label, qa_error *);
    bool (*weapon_bindings)(void *, qa_ui_library *, const qa_input_weapon_binding **, size_t *, qa_error *);
    bool (*prepare_arenas)(void *, qa_ui_library *, qa_error *);
    bool (*arenas)(void *, qa_ui_library *, const qa_base_arena_catalog **,
        const qa_arena_progress **, qa_error *);
} qa_ui_library_services;
/* Borrowed selection views expire on an edit or refresh. No publication runs. */
qa_launch_draft *qa_ui_library_draft(qa_ui_library *);
const qa_launch_choices *qa_ui_library_choices(const qa_ui_library *);
const qa_catalog *qa_ui_library_catalog(const qa_ui_library *);
bool qa_ui_library_bind_services(qa_ui_library *, const qa_ui_library_services *, qa_error *);
bool qa_ui_library_weapon_bindings(qa_ui_library *, const qa_input_weapon_binding **, size_t *, qa_error *);
bool qa_ui_library_select_preset(qa_ui_library *, const char *product_key, const char *optional_start, qa_error *);
bool qa_ui_library_apply(qa_ui_library *, qa_error *);
bool qa_ui_library_input(qa_ui_library *, const qa_input_event *, bool *handled, qa_error *);
bool qa_ui_library_open_arenas(qa_ui_library *, qa_error *);
bool qa_ui_library_launch_failed(qa_ui_library *, const char *message, qa_error *);
bool qa_ui_library_selection_choices(qa_ui_library *, qa_ui_library_field,
    const qa_ui_library_choice **, size_t *, const char **selected, qa_error *);
bool qa_ui_library_select(qa_ui_library *, qa_ui_library_field, const char *choice, qa_error *);
qa_mode_kind qa_ui_library_mode_preference(const qa_ui_library *);
bool qa_ui_library_mode_preference_set(qa_ui_library *, qa_mode_kind, qa_error *);
bool qa_ui_library_select_mode(qa_ui_library *, qa_mode_kind, qa_error *);
#endif
