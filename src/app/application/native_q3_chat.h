#ifndef QA_APPLICATION_NATIVE_Q3_CHAT_H
#define QA_APPLICATION_NATIVE_Q3_CHAT_H

#include "internal.h"

/* The native client dispatcher holds the provider's console lease and orders
 * these calls around the actual source intermission gate. */
bool application_native_q3_chat_command(qa_application *, application_provider *,
    qa_actor_id, const qa_command_invocation *, bool *handled, qa_error *);
bool application_native_q3_chat_intermission(qa_application *, application_provider *,
    qa_actor_id, const qa_command_invocation *, qa_error *);
bool application_native_q3_game_command(qa_application *, application_provider *,
    qa_actor_id, const qa_command_invocation *, qa_error *);

#endif
