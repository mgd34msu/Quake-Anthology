#ifndef QA_FRONTEND_REMOTE_Q3_MODULES_H
#define QA_FRONTEND_REMOTE_Q3_MODULES_H

#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "equipment_source.h"
#include "keys.h"
#include "system_cinematic.h"
#include "qa/application_native_q3_client_modules.h"
#include "qa/q3_cinematic_handles.h"

typedef struct frontend_remote_q3_modules frontend_remote_q3_modules;
typedef struct frontend_remote_q3_module_media {
    const frontend_remote_q3_modules *owner;
    qa_application_q3_role_receipt receipt;
    qa_q3_presentation *presentation;
    qa_q3_presentation_assets *assets;
    qa_vfs *mounts;
    uint32_t physical_seat;
    qa_vfs *const *media_views;
    size_t media_view_count;
} frontend_remote_q3_module_media;

typedef struct frontend_remote_q3_module_topology {
    const frontend_remote_q3_modules *owner;
    size_t index;
    qa_application_q3_remote_source source;
    uint64_t source_group, service_owner;
    uint32_t physical_seat;
    qa_qvm_role role;
    qa_q3_host *host;
    qa_q3_host_client_context constructor;
    application_native_q3_client_modules *modules;
    qa_q3_presentation *presentation;
    qa_q3_presentation_assets *assets;
    qa_media_library *movies;
    qa_q3_cinematic_source *cinematics;
    qa_vfs *mounts;
    frontend_key_profile *keys;
    frontend_equipment_source *equipment;
    qa_audio_music *music;
    bool music_attached;
} frontend_remote_q3_module_topology;

/* Decoded resources precede this child. Construction retains partial outputs
 * for checked cleanup and does not enter source Init. */
bool frontend_remote_q3_modules_create(frontend_remote_q3 *, frontend_remote_q3_modules **, qa_error *);
bool frontend_remote_q3_modules_create_initial(qa_frontend *, frontend_remote_q3_initial *,
    frontend_remote_q3_modules **, qa_error *);
frontend_remote_q3 *frontend_remote_q3_modules_parent(const frontend_remote_q3_modules *);
frontend_remote_q3_initial *frontend_remote_q3_modules_initial_parent(const frontend_remote_q3_modules *);
application_native_q3_client_modules *frontend_remote_q3_modules_owner(const frontend_remote_q3_modules *);
/* Borrowed structural children for the shared cold dictionaries. This proves
 * actual host ownership, including pending restoration, without claiming Init.
 * The enclosing parent inventory fence prevents replacement or retirement. */
size_t frontend_remote_q3_modules_role_count(const frontend_remote_q3_modules *);
bool frontend_remote_q3_modules_role_read(const frontend_remote_q3_modules *, size_t,
    frontend_remote_q3_module_topology *, qa_error *);
bool frontend_remote_q3_modules_role_current(const frontend_remote_q3_module_topology *);
/* Exact numeric CIN namespace, also after ENGINE import but before role music
 * continuation. This proves no completed ownership of the music bus. */
bool frontend_remote_q3_modules_cinematics_role_read(const frontend_remote_q3_modules *, size_t,
    frontend_remote_q3_module_topology *, qa_error *);
bool frontend_remote_q3_modules_cinematics_role_current(const frontend_remote_q3_module_topology *);
bool frontend_remote_q3_modules_capture_returned(const frontend_remote_q3_modules *, qa_error *);
/* After the actual movie parents import, before global numeric handles and
 * Q3MS role media: binds actual saved shared roles without Init. Saved local
 * movie ownership stays local. */
bool frontend_remote_q3_modules_cinematic_source_decode(frontend_remote_q3_modules *,
    const frontend_system_cinematic_identity *, frontend_system_cinematic_source *, qa_error *);
bool frontend_remote_q3_modules_idle(const frontend_remote_q3_modules *);
bool frontend_remote_q3_modules_retired(const frontend_remote_q3_modules *);
bool frontend_remote_q3_modules_destroy(frontend_remote_q3_modules **, qa_error *);
bool frontend_remote_q3_modules_media_read(const frontend_remote_q3_modules *, qa_qvm_role,
    frontend_remote_q3_module_media *, qa_error *);
bool frontend_remote_q3_modules_media_current(const frontend_remote_q3_module_media *);
/* The frame owner clears this contribution immediately before its real role
 * Draw entry, then observes only listener callbacks made by that entry. */
bool frontend_remote_q3_modules_listener_begin(frontend_remote_q3_modules *, qa_qvm_role, qa_error *);
bool frontend_remote_q3_modules_listener_read(const frontend_remote_q3_modules *, qa_qvm_role,
    qa_audio_listener *, bool *present, qa_error *);

#endif
