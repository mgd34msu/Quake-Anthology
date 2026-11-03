/* See include/qa/compat/qvm/mod_pickups.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_pickups.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/inventory.h"

/* ModHostServices.clients.clients() projected to actor ids. */
static size_t client_actors(qa_mod_host_services *services, qa_actor_id *out, size_t capacity) {
  size_t count = 0;
  const qa_mod_client_identity *identities = services->clients->clients(services->clients->ctx, &count);
  for (size_t i = 0; i < count && i < capacity; i++) out[i] = identities[i].actor;
  return count;
}

void qa_qvm_validate_mod_pickups(const qa_qvm_mod_callback_declaration *declaration) {
  for (size_t r = 0; r < declaration->pickup_count; r++) {
    const qa_qvm_mod_pickup *rule = &declaration->pickups[r];
    const qa_mod_pickup_rule *base = &rule->rule;
    bool duplicate_id = false;
    for (size_t j = 0; j < r; j++)
      if (strcmp(declaration->pickups[j].rule.id, base->id) == 0) duplicate_id = true;
    if (declaration->clients == NULL || duplicate_id || base->id[0] == '\0' || base->offered_count == 0)
      qa_range_error("QVM pickups require unique rules and source client admission");
    for (size_t i = 0; i < base->offered_count; i++) {
      for (size_t j = 0; j < r; j++) {
        for (size_t k = 0; k < declaration->pickups[j].rule.offered_count; k++)
          if (strcmp(declaration->pickups[j].rule.offered[k], base->offered[i]) == 0)
            qa_range_error("Ambiguous QVM original pickup item");
      }
      for (size_t k = 0; k < i; k++)
        if (strcmp(base->offered[k], base->offered[i]) == 0)
          qa_range_error("Ambiguous QVM original pickup item");
    }
    for (size_t w = 0; w < base->writes.count; w++) {
      const qa_pickup_write *resource = &base->writes.values[w];
      if (resource->kind == QA_PICKUP_RESOURCE_PROTECTION) {
        bool found = false;
        for (size_t p = 0; p < declaration->protection_count; p++) {
          const char *channel = declaration->protection[p].channel;
          qa_protection_channel value = strcmp(channel, "regular") == 0 ? QA_PROTECTION_REGULAR : QA_PROTECTION_POWERED;
          if (value == resource->channel) found = true;
        }
        if (!found) qa_range_error("QVM pickup has no protection owner");
      } else {
        bool fields_count = strcmp(resource->fields, "count") == 0;
        bool projected = false;
        if (fields_count) {
          for (size_t rec = 0; rec < declaration->actor_record_count && !projected; rec++) {
            for (size_t f = 0; f < declaration->actor_records[rec].field_count; f++) {
              const qa_qvm_mod_actor_field *field = &declaration->actor_records[rec].fields[f];
              if (strcmp(field->binding, "inventory") == 0 && strcmp(field->item, resource->item) == 0)
                projected = true;
            }
          }
        }
        bool owned = false;
        if (declaration->items != NULL) {
          for (size_t s = 0; s < declaration->items->storage_count; s++) {
            const qa_qvm_item_storage *storage = &declaration->items->storage[s];
            if (strcmp(storage->kind, "bits") == 0) {
              if (!fields_count) continue;
              for (size_t i = 0; i < storage->item_count; i++)
                if (strcmp(storage->items[i].item, resource->item) == 0) owned = true;
            } else if (strcmp(storage->item, resource->item) == 0 &&
                       (fields_count || strcmp(storage->capacity.kind, "field") == 0)) {
              owned = true;
            }
          }
        }
        if (!projected && !owned)
          qa_range_error("QVM pickup has no declared inventory storage for its requested count/capacity writes");
      }
    }
    const qa_original_pickup_operation *operation = &base->operation;
    const qa_qvm_mod_source_call *grant = operation->grant;
    const qa_qvm_mod_source_call *gate = operation->gate;
    if ((!operation->is_gate_then_grant && strcmp(grant->returns, "void") == 0) ||
        (operation->is_gate_then_grant && (strcmp(gate->returns, "void") == 0 ||
            (strcmp(operation->grant_accepts, "nonzero") == 0 && strcmp(grant->returns, "void") == 0))))
      qa_range_error("QVM pickup requires its declared source decision");
    for (size_t c = 0; c < rule->context_count; c++) {
      const qa_qvm_mod_pickup_context *field = &rule->context[c];
      const qa_qvm_mod_actor_record *record = NULL;
      for (size_t rec = 0; rec < declaration->actor_record_count; rec++) {
        if (strcmp(declaration->actor_records[rec].id, field->record) == 0) {
          record = &declaration->actor_records[rec]; /* TS Array.find: first */
          break;
        }
      }
      bool in_clients = false;
      if (record != NULL) {
        for (size_t i = 0; i < declaration->clients->records.count; i++)
          if (strcmp(declaration->clients->records.values[i], record->id) == 0) in_clients = true;
      }
      bool occupied = false;
      for (size_t j = 0; j < c; j++) {
        if (strcmp(rule->context[j].record, field->record) == 0 && rule->context[j].offset == field->offset)
          occupied = true;
      }
      if (record == NULL || in_clients || occupied || !isfinite(field->offset) ||
          field->offset != trunc(field->offset) || field->offset > 9007199254740991.0 ||
          field->offset < 0 ||
          (int64_t)field->offset % 4 != 0 || field->offset + 4 > record->stride)
        qa_range_error("Invalid QVM pickup source context");
      for (size_t f = 0; f < record->field_count; f++) {
        const qa_qvm_mod_actor_field *binding = &record->fields[f];
        double width;
        if (strcmp(binding->binding, "private") == 0) width = binding->byte_length;
        else if (strcmp(binding->binding, "origin") == 0 || strcmp(binding->binding, "velocity") == 0 ||
                 strcmp(binding->binding, "angles") == 0 || strcmp(binding->binding, "bounds-min") == 0 ||
                 strcmp(binding->binding, "bounds-max") == 0 || strcmp(binding->binding, "constant-vector") == 0)
          width = 12;
        else width = 4;
        if (binding->offset < field->offset + 4 && field->offset < binding->offset + width &&
            strcmp(binding->binding, "private") != 0 && strcmp(binding->binding, "constant") != 0)
          qa_range_error("QVM pickup context overlaps shared or linked storage");
      }
      const qa_qvm_mod_source_actors *source = declaration->source_actors;
      if (declaration->entity_record != NULL && strcmp(record->id, declaration->entity_record) == 0 &&
          source != NULL) {
        bool overlaps = field->offset == source->inuse;
        if (source->callbacks != NULL) {
          const qa_qvm_mod_source_callbacks *callbacks = source->callbacks;
          if ((callbacks->has_touch && field->offset == callbacks->touch) ||
              (callbacks->has_use && field->offset == callbacks->use) ||
              (callbacks->has_pain && field->offset == callbacks->pain) ||
              (callbacks->has_die && field->offset == callbacks->die))
            overlaps = true;
        }
        if (overlaps) qa_range_error("QVM pickup context overlaps source actor lifetime");
      }
    }
  }
}

typedef struct entry {
  qa_actor_id actor;
  qa_original_pickup_rule *rules; /* owned, definition_count */
  bool has_delegate;
  bool has_binding;
  qa_owned_actor owned;
  int64_t delegate_id;
} entry;

typedef struct take_job {
  qa_qvm_mod_pickups *pickups;
  qa_actor_id actor;
  const qa_qvm_mod_pickup *definition;
} take_job;

static entry *find_entry(qa_qvm_mod_pickups *pickups, qa_actor_id actor) {
  entry *entries = pickups->entries;
  for (size_t i = 0; i < pickups->entry_count; i++)
    if (qa_same_actor(entries[i].actor, actor)) return &entries[i];
  return NULL;
}

static qa_original_pickup_decision take_rule(void *ctx, qa_original_pickup_offer offer,
                                              qa_original_pickup_execution execution);

static entry *actor_entry(qa_qvm_mod_pickups *pickups, qa_actor_id actor) {
  entry *found = find_entry(pickups, actor);
  if (found != NULL) return found;
  if (pickups->entry_count == pickups->entry_capacity) {
    pickups->entry_capacity = pickups->entry_capacity == 0 ? 8 : pickups->entry_capacity * 2;
    pickups->entries = realloc(pickups->entries, pickups->entry_capacity * sizeof(entry));
    if (pickups->entries == NULL) qa_range_error("QVM memory allocation failed");
  }
  entry *created = &((entry *)pickups->entries)[pickups->entry_count++];
  created->actor = actor;
  created->has_delegate = false;
  created->has_binding = false;
  created->delegate_id = 0;
  created->rules = malloc((pickups->definition_count == 0 ? 1 : pickups->definition_count) * sizeof *created->rules);
  if (created->rules == NULL) qa_range_error("QVM memory allocation failed");
  for (size_t i = 0; i < pickups->definition_count; i++) {
    const qa_qvm_mod_pickup *definition = &pickups->definitions[i];
    take_job *job = malloc(sizeof *job);
    if (job == NULL) qa_range_error("QVM memory allocation failed");
    job->pickups = pickups;
    job->actor = actor;
    job->definition = definition;
    created->rules[i].ctx = job;
    created->rules[i].id = definition->rule.id;
    created->rules[i].offered = definition->rule.offered;
    created->rules[i].offered_count = definition->rule.offered_count;
    created->rules[i].writes = definition->rule.writes;
    created->rules[i].take = take_rule;
  }
  return created;
}

typedef struct observe_job {
  qa_qvm_mod_pickups *pickups;
  qa_actor_id actor;
  const qa_original_pickup_offer *offer;
  const qa_qvm_mod_pickup *definition;
  const qa_qvm_pickup_input *inputs;
  size_t input_count;
  const qa_original_pickup_execution *observer;
} observe_job;

static bool take_current(qa_qvm_mod_pickups *pickups, qa_actor_id actor,
                         const qa_original_pickup_offer *offer,
                         const qa_original_pickup_execution *observer) {
  return observer->current(observer->current_ctx) && pickups->active &&
         qa_session_actor_registry_is_live(pickups->services->actors, actor) &&
         pickups->operations->eligible(pickups->operations->ctx, actor) &&
         qa_session_actor_registry_is_live(pickups->services->actors, offer->pickup);
}

static qa_original_pickup_decision run_observe(void *ctx) {
  observe_job *job = ctx;
  qa_qvm_mod_pickups *pickups = job->pickups;
  const qa_original_pickup_operation *operation = &job->definition->rule.operation;
  if (operation->is_gate_then_grant) {
    double gated = pickups->operations->invoke(pickups->operations->ctx, operation->gate,
                                                job->inputs, job->input_count);
    if (gated == 0 || !take_current(pickups, job->actor, job->offer, job->observer))
      return QA_ORIGINAL_PICKUP_REFUSED;
  }
  double result = pickups->operations->invoke(pickups->operations->ctx, operation->grant,
                                               job->inputs, job->input_count);
  bool accepted = (operation->is_gate_then_grant && strcmp(operation->grant_accepts, "always") == 0) || result != 0;
  return accepted ? QA_ORIGINAL_PICKUP_ACCEPTED : QA_ORIGINAL_PICKUP_REFUSED;
}

typedef struct context_job {
  qa_qvm_mod_pickups *pickups;
  qa_actor_id actor;
  const qa_original_pickup_execution *observer;
  observe_job *observe;
} context_job;

static qa_original_pickup_decision run_context(void *ctx) {
  context_job *job = ctx;
  return job->pickups->operations->observe(job->pickups->operations->ctx, job->actor,
                                           job->observer, run_observe, job->observe);
}

/* TS finally { this.depth--; }. */
static void take_unwind(void *ctx) {
  qa_qvm_mod_pickups *pickups = ctx;
  pickups->depth--;
}

static qa_original_pickup_decision take_rule(void *ctx, qa_original_pickup_offer offer,
                                              qa_original_pickup_execution execution) {
  take_job *job = ctx;
  qa_qvm_mod_pickups *pickups = job->pickups;
  qa_actor_id actor = job->actor;
  const qa_qvm_mod_pickup *definition = job->definition;
  pickups->operations->current(pickups->operations->ctx);
  bool offered = false;
  for (size_t i = 0; i < definition->rule.offered_count; i++)
    if (strcmp(definition->rule.offered[i], offer.item) == 0) offered = true;
  if (!qa_same_actor(offer.recipient, actor) || !offered ||
      !take_current(pickups, actor, &offer, &execution))
    qa_range_error("QVM pickup rule is no longer current");
  qa_actor_id self_id = actor;
  qa_actor_id other_id = offer.pickup;
  qa_qvm_pickup_input inputs[7];
  inputs[0] = (qa_qvm_pickup_input){"self", {"actor", 0, NULL, {0, 0, 0}, &self_id}};
  inputs[1] = (qa_qvm_pickup_input){"other", {"actor", 0, NULL, {0, 0, 0}, &other_id}};
  inputs[2] = (qa_qvm_pickup_input){"item", {"string", 0, offer.item, {0, 0, 0}, NULL}};
  inputs[3] = (qa_qvm_pickup_input){"time", {"float", offer.time.kind == QA_SOURCE_TIME_SECONDS ? offer.time.value : offer.time.value / 1000, NULL, {0, 0, 0}, NULL}};
  inputs[4] = (qa_qvm_pickup_input){"pickup-count", {"float", offer.count.is_override ? offer.count.amount : 0, NULL, {0, 0, 0}, NULL}};
  inputs[5] = (qa_qvm_pickup_input){"pickup-has-count", {"float", offer.count.is_override ? 1 : 0, NULL, {0, 0, 0}, NULL}};
  inputs[6] = (qa_qvm_pickup_input){"pickup-dropped", {"float", offer.dropped ? 1 : 0, NULL, {0, 0, 0}, NULL}};
  observe_job observe = {pickups, actor, &offer, definition, inputs, 7, &execution};
  context_job context = {pickups, actor, &execution, &observe};
  pickups->depth++;
  qa_cleanup guard;
  qa_cleanup_push(&guard, take_unwind, pickups);
  qa_original_pickup_decision result = pickups->operations->context(
      pickups->operations->ctx, definition, &offer, inputs, 7, run_context, &context);
  qa_cleanup_pop(&guard);
  pickups->depth--;
  return result;
}

void qa_qvm_mod_pickups_init(qa_qvm_mod_pickups *pickups, const qa_qvm_mod_pickup *definitions,
                             size_t definition_count, qa_mod_host_services *services,
                             qa_provider_id owner, const qa_qvm_mod_pickup_operations *operations) {
  memset(pickups, 0, sizeof *pickups);
  pickups->definitions = definitions;
  pickups->definition_count = definition_count;
  pickups->services = services;
  pickups->owner = owner;
  pickups->operations = operations;
}

void qa_qvm_mod_pickups_destroy(qa_qvm_mod_pickups *pickups) {
  entry *entries = pickups->entries;
  for (size_t i = 0; i < pickups->entry_count; i++) {
    for (size_t r = 0; r < pickups->definition_count; r++) free(entries[i].rules[r].ctx);
    free(entries[i].rules);
  }
  free(entries);
  memset(pickups, 0, sizeof *pickups);
}

bool qa_qvm_mod_pickups_get_is_active(const qa_qvm_mod_pickups *pickups) { return pickups->active; }

size_t qa_qvm_mod_pickups_protection(qa_qvm_mod_pickups *pickups, qa_actor_id actor,
                                     qa_protection_channel channel,
                                     const qa_original_pickup_rule **out, size_t capacity) {
  entry *found = actor_entry(pickups, actor);
  size_t count = 0;
  for (size_t i = 0; i < pickups->definition_count; i++) {
    bool matches = false;
    for (size_t w = 0; w < found->rules[i].writes.count; w++) {
      const qa_pickup_write *write = &found->rules[i].writes.values[w];
      if (write->kind == QA_PICKUP_RESOURCE_PROTECTION && write->channel == channel) matches = true;
    }
    if (matches) {
      if (count < capacity) out[count] = &found->rules[i];
      count++;
    }
  }
  return count;
}

void qa_qvm_mod_pickups_activate(qa_qvm_mod_pickups *pickups) {
  pickups->operations->current(pickups->operations->ctx);
  pickups->active = true;
  if (pickups->services->clients == NULL) return;
  size_t capacity = 64;
  qa_actor_id *clients = malloc(capacity * sizeof *clients);
  if (clients == NULL) qa_range_error("QVM memory allocation failed");
  size_t count = client_actors(pickups->services, clients, capacity);
  if (count > capacity) {
    clients = realloc(clients, count * sizeof *clients);
    if (clients == NULL) qa_range_error("QVM memory allocation failed");
    count = client_actors(pickups->services, clients, count);
  }
  for (size_t i = 0; i < count; i++) qa_qvm_mod_pickups_bind_actor(pickups, clients[i]);
  free(clients);
}

void qa_qvm_mod_pickups_bind_actor(qa_qvm_mod_pickups *pickups, qa_actor_id actor) {
  entry *existing = find_entry(pickups, actor);
  if (!pickups->active || !pickups->operations->eligible(pickups->operations->ctx, actor) ||
      (existing != NULL && existing->has_delegate))
    return;
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(pickups->services->actors, actor, &owned))
    qa_range_error("QVM pickup recipient is retired");
  entry *created = actor_entry(pickups, actor);
  qa_original_pickup_rule *flat = malloc(
      (pickups->definition_count == 0 ? 1 : pickups->definition_count) * sizeof *flat);
  if (flat == NULL) qa_range_error("QVM memory allocation failed");
  size_t count = 0;
  for (size_t i = 0; i < pickups->definition_count; i++) {
    bool inventory = false;
    for (size_t w = 0; w < created->rules[i].writes.count; w++)
      if (created->rules[i].writes.values[w].kind == QA_PICKUP_RESOURCE_INVENTORY) inventory = true;
    if (inventory) flat[count++] = created->rules[i];
  }
  if (count != 0) {
    qa_inventory_pickup_binding binding = {pickups->owner, flat, count};
    created->delegate_id = qa_shared_inventory_table_bind_pickup(pickups->services->inventory, owned, binding);
    created->owned = owned;
    created->has_binding = true;
    created->has_delegate = true;
    free(flat);
  } else {
    free(flat);
    created->has_delegate = true;
    created->delegate_id = 0;
  }
}

void qa_qvm_mod_pickups_assert_idle(const qa_qvm_mod_pickups *pickups) {
  if (pickups->depth != 0)
    qa_range_error("Cannot save or restore during QVM original pickup execution");
}

void qa_qvm_mod_pickups_release(qa_qvm_mod_pickups *pickups, qa_actor_id actor) {
  entry *entries = pickups->entries;
  for (size_t i = 0; i < pickups->entry_count; i++) {
    if (!qa_same_actor(entries[i].actor, actor)) continue;
    bool has_binding = entries[i].has_binding;
    qa_owned_actor owned = entries[i].owned;
    int64_t delegate_id = entries[i].delegate_id;
    for (size_t r = 0; r < pickups->definition_count; r++) free(entries[i].rules[r].ctx);
    free(entries[i].rules);
    memmove(&entries[i], &entries[i + 1], (pickups->entry_count - i - 1) * sizeof *entries);
    pickups->entry_count--;
    if (has_binding) qa_shared_inventory_table_unbind_pickup(pickups->services->inventory, owned, delegate_id);
    return;
  }
}

void qa_qvm_mod_pickups_close(qa_qvm_mod_pickups *pickups) {
  pickups->active = false;
  while (pickups->entry_count != 0) {
    entry *entries = pickups->entries;
    qa_actor_id actor = entries[0].actor;
    qa_qvm_mod_pickups_release(pickups, actor);
  }
}
