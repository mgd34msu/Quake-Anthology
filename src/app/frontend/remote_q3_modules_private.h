#ifndef QA_FRONTEND_REMOTE_Q3_MODULES_PRIVATE_H
#define QA_FRONTEND_REMOTE_Q3_MODULES_PRIVATE_H

#include "remote_q3_modules.h"
#include "remote_q3_private.h"
#include "remote_config.h"
#include "network_browser.h"
#include "network_restore_services.h"
#include "system_cinematic.h"
#include "qa/catalog_write.h"

typedef struct remote_module_saved {
    uint32_t role;
    qa_string_id service_owner;
    uint64_t keys;
    qa_buffer scene, media, equipment, music;
    char *music_intro, *music_loop;
    qa_audio_listener listener;
    bool has_listener, has_music, music_attached, music_looping;
    bool prepared, scene_restored, media_restored, music_restored, music_origin_restored, equipment_restored;
} remote_module_saved;

typedef struct remote_module_lease {
    struct remote_module_lease *next;
    frontend_remote_q3_modules *owner;
    qa_qvm_role role;
    uint64_t service_owner;
    frontend_client_registry *registry;
    qa_catalog_write_resolver *write_resolver;
    frontend_key_profile *keys;
    frontend_config_host_cvars namespaces;
    qa_q3_host_client_services network;
    frontend_network_initial_services_binding initial_network;
    frontend_network_restore_services_binding restored_network;
    frontend_network_browser_binding browser;
    qa_q3_host_client_context constructor;
    qa_q3_presentation *presentation;
    qa_q3_cinematic_source *cinematics;
    frontend_equipment_source *equipment;
    qa_audio_music *music;
    char *music_intro, *music_loop;
    qa_audio_listener listener;
    const qa_q3_host *render_host;
    const qa_qvm_call *render_call;
    const qa_q3_refdef *render_definition;
    qa_command_context command;
    size_t callbacks, movie_references;
    qa_vfs *media_views[1];
    bool released, preparing, music_attached, music_looping, has_listener, legacy_cinematics;
} remote_module_lease;

typedef enum remote_module_basis { REMOTE_MODULE_DECODED, REMOTE_MODULE_INITIAL } remote_module_basis;
struct frontend_remote_q3_modules {
    qa_frontend *frontend;
    qa_application *application;
    remote_module_basis kind;
    union {
        struct { frontend_remote_q3 *row; frontend_remote_q3_resources view; } decoded;
        struct { frontend_remote_q3_initial *owner; frontend_remote_q3_initial_view view; } initial;
    } basis;
    application_native_q3_client_modules *modules;
    remote_module_lease *leases;
    remote_module_saved *saved;
    size_t saved_count;
    qa_buffer saved_bytes;
    bool attached, constructing, retiring, restoring, lower_finished;
    struct frontend_remote_q3_modules_video *video;
};

const qa_application_q3_remote_source *frontend_remote_modules_source(const frontend_remote_q3_modules *);
bool frontend_remote_modules_released_drain(frontend_remote_q3_modules *, qa_error *);
remote_module_saved *frontend_remote_modules_saved(frontend_remote_q3_modules *, qa_qvm_role, uint64_t);
void frontend_remote_modules_saved_dispose(remote_module_saved *, size_t);
bool frontend_remote_modules_music_restore_origin(remote_module_lease *, qa_error *);
bool frontend_remote_modules_restore_renderer_parameters(frontend_remote_q3_modules *, qa_error *);
bool frontend_remote_modules_construct_restored(qa_frontend *, frontend_remote_q3 *,
    frontend_remote_q3_initial *, remote_module_saved **, size_t, qa_bytes, qa_bytes,
    frontend_remote_q3_modules **, qa_error *);

#endif
