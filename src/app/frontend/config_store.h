#ifndef QA_FRONTEND_CONFIG_STORE_H
#define QA_FRONTEND_CONFIG_STORE_H
#include "internal.h"
#include "config_scripts.h"
#include "keys.h"
#include "qa/application_startup_prepare.h"

typedef struct frontend_config_store frontend_config_store;
typedef struct frontend_config_source frontend_config_source;

/* The manager retains actual source ConfigStores and isolated logical input
 * during preparation. Its hooks and context outlive the application. */
frontend_config_store *frontend_config_store_create(qa_frontend *,qa_error *);
bool frontend_config_store_destroy(frontend_config_store *,qa_error *);
const qa_application_startup_hooks *frontend_config_store_hooks(frontend_config_store *);
frontend_config_source *frontend_config_store_source(const frontend_config_store *,const qa_console *);
frontend_config_files *frontend_config_source_files(const frontend_config_source *);
frontend_key_profile *frontend_config_source_keys(const frontend_config_source *);
qa_cvars *frontend_config_source_cvars(const frontend_config_source *);
qa_input_seat *frontend_config_source_input(const frontend_config_source *,uint32_t);
/* These are genuine prepared registries, later consumed by the matching
 * source client factory; they are never ENGINE registry aliases. */
qa_cvars *frontend_config_source_seat_cvars(const frontend_config_source *,uint32_t);
qa_cvars *frontend_config_source_mouse_cvars(const frontend_config_source *,uint32_t);
/* Transfers the prepared registry to its actual client factory exactly once.
 * The manager keeps a borrow until that source's checked retirement. */
bool frontend_config_source_take_seat_cvars(frontend_config_source *,uint32_t,qa_cvars **,qa_error *);
qa_cvars *frontend_config_store_cvar_owner(const frontend_config_store *,const qa_console *,
    const qa_command_context *,const char *);
qa_cvars *frontend_config_store_visible_cvars(const frontend_config_store *,const qa_console *,
    const qa_command_context *,size_t);
/* The caller qualifies and prepares the stable native seat transfer before
 * publication; it commits that prepared transfer at the actual outcome. */
bool frontend_config_source_primary(const frontend_config_source *);
bool frontend_config_source_published(const frontend_config_source *);
bool frontend_config_store_read(frontend_config_store *,const qa_console *,const qa_command_context *,
    const char *,qa_bytes *,void **lease,qa_error *);
void frontend_config_store_release(frontend_config_store *,const qa_console *,void *lease);
bool frontend_config_store_save(frontend_config_store *,qa_error *);
bool frontend_config_store_retire(frontend_config_store *,const qa_console *,qa_error *);
void frontend_config_store_rebind(frontend_config_store *,qa_frontend *);
#endif
