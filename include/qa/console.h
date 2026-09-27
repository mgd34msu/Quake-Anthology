#ifndef QA_CONSOLE_H
#define QA_CONSOLE_H

#include "qa/common.h"

typedef enum qa_console_dialect {
    QA_CONSOLE_Q1,
    QA_CONSOLE_QW,
    QA_CONSOLE_Q2,
    QA_CONSOLE_Q2_RERELEASE,
    QA_CONSOLE_Q3
} qa_console_dialect;

typedef enum qa_command_origin {
    QA_COMMAND_LOCAL,
    QA_COMMAND_SERVER,
    QA_COMMAND_SEAT,
    QA_COMMAND_REMOTE
} qa_command_origin;

/* Owner/client IDs include their lifetime generation. Zero denotes the engine
 * owner or absence of a client. Script origin retains the original caller. */
typedef struct qa_command_context {
    uint64_t session;
    uint64_t owner;
    uint64_t client;
    uint32_t seat;
    qa_console_dialect dialect;
    qa_command_origin origin;
    bool direct;
    bool console_text;
    const char *script;
} qa_command_context;

typedef struct qa_command_tokens {
    size_t count;
    char **values;
    char *args_text;
    char *storage;
} qa_command_tokens;

bool qa_command_tokenize(const char *text, qa_console_dialect dialect,
                          bool console_text, qa_command_tokens *out, qa_error *error);
void qa_command_tokens_free(qa_command_tokens *tokens);
/* First source separator, or length when absent. Quotes protect semicolons;
 * line feeds always split, and Q3 also splits at carriage returns. */
size_t qa_command_separator(const char *text, size_t length, qa_console_dialect dialect);
/* Original command-list filter accepts matching prefixes. */
bool qa_command_filter(const char *pattern, const char *name, bool case_sensitive);

/* Flag words are the original APIs. Select the word's meaning by dialect. */
enum qa_cvar_flags {
    QA_CVAR_ARCHIVE = 1, QA_CVAR_USERINFO = 2, QA_CVAR_SERVERINFO = 4,
    QA_CVAR_SYSTEMINFO = 8, QA_CVAR_INIT = 16, QA_CVAR_LATCH = 32,
    QA_CVAR_READONLY = 64, QA_CVAR_USER_CREATED = 128,
    QA_CVAR_TEMPORARY = 256, QA_CVAR_CHEAT = 512, QA_CVAR_NO_RESTART = 1024
};
enum qa_q2_cvar_flags {
    QA_Q2_CVAR_NOSET = 8, QA_Q2_CVAR_LATCH = 16, QA_Q2_CVAR_CHEAT = 32,
    QA_Q2_CVAR_PRIVATE = 64, QA_Q2_CVAR_READONLY = 128,
    QA_Q2_CVAR_MODIFIED = 256, QA_Q2_CVAR_CUSTOM = 512,
    QA_Q2_CVAR_WEAK = 1024, QA_Q2_CVAR_GAME = 2048,
    QA_Q2_CVAR_NO_ARCHIVE = 4096, QA_Q2_CVAR_FILES = 8192,
    QA_Q2_CVAR_REFRESH = 16384, QA_Q2_CVAR_SOUND = 32768
};

typedef struct qa_cvars qa_cvars;
typedef struct qa_cvar_view {
    const char *name;
    const char *value;
    const char *reset_value;
    const char *latched_value;
    const char *description;
    uint32_t flags;
    uint64_t modification_count;
    uint64_t owner;
    float number;
    int32_t integer;
    bool modified;
    bool console_created;
    size_t handle;
} qa_cvar_view;

typedef enum qa_cvar_effect_kind {
    QA_CVAR_EFFECT_USERINFO,
    QA_CVAR_EFFECT_SERVERINFO,
    QA_CVAR_EFFECT_BROADCAST,
    QA_CVAR_EFFECT_GAME_DIRECTORY
} qa_cvar_effect_kind;

typedef struct qa_cvar_options {
    qa_console_dialect dialect;
    void *user;
    void (*print)(void *user, const char *text);
    bool (*command_exists)(void *user, const char *name);
    bool (*cheats_allowed)(void *user);
    void (*effect)(void *user, qa_cvar_effect_kind kind, const qa_cvar_view *variable);
} qa_cvar_options;

typedef struct qa_cvar_binding {
    uint64_t owner;
    void *user;
    bool (*validate)(void *user, const char *value, qa_error *error);
    void (*changed)(void *user, const char *value);
} qa_cvar_binding;

qa_cvars *qa_cvars_create(const qa_cvar_options *options, qa_error *error);
void qa_cvars_destroy(qa_cvars *registry);
qa_console_dialect qa_cvars_dialect(const qa_cvars *registry);
/* Views and strings remain valid until that registry is next mutated.
 * Output/effect callbacks may inspect state but must not mutate or destroy the
 * registry during notification. Host work can be queued through the console. */
const qa_cvar_view *qa_cvars_find(const qa_cvars *registry, const char *name);
const qa_cvar_view *qa_cvars_at(const qa_cvars *registry, size_t ordinal);
const qa_cvar_view *qa_cvars_handle(const qa_cvars *registry, size_t handle);
size_t qa_cvars_count(const qa_cvars *registry);
size_t qa_cvars_handle_count(const qa_cvars *registry);
bool qa_cvars_register(qa_cvars *registry, const char *name, const char *default_value,
                        uint32_t flags, uint64_t owner, const char *description,
                        qa_error *error);
/* Bindings validate before publication and observe committed values. They use
 * the same notification lifetime rule as registry output/effect callbacks. */
bool qa_cvars_bind(qa_cvars *registry, const char *name, const qa_cvar_binding *binding,
                    qa_error *error);
void qa_cvars_unbind(qa_cvars *registry, const char *name, uint64_t owner);
bool qa_cvars_set(qa_cvars *registry, const char *name, const char *value,
                   bool force, qa_error *error);
bool qa_cvars_set_console(qa_cvars *registry, const char *name, const char *value,
                           qa_error *error);
bool qa_cvars_set_number(qa_cvars *registry, const char *name, float value, qa_error *error);
bool qa_cvars_set_flags(qa_cvars *registry, const char *name, const char *value,
                         uint32_t flag, qa_error *error);
bool qa_cvars_full_set(qa_cvars *registry, const char *name, const char *value,
                        uint32_t flags, qa_error *error);
bool qa_cvars_stage(qa_cvars *registry, const char *name, const char *value, qa_error *error);
bool qa_cvars_apply_latched(qa_cvars *registry, const char *name, qa_error *error);
bool qa_cvars_reset(qa_cvars *registry, const char *name, bool force, qa_error *error);
bool qa_cvars_restart(qa_cvars *registry, qa_error *error);
bool qa_cvars_set_cheats(qa_cvars *registry, bool allowed, qa_error *error);
void qa_cvars_set_server_active(qa_cvars *registry, bool active);
void qa_cvars_set_high_characters(qa_cvars *registry, bool enabled);
void qa_cvars_remove_owner(qa_cvars *registry, uint64_t owner);
uint32_t qa_cvars_take_modified_flags(qa_cvars *registry);
void qa_cvars_clear_modified(qa_cvars *registry, const char *name);
bool qa_cvars_take_userinfo_modified(qa_cvars *registry);
/* Outputs are owned NUL-terminated text; size excludes the terminator. */
bool qa_cvars_info(const qa_cvars *registry, uint32_t flags, size_t maximum_length,
                    qa_buffer *out, qa_error *error);
/* Source .cfg commands cannot encode literal quotes or line breaks inside a
 * value. Such values return FORMAT; structured settings retain them separately. */
bool qa_cvars_config(const qa_cvars *registry, qa_buffer *out, qa_error *error);
/* NULL means excluded by this source's archive rules. Views come from this
 * registry; returned values remain borrowed until its next mutation. */
const char *qa_cvars_archive_value(const qa_cvars *, const qa_cvar_view *);

typedef struct qa_console qa_console;
typedef struct qa_command_invocation {
    qa_console *console;
    qa_command_context context;
    size_t argc;
    const char *const *argv;
    const char *args_text;
    const char *raw;
} qa_command_invocation;

typedef bool (*qa_command_handler)(void *user, const qa_command_invocation *command,
                                    qa_error *error);
typedef enum qa_command_result {
    QA_COMMAND_UNHANDLED,
    QA_COMMAND_HANDLED,
    QA_COMMAND_FAILED
} qa_command_result;
typedef qa_command_result (*qa_command_fallback)(void *user,
                                                  const qa_command_invocation *command,
                                                  qa_error *error);

typedef struct qa_console_options {
    qa_command_context context;
    qa_cvars *cvars;
    void *user;
    /* Output and routing callbacks inspect state only. Command handlers and
     * fallback callbacks may queue work, dispatch nested commands, or retire an
     * owner. Destroy the console only after its active invocation has returned. */
    void (*print)(void *user, const qa_command_context *context, const char *text);
    qa_cvars *(*cvar_owner)(void *user, const qa_command_context *context, const char *name);
    qa_cvars *(*visible_cvars)(void *user, const qa_command_context *context, size_t index);
    /* Supply immutable script bytes through the content service. The release
     * callback, when present, runs once after the console has copied them. */
    bool (*read_script)(void *user, const qa_command_context *context, const char *path,
                        qa_bytes *out, void **lease, qa_error *error);
    void (*release_script)(void *user, void *lease);
    void (*script_complete)(void *user, const qa_command_context *context,
                            const char *path, bool success);
    bool (*allow_command)(void *user, const qa_command_invocation *command);
    qa_command_fallback source_command;
    qa_command_fallback client_game;
    qa_command_fallback server_game;
    qa_command_fallback ui;
    qa_command_fallback forward;
    const char *startup_commands;
    size_t maximum_buffer;
    size_t maximum_command;
    bool disable_builtins;
} qa_console_options;

typedef struct qa_console_entry {
    const char *name;
    const char *description;
    const char *alias_text;
    uint64_t owner;
    bool engine_command;
} qa_console_entry;

qa_console *qa_console_create(const qa_console_options *options, qa_error *error);
void qa_console_destroy(qa_console *console);
/* Queued chunks retain their original dialect and origin. Change the default
 * profile only between command invocations. */
bool qa_console_set_profile(qa_console *console, qa_console_dialect dialect,
                              qa_cvars *cvars, qa_error *error);
bool qa_console_register(qa_console *console, const char *name, const char *description,
                           uint64_t owner, bool engine_command, qa_command_handler handler,
                           void *user, qa_error *error);
bool qa_console_unregister(qa_console *console, const char *name, uint64_t owner);
const qa_console_entry *qa_console_entry_at(const qa_console *console, size_t ordinal);
const qa_console_entry *qa_console_find(const qa_console *console,
                                         const qa_command_context *context,
                                         const char *name);
bool qa_console_alias(qa_console *console, const qa_command_context *context,
                        const char *name, const char *text, qa_error *error);
const qa_console_entry *qa_console_alias_at(const qa_console *console, uint64_t owner,
                                            size_t ordinal);
bool qa_console_append(qa_console *console, const qa_command_context *context,
                         const char *text, qa_error *error);
bool qa_console_insert(qa_console *console, const qa_command_context *context,
                         const char *text, qa_error *error);
bool qa_console_execute_now(qa_console *console, const qa_command_context *context,
                              const char *text, qa_error *error);
/* One frame of queued work, respecting wait. Zero budget is unlimited. */
bool qa_console_drain(qa_console *console, size_t budget, size_t *executed, qa_error *error);
bool qa_console_defer(qa_console *console, qa_error *error);
bool qa_console_resume(qa_console *console, qa_error *error);
bool qa_console_pending(const qa_console *console);
bool qa_console_remove_owner(qa_console *console, uint64_t owner, qa_error *error);
bool qa_console_remove_client(qa_console *console, uint64_t client, qa_error *error);

#endif
