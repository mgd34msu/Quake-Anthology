#ifndef QA_SETTINGS_SERVER_PROFILE_H
#define QA_SETTINGS_SERVER_PROFILE_H

#include "qa/settings.h"
#include "qa/modes.h"

typedef enum qa_server_setting_source {
    QA_SERVER_SOURCE_NONE,
    QA_SERVER_SOURCE_Q1, QA_SERVER_SOURCE_Q1_ROGUE, QA_SERVER_SOURCE_Q1_CTF,
    QA_SERVER_SOURCE_Q2, QA_SERVER_SOURCE_Q2_RERELEASE,
    QA_SERVER_SOURCE_Q3, QA_SERVER_SOURCE_TEAM_ARENA
} qa_server_setting_source;
typedef struct qa_server_setting_selection {
    qa_server_setting_source source;
    qa_mode_source match;
    bool native_combat;
} qa_server_setting_selection;
typedef enum qa_server_setting_kind {
    QA_SERVER_SETTING_TOGGLE, QA_SERVER_SETTING_NUMBER,
    QA_SERVER_SETTING_CHOICE, QA_SERVER_SETTING_TEXT
} qa_server_setting_kind;
typedef enum qa_server_setting_apply_at {
    QA_SERVER_SETTING_LIVE, QA_SERVER_SETTING_NEXT_MATCH,
    QA_SERVER_SETTING_NEXT_MAP, QA_SERVER_SETTING_RESTART
} qa_server_setting_apply_at;
typedef enum qa_server_setting_target_kind {
    QA_SERVER_SETTING_VALUE, QA_SERVER_SETTING_BIT
} qa_server_setting_target_kind;
typedef struct qa_server_setting_target {
    qa_server_setting_target_kind kind;
    const char *name;
    uint32_t mask;
    bool inverted;
} qa_server_setting_target;
typedef struct qa_server_setting_choice { const char *id, *label; } qa_server_setting_choice;
typedef struct qa_server_setting_definition {
    const char *id, *label, *default_value;
    qa_server_setting_target target;
    qa_server_setting_apply_at apply_at;
    qa_server_setting_kind kind;
    union {
        struct { double minimum, maximum, step; bool integer; } number;
        struct { const qa_server_setting_choice *items; size_t count; } choices;
        size_t maximum_length; /* UTF-16 code units, matching the profile format. */
    } control;
} qa_server_setting_definition;
#define QA_SERVER_SETTING_MAX 48u
typedef struct qa_server_setting_catalog {
    const qa_server_setting_definition *items[QA_SERVER_SETTING_MAX];
    size_t count;
} qa_server_setting_catalog;
/* Definitions are immutable. Selection derives from the independently chosen
 * WORLD ENTITIES, WORLD MODE and default-player COMBAT owners. */
bool qa_server_setting_definitions(qa_server_setting_selection, qa_server_setting_catalog *, qa_error *);
bool qa_server_setting_parse(const qa_server_setting_definition *, const char *, qa_buffer *, qa_error *);

typedef struct qa_server_profile qa_server_profile;
typedef struct qa_server_profile_override { const char *id, *value; } qa_server_profile_override;
bool qa_server_profile_parse(qa_bytes, qa_server_setting_selection, qa_server_profile **, qa_error *);
bool qa_server_profile_encode(const qa_server_profile *, qa_buffer *, qa_error *);
void qa_server_profile_destroy(qa_server_profile *);
const qa_server_profile_override *qa_server_profile_overrides(const qa_server_profile *, size_t *);
bool qa_settings_load_server_profile(qa_settings_store, const char *, qa_server_setting_selection,
    qa_server_profile **, bool *found, qa_error *);

typedef struct qa_server_profile_owner {
    void *context;
    /* Borrow the actual owner's latched value when present, otherwise value.
     * Missing targets fail; reading cannot invent console variables. */
    bool (*read_desired)(void *, const char *name, const char **, qa_error *);
    /* Startup writes force the initial value through this qualified Source.
     * Apply timing is metadata for later edits; every profile override applies
     * before the fresh Source resolves its initial rules. */
    bool (*write_initial)(void *, const char *name, const char *value, qa_error *);
} qa_server_profile_owner;
/* Revalidates every selected definition/value and qualifies all targets before
 * the first write. Bit writes merge sequentially into the actual desired value.
 * The enclosing candidate owns rollback if a Source write itself fails. */
bool qa_server_profile_apply_startup(const qa_server_profile *, qa_server_setting_selection,
    const qa_server_profile_owner *, qa_error *);

#endif
