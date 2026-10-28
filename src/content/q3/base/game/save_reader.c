/* Port of src/content/q3/base/game/save-reader.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/save_reader.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/save_values.h"
#include "qa/foundation/support.h"
static char *dupstr(const char *v) { if (!v) return NULL; size_t n = strlen(v); char *d = malloc(n + 1); if (d) memcpy(d, v, n + 1); return d; }
static double rf_int0(const qa_save_reader *r, const char *n) { qa_save_reader f; qa_save_reader_field(r, n, &f); double v = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f); return v; }
static double rf_num(const qa_save_reader *r, const char *n) { qa_save_reader f; qa_save_reader_field(r, n, &f); double v = qa_save_reader_number(&f); qa_save_reader_destroy(&f); return v; }
qa_q3_saved_actor_ref qa_q3_read_actor(const qa_save_reader *reader) {
  qa_q3_saved_actor_ref a = {true, rf_int0(reader, "slot"), rf_int0(reader, "generation")}; return a;
}
static qa_q3_game_trajectory_mirror read_traj(const qa_save_reader *r) {
  qa_save_reader f; qa_q3_game_trajectory_mirror t; t.type = rf_num(r, "type"); t.time = rf_num(r, "time"); t.duration = rf_num(r, "duration");
  qa_save_reader_field(r, "base", &f); t.base = qa_q3_read_vector(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "delta", &f); t.delta = qa_q3_read_vector(&f); qa_save_reader_destroy(&f); return t;
}
static double read_slot(const qa_save_reader *r, double max) {
  double v = qa_save_reader_integer_min(r, 0); if (v >= max) qa_save_reader_fail(r, "Q3 source reference outside retained table"); return v;
}
static bool nullable_slot(const qa_save_reader *r, const char *name, double max, double *out) {
  qa_save_reader f; qa_save_reader_field(r, name, &f);
  if (qa_save_reader_is_null(&f)) { qa_save_reader_destroy(&f); return false; }
  *out = read_slot(&f, max); qa_save_reader_destroy(&f); return true;
}
static char *nullable_str(const qa_save_reader *r, const char *name) {
  qa_save_reader f; qa_save_reader_field(r, name, &f);
  if (qa_save_reader_is_null(&f)) { qa_save_reader_destroy(&f); return NULL; }
  char *v = dupstr(qa_save_reader_string(&f)); qa_save_reader_destroy(&f); return v;
}
static qa_q3_game_save_entity_state read_entity(const qa_save_reader *r) {
  qa_q3_game_save_entity_state s; memset(&s, 0, sizeof s); qa_save_reader f;
  qa_save_reader_field(r, "values", &f); s.values = qa_q3_read_entity_values(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "network", &f); s.network = qa_q3_read_network_values(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "pos", &f); s.pos = read_traj(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "apos", &f); s.apos = read_traj(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "shared", &f); s.sv_flags = rf_num(&f, "svFlags"); s.single_client = rf_num(&f, "singleClient");
  s.contents = rf_num(&f, "contents"); s.owner_num = rf_num(&f, "ownerNum"); qa_save_reader_destroy(&f);
  s.has_client = nullable_slot(r, "client", 64, &s.client);
  s.has_parent = nullable_slot(r, "parent", 1024, &s.parent); s.has_next_train = nullable_slot(r, "nextTrain", 1024, &s.next_train);
  s.has_prev_train = nullable_slot(r, "prevTrain", 1024, &s.prev_train); s.has_target_ent = nullable_slot(r, "targetEnt", 1024, &s.target_ent);
  s.has_chain = nullable_slot(r, "chain", 1024, &s.chain); s.has_enemy = nullable_slot(r, "enemy", 1024, &s.enemy);
  s.has_activator = nullable_slot(r, "activator", 1024, &s.activator); s.has_teamchain = nullable_slot(r, "teamchain", 1024, &s.teamchain);
  s.has_teammaster = nullable_slot(r, "teammaster", 1024, &s.teammaster);
  qa_save_reader_field(r, "item", &f); s.has_item = !qa_save_reader_is_null(&f); if (s.has_item) s.item = qa_save_reader_integer_min(&f, 0); qa_save_reader_destroy(&f);
  s.nextthink = rf_num(r, "nextthink");
  s.think = nullable_str(r, "think"); s.reached = nullable_str(r, "reached"); s.blocked = nullable_str(r, "blocked");
  s.touch = nullable_str(r, "touch"); s.use = nullable_str(r, "use"); s.pain = nullable_str(r, "pain"); s.die = nullable_str(r, "die");
  return s;
}
static double *read_numbers(const qa_save_reader *r, size_t *out) {
  size_t n = qa_save_reader_list_count(r); double *d = malloc((n ? n : 1) * sizeof *d); if (!d) qa_range_error("out of memory");
  for (size_t i = 0; i < n; i++) { qa_save_reader it; qa_save_reader_item(r, i, &it); d[i] = qa_save_reader_number(&it); qa_save_reader_destroy(&it); }
  *out = n; return d;
}
static qa_q3_game_save_client_state read_client(const qa_save_reader *r) {
  qa_q3_game_save_client_state s; memset(&s, 0, sizeof s); qa_save_reader f;
  qa_save_reader_field(r, "values", &f); s.values = qa_q3_read_client_values(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "player", &f); s.player = qa_q3_read_player_values(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "persistant", &f); s.persistant = qa_q3_read_persistant_values(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "command", &f); s.command.server_time = rf_num(&f, "serverTime");
  { qa_save_reader a; qa_save_reader_field(&f, "angles", &a); s.command.angles = qa_q3_read_vector(&a); qa_save_reader_destroy(&a); }
  s.command.buttons = rf_num(&f, "buttons"); s.command.weapon = rf_num(&f, "weapon");
  s.command.forwardmove = rf_num(&f, "forwardmove"); s.command.rightmove = rf_num(&f, "rightmove"); s.command.upmove = rf_num(&f, "upmove");
  qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "team", &f); s.team = qa_q3_read_team_values(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "session", &f); s.session = qa_q3_read_session_values(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "events", &f); s.events = read_numbers(&f, &s.event_count); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "eventParms", &f); s.event_parms = read_numbers(&f, &s.event_parm_count); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "persistantSlots", &f); s.persistant_slots = read_numbers(&f, &s.persistant_count); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "powerups", &f); s.powerups = read_numbers(&f, &s.powerup_count); qa_save_reader_destroy(&f);
  qa_save_reader_field(r, "ammoTimes", &f); s.ammo_times = read_numbers(&f, &s.ammo_time_count); qa_save_reader_destroy(&f);
  s.has_hook = nullable_slot(r, "hook", 1024, &s.hook);
  s.has_persistant_powerup = nullable_slot(r, "persistantPowerup", 1024, &s.persistant_powerup);
  return s;
}
qa_q3_graph_state qa_q3_read_graph(const qa_save_value *value) {
  qa_save_reader reader; qa_save_reader_init(&reader, value, "q3.graph"); qa_q3_graph_state s; memset(&s, 0, sizeof s); qa_save_reader f;
  qa_save_reader_field(&reader, "ownership", &f); s.ownership_count = qa_save_reader_list_count(&f);
  s.ownership = calloc(s.ownership_count ? s.ownership_count : 1, sizeof *s.ownership);
  for (size_t i = 0; i < s.ownership_count; i++) { qa_save_reader it; qa_save_reader_item(&f, i, &it);
    qa_save_reader af; qa_save_reader_field(&it, "actor", &af); s.ownership[i].has_actor = !qa_save_reader_is_null(&af);
    if (s.ownership[i].has_actor) s.ownership[i].actor = qa_q3_read_actor(&af);
    qa_save_reader_destroy(&af);
    qa_save_reader bf; qa_save_reader_field(&it, "active", &bf); s.ownership[i].active = qa_save_reader_boolean(&bf); qa_save_reader_destroy(&bf);
    qa_save_reader_field(&it, "borrowed", &bf); s.ownership[i].borrowed = qa_save_reader_boolean(&bf); qa_save_reader_destroy(&bf);
    qa_save_reader_destroy(&it); }
  qa_save_reader_destroy(&f);
  qa_save_reader_field(&reader, "entities", &f); s.entity_count = qa_save_reader_list_count(&f);
  s.entities = calloc(s.entity_count ? s.entity_count : 1, sizeof *s.entities);
  for (size_t i = 0; i < s.entity_count; i++) { qa_save_reader it; qa_save_reader_item(&f, i, &it); s.entities[i] = read_entity(&it); qa_save_reader_destroy(&it); }
  qa_save_reader_destroy(&f);
  qa_save_reader_field(&reader, "clients", &f); s.client_count = qa_save_reader_list_count(&f);
  s.clients = calloc(s.client_count ? s.client_count : 1, sizeof *s.clients);
  for (size_t i = 0; i < s.client_count; i++) { qa_save_reader it; qa_save_reader_item(&f, i, &it); s.clients[i] = read_client(&it); qa_save_reader_destroy(&it); }
  qa_save_reader_destroy(&f);
  qa_save_reader_field(&reader, "numEntities", &f); s.num_entities = qa_save_reader_integer_min(&f, 64); qa_save_reader_destroy(&f);
  qa_save_reader_field(&reader, "maxClients", &f); s.max_clients = qa_save_reader_integer_min(&f, 1); qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&reader); return s;
}
