/* Port of src/content/q1/addons/coop-spawns.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/coop_spawns.h"

#include <stdio.h>
#include <string.h>

#include "qa/foundation/js.h"

#define SPAWN_START_ACTIVE 1

static const struct { const char *weapons[2]; size_t weapon_count; const char *item; double count; } FLOORS[] = {
  {{"nailgun", "supernailgun"}, 2, "q1:ammo/nails", 30},
  {{"grenadelauncher", "rocketlauncher"}, 2, "q1:ammo/rockets", 4},
  {{"lightning", NULL}, 1, "q1:ammo/cells", 12},
};

static void spawn_coop_point(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (!game->options.coop) { qa_q1_remove(game, entity); return; }
  entity->activated = entity->targetname[0] == '\0' || (qa_js_to_int32(entity->spawnflags) & SPAWN_START_ACTIVE) != 0;
}
static void spawn_activate_coop_spawns(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (!game->options.coop) { qa_q1_remove(game, entity); return; }
  entity->use = qa_q1_callback_registry_use(game->named, entity, "mg:coop_spawns_use");
}
static void coop_spawns_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                            const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  /* held: Set<string> in WEAPONS order of first insertion (players outer, weapons inner). */
  const char *held[8];
  size_t held_count = 0, count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t p = 0; p < count; p++)
    for (size_t w = 0; w < 8; w++) {
      if (qa_shared_inventory_table_count(game->host.inventory, players[p], qa_q1_services_weapon_item(game, QA_Q1_WEAPONS[w])) <= 0)
        continue;
      bool seen = false;
      for (size_t h = 0; h < held_count; h++) seen = seen || strcmp(held[h], QA_Q1_WEAPONS[w]) == 0;
      if (!seen) held[held_count++] = QA_Q1_WEAPONS[w];
    }
  char joined[128] = ""; /* [...held].join(" ") */
  size_t length = 0;
  for (size_t h = 0; h < held_count; h++)
    length += (size_t)snprintf(joined + length, sizeof joined - length, h == 0 ? "%s" : " %s", held[h]);
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *spot = game->entities[i];
    if (strcmp(spot->classname, "info_player_coop") != 0) continue;
    spot->activated = strcmp(spot->targetname, entity->target) == 0;
    if (spot->activated) (void)qa_q1_actor_field_set(spot, "coop.weapons", joined);
    else (void)qa_q1_actor_field_delete(spot, "coop.weapons");
  }
}

void qa_q1ad_register_coop_spawns(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_register_spawn(game, "info_player_coop", spawn_coop_point, context);
  qa_q1_register_spawn(game, "trigger_activate_coop_spawns", spawn_activate_coop_spawns, context);
  qa_q1_callback_registry_register(game->named, "mg:coop_spawns_use", (qa_q1_callback_handlers){.ctx = context, .use = coop_spawns_use});
}

/* held.includes(name) over spot.text("coop.weapons").split(" ").filter(name => name !== ""). */
static bool holds(const char *list, const char *name) {
  size_t length = strlen(name);
  for (const char *cursor = list; *cursor != '\0';) {
    const char *space = strchr(cursor, ' ');
    size_t token = space != NULL ? (size_t)(space - cursor) : strlen(cursor);
    if (token == length && strncmp(cursor, name, length) == 0) return true;
    if (space == NULL) break;
    cursor = space + 1;
  }
  return false;
}
static bool any_token(const char *list) {
  for (const char *cursor = list; *cursor != '\0'; cursor++) if (*cursor != ' ') return true;
  return false;
}

void qa_q1ad_restore_coop_weapons(qa_q1ad_context *context, qa_actor_id actor, qa_q1_actor *spot) {
  qa_q1_services *game = context->game;
  qa_owned_actor owner;
  bool owned = qa_session_actor_registry_resolve_owned(game->host.actors, actor, &owner);
  const char *held = qa_q1_actor_text(spot, "coop.weapons");
  if (!owned || !game->options.coop || !any_token(held)) return;
  for (size_t w = 0; w < 8; w++) {
    qa_item_id item = qa_q1_services_weapon_item(game, QA_Q1_WEAPONS[w]);
    if (holds(held, QA_Q1_WEAPONS[w]) && qa_shared_inventory_table_count(game->host.inventory, actor, item) == 0)
      (void)qa_shared_inventory_table_give(game->host.inventory, owner, item, 1);
  }
  for (size_t f = 0; f < sizeof FLOORS / sizeof FLOORS[0]; f++) {
    bool any = false;
    for (size_t w = 0; w < FLOORS[f].weapon_count; w++) any = any || holds(held, FLOORS[f].weapons[w]);
    if (!any) continue;
    double missing = FLOORS[f].count - qa_shared_inventory_table_count(game->host.inventory, actor, FLOORS[f].item);
    if (missing > 0) (void)qa_shared_inventory_table_give(game->host.inventory, owner, FLOORS[f].item, missing);
  }
}
