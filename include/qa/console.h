#ifndef QA_CONSOLE_H
#define QA_CONSOLE_H

#include "qa/common.h"
#include "qa/actors.h"

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
    /* Application owners stamp deferred work with the exact world/provider
     * publication and optional canonical actor. Generic consoles leave zero. */
    uint64_t registry, generation;
    qa_actor_id actor;
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
struct qa_cvars_edit;
struct qa_cvars_edit_command;
typedef struct qa_console_documentation {
    const char *usage;
    const char *const *examples;
    size_t example_count;
    const char *const *allowed_values;
    size_t allowed_count;
    bool has_allowed_values;
} qa_console_documentation;
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
    const qa_console_documentation *documentation;
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
/* Validates a retained physical name under its registry's dialect. Q3
 * mutation APIs remap forbidden names to BADNAME before admission. */
bool qa_cvars_name_valid(qa_console_dialect dialect, const char *name);
/* Views and strings remain valid until that registry is next mutated.
 * Output/effect callbacks may inspect state but must not mutate or destroy the
 * registry during notification. Host work can be queued through the console. */
const qa_cvar_view *qa_cvars_find(const qa_cvars *registry, const char *name);
const qa_cvar_view *qa_cvars_at(const qa_cvars *registry, size_t ordinal);
/* Iterates physical rows in ordinal order; NULL starts the registry. Previous
 * must be an exact borrowed live row from this registry, valid until mutation.
 * Alias projections and rows from another registry are not physical cursors. */
const qa_cvar_view *qa_cvars_next(const qa_cvars *registry, const qa_cvar_view *previous);
const qa_cvar_view *qa_cvars_handle(const qa_cvars *registry, size_t handle);
size_t qa_cvars_count(const qa_cvars *registry);
size_t qa_cvars_handle_count(const qa_cvars *registry);
bool qa_cvars_register(qa_cvars *registry, const char *name, const char *default_value,
                        uint32_t flags, uint64_t owner, const char *description,
                        qa_error *error);
/* Adds declaration flags to the existing physical record, preserving its
 * value, reset, latch, owner, handle and value modification count. */
bool qa_cvars_add_flags(qa_cvars *,const char *name,uint32_t flags,qa_error *);
bool qa_cvars_document(qa_cvars *, const char *name, uint64_t owner,
                       const qa_console_documentation *, qa_error *);
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
/* Reads policy configuration without evaluating a callback or sv_cheats. */
bool qa_cvars_cheats_policy(const qa_cvars *, bool *callback_backed, bool *fallback_allowed);
void qa_cvars_set_server_active(qa_cvars *registry, bool active);
void qa_cvars_set_high_characters(qa_cvars *registry, bool enabled);
void qa_cvars_remove_owner(qa_cvars *registry, uint64_t owner);
/* Promote the scalar to the registry lifetime. Binding ownership is unchanged;
 * remove_owner still detaches a retiring owner's callbacks. Idempotent. */
bool qa_cvars_retain_shared(qa_cvars *, const char *name, qa_error *);
uint32_t qa_cvars_take_modified_flags(qa_cvars *registry);
void qa_cvars_mark_modified_flags(qa_cvars *registry, uint32_t flags);
void qa_cvars_clear_modified(qa_cvars *registry, const char *name);
bool qa_cvars_take_userinfo_modified(qa_cvars *registry);
typedef struct qa_cvar_registry_state {
    size_t next_handle;
    uint32_t modified_flags;
    bool userinfo_modified, server_active, high_characters, cheats;
} qa_cvar_registry_state;
typedef struct qa_cvar_record_state {
    const char *name;
    size_t handle;
    uint64_t owner, modification_count;
    bool modified, console_created;
} qa_cvar_record_state;
/* Metadata complements typed value/reset/latch/flag restoration. Names borrow
 * the registry; capture requires room for qa_cvars_count records. Restore
 * validates the complete set before changing metadata, calls no notifications,
 * and rejects bound variables whose owner/handle would change. */
bool qa_cvars_capture_metadata(const qa_cvars *, qa_cvar_registry_state *,
                                qa_cvar_record_state *, size_t, qa_error *);
bool qa_cvars_restore_metadata(qa_cvars *, const qa_cvar_registry_state *,
                                const qa_cvar_record_state *, size_t, qa_error *);
/* Outputs are owned NUL-terminated text; size excludes the terminator. */
bool qa_cvars_info(const qa_cvars *registry, uint32_t flags, size_t maximum_length,
                    qa_buffer *out, qa_error *error);
/* Source .cfg commands cannot encode literal quotes or line breaks inside a
 * value. Such values return FORMAT; structured settings retain them separately. */
bool qa_cvars_config(const qa_cvars *registry, qa_buffer *out, qa_error *error);
/* The filter inspects borrowed views without mutating either registry. Archive
 * eligibility, source quoting and latched values retain their ordinary rules. */
typedef bool (*qa_cvar_config_filter)(void *, const qa_cvars *, const qa_cvar_view *);
bool qa_cvars_config_filtered(const qa_cvars *, qa_cvar_config_filter, void *,
                               qa_buffer *, qa_error *);
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
    /* Resolves a prepared scalar view of this exact canonical registry for
     * the actual command context. A successful NULL result uses live values;
     * rejection must not fall back to an unrelated or live owner. */
    bool (*cvar_edit)(void *user, const qa_command_context *context, qa_cvars *registry,
                      struct qa_cvars_edit **out, qa_error *error);
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
    bool (*capture_context)(void *user, const qa_command_context *, qa_command_context *, qa_error *);
    bool (*context_active)(void *user, const qa_command_context *);
    /* Observe each admitted dispatched invocation after its actual handler,
     * while its normalized tokens and innermost frame are still retained.
     * Inspect or copy state only; do not mutate the console or its registries.
     * Rejection fails the command; a prior dispatch failure stays first. */
    bool (*post_dispatch)(void *user,const qa_command_invocation *,bool success,qa_error *);
} qa_console_options;

typedef struct qa_console_entry {
    const char *name;
    const char *description;
    const char *alias_text;
    uint64_t owner;
    bool engine_command;
    const qa_console_documentation *documentation;
} qa_console_entry;

qa_console *qa_console_create(const qa_console_options *options, qa_error *error);
/* Pure teardown qualification; NULL is ready. */
bool qa_console_destroy_ready(const qa_console *console);
void qa_console_destroy(qa_console *console);
/* Queued chunks retain their original dialect and origin. Change the default
 * profile only between command invocations. */
bool qa_console_set_profile(qa_console *console, qa_console_dialect dialect,
                              qa_cvars *cvars, qa_error *error);
/* Borrowed physical default registry; does not invoke namespace routing. */
qa_cvars *qa_console_cvars(const qa_console *console);
bool qa_console_register(qa_console *console, const char *name, const char *description,
                           uint64_t owner, bool engine_command, qa_command_handler handler,
                           void *user, qa_error *error);
/* Dispatch visibility and callback lifetime may differ for shared engine
 * commands. Retirement clears the handler while other contributions survive. */
bool qa_console_register_owned(qa_console *, const char *name, const char *description,
                                 uint64_t dispatch_owner, uint64_t lifetime_owner,
                                 bool engine_command, qa_command_handler, void *, qa_error *);
/* Reads an installed ordinary handler's lifetime owner by exact registration
 * name and dispatch owner. The output remains unchanged when absent. */
bool qa_console_registration_owner(const qa_console *, const char *exact_name,
                                   uint64_t dispatch_owner, uint64_t *out);
/* Pure exact ordinary registration receipt, including entries without handlers.
 * Outputs remain unchanged when that registration is absent. */
bool qa_console_registration_read(const qa_console *, const char *exact_name,
    uint64_t dispatch_owner, uint64_t *lifetime_owner, qa_command_handler *, void **user);
/* A shared dispatch entry retains independent role lifetime contributions.
 * Repeated contributions are idempotent. Removing the dispatch owner retires
 * the entry; removing a lifetime owner retires only its contributions. */
bool qa_console_contribute(qa_console *, const char *name, uint64_t dispatch_owner,
                            uint64_t lifetime_owner, qa_error *);
bool qa_console_uncontribute(qa_console *, const char *name, uint64_t dispatch_owner,
                              uint64_t lifetime_owner);
bool qa_console_unregister(qa_console *console, const char *name, uint64_t owner);
/* Documentation is copied; NULL removes it. Registration owns its lifetime. */
bool qa_console_document(qa_console *, const char *name, uint64_t owner,
                         const qa_console_documentation *, qa_error *);
qa_cvars *qa_console_visible_cvars(qa_console *, const qa_command_context *, size_t ordinal);
bool qa_console_limits(qa_console *, const qa_command_context *,
                        size_t *maximum_command, size_t *maximum_buffer, qa_error *);
qa_cvars *qa_console_cvar_owner(qa_console *, const qa_command_context *, const char *name);
/* Structured routed reads/writes use the same qualified scalar view as
 * builtins and macros. Missing reads succeed with NULL; writes require an
 * actual registry. Contexts are already captured by the owning caller. */
bool qa_console_cvar_read(qa_console *, const qa_command_context *, const char *,
                          const qa_cvar_view **out, qa_error *);
/* Captures the real constructor's current source context without dispatch.
 * Script text is borrowed from the supplied context. */
bool qa_console_cvar_context(qa_console *,const qa_command_context *,qa_command_context *,qa_error *);
typedef bool (*qa_console_cvar_entered_fn)(void *, const qa_console *,
    const qa_command_context *, qa_error *);
typedef bool (*qa_console_cvar_operation_fn)(void *, const qa_command_context *, qa_error *);
/* Execute only a structured cvar operation. Ordinary current capture is used
 * first. An exact entered host/physical tuple qualifier may instead admit its
 * unchanged constructor context for this lexical operation. That context is
 * not a captured command and grants no queue or handler dispatch authority.
 * The pure qualifier is rechecked for each use and at return; borrowed parents remain alive
 * until the operation returns. Nested loans restore the enclosing loan. */
bool qa_console_cvar_enter(qa_console *, const qa_command_context *,
    qa_console_cvar_entered_fn, void *qualifier_user,
    qa_console_cvar_operation_fn, void *operation_user, qa_error *);
/* True only for the exact context inside the retained entered cvar operation.
 * Routing callbacks may use this proof separately from ordinary command
 * admission. It never admits scripts, aliases or command handlers. */
bool qa_console_cvar_entered(const qa_console *, const qa_command_context *);
/* Returned native handlers may be installed inside the exact entered cvar
 * scope for this console's constructor. Invocation and output remain idle. */
bool qa_console_cvar_returned(const qa_console *);
/* Resolves one name's actual owner and optional canonical prepared view.
 * Both pointers are borrowed; a rejected view never falls back to live state. */
bool qa_console_cvar_access(qa_console *,const qa_command_context *,const char *,
    qa_cvars **,struct qa_cvars_edit **,qa_error *);
/* Reads one visible scalar/alias snapshot through the same admitted candidate
 * view. Registry must be an actual visible owner for this captured context.
 * Missing ordinal succeeds with NULL; a refused view never falls back live. */
bool qa_console_cvar_snapshot_at(qa_console *, const qa_command_context *, qa_cvars *,
                                 size_t ordinal, const qa_cvar_view **out, qa_error *);
/* Reads an actual VM handle through that same admitted live/prepared owner.
 * Handles outside its allocated extent fail; cleared in-range slots return NULL. */
bool qa_console_cvar_handle(qa_console *,const qa_command_context *,qa_cvars *,
                            size_t handle,const qa_cvar_view **out,qa_error *);
bool qa_console_cvar_apply(qa_console *, const qa_command_context *,
                           const struct qa_cvars_edit_command *, qa_error *);
/* Actual startup forced publication, empty Q3 registration retaining its
 * physical owner, then USER_CREATED flag promotion. */
bool qa_console_cvar_startup_set(qa_console *, const qa_command_context *,
                                 const char *name, const char *value, qa_error *);
const qa_console_entry *qa_console_entry_at(const qa_console *console, size_t ordinal);
const qa_console_entry *qa_console_context_entry_at(const qa_console *, const qa_command_context *, size_t ordinal);
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
/* Expand through the actual Source console/cvar context without dispatching a
 * command. Owned output includes NUL; a lexical discard leaves it empty. */
bool qa_console_expand_command(qa_console *, const qa_command_context *,
    const char *, qa_buffer *empty_output, qa_error *);
/* Output redirection covers this synchronous invocation and nested commands.
 * Deferred commands retain their ordinary console output owner. */
bool qa_console_execute_capture(qa_console *, const qa_command_context *, const char *,
    void (*print)(void *, const qa_command_context *, const char *), void *, qa_error *);
void qa_console_emit(qa_console *, const qa_command_context *, const char *);
bool qa_console_idle(const qa_console *);
/* Pure exact innermost invocation identity, including post-dispatch receipt. */
bool qa_console_invocation_current(const qa_console *,const qa_command_invocation *);
/* Borrow the original wire text of an entered invocation. Explicit cmd uses
 * its untouched argument tail; each Source retains its own admission policy. */
bool qa_console_forward_text(const qa_command_invocation *,const char **text,
    bool *explicit_command,qa_error *);
bool qa_console_output_redirected(const qa_console *);
/* One frame of queued work, respecting wait. Zero budget is unlimited. */
bool qa_console_drain(qa_console *console, size_t budget, size_t *executed, qa_error *error);
/* Reports an actual wait boundary in the last admitted drain. This observation
 * is transient; queued wait state is retained by the console checkpoint. */
bool qa_console_drain_yielded(const qa_console *console);
bool qa_console_defer(qa_console *console, qa_error *error);
bool qa_console_resume(qa_console *console, qa_error *error);
bool qa_console_pending(const qa_console *console);
bool qa_console_remove_owner(qa_console *console, uint64_t owner, qa_error *error);
bool qa_console_remove_client(qa_console *console, uint64_t client, qa_error *error);

#endif
