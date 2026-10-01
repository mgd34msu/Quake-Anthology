#ifndef QA_FRONTEND_CONFIG_SCRIPTS_H
#define QA_FRONTEND_CONFIG_SCRIPTS_H
#include "startup_config.h"
#include "qa/settings.h"
#include "qa/persistence_content.h"
#include "qa/console_buffer.h"

typedef struct frontend_config_files frontend_config_files;
/* Normal source preparation only. Roots come from the actual configured
 * source owner and remain independent of restricted product media. */
frontend_config_files *frontend_config_files_create(qa_catalog *, qa_product_id,
                                                   const char *user_root, const char *content_root, qa_error *);
bool frontend_config_files_destroy(frontend_config_files *, qa_error *);
bool frontend_config_files_idle(const frontend_config_files *);
qa_product_id frontend_config_files_product(const frontend_config_files *);
qa_catalog *frontend_config_files_catalog(const frontend_config_files *);
qa_settings_store frontend_config_files_store(const frontend_config_files *, bool base);
qa_fs_root *frontend_config_files_root(const frontend_config_files *, bool base);
qa_fs_root *frontend_config_files_loose_root(const frontend_config_files *, bool base);
const char *frontend_config_files_game_directory(const frontend_config_files *);
bool frontend_config_files_read(void *, frontend_script_scope, const char *,
                                const qa_command_context *, qa_bytes *, void **lease, qa_error *);
void frontend_config_files_release(void *, void *lease);
bool frontend_config_files_console_read(frontend_config_files *, const char *,
                                        const qa_command_context *, qa_bytes *, void **lease, qa_error *);
bool frontend_config_files_list(frontend_config_files *, const qa_command_context *, qa_vfs_listing *, qa_error *);
bool frontend_config_files_write_config(frontend_config_files *, const char *, const qa_command_context *,
                                        const qa_cvars *, const qa_input_seat *, bool controllers, qa_error *);
bool frontend_config_files_write_config_text(frontend_config_files *,const char *,
                                             const qa_command_context *,qa_bytes,qa_error *);
bool frontend_config_files_dump(frontend_config_files *,const char *,const qa_command_context *,
                                const qa_console_buffer *,qa_error *);
bool frontend_config_files_visit(const frontend_config_files *,const qa_application_content_visitor *,qa_error *);
bool frontend_config_files_checkpoint(const frontend_config_files *,const qa_application_content_graph *,qa_buffer *,qa_error *);
/* Transfers genuine catalog/view authorities from an admitted pure graph.
 * No directory creation, scan, loose content read or startup replay. */
bool frontend_config_files_restore(qa_application_content_graph *,qa_bytes,frontend_config_files **,qa_error *);
#endif
