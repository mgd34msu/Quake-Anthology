#ifndef QA_Q3_NATIVE_SERVER_COMMANDS_INTERNAL_H
#define QA_Q3_NATIVE_SERVER_COMMANDS_INTERNAL_H
#include "server_commands.h"
#include "voice.h"
#include "events.h"
#include "qa/network_q3.h"
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct q3n_server_commands {
    q3n_server_command_options options;
    q3n_command_state state;
    q3n_voice voice;
    const q3n_compiled_source_rebind_ticket *rebind;
    qa_command_context rebound_context;
    bool initialized, busy, closed;
};
bool q3nc_fail(qa_error *, qa_status, const char *);
bool q3nc_current(q3n_server_commands *, const q3n_frame *, qa_error *);
bool q3nc_cvar(q3n_server_commands *, const char *, qa_native_q3_client_cvar *, qa_error *);
bool q3nc_message(q3n_server_commands *, const q3n_frame *, q3n_command_message_kind,
    const char *, int32_t sender, const char *voice_command, qa_error *);
bool q3nc_team_chat(q3n_server_commands *, const q3n_frame *, const char *, qa_error *);
bool q3nc_sound(q3n_server_commands *, const q3n_frame *, int32_t, int32_t, qa_error *);
bool q3nc_same(const char *, const char *);
void q3nc_copy(char *, size_t, const char *);
int32_t q3nc_integer(const char *);
int32_t q3nc_add(int32_t, int32_t);
int32_t q3nc_sub(int32_t, int32_t);
#endif
