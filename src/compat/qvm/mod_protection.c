/* See include/qa/compat/qvm/mod_protection.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_protection.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static size_t definition_fields(const qa_qvm_mod_protection *definition, const qa_qvm_mod_protection_scalar *out[2]) {
  if (strcmp(definition->channel, "regular") == 0) {
    out[0] = &definition->storage_points;
    if (definition->has_selection) {
      out[1] = &definition->selection.field;
      return 2;
    }
    return 1;
  }
  out[0] = &definition->storage_cells;
  out[1] = &definition->selection.field;
  return 2;
}

void qa_qvm_validate_mod_protection(const qa_qvm_mod_callback_declaration *declaration) {
  const char *channels[8];
  size_t channel_count = 0;
  const char *ids[64];
  size_t id_count = 0;
  /* Occupied (record, offset) pairs; TS keys are full strings. */
  const char *occupied_records[64];
  double occupied_offsets[64];
  size_t occupied_count = 0;
  for (size_t d = 0; d < declaration->protection_count; d++) {
    const qa_qvm_mod_protection *definition = &declaration->protection[d];
    bool seen_channel = false, seen_id = false;
    for (size_t i = 0; i < channel_count; i++) if (strcmp(channels[i], definition->channel) == 0) seen_channel = true;
    for (size_t i = 0; i < id_count; i++) if (strcmp(ids[i], definition->id) == 0) seen_id = true;
    if (declaration->clients == NULL || seen_channel || seen_id || definition->id[0] == '\0' ||
        channel_count >= 8 || id_count >= 64)
      qa_range_error("QVM protection requires unique channels and rules with source client admission");
    channels[channel_count++] = definition->channel;
    ids[id_count++] = definition->id;
    if (strcmp(definition->absorb.returns, "void") == 0)
      qa_range_error("QVM protection requires original source savings");
    double masks[] = {definition->flags.no_armor, definition->flags.no_power_armor,
                      definition->flags.no_regular_armor, definition->flags.energy, definition->flags.radius};
    for (int i = 0; i < 5; i++) {
      if (!isfinite(masks[i]) || masks[i] != trunc(masks[i]) || masks[i] < 0 || masks[i] > 2147483647.0)
        qa_range_error("Invalid QVM protection flag mask");
    }
    const qa_qvm_mod_protection_scalar *storage[2];
    size_t storage_count = definition_fields(definition, storage);
    for (size_t s = 0; s < storage_count; s++) {
      const qa_qvm_mod_protection_scalar *field = storage[s];
      const qa_qvm_mod_actor_record *record = NULL;
      for (size_t r = 0; r < declaration->actor_record_count; r++)
        if (strcmp(declaration->actor_records[r].id, field->record) == 0) { record = &declaration->actor_records[r]; break; } /* TS Array.find: first */
      bool seen_key = false;
      for (size_t i = 0; i < occupied_count; i++)
        if (strcmp(occupied_records[i], field->record) == 0 && occupied_offsets[i] == field->offset)
          seen_key = true;
      if (record == NULL || !isfinite(field->offset) || field->offset != trunc(field->offset) ||
          field->offset < -9007199254740991.0 || field->offset > 9007199254740991.0 || field->offset < 0 ||
          (int64_t)field->offset % 4 != 0 || field->offset + 4 > record->stride || seen_key ||
          occupied_count >= 64)
        qa_range_error("Invalid QVM protection source storage");
      occupied_records[occupied_count] = field->record;
      occupied_offsets[occupied_count++] = field->offset;
      for (size_t f = 0; f < record->field_count; f++) {
        const qa_qvm_mod_actor_field *value = &record->fields[f];
        double width;
        if (strcmp(value->binding, "private") == 0) width = value->byte_length;
        else if (strcmp(value->binding, "origin") == 0 || strcmp(value->binding, "velocity") == 0 ||
                 strcmp(value->binding, "angles") == 0 || strcmp(value->binding, "bounds-min") == 0 ||
                 strcmp(value->binding, "bounds-max") == 0 || strcmp(value->binding, "constant-vector") == 0)
          width = 12;
        else width = 4;
        if (value->offset < field->offset + 4 && field->offset < value->offset + width &&
            strcmp(value->binding, "private") != 0 && strcmp(value->binding, "constant") != 0)
          qa_range_error("QVM protection storage overlaps a shared or linked source field");
      }
    }
    if (definition->has_selection) {
      const qa_qvm_mod_protection_selection *selection = &definition->selection;
      if (selection->has_mask && (!isfinite(selection->mask) || selection->mask != trunc(selection->mask) ||
                                  selection->mask < 0 || selection->mask > 2147483647.0))
        qa_range_error("Invalid QVM protection selection mask");
      for (size_t i = 0; i < selection->value_count; i++) {
        double entry_value = selection->values[i].value;
        bool seen_value = false;
        for (size_t j = 0; j < i; j++) if (selection->values[j].value == entry_value) seen_value = true;
        if (!isfinite(entry_value) || seen_value ||
            (selection->has_mask && (entry_value != trunc(entry_value) ||
                (qa_int32(entry_value) & qa_int32(selection->mask)) != qa_int32(entry_value))))
          qa_range_error("Invalid QVM protection selection");
      }
      if (selection->value_count == 0) qa_range_error("Empty QVM protection selection");
    }
  }
}

typedef struct entry {
  qa_client_id client;
  size_t reservation;
  bool bound;
} entry;

typedef struct actor_entry {
  qa_actor_id actor;
  entry value;
} actor_entry;

typedef struct stage {
  qa_actor_id actor;
  size_t watch_id;
  bool stopped;
  uint64_t generation; /* identity across stages-array realloc */
} stage;

static actor_entry *find_entry(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor) {
  actor_entry *entries = protection->entries;
  for (size_t i = 0; i < protection->entry_count; i++)
    if (qa_same_actor(entries[i].actor, actor)) return &entries[i];
  return NULL;
}

static entry *require_entry(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor) {
  actor_entry *found = find_entry(protection, actor);
  qa_client_id current;
  qa_actor_id resolved;
  if (found == NULL || !protection->services->is_live(protection->services->ctx, actor) ||
      !protection->services->for_actor(protection->services->ctx, actor, &current) ||
      !qa_same_client(current, found->value.client) ||
      !protection->services->actor(protection->services->ctx, found->value.client, &resolved) ||
      !qa_same_actor(resolved, actor))
    qa_range_error("QVM protection client is no longer live");
  return &found->value;
}

static int64_t address_of(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor,
                          const qa_qvm_mod_protection_scalar *field) {
  protection->operations->current(protection->operations->ctx);
  (void)require_entry(protection, actor);
  if (!protection->operations->eligible(protection->operations->ctx, actor))
    qa_range_error("QVM protection requires admitted source client storage");
  return protection->operations->pointer(protection->operations->ctx, actor, field->record) +
         (int64_t)field->offset;
}

static double scalar(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor,
                     const qa_qvm_mod_protection_scalar *field) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(protection->memory, address_of(protection, actor, field), 4, &view);
  return strcmp(field->encoding, "int32") == 0
      ? (double)qa_qvm_writable_view_get_int32(&view, 0, true)
      : (double)qa_qvm_writable_view_get_float32(&view, 0, true);
}

static double count_of(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor,
                       const qa_qvm_mod_protection_scalar *field) {
  double value = scalar(protection, actor, field);
  if (!isfinite(value) || value < 0) qa_range_error("Invalid QVM protection count");
  return value;
}

static const qa_qvm_mod_protection_selection_value *selected(
    qa_qvm_mod_protection_runtime *protection, qa_actor_id actor,
    const qa_qvm_mod_protection_selection *selection) {
  double source = scalar(protection, actor, &selection->field);
  /* TS Math.trunc(source) & mask applies a JS ToInt32 wrap. */
  double value = !selection->has_mask ? source : (double)(qa_int32(source) & qa_int32(selection->mask));
  for (size_t i = 0; i < selection->value_count; i++)
    if (selection->values[i].value == value) return &selection->values[i];
  qa_range_error("QVM protection source selection is undeclared");
}

static qa_regular_armor_state regular_state(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor) {
  if (strcmp(protection->definition->channel, "regular") != 0)
    qa_range_error("QVM protection does not own regular armor");
  qa_regular_armor_state result;
  memset(&result, 0, sizeof result);
  result.kind = QA_REGULAR_ARMOR_SOURCE;
  result.points = count_of(protection, actor, &protection->definition->storage_points);
  result.item = !protection->definition->has_selection
      ? protection->definition->storage_item
      : selected(protection, actor, &protection->definition->selection)->selected_item;
  return result;
}

static qa_powered_protection_state powered_state(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor) {
  if (strcmp(protection->definition->channel, "powered") != 0)
    qa_range_error("QVM protection does not own powered armor");
  const char *kind = selected(protection, actor, &protection->definition->selection)->selected_kind;
  qa_powered_protection_state result;
  result.kind = kind;
  result.cells = strcmp(kind, "none") == 0 ? 0 : count_of(protection, actor, &protection->definition->storage_cells);
  return result;
}

static void validate_count(const qa_qvm_mod_protection_scalar *field, double count) {
  if (!isfinite(count) || count < 0 ||
      (strcmp(field->encoding, "int32") == 0 && (count != trunc(count) || count > 2147483647.0)) ||
      (strcmp(field->encoding, "float32") == 0 && !isfinite((double)(float)count)))
    qa_range_error("Protection count is not representable by its QVM source storage");
}

static void validate_regular(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor, qa_regular_armor_state next) {
  qa_regular_armor_state current = regular_state(protection, actor);
  bool same_item = (current.item == NULL && next.item == NULL) ||
                   (current.item != NULL && next.item != NULL && strcmp(current.item, next.item) == 0);
  if (strcmp(protection->definition->channel, "regular") != 0 || next.kind != QA_REGULAR_ARMOR_SOURCE ||
      current.kind != QA_REGULAR_ARMOR_SOURCE || !same_item)
    qa_range_error("QVM regular armor selection requires its original source operation");
  validate_count(&protection->definition->storage_points, next.points);
}

static void validate_powered(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor, qa_powered_protection_state next) {
  qa_powered_protection_state current = powered_state(protection, actor);
  if (strcmp(protection->definition->channel, "powered") != 0 || strcmp(current.kind, next.kind) != 0)
    qa_range_error("QVM powered armor selection requires its original source operation");
  if (strcmp(next.kind, "none") != 0) validate_count(&protection->definition->storage_cells, next.cells);
}

static void write_scalar(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor,
                         const qa_qvm_mod_protection_scalar *field, double value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(protection->memory, address_of(protection, actor, field), 4, &view);
  if (strcmp(field->encoding, "int32") == 0) qa_qvm_writable_view_set_int32(&view, 0, (int32_t)value, true);
  else qa_qvm_writable_view_set_float32(&view, 0, value, true);
}

typedef struct absorb_job {
  qa_qvm_mod_protection_runtime *protection;
  qa_actor_id actor;
  const qa_qvm_protection_input *inputs;
  size_t input_count;
} absorb_job;

static qa_armor_stage_result run_absorb(void *ctx) {
  absorb_job *job = ctx;
  qa_armor_stage_result result = {(double)job->protection->operations->invoke(
      job->protection->operations->ctx, &job->protection->definition->absorb, job->inputs, job->input_count)};
  return result;
}

static qa_armor_stage_result absorb(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor,
                                    qa_armor_stage_input input, qa_protection_observer observer) {
  if (!qa_same_actor(input.request.target, actor))
    qa_range_error("QVM protection target differs from its source owner");
  (void)require_entry(protection, actor);
  const qa_mod_qc_protection_flags *masks = &protection->definition->flags;
  double scale = input.flags.has_regular_protection_scale ? input.flags.regular_protection_scale : 1;
  if (strcmp(protection->definition->channel, "regular") == 0 && scale != 1) {
    bool lowered = false;
    for (size_t i = 0; i < protection->definition->absorb.argument_count; i++) {
      const qa_qvm_mod_value *value = &protection->definition->absorb.arguments[i];
      if ((strcmp(value->kind, "int32") == 0 || strcmp(value->kind, "float32") == 0) &&
          strcmp(value->value.kind, "input") == 0 &&
          strcmp(value->value.name, "regular-protection-scale") == 0)
        lowered = true;
    }
    for (size_t i = 0; i < protection->definition->absorb.global_count; i++) {
      const qa_qvm_mod_value *value = &protection->definition->absorb.globals[i].value;
      if ((strcmp(value->kind, "int32") == 0 || strcmp(value->kind, "float32") == 0) &&
          strcmp(value->value.kind, "input") == 0 &&
          strcmp(value->value.name, "regular-protection-scale") == 0)
        lowered = true;
    }
    if (!lowered) qa_range_error("QVM regular protection scale has no declared source lowering");
  }
  int32_t lowered = (input.flags.no_armor ? (int32_t)masks->no_armor : 0) |
                    (input.flags.no_power_armor ? (int32_t)masks->no_power_armor : 0) |
                    (input.flags.no_regular_armor ? (int32_t)masks->no_regular_armor : 0) |
                    (input.flags.energy ? (int32_t)masks->energy : 0) |
                    (strcmp(input.request.delivery, "radius") == 0 ? (int32_t)masks->radius : 0);
  qa_source_time time = protection->services->time(protection->services->ctx);
  qa_actor_id self_id = actor;
  const qa_actor_id *attacker = input.request.attack.attacker;
  const qa_actor_id *inflictor = input.request.attack.inflictor;
  qa_mod_runtime_value attacker_value = {"actor", 0, NULL, {0, 0, 0}, attacker};
  qa_mod_runtime_value inflictor_value = {"actor", 0, NULL, {0, 0, 0}, inflictor};
  qa_vec3 point = input.point, direction = input.direction, normal = input.normal;
  qa_qvm_protection_input inputs[] = {
      {"self", {"actor", 0, NULL, {0, 0, 0}, &self_id}},
      {"attacker", attacker_value},
      {"inflictor", inflictor_value},
      {"amount", {"float", input.amount, NULL, point, NULL}},
      {"knockback", {"float", input.request.knockback, NULL, point, NULL}},
      {"damage-flags", {"float", (double)lowered, NULL, point, NULL}},
      {"regular-protection-scale", {"float", scale, NULL, point, NULL}},
      {"point", {"vector", 0, NULL, point, NULL}},
      {"direction", {"vector", 0, NULL, direction, NULL}},
      {"normal", {"vector", 0, NULL, normal, NULL}},
      {"time", {"float", time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000, NULL, point, NULL}},
  };
  absorb_job job = {protection, actor, inputs, 11};
  int32_t saved = (int32_t)qa_qvm_mod_protection_observe(protection, actor, observer, run_absorb, &job).saved;
  qa_armor_stage_result result = {(double)saved};
  return result;
}

typedef struct binding_state {
  qa_qvm_mod_protection_runtime *protection;
  qa_actor_id actor;
} binding_state;

static qa_regular_armor_state binding_read_regular(void *ctx) {
  binding_state *state = ctx;
  return regular_state(state->protection, state->actor);
}
static void binding_validate_regular(void *ctx, qa_regular_armor_state next) {
  binding_state *state = ctx;
  validate_regular(state->protection, state->actor, next);
}
static void binding_write_regular(void *ctx, qa_regular_armor_state next) {
  binding_state *state = ctx;
  validate_regular(state->protection, state->actor, next);
  if (strcmp(state->protection->definition->channel, "regular") != 0 || next.kind != QA_REGULAR_ARMOR_SOURCE)
    qa_range_error("Missing QVM regular source storage");
  write_scalar(state->protection, state->actor, &state->protection->definition->storage_points, next.points);
}
static qa_powered_protection_state binding_read_powered(void *ctx) {
  binding_state *state = ctx;
  return powered_state(state->protection, state->actor);
}
static void binding_validate_powered(void *ctx, qa_powered_protection_state next) {
  binding_state *state = ctx;
  validate_powered(state->protection, state->actor, next);
}
static void binding_write_powered(void *ctx, qa_powered_protection_state next) {
  binding_state *state = ctx;
  validate_powered(state->protection, state->actor, next);
  if (strcmp(state->protection->definition->channel, "powered") != 0)
    qa_range_error("Missing QVM powered source storage");
  if (strcmp(next.kind, "none") != 0)
    write_scalar(state->protection, state->actor, &state->protection->definition->storage_cells, next.cells);
}
static qa_armor_stage_result binding_absorb(void *ctx, qa_armor_stage_input input, qa_protection_observer observer) {
  binding_state *state = ctx;
  return absorb(state->protection, state->actor, input, observer);
}

static qa_qvmy_protection_binding claim_of(qa_qvm_mod_protection_runtime *protection) {
  qa_qvmy_protection_binding claim;
  memset(&claim, 0, sizeof claim);
  claim.owner = protection->owner;
  claim.rule = protection->definition->id;
  claim.admission = protection->definition->admission;
  return claim;
}

void qa_qvm_mod_protection_create(qa_qvm_mod_protection_component *component,
                                  qa_qvm_mod_protection_runtime *storage,
                                  qa_qvm_mod_protection_runtime **channels,
                                  const qa_qvm_mod_protection *definitions, size_t definition_count,
                                  qa_qvm_memory *memory, const qa_qvmy_protection_services *services,
                                  qa_provider_id owner, const qa_qvm_mod_protection_operations *operations) {
  memset(component, 0, sizeof *component);
  component->channels = channels;
  component->channel_count = definition_count;
  for (size_t i = 0; i < definition_count; i++) {
    memset(&storage[i], 0, sizeof storage[i]);
    storage[i].definition = &definitions[i];
    storage[i].memory = memory;
    storage[i].services = services;
    storage[i].owner = owner;
    storage[i].operations = operations;
    storage[i].component = component;
    channels[i] = &storage[i];
  }
}

void qa_qvm_mod_protection_component_destroy(qa_qvm_mod_protection_component *component) {
  free(component->stages);
  memset(component, 0, sizeof *component);
}

void qa_qvm_mod_protection_runtime_destroy(qa_qvm_mod_protection_runtime *protection) {
  free(protection->entries);
  memset(protection, 0, sizeof *protection);
}

bool qa_qvm_mod_protection_runtime_get_is_active(const qa_qvm_mod_protection_runtime *protection) {
  return protection->active;
}

void qa_qvm_mod_protection_reserve(qa_qvm_mod_protection_runtime *protection) {
  if (!protection->services->has_clients) qa_range_error("QVM protection requires canonical clients");
  size_t capacity = 64;
  qa_actor_id *clients = malloc(capacity * sizeof *clients);
  if (clients == NULL) qa_range_error("QVM memory allocation failed");
  size_t count = protection->services->clients(protection->services->ctx, clients, capacity);
  if (count > capacity) {
    clients = realloc(clients, count * sizeof *clients);
    if (clients == NULL) qa_range_error("QVM memory allocation failed");
    count = protection->services->clients(protection->services->ctx, clients, count);
  }
  for (size_t i = 0; i < count; i++) qa_qvm_mod_protection_reserve_actor(protection, clients[i]);
  free(clients);
}

void qa_qvm_mod_protection_reserve_actor(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor) {
  protection->operations->current(protection->operations->ctx);
  if (find_entry(protection, actor) != NULL) {
    (void)require_entry(protection, actor);
    return;
  }
  qa_client_id client;
  qa_owned_actor owned;
  qa_actor_id resolved;
  if (!protection->services->has_clients ||
      !protection->services->for_actor(protection->services->ctx, actor, &client) ||
      !protection->services->resolve_owned(protection->services->ctx, actor, &owned) ||
      !protection->services->actor(protection->services->ctx, client, &resolved) ||
      !qa_same_actor(resolved, actor))
    qa_range_error("QVM protection requires a live canonical client");
  qa_qvmy_protection_binding claim = claim_of(protection);
  size_t reservation = protection->services->reserve_protection(
      protection->services->ctx, owned,
      strcmp(protection->definition->channel, "regular") == 0 ? QA_PROTECTION_REGULAR : QA_PROTECTION_POWERED,
      &claim);
  if (protection->entry_count == protection->entry_capacity) {
    protection->entry_capacity = protection->entry_capacity == 0 ? 8 : protection->entry_capacity * 2;
    protection->entries = realloc(protection->entries, protection->entry_capacity * sizeof(actor_entry));
    if (protection->entries == NULL) qa_range_error("QVM memory allocation failed");
  }
  actor_entry *created = &((actor_entry *)protection->entries)[protection->entry_count++];
  created->actor = actor;
  created->value.client = client;
  created->value.reservation = reservation;
  created->value.bound = false;
}

void qa_qvm_mod_protection_bind_actor(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor) {
  if (!protection->active || !protection->operations->eligible(protection->operations->ctx, actor)) return;
  entry *found = require_entry(protection, actor);
  if (found->bound) return;
  binding_state *state = malloc(sizeof *state);
  if (state == NULL) qa_range_error("QVM memory allocation failed");
  state->protection = protection;
  state->actor = actor;
  qa_qvmy_protection_binding binding = claim_of(protection);
  binding.channel = protection->definition->channel;
  binding.ctx = state;
  binding.absorb = binding_absorb;
  const qa_original_pickup_rule **pickup_ptrs = NULL;
  qa_original_pickup_rule *flat = NULL;
  binding.read_regular = binding_read_regular;
  binding.validate_regular = binding_validate_regular;
  binding.write_regular = binding_write_regular;
  binding.read_powered = binding_read_powered;
  binding.validate_powered = binding_validate_powered;
  binding.write_powered = binding_write_powered;
  /* TS reads the current state before evaluating pickups for bind. */
  if (strcmp(protection->definition->channel, "regular") == 0) (void)regular_state(protection, actor);
  else (void)powered_state(protection, actor);
  if (protection->operations->pickups != NULL) {
    qa_protection_channel channel = strcmp(protection->definition->channel, "regular") == 0
        ? QA_PROTECTION_REGULAR : QA_PROTECTION_POWERED;
    size_t pickup_count = protection->operations->pickups(protection->operations->ctx, actor, channel, NULL, 0);
    pickup_ptrs = malloc((pickup_count == 0 ? 1 : pickup_count) * sizeof *pickup_ptrs);
    flat = malloc((pickup_count == 0 ? 1 : pickup_count) * sizeof *flat);
    if (pickup_ptrs == NULL || flat == NULL) qa_range_error("QVM memory allocation failed");
    pickup_count = protection->operations->pickups(protection->operations->ctx, actor, channel,
                                                    pickup_ptrs, pickup_count);
    for (size_t i = 0; i < pickup_count; i++) flat[i] = *pickup_ptrs[i];
    binding.pickups = flat;
    binding.pickup_count = pickup_count;
  }
  protection->services->bind_reservation(protection->services->ctx, found->reservation, &binding);
  free(pickup_ptrs);
  free(flat);
  /* Re-find: bind_reservation may reenter and realloc the entries. */
  found = require_entry(protection, actor);
  found->bound = true;
}

void qa_qvm_mod_protection_activate(qa_qvm_mod_protection_runtime *protection) {
  qa_qvm_mod_protection_reserve(protection);
  protection->active = true;
  qa_qvm_mod_protection_sync(protection);
}

void qa_qvm_mod_protection_sync(qa_qvm_mod_protection_runtime *protection) {
  if (!protection->active) return;
  /* Snapshot actors: bind_actor callbacks may reserve/release entries. */
  size_t count = protection->entry_count;
  qa_actor_id *actors = malloc((count == 0 ? 1 : count) * sizeof *actors);
  if (actors == NULL) qa_range_error("QVM memory allocation failed");
  actor_entry *entries = protection->entries;
  for (size_t i = 0; i < count; i++) actors[i] = entries[i].actor;
  for (size_t i = 0; i < count; i++) qa_qvm_mod_protection_bind_actor(protection, actors[i]);
  free(actors);
}

typedef struct observe_state {
  qa_qvm_mod_protection_runtime *self;
  qa_actor_id actor;
  uint64_t stage_generation;
  qa_qvm_mod_protection_runtime *regular_channel;
  qa_qvm_mod_protection_runtime *powered_channel;
  bool has_regular;
  qa_regular_armor_state before_regular;
  bool has_powered;
  qa_powered_protection_state before_powered;
  qa_protection_observer observer;
} observe_state;

static void observe_write(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  observe_state *state = ctx;
  qa_qvm_mod_protection_runtime *self = state->self;
  qa_actor_id actor = state->actor;
  if (!self->services->is_live(self->services->ctx, actor) || find_entry(self, actor) == NULL) return;
  qa_regular_armor_state after_regular;
  qa_powered_protection_state after_powered;
  memset(&after_regular, 0, sizeof after_regular);
  memset(&after_powered, 0, sizeof after_powered);
  if (state->has_regular) after_regular = regular_state(state->regular_channel, actor);
  if (state->has_powered) after_powered = powered_state(state->powered_channel, actor);
  bool regular_change = state->has_regular &&
      (state->before_regular.kind != after_regular.kind ||
       (state->before_regular.kind == QA_REGULAR_ARMOR_SOURCE && after_regular.kind == QA_REGULAR_ARMOR_SOURCE &&
        (state->before_regular.points != after_regular.points ||
         !((state->before_regular.item == NULL && after_regular.item == NULL) ||
           (state->before_regular.item != NULL && after_regular.item != NULL &&
            strcmp(state->before_regular.item, after_regular.item) == 0)))));
  bool powered_change = state->has_powered &&
      (strcmp(state->before_powered.kind, after_powered.kind) != 0 ||
       (strcmp(state->before_powered.kind, "none") != 0 && strcmp(after_powered.kind, "none") != 0 &&
        state->before_powered.cells != after_powered.cells));
  qa_regular_armor_state before_regular = state->before_regular;
  qa_powered_protection_state before_powered = state->before_powered;
  if (state->has_regular) state->before_regular = after_regular;
  if (state->has_powered) state->before_powered = after_powered;
  stage *stages = self->component->stages;
  uint64_t current = 0;
  for (size_t i = 0; i < self->component->stage_count; i++)
    if (qa_same_actor(stages[i].actor, actor)) current = stages[i].generation;
  if (current != state->stage_generation) return;
  qa_protection_store change;
  memset(&change, 0, sizeof change);
  if (regular_change) {
    change.has_regular = true;
    change.regular_before = before_regular;
    change.regular_after = after_regular;
    if (powered_change) {
      change.has_powered = true;
      change.powered_before = before_powered;
      change.powered_after = after_powered;
    }
    state->observer.stored(state->observer.ctx, change);
  } else if (powered_change) {
    change.has_powered = true;
    change.powered_before = before_powered;
    change.powered_after = after_powered;
    state->observer.stored(state->observer.ctx, change);
  }
}

qa_armor_stage_result qa_qvm_mod_protection_observe(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor,
                                                    qa_protection_observer observer,
                                                    qa_qvm_protection_execute_fn execute, void *execute_ctx) {
  qa_qvm_mod_protection_component *component = protection->component;
  qa_qvm_mod_protection_runtime *bound[8];
  size_t bound_count = 0;
  for (size_t i = 0; i < component->channel_count; i++) {
    actor_entry *found = find_entry(component->channels[i], actor);
    if (found != NULL && found->value.bound && bound_count < 8) bound[bound_count++] = component->channels[i];
  }
  observe_state state;
  memset(&state, 0, sizeof state);
  state.self = protection;
  state.actor = actor;
  state.observer = observer;
  for (size_t i = 0; i < bound_count; i++) {
    if (strcmp(bound[i]->definition->channel, "regular") == 0) {
      state.has_regular = true;
      state.regular_channel = bound[i];
    } else {
      state.has_powered = true;
      state.powered_channel = bound[i];
    }
  }
  if (state.has_regular) state.before_regular = regular_state(state.regular_channel, actor);
  if (state.has_powered) state.before_powered = powered_state(state.powered_channel, actor);
  qa_qvm_write_range ranges[16];
  size_t range_count = 0;
  for (size_t i = 0; i < bound_count; i++) {
    const qa_qvm_mod_protection_scalar *channel_fields[2];
    size_t count = definition_fields(bound[i]->definition, channel_fields);
    for (size_t f = 0; f < count && range_count < 16; f++) {
      ranges[range_count].byte_offset = address_of(bound[i], actor, channel_fields[f]);
      ranges[range_count].byte_length = 4;
      range_count++;
    }
  }
  if (component->stage_count == component->stage_capacity) {
    component->stage_capacity = component->stage_capacity == 0 ? 8 : component->stage_capacity * 2;
    component->stages = realloc(component->stages, component->stage_capacity * sizeof(stage));
    if (component->stages == NULL) qa_range_error("QVM memory allocation failed");
  }
  stage *created = &((stage *)component->stages)[component->stage_count++];
  created->actor = actor;
  created->stopped = false;
  created->generation = ++component->stage_generation_counter;
  uint64_t generation = created->generation;
  state.stage_generation = generation;
  created->watch_id = qa_qvm_memory_observe_writes(protection->memory, ranges, range_count,
                                                    observe_write, &state, NULL, NULL);
  qa_armor_stage_result result = execute(execute_ctx);
  if (find_entry(protection, actor) != NULL && protection->services->is_live(protection->services->ctx, actor))
    (void)require_entry(protection, actor);
  /* Re-find by generation: nested observes may realloc the array. */
  stage *stages = component->stages;
  for (size_t i = 0; i < component->stage_count; i++) {
    if (stages[i].generation != generation) continue;
    qa_qvm_memory_writes_unobserve(&protection->memory->writes, stages[i].watch_id);
    memmove(&stages[i], &stages[i + 1], (component->stage_count - i - 1) * sizeof *stages);
    component->stage_count--;
    break;
  }
  return result;
}

void qa_qvm_mod_protection_assert_idle(qa_qvm_mod_protection_runtime *protection) {
  if (protection->component->stage_count != 0)
    qa_range_error("Cannot save or restore during QVM protection execution");
}

void qa_qvm_mod_protection_release(qa_qvm_mod_protection_runtime *protection, qa_actor_id actor) {
  stage *stages = protection->component->stages;
  for (size_t i = 0; i < protection->component->stage_count; i++) {
    if (qa_same_actor(stages[i].actor, actor) && !stages[i].stopped) {
      stages[i].stopped = true;
      qa_qvm_memory_writes_unobserve(&protection->memory->writes, stages[i].watch_id);
    }
  }
  actor_entry *entries = protection->entries;
  for (size_t i = 0; i < protection->entry_count; i++) {
    if (!qa_same_actor(entries[i].actor, actor)) continue;
    size_t reservation = entries[i].value.reservation;
    memmove(&entries[i], &entries[i + 1], (protection->entry_count - i - 1) * sizeof *entries);
    protection->entry_count--;
    protection->services->close_reservation(protection->services->ctx, reservation);
    return;
  }
}

void qa_qvm_mod_protection_close(qa_qvm_mod_protection_runtime *protection) {
  protection->active = false;
  while (protection->entry_count != 0) {
    actor_entry *entries = protection->entries;
    qa_actor_id actor = entries[0].actor;
    qa_qvm_mod_protection_release(protection, actor);
  }
}
