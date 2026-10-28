/* Port of src/app/bootstrap/simulation/q3/host.ts. The created host
 * owns its Q3ServerState; engine/configstring closures become the
 * returned qa_app_q3_source_host over the wrapper below. Event text
 * fields are borrowed for the emit call. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_HOST_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_HOST_H
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include "qa/app/bootstrap/simulation/q3/types.h"

typedef enum qa_app_q3_source_event_kind {
  QA_APP_Q3_EVENT_PRINT, QA_APP_Q3_EVENT_LOG, QA_APP_Q3_EVENT_SERVER_COMMAND,
  QA_APP_Q3_EVENT_CONSOLE_COMMAND, QA_APP_Q3_EVENT_DROP_CLIENT, QA_APP_Q3_EVENT_CONFIGSTRING,
  QA_APP_Q3_EVENT_SOUND, QA_APP_Q3_EVENT_ENTITY, QA_APP_Q3_EVENT_PLAYER
} qa_app_q3_source_event_kind;
typedef struct qa_app_q3_source_event {
  qa_app_q3_source_event_kind kind;
  const char *text;          /* print/log/server-command/console-command */
  double client;             /* server-command/drop-client */
  const char *reason;        /* drop-client */
  const char *execution;     /* console-command: "append"|"now" */
  double index;              /* configstring */
  const char *value;         /* configstring */
  qa_actor_id actor;         /* sound */
  qa_vec3 origin, velocity;
  const char *path;
  double channel, volume;
  bool loop;
  qa_app_q3_source_entity_event entity;
  qa_app_q3_source_player_event player;
} qa_app_q3_source_event;

/* TS Q3HostOperations = Q3SourceHost minus engine/cvars/configstrings/
 * bots/entityEvent/serverState, plus bots/emit/clientNumber and the
 * session/now seams the factory reads. */
typedef struct qa_app_q3_host_operations {
  qa_app_q3_source_host base; /* engine/cvars/configstrings/server_state ignored */
  qa_app_q3_source_bots bots;
  void *emit_ctx;
  void (*emit)(void *ctx, qa_app_q3_source_event event);
  double (*client_number)(void *ctx, qa_actor_id actor);
  qa_session_id session;
} qa_app_q3_host_operations;

typedef struct qa_app_q3_created_source_host qa_app_q3_created_source_host;
qa_app_q3_created_source_host *qa_app_create_q3_source_host(qa_app_q3_host_operations operations,
    qa_app_q3_host_settings settings);
void qa_app_q3_created_source_host_destroy(qa_app_q3_created_source_host *host);
qa_app_q3_source_host *qa_app_q3_created_source_host_get(qa_app_q3_created_source_host *host);
qa_app_q3_server_state *qa_app_q3_created_source_host_server_state(qa_app_q3_created_source_host *host);
#endif
