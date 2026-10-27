/* Port of src/content/q2/missionpacks/monsters/power-armor.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/power_armor.h"

#include <stdlib.h>
#include <string.h>

static qa_q2mpm_power_hooks power_hooks;
void qa_q2mpm_set_power_hooks(qa_q2mpm_power_hooks hooks) { power_hooks = hooks; }

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
    if (!grown) qa_q2f_error("Out of memory recording monster power armor binding");
    bindings = grown;
    binding_cap = cap;
  }
  bindings[binding_count++] = actor;
}

typedef struct cell_ctx {
  qa_q2f_game *game;
  qa_owned_actor actor;
  double capacity;
} cell_ctx;
static double cell_read(void *ctx) {
  cell_ctx *cell = ctx;
  return cell->game->host->inventory.count(cell->game->host->inventory.ctx, cell->actor.id, "q2:monster-power");
}
static void cell_write(void *ctx, double count) {
  cell_ctx *cell = ctx;
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "q2:monster-power";
  entry.count = count;
  entry.capacity = cell->capacity;
  cell->game->host->inventory.configure(cell->game->host->inventory.ctx, cell->actor, entry);
}

void qa_q2mpm_restore_monster_power_armor(qa_q2m_context *context) {
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  if (binding_has(entity->actor)) return;
  size_t count = 0;
  const qa_inventory_entry *entries = game->host->inventory.entries(game->host->inventory.ctx, entity->actor.id, &count);
  const qa_inventory_entry *found = NULL;
  for (size_t i = 0; i < count; i++)
    if (strcmp(entries[i].item, "q2:monster-power") == 0) { found = &entries[i]; break; }
  if (!found) qa_q2f_error("Monster power armor is missing its shared inventory cells");
  if (!power_hooks.bind_cells) qa_q2f_error("Monster power armor has no registered cell binding hook");
  cell_ctx *cell = malloc(sizeof *cell);
  if (!cell) qa_q2f_error("Out of memory binding monster power armor cells");
  cell->game = game;
  cell->actor = entity->actor;
  cell->capacity = found->capacity;
  qa_power_armor_cell_binding binding = {cell, cell_read, cell_write};
  power_hooks.bind_cells(power_hooks.ctx, entity->actor, binding);
  binding_add(entity->actor);
}

void qa_q2mpm_monster_power_armor(qa_q2m_context *context, const char *kind, double cells) {
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  if (!power_hooks.inventory_has || !power_hooks.set_powered)
    qa_q2f_error("Monster power armor has no registered power hooks");
  if (!power_hooks.inventory_has(power_hooks.ctx, entity->actor.id))
    power_hooks.inventory_create_empty(power_hooks.ctx, entity->actor);
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "q2:monster-power";
  entry.count = cells;
  entry.capacity = cells;
  game->host->inventory.configure(game->host->inventory.ctx, entity->actor, entry);
  qa_q2mpm_restore_monster_power_armor(context);
  power_hooks.set_powered(power_hooks.ctx, entity->actor, kind, cells);
}

void qa_q2mpm_restore_monster_power_armor_handler(void *ctx, qa_q2m_context *context) {
  (void)ctx;
  qa_q2mpm_restore_monster_power_armor(context);
}
