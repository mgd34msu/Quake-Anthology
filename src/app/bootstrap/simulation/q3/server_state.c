/* Port of src/app/bootstrap/simulation/q3/server-state.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/contracts/common.h"
#include "qa/foundation/support.h"

struct qa_app_q3_server_state {
  qa_app_q3_server_state_options options;
  qa_cvar_registry *cvars;
  bool owns_cvars;
  char *values[1024];
  bool has_values[1024];
  char *userinfo[64];
  qa_app_q3_wire_user_command commands[64];
  bool has_commands[64];
};

static char *dup_str(const char *value) {
  size_t length = strlen(value) + 1;
  char *copy = malloc(length);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, value, length);
  return copy;
}
static void print_cb(void *v, const char *text) {
  qa_app_q3_server_state *state = (qa_app_q3_server_state *)v;
  state->options.print(state->options.ctx, text);
}
static void set_cvar(qa_app_q3_server_state *state, const char *name, const char *value) {
  qa_cvar_snapshot result; qa_cvar_registry_set(state->cvars, name, value, true, &result);
}

qa_app_q3_server_state *qa_app_q3_server_state_create(qa_app_q3_server_state_options options) {
  qa_app_q3_server_state *state = calloc(1, sizeof *state);
  if (!state) return NULL;
  state->options = options;
  qa_app_q3_host_settings *settings = &options.settings;
  if (settings->source_registry) {
    state->cvars = settings->source_registry;
  } else {
    qa_cvar_registry_options registry_options; memset(&registry_options, 0, sizeof registry_options);
    registry_options.dialect = QA_COMMAND_DIALECT_Q3;
    registry_options.context.session = options.session;
    registry_options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
    registry_options.print = print_cb;
    registry_options.print_ctx = state;
    state->cvars = qa_cvar_registry_create(&registry_options);
    state->owns_cvars = true;
    if (!state->cvars) { free(state); return NULL; }
  }
  qa_app_q3_server_cvar_settings cvar_settings = {settings->max_clients, settings->map_name};
  qa_app_register_q3_server_cvars(state->cvars, cvar_settings);
  set_cvar(state, "sv_mapname", settings->map_name);
  if (!settings->source_registry)
    qa_cvar_registry_apply_archive(state->cvars, settings->source_archive, settings->source_archive_count);
  char max_clients[32];
  snprintf(max_clients, sizeof max_clients, "%lld", (long long)settings->max_clients);
  set_cvar(state, "sv_maxclients", max_clients);
  set_cvar(state, "sv_mapname", settings->map_name);
  set_cvar(state, "mapname", settings->map_name);
  for (size_t i = 0; i < settings->cvar_count; i++) set_cvar(state, settings->cvars[i].name, settings->cvars[i].value);
  qa_app_q3_server_state_refresh_server_info(state);
  return state;
}
void qa_app_q3_server_state_destroy(qa_app_q3_server_state *state) {
  if (!state) return;
  for (size_t i = 0; i < 1024; i++) free(state->values[i]);
  for (size_t i = 0; i < 64; i++) free(state->userinfo[i]);
  if (state->owns_cvars) qa_cvar_registry_destroy(state->cvars);
  free(state);
}
qa_cvar_registry *qa_app_q3_server_state_cvars(qa_app_q3_server_state *state) { return state->cvars; }
char *qa_app_q3_server_state_server_info(qa_app_q3_server_state *state) {
  return qa_cvar_registry_info_string(state->cvars, QA_CVAR_FLAG_SERVER_INFO, -1);
}
const char *qa_app_q3_server_state_refresh_server_info(qa_app_q3_server_state *state) {
  char *value = qa_app_q3_server_state_server_info(state);
  if (state->has_values[0] && value && strcmp(state->values[0], value) == 0) { free(value); return NULL; }
  free(state->values[0]);
  state->values[0] = value;
  state->has_values[0] = true;
  return state->values[0];
}
static void config_index(double index) {
  if (index != (double)(long long)index || index < 0 || index >= 1024)
    qa_range_error("Q3 configstring outside source range");
}
const char *qa_app_q3_server_state_configstring_get(qa_app_q3_server_state *state, double index) {
  config_index(index);
  return state->has_values[(size_t)index] ? state->values[(size_t)index] : "";
}
void qa_app_q3_server_state_configstring_set(qa_app_q3_server_state *state, double index, const char *value) {
  config_index(index);
  free(state->values[(size_t)index]);
  state->values[(size_t)index] = dup_str(value);
  state->has_values[(size_t)index] = true;
}
bool qa_app_q3_server_state_has_configstring(qa_app_q3_server_state *state, double index) {
  config_index(index);
  return state->has_values[(size_t)index];
}
void qa_app_q3_server_state_print(qa_app_q3_server_state *state, const char *text) {
  state->options.print(state->options.ctx, text);
}
const char *qa_app_q3_server_state_get_userinfo(qa_app_q3_server_state *state, double slot) {
  if (slot < 0 || slot >= 64 || slot != (double)(long long)slot) return NULL;
  return state->userinfo[(size_t)slot];
}
void qa_app_q3_server_state_set_userinfo(qa_app_q3_server_state *state, double slot, const char *value) {
  if (slot < 0 || slot >= 64 || slot != (double)(long long)slot) return;
  free(state->userinfo[(size_t)slot]);
  state->userinfo[(size_t)slot] = dup_str(value);
}
bool qa_app_q3_server_state_get_user_command(qa_app_q3_server_state *state, double slot, qa_app_q3_wire_user_command *out) {
  if (slot < 0 || slot >= 64 || slot != (double)(long long)slot || !state->has_commands[(size_t)slot]) return false;
  *out = state->commands[(size_t)slot];
  return true;
}
void qa_app_q3_server_state_set_user_command(qa_app_q3_server_state *state, double slot, qa_app_q3_wire_user_command value) {
  if (slot < 0 || slot >= 64 || slot != (double)(long long)slot) return;
  qa_app_q3_wire_user_command filtered = state->options.filter_user_command
      ? state->options.filter_user_command(state->options.ctx, slot, value) : value;
  state->commands[(size_t)slot] = filtered;
  state->has_commands[(size_t)slot] = true;
}
void qa_app_q3_server_state_clear_client(qa_app_q3_server_state *state, double slot) {
  if (slot < 0 || slot >= 64 || slot != (double)(long long)slot) return;
  free(state->userinfo[(size_t)slot]); state->userinfo[(size_t)slot] = NULL;
  state->has_commands[(size_t)slot] = false;
}

static qa_save_value *slot_string(double slot, const char *value) {
  qa_save_value *object = qa_save_value_object();
  if (!object) return NULL;
  if (!qa_save_value_set(object, "slot", qa_save_value_number(slot))
      || !qa_save_value_set(object, "value", qa_save_value_string(value))) { qa_save_value_free(object); return NULL; }
  return object;
}
qa_save_value *qa_app_q3_server_state_capture_save_state(qa_app_q3_server_state *state) {
  qa_save_value *root = qa_save_value_object();
  qa_save_value *cvars = qa_cvar_registry_capture_save_state(state->cvars);
  qa_save_value *configstrings = qa_save_value_array(), *userinfo = qa_save_value_array(), *commands = qa_save_value_array();
  bool ok = root && cvars && configstrings && userinfo && commands && qa_save_value_set(root, "cvars", cvars);
  for (size_t i = 0; ok && i < 1024; i++) if (state->has_values[i]) ok = qa_save_value_push(configstrings, slot_string((double)i, state->values[i]));
  for (size_t i = 0; ok && i < 64; i++) if (state->userinfo[i]) ok = qa_save_value_push(userinfo, slot_string((double)i, state->userinfo[i]));
  for (size_t i = 0; ok && i < 64; i++) {
    if (!state->has_commands[i]) continue;
    qa_app_q3_wire_user_command command = state->commands[i];
    qa_save_value *entry = qa_save_value_object(), *value = qa_save_value_object(), *angles = qa_save_value_array();
    ok = entry && value && angles
        && qa_save_value_push(angles, qa_save_value_number(command.angles.x))
        && qa_save_value_push(angles, qa_save_value_number(command.angles.y))
        && qa_save_value_push(angles, qa_save_value_number(command.angles.z))
        && qa_save_value_set(value, "serverTime", qa_save_value_number(command.server_time))
        && qa_save_value_set(value, "angles", angles)
        && qa_save_value_set(value, "forwardmove", qa_save_value_number(command.forwardmove))
        && qa_save_value_set(value, "rightmove", qa_save_value_number(command.rightmove))
        && qa_save_value_set(value, "upmove", qa_save_value_number(command.upmove))
        && qa_save_value_set(value, "buttons", qa_save_value_number(command.buttons))
        && qa_save_value_set(value, "weapon", qa_save_value_number(command.weapon))
        && qa_save_value_set(entry, "slot", qa_save_value_number((double)i))
        && qa_save_value_set(entry, "value", value)
        && qa_save_value_push(commands, entry);
    if (!ok) { qa_save_value_free(entry); }
  }
  ok = ok && qa_save_value_set(root, "configstrings", configstrings)
      && qa_save_value_set(root, "userinfo", userinfo)
      && qa_save_value_set(root, "commands", commands);
  if (!ok) { qa_save_value_free(root); return NULL; }
  return root;
}

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
static qa_app_q3_wire_user_command read_command(const qa_save_reader *reader) {
  qa_save_reader angles = field(reader, "angles");
  size_t count = qa_save_reader_list_count(&angles);
  double values[3] = {0, 0, 0};
  for (size_t i = 0; i < count && i < 3; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&angles, i, &item)) qa_range_error("Out of memory");
    values[i] = qa_save_reader_integer(&item);
    qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&angles);
  if (count != 3) qa_save_reader_fail(reader, "command requires three angles");
  qa_app_q3_wire_user_command command; memset(&command, 0, sizeof command);
  qa_save_reader r = field(reader, "serverTime"); command.server_time = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
  command.angles = (qa_vec3){values[0], values[1], values[2]};
  r = field(reader, "forwardmove"); command.forwardmove = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
  r = field(reader, "rightmove"); command.rightmove = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
  r = field(reader, "upmove"); command.upmove = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
  r = field(reader, "buttons"); command.buttons = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
  r = field(reader, "weapon"); command.weapon = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
  return command;
}
void qa_app_q3_server_state_restore_save_state(qa_app_q3_server_state *state, const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "q3.server")) qa_range_error("Out of memory");
  typedef struct slot_value { double slot; const char *text; qa_app_q3_wire_user_command command; } slot_value;
  slot_value config[1024]; size_t config_count = 0;
  slot_value info[64]; size_t info_count = 0;
  slot_value cmds[64]; size_t cmd_count = 0;
  const char *names[3] = {"configstrings", "userinfo", "commands"};
  double maxima[3] = {1024, 64, 64};
  for (int group = 0; group < 3; group++) {
    qa_save_reader list = field(&reader, names[group]);
    size_t count = qa_save_reader_list_count(&list);
    bool seen[1024] = {0};
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry;
      if (!qa_save_reader_item(&list, i, &entry)) qa_range_error("Out of memory");
      qa_save_reader slot_reader = field(&entry, "slot");
      double slot = qa_save_reader_integer_min(&slot_reader, 0);
      qa_save_reader_destroy(&slot_reader);
      if (slot >= maxima[group] || seen[(size_t)slot]) qa_save_reader_fail(&list, "invalid or duplicate source storage slot");
      seen[(size_t)slot] = true;
      qa_save_reader value_reader = field(&entry, "value");
      slot_value parsed; memset(&parsed, 0, sizeof parsed); parsed.slot = slot;
      if (group == 2) parsed.command = read_command(&value_reader);
      else parsed.text = qa_save_reader_string(&value_reader);
      qa_save_reader_destroy(&value_reader);
      if (group == 0) config[config_count++] = parsed;
      else if (group == 1) info[info_count++] = parsed;
      else cmds[cmd_count++] = parsed;
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
  }
  qa_save_reader cvars = field(&reader, "cvars");
  qa_cvar_registry_restore_save_state(state->cvars, cvars.value);
  qa_save_reader_destroy(&cvars);
  for (size_t i = 0; i < 1024; i++) { free(state->values[i]); state->values[i] = NULL; state->has_values[i] = false; }
  for (size_t i = 0; i < config_count; i++) {
    state->values[(size_t)config[i].slot] = dup_str(config[i].text);
    state->has_values[(size_t)config[i].slot] = true;
  }
  for (size_t i = 0; i < 64; i++) { free(state->userinfo[i]); state->userinfo[i] = NULL; state->has_commands[i] = false; }
  for (size_t i = 0; i < info_count; i++) state->userinfo[(size_t)info[i].slot] = dup_str(info[i].text);
  for (size_t i = 0; i < cmd_count; i++) {
    state->commands[(size_t)cmds[i].slot] = cmds[i].command;
    state->has_commands[(size_t)cmds[i].slot] = true;
  }
  qa_save_reader_destroy(&reader);
}
