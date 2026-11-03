/* Port of src/content/q3/team-arena/session.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/session.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/foundation/support.h"

enum { MAX_STRING_CHARS = 1024 };

/* TS cvarBuffer: C strings are bytes, so only the 1023-char bound applies.
 * Writes into caller buffer (MAX_STRING_CHARS bytes incl. NUL). */
static void cvar_buffer(char *out, const char *value) {
  size_t length = strlen(value);
  if (length > MAX_STRING_CHARS - 1) length = MAX_STRING_CHARS - 1;
  memcpy(out, value, length);
  out[length] = '\0';
}

static int signed_byte(const char *buffer, size_t offset) {
  unsigned byte = (unsigned char)buffer[offset];
  return byte < 128 ? (int)byte : (int)byte - 256;
}

typedef struct integer_scan { double value; size_t next_offset; } integer_scan;
static integer_scan scan_integer(const char *buffer, size_t offset) {
  size_t length = strlen(buffer);
  if (offset > length) qa_range_error("truncated session data scans beyond its terminating NUL");
  double value = qa_game_atoi(buffer + offset);
  size_t cursor = offset;
  while (cursor < length && signed_byte(buffer, cursor) <= 32) cursor++;
  if (cursor == length) { integer_scan r = {value, cursor}; return r; }
  unsigned sign = (unsigned char)buffer[cursor];
  if (sign == 43 || sign == 45) cursor++;
  for (;;) {
    if (cursor == length) { integer_scan r = {value, cursor + 1}; return r; }
    unsigned character = (unsigned char)buffer[cursor];
    cursor++;
    if (character < 48 || character > 57) { integer_scan r = {value, cursor}; return r; }
  }
}

static qa_q3_game_client *client_at(const qa_q3_ta_session_world_state *world, double client_num) {
  if (client_num != trunc(client_num) || client_num < 0 || client_num >= world->max_clients)
    qa_range_error("session client %s outside configured clients", QA_JS_NUMBER(client_num));
  qa_q3_game_client *client = (size_t)client_num < world->client_count ? world->clients[(size_t)client_num] : NULL;
  if (client == NULL) qa_range_error("session client %s has no backing state", QA_JS_NUMBER(client_num));
  return client;
}

double qa_q3_ta_team_count(const qa_q3_ta_session_world_state *world, double ignore_client_num, qa_q3_base_team team) {
  double count = 0;
  for (double index = 0; index < world->max_clients; index++) {
    if (index == ignore_client_num) continue;
    qa_q3_game_client *client = client_at(world, index);
    if (client->pers.connected == QA_Q3_CONN_DISCONNECTED) continue;
    if (client->sess.session_team == team) count++;
  }
  return count;
}

qa_q3_base_team qa_q3_ta_pick_team(const qa_q3_ta_session_world_state *world, double ignore_client_num) {
  double blue_count = qa_q3_ta_team_count(world, ignore_client_num, QA_Q3_BASE_TEAM_BLUE);
  double red_count = qa_q3_ta_team_count(world, ignore_client_num, QA_Q3_BASE_TEAM_RED);
  if (blue_count > red_count) return QA_Q3_BASE_TEAM_RED;
  if (red_count > blue_count) return QA_Q3_BASE_TEAM_BLUE;
  double left = qa_q3_base_player_state_slots_get(world->team_scores, QA_Q3_BASE_TEAM_BLUE);
  double right = qa_q3_base_player_state_slots_get(world->team_scores, QA_Q3_BASE_TEAM_RED);
  if (left >
      right) return QA_Q3_BASE_TEAM_RED;
  return QA_Q3_BASE_TEAM_BLUE;
}

void qa_q3_ta_game_session_manager_init(qa_q3_ta_game_session_manager *manager,
    qa_q3_ta_session_world_state *world, qa_q3_ta_session_services services) {
  if (world->max_clients != trunc(world->max_clients) || world->max_clients < 0 ||
      world->max_clients > (double)world->client_count)
    qa_range_error("session maxClients exceeds its client storage");
  if (world->team_scores->length != QA_Q3_BASE_TEAM_NUM_TEAMS)
    qa_range_error("session team scores require TEAM_NUM_TEAMS slots");
  manager->world = world;
  manager->services = services;
}

static void session_name(char *out, size_t out_size, double client_num) {
  (void)snprintf(out, out_size, "session%lld", (long long)client_num);
}

void qa_q3_ta_game_session_manager_write_client(qa_q3_ta_game_session_manager *manager, double client_num) {
  qa_q3_client_session *session = &client_at(manager->world, client_num)->sess;
  qa_q3_game_format_arg args[] = {
    qa_q3_game_format_number(session->session_team), qa_q3_game_format_number(session->spectator_time),
    qa_q3_game_format_number(session->spectator_state), qa_q3_game_format_number(session->spectator_client),
    qa_q3_game_format_number(session->wins), qa_q3_game_format_number(session->losses),
    qa_q3_game_format_number(session->team_leader)};
  char *value = qa_q3_game_format("%i %i %i %i %i %i %i", args, 7, MAX_STRING_CHARS);
  char name[32];
  session_name(name, sizeof name, client_num);
  manager->services.cvars.set(manager->services.cvars.ctx, name, value);
  free(value);
}

void qa_q3_ta_game_session_manager_read_client(qa_q3_ta_game_session_manager *manager, double client_num) {
  qa_q3_client_session *session = &client_at(manager->world, client_num)->sess;
  char name[32], buffer[MAX_STRING_CHARS];
  session_name(name, sizeof name, client_num);
  cvar_buffer(buffer, manager->services.cvars.get(manager->services.cvars.ctx, name));
  integer_scan scan = scan_integer(buffer, 0);
  double session_team = scan.value;
  scan = scan_integer(buffer, scan.next_offset);
  session->spectator_time = scan.value;
  scan = scan_integer(buffer, scan.next_offset);
  double spectator_state = scan.value;
  scan = scan_integer(buffer, scan.next_offset);
  session->spectator_client = scan.value;
  scan = scan_integer(buffer, scan.next_offset);
  session->wins = scan.value;
  scan = scan_integer(buffer, scan.next_offset);
  session->losses = scan.value;
  scan = scan_integer(buffer, scan.next_offset);
  double team_leader = scan.value;
  session->session_team = session_team;
  session->spectator_state = spectator_state;
  session->team_leader = team_leader;
}

void qa_q3_ta_game_session_manager_initialize_client(qa_q3_ta_game_session_manager *manager,
    double client_num, qa_q3_ta_session_userinfo userinfo) {
  qa_q3_game_client *client = client_at(manager->world, client_num);
  qa_q3_client_session *session = &client->sess;
  qa_q3_ta_session_world_state *world = manager->world;
  if (world->game_type >= QA_Q3_BASE_GT_TEAM) {
    if (world->team_auto_join) {
      session->session_team = qa_q3_ta_pick_team(world, -1);
      manager->services.broadcast_team_change(manager->services.ctx, client_num, -1);
    } else {
      session->session_team = QA_Q3_BASE_TEAM_SPECTATOR;
    }
  } else if (userinfo.value_for_key(userinfo.ctx, "team")[0] == 's') {
    session->session_team = QA_Q3_BASE_TEAM_SPECTATOR;
  } else if (world->game_type == QA_Q3_BASE_GT_TOURNAMENT) {
    session->session_team = world->num_non_spectator_clients >= 2 ? QA_Q3_BASE_TEAM_SPECTATOR : QA_Q3_BASE_TEAM_FREE;
  } else {
    session->session_team = (world->max_game_clients > 0 &&
        world->num_non_spectator_clients >= world->max_game_clients)
        ? QA_Q3_BASE_TEAM_SPECTATOR : QA_Q3_BASE_TEAM_FREE;
  }
  session->spectator_state = QA_Q3_SPEC_FREE;
  session->spectator_time = world->time;
  qa_q3_ta_game_session_manager_write_client(manager, client_num);
}

void qa_q3_ta_game_session_manager_initialize_world(qa_q3_ta_game_session_manager *manager) {
  char buffer[MAX_STRING_CHARS];
  cvar_buffer(buffer, manager->services.cvars.get(manager->services.cvars.ctx, "session"));
  double previous_game_type = qa_game_atoi(buffer);
  if (manager->world->game_type != previous_game_type) {
    manager->world->new_session = true;
    manager->services.print(manager->services.ctx, "Gametype changed, clearing session data.\n");
  }
}

void qa_q3_ta_game_session_manager_write_world(qa_q3_ta_game_session_manager *manager) {
  qa_q3_game_format_arg args[] = {qa_q3_game_format_number(manager->world->game_type)};
  char *value = qa_q3_game_format("%i", args, 1, MAX_STRING_CHARS);
  manager->services.cvars.set(manager->services.cvars.ctx, "session", value);
  free(value);
  for (double index = 0; index < manager->world->max_clients; index++)
    if (client_at(manager->world, index)->pers.connected == QA_Q3_CONN_CONNECTED)
      qa_q3_ta_game_session_manager_write_client(manager, index);
}
