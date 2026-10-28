/* Port of src/content/q3/base/records.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/records.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/foundation/arsenal.h"
#include "qa/foundation/support.h"

typedef struct source_record {
  qa_q3_base_entity_records *owner;
  double slot;
  qa_q3_game_entity entity;
  bool has_actor;
  qa_owned_actor actor;
  bool active, borrowed;
} source_record;

typedef struct binding_slot_ctx {
  qa_q3_base_entity_records *records;
  double slot;
} binding_slot_ctx;

struct qa_q3_base_entity_records {
  qa_q3_base_record_host host;
  qa_provider_id provider;
  qa_q3_base_product product;
  source_record *records;
  qa_q3_game_client *clients;
  struct { int32_t source_stats[16]; int32_t special_ammo[16]; } backing[QA_Q3_MAX_CLIENTS];
  binding_slot_ctx binding_ctx[QA_Q3_MAX_CLIENTS];
  int64_t release_subscription;
  bool subscribed;
};

static qa_body_state zero_body(void) {
  qa_body_state body;
  memset(&body, 0, sizeof body);
  return body;
}
static source_record *record_at(qa_q3_base_entity_records *records, double slot) {
  if (slot != trunc(slot) || slot < 0 || slot >= QA_Q3_MAX_GENTITIES)
    qa_range_error("Q3 entity %g outside 0..1023", slot);
  return &records->records[(size_t)slot];
}
static const char *admit_request(void *ctx, qa_damage_request request) {
  source_record *record = ctx;
  qa_q3_base_entity_records *records = record->owner;
  if (records->host.admit_damage == NULL) return "continue";
  return records->host.admit_damage(records->host.ctx, &record->entity, request) == QA_Q3_BASE_ADMIT_HANDLED
      ? "handled" : "continue";
}
static qa_owned_actor ensure_actor(qa_q3_base_entity_records *records, double slot);
static void bind_callbacks(source_record *record);
static void bind_owned_services(source_record *record) {
  qa_q3_base_entity_records *records = record->owner;
  if (!record->has_actor) qa_range_error("Cannot bind Q3 services without an actor");
  qa_owned_actor actor = record->actor;
  if (!qa_gameplay_authority_read(records->host.combat, actor.id).has_value) {
    qa_combat_state initial;
    memset(&initial, 0, sizeof initial);
    initial.armor.regular.kind = QA_REGULAR_ARMOR_Q3;
    initial.armor.regular.points = 0;
    initial.armor.regular.protection = (float)0.66;
    initial.armor.powered.kind = "none";
    initial.mass = 200;
    qa_gameplay_authority_create_state(records->host.combat, actor, initial, admit_request, record);
  } else {
    qa_gameplay_authority_bind_damage_admission(records->host.combat, actor, admit_request, record);
  }
  if (!qa_shared_inventory_table_has(records->host.inventory, actor.id))
    qa_shared_inventory_table_create_entries(records->host.inventory, actor, NULL, 0);
  bind_callbacks(record);
}
static qa_owned_actor ensure_actor(qa_q3_base_entity_records *records, double slot) {
  source_record *record = record_at(records, slot);
  if (record->has_actor) {
    qa_session_actor_registry_assert_owned(records->host.actors, record->actor);
    return record->actor;
  }
  qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(
      records->host.actors, records->provider, (int64_t)slot, slot < QA_Q3_MAX_CLIENTS ? "q3:player" : "q3:entity");
  record->actor = actor;
  record->has_actor = true;
  qa_shared_body_table_create_body(records->host.bodies, actor, zero_body());
  bind_owned_services(record);
  return actor;
}

/* Entity binding closures (TS GameEntity binding object). */
static qa_owned_actor binding_actor(void *ctx) {
  source_record *record = ctx;
  return ensure_actor(record->owner, record->slot);
}
static bool binding_active(void *ctx) {
  source_record *record = ctx;
  return record->active && record->has_actor &&
      qa_session_actor_registry_is_live(record->owner->host.actors,
          ensure_actor(record->owner, record->slot).id);
}
static void binding_schedule(void *ctx, double nextthink) {
  source_record *record = ctx;
  if (record->has_actor)
    record->owner->host.schedule(record->owner->host.ctx, record->actor, nextthink > 0, nextthink);
}
static void binding_run_think(void *ctx, double time_ms) {
  source_record *record = ctx;
  record->owner->host.run_think(record->owner->host.ctx,
      ensure_actor(record->owner, record->slot), time_ms);
}
static void on_actor_release(void *ctx, qa_owned_actor actor) {
  qa_q3_base_entity_records *records = ctx;
  for (int i = 0; i < QA_Q3_MAX_GENTITIES; i++) {
    source_record *record = &records->records[i];
    if (record->has_actor && qa_same_owned_actor(record->actor, actor)) {
      record->has_actor = false;
      record->active = false;
    }
  }
}

bool qa_q3_base_entity_records_init(qa_q3_base_entity_records *records,
    qa_q3_base_record_host host, qa_provider_id provider, qa_q3_base_product product) {
  memset(records, 0, sizeof *records);
  records->host = host;
  records->provider = provider;
  records->product = product;
  records->records = calloc(QA_Q3_MAX_GENTITIES, sizeof(source_record));
  records->clients = calloc(QA_Q3_MAX_CLIENTS, sizeof(qa_q3_game_client));
  if (!records->records || !records->clients) {
    free(records->records); free(records->clients);
    records->records = NULL; records->clients = NULL;
    return false;
  }
  for (int slot = 0; slot < QA_Q3_MAX_GENTITIES; slot++) {
    source_record *record = &records->records[slot];
    record->owner = records;
    record->slot = slot;
    qa_q3_game_entity_binding binding;
    memset(&binding, 0, sizeof binding);
    binding.ctx = record;
    binding.actor = binding_actor;
    binding.active = binding_active;
    binding.schedule = binding_schedule;
    binding.run_think = binding_run_think;
    qa_q3_game_entity_init(&record->entity, slot, binding);
  }
  qa_q3_game_product game_product = product == QA_Q3_BASE_PRODUCT_MISSIONPACK
      ? QA_Q3_PRODUCT_MISSIONPACK : QA_Q3_PRODUCT_BASEQ3;
  for (int slot = 0; slot < QA_Q3_MAX_CLIENTS; slot++) {
    qa_q3_game_client_init(&records->clients[slot], game_product);
    records->binding_ctx[slot].records = records;
    records->binding_ctx[slot].slot = slot;
  }
  records->release_subscription = qa_session_actor_registry_on_release(host.actors, on_actor_release, records);
  records->subscribed = true;
  return true;
}
void qa_q3_base_entity_records_destroy(qa_q3_base_entity_records *records) {
  if (records->records) {
    for (int i = 0; i < QA_Q3_MAX_GENTITIES; i++) qa_q3_game_entity_destroy(&records->records[i].entity);
    free(records->records);
    records->records = NULL;
  }
  if (records->clients) {
    for (int i = 0; i < QA_Q3_MAX_CLIENTS; i++) qa_q3_game_client_destroy(&records->clients[i]);
    free(records->clients);
    records->clients = NULL;
  }
}
void qa_q3_base_entity_records_close(qa_q3_base_entity_records *records) {
  if (records->subscribed) {
    qa_session_actor_registry_off_release(records->host.actors, records->release_subscription);
    records->subscribed = false;
  }
}
const qa_q3_base_record_host *qa_q3_base_entity_records_host(const qa_q3_base_entity_records *r) { return &r->host; }
qa_provider_id qa_q3_base_entity_records_provider(const qa_q3_base_entity_records *r) { return r->provider; }
qa_q3_base_product qa_q3_base_entity_records_product(const qa_q3_base_entity_records *r) { return r->product; }

qa_q3_base_ownership_state *qa_q3_base_entity_records_capture_ownership(qa_q3_base_entity_records *records) {
  qa_q3_base_ownership_state *out = malloc(QA_Q3_MAX_GENTITIES * sizeof *out);
  if (!out) return NULL;
  for (int i = 0; i < QA_Q3_MAX_GENTITIES; i++) {
    source_record *record = &records->records[i];
    out[i].has_actor = record->has_actor;
    out[i].actor = record->actor;
    out[i].active = record->active;
    out[i].borrowed = record->borrowed;
  }
  return out;
}
void qa_q3_base_entity_records_restore_ownership(qa_q3_base_entity_records *records,
    const qa_q3_base_ownership_state *states, size_t count) {
  if (count != QA_Q3_MAX_GENTITIES) qa_range_error("Restored Q3 ownership requires all retained slots");
  for (size_t i = 0; i < count; i++) {
    const qa_q3_base_ownership_state *state = &states[i];
    if (!state->has_actor) {
      if (state->active) qa_range_error("Active Q3 slot has no actor");
    } else {
      qa_session_actor_registry_assert_owned(records->host.actors, state->actor);
      for (size_t j = 0; j < i; j++)
        if (states[j].has_actor && qa_same_owned_actor(states[j].actor, state->actor))
          qa_range_error("Duplicate restored Q3 actor");
      if (!state->borrowed && strcmp(state->actor.owner, records->provider) != 0)
        qa_range_error("Q3 owned actor has a foreign provider");
    }
  }
  for (size_t slot = 0; slot < count; slot++) {
    source_record *record = record_at(records, (double)slot);
    if (record->has_actor) qa_range_error("Q3 ownership hydration requires fresh records");
    record->has_actor = states[slot].has_actor;
    record->actor = states[slot].actor;
    record->active = states[slot].active;
    record->borrowed = states[slot].borrowed;
  }
}
void qa_q3_base_entity_records_restore_callbacks(qa_q3_base_entity_records *records) {
  for (int i = 0; i < QA_Q3_MAX_GENTITIES; i++) {
    source_record *record = &records->records[i];
    if (record->has_actor && !record->borrowed) {
      qa_gameplay_authority_bind_damage_admission(records->host.combat, record->actor, admit_request, record);
      bind_callbacks(record);
    }
  }
}
static void backing_at(qa_q3_base_entity_records *records, double slot, int32_t **stats, int32_t **ammo) {
  if (slot != trunc(slot) || slot < 0 || slot >= QA_Q3_MAX_CLIENTS)
    qa_range_error("Q3 client backing slot outside 0..63");
  *stats = records->backing[(size_t)slot].source_stats;
  *ammo = records->backing[(size_t)slot].special_ammo;
}
void qa_q3_base_entity_records_capture_client_backing(qa_q3_base_entity_records *records, double slot,
    double source_stats[16], double special_ammo[16]) {
  int32_t *stats, *ammo;
  backing_at(records, slot, &stats, &ammo);
  for (int i = 0; i < 16; i++) { source_stats[i] = stats[i]; special_ammo[i] = ammo[i]; }
}
void qa_q3_base_entity_records_restore_client_backing(qa_q3_base_entity_records *records, double slot,
    const double *source_stats, size_t source_count, const double *special_ammo, size_t ammo_count) {
  if (slot != trunc(slot) || slot < 0 || slot >= QA_Q3_MAX_CLIENTS || source_count != 16 || ammo_count != 16)
    qa_range_error("Invalid Q3 private client backing");
  int32_t *stats, *ammo;
  backing_at(records, slot, &stats, &ammo);
  for (int i = 0; i < 16; i++) { stats[i] = (int32_t)source_stats[i]; ammo[i] = (int32_t)special_ammo[i]; }
}

qa_q3_game_entity *qa_q3_base_entity_records_get(qa_q3_base_entity_records *records, double slot) {
  if (slot != trunc(slot) || slot < 0 || slot >= QA_Q3_MAX_GENTITIES) return NULL;
  return &records->records[(size_t)slot].entity;
}
qa_q3_game_client *qa_q3_base_entity_records_client(qa_q3_base_entity_records *records, double slot) {
  if (slot != trunc(slot) || slot < 0 || slot >= QA_Q3_MAX_CLIENTS)
    qa_range_error("Q3 client %g outside 0..63", slot);
  return &records->clients[(size_t)slot];
}
qa_q3_game_entity *qa_q3_base_entity_records_native_by_actor(qa_q3_base_entity_records *records,
    bool has_actor, qa_actor_id actor) {
  if (!has_actor) return NULL;
  for (int i = 0; i < QA_Q3_MAX_GENTITIES; i++) {
    source_record *record = &records->records[i];
    if (record->has_actor && qa_same_actor(record->actor.id, actor)) return &record->entity;
  }
  return NULL;
}
qa_q3_game_entity *qa_q3_base_entity_records_by_actor(qa_q3_base_entity_records *records,
    bool has_actor, qa_actor_id actor) {
  if (!has_actor) return NULL;
  qa_q3_game_entity *native = qa_q3_base_entity_records_native_by_actor(records, true, actor);
  return native ? native : records->host.foreign(records->host.ctx, actor);
}
qa_q3_game_entity *qa_q3_base_entity_records_adopt(qa_q3_base_entity_records *records, double slot, qa_owned_actor actor) {
  qa_session_actor_registry_assert_owned(records->host.actors, actor);
  source_record *record = record_at(records, slot);
  if (slot < QA_Q3_MAX_CLIENTS || slot >= 1022 || strcmp(actor.owner, records->provider) != 0 ||
      record->has_actor || qa_q3_base_entity_records_native_by_actor(records, true, actor.id) != NULL ||
      !qa_shared_body_table_read(records->host.bodies, actor.id).has_value)
    qa_range_error("Q3 owned actor adoption requires an unused entity slot and its existing body");
  record->actor = actor;
  record->has_actor = true;
  record->active = true;
  record->borrowed = false;
  record->entity.s.number = slot;
  bind_owned_services(record);
  return &record->entity;
}
qa_q3_game_entity *qa_q3_base_entity_records_attach(qa_q3_base_entity_records *records, double slot,
    qa_owned_actor actor, bool player) {
  qa_session_actor_registry_assert_owned(records->host.actors, actor);
  source_record *record = record_at(records, slot);
  if (record->has_actor && !qa_same_owned_actor(record->actor, actor))
    qa_range_error("Q3 slot %g already has an actor", slot);
  record->actor = actor;
  record->has_actor = true;
  record->active = true;
  record->borrowed = true;
  if (player) record->entity.client = qa_q3_base_entity_records_client(records, slot);
  record->entity.s.number = slot;
  return &record->entity;
}
qa_q3_game_entity *qa_q3_base_entity_records_activate(qa_q3_base_entity_records *records, double slot) {
  source_record *record = record_at(records, slot);
  (void)ensure_actor(records, slot);
  record->active = true;
  return &record->entity;
}
void qa_q3_base_entity_records_deactivate_client(qa_q3_base_entity_records *records, double slot) {
  source_record *record = record_at(records, slot);
  if (record->has_actor && !record->borrowed)
    qa_session_actor_registry_release(records->host.actors, record->actor);
  record->has_actor = false;
  record->active = false;
  record->borrowed = false;
}
void qa_q3_base_entity_records_release(qa_q3_base_entity_records *records, qa_q3_game_entity *entity) {
  source_record *record = record_at(records, entity->slot);
  if (&record->entity != entity) qa_range_error("Q3 entity belongs to another record owner");
  if (record->has_actor && !record->borrowed)
    qa_session_actor_registry_release(records->host.actors, record->actor);
  record->has_actor = false;
  record->active = false;
  record->borrowed = false;
  qa_q3_game_entity_binding binding = entity->binding;
  double slot = entity->slot;
  qa_q3_game_entity_destroy(entity);
  qa_q3_game_entity_init(entity, slot, binding);
}

/* Shared-actor participant origin cell. */
typedef struct origin_cell { qa_q3_base_entity_records *records; qa_actor_id actor; } origin_cell;
static bool shared_origin(void *ctx, qa_vec3 *out) {
  origin_cell *cell = ctx;
  qa_body_state_result body = qa_shared_body_table_read(cell->records->host.bodies, cell->actor);
  if (!qa_session_actor_registry_is_live(cell->records->host.actors, cell->actor) || !body.has_value) return false;
  *out = body.value.origin;
  return true;
}
qa_q3_damage_inflictor qa_q3_base_entity_records_damage_inflictor(qa_q3_base_entity_records *records,
    bool has_actor, qa_actor_id actor) {
  if (!has_actor) return qa_q3_participant_entity(&records->records[1022].entity);
  qa_q3_game_entity *native = qa_q3_base_entity_records_native_by_actor(records, true, actor);
  if (native != NULL) return qa_q3_participant_entity(native);
  origin_cell *cell = malloc(sizeof *cell);
  if (!cell) qa_range_error("out of memory");
  cell->records = records;
  cell->actor = actor;
  qa_q3_damage_inflictor out;
  memset(&out, 0, sizeof out);
  out.actor = actor;
  out.origin_ctx = cell;
  out.origin = shared_origin;
  return out;
}
bool qa_q3_base_entity_records_use_participant(qa_q3_base_entity_records *records,
    bool has_actor, qa_actor_id actor, qa_q3_use_participant *out) {
  if (!has_actor) return false;
  *out = qa_q3_base_entity_records_damage_inflictor(records, true, actor);
  return true;
}

static void call_think(qa_q3_entity_callback cb, qa_q3_game_entity *self) {
  qa_q3_entity_think_fn fn; memcpy(&fn, &cb.fn, sizeof fn < sizeof cb.fn ? sizeof fn : sizeof cb.fn);
  fn(cb.ctx, self);
}
static void call_touch(qa_q3_entity_callback cb, qa_q3_game_entity *self, qa_q3_damage_participant other, qa_touch_contact contact) {
  qa_q3_entity_touch_fn fn; memcpy(&fn, &cb.fn, sizeof fn < sizeof cb.fn ? sizeof fn : sizeof cb.fn);
  fn(cb.ctx, self, other, contact);
}
static void call_use(qa_q3_entity_callback cb, qa_q3_game_entity *self, const qa_q3_use_participant *other, const qa_q3_use_participant *activator) {
  qa_q3_entity_use_fn fn; memcpy(&fn, &cb.fn, sizeof fn < sizeof cb.fn ? sizeof fn : sizeof cb.fn);
  fn(cb.ctx, self, other, activator);
}
static void call_pain(qa_q3_entity_callback cb, qa_q3_game_entity *self, qa_q3_damage_participant attacker, double damage) {
  qa_q3_entity_pain_fn fn; memcpy(&fn, &cb.fn, sizeof fn < sizeof cb.fn ? sizeof fn : sizeof cb.fn);
  fn(cb.ctx, self, attacker, damage);
}
static void call_die(qa_q3_entity_callback cb, qa_q3_game_entity *self, qa_q3_damage_inflictor inflictor,
    qa_q3_damage_participant attacker, double damage, double method) {
  qa_q3_entity_die_fn fn; memcpy(&fn, &cb.fn, sizeof fn < sizeof cb.fn ? sizeof fn : sizeof cb.fn);
  fn(cb.ctx, self, inflictor, attacker, damage, method);
}

static void actor_think(void *ctx, qa_owned_actor self, qa_frame_context frame) {
  (void)self; (void)frame;
  source_record *record = ctx;
  qa_q3_game_entity *entity = &record->entity;
  entity->nextthink = 0;
  if (entity->think.fn == NULL) qa_range_error("NULL ent->think");
  call_think(entity->think, entity);
}
static void actor_touch(void *ctx, qa_touch_contact contact) {
  source_record *record = ctx;
  qa_q3_game_entity *entity = &record->entity;
  if (entity->touch.fn == NULL) return;
  qa_q3_game_entity *native = qa_q3_base_entity_records_native_by_actor(record->owner, true, contact.other);
  qa_q3_damage_participant other = native ? qa_q3_participant_entity(native)
      : qa_q3_base_entity_records_damage_inflictor(record->owner, true, contact.other);
  call_touch(entity->touch, entity, other, contact);
}
static void actor_use(void *ctx, qa_owned_actor self, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)self;
  source_record *record = ctx;
  qa_q3_game_entity *entity = &record->entity;
  if (entity->use.fn == NULL) return;
  qa_q3_use_participant other_part, activator_part;
  bool has_other = qa_q3_base_entity_records_use_participant(record->owner, other != NULL,
      other ? *other : (qa_actor_id){0}, &other_part);
  bool has_activator = qa_q3_base_entity_records_use_participant(record->owner, activator != NULL,
      activator ? *activator : (qa_actor_id){0}, &activator_part);
  call_use(entity->use, entity, has_other ? &other_part : NULL, has_activator ? &activator_part : NULL);
}
static void actor_pain(void *ctx, qa_pain_reaction reaction) {
  source_record *record = ctx;
  qa_q3_game_entity *entity = &record->entity;
  if (entity->pain.fn == NULL) return;
  qa_q3_damage_participant attacker = qa_q3_base_entity_records_damage_inflictor(record->owner,
      reaction.attacker != NULL, reaction.attacker ? *reaction.attacker : (qa_actor_id){0});
  call_pain(entity->pain, entity, attacker, reaction.damage);
}
static void actor_die(void *ctx, qa_death_reaction reaction) {
  source_record *record = ctx;
  qa_q3_game_entity *entity = &record->entity;
  qa_q3_damage_call call;
  bool has_call = record->owner->host.damage_call(record->owner->host.ctx, &call);
  if (entity->die.fn == NULL) qa_range_error("G_Damage lethal target has no die callback");
  double method = has_call ? call.method_of_death
      : (reaction.pain.attack != NULL && reaction.pain.attack->cause.kind == QA_ATTACK_CAUSE_Q3
          ? reaction.pain.attack->cause.means_of_death : 0);
  qa_q3_damage_inflictor inflictor = qa_q3_base_entity_records_damage_inflictor(record->owner,
      reaction.inflictor != NULL, reaction.inflictor ? *reaction.inflictor : (qa_actor_id){0});
  qa_q3_damage_participant attacker = qa_q3_base_entity_records_damage_inflictor(record->owner,
      reaction.pain.attacker != NULL, reaction.pain.attacker ? *reaction.pain.attacker : (qa_actor_id){0});
  call_die(entity->die, entity, inflictor, attacker, reaction.pain.damage, method);
}
static void bind_callbacks(source_record *record) {
  if (!record->has_actor) qa_range_error("Cannot bind inactive Q3 record callbacks");
  qa_actor_callbacks callbacks;
  memset(&callbacks, 0, sizeof callbacks);
  callbacks.ctx = record;
  callbacks.think = actor_think;
  callbacks.touch = actor_touch;
  callbacks.use = actor_use;
  callbacks.pain = actor_pain;
  callbacks.die = actor_die;
  qa_actor_callback_table_bind(record->owner->host.callbacks, record->actor, callbacks);
}

/* Player authority binding (TS playerBinding). */
static qa_body_state read_body(binding_slot_ctx *ctx) {
  source_record *record = &ctx->records->records[(size_t)ctx->slot];
  if (!record->has_actor) return zero_body();
  qa_body_state_result body = qa_shared_body_table_read(ctx->records->host.bodies, record->actor.id);
  return body.has_value ? body.value : zero_body();
}
static qa_vec3 binding_origin(void *ctx) { return read_body(ctx).origin; }
static void binding_set_origin(void *ctx, qa_vec3 value) {
  binding_slot_ctx *slot_ctx = ctx;
  qa_body_state body = read_body(slot_ctx);
  body.origin = value;
  qa_shared_body_table_write(slot_ctx->records->host.bodies,
      ensure_actor(slot_ctx->records, slot_ctx->slot), body);
}
static qa_vec3 binding_velocity(void *ctx) { return read_body(ctx).velocity; }
static void binding_set_velocity(void *ctx, qa_vec3 value) {
  binding_slot_ctx *slot_ctx = ctx;
  qa_body_state body = read_body(slot_ctx);
  body.velocity = value;
  qa_shared_body_table_write(slot_ctx->records->host.bodies,
      ensure_actor(slot_ctx->records, slot_ctx->slot), body);
}
static void configure_item(binding_slot_ctx *ctx, qa_item_id item, double count, double capacity) {
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = item;
  entry.count = count;
  entry.capacity = capacity;
  qa_shared_inventory_table_configure(ctx->records->host.inventory,
      ensure_actor(ctx->records, ctx->slot), entry, NULL, NULL);
}
static bool stats_read(void *ctx, double index, int32_t *out) {
  binding_slot_ctx *slot_ctx = ctx;
  qa_q3_base_entity_records *records = slot_ctx->records;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(records->product);
  source_record *record = &records->records[(size_t)slot_ctx->slot];
  if (index == schema->health) {
    double health = 0;
    if (record->has_actor) {
      qa_combat_state_result state = qa_gameplay_authority_read(records->host.combat, record->actor.id);
      health = state.has_value ? state.value.health : 0;
    }
    *out = (int32_t)health;
    return true;
  }
  if (index == schema->armor) {
    double points = 0;
    if (record->has_actor) {
      qa_combat_state_result state = qa_gameplay_authority_read(records->host.combat, record->actor.id);
      if (state.has_value && state.value.armor.regular.kind != QA_REGULAR_ARMOR_NONE)
        points = state.value.armor.regular.points;
    }
    *out = (int32_t)points;
    return true;
  }
  if (index == schema->weapons) {
    int32_t bits = 0;
    if (record->has_actor) {
      for (int i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) {
        const qa_q3_weapon_item *weapon = &qa_q3_weapon_items[i];
        if (qa_shared_inventory_table_count(records->host.inventory, record->actor.id, weapon->item) > 0)
          bits |= 1 << (int)weapon->weapon;
      }
    }
    *out = bits;
    return true;
  }
  if (index != trunc(index) || index < 0 || index >= 16)
    qa_range_error("Q3 stat %g outside 0..15", index);
  *out = records->backing[(size_t)slot_ctx->slot].source_stats[(size_t)index];
  return true;
}
static void stats_write(void *ctx, double index, int32_t value) {
  binding_slot_ctx *slot_ctx = ctx;
  qa_q3_base_entity_records *records = slot_ctx->records;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(records->product);
  if (index == schema->health) {
    qa_gameplay_authority_set_health(records->host.combat, ensure_actor(records, slot_ctx->slot), value);
    return;
  }
  if (index == schema->armor) {
    qa_regular_armor_state initial;
    memset(&initial, 0, sizeof initial);
    initial.kind = QA_REGULAR_ARMOR_Q3;
    initial.points = value;
    initial.protection = (float)0.66;
    qa_gameplay_authority_set_regular_points(records->host.combat,
        ensure_actor(records, slot_ctx->slot), value, &initial);
    return;
  }
  if (index == schema->weapons) {
    for (int i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) {
      const qa_q3_weapon_item *weapon = &qa_q3_weapon_items[i];
      configure_item(slot_ctx, weapon->item, (value & (1 << (int)weapon->weapon)) ? 1 : 0, 1);
    }
    return;
  }
  if (index != trunc(index) || index < 0 || index >= 16)
    qa_range_error("Q3 stat %g outside 0..15", index);
  records->backing[(size_t)slot_ctx->slot].source_stats[(size_t)index] = value;
}
static bool ammo_read(void *ctx, double index, int32_t *out) {
  binding_slot_ctx *slot_ctx = ctx;
  qa_q3_base_entity_records *records = slot_ctx->records;
  const qa_q3_weapon_item *weapon = qa_q3_get_weapon_item(index);
  if (weapon != NULL && weapon->ammo != NULL) {
    source_record *record = &records->records[(size_t)slot_ctx->slot];
    *out = record->has_actor
        ? (int32_t)qa_shared_inventory_table_count(records->host.inventory, record->actor.id, weapon->ammo) : 0;
    return true;
  }
  if (index != trunc(index) || index < 0 || index >= 16)
    qa_range_error("Q3 ammo %g outside 0..15", index);
  *out = records->backing[(size_t)slot_ctx->slot].special_ammo[(size_t)index];
  return true;
}
static void ammo_write(void *ctx, double index, int32_t value) {
  binding_slot_ctx *slot_ctx = ctx;
  qa_q3_base_entity_records *records = slot_ctx->records;
  const qa_q3_weapon_item *weapon = qa_q3_get_weapon_item(index);
  if (weapon != NULL && weapon->ammo != NULL) {
    configure_item(slot_ctx, weapon->ammo, value, 200);
    return;
  }
  if (index != trunc(index) || index < 0 || index >= 16)
    qa_range_error("Q3 ammo %g outside 0..15", index);
  records->backing[(size_t)slot_ctx->slot].special_ammo[(size_t)index] = value;
}
qa_q3_base_player_authority_binding qa_q3_base_entity_records_player_binding(
    qa_q3_base_entity_records *records, double slot) {
  if (slot != trunc(slot) || slot < 0 || slot >= QA_Q3_MAX_CLIENTS)
    qa_range_error("Q3 client backing slot outside 0..63");
  binding_slot_ctx *ctx = &records->binding_ctx[(size_t)slot];
  qa_q3_base_player_authority_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.ctx = ctx;
  binding.origin = binding_origin;
  binding.set_origin = binding_set_origin;
  binding.velocity = binding_velocity;
  binding.set_velocity = binding_set_velocity;
  binding.stats.ctx = ctx;
  binding.stats.read = stats_read;
  binding.stats.write = stats_write;
  binding.ammo.ctx = ctx;
  binding.ammo.read = ammo_read;
  binding.ammo.write = ammo_write;
  return binding;
}

/* EntityPool records table over these records. */
static qa_q3_game_entity *pool_get(void *ctx, double slot) { return qa_q3_base_entity_records_get(ctx, slot); }
static qa_q3_game_entity *pool_native(void *ctx, qa_actor_id actor) {
  return qa_q3_base_entity_records_native_by_actor(ctx, true, actor); }
static qa_q3_game_client *pool_client(void *ctx, double slot) { return qa_q3_base_entity_records_client(ctx, slot); }
static qa_q3_game_entity *pool_activate(void *ctx, double slot) { return qa_q3_base_entity_records_activate(ctx, slot); }
static void pool_release(void *ctx, qa_q3_game_entity *entity) { qa_q3_base_entity_records_release(ctx, entity); }
static void pool_deactivate(void *ctx, double slot) { qa_q3_base_entity_records_deactivate_client(ctx, slot); }
qa_q3_entity_records qa_q3_base_entity_records_pool_table(qa_q3_base_entity_records *records) {
  qa_q3_entity_records table = {records, pool_get, pool_native, pool_client, pool_activate, pool_release, pool_deactivate};
  return table;
}

qa_q3_base_entity_records *qa_q3_base_entity_records_create(
    qa_q3_base_record_host host, qa_provider_id provider, qa_q3_base_product product) {
  qa_q3_base_entity_records *records = malloc(sizeof *records);
  if (!records) return NULL;
  if (!qa_q3_base_entity_records_init(records, host, provider, product)) {
    free(records);
    return NULL;
  }
  return records;
}
void qa_q3_base_entity_records_free(qa_q3_base_entity_records *records) {
  if (!records) return;
  qa_q3_base_entity_records_destroy(records);
  free(records);
}
