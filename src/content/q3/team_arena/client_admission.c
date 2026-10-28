/* Port of src/content/q3/team-arena/client-admission.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/client_admission.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/foundation/support.h"

enum { MAX_INFO_STRING = 1024, MAX_QPATH = 64, MAX_NETNAME = 36, CS_PLAYERS = 544 };
static double s32(double v) { return (int32_t)v; }
static double slot_get(qa_q3_game_player_slots_mirror *s, double i) { return qa_q3_game_slots_get_mirror(s, (size_t)i); }
static void slot_set(qa_q3_game_player_slots_mirror *s, double i, double v) { qa_q3_game_slots_set_mirror(s, (size_t)i, v); }

/* TS byteBuffer: C strings are byte strings ending at first NUL. */
static void byte_buffer(char *out, const char *value, size_t capacity) {
  size_t n = strlen(value ? value : "");
  if (n > capacity - 1) n = capacity - 1;
  memcpy(out, value ? value : "", n);
  out[n] = '\0';
}
static char *dup_range(const char *start, size_t length) {
  char *out = malloc(length + 1);
  if (!out) return NULL;
  memcpy(out, start, length);
  out[length] = '\0';
  return out;
}
static unsigned ascii_upper(unsigned byte) { return byte >= 97 && byte <= 122 ? byte - 32 : byte; }
static bool case_insensitive_equal(const char *first, const char *second) {
  size_t index = 0;
  for (;;) {
    unsigned left = first[index] ? (unsigned char)first[index] : 0;
    unsigned right = second[index] ? (unsigned char)second[index] : 0;
    if (ascii_upper(left) != ascii_upper(right)) return false;
    if (left == 0) return true;
    index++;
  }
}
static bool case_insensitive_equal_range(const char *wanted, const char *key, size_t key_length) {
  size_t index = 0;
  for (;;) {
    unsigned left = wanted[index] ? (unsigned char)wanted[index] : 0;
    unsigned right = index < key_length ? (unsigned char)key[index] : 0;
    if (ascii_upper(left) != ascii_upper(right)) return false;
    if (left == 0) return true;
    index++;
  }
}
/* TS valueForKey over bounded buffers. */
static char *value_for_key(const char *info, const char *wanted) {
  size_t length = strlen(info);
  size_t cursor = info[0] == '\\' ? 1 : 0;
  for (;;) {
    size_t key_start = cursor;
    while (cursor < length && info[cursor] != '\\') cursor++;
    if (cursor == length) return dup_range("", 0);
    size_t key_length = cursor - key_start;
    cursor++;
    size_t value_start = cursor;
    while (cursor < length && info[cursor] != '\\') cursor++;
    size_t value_length = cursor - value_start;
    if (case_insensitive_equal_range(wanted, info + key_start, key_length))
      return dup_range(info + value_start, value_length);
    if (cursor == length) return dup_range("", 0);
    cursor++;
  }
}
char *qa_q3_ta_client_info_value(const char *info, const char *wanted) {
  char info_buffer[MAX_INFO_STRING], wanted_buffer[MAX_INFO_STRING];
  byte_buffer(info_buffer, info, MAX_INFO_STRING);
  byte_buffer(wanted_buffer, wanted, MAX_INFO_STRING);
  return value_for_key(info_buffer, wanted_buffer);
}
static bool valid_info(const char *info) { return !strchr(info, '"') && !strchr(info, ';'); }

char *qa_q3_ta_clean_client_name(const char *input) {
  char source[MAX_INFO_STRING];
  byte_buffer(source, input, MAX_INFO_STRING);
  size_t source_length = strlen(source);
  const size_t output_size = MAX_NETNAME - 1;
  char output[MAX_NETNAME];
  size_t out_len = 0, colorless = 0, spaces = 0, cursor = 0;
  while (cursor < source_length) {
    unsigned character = (unsigned char)source[cursor++];
    if (out_len == 0 && character == 32) continue;
    if (character == 94) {
      if (cursor == source_length) break;
      unsigned color = (unsigned char)source[cursor];
      if (((color - 48) & 7) == 0) { cursor++; continue; }
      if (out_len > output_size - 2) break;
      output[out_len++] = '^';
      output[out_len++] = (char)color;
      cursor++;
      continue;
    }
    if (character == 32) { spaces++; if (spaces > 3) continue; } else spaces = 0;
    if (out_len > output_size - 1) break;
    output[out_len++] = (char)character;
    colorless++;
  }
  output[out_len] = '\0';
  return dup_range(out_len == 0 || colorless == 0 ? "UnnamedPlayer" : output,
      out_len == 0 || colorless == 0 ? strlen("UnnamedPlayer") : out_len);
}

/* TS resetPlayerState: fresh mirror ps fields with source slot lengths. */
static void slots_reset(qa_q3_game_player_slots_mirror *s, size_t length) {
  memset(s->values, 0, sizeof s->values);
  s->length = length;
}
static void reset_player_state(qa_q3_game_player_state_mirror *ps) {
  qa_q3_game_product product = ps->product;
  memset(ps, 0, sizeof *ps);
  ps->product = product;
  slots_reset(&ps->stats, 16); slots_reset(&ps->persistant, 16);
  slots_reset(&ps->powerups, 16); slots_reset(&ps->ammo, 16);
  slots_reset(&ps->events, 2); slots_reset(&ps->event_parms, 2);
  slots_reset(&ps->delta_angles, 3);
}
static void reset_client(qa_q3_game_client *client, qa_q3_base_product product) {
  qa_q3_game_client_destroy(client);
  qa_q3_game_client_init(client, product == QA_Q3_BASE_PRODUCT_MISSIONPACK
      ? QA_Q3_GAME_PRODUCT_MISSIONPACK : QA_Q3_GAME_PRODUCT_BASEQ3);
}
static char *info_value(const char *userinfo, const char *key) {
  return qa_q3_ta_client_info_value(userinfo, key);
}
static double info_atoi(const char *userinfo, const char *key) {
  char *value = info_value(userinfo, key);
  double out = qa_game_atoi(value);
  free(value);
  return out;
}

char *qa_q3_ta_client_presentation_config(qa_q3_game_client *client, const char *userinfo,
    double game_type, bool has_bot_team, qa_q3_base_team bot_team) {
  bool team_game = game_type >= QA_Q3_BASE_GT_TEAM;
  char *raw_model = info_value(userinfo, team_game ? "team_model" : "model");
  char *raw_head = info_value(userinfo, team_game ? "team_headmodel" : "headmodel");
  char model[MAX_QPATH], head[MAX_QPATH];
  byte_buffer(model, raw_model, MAX_QPATH);
  byte_buffer(head, raw_head, MAX_QPATH);
  free(raw_model); free(raw_head);
  double team_task = info_atoi(userinfo, "teamtask");
  char *color1 = info_value(userinfo, "color1"), *color2 = info_value(userinfo, "color2");
  char *result;
  if (has_bot_team) {
    char *skill = info_value(userinfo, "skill");
    qa_q3_game_format_arg a[] = {
      qa_q3_game_format_string(client->pers.netname), qa_q3_game_format_number(bot_team),
      qa_q3_game_format_string(model), qa_q3_game_format_string(head),
      qa_q3_game_format_string(color1), qa_q3_game_format_string(color2),
      qa_q3_game_format_number(client->pers.max_health), qa_q3_game_format_number(client->sess.wins),
      qa_q3_game_format_number(client->sess.losses), qa_q3_game_format_string(skill),
      qa_q3_game_format_number(team_task), qa_q3_game_format_number(client->sess.team_leader)};
    result = qa_q3_game_format("n\\%s\\t\\%i\\model\\%s\\hmodel\\%s\\c1\\%s\\c2\\%s\\hc\\%i\\w\\%i\\l\\%i\\skill\\%s\\tt\\%d\\tl\\%d", a, 12, 32000);
    free(skill);
  } else {
    char *red = info_value(userinfo, "g_redteam"), *blue = info_value(userinfo, "g_blueteam");
    qa_q3_game_format_arg a[] = {
      qa_q3_game_format_string(client->pers.netname), qa_q3_game_format_number(client->sess.session_team),
      qa_q3_game_format_string(model), qa_q3_game_format_string(head),
      qa_q3_game_format_string(red), qa_q3_game_format_string(blue),
      qa_q3_game_format_string(color1), qa_q3_game_format_string(color2),
      qa_q3_game_format_number(client->pers.max_health), qa_q3_game_format_number(client->sess.wins),
      qa_q3_game_format_number(client->sess.losses),
      qa_q3_game_format_number(team_task), qa_q3_game_format_number(client->sess.team_leader)};
    result = qa_q3_game_format("n\\%s\\t\\%i\\model\\%s\\hmodel\\%s\\g_redteam\\%s\\g_blueteam\\%s\\c1\\%s\\c2\\%s\\hc\\%i\\w\\%i\\l\\%i\\tt\\%d\\tl\\%d", a, 13, 32000);
    free(red); free(blue);
  }
  free(color1); free(color2);
  return result;
}

void qa_q3_ta_client_admission_runtime_init(qa_q3_ta_client_admission_runtime *r, qa_q3_ta_client_admission_host host) {
  if ((qa_q3_base_product)host.pool->options.product != host.product)
    qa_range_error("Client admission product differs from its entity pool");
  if (host.team_scores->length != QA_Q3_BASE_TEAM_NUM_TEAMS)
    qa_range_error("Client admission requires four shared team-score slots");
  r->host = host;
}
static qa_q3_game_entity *client_entity(qa_q3_ta_client_admission_runtime *r, double num) {
  if (num != trunc(num) || num < 0 || num >= r->host.pool->max_clients)
    qa_range_error("client %g outside configured clients", num);
  return qa_q3_entity_pool_at(r->host.pool, num);
}
static void get_userinfo(qa_q3_ta_client_admission_runtime *r, double num, char *out) {
  byte_buffer(out, r->host.get_userinfo(r->host.ctx, num), MAX_INFO_STRING);
}
static void send_fmt(qa_q3_ta_client_admission_runtime *r, double target, const char *f,
    const qa_q3_game_format_arg *a, size_t n) {
  char *t = qa_q3_game_format(f, a, n, 32000);
  r->host.send_server_command(r->host.ctx, target, t);
  free(t);
}
static void log_fmt(qa_q3_ta_client_admission_runtime *r, const char *f, const qa_q3_game_format_arg *a, size_t n) {
  char *t = qa_q3_game_format(f, a, n, 32000);
  r->host.log(r->host.ctx, t);
  free(t);
}

void qa_q3_ta_client_admission_runtime_userinfo_changed(qa_q3_ta_client_admission_runtime *r, double client_num) {
  qa_q3_game_entity *entity = client_entity(r, client_num);
  qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, client_num);
  char userinfo[MAX_INFO_STRING];
  get_userinfo(r, client_num, userinfo);
  if (!valid_info(userinfo)) strcpy(userinfo, "\\name\\badinfo");
  char *ip = info_value(userinfo, "ip");
  if (strcmp(ip, "localhost") == 0) client->pers.local_client = true;
  free(ip);
  client->pers.predict_item_pickup = info_atoi(userinfo, "cg_predictItems") != 0;
  char old_name[MAX_INFO_STRING];
  byte_buffer(old_name, client->pers.netname, MAX_INFO_STRING);
  char *raw_name = info_value(userinfo, "name");
  char *cleaned = qa_q3_ta_clean_client_name(raw_name);
  free(raw_name);
  free(client->pers.netname);
  client->pers.netname = cleaned;
  if (client->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR &&
      client->sess.spectator_state == QA_Q3_SPEC_SCOREBOARD) {
    free(client->pers.netname);
    client->pers.netname = dup_range("scoreboard", strlen("scoreboard"));
  }
  if (client->pers.connected == QA_Q3_CONN_CONNECTED && strcmp(old_name, client->pers.netname) != 0) {
    qa_q3_game_format_arg a[] = {qa_q3_game_format_string(old_name), qa_q3_game_format_string(client->pers.netname)};
    send_fmt(r, -1, "print \"%s^7 renamed to %s\n\"", a, 2);
  }
  double health = info_atoi(userinfo, "handicap");
  if (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && slot_get(&client->ps.powerups, QA_Q3_PW_GUARD) != 0) health = 200;
  else if (health < 1 || health > 100) health = 100;
  client->pers.max_health = health;
  slot_set(&client->ps.stats, qa_q3_base_stat_schema_for(r->host.product)->max_health, health);
  qa_q3_ta_client_admission_settings settings = r->host.settings(r->host.ctx);
  double game_type = settings.game_type;
  double team = client->sess.session_team;
  bool is_bot = ((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) != 0;
  if (game_type >= QA_Q3_BASE_GT_TEAM && is_bot) {
    char *requested = info_value(userinfo, "team");
    if (case_insensitive_equal(requested, "red") || case_insensitive_equal(requested, "r")) team = QA_Q3_BASE_TEAM_RED;
    else if (case_insensitive_equal(requested, "blue") || case_insensitive_equal(requested, "b")) team = QA_Q3_BASE_TEAM_BLUE;
    else {
      qa_q3_ta_session_world_state world;
      memset(&world, 0, sizeof world);
      world.clients = r->host.pool->clients;
      world.client_count = QA_Q3_MAX_CLIENTS;
      world.max_clients = r->host.pool->max_clients;
      world.team_scores = r->host.team_scores;
      team = qa_q3_ta_pick_team(&world, client_num);
    }
    free(requested);
  }
  if (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && game_type >= QA_Q3_BASE_GT_TEAM)
    client->pers.team_info = true;
  else {
    char *overlay = info_value(userinfo, "teamoverlay");
    client->pers.team_info = overlay[0] == '\0' || qa_game_atoi(overlay) != 0;
    free(overlay);
  }
  char *config = qa_q3_ta_client_presentation_config(client, userinfo, game_type, is_bot, (qa_q3_base_team)team);
  r->host.set_configstring(r->host.ctx, CS_PLAYERS + client_num, config);
  qa_q3_game_format_arg a[] = {qa_q3_game_format_number(client_num), qa_q3_game_format_string(config)};
  log_fmt(r, "ClientUserinfoChanged: %i %s\n", a, 2);
  free(config);
}

typedef struct session_info_ctx { char *team; } session_info_ctx;
static const char *session_team_value(void *ctx, const char *key) {
  (void)key;
  return ((session_info_ctx *)ctx)->team;
}

const char *qa_q3_ta_client_admission_runtime_connect(qa_q3_ta_client_admission_runtime *r,
    double client_num, bool first_time, bool is_bot) {
  qa_q3_game_entity *entity = client_entity(r, client_num);
  char userinfo[MAX_INFO_STRING];
  get_userinfo(r, client_num, userinfo);
  char *address = info_value(userinfo, "ip");
  if (r->host.filter_packet(r->host.ctx, address)) { free(address); return "You are banned from this server."; }
  char password[MAX_INFO_STRING];
  byte_buffer(password, r->host.settings(r->host.ctx).password, MAX_INFO_STRING);
  char *given = info_value(userinfo, "password");
  bool rejected = ((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) == 0 &&
      strcmp(address, "localhost") != 0 && password[0] != '\0' &&
      !case_insensitive_equal(password, "none") && strcmp(password, given) != 0;
  free(given);
  if (rejected) { free(address); return "Invalid password"; }
  free(address);
  qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, client_num);
  entity->client = client;
  reset_client(client, r->host.product);
  client->pers.connected = QA_Q3_CONN_CONNECTING;
  char *team_value = info_value(userinfo, "team");
  session_info_ctx sctx = {team_value};
  qa_q3_ta_session_userinfo session_info = {&sctx, session_team_value};
  if (first_time || r->host.new_session(r->host.ctx))
    qa_q3_ta_game_session_manager_initialize_client(r->host.session, client_num, session_info);
  free(team_value);
  qa_q3_ta_game_session_manager_read_client(r->host.session, client_num);
  if (is_bot) {
    entity->r.sv_flags = (double)((int32_t)entity->r.sv_flags | QA_Q3_BASE_SVF_BOT);
    (void)qa_q3_entity_pool_activate_client(r->host.pool, entity->slot);
    if (r->host.bots.kind == QA_Q3_TA_BOTS_UNAVAILABLE) return r->host.bots.reason;
    if (!r->host.bots.connect(r->host.bots.ctx, client_num, !first_time)) return "BotConnectfailed";
  }
  qa_q3_game_format_arg a1 = qa_q3_game_format_number(client_num);
  log_fmt(r, "ClientConnect: %i\n", &a1, 1);
  qa_q3_ta_client_admission_runtime_userinfo_changed(r, client_num);
  if (first_time) {
    qa_q3_game_format_arg a = qa_q3_game_format_string(client->pers.netname);
    send_fmt(r, -1, "print \"%s^7 connected\n\"", &a, 1);
  }
  double game_type = r->host.settings(r->host.ctx).game_type;
  if (game_type >= QA_Q3_BASE_GT_TEAM && client->sess.session_team != QA_Q3_BASE_TEAM_SPECTATOR)
    r->host.broadcast_team_change(r->host.ctx, client_num, -1);
  r->host.calculate_ranks(r->host.ctx);
  return NULL;
}

void qa_q3_ta_client_admission_runtime_begin(qa_q3_ta_client_admission_runtime *r, double client_num) {
  qa_q3_game_entity *entity = client_entity(r, client_num);
  qa_q3_game_client *client = qa_q3_entity_pool_client_at(r->host.pool, client_num);
  qa_q3_base_link_state link;
  if (r->host.world->link_state && r->host.world->link_state(r->host.world->ctx, entity->slot, &link) && link.linked)
    r->host.world->unlink(r->host.world->ctx, entity->slot);
  qa_q3_init_game_entity(entity);
  entity->touch.fn = NULL; entity->touch.ctx = NULL;
  entity->pain.fn = NULL; entity->pain.ctx = NULL;
  entity->client = client;
  client->pers.connected = QA_Q3_CONN_CONNECTED;
  client->pers.enter_time = r->host.state->time;
  client->pers.team_state.state = QA_Q3_TEAM_STATE_BEGIN;
  double flags = client->ps.e_flags;
  reset_player_state(&client->ps);
  client->ps.e_flags = flags;
  r->host.client_spawn(r->host.ctx, entity);
  if (client->sess.session_team != QA_Q3_BASE_TEAM_SPECTATOR) {
    qa_q3_game_entity *temporary = qa_q3_entity_pool_temp_entity(r->host.pool, client->ps.origin, QA_Q3_EV_PLAYER_TELEPORT_IN);
    temporary->s.client_num = entity->s.client_num;
    if (r->host.settings(r->host.ctx).game_type != QA_Q3_BASE_GT_TOURNAMENT) {
      qa_q3_game_format_arg a = qa_q3_game_format_string(client->pers.netname);
      send_fmt(r, -1, "print \"%s^7 entered the game\n\"", &a, 1);
    }
  }
  qa_q3_game_format_arg a1 = qa_q3_game_format_number(client_num);
  log_fmt(r, "ClientBegin: %i\n", &a1, 1);
  r->host.calculate_ranks(r->host.ctx);
}

void qa_q3_ta_client_admission_runtime_disconnect(qa_q3_ta_client_admission_runtime *r, double client_num) {
  qa_q3_game_entity *entity = client_entity(r, client_num);
  bool is_bot = ((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) != 0;
  if (r->host.bots.kind == QA_Q3_TA_BOTS_AVAILABLE) r->host.bots.remove_queued_begin(r->host.bots.ctx, client_num);
  else if (is_bot) qa_range_error("%s", r->host.bots.reason);
  if (entity->client == NULL) return;
  qa_q3_game_client *client = entity->client;
  for (double index = 0; index < r->host.pool->max_clients; index++) {
    qa_q3_game_client *follower = qa_q3_entity_pool_client_at(r->host.pool, index);
    if (follower->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR &&
        follower->sess.spectator_state == QA_Q3_SPEC_FOLLOW && follower->sess.spectator_client == client_num)
      r->host.stop_following(r->host.ctx, qa_q3_entity_pool_at(r->host.pool, index));
  }
  if (client->pers.connected == QA_Q3_CONN_CONNECTED && client->sess.session_team != QA_Q3_BASE_TEAM_SPECTATOR) {
    qa_q3_game_entity *temporary = qa_q3_entity_pool_temp_entity(r->host.pool, client->ps.origin, QA_Q3_EV_PLAYER_TELEPORT_OUT);
    temporary->s.client_num = entity->s.client_num;
    r->host.toss_client_items(r->host.ctx, entity);
    if (r->host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
      r->host.toss_client_persistant_powerups(r->host.ctx, entity);
      if (r->host.settings(r->host.ctx).game_type == QA_Q3_BASE_GT_HARVESTER)
        r->host.toss_client_cubes(r->host.ctx, entity);
    }
  }
  qa_q3_game_format_arg a1 = qa_q3_game_format_number(client_num);
  log_fmt(r, "ClientDisconnect: %i\n", &a1, 1);
  double game_type = r->host.settings(r->host.ctx).game_type;
  if (game_type == QA_Q3_BASE_GT_TOURNAMENT && r->host.state->intermission_time == 0 &&
      r->host.state->warmup_time == 0 && r->host.state->sorted_clients[1] == client_num) {
    double winner = r->host.state->sorted_clients[0];
    qa_q3_game_client *winner_client = qa_q3_entity_pool_client_at(r->host.pool, winner);
    winner_client->sess.wins = s32(winner_client->sess.wins + 1);
    qa_q3_ta_client_admission_runtime_userinfo_changed(r, winner);
  }
  r->host.world->unlink(r->host.world->ctx, entity->slot);
  entity->s.modelindex = 0;
  qa_q3_entity_pool_deactivate_client(r->host.pool, entity->slot);
  qa_q3_game_entity_set_classname(entity, "disconnected");
  client->pers.connected = QA_Q3_CONN_DISCONNECTED;
  slot_set(&client->ps.persistant, QA_Q3_BASE_PERS_TEAM, QA_Q3_BASE_TEAM_FREE);
  client->sess.session_team = QA_Q3_BASE_TEAM_FREE;
  r->host.set_configstring(r->host.ctx, CS_PLAYERS + client_num, "");
  r->host.calculate_ranks(r->host.ctx);
  if (is_bot && r->host.bots.kind == QA_Q3_TA_BOTS_AVAILABLE)
    r->host.bots.shutdown_client(r->host.bots.ctx, client_num, false);
}
