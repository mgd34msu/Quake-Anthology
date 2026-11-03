/* Port of src/content/q3/base/map-spawns.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/map_spawns.h"
#include "qa/foundation/js.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/team_arena/client_spawn.h"
#include "qa/foundation/support.h"

typedef struct handler_table {
  qa_q3_spawn_handler_entry *out;
  size_t capacity, count;
} handler_table;
static void put(handler_table *table, const char *classname, qa_q3_spawn_handler_fn fn, void *ctx) {
  for (size_t i = 0; i < table->count; i++) {
    if (strcmp(table->out[i].classname, classname) == 0) {
      table->out[i].fn = fn;
      table->out[i].ctx = ctx;
      return;
    }
  }
  if (table->count < table->capacity) {
    table->out[table->count].classname = classname;
    table->out[table->count].fn = fn;
    table->out[table->count].ctx = ctx;
  }
  table->count++;
}
static void put_all(handler_table *table, const qa_q3_spawn_handler_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) put(table, entries[i].classname, entries[i].fn, entries[i].ctx);
}
static const qa_q3_spawn_handler_entry *find_entry(handler_table *table, const char *classname) {
  size_t stored = table->count < table->capacity ? table->count : table->capacity;
  for (size_t i = 0; i < stored; i++)
    if (strcmp(table->out[i].classname, classname) == 0) return &table->out[i];
  return NULL;
}
static void spawn_player_start_handler(void *ctx, qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables) {
  (void)ctx; qa_q3_ta_spawn_player_start(entity, variables);
}
static void spawn_deathmatch_handler(void *ctx, qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables) {
  (void)ctx; qa_q3_ta_spawn_deathmatch_point(entity, variables);
}
static void spawn_empty_handler(void *ctx, qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables) {
  (void)ctx; (void)entity; (void)variables;
}
static void spawn_team_point_handler(void *ctx, qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables) {
  (void)ctx; (void)variables; qa_q3_ta_spawn_team_point(entity);
}
static void spawn_red_obelisk_handler(void *ctx, qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables) {
  (void)variables;
  qa_q3_ta_team_runtime_spawn_team_obelisk((qa_q3_ta_team_runtime *)ctx, entity, QA_Q3_BASE_TEAM_RED);
}
static void spawn_blue_obelisk_handler(void *ctx, qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables) {
  (void)variables;
  qa_q3_ta_team_runtime_spawn_team_obelisk((qa_q3_ta_team_runtime *)ctx, entity, QA_Q3_BASE_TEAM_BLUE);
}
static void spawn_neutral_obelisk_handler(void *ctx, qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables) {
  (void)variables;
  qa_q3_ta_team_runtime_spawn_neutral_obelisk((qa_q3_ta_team_runtime *)ctx, entity);
}

size_t qa_q3_base_create_spawn_handlers(qa_q3_base_spawn_handlers_host host,
    qa_q3_spawn_handler_entry *out, size_t out_cap) {
  handler_table table = {out, out_cap, 0};
  put(&table, "info_player_start", spawn_player_start_handler, NULL);
  put(&table, "info_player_deathmatch", spawn_deathmatch_handler, NULL);
  put(&table, "info_player_intermission", spawn_empty_handler, NULL);
  put(&table, "item_botroam", spawn_empty_handler, NULL);
  put(&table, "team_CTF_redplayer", spawn_team_point_handler, NULL);
  put(&table, "team_CTF_blueplayer", spawn_team_point_handler, NULL);
  put(&table, "team_CTF_redspawn", spawn_team_point_handler, NULL);
  put(&table, "team_CTF_bluespawn", spawn_team_point_handler, NULL);
  qa_q3_spawn_handler_entry scratch[128];
  size_t count = qa_q3_misc_spawn_handlers(host.misc, scratch, 128);
  put_all(&table, scratch, count < 128 ? count : 128);
  count = qa_q3_mover_spawn_runtime_handlers(host.movers, scratch, 128);
  put_all(&table, scratch, count < 128 ? count : 128);
  count = qa_q3_trigger_spawn_handlers(host.triggers, scratch, 128);
  put_all(&table, scratch, count < 128 ? count : 128);
  count = qa_q3_target_spawn_handlers(host.targets, scratch, 128);
  put_all(&table, scratch, count < 128 ? count : 128);
  const qa_q3_spawn_handler_entry *remove = find_entry(&table, "info_null");
  if (remove == NULL) qa_range_error("Source info_null handler is unavailable");
  qa_q3_spawn_handler_fn remove_fn = remove->fn;
  void *remove_ctx = remove->ctx;
  put(&table, "func_group", remove_fn, remove_ctx);
  if (host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    put(&table, "team_redobelisk", spawn_red_obelisk_handler, host.team);
    put(&table, "team_blueobelisk", spawn_blue_obelisk_handler, host.team);
    put(&table, "team_neutralobelisk", spawn_neutral_obelisk_handler, host.team);
  }
  return table.count;
}

qa_q3_base_entity_teams qa_q3_base_find_entity_teams(qa_q3_entity_pool *pool) {
  double teams = 0, entities = 0;
  for (double index = 1; index < pool->num_entities; index++) {
    qa_q3_game_entity *master = qa_q3_entity_pool_at(pool, index);
    if (!qa_q3_game_entity_inuse(master) || master->team == NULL ||
        (qa_js_to_int32(master->flags) & QA_Q3_GAME_FLAG_TEAMSLAVE) != 0) continue;
    master->teammaster = master;
    teams++;
    entities++;
    for (double next = index + 1; next < pool->num_entities; next++) {
      qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool, next);
      if (!qa_q3_game_entity_inuse(entity) || entity->team == NULL ||
          (qa_js_to_int32(entity->flags) & QA_Q3_GAME_FLAG_TEAMSLAVE) != 0 ||
          strcmp(entity->team, master->team) != 0) continue;
      entities++;
      entity->teamchain = master->teamchain;
      master->teamchain = entity;
      entity->teammaster = master;
      entity->flags = (double)(qa_js_to_int32(entity->flags) | QA_Q3_GAME_FLAG_TEAMSLAVE);
      if (entity->targetname != NULL) {
        /* TS transfers the string reference; free the replaced owned copy. */
        free(master->targetname);
        master->targetname = entity->targetname;
        entity->targetname = NULL;
      }
    }
  }
  qa_q3_base_entity_teams result = {teams, entities};
  return result;
}
