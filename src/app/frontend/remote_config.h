#ifndef QA_FRONTEND_REMOTE_CONFIG_H
#define QA_FRONTEND_REMOTE_CONFIG_H
#include "config_store.h"
#include "qa/application_q3_factory.h"
#include "qa/source_frame_time.h"

typedef struct frontend_remote_configs frontend_remote_configs;
typedef struct frontend_remote_config frontend_remote_config;
typedef struct frontend_remote_config_view {
    const frontend_remote_config *owner;
    const qa_launch_instance *receiver;
    qa_application_console_scope scope;
    qa_console *console;
    qa_cvars *cvars,*q3_mouse,*q3_view,*movement_mouse;
    frontend_key_profile *keys;
    uint32_t physical_seat;
    qa_ruleset_id movement;
    bool ready,published;
    const qa_source_frame_time_binding *frame_time;
    const qa_input_tuning_handles *q3_input_tuning,*movement_input_tuning;
} frontend_remote_config_view;

frontend_remote_configs *frontend_remote_configs_create(qa_frontend *,frontend_config_store *,qa_error *);
bool frontend_remote_configs_destroy(frontend_remote_configs *,qa_error *);
bool frontend_remote_configs_empty(const frontend_remote_configs *);
frontend_remote_config *frontend_remote_config_find(const frontend_remote_configs *,const qa_cvars *);
frontend_remote_config *frontend_remote_config_find_context(const frontend_remote_configs *,const qa_command_context *);
bool frontend_remote_config_phase(const frontend_remote_configs *,const void *);
bool frontend_remote_config_read(const frontend_remote_config *,frontend_remote_config_view *);
bool frontend_remote_config_current(const frontend_remote_config *,const frontend_remote_config_view *);
/* After physical CLIENT leases return, project retained local logical clients
 * onto the currently published dense physical seat order. */
bool frontend_remote_configs_local_routes(frontend_remote_configs *,qa_application *,qa_error *);
bool frontend_remote_config_pending(const frontend_remote_config *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *);
bool frontend_remote_config_tuple(const frontend_remote_config *,qa_application_startup_source *);
const frontend_config_files *frontend_remote_config_files(const frontend_remote_config *);
bool frontend_remote_config_cvar_active(const frontend_remote_config *,const qa_command_context *);
bool frontend_remote_config_refresh(frontend_remote_config *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,qa_error *);
bool frontend_remote_config_registries(const frontend_remote_config *,qa_application *,
    const qa_application_startup_source *,qa_cvars *namespaces[8],qa_cvars **hosted_game,qa_error *);
bool frontend_remote_config_bindings(const frontend_remote_config *,qa_application *,
    const qa_application_startup_source *,const qa_input_seat *,qa_input_seat **,qa_error *);
qa_input_seat *frontend_remote_configs_candidate_input(const frontend_remote_configs *,qa_application *,
    const qa_launch_snapshot *,unsigned physical_ordinal);
/* The actual new remote CGAME row has no carried same-profile input owner. */
bool frontend_remote_configs_fresh_input(const frontend_remote_configs *,qa_application *,const qa_launch_snapshot *);
bool frontend_remote_config_acquire(const frontend_remote_config *,frontend_client_registry **,qa_error *);
bool frontend_remote_config_reset_bindings(frontend_remote_config *,int32_t controller,qa_error *);
bool frontend_remote_config_prepare(frontend_remote_configs *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,void **,qa_error *);
bool frontend_remote_config_advance(frontend_remote_config *,qa_console *,bool *,qa_error *);
bool frontend_remote_config_release_phase(frontend_remote_config *,qa_error *);
bool frontend_remote_config_script_read(frontend_remote_config *,const qa_command_context *,const char *,qa_bytes *,void **,qa_error *);
void frontend_remote_config_script_release(frontend_remote_config *,void *);
void frontend_remote_config_script_complete(frontend_remote_config *,const qa_command_context *,const char *,bool);
bool frontend_remote_config_allow(frontend_remote_config *,const qa_command_invocation *);
qa_cvars *frontend_remote_config_cvar_owner(frontend_remote_config *,const qa_command_context *,const char *);
qa_cvars *frontend_remote_config_visible(frontend_remote_config *,const qa_command_context *,size_t);
bool frontend_remote_configs_ready(frontend_remote_configs *,qa_application *,const qa_launch_snapshot *,qa_error *);
void frontend_remote_configs_finish(frontend_remote_configs *,qa_application *,const qa_launch_snapshot *,bool);
bool frontend_remote_config_preinit(frontend_remote_configs *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,qa_error *);
bool frontend_remote_config_retire(frontend_remote_configs *,qa_application *,const qa_application_startup_source *,qa_error *);
bool frontend_remote_config_retire_hosted(frontend_remote_configs *,qa_application *,
    const qa_application_startup_source *,qa_error *);
bool frontend_remote_config_bind_hosted(frontend_remote_configs *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,const qa_application_startup_source *,qa_cvars **,qa_error *);
void frontend_remote_config_publish_hosted(frontend_remote_configs *,qa_application *,const qa_application_startup_source *);
bool frontend_remote_configs_retire_staged_parent(frontend_remote_configs *,qa_application *,
    const qa_application_startup_source *,qa_error *);
void frontend_remote_configs_rebind(frontend_remote_configs *,qa_frontend *,frontend_config_store *);
bool frontend_remote_configs_visit(const frontend_remote_configs *,const qa_application_content_visitor *,qa_error *);
bool frontend_remote_configs_save(frontend_remote_configs *,qa_application *,qa_error *);
bool frontend_remote_configs_checkpoint(const frontend_remote_configs *,const qa_application_content_graph *,qa_buffer *,qa_error *);
bool frontend_remote_configs_restore(frontend_remote_configs *,qa_application *,qa_application_content_graph *,frontend_keys *,qa_bytes,qa_error *);
/* Actual core slot qualification precedes QFCR import and services.
 * The later tuple restore verifies the same reconstructed Source view. */
bool frontend_remote_config_bind_restored(frontend_remote_configs *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,qa_error *);
bool frontend_remote_configs_finish_restore(frontend_remote_configs *,qa_error *);
bool frontend_remote_config_program_source(const frontend_remote_configs *,qa_application *,const qa_launch_snapshot *,
    const qa_application_startup_source *,qa_application_startup_source *,bool *,qa_error *);

#endif
