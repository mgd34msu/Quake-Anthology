#ifndef QA_APPLICATION_NATIVE_Q1_CONSOLE_H
#define QA_APPLICATION_NATIVE_Q1_CONSOLE_H
#include "internal.h"

bool application_native_q1_console_create(application_provider *, const qa_q1_options *, qa_error *);
bool application_native_q1_console_create_restored(application_provider *, qa_error *);
bool application_native_q1_console_destroy(application_provider *, qa_error *);
bool application_native_q1_console_idle(const application_provider *);
bool application_native_q1_console_capture(application_provider *, qa_buffer *, qa_error *);
bool application_native_q1_console_restore(application_provider *, qa_bytes, qa_error *);
bool application_native_q1_console_at(application_provider *, qa_console **, qa_cvars **,
                                      qa_command_context *);
qa_cvars *application_native_q1_console_registry(const application_provider *);
void application_native_q1_source_console_print(void *, const char *);
void application_native_q1_source_logfrag_write(void *, const char *);
bool application_native_q1_source_logfrag_enabled(application_provider *,bool *,qa_error *);
bool application_native_q1_cvar(void *, qa_string_id, float *, qa_error *);
bool application_native_q1_client_attack(void *, qa_actor_id, bool *);
#endif
