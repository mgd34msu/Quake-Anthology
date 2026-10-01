#ifndef QA_APPLICATION_NATIVE_Q3_SESSION_H
#define QA_APPLICATION_NATIVE_Q3_SESSION_H

#include "internal.h"

/* The source level owns newSession, GAME owns the fixed client session rows,
 * and the real source cvar registry owns session/sessionN text. These
 * producers add no independent copies and never run during private restore. */
bool application_native_q3_session_initialize(void *provider, qa_error *);
/* After the actual ClientConnect memset and CONNECTING write, before bot
 * admission, ClientConnect logging and ClientUserinfoChanged. */
bool application_native_q3_session_client_connect(application_provider *,
    qa_actor_id, bool first_time, const char *userinfo, qa_error *);
bool application_native_q3_session_read_client(application_provider *,
    qa_actor_id, qa_error *);
bool application_native_q3_session_write_client(application_provider *,
    uint32_t source_slot, qa_error *);
/* Actual WriteSessionData, including ExitLevel before clients become
 * CONNECTING. Session carry additionally writes every non-disconnected slot. */
bool application_native_q3_session_write_world(application_provider *, qa_error *);
/* The actual source session-carry capture writes CONNECTING clients too,
 * before capturing source cvars and before shutting down the old source. */
bool application_native_q3_session_capture_carry(application_provider *, qa_error *);

#endif
