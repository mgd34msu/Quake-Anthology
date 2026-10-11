#ifndef QA_Q3_NATIVE_CGAME_COMMANDS_H
#define QA_Q3_NATIVE_CGAME_COMMANDS_H

#include "events.h"
#include "hud.h"
#include "mission_hud.h"
#include "server_commands.h"
#include "qa/console.h"

bool q3n_cgame_command_equal(const char *, const char *);
size_t q3n_cgame_local_command_count(qa_q3_product);
size_t q3n_cgame_command_count(qa_q3_product);
const char *q3n_cgame_command_name(qa_q3_product, size_t);
typedef struct q3n_cgame_command_services {
    void *context;
    const qa_cvars *cvars;
    q3n_mission_hud *mission;
    q3n_hud *hud;
    qa_hud *messages;
    q3n_server_commands *commands;
    q3n_media *media;
    q3n_events *events;
    bool (*set)(void *, const qa_command_invocation *, const char *, const char *, qa_error *);
    bool (*send)(void *, const qa_command_invocation *, const char *, qa_error *);
    bool (*append)(void *, const qa_command_invocation *, const char *, qa_error *);
} q3n_cgame_command_services;
bool q3n_cgame_mission_command(const q3n_cgame_command_services *, const qa_command_invocation *, const q3n_frame *, qa_error *);

#endif
