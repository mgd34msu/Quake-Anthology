#ifndef QA_FRONTEND_NEUTRAL_CONFIG_H
#define QA_FRONTEND_NEUTRAL_CONFIG_H
#include "client_source.h"
#include "config_scripts.h"
#include "qa/input.h"
#include "qa/input_release.h"
#include "qa/inventory.h"
#include "qa/application_client_prepare.h"
#include "view_settings.h"

typedef struct frontend_neutral_configs frontend_neutral_configs;
typedef struct frontend_neutral_config frontend_neutral_config;
typedef struct frontend_config_store frontend_config_store;
typedef struct frontend_neutral_config_view {
    const frontend_neutral_config *owner;
    qa_application_client_source source;
    qa_cvars *client, *mouse, *movement;
    qa_movement_kind kind;
    uint32_t physical_seat;
    uint64_t namespace_revision;
    bool ready, published;
} frontend_neutral_config_view;

frontend_neutral_configs *frontend_neutral_configs_create(qa_frontend *, frontend_config_store *, qa_error *);
bool frontend_neutral_configs_destroy(frontend_neutral_configs *, qa_error *);
bool frontend_neutral_configs_empty(const frontend_neutral_configs *);
bool frontend_neutral_configs_restore_abort_unbound(frontend_neutral_configs *,qa_error *);
bool frontend_neutral_config_options(frontend_neutral_configs *, uint32_t physical_seat,
    qa_movement_kind actual_movement, frontend_client_source_options *, qa_error *);
bool frontend_neutral_config_pending_options(frontend_neutral_configs *,uint32_t physical_seat,
    frontend_client_source_options *,qa_error *);
bool frontend_neutral_config_options_cancel(frontend_neutral_configs *,frontend_client_source_options *,qa_error *);
bool frontend_neutral_config_movement_adopt(frontend_neutral_configs *,const qa_cvars *,
    qa_movement_kind actual_movement,qa_error *);
bool frontend_neutral_config_read(const frontend_neutral_configs *, const qa_cvars *,
    frontend_neutral_config_view *, qa_error *);
bool frontend_neutral_config_current(const frontend_neutral_config_view *);
/* Returned capture/import namespace custody only; this does not admit live input. */
bool frontend_neutral_config_checkpoint_read(const frontend_neutral_configs *,const qa_cvars *,
    frontend_neutral_config_view *,qa_error *);
/* Normal shutdown borrows only the exact returned CLIENT-owned ALL ticket. */
bool frontend_neutral_config_retirement_release_ready(const frontend_neutral_configs *,
    const qa_input_seat *,const qa_input_release *,qa_error *);
bool frontend_config_store_neutral_retirement_release_ready(const frontend_config_store *,
    const qa_input_seat *,const qa_input_release *,qa_error *);
bool frontend_neutral_config_checkpoint_current(const frontend_neutral_config_view *,qa_error *);
bool frontend_neutral_config_retired_recipient(const frontend_neutral_configs *,
    const qa_application_client_source *,bool *,qa_error *);
bool frontend_neutral_config_startup_read(const frontend_neutral_configs *,qa_application_client_source *,bool *,qa_error *);
bool frontend_neutral_config_reset_bindings(frontend_neutral_configs *,uint32_t,int32_t,bool *,qa_error *);
bool frontend_neutral_config_select_bindings(frontend_neutral_configs *,uint32_t,qa_strings *,
    const qa_item_definition *,size_t,int32_t,bool *,qa_error *);
bool frontend_config_store_neutral_startup_read(const frontend_config_store *,qa_application_client_source *,bool *,qa_error *);
bool frontend_config_store_neutral_read(const frontend_config_store *, const qa_cvars *,
    frontend_neutral_config_view *, qa_error *);
bool frontend_config_store_neutral_checkpoint_read(const frontend_config_store *,const qa_cvars *,
    frontend_neutral_config_view *,qa_error *);
bool frontend_config_store_neutral_retired_recipient(const frontend_config_store *,
    const qa_application_client_source *,bool *,qa_error *);
bool frontend_config_store_neutral_options(frontend_config_store *,uint32_t physical_seat,
    qa_movement_kind actual_movement,frontend_client_source_options *,qa_error *);
bool frontend_config_store_neutral_pending_options(frontend_config_store *,uint32_t physical_seat,
    frontend_client_source_options *,qa_error *);
bool frontend_config_store_neutral_options_cancel(frontend_config_store *,frontend_client_source_options *,qa_error *);
bool frontend_config_store_neutral_movement_adopt(frontend_config_store *,const qa_cvars *,
    qa_movement_kind actual_movement,qa_error *);
bool frontend_config_store_client_profile(const frontend_config_store *,qa_game_family,
    qa_product_id *,qa_error *);
bool frontend_config_store_neutral_adopt_store(frontend_config_store *,const qa_launch_instance *,
    frontend_config_files *,qa_error *);
bool frontend_neutral_config_client_input(const frontend_neutral_configs *,
    const qa_application_client_preparation *,uint32_t,qa_input_seat **,qa_error *);
bool frontend_neutral_config_client_controller(const frontend_neutral_configs *,
    const qa_application_client_preparation *,uint32_t,qa_controller_selection *,qa_error *);
bool frontend_config_store_client_input_configuration(const frontend_config_store *,
    const qa_application_client_preparation *,uint32_t,qa_input_seat **,qa_error *);
bool frontend_config_store_client_controller_selection(const frontend_config_store *,
    const qa_application_client_preparation *,uint32_t,qa_controller_selection *,qa_error *);
bool frontend_config_store_client_view_transition(const frontend_config_store *,
    const qa_application_client_preparation *,frontend_view_transition *,qa_error *);
bool frontend_neutral_configs_save(frontend_neutral_configs *, qa_error *);
bool frontend_neutral_configs_visit(const frontend_neutral_configs *,
    const qa_application_content_visitor *, qa_error *);
void frontend_neutral_configs_rebind(frontend_neutral_configs *, qa_frontend *,frontend_config_store *);
bool frontend_neutral_configs_checkpoint(const frontend_neutral_configs *,
    const qa_application_content_graph *, qa_buffer *, qa_error *);
bool frontend_neutral_configs_restore(frontend_neutral_configs *, qa_application_content_graph *,
    qa_bytes, qa_error *);
bool frontend_neutral_configs_finish_restore(frontend_neutral_configs *, qa_error *);
#endif
