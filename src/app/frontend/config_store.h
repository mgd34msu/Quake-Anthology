#ifndef QA_FRONTEND_CONFIG_STORE_H
#define QA_FRONTEND_CONFIG_STORE_H
#include "internal.h"
#include "config_scripts.h"
#include "keys.h"
#include "client_registry.h"
#include "view_settings.h"
#include "qa/application_startup_prepare.h"
#include "qa/application_engine_shutdown.h"
#include "qa/q3_host.h"

typedef struct frontend_config_store frontend_config_store;
typedef struct frontend_config_source frontend_config_source;
typedef struct frontend_remote_config frontend_remote_config;
typedef struct frontend_authored_bindings frontend_authored_bindings;
typedef struct frontend_shared_settings frontend_shared_settings;
typedef struct frontend_shared_storage frontend_shared_storage;
typedef bool (*frontend_config_host_entry_read)(void *,qa_application *,
    const qa_application_startup_source *,const qa_q3_host **,qa_error *);
typedef struct frontend_config_host_cvars {
    const frontend_config_store *manager;
    qa_application *application;
    qa_application_startup_source source,parent_game;
    bool has_parent;
    void *entry_context;
    frontend_config_host_entry_read entry_read;
} frontend_config_host_cvars;

/* The manager retains actual source ConfigStores and isolated logical input
 * during preparation. Its hooks and context outlive the application. */
frontend_config_store *frontend_config_store_create(qa_frontend *,qa_error *);
bool frontend_config_store_destroy(frontend_config_store *,qa_error *);
bool frontend_config_store_park_server(frontend_config_store *,const qa_application_startup_source *,qa_error *);
bool frontend_config_store_parked_finish(frontend_config_store *,qa_error *);
bool frontend_config_store_parked_release(frontend_config_store *,qa_error *);
bool frontend_config_store_parked_current(const frontend_config_store *,const qa_application_startup_source *);
bool frontend_config_store_parked_recipient(const frontend_config_store *,const qa_application_startup_source *,uint32_t *);
bool frontend_config_store_retired_ready(const frontend_config_store *,qa_error *);
bool frontend_config_store_restore_abort_unbound(frontend_config_store *,qa_application *,qa_error *);
const qa_application_startup_hooks *frontend_config_store_hooks(frontend_config_store *);
/* Rebuild local dictionaries and publish actual Source rows after GAME state
 * restores; user preferences come from the normal selected ConfigStore. */
bool frontend_config_store_rebuild_finish(frontend_config_store *,qa_error *);
frontend_config_source *frontend_config_store_source(const frontend_config_store *,const qa_console *);
bool frontend_config_store_commands_restored(frontend_config_store *,qa_application *,qa_console *,qa_error *);
frontend_remote_config *frontend_config_store_client(const frontend_config_store *,const qa_console *);
/* Qualifies the actual retained pending tuple without executing commands or
 * borrowing a published owner as an isolated configuration source. */
bool frontend_config_store_source_pending(const frontend_config_store *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *);
/* A real linked source joins the candidate's one canonical ENGINE ticket
 * before its startup variables or configuration script execute. */
bool frontend_config_store_shared_begin(frontend_config_store *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,qa_error *);
bool frontend_config_store_shared_pending(const frontend_config_store *);
frontend_shared_settings *frontend_config_store_shared(const frontend_config_store *,const qa_application *,
    const qa_launch_snapshot *);
/* Before startup abort, finish only already retained release programmes.
 * A genuine WAIT keeps the same candidate association and complete=false. */
bool frontend_config_store_shared_cancel_advance(frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,bool *complete,qa_error *);
/* Exact synchronous command admission for the separate retained image programme. */
bool frontend_config_store_images_command_current(const frontend_config_store *,const qa_application *,
    const qa_launch_snapshot *,const qa_console *,const qa_command_context *);
const frontend_shared_storage *frontend_config_store_shared_storage(const frontend_config_store *);
bool frontend_config_store_images_pending(const frontend_config_store *);
/* Release the completed programme before final resources, retaining the
 * copied shared archive and completion receipt through the actual outcome. */
bool frontend_config_store_images_release(frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,qa_error *);
bool frontend_config_store_apply_shared_archive(frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,const qa_application_startup_source *,qa_error *);
/* Consumes only this candidate's failed shared owner under its genuine
 * detached ENGINE loan. A completed cleanup clears the retained association. */
bool frontend_config_store_shared_engine_shutdown(frontend_config_store *,
    const qa_application_engine_shutdown *,bool *complete,qa_error *);
bool frontend_config_store_cvar_edit(frontend_config_store *,qa_application *,const qa_console *,
    const qa_command_context *,qa_cvars *,qa_cvars_edit **,qa_error *);
bool frontend_config_store_config_filtered(frontend_config_store *,qa_application *,const qa_console *,
    const qa_command_context *,qa_cvars *,qa_cvar_config_filter,void *,qa_buffer *,qa_error *);
/* Private registry policy callbacks borrow their own pending source's
 * canonical scalar view; published sources retain the live ENGINE view. */
const qa_cvar_view *frontend_config_store_engine_value(const frontend_config_store *,
    qa_application *,const qa_console *,const char *);
bool frontend_config_store_stage_input(frontend_config_store *,const qa_console *,
    const qa_command_invocation *,bool *staged,qa_error *);
bool frontend_config_store_apply_archive(frontend_config_store *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,qa_cvars *,const qa_cvar_archive *,bool shared,qa_error *);
frontend_config_source *frontend_config_store_named_source(const frontend_config_store *,const char *);
/* Read the selected WORLD ENTITIES profile before physical Source startup.
 * Uses the same retained product/global roots and archive namespace as prepare. */
bool frontend_config_store_draft_archive(frontend_config_store *,const qa_launch_draft *,
    qa_cvar_archive *,qa_error *);
bool frontend_config_source_clone_bindings(const frontend_config_source *,uint32_t,frontend_authored_bindings **,qa_error *);
qa_settings_store frontend_config_store_input_store(const frontend_config_store *);
bool frontend_config_store_same_profile(const qa_launch_instance *,const qa_launch_instance *);
frontend_config_files *frontend_config_source_files(const frontend_config_source *);
frontend_key_profile *frontend_config_source_keys(const frontend_config_source *);
qa_cvars *frontend_config_source_cvars(const frontend_config_source *);
qa_console *frontend_config_source_console(const frontend_config_source *);
qa_application_console_scope frontend_config_source_scope(const frontend_config_source *);
/* Seat arguments are authored launch IDs. The owner resolves the actual
 * physical frontend ordinal; published input always borrows its stable seat. */
qa_input_seat *frontend_config_source_input(const frontend_config_source *,uint32_t);
/* These are genuine prepared registries, later consumed by the matching
 * source client factory; they are never ENGINE registry aliases. */
qa_cvars *frontend_config_source_seat_cvars(const frontend_config_source *,uint32_t);
qa_cvars *frontend_config_source_mouse_cvars(const frontend_config_source *,uint32_t);
/* Borrows the installed WORLD ENTITIES source's actual authored seat mouse
 * owner and selected movement kind. Absent source/seat returns NULL. */
qa_cvars *frontend_config_store_primary_mouse_cvars(const frontend_config_store *,uint32_t,qa_movement_kind *);
typedef struct frontend_config_legacy_view {
    const frontend_config_source *source;
    const qa_launch_instance *descriptor;
    const qa_product *product;
    qa_cvars *registry;
    uint32_t authored_seat;
    uint64_t command_generation;
} frontend_config_legacy_view;
bool frontend_config_store_primary_legacy_read(const frontend_config_store *,uint32_t,
    frontend_config_legacy_view *,bool *present,qa_error *);
bool frontend_config_store_primary_legacy_current(const frontend_config_store *,const frontend_config_legacy_view *);
bool frontend_config_store_select_bindings(frontend_config_store *,uint32_t authored_seat,
    qa_strings *,const qa_item_definition *,size_t,int32_t controller,qa_error *);
bool frontend_config_store_reset_bindings(frontend_config_store *,uint32_t authored_seat,int32_t controller,qa_error *);
/* Called at the returned live-player boundary after releasing held input and
 * before replacing any physical seat services. The retained GAME owns these
 * dictionaries until its actual candidate publishes or is cancelled. */
bool frontend_config_store_local_seats_capture(frontend_config_store *,qa_error *);
bool frontend_config_store_local_seat_prepare(frontend_config_store *,const qa_launch_seat *,unsigned physical_slot,qa_error *);
bool frontend_config_store_local_seats_route(frontend_config_store *,qa_error *);
/* Release omitted logical profiles only after their genuine CLIENT leases and
 * physical input services have returned. Also consumes captured dictionaries. */
bool frontend_config_store_local_seats_retire(frontend_config_store *,qa_error *);
/* Only isolated input from this exact pending primary source is exposed. */
qa_input_seat *frontend_config_store_candidate_input(const frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,unsigned physical_ordinal);
/* Borrows the one genuine pending graphical dictionary, including CLIENT
 * preparation when the actual primary GAME is physically reused. */
qa_input_seat *frontend_config_store_prepared_input(const frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,unsigned physical_ordinal);
/* Borrow a real pending dictionary, or the stable dictionary only when its
 * published source and authored physical seat are unchanged in this candidate.
 * A NULL candidate requires the genuine source-free ENGINE bootstrap and its
 * installed physical seat, rather than a missing source staging fallback. */
bool frontend_config_store_input_configuration(const frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,unsigned physical_ordinal,qa_input_seat **,qa_error *);
/* The same frontend publishes its initial or replacement view through the
 * installed preference owner's actual consume history. CLIENT scopes alone
 * do not imply ownership borrowed from a different frontend. */
bool frontend_config_store_view_transition(const frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,frontend_view_transition *,qa_error *);
bool frontend_config_store_client_input_configuration(const frontend_config_store *,
    const qa_application_client_preparation *,uint32_t,qa_input_seat **,qa_error *);
bool frontend_config_store_client_controller_selection(const frontend_config_store *,
    const qa_application_client_preparation *,uint32_t,qa_controller_selection *,qa_error *);
bool frontend_config_store_client_view_transition(const frontend_config_store *,
    const qa_application_client_preparation *,frontend_view_transition *,qa_error *);
bool frontend_config_store_client_settings_begin(frontend_config_store *,qa_application_client_preparation *,qa_error *);
bool frontend_config_store_client_settings_seed(frontend_config_store *,qa_application_client_preparation *,qa_cvar_archive *,qa_error *);
bool frontend_config_store_client_settings_advance(frontend_config_store *,qa_application_client_preparation *,bool,bool *,qa_error *);
bool frontend_config_store_client_settings_prepare(frontend_config_store *,qa_application_client_preparation *,qa_error *);
bool frontend_config_store_client_settings_ready_is(const frontend_config_store *,const qa_application_client_preparation *);
void frontend_config_store_client_settings_consume(frontend_config_store *,qa_application_client_preparation *);
bool frontend_config_store_client_settings_finish(frontend_config_store *,qa_application_client_preparation *,bool *,qa_error *);
bool frontend_config_store_client_settings_abort(frontend_config_store *,qa_application_client_preparation *,bool *complete,qa_error *);
bool frontend_config_store_client_settings_cancel(frontend_config_store *,qa_application_client_preparation *,bool *complete,qa_error *);
qa_application_client_preparation *frontend_config_store_client_preparation(const frontend_config_store *);
/* The actual source factory calls before destination options/capacity/Init.
 * A different physical program/profile returns carried=false. The supplied
 * tuple identifies that fresh factory's real console, registry, GAME scope
 * and module declaration owner. Completed script phases are preserved. */
bool frontend_config_store_carry_variables(frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,const qa_application_startup_source *,bool *carried,qa_error *);
/* The manager owns one canonical wrapper reference; each actual client lease
 * acquires another. The first acquisition adopts the prepared heap once.
 * Pure import creates an empty physical heap for the canonical QFCR prefix
 * decoder; the post-decode restore binder must qualify it before Init. */
bool frontend_config_source_acquire_seat_registry(frontend_config_source *,uint32_t,
    frontend_client_registry **,qa_error *);
/* Actual client registry wrappers retain this callback context until their
 * final observer-idle QACV destruction. Checked source retirement waits for it. */
bool frontend_config_source_registry_retain(frontend_config_source *,qa_error *);
bool frontend_config_source_registry_release(frontend_config_source *,qa_error *);
qa_cvars *frontend_config_store_cvar_owner(const frontend_config_store *,const qa_console *,
    const qa_command_context *,const char *);
qa_cvars *frontend_config_store_visible_cvars(const frontend_config_store *,const qa_console *,
    const qa_command_context *,size_t);
/* Stable host namespace roles borrow this exact physical constructor. A
 * supplemental hosted CLIENT supplies its actual GAME parent; prepared CLIENT
 * rows already retain that association. Structurally bound import is allowed
 * before the enclosing restore finishes. Neither operation reads cvar names. */
bool frontend_config_store_registry_reference(const frontend_config_store *,qa_application *,
    const qa_application_startup_source *,const qa_application_startup_source *parent_game,
    const qa_cvars *,qa_q3_host_cvar_namespace *,qa_error *);
bool frontend_config_store_registry_resolve(const frontend_config_store *,qa_application *,
    const qa_application_startup_source *,const qa_application_startup_source *parent_game,
    qa_q3_host_cvar_namespace,qa_cvars **,qa_error *);
/* The actual factory/CLIENT lease retains this callback root through host
 * destruction. GAME uses its existing manager row as that retained root. */
bool frontend_config_host_cvars_prepare(frontend_config_host_cvars *,const frontend_config_store *,qa_application *,
    const qa_application_startup_source *,const qa_application_startup_source *parent_game,
    qa_q3_host_cvar_services *,qa_error *);
/* The actual acquired-module lease supplies its pure entered-host reader once,
 * before native activation. No host pointer is cached by these callbacks. */
bool frontend_config_host_cvars_set_entry(frontend_config_host_cvars *,void *,frontend_config_host_entry_read,qa_error *);
bool frontend_config_host_cvar_entered(void *,const qa_q3_host *,const qa_console *,const qa_command_context *,qa_error *);
bool frontend_config_source_cvar_entered(void *,const qa_q3_host *,const qa_console *,const qa_command_context *,qa_error *);
/* Binding traps borrow the dictionary of this exact retained CLIENT while
 * physical key/catcher/release ownership stays with the stable seat root. */
bool frontend_config_host_bindings(void *,const qa_input_seat *,qa_input_seat **,qa_error *);
bool frontend_config_source_host_cvars(frontend_config_source *,qa_q3_host_cvar_services *,qa_error *);
bool frontend_config_source_tuple(const frontend_config_source *,qa_application_startup_source *);
/* The caller qualifies and prepares the stable native seat transfer before
 * publication; it commits that prepared transfer at the actual outcome. */
bool frontend_config_source_primary(const frontend_config_source *);
bool frontend_config_source_published(const frontend_config_source *);
bool frontend_config_store_read(frontend_config_store *,const qa_console *,const qa_command_context *,
    const char *,qa_bytes *,void **lease,qa_error *);
bool frontend_config_store_write_source_text(frontend_config_store *,const qa_command_invocation *,
    const char *,qa_bytes,qa_error *);
bool frontend_config_store_server_invocation_read(frontend_config_store *,const qa_command_invocation *,
    qa_application_startup_source *,qa_error *);
bool frontend_config_store_server_invocation_application(frontend_config_store *,const qa_command_invocation *,
    qa_application **,qa_error *);
/* Borrows the exact entered primary Source's retained product write owner. */
bool frontend_config_store_server_write_root(frontend_config_store *,const qa_command_invocation *,
    qa_settings_store *,qa_fs_root **,qa_error *);
bool frontend_config_store_primary_server_read(frontend_config_store *,qa_application_startup_source *,
    bool *present,qa_error *);
bool frontend_config_store_admin_dispatch(frontend_config_store *,const qa_command_invocation *,
    size_t skip,bool *handled,qa_error *);
bool frontend_config_store_admin_adopt(frontend_config_store *,qa_error *);
/* Includes a restored early child. Transport adoption belongs to the normal
 * network pump after its actual Source and directory bindings finish. */
bool frontend_config_store_admin_pending(const frontend_config_store *);
void frontend_config_store_release(frontend_config_store *,const qa_console *,void *lease);
bool frontend_config_store_save(frontend_config_store *,qa_error *);
bool frontend_config_store_retire(frontend_config_store *,const qa_console *,qa_error *);
void frontend_config_store_rebind(frontend_config_store *,qa_frontend *);
bool frontend_config_store_visit(const frontend_config_store *,const qa_application_content_visitor *,qa_error *);
/* The real factory calls this after canonical client QACV decode, before Init.
 * Repeated UI/CGAME aliases must qualify that same physical registry. */
bool frontend_config_source_restore_seat_cvars(frontend_config_source *,uint32_t,
    qa_cvars *,const frontend_keys_cvar_refs *,qa_error *);
bool frontend_config_source_restore_seat_registry(frontend_config_source *,uint32_t,
    frontend_client_registry *,const frontend_keys_cvar_refs *,qa_error *);
/* Genuine primary CLIENT slots bind the decoded roster before services copy
 * their canonical heap. Supplemental GAME-owned roles use the GAME binder. */
bool frontend_config_store_restore_client(frontend_config_store *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,qa_error *);
#endif
