#ifndef QA_APPLICATION_NATIVE_Q3_CLIENT_PRIVATE_H
#define QA_APPLICATION_NATIVE_Q3_CLIENT_PRIVATE_H
#include "qa/application_native_q3_client.h"
#include "qa/source_save.h"
#include "qa/console_cvar_observer.h"

typedef struct native_client_definition {
    const char *symbol, *name, *value;
    uint32_t flags;
    bool missionpack;
} native_client_definition;

#define QA_NATIVE_CLIENT_CVARS 101
struct qa_native_q3_client_service {
    qa_application *application;
    qa_native_q3_client_services services;
    qa_native_q3_character_selection character;
    qa_launch_instance_lease *source_lease;
    const qa_q3_game *source_game;
    qa_product_id content_product;
    qa_q3_product product;
    qa_native_q3_client_cvar cache[QA_NATIVE_CLIENT_CVARS];
    size_t count;
    uint64_t force_model_count, overlay_count;
    int32_t local_server;
    qa_cvar_observer_token time_owner_tokens[6], time_mirror_tokens[6];
    bool time_names[6], time_bound;
    size_t time_busy;
    size_t action_busy;
    char *system_info;
    bool registered, updating, overlay_initial;
};
extern const native_client_definition native_client_definitions[];
extern const size_t native_client_definition_count;
bool native_client_allocate(qa_application *, const qa_application_native_q3_presentation *,
    const qa_native_q3_client_services *, const qa_native_q3_character_selection *,
    qa_native_q3_client_service **, qa_error *);
bool native_client_allocate_bound(qa_application *, const qa_native_q3_client_basis *,
    const qa_native_q3_client_services *, const qa_native_q3_character_selection *,
    qa_native_q3_client_service **, qa_error *);
bool native_client_fail(qa_error *, qa_status, const char *);
bool native_client_cvar_fields(qa_source_save_io *, qa_native_q3_client_cvar *);
bool native_client_time_bind(qa_native_q3_client_service *, bool restoring, qa_error *);
void native_client_time_close(qa_native_q3_client_service *);
bool native_client_time_fields(qa_source_save_io *, qa_native_q3_client_service *);
#endif
