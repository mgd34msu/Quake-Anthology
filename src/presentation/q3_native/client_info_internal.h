#ifndef QA_Q3_NATIVE_CLIENT_INFO_INTERNAL_H
#define QA_Q3_NATIVE_CLIENT_INFO_INTERNAL_H
#include "client_info.h"
#include "remote_frame.h"
#include "../q3/internal.h"
#include "qa/vfs_view_save.h"
#include "qa/common_parse.h"

typedef struct q3n_animation_holder {
    qa_resource *resource;
    qa_vfs_acquisition receipt;
} q3n_animation_holder;
struct q3n_clients {
    q3n_client_options options;
    q3n_client_info clients[64];
    q3n_animation_holder holders[64];
    qa_common_parser animation_parser;
    qa_application *application;
    const qa_application_native_q3_presentation *cut;
    const q3n_remote_source_view *remote_cut;
    uint64_t next_media_revision;
    uint64_t active_configstring_revision;
    uint64_t serverinfo_revision;
    uint32_t active_client;
    uint32_t max_clients;
    int32_t game_type;
    bool busy, active_info;
};
bool q3n_client_fail(qa_error *, qa_status, const char *);
void q3n_animation_dispose(q3n_animation_holder *);
bool q3n_client_handles_valid(const q3n_clients *, const q3n_client_info *, qa_error *);
bool q3n_clients_runtime_bound(const q3n_clients *, const q3n_client_options *, qa_error *);
bool q3n_clients_runtime_dispose(q3n_clients *, const q3n_client_options *, qa_error *);
#endif
