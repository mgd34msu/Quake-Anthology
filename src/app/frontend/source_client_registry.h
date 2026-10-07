#ifndef QA_FRONTEND_SOURCE_CLIENT_REGISTRY_H
#define QA_FRONTEND_SOURCE_CLIENT_REGISTRY_H
#include "internal.h"
typedef struct frontend_source_client_registry {
    const qa_launch_snapshot *publication;
    qa_console *console;
    qa_cvars *cvars;
    qa_actor_owner receiver;
    uint32_t physical_seat,launch_seat;
    qa_application_console_kind kind;
} frontend_source_client_registry;
bool frontend_source_client_registry_read(const qa_frontend *,uint32_t,
    frontend_source_client_registry *,bool *present,qa_error *);
bool frontend_source_client_registry_current(const qa_frontend *,const frontend_source_client_registry *);
bool frontend_source_server_read(qa_frontend *,qa_actor_owner,qa_game_family,
    qa_application_startup_source *,qa_error *);
#endif
