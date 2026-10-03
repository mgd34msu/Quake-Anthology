#ifndef QA_APPLICATION_NATIVE_Q1_CONSOLE_H
#define QA_APPLICATION_NATIVE_Q1_CONSOLE_H
#include "internal.h"
#include "qa/q1_chat_commands.h"

bool application_native_q1_console_create(application_provider *, const qa_q1_options *, qa_error *);
bool application_native_q1_console_create_restored(application_provider *, qa_error *);
bool application_native_q1_console_destroy(application_provider *, qa_error *);
bool application_native_q1_console_idle(const application_provider *);
bool application_native_q1_console_capture(application_provider *, qa_buffer *, qa_error *);
bool application_native_q1_console_restore(application_provider *, qa_bytes, qa_error *);
bool application_native_q1_console_at(application_provider *, qa_console **, qa_cvars **,
                                      qa_command_context *);
qa_cvars *application_native_q1_console_registry(const application_provider *);
bool application_native_q1_chat(application_provider *, const qa_command_invocation *,
    qa_q1_chat_mode, qa_error *);
void application_native_q1_source_console_print(void *, const char *);
void application_native_q1_source_logfrag_write(void *, const char *);
bool application_native_q1_source_logfrag_enabled(application_provider *,bool *,qa_error *);
/* Borrow the actual engine dictionary until the Source console retires. */
bool application_native_q1_source_info(application_provider *,bool local,const char **,qa_error *);
/* SV_New reads only svs.info; an absent or empty *gamedir means qw. */
bool application_native_q1_source_visible_gamedir(application_provider *,const char **,qa_error *);
bool application_native_q1_source_files(application_provider *,qa_launch_source_files *,const char **,qa_error *);
bool application_native_q1_world_info(void *,const char *,qa_string_id *,qa_error *);
void application_native_q1_source_info_map_reset(void *);
bool application_native_q1_source_info_flush(application_provider *,qa_error *);
bool application_native_q1_cvar(void *, qa_string_id, float *, qa_error *);
bool application_native_q1_client_attack(void *, qa_actor_id, bool *);
#endif
