/* Port of src/app/bootstrap/simulation/q3/host.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

struct qa_app_q3_created_source_host {
  qa_app_q3_host_operations operations;
  qa_app_q3_server_state *server_state;
  qa_app_q3_source_host host;
  qa_app_q3_host_cvar *cvars;
  char *gametype_value, *single_player_value;
};

static void emit_event(qa_app_q3_created_source_host *created, qa_app_q3_source_event event) {
  created->operations.emit(created->operations.emit_ctx, event);
}
static void print_sink(void *v, const char *text) {
  qa_app_q3_source_event event; memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_PRINT; event.text = text;
  emit_event((qa_app_q3_created_source_host *)v, event);
}
static double now_cb(void *v) {
  qa_app_q3_created_source_host *created = (qa_app_q3_created_source_host *)v;
  return created->operations.base.now(created->operations.base.ctx);
}
static void engine_print(void *v, const char *text) { print_sink(v, text); }
static void engine_log(void *v, const char *text) {
  qa_app_q3_source_event event; memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_LOG; event.text = text;
  emit_event((qa_app_q3_created_source_host *)v, event);
}
static void engine_server_command(void *v, double client, const char *text) {
  qa_app_q3_source_event event; memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_SERVER_COMMAND; event.client = client; event.text = text;
  emit_event((qa_app_q3_created_source_host *)v, event);
}
static void engine_drop(void *v, double client, const char *reason) {
  qa_app_q3_source_event event; memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_DROP_CLIENT; event.client = client; event.reason = reason;
  emit_event((qa_app_q3_created_source_host *)v, event);
}
static char *engine_userinfo(void *v, double client) {
  qa_app_q3_created_source_host *created = (qa_app_q3_created_source_host *)v;
  const char *stored = qa_app_q3_server_state_get_userinfo(created->server_state, client);
  char buffer[256];
  if (!stored) snprintf(buffer, sizeof buffer, "\\name\\Player %lld\\ip\\localhost\\handicap\\100\\model\\sarge/default\\team_model\\sarge/default", (long long)client + 1);
  else snprintf(buffer, sizeof buffer, "%s", stored);
  size_t length = strlen(buffer) + 1;
  char *copy = malloc(length);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, buffer, length);
  return copy;
}
static void engine_set_userinfo(void *v, double client, const char *value) {
  qa_app_q3_created_source_host *created = (qa_app_q3_created_source_host *)v;
  qa_app_q3_server_state_set_userinfo(created->server_state, client, value);
}
static qa_q3_base_user_command engine_user_command(void *v, double client) {
  qa_app_q3_created_source_host *created = (qa_app_q3_created_source_host *)v;
  qa_app_q3_wire_user_command command;
  if (!qa_app_q3_server_state_get_user_command(created->server_state, client, &command)) {
    memset(&command, 0, sizeof command);
    command.server_time = created->operations.base.now(created->operations.base.ctx);
    command.weapon = 2;
  }
  return command;
}
static void engine_append(void *v, const char *text) {
  qa_app_q3_source_event event; memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_CONSOLE_COMMAND; event.execution = "append"; event.text = text;
  emit_event((qa_app_q3_created_source_host *)v, event);
}
static void engine_now(void *v, const char *text) {
  qa_app_q3_source_event event; memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_CONSOLE_COMMAND; event.execution = "now"; event.text = text;
  emit_event((qa_app_q3_created_source_host *)v, event);
}
static const char *configstring_get(void *v, double index) {
  qa_app_q3_created_source_host *created = (qa_app_q3_created_source_host *)v;
  return qa_app_q3_server_state_configstring_get(created->server_state, index);
}
static void configstring_set(void *v, double index, const char *value) {
  qa_app_q3_created_source_host *created = (qa_app_q3_created_source_host *)v;
  if (qa_app_q3_server_state_has_configstring(created->server_state, index)
      && strcmp(qa_app_q3_server_state_configstring_get(created->server_state, index), value) == 0) return;
  qa_app_q3_server_state_configstring_set(created->server_state, index, value);
  qa_app_q3_source_event event; memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_CONFIGSTRING; event.index = index; event.value = value;
  emit_event(created, event);
}
/* entityEvent and sourceCommand are the two closures createQ3SourceHost
 * replaces; they take the created host as their own context. */
static void entity_event_cb(void *v, qa_app_q3_source_entity_event entity) {
  qa_app_q3_source_event event; memset(&event, 0, sizeof event);
  event.kind = QA_APP_Q3_EVENT_ENTITY; event.entity = entity;
  emit_event((qa_app_q3_created_source_host *)v, event);
}
static qa_q3_base_user_command source_command_cb(void *v, qa_actor_command input) {
  qa_app_q3_created_source_host *created = v;
  qa_q3_base_user_command command = created->operations.base.source_command(created->operations.base.ctx, input);
  double client = created->operations.client_number(created->operations.emit_ctx, input.actor);
  qa_app_q3_wire_user_command wire = command;
  qa_app_q3_server_state_set_user_command(created->server_state, client, wire);
  return command;
}

qa_app_q3_created_source_host *qa_app_create_q3_source_host(qa_app_q3_host_operations operations,
    qa_app_q3_host_settings settings) {
  qa_app_q3_created_source_host *created = calloc(1, sizeof *created);
  if (!created) return NULL;
  created->operations = operations;
  size_t capacity = settings.cvar_count + 2;
  created->cvars = calloc(capacity, sizeof *created->cvars);
  created->gametype_value = malloc(32);
  created->single_player_value = malloc(2);
  if (!created->cvars || !created->gametype_value || !created->single_player_value) qa_range_error("Out of memory");
  snprintf(created->gametype_value, 32, "%s", QA_JS_NUMBER(settings.game_type));
  snprintf(created->single_player_value, 2, "%s", settings.single_player ? "1" : "0");
  size_t count = 0;
  if (!settings.source_registry) created->cvars[count++] = (qa_app_q3_host_cvar){"g_gametype", created->gametype_value};
  created->cvars[count++] = (qa_app_q3_host_cvar){"ui_singlePlayerActive", created->single_player_value};
  for (size_t i = 0; i < settings.cvar_count; i++) created->cvars[count++] = settings.cvars[i];
  qa_app_q3_host_settings state_settings = settings;
  state_settings.cvars = created->cvars;
  state_settings.cvar_count = count;
  qa_app_q3_server_state_options state_options; memset(&state_options, 0, sizeof state_options);
  state_options.session = operations.session;
  state_options.settings = state_settings;
  state_options.ctx = created;
  state_options.now = now_cb;
  state_options.print = print_sink;
  created->server_state = qa_app_q3_server_state_create(state_options);
  if (!created->server_state) { free(created->cvars); free(created->gametype_value); free(created->single_player_value); free(created); return NULL; }
  created->host = operations.base;
  created->host.server_state = created->server_state;
  created->host.cvars = qa_app_q3_server_state_cvars(created->server_state);
  created->host.bots = operations.bots;
  created->host.entity_event_ctx = created;
  created->host.entity_event = entity_event_cb;
  created->host.configstrings_ctx = created;
  created->host.configstring_get = configstring_get;
  created->host.configstring_set = configstring_set;
  created->host.engine.ctx = created;
  created->host.engine.print = engine_print;
  created->host.engine.log = engine_log;
  created->host.engine.send_server_command = engine_server_command;
  created->host.engine.drop_client = engine_drop;
  created->host.engine.get_userinfo = engine_userinfo;
  created->host.engine.set_userinfo = engine_set_userinfo;
  created->host.engine.get_user_command = engine_user_command;
  created->host.engine.append_console_command = engine_append;
  created->host.engine.execute_console_now = engine_now;
  created->host.source_command_ctx = created;
  created->host.source_command = source_command_cb;
  return created;
}
void qa_app_q3_created_source_host_destroy(qa_app_q3_created_source_host *host) {
  if (!host) return;
  qa_app_q3_server_state_destroy(host->server_state);
  free(host->cvars); free(host->gametype_value); free(host->single_player_value);
  free(host);
}
qa_app_q3_source_host *qa_app_q3_created_source_host_get(qa_app_q3_created_source_host *host) { return &host->host; }
qa_app_q3_server_state *qa_app_q3_created_source_host_server_state(qa_app_q3_created_source_host *host) { return host->server_state; }
