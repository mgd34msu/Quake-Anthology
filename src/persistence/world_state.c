/* Port of src/persistence/world-state.ts. readPrimaryProtection is
 * qa_read_primary_protection from src/persistence/protection.ts
 * (protection.h); its bool is false only on OOM, which restore
 * reports as the "world:primary-protection" out-of-memory error
 * the former mirrored static raised. GPL-2.0-or-later. */
#include "qa/persistence/world_state.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/persistence/protection.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"

static bool same_text(const char *left, const char *right) {
  if (left == NULL || right == NULL) return left == right;
  return strcmp(left, right) == 0;
}
/* isDeepStrictEqual / Object.is number equality for doubles. */
static bool same_number(double left, double right) {
  if (left == right) return left != 0 || signbit(left) == signbit(right);
  return isnan(left) && isnan(right);
}
static bool same_owned(qa_owned_actor left, qa_owned_actor right) {
  return left.token == right.token && qa_same_actor(left.id, right.id) &&
         same_text(left.owner, right.owner);
}
static bool same_vec3(qa_vec3 left, qa_vec3 right) {
  return same_number(left.x, right.x) && same_number(left.y, right.y) &&
         same_number(left.z, right.z);
}
static bool same_body(qa_body_state body, qa_saved_body_state saved) {
  return same_vec3(body.origin, saved.origin) &&
         same_vec3(body.angles, saved.angles) &&
         same_vec3(body.velocity, saved.velocity) &&
         same_vec3(body.bounds.min, saved.bounds.min) &&
         same_vec3(body.bounds.max, saved.bounds.max);
}
static bool same_regular(qa_regular_armor_state left, qa_regular_armor_state right) {
  if (left.kind != right.kind) return false;
  switch (left.kind) {
    case QA_REGULAR_ARMOR_NONE: return true;
    case QA_REGULAR_ARMOR_Q1:
      return same_number(left.points, right.points) &&
             same_number(left.absorption, right.absorption) &&
             same_text(left.item, right.item);
    case QA_REGULAR_ARMOR_Q2:
      return same_number(left.points, right.points) &&
             same_number(left.normal_protection, right.normal_protection) &&
             same_number(left.energy_protection, right.energy_protection) &&
             same_text(left.item, right.item);
    case QA_REGULAR_ARMOR_Q3:
      return same_number(left.points, right.points) &&
             same_number(left.protection, right.protection);
    case QA_REGULAR_ARMOR_SOURCE:
      return same_number(left.points, right.points) &&
             same_text(left.item, right.item);
  }
  return false;
}
static bool same_powered(qa_powered_protection_state left,
                         qa_powered_protection_state right) {
  if (!same_text(left.kind, right.kind)) return false;
  return same_text(left.kind, "none") || same_number(left.cells, right.cells);
}
static bool same_combat(const qa_combat_state *left, const qa_combat_state *right) {
  return same_number(left->health, right->health) &&
         same_regular(left->armor.regular, right->armor.regular) &&
         same_powered(left->armor.powered, right->armor.powered) &&
         same_number(left->mass, right->mass) &&
         left->can_take_damage == right->can_take_damage &&
         left->invulnerable == right->invulnerable &&
         left->has_no_knockback == right->has_no_knockback &&
         (!left->has_no_knockback || left->no_knockback == right->no_knockback) &&
         same_text(left->team, right->team);
}
static bool same_inventory_entry(const qa_inventory_entry *left,
                                 const qa_inventory_entry *right) {
  if (!same_text(left->item, right->item) || !same_number(left->count, right->count) ||
      !same_number(left->capacity, right->capacity) ||
      left->has_count_policy != right->has_count_policy) return false;
  if (!left->has_count_policy) return true;
  return same_text(left->count_policy.kind, right->count_policy.kind) &&
         same_text(left->count_policy.arithmetic, right->count_policy.arithmetic);
}
static bool same_inventory_entries(const qa_inventory_entry *left, size_t left_count,
                                   const qa_inventory_entry *right, size_t right_count) {
  if (left_count != right_count) return false;
  for (size_t i = 0; i < left_count; i++)
    if (!same_inventory_entry(&left[i], &right[i])) return false;
  return true;
}
static qa_regular_armor_state none_regular(void) {
  qa_regular_armor_state armor;
  memset(&armor, 0, sizeof armor);
  armor.kind = QA_REGULAR_ARMOR_NONE;
  return armor;
}
static qa_powered_protection_state none_powered(void) {
  qa_powered_protection_state powered;
  memset(&powered, 0, sizeof powered);
  powered.kind = "none";
  return powered;
}
static qa_owned_actor require_actor(const qa_session_actor_registry *actors,
                                    qa_saved_actor_id saved, const char *path) {
  qa_owned_actor restored;
  if (!actors->resolve_saved(actors->ctx, saved, &restored))
    qa_save_format_error(path, "missing saved actor %.17g/%.17g", saved.slot,
                         saved.generation);
  return restored;
}
static qa_world_storage_kind storage_of(const qa_shared_world_restore_host *host,
                                        qa_owned_actor actor) {
  return host->storage(host->storage_ctx, actor);
}

/* TS readPrimaryProtection now comes from persistence/protection.h
 * (qa_read_primary_protection); only the Map lookup stays local. */
static const qa_primary_protection_entry *hidden_for(
    const qa_primary_protection *hidden, qa_owned_actor actor) {
  for (size_t i = 0; i < hidden->entry_count; i++)
    if (same_owned(hidden->entries[i].actor, actor)) return &hidden->entries[i];
  return NULL;
}

/* --- captureSharedBodies --- */
/* Fills the value fields and a malloc'd ground id (or NULL).
 * False only on OOM. */
static bool save_body(const qa_body_state *state, qa_saved_body_state *out) {
  out->origin = state->origin;
  out->angles = state->angles;
  out->velocity = state->velocity;
  out->bounds = state->bounds;
  out->ground = NULL;
  if (state->ground == NULL) return true;
  qa_saved_actor_id *ground = (qa_saved_actor_id *)malloc(sizeof *ground);
  if (ground == NULL) return false;
  *ground = qa_saved_actor_id_of(*state->ground);
  out->ground = ground;
  return true;
}
void qa_body_checkpoints_free(qa_body_checkpoint *checkpoints, size_t count) {
  if (checkpoints == NULL) return;
  for (size_t i = 0; i < count; i++) {
    free((void *)checkpoints[i].body.ground);
    free((void *)checkpoints[i].attachment);
    if (checkpoints[i].linked != NULL) {
      free((void *)checkpoints[i].linked->state.ground);
      free((void *)checkpoints[i].linked);
    }
  }
  free(checkpoints);
}
bool qa_capture_shared_bodies(const qa_session_actor_registry *actors,
                              const qa_shared_body_table *bodies,
                              qa_body_checkpoint **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  qa_body_checkpoint *result = NULL;
  size_t count = 0;
  size_t capacity = 0;
  size_t observation_count = 0;
  const qa_actor_observation *observations =
      actors->observations(actors->ctx, &observation_count);
  for (size_t i = 0; i < observation_count; i++) {
    qa_actor_id id = observations[i].id;
    qa_body_state body;
    bool has_body = bodies->read(bodies->ctx, id, &body);
    qa_body_link_state links;
    bool has_links = bodies->link_state(bodies->ctx, id, &links);
    qa_body_attachment attachment;
    bool has_attachment = bodies->attachment(bodies->ctx, id, &attachment);
    if (!has_body || !has_links) continue;
    if (count == capacity) {
      size_t grown = capacity == 0 ? 8 : capacity * 2;
      qa_body_checkpoint *grown_result =
          (qa_body_checkpoint *)realloc(result, grown * sizeof *grown_result);
      if (grown_result == NULL) {
        qa_body_checkpoints_free(result, count);
        return false;
      }
      result = grown_result;
      capacity = grown;
    }
    qa_body_checkpoint *checkpoint = &result[count];
    memset(checkpoint, 0, sizeof *checkpoint);
    checkpoint->actor = qa_saved_actor_id_of(id);
    checkpoint->link_count = links.link_count;
    bool ok = save_body(&body, &checkpoint->body);
    if (ok && has_attachment) {
      qa_saved_body_attachment *saved =
          (qa_saved_body_attachment *)malloc(sizeof *saved);
      ok = saved != NULL;
      if (ok) {
        saved->anchor = qa_saved_actor_id_of(attachment.anchor);
        saved->follow_kind = attachment.follow_kind;
        saved->follow_offset = attachment.follow_offset;
        checkpoint->attachment = saved;
      }
    }
    if (ok && links.has_linked) {
      qa_body_checkpoint_linked *linked =
          (qa_body_checkpoint_linked *)calloc(1, sizeof *linked);
      ok = linked != NULL;
      if (ok) {
        linked->absolute_bounds = links.absolute_bounds;
        checkpoint->linked = linked;
        ok = save_body(&links.state, &linked->state);
      }
    }
    if (!ok) {
      count++;
      qa_body_checkpoints_free(result, count);
      return false;
    }
    count++;
  }
  *out = result;
  *out_count = count;
  return true;
}

/* --- restoreSharedBodyLinks --- */
void qa_restore_shared_body_links(const qa_save_image *save,
                                  const qa_shared_body_link_host *host) {
  for (size_t i = 0; i < save->body_count; i++) {
    const qa_body_checkpoint *entry = &save->bodies[i];
    qa_owned_actor actor =
        require_actor(&host->actors, entry->actor, "world.body-links");
    if (host->storage != NULL &&
        host->storage(host->storage_ctx, actor) ==
            QA_WORLD_STORAGE_SOURCE_RECONSTRUCTED) {
      qa_body_state body;
      if (!host->bodies.read(host->bodies.ctx, actor.id, &body))
        qa_save_format_error("world.body-links",
                             "reconstructed source body has not been bound");
      continue;
    }
    qa_body_link_state state;
    memset(&state, 0, sizeof state);
    state.link_count = entry->link_count;
    qa_actor_id ground;
    if (entry->linked != NULL) {
      state.has_linked = true;
      state.absolute_bounds = entry->linked->absolute_bounds;
      state.state.origin = entry->linked->state.origin;
      state.state.angles = entry->linked->state.angles;
      state.state.velocity = entry->linked->state.velocity;
      state.state.bounds = entry->linked->state.bounds;
      if (entry->linked->state.ground != NULL) {
        ground = host->actors.reference_saved(host->actors.ctx,
                                              *entry->linked->state.ground);
        state.state.ground = &ground;
      }
    }
    host->bodies.restore_link_state(host->bodies.ctx, actor, state);
  }
}

/* --- restoreSharedWorldState --- */
enum completion_state { COMPLETION_PENDING, COMPLETION_COMPLETE, COMPLETION_FAILED };
struct qa_shared_world_restore_completion {
  const qa_save_image *save;
  qa_shared_world_restore_host *host;
  qa_primary_protection hidden;
  qa_source_items_result source_items;
  /* TS inventories Map: parallel arrays, later rows win on lookup. */
  qa_owned_actor *inventory_actors;
  const qa_inventory_entry **inventory_entries;
  size_t *inventory_counts;
  size_t inventory_count;
  enum completion_state state;
};
static const char *state_name(enum completion_state state) {
  return state == COMPLETION_PENDING ? "pending"
      : state == COMPLETION_COMPLETE ? "complete" : "failed";
}
static bool owned_in(const qa_owned_actor *actors, size_t count, qa_owned_actor actor) {
  for (size_t i = 0; i < count; i++)
    if (same_owned(actors[i], actor)) return true;
  return false;
}
static bool inventory_lookup(const qa_shared_world_restore_completion *context,
                             qa_owned_actor actor,
                             const qa_inventory_entry **out_entries,
                             size_t *out_count) {
  for (size_t i = context->inventory_count; i > 0; i--) {
    if (!same_owned(context->inventory_actors[i - 1], actor)) continue;
    *out_entries = context->inventory_entries[i - 1];
    *out_count = context->inventory_counts[i - 1];
    return true;
  }
  return false;
}
typedef struct source_item_binding {
  bool has_actor;
  qa_owned_actor actor;
  const qa_source_items_record *record;
} source_item_binding;
static const qa_source_items_record *source_items_for(
    const source_item_binding *bindings, size_t count, qa_owned_actor actor) {
  const qa_source_items_record *found = NULL;
  for (size_t i = 0; i < count; i++)
    if (bindings[i].has_actor && same_owned(bindings[i].actor, actor))
      found = bindings[i].record; /* TS Map: the later row wins */
  return found;
}
static void context_free(qa_shared_world_restore_completion *context) {
  qa_primary_protection_free(&context->hidden);
  qa_source_items_result_free(&context->source_items);
  free(context->inventory_actors);
  free(context->inventory_entries);
  free(context->inventory_counts);
  context->inventory_actors = NULL;
  context->inventory_entries = NULL;
  context->inventory_counts = NULL;
  context->inventory_count = 0;
}
void qa_shared_world_restore_completion_free(
    qa_shared_world_restore_completion *completion) {
  if (completion == NULL) return;
  context_free(completion);
  free(completion);
}

/* The shared body of both overloads. When `deferred` is set the
 * caller-owned completion keeps the captured context; otherwise it
 * is freed before returning. */
static void restore_world(const qa_save_image *save,
                          qa_shared_world_restore_host *host, bool deferred,
                          qa_hydrate_inventory_sources_fn hydrate, void *hydrate_ctx,
                          qa_shared_world_restore_completion *context) {
  memset(context, 0, sizeof *context);
  context->save = save;
  context->host = host;
  context->state = COMPLETION_PENDING;
  host->combat.assert_idle(host->combat.ctx);
  if (!qa_read_primary_protection(save, &host->actors, &context->hidden))
    qa_save_format_error("world:primary-protection", "out of memory");
  if (!qa_read_source_items(save, &context->source_items))
    qa_save_format_error("world", "out of memory");
  size_t binding_count = context->source_items.count;
  source_item_binding *bindings = (source_item_binding *)calloc(
      binding_count != 0 ? binding_count : 1, sizeof *bindings);
  qa_owned_actor *body_actors = (qa_owned_actor *)calloc(
      save->body_count != 0 ? save->body_count : 1, sizeof *body_actors);
  qa_owned_actor *combat_actors = (qa_owned_actor *)calloc(
      save->combat_count != 0 ? save->combat_count : 1, sizeof *combat_actors);
  context->inventory_actors = (qa_owned_actor *)calloc(
      save->inventory_count != 0 ? save->inventory_count : 1,
      sizeof *context->inventory_actors);
  context->inventory_entries = (const qa_inventory_entry **)calloc(
      save->inventory_count != 0 ? save->inventory_count : 1,
      sizeof *context->inventory_entries);
  context->inventory_counts = (size_t *)calloc(
      save->inventory_count != 0 ? save->inventory_count : 1,
      sizeof *context->inventory_counts);
  if (bindings == NULL || body_actors == NULL || combat_actors == NULL ||
      context->inventory_actors == NULL || context->inventory_entries == NULL ||
      context->inventory_counts == NULL)
    qa_save_format_error("world", "out of memory");
  context->inventory_count = save->inventory_count;
  for (size_t i = 0; i < binding_count; i++) {
    bindings[i].record = &context->source_items.records[i];
    bindings[i].has_actor = host->actors.resolve_saved(
        host->actors.ctx, bindings[i].record->actor, &bindings[i].actor);
  }
  for (size_t i = 0; i < save->body_count; i++)
    body_actors[i] = require_actor(&host->actors, save->bodies[i].actor, "world");
  for (size_t i = 0; i < save->combat_count; i++)
    combat_actors[i] =
        require_actor(&host->actors, save->combat[i].actor, "world");
  for (size_t i = 0; i < context->hidden.entry_count; i++) {
    qa_owned_actor owner = context->hidden.entries[i].actor;
    if (!owned_in(combat_actors, save->combat_count, owner) ||
        storage_of(host, owner) != QA_WORLD_STORAGE_COPIED)
      qa_save_format_error("world.combat",
                           "hidden armor checkpoint requires a copied combat owner");
  }
  for (size_t i = 0; i < save->inventory_count; i++) {
    context->inventory_actors[i] =
        require_actor(&host->actors, save->inventories[i].actor, "world");
    context->inventory_entries[i] = save->inventories[i].entries;
    context->inventory_counts[i] = save->inventories[i].entry_count;
  }
  for (size_t i = 0; i < save->actor_count; i++) {
    if (!save->actors[i].lifetime_is_active) continue;
    qa_owned_actor restored = require_actor(&host->actors, save->actors[i].id, "world");
    if (storage_of(host, restored) == QA_WORLD_STORAGE_COPIED) continue;
    qa_body_state body;
    qa_combat_state combat;
    const qa_inventory_entry *entries;
    size_t entry_count;
    bool has_body = host->bodies.read(host->bodies.ctx, restored.id, &body);
    bool has_combat = host->combat.read(host->combat.ctx, restored.id, &combat);
    bool has_inventory = host->inventory.has(host->inventory.ctx, restored.id);
    if (has_body != owned_in(body_actors, save->body_count, restored) ||
        has_combat != owned_in(combat_actors, save->combat_count, restored) ||
        has_inventory != inventory_lookup(context, restored, &entries, &entry_count))
      qa_save_format_error("world",
                           "source bindings disagree with saved shared state coverage");
  }
  for (size_t i = 0; i < save->body_count; i++) {
    const qa_body_checkpoint *entry = &save->bodies[i];
    qa_owned_actor restored = require_actor(&host->actors, entry->actor, "world");
    qa_world_storage_kind storage = storage_of(host, restored);
    if (storage != QA_WORLD_STORAGE_COPIED) {
      qa_body_state body;
      if (!host->bodies.read(host->bodies.ctx, restored.id, &body))
        qa_save_format_error("world.bodies", "source body view has not been bound");
      if (storage == QA_WORLD_STORAGE_SOURCE_RECONSTRUCTED) continue;
      qa_actor_id ground;
      bool has_ground = entry->body.ground != NULL;
      if (has_ground)
        ground = host->actors.reference_saved(host->actors.ctx, *entry->body.ground);
      bool same_ground = body.ground == NULL
          ? !has_ground
          : has_ground && qa_same_actor(*body.ground, ground);
      if (!same_ground || !same_body(body, entry->body))
        qa_save_format_error("world.bodies",
                             "source body disagrees with saved shared state");
    } else {
      qa_body_state created;
      memset(&created, 0, sizeof created);
      created.origin = entry->body.origin;
      created.angles = entry->body.angles;
      created.velocity = entry->body.velocity;
      created.bounds = entry->body.bounds;
      qa_actor_id ground;
      if (entry->body.ground != NULL) {
        ground = host->actors.reference_saved(host->actors.ctx, *entry->body.ground);
        created.ground = &ground;
      }
      host->bodies.create(host->bodies.ctx, restored, created);
    }
  }
  for (size_t i = 0; i < save->body_count; i++) {
    const qa_body_checkpoint *entry = &save->bodies[i];
    if (entry->attachment == NULL) continue;
    qa_body_attachment attachment;
    attachment.anchor =
        require_actor(&host->actors, entry->attachment->anchor, "world").id;
    attachment.follow_kind = entry->attachment->follow_kind;
    attachment.follow_offset = entry->attachment->follow_offset;
    host->bodies.attach(host->bodies.ctx,
                        require_actor(&host->actors, entry->actor, "world"),
                        attachment);
  }
  for (size_t i = 0; i < save->combat_count; i++) {
    const qa_combat_checkpoint *entry = &save->combat[i];
    qa_owned_actor restored = require_actor(&host->actors, entry->actor, "world");
    qa_world_storage_kind storage = storage_of(host, restored);
    qa_combat_state saved = entry->state;
    if (!deferred && save->legacy_armor_layout && storage != QA_WORLD_STORAGE_COPIED)
      saved.armor = host->combat.normalize_legacy_armor(host->combat.ctx, restored,
                                                        entry->state.armor);
    if (storage != QA_WORLD_STORAGE_COPIED) {
      qa_combat_state state;
      if (!host->combat.read(host->combat.ctx, restored.id, &state))
        qa_save_format_error("world.combat", "source combat view has not been bound");
      if (!deferred && storage == QA_WORLD_STORAGE_PREBOUND &&
          !same_combat(&state, &saved))
        qa_save_format_error("world.combat",
                             "source combat disagrees with saved shared state");
    } else {
      const qa_primary_protection_entry *primary = hidden_for(&context->hidden, restored);
      if (!deferred && primary != NULL)
        qa_save_format_error("world.combat",
                             "hidden primary armor requires component restoration");
      qa_combat_state created = saved;
      if (deferred) {
        created.armor.regular = primary != NULL && primary->has_regular
            ? primary->regular
            : saved.armor.regular.kind == QA_REGULAR_ARMOR_SOURCE
                  ? none_regular() : saved.armor.regular;
        created.armor.powered = primary != NULL && primary->has_powered
            ? primary->powered : none_powered();
      }
      host->combat.create(host->combat.ctx, restored, created);
    }
  }
  if (hydrate != NULL) hydrate(hydrate_ctx);
  for (size_t i = 0; i < save->inventory_count; i++) {
    const qa_inventory_checkpoint *entry = &save->inventories[i];
    qa_owned_actor restored = require_actor(&host->actors, entry->actor, "world");
    qa_world_storage_kind storage = storage_of(host, restored);
    const qa_source_items_record *source =
        source_items_for(bindings, binding_count, restored);
    const qa_inventory_entry *primary =
        source != NULL ? source->primary : entry->entries;
    size_t primary_count = source != NULL ? source->primary_count : entry->entry_count;
    if (storage != QA_WORLD_STORAGE_COPIED) {
      if (!host->inventory.has(host->inventory.ctx, restored.id))
        qa_save_format_error("world.inventories",
                             "source inventory view has not been bound");
      if (storage == QA_WORLD_STORAGE_PREBOUND) {
        size_t entry_count = 0;
        const qa_inventory_entry *entries =
            host->inventory.entries(host->inventory.ctx, restored.id, &entry_count);
        if (!same_inventory_entries(entries, entry_count, primary, primary_count))
          qa_save_format_error("world.inventories",
                               "source inventory disagrees with saved shared state");
      }
    } else {
      host->inventory.create(host->inventory.ctx, restored, primary, primary_count);
    }
  }
  free(bindings);
  free(body_actors);
  free(combat_actors);
  if (!deferred) context_free(context);
}

void qa_restore_shared_world_state(const qa_save_image *save,
                                   qa_shared_world_restore_host *host) {
  qa_shared_world_restore_completion context;
  restore_world(save, host, false, NULL, NULL, &context);
}
qa_shared_world_restore_completion *qa_restore_shared_world_state_deferred(
    const qa_save_image *save, qa_shared_world_restore_host *host,
    qa_hydrate_inventory_sources_fn hydrate, void *hydrate_ctx) {
  qa_shared_world_restore_completion *completion =
      (qa_shared_world_restore_completion *)calloc(1, sizeof *completion);
  if (completion == NULL) return NULL;
  restore_world(save, host, true, hydrate, hydrate_ctx, completion);
  return completion;
}
void qa_shared_world_restore_assert_complete(
    const qa_shared_world_restore_completion *completion) {
  if (completion->state != COMPLETION_COMPLETE)
    qa_save_format_error("world.combat", "shared restoration is %s",
                         state_name(completion->state));
}

/* TS completion.finish(). Every TS throw inside aborts in C, so the
 * "failed" transition is recorded before each throwing check only
 * insofar as the process survives it; assertComplete still observes
 * "pending"/"complete" exactly. */
void qa_shared_world_restore_finish(qa_shared_world_restore_completion *completion) {
  if (completion->state != COMPLETION_PENDING)
    qa_save_format_error("world.combat", "shared restoration is %s",
                         state_name(completion->state));
  const qa_save_image *save = completion->save;
  qa_shared_world_restore_host *host = completion->host;
  host->combat.assert_idle(host->combat.ctx);
  typedef struct copied_entry {
    qa_owned_actor actor;
    qa_combat_state state;
  } copied_entry;
  copied_entry *copied = (copied_entry *)calloc(
      save->combat_count != 0 ? save->combat_count : 1, sizeof *copied);
  if (copied == NULL) qa_save_format_error("world.combat", "out of memory");
  size_t copied_count = 0;
  for (size_t i = 0; i < save->combat_count; i++) {
    const qa_combat_checkpoint *entry = &save->combat[i];
    qa_owned_actor restored = require_actor(&host->actors, entry->actor, "world");
    qa_world_storage_kind storage = storage_of(host, restored);
    qa_combat_state saved = entry->state;
    if (save->legacy_armor_layout && storage != QA_WORLD_STORAGE_COPIED)
      saved.armor = host->combat.normalize_legacy_armor(host->combat.ctx, restored,
                                                        entry->state.armor);
    qa_combat_state current;
    bool has_current = host->combat.read(host->combat.ctx, restored.id, &current);
    bool external_regular = host->combat.protection_owner(
        host->combat.ctx, restored, QA_PROTECTION_REGULAR) != NULL;
    bool external_power = host->combat.protection_owner(
        host->combat.ctx, restored, QA_PROTECTION_POWERED) != NULL;
    size_t regular_item_count = 0;
    const qa_item_id *regular_items = host->combat.protection_inventory_items(
        host->combat.ctx, restored, QA_PROTECTION_REGULAR, &regular_item_count);
    size_t powered_item_count = 0;
    const qa_item_id *powered_items = host->combat.protection_inventory_items(
        host->combat.ctx, restored, QA_PROTECTION_POWERED, &powered_item_count);
    const qa_primary_protection_entry *primary = hidden_for(&completion->hidden, restored);
    qa_armor_state copied_armor;
    bool owns_copy =
        host->combat.copied_primary_armor(host->combat.ctx, restored, &copied_armor);
    if (!has_current)
      qa_save_format_error("world.combat", "restored combat view has not been bound");
    if (primary != NULL && !owns_copy)
      qa_save_format_error("world.combat",
                           "source binding cannot restore copied primary armor");
    if (owns_copy && (completion->hidden.recorded || external_regular)) {
      bool primary_regular = primary != NULL && primary->has_regular;
      bool primary_powered = primary != NULL && primary->has_powered;
      if (primary_regular != external_regular || primary_powered != external_power)
        qa_save_format_error("world.combat",
            "hidden primary armor coverage differs from restored component ownership");
    }
    if (storage == QA_WORLD_STORAGE_SOURCE_RECONSTRUCTED) {
      const qa_protection_channel channels[2] = {QA_PROTECTION_REGULAR,
                                                 QA_PROTECTION_POWERED};
      const qa_item_id *channel_items[2] = {regular_items, powered_items};
      const size_t channel_counts[2] = {regular_item_count, powered_item_count};
      for (size_t channel = 0; channel < 2; channel++) {
        bool owned = host->combat.protection_owner(
            host->combat.ctx, restored, channels[channel]) != NULL;
        bool channel_same = channels[channel] == QA_PROTECTION_REGULAR
            ? same_regular(current.armor.regular, saved.armor.regular)
            : same_powered(current.armor.powered, saved.armor.powered);
        if (owned && !channel_same)
          qa_save_format_error("world.combat",
              "component %s protection disagrees with saved shared state",
              channels[channel] == QA_PROTECTION_REGULAR ? "regular" : "powered");
        for (size_t k = 0; k < channel_counts[channel]; k++) {
          const char *wanted = channel_items[channel][k];
          const qa_inventory_entry *saved_entries;
          size_t saved_count;
          const qa_inventory_entry *saved_item = NULL;
          if (inventory_lookup(completion, restored, &saved_entries, &saved_count)) {
            for (size_t j = 0; j < saved_count; j++)
              if (same_text(saved_entries[j].item, wanted)) {
                saved_item = &saved_entries[j];
                break;
              }
          }
          size_t current_count = 0;
          const qa_inventory_entry *current_entries =
              host->inventory.entries(host->inventory.ctx, restored.id,
                                      &current_count);
          const qa_inventory_entry *current_item = NULL;
          for (size_t j = 0; j < current_count; j++)
            if (same_text(current_entries[j].item, wanted)) {
              current_item = &current_entries[j];
              break;
            }
          if (saved_item == NULL || current_item == NULL ||
              !same_number(saved_item->count, current_item->count))
            qa_save_format_error("world.inventories",
                "component protection inventory disagrees with saved shared state");
        }
      }
    } else if (storage == QA_WORLD_STORAGE_COPIED && !external_power) {
      qa_combat_state modified = current;
      modified.armor.powered = saved.armor.powered;
      if (!same_combat(&modified, &saved))
        qa_save_format_error("world.combat",
                             "copied combat disagrees with saved shared state");
      copied[copied_count].actor = restored;
      copied[copied_count].state = saved;
      copied_count++;
    } else if (!same_combat(&current, &saved)) {
      qa_save_format_error("world.combat",
                           "source combat disagrees with saved shared state");
    }
  }
  for (size_t i = 0; i < save->inventory_count; i++) {
    const qa_inventory_checkpoint *entry = &save->inventories[i];
    qa_owned_actor restored = require_actor(&host->actors, entry->actor, "world");
    if (storage_of(host, restored) == QA_WORLD_STORAGE_SOURCE_RECONSTRUCTED)
      continue;
    bool bound = host->inventory.has(host->inventory.ctx, restored.id);
    size_t entry_count = 0;
    const qa_inventory_entry *entries =
        bound ? host->inventory.entries(host->inventory.ctx, restored.id,
                                        &entry_count) : NULL;
    if (!bound ||
        !same_inventory_entries(entries, entry_count, entry->entries,
                                entry->entry_count))
      qa_save_format_error("world.inventories",
                           "restored inventory disagrees with saved shared state");
  }
  for (size_t i = 0; i < copied_count; i++) {
    host->combat.set_powered_protection(host->combat.ctx, copied[i].actor,
                                        copied[i].state.armor.powered);
    qa_combat_state current;
    bool has_current =
        host->combat.read(host->combat.ctx, copied[i].actor.id, &current);
    if (!has_current || !same_combat(&current, &copied[i].state))
      qa_save_format_error("world.combat",
          "copied powered protection disagrees with saved shared state");
  }
  free(copied);
  completion->state = COMPLETION_COMPLETE;
}
