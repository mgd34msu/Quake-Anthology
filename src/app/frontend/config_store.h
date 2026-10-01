#ifndef QA_FRONTEND_CONFIG_STORE_H
#define QA_FRONTEND_CONFIG_STORE_H
#include "internal.h"
#include "config_scripts.h"
#include "keys.h"
#include "client_registry.h"
#include "qa/application_startup_prepare.h"

typedef struct frontend_config_store frontend_config_store;
typedef struct frontend_config_source frontend_config_source;

/* The manager retains actual source ConfigStores and isolated logical input
 * during preparation. Its hooks and context outlive the application. */
frontend_config_store *frontend_config_store_create(qa_frontend *,qa_error *);
bool frontend_config_store_destroy(frontend_config_store *,qa_error *);
bool frontend_config_store_retired_ready(const frontend_config_store *,qa_error *);
const qa_application_startup_hooks *frontend_config_store_hooks(frontend_config_store *);
frontend_config_source *frontend_config_store_source(const frontend_config_store *,const qa_console *);
frontend_config_files *frontend_config_source_files(const frontend_config_source *);
frontend_key_profile *frontend_config_source_keys(const frontend_config_source *);
qa_cvars *frontend_config_source_cvars(const frontend_config_source *);
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
bool frontend_config_store_visit(const frontend_config_store *,const qa_application_content_visitor *,qa_error *);
bool frontend_config_store_checkpoint(const frontend_config_store *,const qa_application_content_graph *,
    const frontend_keys_cvar_refs *,qa_buffer *,qa_error *);
/* Decode real file and supplemental registry owners before physical source
 * construction. GAME and transferred client registries remain typed borrows. */
bool frontend_config_store_restore(qa_frontend *,qa_application *,qa_application_content_graph *,
    frontend_keys *,const frontend_keys_cvar_refs *,qa_bytes,frontend_config_store **,qa_error *);
bool frontend_config_store_restore_into(frontend_config_store *,qa_application *,qa_application_content_graph *,
    frontend_keys *,const frontend_keys_cvar_refs *,qa_bytes,qa_error *);
/* Pure canonical registry prefix staging uses the already decoded source row.
 * The caller retains this callback root when it creates the one physical heap;
 * no provider lookup, metadata construction or configuration replay occurs. */
bool frontend_config_store_restore_registry_options(frontend_config_store *,const char *source_instance,
    uint32_t authored_seat,qa_cvar_options *,frontend_client_registry_context *,qa_sha256_digest *,qa_error *);
/* The real factory calls this after canonical client QACV decode, before Init.
 * Repeated UI/CGAME aliases must qualify that same physical registry. */
bool frontend_config_source_restore_seat_cvars(frontend_config_source *,uint32_t,
    qa_cvars *,const frontend_keys_cvar_refs *,qa_error *);
bool frontend_config_source_restore_seat_registry(frontend_config_source *,uint32_t,
    frontend_client_registry *,const frontend_keys_cvar_refs *,qa_error *);
bool frontend_config_store_finish_restore(frontend_config_store *,qa_error *);
#endif
