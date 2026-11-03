/* Port of src/content/q2/missionpacks/monsters/power-armor.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/power_armor.h"

#include <stdlib.h>
#include <string.h>


/* TS WeakSet<OwnedActor> of bound actors. */
static qa_owned_actor *bindings;
static size_t binding_count, binding_cap;
static bool binding_has(qa_owned_actor actor) {
  for (size_t i = 0; i < binding_count; i++)
    if (bindings[i].token == actor.token && qa_same_actor(bindings[i].id, actor.id)) return true;
  return false;
}
static void binding_add(qa_owned_actor actor) {
  if (binding_count == binding_cap) {
    size_t cap = binding_cap ? binding_cap * 2 : 8;
    qa_owned_actor *grown = realloc(bindings, cap * sizeof *grown);
    if (!grown) qa_raise("Error", 0, 0, "Out of memory recording monster power armor binding");
    bindings = grown;
    binding_cap = cap;
  }
  bindings[binding_count++] = actor;
}

typedef struct cell_ctx {
  qa_q2_game_services *game;
  qa_owned_actor actor;
  double capacity;
} cell_ctx;
static double cell_read(void *ctx) {
  cell_ctx *cell = ctx;
  return qa_shared_inventory_table_count(cell->game->host->inventory, cell->actor.id, "q2:monster-power");
}
static void cell_write(void *ctx, double count) {
  cell_ctx *cell = ctx;
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "q2:monster-power";
  entry.count = count;
  entry.capacity = cell->capacity;
  qa_shared_inventory_table_configure(cell->game->host->inventory, cell->actor, entry, NULL, NULL);
}

void qa_q2mpm_restore_monster_power_armor(qa_q2m_context *context) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  if (binding_has(entity->actor)) return;
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host->inventory, entity->actor.id, &count);
  const qa_inventory_entry *found = NULL;
  for (size_t i = 0; i < count; i++)
    if (strcmp(entries[i].item, "q2:monster-power") == 0) { found = &entries[i]; break; }
  if (!found) qa_raise("Error", 0, 0, "Monster power armor is missing its shared inventory cells");
  cell_ctx *cell = malloc(sizeof *cell);
  if (!cell) qa_raise("Error", 0, 0, "Out of memory binding monster power armor cells");
  cell->game = game;
  cell->actor = entity->actor;
  cell->capacity = found->capacity;
  qa_power_armor_cell_binding binding = {cell, cell_read, cell_write};
  qa_gameplay_authority_bind_power_armor_cells(game->host->combat, entity->actor, binding);
  binding_add(entity->actor);
}

void qa_q2mpm_monster_power_armor(qa_q2m_context *context, const char *kind, double cells) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  if (!qa_shared_inventory_table_has(game->host->inventory, entity->actor.id))
    qa_shared_inventory_table_create_entries(game->host->inventory, entity->actor, NULL, 0);
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "q2:monster-power";
  entry.count = cells;
  entry.capacity = cells;
  qa_shared_inventory_table_configure(game->host->inventory, entity->actor, entry, NULL, NULL);
  qa_q2mpm_restore_monster_power_armor(context);
  qa_powered_protection_state powered = {kind, cells};
  qa_gameplay_authority_set_powered_protection(game->host->combat, entity->actor, powered);
}

void qa_q2mpm_restore_monster_power_armor_handler(void *ctx, qa_q2m_context *context) {
  (void)ctx;
  qa_q2mpm_restore_monster_power_armor(context);
}
