/* Port of src/app/bootstrap/simulation/arsenal/q3-ammo-regen.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal/q3_ammo_regen.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include <math.h>

typedef struct binding { qa_item_id item, source; const qa_q3_ta_ammo_regeneration_rule *rule; } binding;
typedef struct pool_timer {
  qa_item_id item, source;
  const qa_q3_ta_ammo_regeneration_rule *rule;
  double elapsed;
  struct actor_entry *entry;
  size_t index;
  qa_q3_ta_mapped_ammo_timer view;
} pool_timer;
typedef struct actor_entry {
  qa_owned_actor actor;
  pool_timer *timers;
  qa_q3_ta_mapped_ammo_timer *views;
  size_t count;
  struct qa_app_q3_mapped_ammo_regeneration *owner;
} actor_entry;
struct qa_app_q3_mapped_ammo_regeneration {
  qa_pickup_supply_profile profile;
  qa_app_ammo_actor_registry actors;
  qa_app_inventory_access inventory;
  const qa_pickup_supply_owner *legacy_owners;
  size_t legacy_owner_count;
  binding bindings[16];
  size_t binding_count;
  actor_entry entries[64];
  size_t entry_count;
  bool closed;
};

static bool item_eq(qa_item_id a, qa_item_id b) { return a && b && strcmp(a, b) == 0; }
static actor_entry *find_entry(qa_app_q3_mapped_ammo_regeneration *regen, qa_owned_actor actor) {
  for (size_t i = 0; i < regen->entry_count; i++)
    if (qa_same_actor(regen->entries[i].actor.id, actor.id) && regen->entries[i].actor.token == actor.token) return &regen->entries[i];
  return NULL;
}
static bool timer_current(pool_timer *timer) {
  actor_entry *entry = timer->entry;
  qa_app_q3_mapped_ammo_regeneration *regen = entry->owner;
  return !regen->closed && regen->actors.is_live(regen->actors.ctx, entry->actor.id)
      && find_entry(regen, entry->actor) == entry;
}
static qa_inventory_entry read_entry(pool_timer *timer) {
  if (!timer_current(timer)) qa_range_error("Mapped ammo timer actor is retired");
  actor_entry *entry = timer->entry;
  size_t count = 0;
  const qa_inventory_entry *entries = entry->owner->inventory.entries(entry->owner->inventory.ctx, entry->actor.id, &count);
  for (size_t i = 0; i < count; i++) if (item_eq(entries[i].item, timer->item)) return entries[i];
  qa_range_error("Mapped ammo pool is not admitted by the selected inventory");
}
static bool current_cb(void *v) { return timer_current((pool_timer *)v); }
static double get_count_cb(void *v) { return read_entry((pool_timer *)v).count; }
static void set_count_cb(void *v, double value) {
  pool_timer *timer = (pool_timer *)v;
  qa_inventory_entry entry = read_entry(timer);
  entry.count = value;
  timer->entry->owner->inventory.configure(timer->entry->owner->inventory.ctx, timer->entry->actor, entry);
}
static double get_elapsed_cb(void *v) { return ((pool_timer *)v)->elapsed; }
static void set_elapsed_cb(void *v, double value) { ((pool_timer *)v)->elapsed = value; }

static actor_entry *require_entry(qa_app_q3_mapped_ammo_regeneration *regen, qa_owned_actor actor) {
  if (regen->closed) qa_range_error("Mapped ammo timer owner is closed");
  regen->actors.assert_owned(regen->actors.ctx, actor);
  actor_entry *retained = find_entry(regen, actor);
  if (retained) return retained;
  if (regen->entry_count >= 64) qa_range_error("Out of memory");
  actor_entry *entry = &regen->entries[regen->entry_count];
  memset(entry, 0, sizeof *entry);
  entry->actor = actor; entry->owner = regen; entry->count = regen->binding_count;
  entry->timers = calloc(entry->count ? entry->count : 1, sizeof *entry->timers);
  entry->views = calloc(entry->count ? entry->count : 1, sizeof *entry->views);
  if (!entry->timers || !entry->views) qa_range_error("Out of memory");
  for (size_t i = 0; i < entry->count; i++) {
    pool_timer *timer = &entry->timers[i];
    timer->item = regen->bindings[i].item; timer->source = regen->bindings[i].source;
    timer->rule = regen->bindings[i].rule; timer->entry = entry; timer->index = i;
    entry->views[i].rule = timer->rule; entry->views[i].ctx = timer;
    entry->views[i].current = current_cb; entry->views[i].get_count = get_count_cb;
    entry->views[i].set_count = set_count_cb; entry->views[i].get_elapsed = get_elapsed_cb;
    entry->views[i].set_elapsed = set_elapsed_cb;
    timer->view = entry->views[i];
  }
  regen->entry_count++;
  for (size_t i = 0; i < entry->count; i++) (void)read_entry(&entry->timers[i]);
  return entry;
}
static void free_entry(actor_entry *entry) { free(entry->timers); free(entry->views); entry->timers = NULL; entry->views = NULL; }

qa_app_q3_mapped_ammo_regeneration *qa_app_q3_mapped_ammo_regeneration_create(qa_pickup_supply_profile profile,
    qa_app_ammo_actor_registry actors, qa_app_inventory_access inventory,
    const qa_pickup_supply_owner *legacy_owners, size_t legacy_owner_count) {
  qa_app_q3_mapped_ammo_regeneration *regen = calloc(1, sizeof *regen);
  if (!regen) return NULL;
  regen->profile = profile; regen->actors = actors; regen->inventory = inventory;
  regen->legacy_owners = legacy_owners; regen->legacy_owner_count = legacy_owner_count;
  size_t owner_count = profile.ammo_owners ? profile.ammo_owner_count : 0;
  for (size_t i = 0; i < owner_count; i++) {
    qa_pickup_supply_owner owner = profile.ammo_owners[i];
    for (size_t j = 0; j < regen->binding_count; j++)
      if (item_eq(regen->bindings[j].item, owner.item)) qa_range_error("Periodic ammo pool has multiple owners");
    const qa_q3_weapon_item *source = NULL;
    for (size_t j = 0; j < QA_Q3_WEAPON_ITEM_COUNT; j++)
      if (qa_q3_weapon_items[j].ammo && item_eq(qa_q3_weapon_items[j].ammo, owner.source)) source = &qa_q3_weapon_items[j];
    if (!source) qa_range_error("Periodic ammo rule requires original source ammunition");
    if (regen->binding_count >= 16) qa_range_error("Out of memory");
    regen->bindings[regen->binding_count].item = owner.item;
    regen->bindings[regen->binding_count].source = owner.source;
    regen->bindings[regen->binding_count].rule = qa_q3_ta_ammo_regeneration_rule_for(source->weapon);
    regen->binding_count++;
  }
  return regen;
}
void qa_app_q3_mapped_ammo_regeneration_destroy(qa_app_q3_mapped_ammo_regeneration *regen) {
  if (!regen) return;
  qa_app_q3_mapped_ammo_regeneration_close(regen);
  free(regen);
}
const qa_pickup_supply_profile *qa_app_q3_mapped_ammo_regeneration_profile(const qa_app_q3_mapped_ammo_regeneration *regen) {
  return &regen->profile;
}
void qa_app_q3_mapped_ammo_regeneration_release_actor(qa_app_q3_mapped_ammo_regeneration *regen, qa_owned_actor actor) {
  for (size_t i = 0; i < regen->entry_count; i++) {
    if (qa_same_actor(regen->entries[i].actor.id, actor.id) && regen->entries[i].actor.token == actor.token) {
      free_entry(&regen->entries[i]);
      memmove(regen->entries + i, regen->entries + i + 1, (regen->entry_count - i - 1) * sizeof *regen->entries);
      regen->entry_count--;
      for (size_t j = i; j < regen->entry_count; j++)
        for (size_t k = 0; k < regen->entries[j].count; k++) regen->entries[j].timers[k].entry = &regen->entries[j];
      return;
    }
  }
}
const qa_q3_ta_mapped_ammo_timer *qa_app_q3_mapped_ammo_regeneration_timers(qa_app_q3_mapped_ammo_regeneration *regen,
    qa_owned_actor actor, size_t *out_count) {
  actor_entry *entry = require_entry(regen, actor);
  *out_count = entry->count;
  return entry->views;
}
void qa_app_q3_mapped_ammo_regeneration_stored(qa_app_q3_mapped_ammo_regeneration *regen, qa_actor_id actor, double weapon, double value) {
  qa_owned_actor owner;
  if (!regen->actors.resolve_owned(regen->actors.ctx, actor, &owner)) return;
  actor_entry *entry = find_entry(regen, owner);
  if (!entry) return;
  for (size_t i = 0; i < entry->count; i++) if (entry->timers[i].rule->weapon == weapon) entry->timers[i].elapsed = value;
}

static qa_save_value *saved_actor_value(qa_saved_actor_id saved) {
  qa_save_value *object = qa_save_value_object();
  if (!object) return NULL;
  if (!qa_save_value_set(object, "slot", qa_save_value_number(saved.slot))
      || !qa_save_value_set(object, "generation", qa_save_value_number(saved.generation))) {
    qa_save_value_free(object); return NULL;
  }
  return object;
}
qa_save_value *qa_app_q3_mapped_ammo_regeneration_capture(qa_app_q3_mapped_ammo_regeneration *regen,
    const qa_owned_actor *actors, size_t actor_count) {
  qa_save_value *root = qa_save_value_object(), *list = qa_save_value_array();
  if (!root || !list) { qa_save_value_free(root); qa_save_value_free(list); return NULL; }
  bool ok = qa_save_value_set(root, "profile", qa_save_value_string(regen->profile.id));
  for (size_t i = 0; ok && i < actor_count; i++) {
    actor_entry *entry = require_entry(regen, actors[i]);
    qa_save_value *saved = qa_save_value_object(), *timers = qa_save_value_array();
    ok = saved && timers
        && qa_save_value_set(saved, "actor", saved_actor_value(qa_saved_actor_id_of(actors[i].id)));
    for (size_t j = 0; ok && j < entry->count; j++) {
      qa_save_value *timer = qa_save_value_object();
      ok = timer && qa_save_value_set(timer, "item", qa_save_value_string(entry->timers[j].item))
          && qa_save_value_set(timer, "source", qa_save_value_string(entry->timers[j].source))
          && qa_save_value_set(timer, "elapsedMilliseconds", qa_save_value_number(entry->timers[j].elapsed))
          && qa_save_value_push(timers, timer);
      if (!ok) qa_save_value_free(timer);
    }
    ok = ok && qa_save_value_set(saved, "timers", timers) && qa_save_value_push(list, saved);
    if (!ok) { qa_save_value_free(saved); if (timers && !qa_save_value_get(saved, "timers")) qa_save_value_free(timers); }
  }
  ok = ok && qa_save_value_set(root, "actors", list);
  if (!ok) { qa_save_value_free(root); return NULL; }
  return root;
}

static double elapsed_value(const qa_save_reader *reader, pool_timer *timer, double value) {
  if (!(isfinite(value) && value == trunc(value)) || value < 0 || value >= timer->rule->time)
    qa_save_reader_fail(reader, "Invalid original ammo regeneration counter");
  return value;
}
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
void qa_app_q3_mapped_ammo_regeneration_restore(qa_app_q3_mapped_ammo_regeneration *regen, const qa_save_reader *reader,
    const qa_owned_actor *actors, size_t actor_count,
    double (*legacy)(void *ctx, qa_actor_id actor, double weapon), void *legacy_ctx) {
  if (qa_save_reader_is_undefined(reader)) {
    for (size_t i = 0; i < actor_count; i++) {
      actor_entry *entry = require_entry(regen, actors[i]);
      for (size_t j = 0; j < entry->count; j++) {
        bool previous = false;
        for (size_t k = 0; k < regen->legacy_owner_count; k++)
          if (item_eq(regen->legacy_owners[k].item, entry->timers[j].item) && item_eq(regen->legacy_owners[k].source, entry->timers[j].source)) previous = true;
        entry->timers[j].elapsed = previous ? elapsed_value(reader, &entry->timers[j], legacy(legacy_ctx, actors[i].id, entry->timers[j].rule->weapon)) : 0;
      }
    }
    return;
  }
  if (qa_save_reader_is_null(reader)) qa_save_reader_fail(reader, "Mapped ammo profile differs from selected supply");
  qa_save_reader profile = field(reader, "profile");
  const char *profile_id = qa_namespaced(&profile);
  qa_save_reader_destroy(&profile);
  if (!profile_id || strcmp(profile_id, regen->profile.id) != 0)
    qa_save_reader_fail(reader, "Mapped ammo profile differs from selected supply");
  bool remaining[64] = {0};
  for (size_t i = 0; i < actor_count && i < 64; i++) remaining[i] = true;
  qa_save_reader saved_actors = field(reader, "actors");
  size_t saved_count = qa_save_reader_list_count(&saved_actors);
  for (size_t i = 0; i < saved_count; i++) {
    qa_save_reader saved;
    if (!qa_save_reader_item(&saved_actors, i, &saved)) qa_range_error("Out of memory");
    qa_save_reader actor_reader = field(&saved, "actor");
    qa_saved_actor_id saved_id = qa_read_saved_actor(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    qa_owned_actor actor;
    long match = -1;
    if (regen->actors.resolve_saved(regen->actors.ctx, saved_id, &actor)) {
      for (size_t j = 0; j < actor_count && j < 64; j++)
        if (remaining[j] && qa_same_actor(actors[j].id, actor.id) && actors[j].token == actor.token) match = (long)j;
    }
    if (match < 0) qa_save_reader_fail(&saved, "Mapped ammo actor is missing or duplicated");
    remaining[match] = false;
    actor_entry *entry = require_entry(regen, actors[match]);
    bool used[16] = {0};
    qa_save_reader timers = field(&saved, "timers");
    size_t timer_count = qa_save_reader_list_count(&timers);
    for (size_t j = 0; j < timer_count; j++) {
      qa_save_reader value;
      if (!qa_save_reader_item(&timers, j, &value)) qa_range_error("Out of memory");
      qa_save_reader item_reader = field(&value, "item"), source_reader = field(&value, "source");
      const char *item = qa_namespaced(&item_reader), *source = qa_namespaced(&source_reader);
      long found = -1;
      for (size_t k = 0; k < entry->count; k++)
        if (item_eq(entry->timers[k].item, item) && item_eq(entry->timers[k].source, source)) found = (long)k;
      if (found < 0 || used[found]) qa_save_reader_fail(&value, "Mapped ammo pool or original rule differs from selected supply");
      used[found] = true;
      qa_save_reader elapsed = field(&value, "elapsedMilliseconds");
      entry->timers[found].elapsed = elapsed_value(&value, &entry->timers[found], qa_save_reader_integer_min(&elapsed, 0));
      qa_save_reader_destroy(&elapsed); qa_save_reader_destroy(&item_reader); qa_save_reader_destroy(&source_reader);
      qa_save_reader_destroy(&value);
    }
    qa_save_reader_destroy(&timers);
    for (size_t j = 0; j < entry->count; j++) if (!used[j]) qa_save_reader_fail(&saved, "Saved mapped ammo pool is missing");
    qa_save_reader_destroy(&saved);
  }
  qa_save_reader_destroy(&saved_actors);
  for (size_t i = 0; i < actor_count && i < 64; i++) if (remaining[i]) qa_save_reader_fail(reader, "Saved mapped ammo actor is missing");
}
void qa_app_q3_mapped_ammo_regeneration_close(qa_app_q3_mapped_ammo_regeneration *regen) {
  if (regen->closed) return;
  regen->closed = true;
  for (size_t i = 0; i < regen->entry_count; i++) free_entry(&regen->entries[i]);
  regen->entry_count = 0;
}
