#ifndef QA_APPLICATION_NATIVE_Q3_CLIENT_PRIVATE_H
#define QA_APPLICATION_NATIVE_Q3_CLIENT_PRIVATE_H
#include "qa/application_native_q3_client.h"
#include "qa/source_save.h"
#include "qa/source_frame_time.h"
#include "qa/application_native_q3_cvars.h"

typedef struct native_client_definition {
    qa_native_q3_cvar_id id;
    const char *symbol, *name, *value;
    uint32_t flags;
    bool missionpack;
} native_client_definition;

#define QA_NATIVE_CLIENT_CVARS QA_NATIVE_Q3_CVAR_COUNT
_Static_assert(QA_NATIVE_CLIENT_CVARS<UINT8_MAX,"CGAME cvar ordinals fit their cold map");
struct qa_native_q3_client_service {
    qa_application *application;
    qa_native_q3_client_services services;
    qa_source_frame_time_binding frame_time;
    qa_native_q3_character_selection character;
    qa_launch_instance_lease *source_lease;
    const qa_q3_game *source_game;
    qa_product_id content_product;
    qa_q3_product product;
    qa_native_q3_client_cvar cache[QA_NATIVE_CLIENT_CVARS];
    qa_native_q3_cvar_refs cvar_refs;
    size_t count;
    uint64_t force_model_count, overlay_count;
    int32_t local_server;
    size_t action_busy;
    char *system_info;
    bool registered, updating, overlay_initial;
};
extern const native_client_definition native_client_definitions[];
extern const size_t native_client_definition_count;
/* Borrowed access for one cache operation; physical service custody stays
 * with the local or received CLIENT owner. */
typedef struct native_client_cache_access {
    void *context;
    bool (*current)(void *);
    bool (*configstring)(void *, uint32_t, const char **, qa_error *);
    bool (*reload_client_info)(void *, uint32_t, const char *, qa_error *);
    qa_cvars *registry;
    uint64_t owner;
    qa_q3_product product;
    qa_native_q3_client_cvar *cache;
    qa_native_q3_cvar_refs *refs;
    size_t count;
    const char *oversized_error, *reload_memory_error, *missing_error;
} native_client_cache_access;
typedef struct native_client_userinfo_text {
    const char *defaults, *identity, *character, *capacity_error, *memory_error;
} native_client_userinfo_text;
void native_client_cache_bind(const native_client_cache_access *,bool dense);
bool native_client_cache_copy_row(const native_client_cache_access *,qa_native_q3_cvar_id,bool,qa_error *);
bool native_client_cache_refresh(const native_client_cache_access *,qa_error *);
bool native_client_cache_read(const qa_native_q3_cvar_refs *,const qa_native_q3_client_cvar *,size_t,
    qa_native_q3_cvar_id,qa_native_q3_client_cvar *,qa_error *);
bool native_client_cache_register(const native_client_cache_access *, const char *,
    int32_t *, uint64_t *, qa_error *);
bool native_client_cache_userinfo(const native_client_cache_access *,
    const qa_native_q3_character_selection *, const char *,
    const native_client_userinfo_text *, qa_error *);
bool native_client_cache_reload(const native_client_cache_access *, qa_error *);
bool native_client_cache_update(const native_client_cache_access *, bool *,
    uint64_t *, uint64_t *, qa_error *);
bool native_client_allocate(qa_application *, const qa_application_native_q3_presentation *,
    const qa_native_q3_client_services *, const qa_native_q3_character_selection *,
    qa_native_q3_client_service **, qa_error *);
bool native_client_allocate_bound(qa_application *, const qa_native_q3_client_basis *,
    const qa_native_q3_client_services *, const qa_native_q3_character_selection *,
    qa_native_q3_client_service **, qa_error *);
bool native_client_fail(qa_error *, qa_status, const char *);
bool native_client_cvar_fields(qa_source_save_io *, qa_native_q3_client_cvar *);
bool native_client_time_register(qa_native_q3_client_service *, qa_error *);
bool native_client_time_fields(qa_source_save_io *, qa_native_q3_client_service *);
#endif
