/* Port of src/compat/q2/native-mod-protection.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_protection.h"
#include <stdlib.h>
#include <string.h>
#include "qa/world/gameplay/authority.h"

typedef qa_compat_q2_native_mod_protection protection_t;
typedef qa_native_mod_protection_definition definition_t;
typedef struct counter_t { qa_protection_channel channel; const char *selection; qa_native_mod_armor_field field; qa_item_id inventory; } counter_t;
typedef struct binding_ctx { protection_t *owner; qa_actor_id actor; const definition_t *definition; struct binding_ctx *next; } binding_ctx;
typedef struct entry_t {
  qa_actor_id actor; qa_client_id client; qa_protection_reservation *reservations; size_t reservation_count;
  bool bound; uint64_t identity; binding_ctx *contexts;
} entry_t;
typedef struct stage_t {
  qa_actor_id actor; qa_armor_state armor; int suppressed; qa_compat_q2_native_armor_observation *observation;
  protection_t *owner; qa_protection_observer observer; double slot;
  void *assert_ctx; void (*assert_current)(void *);
} stage_t;
typedef struct authority_t { qa_compat_q2_native_mod_region_authority authority; protection_t *owner; qa_actor_id actor; uint64_t identity; const qa_compat_q2_native_mod_host *host; bool has_slot; double slot; struct authority_t *next; } authority_t;
struct qa_compat_q2_native_mod_protection {
  const definition_t *definitions; size_t definition_count;
  const qa_native_mod_declaration *declaration; const qa_mod_host_services *services; qa_provider_id instance;
  qa_compat_q2_native_protection_operations operations;
  entry_t **entries; size_t entry_count, entry_capacity; uint64_t next_identity;
  counter_t *counters; size_t counter_count, counter_capacity;
  stage_t **stages; size_t stage_count, stage_capacity;
  qa_item_id *items_all; size_t items_all_count; qa_item_id *items_channel[2]; size_t items_channel_count[2];
  const qa_compat_q2_native_mod_host *host;
  bool has_storage; qa_compat_q2_native_mod_armor_state storage;
  qa_native_mod_regular_armor_item *flat_regular; qa_native_mod_power_armor_item *flat_power;
  bool active;
  authority_t *authorities;
};
static bool is_regular(const definition_t *d) { return !strcmp(d->channel, "regular"); }
static qa_protection_channel channel_of(const definition_t *d) { return is_regular(d) ? QA_PROTECTION_REGULAR : QA_PROTECTION_POWERED; }
static void add_counter(protection_t *s, const definition_t *d, const char *selection, qa_native_mod_armor_field field) {
  const qa_native_mod_actor_field *shared = NULL;
  for (size_t i = 0; i < s->declaration->actor_record_count; i++) {
    const qa_native_mod_actor_record *record = &s->declaration->actor_records[i];
    if (!qa_compat_q2_text_equal(record->id, field.record)) continue;
    for (size_t j = 0; j < record->field_count && shared == NULL; j++) {
      const qa_native_mod_actor_field *value = &record->fields[j];
      if (!strcmp(value->binding, "inventory") && value->offset == field.field.offset && qa_compat_q2_text_equal(value->encoding, field.field.encoding)) shared = value;
    }
    break;
  }
  if (!is_regular(d) && shared == NULL) qa_range_error("Native power fuel requires one declared canonical inventory field");
  counter_t *counter = qa_compat_q2_grow((void **)&s->counters, &s->counter_count, &s->counter_capacity, sizeof *counter);
  counter->channel = channel_of(d); counter->selection = selection; counter->field = field; counter->inventory = shared != NULL ? shared->item : NULL;
}
/* TS inventoryItems(channel?): distinct, in counter order. */
static size_t collect_items(const protection_t *s, bool has_channel, qa_protection_channel channel, qa_item_id *out) {
  size_t count = 0;
  for (size_t i = 0; i < s->counter_count; i++) {
    const counter_t *c = &s->counters[i]; bool seen = false;
    if (c->inventory == NULL || (has_channel && c->channel != channel)) continue;
    for (size_t j = 0; j < count && !seen; j++) seen = !strcmp(out[j], c->inventory);
    if (!seen) out[count++] = c->inventory;
  }
  return count;
}
protection_t *qa_compat_q2_native_mod_protection_create(const definition_t *definitions, size_t definition_count, const qa_native_mod_declaration *declaration, const qa_mod_host_services *services, qa_provider_id instance, qa_compat_q2_native_protection_operations operations) {
  protection_t *s = calloc(1, sizeof *s); if (s == NULL) qa_range_error("Out of memory");
  s->definitions = definitions; s->definition_count = definition_count; s->declaration = declaration; s->services = services; s->instance = instance; s->operations = operations;
  for (size_t i = 0; i < definition_count; i++) {
    const definition_t *d = &definitions[i];
    if (is_regular(d)) for (size_t j = 0; j < d->regular_storage_count; j++) add_counter(s, d, d->regular_storage[j].item, d->regular_storage[j].points);
    else for (size_t j = 0; j < d->power_storage_count; j++) add_counter(s, d, d->power_storage[j].kind, d->power_storage[j].cells);
  }
  size_t capacity = s->counter_count + 1;
  s->items_all = calloc(capacity, sizeof *s->items_all); s->items_channel[0] = calloc(capacity, sizeof(qa_item_id)); s->items_channel[1] = calloc(capacity, sizeof(qa_item_id));
  if (s->items_all == NULL || s->items_channel[0] == NULL || s->items_channel[1] == NULL) qa_range_error("Out of memory");
  s->items_all_count = collect_items(s, false, QA_PROTECTION_REGULAR, s->items_all);
  s->items_channel_count[QA_PROTECTION_REGULAR] = collect_items(s, true, QA_PROTECTION_REGULAR, s->items_channel[QA_PROTECTION_REGULAR]);
  s->items_channel_count[QA_PROTECTION_POWERED] = collect_items(s, true, QA_PROTECTION_POWERED, s->items_channel[QA_PROTECTION_POWERED]);
  return s;
}
static qa_protection_claim claim_of(const protection_t *s, const definition_t *d) {
  qa_protection_claim claim; memset(&claim, 0, sizeof claim);
  claim.owner = s->instance; claim.rule = d->id;
  if (d->admission.kind != NULL) { claim.admission.kind = d->admission.kind; claim.admission.owner = d->admission.owner; } else claim.admission.kind = "claim";
  return claim;
}
static double storage_scalar(void *ctx, qa_guest_address base, qa_native_mod_scalar_field field, bool has_value, double value) { protection_t *s = ctx; return s->operations.scalar(s->operations.ctx, base, field, has_value, value); }
void qa_compat_q2_native_mod_protection_attach(protection_t *s, const qa_compat_q2_native_mod_host *host) {
  s->host = host; size_t regular = 0, power = 0;
  for (size_t i = 0; i < s->definition_count; i++) { if (is_regular(&s->definitions[i])) regular += s->definitions[i].regular_storage_count; else power += s->definitions[i].power_storage_count; }
  s->flat_regular = calloc(regular + 1, sizeof *s->flat_regular); s->flat_power = calloc(power + 1, sizeof *s->flat_power);
  if (s->flat_regular == NULL || s->flat_power == NULL) qa_range_error("Out of memory");
  size_t r = 0, p = 0;
  for (size_t i = 0; i < s->definition_count; i++) {
    const definition_t *d = &s->definitions[i];
    if (is_regular(d)) { for (size_t j = 0; j < d->regular_storage_count; j++) s->flat_regular[r++] = d->regular_storage[j]; }
    else for (size_t j = 0; j < d->power_storage_count; j++) s->flat_power[p++] = d->power_storage[j];
  }
  qa_native_mod_armor armor; memset(&armor, 0, sizeof armor);
  armor.kind = "source"; armor.regular = s->flat_regular; armor.regular_count = regular; armor.power = s->flat_power; armor.power_count = power;
  qa_compat_q2_native_mod_armor_state_init(&s->storage, armor, s->declaration, host, storage_scalar, s); s->has_storage = true;
  for (size_t i = 0; i < s->definition_count; i++) {
    const definition_t *d = &s->definitions[i]; qa_native_mod_entry entry; bool region = !strcmp(d->absorb_abi, "source-region");
    if (!strcmp(d->absorb_abi, "source-call")) entry = d->absorb_call.entry;
    else if (region) entry = d->absorb_region->call.entry;
    else entry = d->absorb_q2.entry;
    qa_guest_address target = !strcmp(entry.kind, "game-export") ? host->game_entry(host->ctx, entry.name).address : qa_compat_q2_native_mod_entry_address(host, entry);
    host->memory->check(host->memory->ctx, target, 1, "execute");
    if (region) {
      const qa_native_mod_protection_region *r2 = d->absorb_region; double rvas[4] = {r2->frame.entry, r2->entry, r2->join, r2->frame.exit};
      for (int k = 0; k < 4; k++) host->memory->check(host->memory->ctx, qa_compat_q2_native_mod_image_rva(host, rvas[k]), 1, "execute");
    }
  }
}
static entry_t *find_entry(const protection_t *s, qa_actor_id actor) { for (size_t i = 0; i < s->entry_count; i++) if (qa_same_actor(s->entries[i]->actor, actor)) return s->entries[i]; return NULL; }
static bool client_matches(const protection_t *s, qa_actor_id actor, qa_client_id client) {
  const qa_mod_client_services *clients = s->services->clients; qa_client_id now; qa_actor_id back;
  return clients != NULL && clients->for_actor(clients->ctx, actor, &now) && qa_same_client(now, client) && clients->actor(clients->ctx, client, &back) && qa_same_actor(back, actor);
}
static entry_t *require_entry(const protection_t *s, qa_actor_id actor) {
  entry_t *entry = find_entry(s, actor);
  if (entry == NULL || !qa_session_actor_registry_is_live(s->services->actors, actor) || !client_matches(s, actor, entry->client)) qa_range_error("Native protection client is no longer live");
  return entry;
}
void qa_compat_q2_native_mod_protection_reserve(protection_t *s) {
  const qa_mod_client_services *clients = s->services->clients;
  if (clients == NULL) qa_range_error("Native protection requires canonical clients");
  size_t count = 0; const qa_mod_client_identity *listed = clients->clients(clients->ctx, &count);
  qa_mod_client_identity *identities = calloc(count + 1, sizeof *identities); if (identities == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) identities[i] = listed[i];
  for (size_t i = 0; i < count; i++) qa_compat_q2_native_mod_protection_reserve_actor(s, identities[i].actor);
  free(identities);
}
void qa_compat_q2_native_mod_protection_reserve_actor(protection_t *s, qa_actor_id actor) {
  s->operations.current(s->operations.ctx);
  if (find_entry(s, actor) != NULL) { (void)require_entry(s, actor); return; }
  const qa_mod_client_services *clients = s->services->clients; qa_client_id client; qa_actor_id back; qa_owned_actor owner;
  bool has_client = clients != NULL && clients->for_actor(clients->ctx, actor, &client), has_owner = qa_session_actor_registry_resolve_owned(s->services->actors, actor, &owner);
  if (!has_client || !has_owner || !clients->actor(clients->ctx, client, &back) || !qa_same_actor(back, actor)) qa_range_error("Native protection requires a live canonical client");
  size_t inventory_count = 0; const qa_inventory_entry *inventory = qa_shared_inventory_table_entries(s->services->inventory, actor, &inventory_count);
  for (size_t i = 0; i < s->items_all_count; i++) {
    bool found = false;
    for (size_t j = 0; j < inventory_count && !found; j++) found = !strcmp(inventory[j].item, s->items_all[i]);
    if (!found) qa_range_error("Native protection item has no canonical inventory owner: %s", s->items_all[i]);
  }
  entry_t *entry = calloc(1, sizeof *entry); if (entry == NULL) qa_range_error("Out of memory");
  entry->actor = actor; entry->client = client; entry->identity = ++s->next_identity;
  entry->reservations = calloc(s->definition_count + 1, sizeof *entry->reservations); if (entry->reservations == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < s->definition_count; i++)
    entry->reservations[entry->reservation_count++] = qa_gameplay_authority_reserve_protection(s->services->combat, owner, channel_of(&s->definitions[i]), claim_of(s, &s->definitions[i]));
  entry_t **slot = qa_compat_q2_grow((void **)&s->entries, &s->entry_count, &s->entry_capacity, sizeof *slot); *slot = entry;
}
typedef struct source_t { qa_compat_q2_native_mod_armor_state *storage; double slot; } source_t;
static source_t source_of(protection_t *s, qa_actor_id actor) {
  s->operations.current(s->operations.ctx); (void)require_entry(s, actor);
  if (!s->operations.eligible(s->operations.ctx, actor)) qa_range_error("Native protection client has not been admitted");
  double slot; qa_guest_address client;
  if (!s->operations.slot(s->operations.ctx, actor, &slot) || !s->has_storage || s->host == NULL || !s->host->client(s->host->ctx, slot, &client)) qa_range_error("Native protection has no source client record");
  if (!s->host->active(s->host->ctx, slot)) qa_range_error("Native protection requires an active source record retained by original saves");
  source_t source = {&s->storage, slot}; return source;
}
static const counter_t *counter_for(const protection_t *s, qa_protection_channel channel, qa_armor_state state) {
  if (channel == QA_PROTECTION_REGULAR ? state.regular.kind != QA_REGULAR_ARMOR_SOURCE : !strcmp(state.powered.kind, "none")) return NULL;
  const char *selection = channel == QA_PROTECTION_REGULAR ? state.regular.item : state.powered.kind;
  for (size_t i = 0; i < s->counter_count; i++) if (s->counters[i].channel == channel && qa_compat_q2_text_equal(s->counters[i].selection, selection)) return &s->counters[i];
  return NULL;
}
static qa_armor_state read_armor(protection_t *s, qa_actor_id actor) {
  source_t source = source_of(s, actor); qa_armor_state state = qa_compat_q2_native_mod_armor_read(source.storage, source.slot);
  const counter_t *regular = counter_for(s, QA_PROTECTION_REGULAR, state), *power = counter_for(s, QA_PROTECTION_POWERED, state);
  if (state.regular.kind != QA_REGULAR_ARMOR_NONE && regular != NULL && regular->inventory != NULL) state.regular.points = qa_shared_inventory_table_count(s->services->inventory, actor, regular->inventory);
  if (strcmp(state.powered.kind, "none") != 0 && power != NULL && power->inventory != NULL) state.powered.cells = qa_shared_inventory_table_count(s->services->inventory, actor, power->inventory);
  return state;
}
static qa_armor_state channel_merged(qa_armor_state current, qa_protection_channel channel, qa_armor_state next) {
  if (channel == QA_PROTECTION_REGULAR) current.regular = next.regular; else current.powered = next.powered;
  return current;
}
static void validate_write(protection_t *s, qa_actor_id actor, qa_protection_channel channel, qa_armor_state next) {
  source_t source = source_of(s, actor); qa_armor_state current = qa_compat_q2_native_mod_armor_read(source.storage, source.slot);
  if (channel == QA_PROTECTION_POWERED && strcmp(current.powered.kind, next.powered.kind) != 0) qa_range_error("Native power activation requires its original source operation");
  qa_compat_q2_native_mod_armor_validate_write(source.storage, source.slot, channel_merged(current, channel, next));
}
typedef struct suppress_call { protection_t *s; stage_t **stages; size_t count; void *ctx; void (*invoke)(void *); } suppress_call;
static void suppress_body(void *value) { suppress_call *c = value; c->invoke(c->ctx); }
static void suppress_cleanup(void *value) { suppress_call *c = value; for (size_t i = 0; i < c->count; i++) c->stages[i]->suppressed--; free(c->stages); }
static void suppress_current(protection_t *s, void *ctx, void (*invoke)(void *)) {
  suppress_call c = {s, calloc(s->stage_count + 1, sizeof(stage_t *)), s->stage_count, ctx, invoke};
  if (c.stages == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < c.count; i++) { c.stages[i] = s->stages[i]; c.stages[i]->suppressed++; }
  qa_compat_q2_native_mod_finally(suppress_body, &c, suppress_cleanup, &c);
}
typedef struct write_call { protection_t *s; qa_actor_id actor; qa_protection_channel channel; qa_armor_state next; } write_call;
static void write_transfer(void *value) {
  write_call *c = value; source_t source = source_of(c->s, c->actor); qa_armor_state current = qa_compat_q2_native_mod_armor_read(source.storage, source.slot);
  qa_compat_q2_native_mod_armor_write(source.storage, source.slot, channel_merged(current, c->channel, c->next));
}
static void write_suppressed(void *value) { write_call *c = value; c->s->operations.transfer(c->s->operations.ctx, write_transfer, c); }
static void write_armor(protection_t *s, qa_actor_id actor, qa_protection_channel channel, qa_armor_state next) {
  validate_write(s, actor, channel, next);
  write_call c = {s, actor, channel, next};
  suppress_current(s, &c, write_suppressed);
  if (qa_session_actor_registry_is_live(s->services->actors, actor)) {
    qa_armor_state armor = read_armor(s, actor);
    for (size_t i = 0; i < s->stage_count; i++) if (qa_same_actor(s->stages[i]->actor, actor)) s->stages[i]->armor = armor;
  }
}
static qa_regular_armor_state binding_read_regular(void *ctx) { binding_ctx *b = ctx; return read_armor(b->owner, b->actor).regular; }
static qa_powered_protection_state binding_read_powered(void *ctx) { binding_ctx *b = ctx; return read_armor(b->owner, b->actor).powered; }
static void binding_validate_regular(void *ctx, qa_regular_armor_state regular) { binding_ctx *b = ctx; qa_armor_state next = read_armor(b->owner, b->actor); next.regular = regular; validate_write(b->owner, b->actor, QA_PROTECTION_REGULAR, next); }
static void binding_validate_powered(void *ctx, qa_powered_protection_state powered) { binding_ctx *b = ctx; qa_armor_state next = read_armor(b->owner, b->actor); next.powered = powered; validate_write(b->owner, b->actor, QA_PROTECTION_POWERED, next); }
static void binding_write_regular(void *ctx, qa_regular_armor_state regular) { binding_ctx *b = ctx; qa_armor_state next = read_armor(b->owner, b->actor); next.regular = regular; write_armor(b->owner, b->actor, QA_PROTECTION_REGULAR, next); }
static void binding_write_powered(void *ctx, qa_powered_protection_state powered) { binding_ctx *b = ctx; qa_armor_state next = read_armor(b->owner, b->actor); next.powered = powered; write_armor(b->owner, b->actor, QA_PROTECTION_POWERED, next); }
static qa_armor_stage_result binding_absorb(void *ctx, qa_armor_stage_input input, qa_protection_observer observer);
void qa_compat_q2_native_mod_protection_bind_actor(protection_t *s, qa_actor_id actor) {
  if (!s->active || !s->operations.eligible(s->operations.ctx, actor)) return;
  entry_t *entry = require_entry(s, actor); if (entry->bound) return;
  (void)source_of(s, actor);
  for (size_t i = 0; i < entry->reservation_count; i++) {
    qa_protection_reservation *reservation = &entry->reservations[i]; const definition_t *definition = NULL;
    for (size_t j = 0; j < s->definition_count && definition == NULL; j++) if (channel_of(&s->definitions[j]) == reservation->channel) definition = &s->definitions[j];
    if (definition == NULL) qa_range_error("Native protection reservation has no declaration");
    binding_ctx *context = calloc(1, sizeof *context); if (context == NULL) qa_range_error("Out of memory");
    context->owner = s; context->actor = actor; context->definition = definition; context->next = entry->contexts; entry->contexts = context;
    qa_protection_binding binding; memset(&binding, 0, sizeof binding);
    binding.ctx = context; binding.claim = claim_of(s, definition); binding.channel = reservation->channel;
    binding.inventory_items = s->items_channel[reservation->channel]; binding.inventory_item_count = s->items_channel_count[reservation->channel];
    binding.pickups = s->operations.pickups(s->operations.ctx, actor, reservation->channel, &binding.pickup_count);
    binding.absorb = binding_absorb;
    if (reservation->channel == QA_PROTECTION_REGULAR) { binding.read_regular = binding_read_regular; binding.validate_write_regular = binding_validate_regular; binding.write_regular = binding_write_regular; }
    else { binding.read_powered = binding_read_powered; binding.validate_write_powered = binding_validate_powered; binding.write_powered = binding_write_powered; }
    (void)reservation->bind(reservation->ctx, binding);
  }
  entry->bound = true;
}
void qa_compat_q2_native_mod_protection_activate(protection_t *s) {
  qa_compat_q2_native_mod_protection_reserve(s); s->active = true;
  for (size_t i = 0; i < s->entry_count; i++) qa_compat_q2_native_mod_protection_bind_actor(s, s->entries[i]->actor);
}
void qa_compat_q2_native_mod_protection_prepare_restore(protection_t *s) { qa_compat_q2_native_mod_protection_close(s); qa_compat_q2_native_mod_protection_reserve(s); }
void qa_compat_q2_native_mod_protection_validate_restored_inventory(protection_t *s) {
  for (size_t i = 0; i < s->entry_count; i++) {
    double left;
    double right;
    qa_actor_id actor = s->entries[i]->actor; source_t source = source_of(s, actor);
    for (size_t j = 0; j < s->counter_count; j++) if (s->counters[j].inventory != NULL
        && (left = qa_compat_q2_native_mod_armor_read_count(source.storage, source.slot, s->counters[j].field), right = qa_shared_inventory_table_count(s->services->inventory, actor, s->counters[j].inventory), left != right))
      qa_range_error("Restored native protection count differs from its canonical inventory");
  }
}
/* --- observe(). --- */
static void stage_assert(const stage_t *stage) { if (stage->assert_current != NULL) stage->assert_current(stage->assert_ctx); }
static void stage_publish(stage_t *stage) {
  protection_t *s = stage->owner;
  stage_assert(stage);
  if (!qa_session_actor_registry_is_live(s->services->actors, stage->actor)) return;
  (void)source_of(s, stage->actor);
  qa_armor_state before = stage->armor, after = read_armor(s, stage->actor);
  bool regular = !qa_compat_q2_regular_armor_equal(before.regular, after.regular), powered = !qa_compat_q2_powered_protection_equal(before.powered, after.powered);
  for (size_t i = 0; i < s->stage_count; i++) if (qa_same_actor(s->stages[i]->actor, stage->actor)) s->stages[i]->armor = after;
  if (regular || powered) {
    qa_protection_store change; memset(&change, 0, sizeof change);
    change.has_regular = regular; change.regular_before = before.regular; change.regular_after = after.regular;
    change.has_powered = powered; change.powered_before = before.powered; change.powered_after = after.powered;
    stage->observer.stored(stage->observer.ctx, change);
  }
  stage_assert(stage);
}
typedef struct commit_write { stage_t *stage; qa_inventory_committed_change change; } commit_write;
static void commit_counts(void *value) {
  commit_write *c = value; stage_t *stage = c->stage; protection_t *s = stage->owner;
  for (size_t i = 0; i < s->counter_count; i++) {
    const counter_t *counter = &s->counters[i];
    if (counter->inventory != NULL && !strcmp(counter->inventory, c->change.after.item) && qa_compat_q2_native_mod_armor_read_count(&s->storage, stage->slot, counter->field) != c->change.after.count)
      qa_compat_q2_native_mod_armor_write_count(&s->storage, stage->slot, counter->field, c->change.after.count);
  }
}
static void stage_committed(void *ctx, qa_inventory_committed_change change) {
  stage_t *stage = ctx; protection_t *s = stage->owner;
  stage_assert(stage); (void)source_of(s, stage->actor);
  if (!change.has_before) qa_range_error("Native protection count lost its canonical baseline");
  commit_write c = {stage, change};
  suppress_current(s, &c, commit_counts);
  stage_publish(stage);
}
static void stage_storage_changed(void *ctx, qa_armor_state before, qa_armor_state after) {
  (void)before; (void)after; /* TS storage.observe(slot, () => ...) re-reads. */
  stage_t *stage = ctx; protection_t *s = stage->owner;
  stage_assert(stage);
  if (s->stage_count == 0 || s->stages[s->stage_count - 1] != stage || stage->suppressed > 0 || !qa_session_actor_registry_is_live(s->services->actors, stage->actor)) return;
  (void)require_entry(s, stage->actor);
  qa_compat_q2_native_protection_inventory_commit commit; memset(&commit, 0, sizeof commit);
  commit.ctx = stage; commit.actor = stage->actor; commit.items = s->items_all; commit.item_count = s->items_all_count; commit.committed = stage_committed;
  s->operations.flush(s->operations.ctx, &commit);
  stage_publish(stage);
}
typedef struct observe_call { protection_t *s; stage_t stage; void *ctx; void (*invoke)(void *); } observe_call;
static void stage_stop(stage_t *stage) { if (stage->observation != NULL) { qa_compat_q2_native_armor_observation_close(stage->observation); stage->observation = NULL; } }
static void observe_body(void *value) {
  observe_call *c = value;
  c->stage.observation = qa_compat_q2_native_mod_armor_observe(&c->s->storage, c->stage.slot, &c->stage, stage_storage_changed);
  c->invoke(c->ctx);
}
static void observe_cleanup(void *value) {
  observe_call *c = value; protection_t *s = c->s; stage_stop(&c->stage);
  for (size_t i = 0; i < s->stage_count; i++) if (s->stages[i] == &c->stage) { memmove(&s->stages[i], &s->stages[i + 1], (s->stage_count - i - 1) * sizeof *s->stages); s->stage_count--; break; }
}
static void observe_transfer(void *value) {
  observe_call *c = value; protection_t *s = c->s;
  source_t source = source_of(s, c->stage.actor);
  c->stage.slot = source.slot; c->stage.armor = read_armor(s, c->stage.actor); c->stage.suppressed = 0; c->stage.observation = NULL;
  stage_t **slot = qa_compat_q2_grow((void **)&s->stages, &s->stage_count, &s->stage_capacity, sizeof *slot); *slot = &c->stage;
  qa_compat_q2_native_mod_finally(observe_body, c, observe_cleanup, c);
}
void qa_compat_q2_native_mod_protection_observe(protection_t *s, qa_actor_id actor, qa_protection_observer observer, void *invoke_ctx, void (*invoke)(void *), void *assert_ctx, void (*assert_current)(void *)) {
  observe_call c; memset(&c, 0, sizeof c);
  c.s = s; c.ctx = invoke_ctx; c.invoke = invoke; c.stage.owner = s; c.stage.actor = actor; c.stage.observer = observer; c.stage.assert_ctx = assert_ctx; c.stage.assert_current = assert_current;
  s->operations.transfer(s->operations.ctx, observe_transfer, &c);
}
/* --- absorb(). --- */
static double damage_flags(const protection_t *s, const definition_t *d, const qa_armor_stage_input *input) {
  const qa_armor_damage_flags *flags = &input->flags; bool classic = s->declaration->target.api.kind == QA_Q2_GAME_API_CLASSIC;
  bool radius = input->request.delivery != NULL && !strcmp(input->request.delivery, "radius");
  return (double)((radius ? 1 : 0) | (flags->no_armor || (classic && (!is_regular(d) ? flags->no_power_armor : flags->no_regular_armor)) ? 2 : 0)
      | (flags->energy ? 4 : 0) | (flags->no_regular_armor ? 128 : 0) | (!classic && flags->no_power_armor ? 256 : 0));
}
static bool value_names_scale(const qa_native_mod_value *value) {
  return (!strcmp(value->kind, "float32") || !strcmp(value->kind, "float64")) && !strcmp(value->value.kind, "input") && !strcmp(value->value.name, "regular-protection-scale");
}
static bool absorb_names_scale(const definition_t *d) {
  bool region = !strcmp(d->absorb_abi, "source-region");
  if (!region && strcmp(d->absorb_abi, "source-call") != 0) return false;
  const qa_native_mod_source_call *call = region ? &d->absorb_region->call : &d->absorb_call;
  for (size_t i = 0; i < call->argument_count; i++) if (value_names_scale(&call->arguments[i])) return true;
  for (size_t i = 0; i < call->global_count; i++) if (value_names_scale(&call->globals[i].value)) return true;
  if (region) for (size_t i = 0; i < d->absorb_region->input_count; i++) if (value_names_scale(&d->absorb_region->inputs[i].value)) return true;
  return false;
}
static qa_native_mod_value vector_input(const char *name) { qa_native_mod_value v; memset(&v, 0, sizeof v); v.kind = "vector"; v.value.kind = "input"; v.value.name = name; return v; }
static qa_native_mod_value int_input(const char *name) { qa_native_mod_value v; memset(&v, 0, sizeof v); v.kind = "int32"; v.value.kind = "input"; v.value.name = name; return v; }
static qa_native_mod_value int_constant(double value) { qa_native_mod_value v; memset(&v, 0, sizeof v); v.kind = "int32"; v.value.kind = "float"; v.value.number_value = value; return v; }
static bool authority_current(void *ctx) {
  authority_t *a = ctx; protection_t *s = a->owner; double slot;
  if (!s->active || s->host != a->host || a->host == NULL || !a->has_slot) return false;
  const entry_t *entry = find_entry(s, a->actor);
  if (entry == NULL || entry->identity != a->identity || !qa_session_actor_registry_is_live(s->services->actors, a->actor) || !client_matches(s, a->actor, entry->client)) return false;
  return s->operations.eligible(s->operations.ctx, a->actor) && s->operations.slot(s->operations.ctx, a->actor, &slot) && slot == a->slot && a->host->active(a->host->ctx, a->slot);
}
static void authority_assert(void *ctx) { authority_t *a = ctx; if (!authority_current(a)) qa_compat_q2_native_throw(&a->authority, a->authority.error); }
typedef struct absorb_call { protection_t *s; const qa_native_mod_source_call *call; qa_compat_q2_native_pickup_inputs inputs; const qa_native_mod_protection_region *region; const authority_t *authority; bool has_result; double result; } absorb_call;
static void absorb_invoke(void *value) {
  absorb_call *c = value; const qa_compat_q2_native_protection_operations *o = &c->s->operations;
  c->has_result = o->invoke(o->ctx, c->call, c->inputs, c->region, c->authority != NULL ? &c->authority->authority : NULL, &c->result);
}
static qa_mod_runtime_value float_value(double value) { qa_mod_runtime_value v; memset(&v, 0, sizeof v); v.kind = "float"; v.number_value = value; return v; }
static qa_mod_runtime_value vector_value(qa_vec3 value) { qa_mod_runtime_value v; memset(&v, 0, sizeof v); v.kind = "vector"; v.vector_value = value; return v; }
static qa_mod_runtime_value actor_value(const qa_actor_id *value) { qa_mod_runtime_value v; memset(&v, 0, sizeof v); v.kind = "actor"; v.actor_value = value; return v; }
static qa_armor_stage_result binding_absorb(void *ctx, qa_armor_stage_input input, qa_protection_observer observer) {
  binding_ctx *b = ctx; protection_t *s = b->owner; const definition_t *d = b->definition; qa_actor_id actor = b->actor;
  if (!qa_same_actor(input.request.target, actor)) qa_range_error("Native armor stage target differs from its binding");
  double scale = input.flags.has_regular_protection_scale ? input.flags.regular_protection_scale : 1;
  if (is_regular(d) && scale != 1 && !absorb_names_scale(d)) qa_range_error("Native regular protection scale requires an explicit source input");
  qa_source_time time = qa_compat_q2_services_time(s->services);
  qa_compat_q2_native_pickup_input rows[11]; memset(rows, 0, sizeof rows);
  rows[0].name = "self"; rows[0].value = actor_value(&actor);
  rows[1].name = "attacker"; rows[1].value = actor_value(input.request.attack.attacker);
  rows[2].name = "inflictor"; rows[2].value = actor_value(input.request.attack.inflictor);
  rows[3].name = "amount"; rows[3].value = float_value(input.amount);
  rows[4].name = "damage-flags"; rows[4].value = float_value(damage_flags(s, d, &input));
  rows[5].name = "regular-protection-scale"; rows[5].value = float_value(scale);
  rows[6].name = "point"; rows[6].value = vector_value(input.point);
  rows[7].name = "normal"; rows[7].value = vector_value(input.normal);
  rows[8].name = "direction"; rows[8].value = vector_value(input.direction);
  rows[9].name = "knockback"; rows[9].value = float_value(input.request.knockback);
  rows[10].name = "time"; rows[10].value = float_value(qa_compat_q2_time_seconds(time));
  const char *record = s->declaration->entity_record; if (record == NULL) qa_range_error("Native protection requires a declared source entity");
  /* TS sourceCall. */
  qa_native_mod_source_call lowered; memset(&lowered, 0, sizeof lowered); qa_native_mod_value arguments[6];
  bool region = !strcmp(d->absorb_abi, "source-region"), direct = region || !strcmp(d->absorb_abi, "source-call");
  const qa_native_mod_source_call *call = region ? &d->absorb_region->call : !strcmp(d->absorb_abi, "source-call") ? &d->absorb_call : &lowered;
  if (!direct) {
    size_t n = 0; memset(arguments, 0, sizeof arguments);
    arguments[n].kind = "actor"; arguments[n].record = record; arguments[n++].input = "self";
    arguments[n++] = vector_input("point"); arguments[n++] = vector_input("normal"); arguments[n++] = int_input("amount");
    if (!strcmp(d->absorb_abi, "q2-check-armor")) arguments[n++] = int_constant(d->absorb_sparks);
    arguments[n++] = int_constant(damage_flags(s, d, &input));
    lowered.entry = d->absorb_q2.entry; lowered.globals = d->absorb_q2.globals; lowered.global_count = d->absorb_q2.global_count; lowered.returns = "int32"; lowered.arguments = arguments; lowered.argument_count = n;
  }
  const entry_t *reserved = require_entry(s, actor);
  authority_t *authority = NULL;
  if (region) {
    authority = calloc(1, sizeof *authority); if (authority == NULL) qa_range_error("Out of memory");
    authority->authority.ctx = authority; authority->authority.error = "Native protection donor owner retired"; authority->authority.current = authority_current;
    authority->owner = s; authority->actor = actor; authority->identity = reserved->identity; authority->host = s->host;
    authority->has_slot = s->operations.slot(s->operations.ctx, actor, &authority->slot);
    authority->next = s->authorities; s->authorities = authority;
  }
  qa_compat_q2_native_pickup_inputs inputs = {rows, 11};
  absorb_call c = {s, call, inputs, region ? d->absorb_region : NULL, authority, false, 0};
  qa_compat_q2_native_mod_protection_observe(s, actor, observer, &c, absorb_invoke, authority, authority != NULL ? authority_assert : NULL);
  if (authority != NULL) {
    /* Reached only on a normal return; an unwound authority stays listed for
     * its catch site and is reclaimed at close. */
    for (authority_t **link = &s->authorities; *link != NULL; link = &(*link)->next) if (*link == authority) { *link = authority->next; break; }
    free(authority);
  }
  qa_armor_stage_result result; result.saved = c.has_result ? c.result : 0; return result;
}
void qa_compat_q2_native_mod_protection_release(protection_t *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->stage_count; i++) if (qa_same_actor(s->stages[i]->actor, actor)) stage_stop(s->stages[i]);
  entry_t *entry = find_entry(s, actor); if (entry == NULL) return;
  for (size_t i = 0; i < entry->reservation_count; i++) entry->reservations[i].close(entry->reservations[i].ctx);
  for (size_t i = 0; i < s->entry_count; i++) if (s->entries[i] == entry) { memmove(&s->entries[i], &s->entries[i + 1], (s->entry_count - i - 1) * sizeof *s->entries); s->entry_count--; break; }
  while (entry->contexts != NULL) { binding_ctx *next = entry->contexts->next; free(entry->contexts); entry->contexts = next; }
  free(entry->reservations); free(entry);
}
void qa_compat_q2_native_mod_protection_close(protection_t *s) {
  s->active = false;
  while (s->entry_count > 0) qa_compat_q2_native_mod_protection_release(s, s->entries[0]->actor);
}
void qa_compat_q2_native_mod_protection_destroy(protection_t *s) {
  if (s == NULL) return;
  qa_compat_q2_native_mod_protection_close(s);
  if (s->has_storage) qa_compat_q2_native_mod_armor_state_destroy(&s->storage);
  while (s->authorities != NULL) { authority_t *next = s->authorities->next; free(s->authorities); s->authorities = next; }
  free(s->flat_regular); free(s->flat_power); free(s->counters); free(s->entries); free(s->stages); free(s->items_all); free(s->items_channel[0]); free(s->items_channel[1]); free(s);
}
