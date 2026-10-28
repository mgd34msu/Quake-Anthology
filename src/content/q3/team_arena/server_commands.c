/* Port of src/content/q3/team-arena/server-commands.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/server_commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/team_arena/commands.h"
#include "qa/foundation/support.h"

enum { MAX_BAN_STRING = 256 };
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static char *dup_str(const char *v) {
  size_t n = strlen(v ? v : "") + 1;
  char *out = malloc(n);
  if (out) memcpy(out, v ? v : "", n);
  return out;
}
static void lower_inplace(char *text) {
  for (char *p = text; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
}
/* TS argument(): missing is "", truncated to 1023 bytes. Malloc'd. */
static char *argument(const char *const *argv, size_t argc, size_t index) {
  const char *value = index < argc ? argv[index] : "";
  size_t n = strlen(value);
  if (n > 1023) n = 1023;
  char *out = malloc(n + 1);
  if (!out) qa_range_error("out of memory");
  memcpy(out, value, n);
  out[n] = '\0';
  return out;
}
static void print_fmt(qa_q3_ta_game_server_command_runtime *r, const char *f,
    const qa_q3_game_format_arg *a, size_t n) {
  char *t = qa_q3_game_format(f, a, n, 32000);
  r->host.print(r->host.ctx, t);
  free(t);
}

/* TS stringToFilter: returns false for TS null. */
static bool string_to_filter(qa_q3_ta_game_server_command_runtime *r, const char *text, qa_q3_ta_ip_filter *out) {
  size_t length = strlen(text), offset = 0;
  uint32_t mask = 0, compare = 0;
  for (int i = 0; i < 4; i++) {
    char first = offset < length ? text[offset] : '\0';
    if (!is_digit(first)) {
      if (first == '*') {
        offset++;
        if (offset == length) break;
        offset++;
        continue;
      }
      qa_q3_game_format_arg a = qa_q3_game_format_string(text + offset);
      (void)a;
      char buffer[1200];
      (void)snprintf(buffer, sizeof buffer, "Bad filter address: %s\n", text + offset);
      r->host.print(r->host.ctx, buffer);
      return false;
    }
    size_t start = offset;
    while (offset < length && is_digit(text[offset])) offset++;
    if (offset - start >= 128)
      qa_range_error("IP filter component exceeds the source127-digit scratch buffer");
    char component[129];
    memcpy(component, text + start, offset - start);
    component[offset - start] = '\0';
    compare |= (uint32_t)(qa_game_atoi(component) & 255) << (i * 8);
    mask |= 255u << (i * 8);
    if (offset == length) break;
    offset++;
  }
  out->mask = mask;
  out->compare = compare;
  return true;
}

void qa_q3_ta_game_server_command_state_init(qa_q3_ta_game_server_command_state *s) {
  s->filters = NULL; s->filter_count = 0; s->filter_capacity = 0;
}
void qa_q3_ta_game_server_command_state_destroy(qa_q3_ta_game_server_command_state *s) {
  free(s->filters); s->filters = NULL; s->filter_count = s->filter_capacity = 0;
}
void qa_q3_ta_server_command_save_state_destroy(qa_q3_ta_server_command_save_state *s) {
  free(s->filters); free(s->ban_value); s->filters = NULL; s->ban_value = NULL;
}
void qa_q3_ta_game_server_command_runtime_init(qa_q3_ta_game_server_command_runtime *r,
    qa_q3_entity_pool *pool, qa_cvar_registry *cvars, qa_q3_ta_game_server_command_host host,
    qa_q3_ta_game_server_command_state *state) {
  r->pool = pool; r->cvars = cvars; r->host = host;
  qa_q3_ta_game_server_command_state_init(&r->owned_state);
  r->state = state ? state : &r->owned_state;
  r->has_ban_vm_string = false; r->ban_modification_count = 0; r->ban_value = NULL;
}
void qa_q3_ta_game_server_command_runtime_destroy(qa_q3_ta_game_server_command_runtime *r) {
  qa_q3_ta_game_server_command_state_destroy(&r->owned_state);
  free(r->ban_value);
  r->ban_value = NULL;
}
qa_q3_ta_server_command_save_state qa_q3_ta_game_server_command_runtime_capture(qa_q3_ta_game_server_command_runtime *r) {
  qa_q3_ta_server_command_save_state out;
  out.filter_count = r->state->filter_count;
  out.filters = NULL;
  if (out.filter_count) {
    out.filters = malloc(out.filter_count * sizeof *out.filters);
    if (!out.filters) qa_range_error("out of memory");
    memcpy(out.filters, r->state->filters, out.filter_count * sizeof *out.filters);
  }
  out.has_ban_vm_string = r->has_ban_vm_string;
  out.ban_modification_count = r->ban_modification_count;
  out.ban_value = r->has_ban_vm_string ? dup_str(r->ban_value) : NULL;
  return out;
}
void qa_q3_ta_game_server_command_runtime_restore(qa_q3_ta_game_server_command_runtime *r, const qa_save_value *value) {
  qa_save_reader reader, filters_reader, ban;
  if (!qa_save_reader_init(&reader, value, "q3.serverCommands")) qa_range_error("out of memory");
  if (!qa_save_reader_field(&reader, "filters", &filters_reader)) qa_range_error("out of memory");
  size_t count = qa_save_reader_list_count(&filters_reader);
  qa_q3_ta_ip_filter *filters = count ? malloc(count * sizeof *filters) : NULL;
  if (count && !filters) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, mask, compare;
    if (!qa_save_reader_item(&filters_reader, i, &item)) qa_range_error("out of memory");
    if (!qa_save_reader_field(&item, "mask", &mask)) qa_range_error("out of memory");
    double mask_value = qa_save_reader_integer_min(&mask, 0);
    qa_save_reader_destroy(&mask);
    if (!qa_save_reader_field(&item, "compare", &compare)) qa_range_error("out of memory");
    double compare_value = qa_save_reader_integer_min(&compare, 0);
    qa_save_reader_destroy(&compare);
    qa_save_reader_destroy(&item);
    filters[i].mask = (uint32_t)mask_value;
    filters[i].compare = (uint32_t)compare_value;
    if (mask_value > 4294967295.0 || compare_value > 4294967295.0) {
      /* validated after the full read below, like the TS */
      filters[i].mask = 0; /* placeholder, fail follows */
      qa_save_reader_destroy(&filters_reader);
      free(filters);
      qa_save_reader_fail(&reader, "too many IP filters");
    }
  }
  qa_save_reader_destroy(&filters_reader);
  if (!qa_save_reader_field(&reader, "banVmString", &ban)) qa_range_error("out of memory");
  bool has_ban = !qa_save_reader_is_null(&ban);
  double ban_count = 0;
  char *ban_value = NULL;
  if (has_ban) {
    qa_save_reader count_reader, value_reader;
    if (!qa_save_reader_field(&ban, "modificationCount", &count_reader)) qa_range_error("out of memory");
    ban_count = qa_save_reader_integer(&count_reader);
    qa_save_reader_destroy(&count_reader);
    if (!qa_save_reader_field(&ban, "value", &value_reader)) qa_range_error("out of memory");
    ban_value = dup_str(qa_save_reader_string(&value_reader));
    qa_save_reader_destroy(&value_reader);
  }
  qa_save_reader_destroy(&ban);
  if (count > QA_Q3_TA_MAX_IP_FILTERS) {
    free(filters); free(ban_value);
    qa_save_reader_fail(&reader, "too many IP filters");
  }
  qa_save_reader_destroy(&reader);
  free(r->state->filters);
  r->state->filters = filters;
  r->state->filter_count = count;
  r->state->filter_capacity = count;
  free(r->ban_value);
  r->has_ban_vm_string = has_ban;
  r->ban_modification_count = ban_count;
  r->ban_value = ban_value;
}

static qa_cvar_snapshot cvar_of(qa_q3_ta_game_server_command_runtime *r, const char *name) {
  return r->host.read_vm_cvar(r->host.ctx, name);
}
static void update_ip_bans(qa_q3_ta_game_server_command_runtime *r) {
  char list[MAX_BAN_STRING + 32] = "";
  size_t length = 0;
  for (size_t i = 0; i < r->state->filter_count; i++) {
    qa_q3_ta_ip_filter *filter = &r->state->filters[i];
    if (filter->compare == QA_Q3_TA_FREE_FILTER) continue;
    char address[32] = "";
    size_t alen = 0;
    for (int byte = 0; byte < 4; byte++) {
      char part[8];
      if (((filter->mask >> (byte * 8)) & 255) == 255)
        (void)snprintf(part, sizeof part, "%u", (filter->compare >> (byte * 8)) & 255);
      else strcpy(part, "*");
      size_t plen = strlen(part);
      memcpy(address + alen, part, plen);
      alen += plen;
      address[alen++] = byte < 3 ? '.' : ' ';
      address[alen] = '\0';
    }
    if (length + alen >= MAX_BAN_STRING) {
      r->host.print(r->host.ctx, "g_banIPs overflowed at MAX_CVAR_VALUE_STRING\n");
      break;
    }
    memcpy(list + length, address, alen + 1);
    length += alen;
  }
  qa_cvar_snapshot out;
  (void)qa_cvar_registry_set(r->cvars, "g_banIPs", list, true, &out);
  qa_cvar_read_free(&out);
}
static void add_ip(qa_q3_ta_game_server_command_runtime *r, const char *text) {
  size_t index = r->state->filter_count;
  for (size_t i = 0; i < r->state->filter_count; i++)
    if (r->state->filters[i].compare == QA_Q3_TA_FREE_FILTER) { index = i; break; }
  if (index == QA_Q3_TA_MAX_IP_FILTERS) { r->host.print(r->host.ctx, "IP filter list is full\n"); return; }
  qa_q3_ta_ip_filter parsed;
  bool ok = string_to_filter(r, text, &parsed);
  if (index == r->state->filter_count) {
    if (r->state->filter_count == r->state->filter_capacity) {
      size_t capacity = r->state->filter_capacity ? r->state->filter_capacity * 2 : 16;
      qa_q3_ta_ip_filter *grown = realloc(r->state->filters, capacity * sizeof *grown);
      if (!grown) qa_range_error("out of memory");
      r->state->filters = grown;
      r->state->filter_capacity = capacity;
    }
    r->state->filters[r->state->filter_count].mask = 0;
    r->state->filters[r->state->filter_count].compare = QA_Q3_TA_FREE_FILTER;
    r->state->filter_count++;
  }
  qa_q3_ta_ip_filter *selected = &r->state->filters[index];
  if (!ok) selected->compare = QA_Q3_TA_FREE_FILTER;
  else { selected->mask = parsed.mask; selected->compare = parsed.compare; }
  update_ip_bans(r);
}
void qa_q3_ta_game_server_command_runtime_process_ip_bans(qa_q3_ta_game_server_command_runtime *r) {
  qa_cvar_snapshot snapshot = cvar_of(r, "g_banIPs");
  if (!r->has_ban_vm_string || r->ban_modification_count != snapshot.modification_count) {
    free(r->ban_value);
    r->ban_value = dup_str(snapshot.value);
    r->ban_modification_count = snapshot.modification_count;
    r->has_ban_vm_string = true;
  }
  qa_cvar_read_free(&snapshot);
  char *input = dup_str(r->ban_value);
  size_t length = strlen(input);
  if (length >= MAX_BAN_STRING) {
    free(input);
    qa_range_error("g_banIPs exceeds the source256-byte vmCvar buffer");
  }
  char *first_space = strchr(input, ' ');
  if (first_space) {
    free(r->ban_value);
    r->ban_value = dup_str(input);
    r->ban_value[first_space - input] = '\0';
  }
  size_t start = 0;
  while (start < length) {
    char *space = strchr(input + start, ' ');
    if (!space) break;
    size_t space_index = (size_t)(space - input);
    if (space_index > start) {
      char *token = malloc(space_index - start + 1);
      if (!token) { free(input); qa_range_error("out of memory"); }
      memcpy(token, input + start, space_index - start);
      token[space_index - start] = '\0';
      add_ip(r, token);
      free(token);
    }
    start = space_index;
    while (start < length && input[start] == ' ') start++;
  }
  free(input);
}
bool qa_q3_ta_game_server_command_runtime_filter_packet(qa_q3_ta_game_server_command_runtime *r, const char *from) {
  size_t length = strlen(from);
  if (length > 1023) qa_range_error("IP address exceeds the source userinfo bound");
  size_t offset = 0;
  int initialized = 0;
  uint32_t incoming = 0;
  while (offset < length && initialized < 4) {
    uint32_t byte = 0;
    while (offset < length && is_digit(from[offset])) {
      byte = (byte * 10 + (uint32_t)(from[offset] - '0')) & 255;
      offset++;
    }
    incoming |= byte << (initialized * 8);
    initialized++;
    if (offset == length || from[offset] == ':') break;
    offset++;
  }
  qa_cvar_snapshot snap = cvar_of(r, "g_filterBan");
  bool deny_matches = snap.integer_value != 0;
  qa_cvar_read_free(&snap);
  uint32_t initialized_mask = initialized == 4 ? 0xffffffffu
      : (uint32_t)(((uint64_t)1 << (initialized * 8)) - 1);
  for (size_t i = 0; i < r->state->filter_count; i++) {
    qa_q3_ta_ip_filter *filter = &r->state->filters[i];
    if ((incoming & filter->mask & initialized_mask) != (filter->compare & initialized_mask)) continue;
    if ((filter->mask & ~initialized_mask) != 0)
      qa_range_error("IP filter reads uninitialized source octets");
    if ((incoming & filter->mask) == filter->compare) return deny_matches;
  }
  return !deny_matches;
}
static void remove_ip(qa_q3_ta_game_server_command_runtime *r, const char *const *argv, size_t argc) {
  if (argc < 2) { r->host.print(r->host.ctx, "Usage:  sv removeip <ip-mask>\n"); return; }
  char *text = argument(argv, argc, 1);
  qa_q3_ta_ip_filter parsed;
  if (!string_to_filter(r, text, &parsed)) { free(text); return; }
  for (size_t i = 0; i < r->state->filter_count; i++) {
    qa_q3_ta_ip_filter *filter = &r->state->filters[i];
    if (filter->mask != parsed.mask || filter->compare != parsed.compare) continue;
    filter->compare = QA_Q3_TA_FREE_FILTER;
    r->host.print(r->host.ctx, "Removed.\n");
    update_ip_bans(r);
    free(text);
    return;
  }
  char buffer[1200];
  (void)snprintf(buffer, sizeof buffer, "Didn't find %s.\n", text);
  r->host.print(r->host.ctx, buffer);
  free(text);
}
qa_q3_game_client *qa_q3_ta_game_server_command_runtime_client_for_string(qa_q3_ta_game_server_command_runtime *r, const char *value) {
  if (is_digit(value[0])) {
    double slot = qa_game_atoi(value);
    if (slot < 0 || slot >= r->pool->max_clients) {
      qa_q3_game_format_arg a = qa_q3_game_format_number(slot);
      print_fmt(r, "Bad client slot: %i\n", &a, 1);
      return NULL;
    }
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->pool, slot);
    if (client->pers.connected == QA_Q3_CONN_DISCONNECTED) {
      qa_q3_game_format_arg a = qa_q3_game_format_number(slot);
      print_fmt(r, "Client %i is not connected\n", &a, 1);
      return NULL;
    }
    return client;
  }
  char *wanted = dup_str(value);
  lower_inplace(wanted);
  for (double i = 0; i < r->pool->max_clients; i++) {
    qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->pool, i);
    if (client->pers.connected != QA_Q3_CONN_DISCONNECTED) {
      char *name = dup_str(client->pers.netname ? client->pers.netname : "");
      lower_inplace(name);
      bool match = strcmp(name, wanted) == 0;
      free(name);
      if (match) { free(wanted); return client; }
    }
  }
  free(wanted);
  char buffer[1200];
  (void)snprintf(buffer, sizeof buffer, "User %s is not on the server\n", value);
  r->host.print(r->host.ctx, buffer);
  return NULL;
}
static void entity_list(qa_q3_ta_game_server_command_runtime *r) {
  static const char *const labels[] = {"ET_GENERAL", "ET_PLAYER", "ET_ITEM", "ET_MISSILE", "ET_MOVER",
      "ET_BEAM", "ET_PORTAL", "ET_SPEAKER", "ET_PUSH_TRIGGER", "ET_TELEPORT_TRIGGER", "ET_INVISIBLE",
      "ET_GRAPPLE"};
  for (double i = 1; i < r->pool->num_entities; i++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(r->pool, i);
    if (!qa_q3_game_entity_inuse(entity)) continue;
    qa_q3_game_format_arg a = qa_q3_game_format_number(i);
    print_fmt(r, "%3i:", &a, 1);
    double type = entity->s.e_type;
    if (type == (int)type && type >= 0 && type < 12) {
      char padded[24];
      (void)snprintf(padded, sizeof padded, "%-20s", labels[(int)type]);
      r->host.print(r->host.ctx, padded);
    } else {
      qa_q3_game_format_arg b = qa_q3_game_format_number(type);
      print_fmt(r, "%3i                 ", &b, 1);
    }
    const char *classname = qa_q3_game_entity_classname(entity);
    if (entity->classname != NULL || entity->classname_client != NULL) {
      if (classname) r->host.print(r->host.ctx, classname);
    }
    r->host.print(r->host.ctx, "\n");
  }
}
static void capability(qa_q3_ta_game_server_command_runtime *r, qa_q3_ta_server_command_capability *service,
    const char *const *argv, size_t argc) {
  (void)r;
  if (service->kind == QA_Q3_TA_CAP_UNAVAILABLE) {
    char *command = argument(argv, argc, 0);
    qa_range_error("%s unavailable: %s", command, service->reason);
  }
  service->run(service->ctx, argv, argc);
}
bool qa_q3_ta_game_server_command_runtime_console_command(qa_q3_ta_game_server_command_runtime *r,
    const char *const *argv, size_t argc) {
  char *command = argument(argv, argc, 0);
  lower_inplace(command);
  bool handled = true;
  if (strcmp(command, "entitylist") == 0) entity_list(r);
  else if (strcmp(command, "forceteam") == 0) {
    char *arg1 = argument(argv, argc, 1);
    qa_q3_game_client *client = qa_q3_ta_game_server_command_runtime_client_for_string(r, arg1);
    free(arg1);
    if (client != NULL) {
      double slot = -1;
      for (int i = 0; i < QA_Q3_MAX_CLIENTS; i++) if (r->pool->clients[i] == client) { slot = i; break; }
      if (slot < 0) { free(command); qa_range_error("Console client belongs to another entity pool"); }
      char *arg2 = argument(argv, argc, 2);
      r->host.set_team(r->host.ctx, qa_q3_entity_pool_at(r->pool, slot), arg2);
      free(arg2);
    }
  }
  else if (strcmp(command, "game_memory") == 0) capability(r, &r->host.memory, argv, argc);
  else if (strcmp(command, "addbot") == 0 || strcmp(command, "botlist") == 0) capability(r, &r->host.bots, argv, argc);
  else if (strcmp(command, "abort_podium") == 0) {
    qa_cvar_snapshot snap = cvar_of(r, "g_gametype");
    double value = snap.integer_value;
    qa_cvar_read_free(&snap);
    if (value == 2) capability(r, &r->host.podium, argv, argc);
  }
  else if (strcmp(command, "addip") == 0) {
    if (argc < 2) r->host.print(r->host.ctx, "Usage:  addip <ip-mask>\n");
    else { char *a = argument(argv, argc, 1); add_ip(r, a); free(a); }
  }
  else if (strcmp(command, "removeip") == 0) remove_ip(r, argv, argc);
  else if (strcmp(command, "listip") == 0) r->host.execute_console_now(r->host.ctx, "g_banIPs\n");
  else {
    qa_cvar_snapshot snap = cvar_of(r, "dedicated");
    double dedicated = snap.integer_value;
    qa_cvar_read_free(&snap);
    if (dedicated == 0) handled = false;
    else {
      char *text = qa_q3_ta_concat_command_args(argv, argc, strcmp(command, "say") == 0 ? 1 : 0);
      qa_q3_game_format_arg a = qa_q3_game_format_string(text);
      char *t = qa_q3_game_format("print \"server: %s\"", &a, 1, 32000);
      r->host.send_server_command(r->host.ctx, -1, t);
      free(t);
      free(text);
    }
  }
  free(command);
  return handled;
}
