/* See include/qa/compat/qvm/mod_provider.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_provider.h"

/* Checkpoint forms are qa_qvm_cp_* in mod_presentation_checkpoint.h;
 * borrowed view forms are qa_qvm_* in mod_presentations.h. */
#include "qa/compat/qvm/mod_presentation_checkpoint.h"
typedef qa_qvm_cp_mod_scene_publication CpScene;

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/client_game_syscalls.h"
#include "qa/compat/qvm/common_syscalls.h"
#include "qa/compat/qvm/entity_record.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/mod_objectives.h"
#include "qa/compat/qvm/player_record.h"
#include "qa/compat/qvm/shared_entity_record.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/compat/qvm/trace_record.h"
#include "qa/content/mounts.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/qvm_mod_actor_frame.h"
#include "qa/core/cvars/index.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/body.h"
#include "qa/world/collision/q3/model.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/session/mod_clients.h"

typedef qa_qvm_mod_provider P;

static bool text_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static bool actor_eq(qa_actor_id a, qa_actor_id b) { return qa_same_actor(a, b); }
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("QVM memory allocation failed");
  return next;
}
static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = xrealloc(NULL, length);
  memcpy(copy, text, length);
  return copy;
}
static bool safe_int(double value) {
  return isfinite(value) && trunc(value) == value && value >= -9007199254740991.0 &&
         value <= 9007199254740991.0;
}
static bool is_int(double value) { return isfinite(value) && trunc(value) == value; }

/* TS scalar(). */
static int32_t scalar_of(double value, const char *encoding) {
  if (!isfinite(value) ||
      (text_eq(encoding, "float32") && !isfinite((double)(float)value)) ||
      (text_eq(encoding, "int32") && (value < -2147483648.0 || value > 2147483647.0)))
    qa_range_error("Mod value exceeds %s", encoding == NULL ? "" : encoding);
  if (text_eq(encoding, "float32")) return (int32_t)qa_float32_to_bits(value);
  return (int32_t)trunc(value);
}
static int64_t field_size(const qa_qvm_mod_actor_field *field) {
  if (text_eq(field->binding, "private")) return (int64_t)field->byte_length;
  const char *b = field->binding;
  if (text_eq(b, "origin") || text_eq(b, "velocity") || text_eq(b, "angles") ||
      text_eq(b, "bounds-min") || text_eq(b, "bounds-max") || text_eq(b, "constant-vector"))
    return 12;
  return 4;
}
static bool shared_field(const qa_qvm_mod_actor_field *field) {
  const char *b = field->binding;
  return text_eq(b, "team") || text_eq(b, "score") || text_eq(b, "health") ||
         text_eq(b, "inventory") || text_eq(b, "origin") || text_eq(b, "velocity") ||
         text_eq(b, "angles") || text_eq(b, "bounds-min") || text_eq(b, "bounds-max");
}
static double seconds_of(qa_mod_host_services *services) {
  qa_source_time time = services->time(services->ctx);
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}
static bool client_record(const qa_qvm_mod_callback_declaration *declaration,
                          const qa_qvm_mod_actor_record *record) {
  if (declaration->clients == NULL) return false;
  for (size_t i = 0; i < declaration->clients->records.count; i++)
    if (text_eq(declaration->clients->records.values[i], record->id)) return true;
  return false;
}
static const qa_qvm_mod_actor_record *record_named(
    const qa_qvm_mod_callback_declaration *declaration, const char *id) {
  if (id == NULL) return NULL;
  for (size_t i = 0; i < declaration->actor_record_count; i++)
    if (text_eq(declaration->actor_records[i].id, id)) return &declaration->actor_records[i];
  return NULL;
}
static bool abi_eq(const char *a, const char *b) { return text_eq(a, b); }
static const char *artifact_abi(const qa_qvmy_artifact *artifact) {
  return artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
}
static const char *presentation_runtime(const qa_qvm_mod_callback_declaration *d) {
  if (d->presentation == NULL) return NULL;
  return d->presentation->is_scene ? "qvm-scene" : "qvm-player-events";
}

/* ---- validateQvmMod ---- */
typedef struct {
  const qa_qvmy_artifact *artifact;
  const qa_qvm_mod_callback_declaration *declaration;
  int64_t source_end;
} Validate;
static void vrange(const Validate *v, double address, double size) {
  if (!safe_int(address) || address < 0 || !safe_int(size) || size < 1 ||
      address + size > (double)v->source_end)
    qa_range_error("QVM mod declaration exceeds source data");
}
static bool avail_has(const char *const *names, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++) if (text_eq(names[i], name)) return true;
  return false;
}
static void check_value(const Validate *v, const qa_qvm_mod_value *value,
                        const char *const *available, size_t available_count) {
  const qa_qvm_mod_callback_declaration *d = v->declaration;
  if (text_eq(value->kind, "address")) {
    if (value->address_value != 0) vrange(v, value->address_value, 1);
    return;
  }
  if (text_eq(value->kind, "actor")) {
    if (record_named(d, value->record) == NULL ||
        !avail_has(available, available_count, value->input))
      qa_range_error("Invalid QVM actor argument");
    return;
  }
  if (text_eq(value->kind, "client")) {
    if (d->clients == NULL || !avail_has(available, available_count, value->input))
      qa_range_error("Invalid QVM client argument");
    return;
  }
  if (text_eq(value->kind, "time")) {
    if (!avail_has(available, available_count, value->time_input))
      qa_range_error("Unavailable QVM time input");
    return;
  }
  const qa_mod_callback_value *inner = &value->value;
  const char *kind;
  if (text_eq(inner->kind, "input")) {
    static const char *const actors[] = {"self", "other", "activator", "attacker", "inflictor"};
    static const char *const vectors[] = {"point", "direction", "normal", "view-angles"};
    kind = avail_has(actors, 5, inner->name) ? "actor"
           : avail_has(vectors, 4, inner->name) ? "vector"
           : text_eq(inner->name, "item") ? "string" : "float";
  } else kind = inner->kind;
  if (text_eq(inner->kind, "input") && !avail_has(available, available_count, inner->name))
    qa_range_error("Unavailable QVM callback input %s", inner->name == NULL ? "" : inner->name);
  const char *expected = (text_eq(value->kind, "int32") || text_eq(value->kind, "float32"))
                             ? "float" : value->kind;
  if (!text_eq(kind, expected))
    qa_range_error("QVM callback value has an incompatible representation");
  if (text_eq(inner->kind, "float") &&
      (text_eq(value->kind, "int32") || text_eq(value->kind, "float32")))
    (void)scalar_of(inner->number_value, value->kind);
}
static bool is_enter(const qa_qvmy_artifact *artifact, double entry) {
  if (!is_int(entry) || entry < 0 || entry >= (double)artifact->instruction_count) return false;
  return artifact->instructions[(size_t)entry].opcode == QA_QVM_OP_ENTER;
}
static void check_call(const Validate *v, const qa_qvm_mod_source_call *call,
                       const char *const *available, size_t available_count) {
  if (!is_enter(v->artifact, call->entry) ||
      call->argument_count > QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS)
    qa_range_error("QVM mod callback requires a source function entry and bounded OP_ARG arguments");
  for (size_t i = 0; i < call->argument_count; i++)
    check_value(v, &call->arguments[i], available, available_count);
  /* Overlap over global bytes (addresses are bounded by vrange). */
  for (size_t i = 0; i < call->global_count; i++) {
    const qa_qvm_mod_global_value *g = &call->globals[i];
    double size = text_eq(g->value.kind, "vector") ? 12 : 4;
    vrange(v, g->address, size);
    if (fmod(g->address, 4) != 0) qa_range_error("Unaligned QVM callback global");
    for (size_t j = 0; j < i; j++) {
      const qa_qvm_mod_global_value *o = &call->globals[j];
      double osize = text_eq(o->value.kind, "vector") ? 12 : 4;
      if (g->address < o->address + osize && o->address < g->address + size)
        qa_range_error("Overlapping QVM callback globals");
    }
    check_value(v, &g->value, available, available_count);
  }
}
typedef struct {
  const Validate *v;
  const qa_qvm_mod_clients *clients;
} OutputCtx;
static void output_field(OutputCtx *c, const void *field_opaque, int64_t length) {
  const qa_qvm_mod_record_offset *field = field_opaque;
  const qa_qvm_mod_actor_record *record =
      field == NULL ? NULL : record_named(c->v->declaration, field->record);
  bool ok = c->clients != NULL && record != NULL;
  if (ok) {
    bool listed = false;
    for (size_t i = 0; i < c->clients->records.count; i++)
      if (text_eq(c->clients->records.values[i], record->id)) listed = true;
    if (!listed && !text_eq(record->id, c->v->declaration->entity_record)) ok = false;
  }
  if (ok && (!is_int(field->offset) || field->offset < 0 || fmod(field->offset, 4) != 0)) ok = false;
  if (ok) {
    bool found = false;
    for (size_t i = 0; i < record->field_count && !found; i++) {
      const qa_qvm_mod_actor_field *f = &record->fields[i];
      if (text_eq(f->binding, "private") && f->offset <= field->offset &&
          field->offset + (double)length <= f->offset + f->byte_length)
        found = true;
      else if (length == 12 &&
               (text_eq(f->binding, "bounds-min") || text_eq(f->binding, "bounds-max")) &&
               f->offset == field->offset)
        found = true;
    }
    if (!found) ok = false;
  }
  if (!ok) qa_range_error("QVM client output requires declared private client storage");
}
static void out_scalar_check(void *ctx, const void *field) { output_field(ctx, field, 4); }
static void out_vector_check(void *ctx, const void *field) { output_field(ctx, field, 12); }

void qa_qvm_validate_mod(const qa_qvmy_artifact *artifact,
                         const qa_qvm_mod_callback_declaration *declaration) {
  if (!text_eq(artifact->module.digest, declaration->program.digest) ||
      !text_eq(artifact->module.artifact_path, declaration->program.path) ||
      !text_eq(artifact->role, "qagame") || !abi_eq(artifact_abi(artifact), declaration->abi_profile))
    qa_range_error("Gameplay mod differs from its declared QVM artifact or ABI");
  {
    qa_qvm_entity_tokens tokens;
    const char *spawn = declaration->has_spawn_entities && declaration->spawn_entities != NULL
                            ? declaration->spawn_entities : "";
    if (!qa_qvm_entity_tokens_init(&tokens, spawn, strlen(spawn)))
      qa_range_error("QVM memory allocation failed");
    qa_qvm_entity_tokens_destroy(&tokens);
  }
  Validate v = {artifact, declaration,
                artifact->data_length + artifact->literal_length + artifact->bss_length};
  const qa_qvm_mod_clients *clients = declaration->clients;
  /* Canonical shared-store keys. */
  const char **canonical = NULL;
  size_t canonical_count = 0, canonical_capacity = 0;
  char key_buffer[512];
  for (size_t r = 0; r < declaration->actor_record_count; r++) {
    const qa_qvm_mod_actor_record *record = &declaration->actor_records[r];
    bool duplicate = false;
    for (size_t q = 0; q < r; q++)
      if (text_eq(declaration->actor_records[q].id, record->id)) duplicate = true;
    if (duplicate || record->id == NULL || record->id[0] == '\0' || record->address == 0 ||
        fmod(record->address, 4) != 0 || !safe_int(record->stride) || record->stride < 4 ||
        fmod(record->stride, 4) != 0 || !safe_int(record->capacity) || record->capacity < 1 ||
        record->capacity > 1024)
      qa_range_error("Invalid QVM mod actor array");
    vrange(&v, record->address, record->stride * record->capacity);
    for (size_t q = 0; q < r; q++) {
      const qa_qvm_mod_actor_record *previous = &declaration->actor_records[q];
      if (record->address < previous->address + previous->stride * previous->capacity &&
          previous->address < record->address + record->stride * record->capacity)
        qa_range_error("Overlapping QVM mod actor arrays");
    }
    for (size_t f = 0; f < record->field_count; f++) {
      const qa_qvm_mod_actor_field *field = &record->fields[f];
      if (text_eq(field->binding, "team") || text_eq(field->binding, "score")) {
        qa_source_match_field match = {text_eq(field->binding, "score"),
                                       field->team_values, field->team_value_count};
        qa_validate_source_match_field(match);
      }
      int64_t size = field_size(field);
      if (!safe_int(field->offset) || field->offset < 0 || fmod(field->offset, 4) != 0 ||
          size < 1 || field->offset + (double)size > record->stride)
        qa_range_error("QVM mod actor field exceeds its source record");
      for (size_t q = 0; q < f; q++) {
        const qa_qvm_mod_actor_field *other = &record->fields[q];
        if (field->offset < other->offset + (double)field_size(other) &&
            other->offset < field->offset + (double)size)
          qa_range_error("Overlapping QVM mod actor fields");
      }
      if (field->access != NULL && !shared_field(field))
        qa_range_error("QVM projection direction requires a canonical field");
      if (shared_field(field) && !text_eq(field->access, "read-only")) {
        if (text_eq(field->binding, "inventory"))
          snprintf(key_buffer, sizeof key_buffer, "inventory:%s",
                   field->item == NULL ? "" : field->item);
        else
          snprintf(key_buffer, sizeof key_buffer, "%s", field->binding);
        for (size_t q = 0; q < canonical_count; q++)
          if (text_eq(canonical[q], key_buffer))
            qa_range_error("Multiple authoritative QVM mod stores for %s", key_buffer);
        if (canonical_count == canonical_capacity) {
          canonical_capacity = canonical_capacity == 0 ? 8 : canonical_capacity * 2;
          canonical = xrealloc(canonical, canonical_capacity * sizeof *canonical);
        }
        canonical[canonical_count++] = xstrdup(key_buffer);
      }
      if (text_eq(field->binding, "constant")) (void)scalar_of(field->constant_value, field->encoding);
      if (text_eq(field->binding, "constant-vector")) {
        (void)scalar_of(field->constant_vector.x, "float32");
        (void)scalar_of(field->constant_vector.y, "float32");
        (void)scalar_of(field->constant_vector.z, "float32");
      }
    }
  }
  for (size_t q = 0; q < canonical_count; q++) free((void *)canonical[q]);
  free(canonical);
  if (declaration->entity_record != NULL &&
      record_named(declaration, declaration->entity_record) == NULL)
    qa_range_error("Unknown QVM engine entity record");
  {
    OutputCtx ctx = {&v, clients};
    qa_validate_mod_client_outputs(clients == NULL ? NULL : clients->outputs,
                                   clients == NULL ? 0 : clients->output_count,
                                   out_scalar_check, out_vector_check, &ctx);
  }
  if (clients != NULL) {
    for (size_t i = 0; i < clients->output_count; i++) {
      const qa_mod_client_output_declaration *output = &clients->outputs[i];
      if (output->kind != QA_MOD_CLIENT_OUTPUT_BODY_SHAPE) continue;
      const qa_qvm_mod_record_offset *fields[2] = {output->min_vector, output->max_vector};
      const char *bindings[2] = {"bounds-min", "bounds-max"};
      for (size_t k = 0; k < 2; k++) {
        const qa_qvm_mod_actor_record *record =
            fields[k] == NULL ? NULL : record_named(declaration, fields[k]->record);
        bool found = false;
        if (record != NULL)
          for (size_t f = 0; f < record->field_count; f++)
            if (text_eq(record->fields[f].binding, bindings[k]) &&
                record->fields[f].offset == fields[k]->offset &&
                !text_eq(record->fields[f].access, "read-only"))
              found = true;
        if (!found) qa_range_error("Client body shape must name its writable source mins/maxs");
      }
    }
    const qa_qvm_mod_actor_record *state = record_named(declaration, clients->player_state_record);
    const qa_qvm_mod_actor_record *entity =
        declaration->entity_record == NULL ? NULL : record_named(declaration, declaration->entity_record);
    bool bad = !safe_int(clients->maximum) || clients->maximum < 1 || clients->maximum > 64 ||
               entity == NULL || entity->capacity < clients->maximum;
    if (!bad) {
      for (size_t i = 0; i < clients->records.count; i++) {
        if (text_eq(clients->records.values[i], entity->id)) bad = true;
        if (record_named(declaration, clients->records.values[i]) == NULL) bad = true;
        for (size_t j = 0; j < i; j++)
          if (text_eq(clients->records.values[j], clients->records.values[i])) bad = true;
      }
    }
    if (!bad && state == NULL) bad = true;
    if (!bad) {
      bool listed = false;
      for (size_t i = 0; i < clients->records.count; i++)
        if (text_eq(clients->records.values[i], state->id)) listed = true;
      if (!listed) bad = true;
      if (state->stride < qa_qvm_player_state_bytes(declaration->abi_profile)) bad = true;
      for (size_t i = 0; i < declaration->actor_record_count; i++)
        if (declaration->actor_records[i].capacity < clients->maximum) bad = true;
    }
    if (bad) qa_range_error("Invalid QVM source client record reservation");
  }
  for (size_t r = 0; r < declaration->actor_record_count; r++)
    for (size_t f = 0; f < declaration->actor_records[r].field_count; f++) {
      const qa_qvm_mod_actor_field *field = &declaration->actor_records[r].fields[f];
      if (text_eq(field->binding, "record") && record_named(declaration, field->record) == NULL)
        qa_range_error("Unknown linked QVM actor record");
    }
  const qa_qvm_mod_source_actors *lifecycle = declaration->source_actors;
  {
    size_t store_count = lifecycle == NULL ? 0 : lifecycle->initial_stores.count;
    int64_t *stores = xrealloc(NULL, (store_count == 0 ? 1 : store_count) * sizeof *stores);
    qa_qvm_actor_store *out = xrealloc(NULL, (store_count == 0 ? 1 : store_count) * sizeof *out);
    for (size_t i = 0; i < store_count; i++) stores[i] = (int64_t)lifecycle->initial_stores.values[i];
    qa_qvm_actor_bootstrap(stores, store_count, artifact->instructions,
                           artifact->instruction_count,
                           (int64_t)artifact->initialized_data_length, artifact->data_length,
                           artifact->bss_length, declaration->actor_records,
                           declaration->actor_record_count, out);
    free(stores);
    free(out);
  }
  if (lifecycle != NULL && lifecycle->frame != NULL) {
    const qa_qvm_mod_actor_record *record =
        declaration->entity_record == NULL ? NULL : record_named(declaration, declaration->entity_record);
    if (record == NULL || lifecycle->update != NULL)
      qa_range_error("QVM actor frame requires exclusive original lifecycle ownership");
    qa_qvm_validate_mod_actor_frame(lifecycle->frame, artifact->instructions,
                                    artifact->instruction_count,
                                    (int64_t)artifact->initialized_data_length,
                                    artifact->bss_length, record, (int64_t)lifecycle->inuse);
    static const char *const avail[] = {"time", "elapsed"};
    check_call(&v, &lifecycle->frame->call, avail, 2);
  }
  if (text_eq(presentation_runtime(declaration), "qvm-scene")) {
    const qa_qvm_mod_actor_record *record =
        declaration->entity_record == NULL ? NULL : record_named(declaration, declaration->entity_record);
    if (clients == NULL || record == NULL ||
        record->stride < qa_qvm_shared_entity_bytes(declaration->abi_profile))
      qa_range_error("QVM scene presentation requires admitted source clients and the original shared entity prefix");
  }
  if (lifecycle != NULL) {
    const qa_qvm_mod_actor_record *entity =
        declaration->entity_record == NULL ? NULL : record_named(declaration, declaration->entity_record);
    int64_t shared = qa_qvm_shared_entity_bytes(declaration->abi_profile);
    if (entity == NULL || entity->stride < (double)shared || lifecycle->inuse < (double)shared ||
        fmod(lifecycle->inuse, 4) != 0 || lifecycle->inuse + 4 > entity->stride ||
        !safe_int(lifecycle->event_entity_type) || lifecycle->event_entity_type < 0 ||
        lifecycle->release.argument >= QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS ||
        lifecycle->allocate == lifecycle->release.entry ||
        !is_enter(artifact, lifecycle->allocate) || !is_enter(artifact, lifecycle->release.entry))
      qa_range_error("Invalid QVM source actor lifecycle");
    if (lifecycle->update != NULL) {
      static const char *const avail[] = {"self", "time", "elapsed"};
      check_call(&v, lifecycle->update, avail, 3);
    }
  }
  for (size_t i = 0; i < declaration->objective_count; i++) {
    const qa_source_objective_declaration *objective = &declaration->objectives[i];
    const qa_qvm_mod_objective_scalar *storage = objective->state_storage;
    const qa_qvm_mod_objective_address *addresses[3] = {
        (const qa_qvm_mod_objective_address *)&storage->address, objective->carrier, objective->target};
    /* The scalar address shares the QvmModObjectiveAddress head
     * (is_global_pointer flag is storage->address_is_global_pointer);
     * validate through a normalized copy. */
    qa_qvm_mod_objective_address state_address = {storage->address_is_global_pointer,
                                                  storage->address, {0, 0, 0, {NULL, 0}, 0}};
    state_address.pointer.is_global = storage->address_is_global_pointer;
    state_address.pointer.address = storage->address;
    state_address.pointer.indirections = storage->indirections;
    state_address.pointer.offset = storage->offset;
    addresses[0] = &state_address;
    for (size_t k = 0; k < 3; k++)
      if (addresses[k] != NULL) qa_qvm_validate_objective_address(v.source_end, addresses[k]);
    if (objective->is_owned && objective->change != NULL) {
      static const char *const avail[] = {"self", "other", "activator", "amount", "time"};
      check_call(&v, objective->change, avail, 5);
    }
  }
  if (declaration->items != NULL)
    for (size_t i = 0; i < declaration->items->definition_count; i++) {
      const qa_qvm_mod_item_definition *item = &declaration->items->definitions[i];
      static const char *const avail[] = {"self", "time"};
      if (item->action_use != NULL) check_call(&v, item->action_use, avail, 2);
      if (item->action_drop != NULL) check_call(&v, item->action_drop, avail, 2);
    }
  {
    static const char *const avail[] = {"time"};
    for (size_t i = 0; i < declaration->initialize_count; i++)
      check_call(&v, &declaration->initialize[i], avail, 1);
  }
  if (clients != NULL) {
    static const char *const avail[] = {"self", "time"};
    for (size_t i = 0; i < clients->admit_count; i++) check_call(&v, &clients->admit[i], avail, 2);
    for (size_t i = 0; i < clients->userinfo_count; i++) check_call(&v, &clients->userinfo[i], avail, 2);
    for (size_t i = 0; i < clients->disconnect_count; i++) check_call(&v, &clients->disconnect[i], avail, 2);
    static const char *const favail[] = {"self", "time", "elapsed"};
    for (size_t i = 0; i < clients->frame_count; i++) check_call(&v, &clients->frame[i], favail, 3);
    for (size_t i = 0; i < clients->input_count; i++) {
      const qa_mod_client_input_binding *binding = &clients->input[i];
      static const char *const iavail[] = {"self", "time", "elapsed", "view-angles", "attack",
                                           "jump", "impulse", "forward-move", "side-move", "up-move"};
      const qa_qvm_mod_source_call *calls = binding->calls;
      for (size_t c = 0; c < binding->call_count; c++) check_call(&v, &calls[c], iavail, 10);
      if (text_eq(binding->phase, "before")) {
        const qa_qvm_mod_input_output *outputs = binding->outputs;
        for (size_t o = 0; o < binding->output_count; o++) {
          const qa_qvm_mod_input_output *output = &outputs[o];
          if (text_eq(output->kind, "field")) continue;
          if (!is_enter(artifact, output->entry))
            qa_range_error("QVM input handler requires an original function entry");
          if (output->actor_pointer.is_global) vrange(&v, output->actor_pointer.address, 4);
          if (text_eq(output->kind, "command") && output->command.is_global)
            vrange(&v, output->command.address, 4);
          if (text_eq(output->kind, "handler") && output->has_returns)
            (void)scalar_of(output->returns_value, output->returns_encoding);
        }
      }
    }
  }
  if (declaration->combat != NULL) {
    qa_qvm_mod_source_call call = {declaration->combat->fields.entry, NULL, 0,
                                   declaration->combat->fields.globals,
                                   declaration->combat->fields.global_count, "void"};
    static const char *const avail[] = {"time"};
    check_call(&v, &call, avail, 1);
  }
  qa_qvm_validate_mod_actors(artifact, declaration);
  qa_qvm_validate_mod_protection(declaration);
  if (declaration->items != NULL)
    qa_qvm_validate_mod_items(declaration->items, declaration, artifact->instructions,
                              artifact->instruction_count,
                              (int64_t)artifact->initialized_data_length, artifact->bss_length);
  {
    static const char *const avail[] = {"self", "attacker", "inflictor", "amount", "knockback",
                                        "damage-flags", "regular-protection-scale", "point",
                                        "direction", "normal", "time"};
    for (size_t i = 0; i < declaration->protection_count; i++)
      check_call(&v, &declaration->protection[i].absorb, avail, 11);
  }
  qa_qvm_validate_mod_pickups(declaration);
  for (size_t i = 0; i < declaration->pickup_count; i++) {
    const qa_qvm_mod_pickup *rule = &declaration->pickups[i];
    static const char *const avail[] = {"self", "other", "item", "time",
                                        "pickup-count", "pickup-has-count", "pickup-dropped"};
    for (size_t c = 0; c < rule->context_count; c++)
      check_value(&v, &rule->context[c].value, avail, 7);
    if (rule->rule.operation.is_gate_then_grant)
      check_call(&v, rule->rule.operation.gate, avail, 7);
    check_call(&v, rule->rule.operation.grant, avail, 7);
  }
  for (size_t i = 0; i < declaration->callback_count; i++) {
    const qa_qvm_mod_callback *call = &declaration->callbacks[i];
    bool duplicate = false;
    for (size_t j = 0; j < i; j++)
      if (text_eq(declaration->callbacks[j].binding.id, call->binding.id)) duplicate = true;
    if (duplicate || (!text_eq(call->binding.stage, "observe") && text_eq(call->call.returns, "void")))
      qa_range_error("Duplicate QVM callback or missing source return value");
    const char *avail[16];
    size_t count = 0;
    avail[count++] = "self";
    avail[count++] = "time";
    if (text_eq(call->binding.stage, "observe")) avail[count++] = "result";
    const char *op = call->binding.operation;
    static const char *const damage[] = {"attacker", "inflictor", "amount", "knockback",
                                         "direction", "point", "normal"};
    static const char *const inv[] = {"item", "amount"};
    static const char *const use[] = {"other", "activator"};
    static const char *const touch[] = {"other"};
    static const char *const think[] = {"elapsed"};
    static const char *const pain[] = {"attacker", "amount", "knockback"};
    static const char *const other[] = {"attacker", "inflictor", "amount", "knockback", "point"};
    const char *const *extra = other;
    size_t extra_count = 5;
    if (text_eq(op, "damage")) { extra = damage; extra_count = 7; }
    else if (text_eq(op, "inventory.give") || text_eq(op, "inventory.consume")) { extra = inv; extra_count = 2; }
    else if (text_eq(op, "actor.use")) { extra = use; extra_count = 2; }
    else if (text_eq(op, "actor.touch")) { extra = touch; extra_count = 1; }
    else if (text_eq(op, "actor.think")) { extra = think; extra_count = 1; }
    else if (text_eq(op, "actor.pain")) { extra = pain; extra_count = 3; }
    for (size_t k = 0; k < extra_count; k++) avail[count++] = extra[k];
    check_call(&v, &call->call, avail, count);
  }
}

/* ---- hostImage / validateQvmModCheckpoint ---- */
typedef struct {
  qa_save_value *decoded; /* owned tree backing borrowed values */
  struct { qa_saved_actor_id actor; int64_t slot; bool owned; } *projections;
  size_t projection_count;
  struct { qa_saved_actor_id actor; int64_t slot; bool admitted; } *client_slots;
  size_t client_slot_count;
  int64_t next_slot;
  struct { const char *id; const uint8_t *bytes; size_t length; } *defaults;
  size_t default_count;
  struct { int64_t index; const char *value; } *configstrings;
  size_t configstring_count;
  const qa_save_value *cvars;
  const qa_save_value *files;
} HostImage;

static void host_image_free(HostImage *image) {
  free(image->projections);
  free(image->client_slots);
  free(image->defaults);
  free(image->configstrings);
  qa_save_value_free(image->decoded);
  memset(image, 0, sizeof *image);
}
static void reader_field(const qa_save_reader *parent, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(parent, name, out)) qa_range_error("QVM memory allocation failed");
}
static void reader_item(const qa_save_reader *parent, size_t index, qa_save_reader *out) {
  if (!qa_save_reader_item(parent, index, out)) qa_range_error("QVM memory allocation failed");
}
static int32_t checkpoint_word(const qa_guest_checkpoint *checkpoint, int64_t offset) {
  if (offset < 0 || offset + 4 > (int64_t)checkpoint->data.length)
    qa_range_error("Offset is outside the bounds of the DataView");
  const uint8_t *b = checkpoint->data.bytes + offset;
  return (int32_t)((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 |
                   (uint32_t)b[3] << 24);
}

static void host_image(const qa_guest_checkpoint *checkpoint,
                       const qa_qvm_mod_callback_declaration *declaration,
                       HostImage *out) {
  memset(out, 0, sizeof *out);
  if (!text_eq(checkpoint->host_state.format, "qvm:mod-host-v1") ||
      checkpoint->random_count != 0 || checkpoint->callback_count != 0)
    qa_range_error("Invalid QVM mod host checkpoint");
  out->decoded = qa_decode_checkpoint_value(checkpoint->host_state.bytes.bytes,
                                            checkpoint->host_state.bytes.length);
  if (out->decoded == NULL) qa_range_error("QVM memory allocation failed");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, out->decoded, "mod-host"))
    qa_range_error("QVM memory allocation failed");
  qa_save_reader field;
  reader_field(&reader, "version", &field);
  (void)qa_save_reader_literal_number(&field, 1);
  qa_save_reader_destroy(&field);
  {
    qa_qvm_entity_tokens tokens;
    const char *spawn = declaration->has_spawn_entities && declaration->spawn_entities != NULL
                            ? declaration->spawn_entities : "";
    if (!qa_qvm_entity_tokens_init(&tokens, spawn, strlen(spawn)))
      qa_range_error("QVM memory allocation failed");
    reader_field(&reader, "entityTokens", &field);
    qa_qvm_entity_tokens_restore(&tokens, field.value);
    qa_save_reader_destroy(&field);
    qa_qvm_entity_tokens_destroy(&tokens);
  }
  int64_t capacity = 0;
  bool any_common = false;
  for (size_t i = 0; i < declaration->actor_record_count; i++) {
    const qa_qvm_mod_actor_record *record = &declaration->actor_records[i];
    if (client_record(declaration, record)) continue;
    if (!any_common || (int64_t)record->capacity < capacity) capacity = (int64_t)record->capacity;
    any_common = true;
  }
  reader_field(&reader, "nextSlot", &field);
  out->next_slot = (int64_t)qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  if (out->next_slot > capacity) qa_range_error("Invalid QVM projection allocation cursor");
  reader_field(&reader, "projections", &field);
  out->projection_count = qa_save_reader_list_count(&field);
  out->projections = xrealloc(NULL, (out->projection_count == 0 ? 1 : out->projection_count) *
                                        sizeof *out->projections);
  const qa_qvm_mod_actor_record *entity_record =
      declaration->entity_record == NULL ? NULL : record_named(declaration, declaration->entity_record);
  for (size_t i = 0; i < out->projection_count; i++) {
    qa_save_reader entry, sub;
    reader_item(&field, i, &entry);
    reader_field(&entry, "actor", &sub);
    qa_saved_actor_id actor = qa_read_saved_actor(&sub);
    qa_save_reader_destroy(&sub);
    reader_field(&entry, "slot", &sub);
    int64_t slot = (int64_t)qa_save_reader_integer_min(&sub, 0);
    qa_save_reader_destroy(&sub);
    reader_field(&entry, "owned", &sub);
    bool owned = qa_save_reader_boolean(&sub);
    qa_save_reader_destroy(&sub);
    bool bad = owned ? (declaration->source_actors == NULL || entity_record == NULL ||
                        slot >= (int64_t)entity_record->capacity)
                     : slot >= out->next_slot;
    for (size_t j = 0; j < i && !bad; j++)
      if (out->projections[j].slot == slot ||
          (out->projections[j].actor.slot == actor.slot &&
           out->projections[j].actor.generation == actor.generation))
        bad = true;
    if (bad) qa_range_error("Invalid saved QVM actor projection");
    reader_field(&entry, "event", &sub);
    if (!qa_save_reader_is_null(&sub)) (void)qa_save_reader_string(&sub);
    qa_save_reader_destroy(&sub);
    qa_save_reader_destroy(&entry);
    out->projections[i].actor = actor;
    out->projections[i].slot = slot;
    out->projections[i].owned = owned;
  }
  qa_save_reader_destroy(&field);
  reader_field(&reader, "clientSlots", &field);
  if (!qa_save_reader_is_undefined(&field)) {
    out->client_slot_count = qa_save_reader_list_count(&field);
    out->client_slots = xrealloc(NULL, (out->client_slot_count == 0 ? 1 : out->client_slot_count) *
                                           sizeof *out->client_slots);
    for (size_t i = 0; i < out->client_slot_count; i++) {
      qa_save_reader entry, sub;
      reader_item(&field, i, &entry);
      reader_field(&entry, "actor", &sub);
      out->client_slots[i].actor = qa_read_saved_actor(&sub);
      qa_save_reader_destroy(&sub);
      reader_field(&entry, "slot", &sub);
      out->client_slots[i].slot = (int64_t)qa_save_reader_integer_min(&sub, 0);
      qa_save_reader_destroy(&sub);
      reader_field(&entry, "admitted", &sub);
      out->client_slots[i].admitted = qa_save_reader_boolean(&sub);
      qa_save_reader_destroy(&sub);
      qa_save_reader_destroy(&entry);
    }
  }
  qa_save_reader_destroy(&field);
  int64_t reserved = declaration->clients == NULL ? 0 : (int64_t)declaration->clients->maximum;
  for (size_t i = 0; i < out->client_slot_count; i++) {
    bool bad = out->client_slots[i].slot >= reserved;
    for (size_t j = 0; j < i && !bad; j++)
      if (out->client_slots[j].slot == out->client_slots[i].slot) bad = true;
    bool matched = false;
    for (size_t j = 0; j < out->projection_count; j++)
      if (!out->projections[j].owned && out->projections[j].slot == out->client_slots[i].slot &&
          out->projections[j].actor.slot == out->client_slots[i].actor.slot &&
          out->projections[j].actor.generation == out->client_slots[i].actor.generation)
        matched = true;
    if (bad || !matched) qa_range_error("Invalid QVM saved source client mapping");
  }
  for (size_t i = 0; i < out->projection_count; i++)
    if (out->projections[i].slot < reserved) {
      bool occupied = false;
      for (size_t j = 0; j < out->client_slot_count; j++)
        if (out->client_slots[j].slot == out->projections[i].slot) occupied = true;
      if (!occupied) qa_range_error("QVM saved actor occupies a reserved client row");
    }
  reader_field(&reader, "playerEvents", &field);
  {
    qa_qvm_player_events_checkpoint events;
    if (qa_qvm_read_player_events(&field, &events)) {
      const qa_qvm_mod_actor_record *record =
          declaration->clients == NULL
              ? NULL : record_named(declaration, declaration->clients->player_state_record);
      if (events.client_count != out->client_slot_count)
        qa_range_error("Saved QVM player event cursors differ from source clients");
      for (size_t i = 0; i < events.client_count; i++) {
        const qa_qvm_saved_event_cursor *cursor = &events.clients[i];
        int64_t slot = -1;
        for (size_t j = 0; j < out->client_slot_count; j++)
          if (out->client_slots[j].actor.slot == cursor->actor.slot &&
              out->client_slots[j].actor.generation == cursor->actor.generation)
            slot = out->client_slots[j].slot;
        if (slot < 0 || record == NULL ||
            checkpoint_word(checkpoint, (int64_t)(record->address + (double)slot * record->stride) + 108) !=
                cursor->observed_sequence)
          qa_range_error("Saved QVM player event cursor differs from its source client");
      }
      qa_qvm_player_events_checkpoint_free(&events);
    }
  }
  qa_save_reader_destroy(&field);
  reader_field(&reader, "defaults", &field);
  out->default_count = qa_save_reader_list_count(&field);
  out->defaults = xrealloc(NULL, (out->default_count == 0 ? 1 : out->default_count) * sizeof *out->defaults);
  for (size_t i = 0; i < out->default_count; i++) {
    if (i >= declaration->actor_record_count) qa_range_error("Unexpected QVM actor template");
    const qa_qvm_mod_actor_record *record = &declaration->actor_records[i];
    qa_save_reader entry, sub;
    reader_item(&field, i, &entry);
    reader_field(&entry, "id", &sub);
    out->defaults[i].id = qa_save_reader_literal_string(&sub, record->id);
    qa_save_reader_destroy(&sub);
    reader_field(&entry, "bytes", &sub);
    out->defaults[i].bytes = qa_save_reader_bytes(&sub, &out->defaults[i].length);
    qa_save_reader_destroy(&sub);
    qa_save_reader_destroy(&entry);
    if (out->defaults[i].length != (size_t)(record->stride * record->capacity))
      qa_range_error("QVM actor template differs from its source layout");
  }
  qa_save_reader_destroy(&field);
  if (out->default_count != declaration->actor_record_count)
    qa_range_error("Missing QVM actor templates");
  reader_field(&reader, "configstrings", &field);
  out->configstring_count = qa_save_reader_list_count(&field);
  out->configstrings = xrealloc(NULL, (out->configstring_count == 0 ? 1 : out->configstring_count) *
                                          sizeof *out->configstrings);
  for (size_t i = 0; i < out->configstring_count; i++) {
    qa_save_reader entry, sub;
    reader_item(&field, i, &entry);
    reader_field(&entry, "index", &sub);
    out->configstrings[i].index = (int64_t)qa_save_reader_integer_min(&sub, 0);
    qa_save_reader_destroy(&sub);
    reader_field(&entry, "value", &sub);
    out->configstrings[i].value = qa_save_reader_string(&sub);
    qa_save_reader_destroy(&sub);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&field);
  for (size_t i = 0; i < out->configstring_count; i++) {
    if (out->configstrings[i].index >= 1024) qa_range_error("Invalid QVM configstrings");
    for (size_t j = 0; j < i; j++)
      if (out->configstrings[j].index == out->configstrings[i].index)
        qa_range_error("Invalid QVM configstrings");
  }
  reader_field(&reader, "cvars", &field);
  out->cvars = field.value;
  qa_save_reader_destroy(&field);
  reader_field(&reader, "files", &field);
  out->files = field.value;
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
}

void qa_qvm_validate_mod_checkpoint(const qa_qvmy_artifact *artifact,
                                    const qa_qvm_mod_callback_declaration *declaration,
                                    const qa_guest_checkpoint *checkpoint) {
  const qa_module_identity *module = &artifact->module;
  qa_q3_api_identity api = qa_qvm_api("qagame", declaration->abi_profile);
  const char *checkpoint_abi = checkpoint->has_abi_profile ? checkpoint->abi_profile : "q3-modern";
  if (!text_eq(checkpoint->module.id, module->id) ||
      !text_eq(checkpoint->module.digest, module->digest) ||
      !text_eq(checkpoint->module.artifact_path, module->artifact_path) ||
      !text_eq(checkpoint->module.revision, module->revision) ||
      (int64_t)checkpoint->data.length != artifact->allocated_data_length ||
      checkpoint->q3_api.kind != api.kind || checkpoint->q3_api.version != api.version ||
      !abi_eq(checkpoint_abi, declaration->abi_profile) ||
      checkpoint->instruction_index != 0 || checkpoint->operand_stack.count != 0 ||
      checkpoint->program_stack != (double)checkpoint->data.length ||
      !text_eq(checkpoint->host_state.module.id, module->id) ||
      !text_eq(checkpoint->host_state.module.digest, module->digest) ||
      !text_eq(checkpoint->host_state.module.revision, module->revision) ||
      !text_eq(checkpoint->host_state.module.artifact_path, module->artifact_path))
    qa_range_error("Incompatible QVM gameplay mod checkpoint");
  HostImage image;
  host_image(checkpoint, declaration, &image);
  host_image_free(&image);
}

/* ---- provider core ---- */
typedef struct {
  qa_actor_id actor;
  int64_t address;
  qa_qvm_mod_actor_field field;
  uint8_t bytes[12];
  int64_t size;
} Observation;
typedef enum { PENDING_MATCH, PENDING_HEALTH, PENDING_INVENTORY, PENDING_VECTOR } PendingKind;
typedef struct {
  PendingKind kind;
  qa_actor_id actor;
  int64_t address;
  qa_qvm_mod_actor_field field;
  double value;
  qa_vec3 vector;
  bool has_execution;
  qa_original_pickup_execution execution;
} Pending;
typedef struct {
  Observation *observations;
  size_t observation_count, observation_capacity;
  Pending *pending;
  size_t pending_count, pending_capacity, cursor;
} Frame;
typedef struct {
  qa_actor_id actor;
  qa_original_pickup_execution execution;
  Frame *frame; /* NULL until begin() adopts it */
} PickupScope;
typedef struct {
  qa_actor_id actor;
  Frame *frame;
} WeaponInput;
typedef struct {
  P *provider;
  qa_actor_id actor;
  qa_owned_actor owned;
  const qa_qvm_mod_actor_record *record;
  int64_t slot;
  const qa_qvm_mod_actor_field *team;
  const qa_qvm_mod_actor_field *score;
} MatchCell;
typedef struct {
  bool used;
  qa_protection_reservation reservation;
} ReservationCell;
typedef struct {
  bool used;
  qa_owned_actor actor;
  int64_t id;
} PickupBindingCell;
typedef struct {
  int64_t sequence;
  bool has_recipient;
  qa_actor_id recipient;
  char *text;
} SceneCommand;

static qa_qvm_memory *mem(P *p) { return p->module.memory; }
static void view_of(P *p, int64_t address, int64_t size, qa_qvm_writable_view *out) {
  qa_qvm_memory_data_view(mem(p), address, size, out);
}
static int32_t read_word(P *p, int64_t address) {
  qa_qvm_writable_view view;
  view_of(p, address, 4, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}
static void write_word(P *p, int64_t address, int32_t value) {
  qa_qvm_writable_view view;
  view_of(p, address, 4, &view);
  qa_qvm_writable_view_set_int32(&view, 0, value, true);
}
static double read_scalar_at(P *p, int64_t address, const char *encoding) {
  qa_qvm_writable_view view;
  view_of(p, address, 4, &view);
  return text_eq(encoding, "int32") ? (double)qa_qvm_writable_view_get_int32(&view, 0, true)
                                    : (double)qa_qvm_writable_view_get_float32(&view, 0, true);
}
static qa_vec3 read_vector(P *p, int64_t address) {
  qa_qvm_writable_view view;
  view_of(p, address, 12, &view);
  qa_vec3 result = {(double)qa_qvm_writable_view_get_float32(&view, 0, true),
                    (double)qa_qvm_writable_view_get_float32(&view, 4, true),
                    (double)qa_qvm_writable_view_get_float32(&view, 8, true)};
  return result;
}
static void write_vector(P *p, int64_t address, qa_vec3 value) {
  qa_qvm_writable_view view;
  view_of(p, address, 12, &view);
  double components[3] = {value.x, value.y, value.z};
  for (size_t i = 0; i < 3; i++)
    qa_qvm_writable_view_set_int32(&view, (int64_t)i * 4, scalar_of(components[i], "float32"), true);
}
static void current(P *p) {
  if (p->assert_current != NULL) p->assert_current(p->assert_current_ctx);
  if (p->closed) qa_range_error("QVM gameplay mod is closed");
}
static bool is_live(P *p, qa_actor_id actor) {
  return qa_session_actor_registry_is_live(p->services->actors, actor);
}
static bool resolve_owned(P *p, qa_actor_id actor, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_owned(p->services->actors, actor, out);
}
static bool projection_slot(P *p, qa_actor_id actor, int64_t *out) {
  for (size_t i = 0; i < p->projection_count; i++)
    if (actor_eq(p->projections[i].actor, actor)) { *out = p->projections[i].slot; return true; }
  return false;
}
static bool projection_has(P *p, qa_actor_id actor) {
  int64_t slot;
  return projection_slot(p, actor, &slot);
}
static void projection_set(P *p, qa_actor_id actor, int64_t slot) {
  for (size_t i = 0; i < p->projection_count; i++)
    if (actor_eq(p->projections[i].actor, actor)) { p->projections[i].slot = slot; return; }
  if (p->projection_count == p->projection_capacity) {
    p->projection_capacity = p->projection_capacity == 0 ? 8 : p->projection_capacity * 2;
    p->projections = xrealloc(p->projections, p->projection_capacity * sizeof *p->projections);
  }
  p->projections[p->projection_count].actor = actor;
  p->projections[p->projection_count].slot = slot;
  p->projection_count++;
}
static void projection_delete(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->projection_count; i++)
    if (actor_eq(p->projections[i].actor, actor)) {
      memmove(&p->projections[i], &p->projections[i + 1],
              (p->projection_count - i - 1) * sizeof *p->projections);
      p->projection_count--;
      return;
    }
}
static bool actor_at(P *p, int64_t slot, qa_actor_id *out) {
  for (size_t i = 0; i < p->projection_count; i++)
    if (p->projections[i].slot == slot) { *out = p->projections[i].actor; return true; }
  return false;
}
static bool owned_has(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->owned_count; i++)
    if (actor_eq(p->owned[i].id, actor)) return true;
  return false;
}
static bool owned_get(P *p, qa_actor_id actor, qa_owned_actor *out) {
  for (size_t i = 0; i < p->owned_count; i++)
    if (actor_eq(p->owned[i].id, actor)) { *out = p->owned[i].actor; return true; }
  return false;
}
static void owned_set(P *p, qa_owned_actor actor) {
  for (size_t i = 0; i < p->owned_count; i++)
    if (actor_eq(p->owned[i].id, actor.id)) { p->owned[i].actor = actor; return; }
  if (p->owned_count == p->owned_capacity) {
    p->owned_capacity = p->owned_capacity == 0 ? 8 : p->owned_capacity * 2;
    p->owned = xrealloc(p->owned, p->owned_capacity * sizeof *p->owned);
  }
  p->owned[p->owned_count].id = actor.id;
  p->owned[p->owned_count].actor = actor;
  p->owned_count++;
}
static void owned_delete(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->owned_count; i++)
    if (actor_eq(p->owned[i].id, actor)) {
      memmove(&p->owned[i], &p->owned[i + 1], (p->owned_count - i - 1) * sizeof *p->owned);
      p->owned_count--;
      return;
    }
}
static bool retired_has(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->retired_count; i++)
    if (actor_eq(p->retired[i], actor)) return true;
  return false;
}
static void retired_add(P *p, qa_actor_id actor) {
  if (retired_has(p, actor)) return;
  if (p->retired_count == p->retired_capacity) {
    p->retired_capacity = p->retired_capacity == 0 ? 8 : p->retired_capacity * 2;
    p->retired = xrealloc(p->retired, p->retired_capacity * sizeof *p->retired);
  }
  p->retired[p->retired_count++] = actor;
}
static bool removing_has(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->removing_count; i++)
    if (actor_eq(p->removing[i], actor)) return true;
  return false;
}
static void removing_add(P *p, qa_actor_id actor) {
  if (p->removing_count == p->removing_capacity) {
    p->removing_capacity = p->removing_capacity == 0 ? 8 : p->removing_capacity * 2;
    p->removing = xrealloc(p->removing, p->removing_capacity * sizeof *p->removing);
  }
  p->removing[p->removing_count++] = actor;
}
static void removing_delete(P *p, qa_actor_id actor) {
  for (size_t i = p->removing_count; i-- > 0;)
    if (actor_eq(p->removing[i], actor)) { p->removing[i] = p->removing[--p->removing_count]; return; }
}
static void event_key_delete(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->event_key_count; i++)
    if (actor_eq(p->event_keys[i].actor, actor)) {
      free(p->event_keys[i].key);
      memmove(&p->event_keys[i], &p->event_keys[i + 1],
              (p->event_key_count - i - 1) * sizeof *p->event_keys);
      p->event_key_count--;
      return;
    }
}
static const char *event_key_get(P *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->event_key_count; i++)
    if (actor_eq(p->event_keys[i].actor, actor)) return p->event_keys[i].key;
  return NULL;
}
static void event_key_set(P *p, qa_actor_id actor, const char *key) {
  event_key_delete(p, actor);
  if (p->event_key_count == p->event_key_capacity) {
    p->event_key_capacity = p->event_key_capacity == 0 ? 8 : p->event_key_capacity * 2;
    p->event_keys = xrealloc(p->event_keys, p->event_key_capacity * sizeof *p->event_keys);
  }
  p->event_keys[p->event_key_count].actor = actor;
  p->event_keys[p->event_key_count].key = xstrdup(key);
  p->event_key_count++;
}
static const uint8_t *defaults_of(P *p, const char *id, size_t *out_length) {
  for (size_t i = 0; i < p->default_count; i++)
    if (text_eq(p->defaults[i].id, id)) {
      if (out_length != NULL) *out_length = p->defaults[i].length;
      return p->defaults[i].bytes;
    }
  return NULL;
}
static Frame *frames(P *p) { return p->frames; }
static Frame *top_frame(P *p) {
  return p->frame_count == 0 ? NULL : &frames(p)[p->frame_count - 1];
}
/* Pickup scopes are heap objects on a pointer stack: executions and
 * write observers hold scope pointers across nested pickup observes. */
static PickupScope **pickup_scopes(P *p) { return (PickupScope **)p->pickup_scopes; }
static PickupScope *pickup_scope(P *p) {
  if (p->pickup_scope_count == 0) return NULL;
  PickupScope *scope = pickup_scopes(p)[p->pickup_scope_count - 1];
  return scope->frame == top_frame(p) ? scope : NULL;
}
static WeaponInput *weapon_inputs(P *p) { return p->weapon_inputs; }

static bool client_has(P *p, qa_actor_id actor) {
  return p->has_client_bindings && qa_qvm_mod_client_bindings_has(&p->client_bindings, actor);
}
static bool actor_record_applies(P *p, qa_actor_id actor, const qa_qvm_mod_actor_record *record) {
  return !client_record(p->declaration, record) || client_has(p, actor);
}
static const qa_qvm_mod_actor_record *entity_record_of(P *p) {
  const qa_qvm_mod_actor_record *record =
      p->declaration->entity_record == NULL
          ? NULL : record_named(p->declaration, p->declaration->entity_record);
  if (record == NULL ||
      record->stride < qa_qvm_shared_entity_bytes(p->declaration->abi_profile))
    qa_range_error("QVM engine service requires its declared sharedEntity_t array");
  return record;
}
static int64_t entity_address(P *p, int64_t slot) {
  const qa_qvm_mod_actor_record *record = entity_record_of(p);
  if (slot < 0 || slot >= (int64_t)record->capacity)
    qa_range_error("QVM entity exceeds its declared source array");
  return (int64_t)record->address + slot * (int64_t)record->stride;
}
static void entity_of(P *p, int64_t slot, qa_qvm_shared_entity *out) {
  qa_qvm_writable_view view;
  view_of(p, entity_address(p, slot), qa_qvm_shared_entity_bytes(p->declaration->abi_profile), &view);
  qa_qvm_borrow_shared_entity(&view, p->declaration->abi_profile, out);
}
static int64_t pointer_slot(P *p, int64_t pointer) {
  const qa_qvm_mod_actor_record *record = entity_record_of(p);
  int64_t offset = pointer - (int64_t)record->address;
  /* TS divides in doubles; a non-multiple yields a fractional slot
   * that entityAddress then rejects as non-integer. */
  if ((int64_t)record->stride == 0 || offset % (int64_t)record->stride != 0)
    qa_range_error("QVM entity exceeds its declared source array");
  int64_t slot = offset / (int64_t)record->stride;
  (void)entity_address(p, slot);
  return slot;
}
static int64_t pointer_for(P *p, const qa_actor_id *actor, const char *record_id);
static void release_projection(P *p, qa_actor_id actor);
static void finish_retired(P *p);
static void client_outputs_release(P *p, qa_actor_id actor) {
  if (p->client_outputs != NULL) qa_source_mod_client_outputs_release(p->client_outputs, actor);
}
static int64_t pointer_for(P *p, const qa_actor_id *actor, const char *record_id) {
  if (actor == NULL) return 0;
  if (!is_live(p, *actor)) qa_range_error("QVM mod cannot project a stale actor");
  const qa_qvm_mod_actor_record *record = record_named(p->declaration, record_id);
  if (record == NULL) qa_range_error("Missing validated QVM actor record");
  int64_t slot;
  if (!projection_slot(p, *actor, &slot)) {
    int64_t client = -1;
    bool has_client = p->has_client_bindings &&
                      qa_qvm_mod_client_bindings_slot(&p->client_bindings, *actor, &client);
    int64_t next = has_client ? client
                              : (p->declaration->clients == NULL
                                     ? 0 : (int64_t)p->declaration->clients->maximum);
    if (!has_client) {
      for (;;) {
        bool used = false;
        for (size_t i = 0; i < p->projection_count; i++)
          if (p->projections[i].slot == next) used = true;
        if (!used) break;
        next++;
      }
    } else {
      for (size_t i = 0; i < p->projection_count; i++)
        if (p->projections[i].slot == next)
          qa_range_error("QVM source client row is already occupied");
    }
    for (size_t i = 0; i < p->declaration->actor_record_count; i++) {
      const qa_qvm_mod_actor_record *candidate = &p->declaration->actor_records[i];
      if (actor_record_applies(p, *actor, candidate) && next >= (int64_t)candidate->capacity)
        qa_range_error("QVM mod actor projection capacity exceeded");
    }
    slot = next;
    if (next + 1 > p->next_slot) p->next_slot = next + 1;
    projection_set(p, *actor, slot);
    for (size_t i = 0; i < p->declaration->actor_record_count; i++) {
      const qa_qvm_mod_actor_record *candidate = &p->declaration->actor_records[i];
      if (!actor_record_applies(p, *actor, candidate)) continue;
      int64_t address = (int64_t)candidate->address + slot * (int64_t)candidate->stride;
      size_t length = 0;
      const uint8_t *defaults = defaults_of(p, candidate->id, &length);
      if (defaults == NULL) qa_range_error("Missing source actor defaults");
      qa_qvm_memory_write_bytes(mem(p), address,
                                defaults + slot * (int64_t)candidate->stride,
                                (size_t)candidate->stride);
      for (size_t f = 0; f < candidate->field_count; f++) {
        const qa_qvm_mod_actor_field *field = &candidate->fields[f];
        if (text_eq(field->binding, "constant"))
          write_word(p, address + (int64_t)field->offset,
                     scalar_of(field->constant_value, field->encoding));
        else if (text_eq(field->binding, "constant-vector"))
          write_vector(p, address + (int64_t)field->offset, field->constant_vector);
        else if (text_eq(field->binding, "record")) {
          bool is_client_field = false;
          if (p->declaration->clients != NULL)
            for (size_t c = 0; c < p->declaration->clients->records.count; c++)
              if (text_eq(p->declaration->clients->records.values[c], field->record))
                is_client_field = true;
          write_word(p, address + (int64_t)field->offset,
                     is_client_field && !client_has(p, *actor)
                         ? 0 : (int32_t)pointer_for(p, actor, field->record));
        }
      }
    }
    if (p->declaration->source_actors != NULL) {
      write_word(p, entity_address(p, slot) + (int64_t)p->declaration->source_actors->inuse, 1);
      /* entity.s.number = slot (entityState_t is the shared
       * entity prefix and number is its first word). */
      qa_qvm_writable_view view;
      view_of(p, entity_address(p, slot), 4, &view);
      qa_qvm_writable_view_set_int32(&view, 0, (int32_t)slot, true);
    }
  }
  if (slot >= (int64_t)record->capacity ||
      (client_record(p->declaration, record) && !client_has(p, *actor)))
    qa_range_error("Source actor has no declared auxiliary record");
  return (int64_t)record->address + slot * (int64_t)record->stride;
}
static int64_t player_address(P *p, qa_actor_id actor) {
  if (p->declaration->clients == NULL) qa_range_error("Missing QVM client player state");
  return pointer_for(p, &actor, p->declaration->clients->player_state_record);
}
static void release_projection(P *p, qa_actor_id actor) {
  client_outputs_release(p, actor);
  if (p->has_player_events) qa_qvm_mod_player_events_release(&p->player_events, actor);
  qa_qvm_mod_pickups_release(&p->pickups, actor);
  for (size_t i = 0; i < p->protection_count; i++)
    qa_qvm_mod_protection_release(p->protection_channels[i], actor);
  int64_t slot;
  if (!projection_slot(p, actor, &slot)) return;
  if (p->frame_count != 0 || p->pickup_contexts != 0) { retired_add(p, actor); return; }
  for (size_t i = 0; i < p->declaration->actor_record_count; i++) {
    const qa_qvm_mod_actor_record *record = &p->declaration->actor_records[i];
    if (slot < (int64_t)record->capacity) {
      const uint8_t *defaults = defaults_of(p, record->id, NULL);
      if (defaults == NULL) qa_range_error("Missing QVM source client defaults");
      qa_qvm_memory_write_bytes(mem(p), (int64_t)record->address + slot * (int64_t)record->stride,
                                defaults + slot * (int64_t)record->stride, (size_t)record->stride);
    }
  }
  projection_delete(p, actor);
  event_key_delete(p, actor);
}
static void finish_retired(P *p) {
  if (p->frame_count != 0 || p->pickup_contexts != 0) return;
  while (p->retired_count != 0) {
    qa_actor_id actor = p->retired[0];
    memmove(&p->retired[0], &p->retired[1], (p->retired_count - 1) * sizeof *p->retired);
    p->retired_count--;
    release_projection(p, actor);
  }
}

static int64_t objective_address_of(P *p, const qa_qvm_mod_objective_address *address);
static void objective_current(void *ctx) { current(ctx); }
static int64_t objective_address_of(P *p, const qa_qvm_mod_objective_address *address) {
  int64_t end = p->artifact->data_length + p->artifact->literal_length + p->artifact->bss_length;
  return qa_qvm_resolve_objective_address(mem(p), end, address, objective_current, p);
}
static qa_source_match_field match_field_of(const qa_qvm_mod_actor_field *field) {
  qa_source_match_field match = {text_eq(field->binding, "score"), field->team_values,
                                  field->team_value_count};
  return match;
}
static bool body_output_field(P *p, qa_actor_id actor, const char *record,
                              const qa_qvm_mod_actor_field *field) {
  if (!p->has_client_bindings || !qa_qvm_mod_client_bindings_admitted(&p->client_bindings, actor) ||
      p->declaration->clients == NULL)
    return false;
  for (size_t i = 0; i < p->declaration->clients->output_count; i++) {
    const qa_mod_client_output_declaration *output = &p->declaration->clients->outputs[i];
    if (output->kind != QA_MOD_CLIENT_OUTPUT_BODY_SHAPE) continue;
    const qa_qvm_mod_record_offset *min = output->min_vector;
    const qa_qvm_mod_record_offset *max = output->max_vector;
    if ((min != NULL && text_eq(min->record, record) && min->offset == field->offset) ||
        (max != NULL && text_eq(max->record, record) && max->offset == field->offset))
      return true;
  }
  return false;
}
static void observe_into(P *p, Frame *frame) {
  frame->observation_count = 0;
  for (size_t i = 0; i < p->projection_count; i++) {
    qa_actor_id actor = p->projections[i].actor;
    int64_t slot = p->projections[i].slot;
    if (owned_has(p, actor) || retired_has(p, actor)) continue;
    for (size_t r = 0; r < p->declaration->actor_record_count; r++) {
      const qa_qvm_mod_actor_record *record = &p->declaration->actor_records[r];
      if (!actor_record_applies(p, actor, record)) continue;
      for (size_t f = 0; f < record->field_count; f++) {
        const qa_qvm_mod_actor_field *field = &record->fields[f];
        if (!shared_field(field) || text_eq(field->access, "read-only") ||
            body_output_field(p, actor, record->id, field))
          continue;
        if (frame->observation_count == frame->observation_capacity) {
          frame->observation_capacity =
              frame->observation_capacity == 0 ? 16 : frame->observation_capacity * 2;
          frame->observations =
              xrealloc(frame->observations, frame->observation_capacity * sizeof *frame->observations);
        }
        Observation *entry = &frame->observations[frame->observation_count++];
        entry->actor = actor;
        entry->address = (int64_t)record->address + slot * (int64_t)record->stride +
                         (int64_t)field->offset;
        entry->field = *field;
        entry->size = field_size(field);
        memcpy(entry->bytes, mem(p)->bytes + entry->address, (size_t)entry->size);
      }
    }
  }
}
static void refresh(P *p) {
  if (p->objectives != NULL) qa_mod_source_objectives_refresh(p->objectives);
  p->projection_writes++;
  for (size_t i = 0; i < p->projection_count; i++) {
    qa_actor_id actor = p->projections[i].actor;
    int64_t slot = p->projections[i].slot;
    if (owned_has(p, actor) || retired_has(p, actor)) continue;
    for (size_t r = 0; r < p->declaration->actor_record_count; r++) {
      const qa_qvm_mod_actor_record *record = &p->declaration->actor_records[r];
      if (!actor_record_applies(p, actor, record)) continue;
      for (size_t f = 0; f < record->field_count; f++) {
        const qa_qvm_mod_actor_field *field = &record->fields[f];
        if (!shared_field(field) || body_output_field(p, actor, record->id, field)) continue;
        int64_t address = (int64_t)record->address + slot * (int64_t)record->stride +
                          (int64_t)field->offset;
        if (text_eq(field->binding, "team") || text_eq(field->binding, "score")) {
          write_word(p, address,
                     scalar_of(qa_read_source_match_field(p->services->match, actor,
                                                          match_field_of(field)),
                               field->encoding));
        } else if (text_eq(field->binding, "health")) {
          qa_combat_state_result state = qa_gameplay_authority_read(p->services->combat, actor);
          write_word(p, address, scalar_of(state.has_value ? state.value.health : 0, field->encoding));
        } else if (text_eq(field->binding, "inventory")) {
          size_t count = 0;
          const qa_inventory_entry *entries =
              qa_shared_inventory_table_entries(p->services->inventory, actor, &count);
          double amount = 0;
          for (size_t e = 0; e < count; e++)
            if (text_eq(entries[e].item, field->item)) amount = entries[e].count;
          write_word(p, address, scalar_of(amount, field->encoding));
        } else {
          qa_body_state_result state = qa_shared_body_table_read(p->services->bodies, actor);
          qa_vec3 zero = {0, 0, 0};
          qa_vec3 value = zero;
          if (state.has_value)
            value = text_eq(field->binding, "bounds-min") ? state.value.bounds.min
                    : text_eq(field->binding, "bounds-max") ? state.value.bounds.max
                    : text_eq(field->binding, "origin") ? state.value.origin
                    : text_eq(field->binding, "velocity") ? state.value.velocity
                    : state.value.angles;
          write_vector(p, address, value);
        }
      }
    }
  }
  for (size_t i = 0; i < p->frame_count; i++) observe_into(p, &frames(p)[i]);
  p->projection_writes--;
}
static bool execution_current(const qa_original_pickup_execution *execution) {
  return execution->current(execution->current_ctx);
}
static void validate_pickup_writes(P *p, const Observation *changes, size_t count) {
  PickupScope *scope = pickup_scope(p);
  if (scope == NULL || count == 0 || p->projection_writes != 0) return;
  if (!execution_current(&scope->execution))
    qa_range_error("Original pickup resource binding is no longer current");
  qa_owned_actor owner;
  if (!resolve_owned(p, scope->actor, &owner))
    qa_range_error("Original pickup recipient was retired");
  for (size_t i = 0; i < count; i++) {
    bool ok = actor_eq(changes[i].actor, scope->actor) &&
              text_eq(changes[i].field.binding, "inventory");
    if (ok) {
      ok = false;
      for (size_t w = 0; w < scope->execution.writes.count && !ok; w++) {
        const qa_pickup_write *write = &scope->execution.writes.values[w];
        if (write->kind == QA_PICKUP_RESOURCE_INVENTORY) {
          if (text_eq(write->item, changes[i].field.item) && !text_eq(write->fields, "capacity"))
            ok = true;
        } else {
          size_t item_count = 0;
          const qa_item_id *items = qa_gameplay_authority_protection_inventory_items(
              p->services->combat, owner, write->channel, &item_count);
          for (size_t k = 0; k < item_count; k++)
            if (text_eq(items[k], changes[i].field.item)) ok = true;
        }
      }
    }
    if (!ok) qa_range_error("Original pickup changed an undeclared resource");
  }
}
static void flush(P *p);
typedef struct {
  P *provider;
  int64_t address;
  const char *encoding;
} InventoryCommit;
static void inventory_committed(void *ctx, qa_inventory_committed_change change) {
  InventoryCommit *commit = ctx;
  P *p = commit->provider;
  p->projection_writes++;
  write_word(p, commit->address, scalar_of(change.after.count, commit->encoding));
  p->projection_writes--;
  for (size_t i = 0; i < p->frame_count; i++) observe_into(p, &frames(p)[i]);
  flush(p);
}
static void run_pending(P *p, const Pending *pending) {
  if (pending->has_execution && !execution_current(&pending->execution))
    qa_range_error("Original pickup resource binding is no longer current");
  qa_owned_actor owner;
  if (!resolve_owned(p, pending->actor, &owner))
    qa_range_error("QVM mod wrote an expired actor");
  switch (pending->kind) {
    case PENDING_MATCH: {
      qa_write_source_match_field(p->services->match, pending->actor,
                                  match_field_of(&pending->field), pending->value);
      if (!retired_has(p, pending->actor) && is_live(p, pending->actor)) {
        p->projection_writes++;
        write_word(p, pending->address,
                   scalar_of(qa_read_source_match_field(p->services->match, pending->actor,
                                                        match_field_of(&pending->field)),
                             pending->field.encoding));
        p->projection_writes--;
      }
      break;
    }
    case PENDING_HEALTH:
      qa_gameplay_authority_set_health(p->services->combat, owner, pending->value);
      break;
    case PENDING_INVENTORY: {
      size_t count = 0;
      const qa_inventory_entry *entries =
          qa_shared_inventory_table_entries(p->services->inventory, pending->actor, &count);
      bool found = false;
      qa_inventory_entry entry = {0};
      for (size_t e = 0; e < count; e++)
        if (text_eq(entries[e].item, pending->field.item)) { entry = entries[e]; found = true; }
      if (!found) qa_range_error("QVM mod inventory binding disappeared");
      entry.count = pending->value;
      if (pending->has_execution) {
        InventoryCommit commit = {p, pending->address, pending->field.encoding};
        qa_shared_inventory_table_configure(p->services->inventory, owner, entry,
                                            inventory_committed, &commit);
        if (!execution_current(&pending->execution))
          qa_range_error("Original pickup resource binding is no longer current");
        refresh(p);
      } else {
        qa_shared_inventory_table_configure(p->services->inventory, owner, entry, NULL, NULL);
      }
      break;
    }
    case PENDING_VECTOR: {
      qa_body_state_result state = qa_shared_body_table_read(p->services->bodies, pending->actor);
      if (!state.has_value) qa_range_error("QVM mod body binding disappeared");
      qa_body_state next = state.value;
      if (text_eq(pending->field.binding, "bounds-min")) next.bounds.min = pending->vector;
      else if (text_eq(pending->field.binding, "bounds-max")) next.bounds.max = pending->vector;
      else if (text_eq(pending->field.binding, "origin")) next.origin = pending->vector;
      else if (text_eq(pending->field.binding, "velocity")) next.velocity = pending->vector;
      else next.angles = pending->vector;
      qa_shared_body_table_write(p->services->bodies, owner, next);
      break;
    }
  }
}
static void capture_writes(P *p) {
  Frame *frame = top_frame(p);
  if (frame == NULL) return;
  WeaponInput *weapon = p->weapon_input_count == 0 ? NULL
                                                    : &weapon_inputs(p)[p->weapon_input_count - 1];
  size_t change_count = 0, change_capacity = 0;
  Observation *changes = NULL;
  for (size_t i = 0; i < frame->observation_count; i++) {
    Observation *entry = &frame->observations[i];
    if (weapon != NULL && weapon->frame == frame && actor_eq(weapon->actor, entry->actor) &&
        !text_eq(entry->field.binding, "health") && !text_eq(entry->field.binding, "inventory"))
      continue;
    if (memcmp(entry->bytes, mem(p)->bytes + entry->address, (size_t)entry->size) == 0) continue;
    if (change_count == change_capacity) {
      change_capacity = change_capacity == 0 ? 8 : change_capacity * 2;
      changes = xrealloc(changes, change_capacity * sizeof *changes);
    }
    changes[change_count++] = *entry;
  }
  validate_pickup_writes(p, changes, change_count);
  for (size_t i = 0; i < p->frame_count; i++) observe_into(p, &frames(p)[i]);
  PickupScope *scope = pickup_scope(p);
  for (size_t i = 0; i < change_count; i++) {
    Observation *change = &changes[i];
    if (retired_has(p, change->actor)) continue;
    if (frame->pending_count == frame->pending_capacity) {
      frame->pending_capacity = frame->pending_capacity == 0 ? 8 : frame->pending_capacity * 2;
      frame->pending = xrealloc(frame->pending, frame->pending_capacity * sizeof *frame->pending);
    }
    Pending *pending = &frame->pending[frame->pending_count++];
    memset(pending, 0, sizeof *pending);
    pending->actor = change->actor;
    pending->address = change->address;
    pending->field = change->field;
    if (scope != NULL) {
      pending->has_execution = true;
      pending->execution = scope->execution;
    }
    if (text_eq(change->field.binding, "team") || text_eq(change->field.binding, "score")) {
      pending->kind = PENDING_MATCH;
      pending->value = read_scalar_at(p, change->address, change->field.encoding);
      (void)scalar_of(pending->value, change->field.encoding);
    } else if (text_eq(change->field.binding, "health") ||
               text_eq(change->field.binding, "inventory")) {
      pending->kind = text_eq(change->field.binding, "health") ? PENDING_HEALTH : PENDING_INVENTORY;
      pending->value = read_scalar_at(p, change->address, change->field.encoding);
      (void)scalar_of(pending->value, change->field.encoding);
    } else {
      pending->kind = PENDING_VECTOR;
      pending->vector = read_vector(p, change->address);
      (void)scalar_of(pending->vector.x, "float32");
      (void)scalar_of(pending->vector.y, "float32");
      (void)scalar_of(pending->vector.z, "float32");
    }
  }
  free(changes);
}
static void flush(P *p) {
  if (p->objectives != NULL) qa_mod_source_objectives_flush(p->objectives);
  capture_writes(p);
  Frame *frame = top_frame(p);
  if (frame == NULL) return;
  while (frame->cursor < frame->pending_count) {
    Pending pending = frame->pending[frame->cursor++];
    run_pending(p, &pending);
    frame = top_frame(p);
    if (frame == NULL) return;
  }
  frame->pending_count = 0;
  frame->cursor = 0;
}
static void publish_client_outputs(P *p) {
  if (p->client_outputs == NULL || !qa_source_mod_client_outputs_enabled(p->client_outputs)) return;
  for (size_t i = 0; i < p->projection_count; i++) {
    qa_actor_id actor = p->projections[i].actor;
    if (is_live(p, actor) && qa_source_mod_client_outputs_has(p->client_outputs, actor) &&
        p->has_client_bindings && qa_qvm_mod_client_bindings_admitted(&p->client_bindings, actor))
      qa_source_mod_client_outputs_publish(p->client_outputs, actor);
  }
}
static int64_t client_output_address(P *p, qa_actor_id actor,
                                     const qa_qvm_mod_record_offset *field) {
  const qa_qvm_mod_actor_record *record =
      field == NULL ? NULL : record_named(p->declaration, field->record);
  int64_t slot;
  if (record == NULL || !projection_slot(p, actor, &slot))
    qa_range_error("QVM client output lost its source projection");
  return (int64_t)record->address + slot * (int64_t)record->stride + (int64_t)field->offset;
}
typedef struct {
  P *provider;
  PickupScope *scope;
} ObserveCtx;
static void observe_publish(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  ObserveCtx *observe = ctx;
  if (pickup_scope(observe->provider) == observe->scope &&
      observe->provider->projection_writes == 0)
    capture_writes(observe->provider);
}
static void observe_after(void *ctx, const qa_qvm_committed_write *event) {
  (void)event;
  ObserveCtx *observe = ctx;
  if (pickup_scope(observe->provider) == observe->scope &&
      observe->provider->projection_writes == 0)
    flush(observe->provider);
}
static qa_original_pickup_decision observe_pickup(
    P *p, qa_actor_id actor, const qa_original_pickup_execution *execution,
    qa_qvm_mod_pickup_execute_fn execute, void *execute_ctx) {
  if (p->pickup_scope_count == p->pickup_scope_capacity) {
    p->pickup_scope_capacity = p->pickup_scope_capacity == 0 ? 8 : p->pickup_scope_capacity * 2;
    p->pickup_scopes = xrealloc(p->pickup_scopes, p->pickup_scope_capacity * sizeof(PickupScope *));
  }
  PickupScope *scope = xrealloc(NULL, sizeof *scope);
  scope->actor = actor;
  scope->execution = *execution;
  scope->frame = NULL;
  pickup_scopes(p)[p->pickup_scope_count] = scope;
  size_t range_count = 0, range_capacity = 0;
  qa_qvm_write_range *ranges = NULL;
  for (size_t i = 0; i < p->projection_count; i++) {
    qa_actor_id projected = p->projections[i].actor;
    int64_t slot = p->projections[i].slot;
    for (size_t r = 0; r < p->declaration->actor_record_count; r++) {
      const qa_qvm_mod_actor_record *record = &p->declaration->actor_records[r];
      if (!actor_record_applies(p, projected, record)) continue;
      if (range_count == range_capacity) {
        range_capacity = range_capacity == 0 ? 16 : range_capacity * 2;
        ranges = xrealloc(ranges, range_capacity * sizeof *ranges);
      }
      ranges[range_count].byte_offset =
          (int64_t)record->address + slot * (int64_t)record->stride;
      ranges[range_count].byte_length = (int64_t)record->stride;
      range_count++;
    }
  }
  p->pickup_scope_count++;
  ObserveCtx observe = {p, scope};
  size_t watch = qa_qvm_memory_observe_writes(mem(p), ranges, range_count, observe_publish,
                                              &observe, observe_after, &observe);
  free(ranges);
  qa_original_pickup_decision result = execute(execute_ctx);
  qa_qvm_memory_writes_unobserve(&mem(p)->writes, watch);
  p->pickup_scope_count--;
  free(scope);
  return result;
}

static void publish_events(P *p) {
  if (p->has_player_events) qa_qvm_mod_player_events_publish(&p->player_events);
}
static void discard_events(P *p) {
  if (p->has_player_events) qa_qvm_mod_player_events_discard(&p->player_events);
}
static void publish(P *p);
static void complete_direct_lifecycle(P *p, const qa_qvm_mod_source_call *call,
                                      const int32_t *words, size_t count, int32_t result);
static int64_t allocate_scratch(P *p, int64_t size) {
  int64_t address = p->scratch;
  p->scratch += (size + 3) / 4 * 4;
  int64_t stack_pointer = p->module.interpreter->stack_pointer(p->module.interpreter->ctx);
  if (p->scratch > stack_pointer - 65536)
    qa_range_error("QVM mod argument scratch exceeds its reserved space");
  return address;
}
static const qa_mod_runtime_value *input_of(const qa_qvm_mod_provider_input *inputs,
                                            size_t count, const char *name) {
  for (size_t i = 0; i < count; i++)
    if (text_eq(inputs[i].name, name)) return &inputs[i].value;
  return NULL;
}
static int32_t lower(P *p, const qa_qvm_mod_value *value,
                     const qa_qvm_mod_provider_input *inputs, size_t input_count) {
  if (text_eq(value->kind, "address")) return (int32_t)value->address_value;
  if (text_eq(value->kind, "actor")) {
    const qa_mod_runtime_value *input = input_of(inputs, input_count, value->input);
    if (input == NULL || !text_eq(input->kind, "actor")) qa_range_error("Missing QVM actor input");
    return (int32_t)pointer_for(p, input->actor_value, value->record);
  }
  if (text_eq(value->kind, "client")) {
    const qa_mod_runtime_value *input = input_of(inputs, input_count, value->input);
    int64_t slot = -1;
    bool has_slot = input != NULL && text_eq(input->kind, "actor") && input->actor_value != NULL &&
                    p->has_client_bindings &&
                    qa_qvm_mod_client_bindings_slot(&p->client_bindings, *input->actor_value, &slot);
    if (!has_slot || p->declaration->entity_record == NULL)
      qa_range_error("QVM source call requires an admitted destination client");
    (void)pointer_for(p, input->actor_value, p->declaration->entity_record);
    return (int32_t)slot;
  }
  if (text_eq(value->kind, "time")) {
    const qa_mod_runtime_value *input = input_of(inputs, input_count, value->time_input);
    if (input == NULL || !text_eq(input->kind, "float")) qa_range_error("Missing QVM time input");
    return scalar_of(input->number_value * (text_eq(value->units, "milliseconds") ? 1000 : 1),
                     value->encoding);
  }
  const qa_mod_runtime_value *resolved = NULL;
  qa_mod_runtime_value literal;
  memset(&literal, 0, sizeof literal);
  if (text_eq(value->value.kind, "input")) {
    resolved = input_of(inputs, input_count, value->value.name);
  } else {
    literal.kind = value->value.kind;
    literal.number_value = value->value.number_value;
    literal.string_value = value->value.string_value;
    literal.vector_value = value->value.vector_value;
    resolved = &literal;
  }
  if (text_eq(value->kind, "int32") || text_eq(value->kind, "float32")) {
    if (resolved == NULL || !text_eq(resolved->kind, "float"))
      qa_range_error("Missing scalar QVM input");
    return scalar_of(resolved->number_value, value->kind);
  }
  if (text_eq(value->kind, "vector")) {
    if (resolved == NULL || !text_eq(resolved->kind, "vector"))
      qa_range_error("Missing vector QVM input");
    int64_t address = allocate_scratch(p, 12);
    write_vector(p, address, resolved->vector_value);
    return (int32_t)address;
  }
  /* string */
  if (resolved == NULL || !text_eq(resolved->kind, "string") || resolved->string_value == NULL)
    qa_range_error("Invalid QVM string input");
  int64_t length = (int64_t)strlen(resolved->string_value);
  int64_t address = allocate_scratch(p, length + 1);
  qa_qvm_memory_write_string(mem(p), address, resolved->string_value, length + 1);
  return (int32_t)address;
}
typedef struct {
  int32_t words[QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS];
  size_t word_count;
  int64_t scratch;
  struct { int64_t address; uint8_t bytes[12]; int64_t size; } *globals;
  size_t global_count;
  Frame *frame;
  PickupScope *pickup;
} Execution;
static Execution *begin_call(P *p, const qa_qvm_mod_source_call *call,
                             const qa_qvm_mod_provider_input *inputs, size_t input_count) {
  current(p);
  if (p->frame_count >= 64) qa_range_error("QVM mod callback recursion exceeds 64 calls");
  if (p->frame_count != 0) { flush(p); publish_events(p); }
  Execution *execution = xrealloc(NULL, sizeof *execution);
  memset(execution, 0, sizeof *execution);
  execution->scratch = p->scratch;
  execution->global_count = call->global_count;
  execution->globals =
      xrealloc(NULL, (call->global_count == 0 ? 1 : call->global_count) * sizeof *execution->globals);
  for (size_t i = 0; i < call->global_count; i++) {
    int64_t size = text_eq(call->globals[i].value.kind, "vector") ? 12 : 4;
    execution->globals[i].address = (int64_t)call->globals[i].address;
    execution->globals[i].size = size;
    memcpy(execution->globals[i].bytes, mem(p)->bytes + execution->globals[i].address, (size_t)size);
  }
  execution->word_count = call->argument_count;
  for (size_t i = 0; i < call->argument_count; i++)
    execution->words[i] = lower(p, &call->arguments[i], inputs, input_count);
  for (size_t i = 0; i < call->global_count; i++) {
    int32_t word = lower(p, &call->globals[i].value, inputs, input_count);
    if (text_eq(call->globals[i].value.kind, "vector"))
      qa_qvm_memory_copy_bytes(mem(p), (int64_t)call->globals[i].address, word, 12);
    else
      write_word(p, (int64_t)call->globals[i].address, word);
  }
  const qa_qvm_mod_source_actors *source = p->declaration->source_actors;
  if (source != NULL && call->entry == source->release.entry) {
    size_t argument = (size_t)source->release.argument;
    if (argument >= execution->word_count) qa_range_error("Missing source release argument");
    int32_t pointer = execution->words[argument];
    qa_actor_id actor;
    bool has_actor = actor_at(p, pointer_slot(p, pointer), &actor);
    if (has_actor && !owned_has(p, actor))
      qa_range_error("QVM source removal of a foreign actor requires its owner continuation");
    if (has_actor && p->has_actor_semantics)
      qa_qvm_mod_actors_before_release(&p->actor_semantics, actor);
  }
  refresh(p);
  Frame *frame = &frames(p)[p->frame_count];
  frame->pending_count = 0;
  frame->cursor = 0;
  observe_into(p, frame);
  p->frame_count++;
  execution->frame = frame;
  PickupScope *pickup = p->pickup_scope_count == 0 ? NULL
                                                    : pickup_scopes(p)[p->pickup_scope_count - 1];
  if (pickup != NULL && pickup->frame == NULL) pickup->frame = frame;
  execution->pickup = pickup;
  WeaponInput *weapon = p->weapon_input_count == 0 ? NULL
                                                    : &weapon_inputs(p)[p->weapon_input_count - 1];
  if (weapon != NULL && weapon->frame == NULL) weapon->frame = frame;
  return execution;
}
static void finish_call(P *p, Execution *execution, bool succeeded) {
  Frame *frame = execution->frame;
  if (!p->closed) {
    current(p);
    if (!succeeded && execution->pickup != NULL && execution->pickup->frame == frame) {
      frame->pending_count = 0;
      frame->cursor = 0;
    } else {
      flush(p);
    }
    if (succeeded) {
      publish_client_outputs(p);
      publish_events(p);
    } else {
      discard_events(p);
    }
  }
  p->frame_count--;
  if (execution->pickup != NULL && execution->pickup->frame == frame)
    execution->pickup->frame = NULL;
  if (!p->closed) {
    for (size_t i = 0; i < execution->global_count; i++)
      qa_qvm_memory_write_bytes(mem(p), execution->globals[i].address,
                                execution->globals[i].bytes, (size_t)execution->globals[i].size);
    discard_events(p);
  }
  p->scratch = execution->scratch;
  free(execution->globals);
  free(execution);
}
double qa_qvm_mod_provider_invoke(qa_qvm_mod_provider *p, const qa_qvm_mod_source_call *call,
                                  const qa_qvm_mod_provider_input *inputs, size_t input_count) {
  Execution *execution = begin_call(p, call, inputs, input_count);
  int32_t result = qa_qvm_module_call(&p->module, execution->words, execution->word_count,
                                      (int64_t)call->entry);
  complete_direct_lifecycle(p, call, execution->words, execution->word_count, result);
  double value = 0;
  bool succeeded = true;
  if (text_eq(call->returns, "void")) value = 0;
  else if (text_eq(call->returns, "int32")) value = (double)result;
  else {
    double decoded = (double)qa_bits_to_float32((uint32_t)result);
    if (!isfinite(decoded)) {
      finish_call(p, execution, false);
      finish_retired(p);
      qa_range_error("QVM mod returned a nonfinite scalar");
    }
    value = decoded;
  }
  (void)succeeded;
  finish_call(p, execution, true);
  finish_retired(p);
  if (!p->closed && p->frame_count == 0) publish(p);
  return value;
}
static int32_t invoke_adapter(P *p, const qa_qvm_mod_source_call *call,
                              const qa_qvm_mod_provider_input *inputs, size_t input_count) {
  /* Landed subsystem invoke operations return int32 (their callers
   * widen to double); float32 source returns therefore truncate
   * here, where TS would pass the decoded float. */
  return (int32_t)qa_qvm_mod_provider_invoke(p, call, inputs, input_count);
}
static qa_original_pickup_decision pickup_context(
    P *p, const qa_qvm_mod_pickup *definition, const qa_original_pickup_offer *offer,
    const qa_qvm_pickup_input *inputs, size_t input_count,
    qa_qvm_mod_pickup_execute_fn execute, void *execute_ctx) {
  current(p);
  qa_actor_id world;
  bool has_world = p->services->engine != NULL && p->services->engine->world != NULL &&
                   p->services->engine->world(p->services->engine->ctx, &world);
  if (actor_eq(offer->pickup, offer->recipient) || client_has(p, offer->pickup) ||
      (has_world && actor_eq(world, offer->pickup)) || owned_has(p, offer->pickup))
    qa_range_error("QVM pickup context requires a foreign non-player pickup actor");
  if (p->declaration->entity_record == NULL)
    qa_range_error("QVM pickup requires an actor projection");
  p->pickup_contexts++;
  int64_t scratch = p->scratch;
  size_t restore_count = definition->context_count;
  struct { int64_t address; uint8_t bytes[4]; } *restore =
      xrealloc(NULL, (restore_count == 0 ? 1 : restore_count) * sizeof *restore);
  int32_t *words = xrealloc(NULL, (restore_count == 0 ? 1 : restore_count) * sizeof *words);
  (void)pointer_for(p, &offer->recipient, p->declaration->entity_record);
  (void)pointer_for(p, &offer->pickup, p->declaration->entity_record);
  for (size_t i = 0; i < restore_count; i++) {
    restore[i].address = pointer_for(p, &offer->pickup, definition->context[i].record) +
                         (int64_t)definition->context[i].offset;
    words[i] = lower(p, &definition->context[i].value,
                     (const qa_qvm_mod_provider_input *)inputs, input_count);
  }
  for (size_t i = 0; i < restore_count; i++)
    memcpy(restore[i].bytes, mem(p)->bytes + restore[i].address, 4);
  for (size_t i = 0; i < restore_count; i++) write_word(p, restore[i].address, words[i]);
  free(words);
  qa_original_pickup_decision result = execute(execute_ctx);
  if (!p->closed)
    for (size_t i = 0; i < restore_count; i++)
      qa_qvm_memory_write_bytes(mem(p), restore[i].address, restore[i].bytes, 4);
  free(restore);
  p->scratch = scratch;
  p->pickup_contexts--;
  finish_retired(p);
  return result;
}

/* ---- cvars, configstrings, presentation ---- */
static void engine_print(void *ctx, const char *text) {
  P *p = ctx;
  if (p->services->engine != NULL && p->services->engine->print != NULL)
    p->services->engine->print(p->services->engine->ctx, text);
}
static void bind_server_variables(P *p, qa_cvar_registry *cvars) {
  /* TS also binds mapname from engine.presentation.map; the landed
   * engine fragment carries presentation as an opaque pointer, so
   * the map name is unresolved here. */
  const qa_session_mod_engine_services *engine = p->services->engine;
  if (engine != NULL && engine->has_environment) {
    char buffer[64];
    snprintf(buffer, sizeof buffer, "%g", engine->environment_max_clients);
    (void)qa_cvar_registry_register(cvars, "sv_maxclients", buffer,
                                    QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_LATCH, NULL);
    (void)qa_cvar_registry_set(cvars, "sv_maxclients", buffer, true, NULL);
  }
}
static qa_cvar_registry *new_cvars(P *p) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q3;
  options.context.session = qa_session_actor_registry_session(p->services->actors);
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  options.print = engine_print;
  options.print_ctx = p;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  if (cvars == NULL) return NULL;
  bind_server_variables(p, cvars);
  return cvars;
}
static const char *configstring_of(P *p, int64_t index) {
  for (size_t i = 0; i < p->configstring_count; i++)
    if (p->configstrings[i].index == index) return p->configstrings[i].value;
  return NULL;
}
static void scene_command_push(P *p, const char *text, const qa_actor_id *recipient) {
  if (!text_eq(presentation_runtime(p->declaration), "qvm-scene")) return;
  if (p->scene_commands.count == p->scene_commands.capacity &&
      p->scene_commands.capacity != 0) {
    const SceneCommand *oldest = qa_bounded_queue_peek_oldest(&p->scene_commands);
    if (oldest != NULL) free(oldest->text);
  }
  SceneCommand command;
  command.sequence = ++p->scene_command_sequence;
  command.has_recipient = recipient != NULL;
  if (recipient != NULL) command.recipient = *recipient;
  command.text = xstrdup(text);
  qa_bounded_queue_push(&p->scene_commands, &command);
  p->scene_dirty = true;
}
static char *json_stringify(const char *value) {
  /* JSON.stringify for the configstring command payload. */
  size_t length = strlen(value);
  char *out = xrealloc(NULL, length * 2 + 3);
  size_t written = 0;
  out[written++] = '"';
  for (size_t i = 0; i < length; i++) {
    unsigned char c = (unsigned char)value[i];
    if (c == '"' || c == '\\') { out[written++] = '\\'; out[written++] = (char)c; }
    else if (c == '\n') { memcpy(out + written, "\\n", 2); written += 2; }
    else if (c == '\r') { memcpy(out + written, "\\r", 2); written += 2; }
    else if (c == '\t') { memcpy(out + written, "\\t", 2); written += 2; }
    else if (c == '\b') { memcpy(out + written, "\\b", 2); written += 2; }
    else if (c == '\f') { memcpy(out + written, "\\f", 2); written += 2; }
    else if (c < 0x20) written += (size_t)snprintf(out + written, 7, "\\u%04x", c);
    else out[written++] = (char)c;
  }
  out[written++] = '"';
  out[written] = '\0';
  return out;
}
static void set_configstring(P *p, int64_t index, const char *value) {
  const char *existing = configstring_of(p, index);
  if (text_eq(existing == NULL ? "" : existing, value)) return;
  bool found = false;
  for (size_t i = 0; i < p->configstring_count; i++)
    if (p->configstrings[i].index == index) {
      free(p->configstrings[i].value);
      p->configstrings[i].value = xstrdup(value);
      found = true;
    }
  if (!found) {
    if (p->configstring_count == p->configstring_capacity) {
      p->configstring_capacity = p->configstring_capacity == 0 ? 16 : p->configstring_capacity * 2;
      p->configstrings = xrealloc(p->configstrings, p->configstring_capacity * sizeof *p->configstrings);
    }
    p->configstrings[p->configstring_count].index = index;
    p->configstrings[p->configstring_count].value = xstrdup(value);
    p->configstring_count++;
  }
  p->presentation_revision++;
  if (p->has_presentation_state) {
    qa_q3_source_game_state_record_destroy(&p->presentation_state);
    p->has_presentation_state = false;
  }
  char *quoted = json_stringify(value);
  size_t length = strlen(quoted) + 32;
  char *text = xrealloc(NULL, length);
  snprintf(text, length, "cs %lld %s", (long long)index, quoted);
  free(quoted);
  scene_command_push(p, text, NULL);
  free(text);
}
static void refresh_server_information(P *p) {
  char *server = qa_cvar_registry_info_string(p->cvars, QA_CVAR_FLAG_SERVER_INFO, 1024);
  set_configstring(p, 0, server == NULL ? "" : server);
  free(server);
  char *system = qa_cvar_registry_info_string(p->cvars, QA_CVAR_FLAG_SYSTEM_INFO, 1024);
  set_configstring(p, 1, system == NULL ? "" : system);
  free(system);
}
static const char *information_get(void *ctx, int32_t index) {
  P *p = ctx;
  refresh_server_information(p);
  const char *value = configstring_of(p, index);
  free(p->configstring_scratch);
  p->configstring_scratch = xstrdup(value == NULL ? "" : value);
  return p->configstring_scratch;
}
static void information_set(void *ctx, int32_t index, const char *value) {
  set_configstring(ctx, index, value);
}
static void game_state_drop(void *ctx, const char *message) {
  (void)ctx;
  qa_range_error("%s", message);
}
static const qa_q3_source_game_state_record *game_state_of(P *p) {
  refresh_server_information(p);
  if (!p->has_presentation_state) {
    qa_q3_client_game_state_storage *storage = NULL;
    if (!qa_q3_client_game_state_storage_init(&storage, game_state_drop, NULL))
      qa_range_error("QVM memory allocation failed");
    qa_q3_client_game_state_storage_begin_entries(storage);
    for (size_t i = 0; i < p->configstring_count; i++)
      if (p->configstrings[i].value[0] != '\0')
        qa_q3_client_game_state_storage_append(storage, p->configstrings[i].index,
                                               p->configstrings[i].value);
    memset(&p->presentation_state, 0, sizeof p->presentation_state);
    if (!qa_q3_client_game_state_storage_copy_source_record(storage, &p->presentation_state))
      qa_range_error("QVM memory allocation failed");
    qa_q3_client_game_state_storage_destroy(storage);
    p->has_presentation_state = true;
  }
  return &p->presentation_state;
}
static void free_cp_scene(CpScene *scene) {
  if (scene == NULL) return;
  qa_qvm_mod_scene_publication_free(scene);
  free(scene);
}
static CpScene *scene_publication(P *p) {
  current(p);
  if (p->frame_count != 0) qa_range_error("Cannot read presentation during an original source call");
  const qa_q3_source_game_state_record *game_state = game_state_of(p);
  int64_t server_time = (int64_t)trunc(seconds_of(p->services) * 1000);
  CpScene *current_scene = p->scene_state;
  if (!p->scene_dirty && current_scene != NULL && current_scene->server_time == server_time)
    return current_scene;
  CpScene *scene = xrealloc(NULL, sizeof *scene);
  memset(scene, 0, sizeof *scene);
  for (size_t i = 0; i < p->projection_count; i++) {
    qa_actor_id actor = p->projections[i].actor;
    int64_t slot = p->projections[i].slot;
    if (!is_live(p, actor) || retired_has(p, actor)) continue;
    scene->entities = xrealloc(scene->entities, (scene->entity_count + 1) * sizeof *scene->entities);
    qa_qvm_mod_scene_entity *entry = &scene->entities[scene->entity_count++];
    memset(entry, 0, sizeof *entry);
    entry->actor = actor;
    qa_qvm_writable_view view;
    view_of(p, entity_address(p, slot), qa_qvmy_entity_state_bytes(p->declaration->abi_profile), &view);
    qa_qvmy_read_source_entity_state(&view, p->declaration->abi_profile, &entry->state);
    qa_qvm_shared_entity entity;
    entity_of(p, slot, &entity);
    entry->owned = owned_has(p, actor);
    entry->linked = qa_qvm_shared_entity_get_linked(&entity);
    entry->server_flags = qa_qvm_shared_entity_get_sv_flags(&entity);
    entry->single_client = qa_qvm_shared_entity_get_single_client(&entity);
    entry->bounds.min = qa_qvm_shared_entity_get_absmin(&entity);
    entry->bounds.max = qa_qvm_shared_entity_get_absmax(&entity);
  }
  if (p->has_client_bindings) {
    size_t count = qa_qvm_mod_client_bindings_players(&p->client_bindings, NULL, 0);
    if (count > p->client_scratch_capacity) {
      p->client_scratch = xrealloc(p->client_scratch, count * sizeof *p->client_scratch);
      p->client_scratch_capacity = count;
    }
    count = qa_qvm_mod_client_bindings_players(&p->client_bindings, p->client_scratch,
                                               p->client_scratch_capacity);
    for (size_t i = 0; i < count; i++) {
      if (!p->client_scratch[i].admitted || !is_live(p, p->client_scratch[i].actor)) continue;
      scene->clients = xrealloc(scene->clients, (scene->client_count + 1) * sizeof *scene->clients);
      qa_qvm_mod_scene_client *entry = &scene->clients[scene->client_count++];
      memset(entry, 0, sizeof *entry);
      entry->actor = p->client_scratch[i].actor;
      entry->slot = p->client_scratch[i].slot;
      qa_qvm_writable_view view;
      view_of(p, player_address(p, entry->actor), qa_qvm_player_state_bytes(p->declaration->abi_profile), &view);
      qa_qvm_read_source_player_state(&view, p->declaration->abi_profile, &entry->state);
    }
  }
  size_t command_count = 0;
  SceneCommand *commands = qa_bounded_queue_to_array(&p->scene_commands, &command_count);
  scene->command_count = command_count;
  scene->commands = xrealloc(NULL, (command_count == 0 ? 1 : command_count) * sizeof *scene->commands);
  for (size_t i = 0; i < command_count; i++) {
    memset(&scene->commands[i], 0, sizeof scene->commands[i]);
    scene->commands[i].sequence = commands[i].sequence;
    scene->commands[i].has_recipient = commands[i].has_recipient;
    scene->commands[i].recipient_storage = commands[i].recipient;
    scene->commands[i].recipient =
        commands[i].has_recipient ? &scene->commands[i].recipient_storage : NULL;
    scene->commands[i].text = commands[i].text;
  }
  free(commands);
  scene->revision = ++p->scene_revision;
  scene->server_time = server_time;
  /* Deep-copy the game state record into the publication. */
  scene->game_state.string_offsets = xrealloc(NULL, 1024 * sizeof(int32_t));
  memcpy(scene->game_state.string_offsets, game_state->string_offsets, 1024 * sizeof(int32_t));
  scene->game_state.string_data = xrealloc(NULL, 16000);
  memcpy(scene->game_state.string_data, game_state->string_data, 16000);
  scene->game_state.data_count = game_state->data_count;
  scene->game_state_revision = p->presentation_revision;
  free_cp_scene(p->scene_state);
  p->scene_state = scene;
  p->scene_dirty = false;
  return scene;
}
static void build_scene_view(P *p, const CpScene *scene, qa_qvm_mod_scene_publication *view,
                             qa_qvm_scene_client **clients, size_t *client_capacity,
                             qa_qvm_scene_entity **entities, size_t *entity_capacity,
                             qa_qvm_scene_command **commands, size_t *command_capacity) {
  (void)p;
  memset(view, 0, sizeof *view);
  if (scene == NULL) return;
  view->revision = (double)scene->revision;
  view->server_time = (double)scene->server_time;
  view->game_state = &scene->game_state;
  view->game_state_revision = (double)scene->game_state_revision;
  if (scene->client_count > *client_capacity) {
    *clients = xrealloc(*clients, scene->client_count * sizeof **clients);
    *client_capacity = scene->client_count;
  }
  for (size_t i = 0; i < scene->client_count; i++) {
    (*clients)[i].actor = scene->clients[i].actor;
    (*clients)[i].slot = (double)scene->clients[i].slot;
    (*clients)[i].state = scene->clients[i].state;
  }
  view->clients = *clients;
  view->client_count = scene->client_count;
  if (scene->entity_count > *entity_capacity) {
    *entities = xrealloc(*entities, scene->entity_count * sizeof **entities);
    *entity_capacity = scene->entity_count;
  }
  for (size_t i = 0; i < scene->entity_count; i++) {
    (*entities)[i].actor = scene->entities[i].actor;
    (*entities)[i].state = &scene->entities[i].state;
    (*entities)[i].owned = scene->entities[i].owned;
    (*entities)[i].linked = scene->entities[i].linked;
    (*entities)[i].server_flags = (double)scene->entities[i].server_flags;
    (*entities)[i].single_client = (double)scene->entities[i].single_client;
    (*entities)[i].bounds = scene->entities[i].bounds;
  }
  view->entities = *entities;
  view->entity_count = scene->entity_count;
  if (scene->command_count > *command_capacity) {
    *commands = xrealloc(*commands, scene->command_count * sizeof **commands);
    *command_capacity = scene->command_count;
  }
  for (size_t i = 0; i < scene->command_count; i++) {
    (*commands)[i].sequence = (double)scene->commands[i].sequence;
    (*commands)[i].recipient = scene->commands[i].recipient;
    (*commands)[i].text = scene->commands[i].text;
  }
  view->commands = *commands;
  view->command_count = scene->command_count;
}
static void presentation_client_command(P *p, qa_actor_id actor,
                                        const char *const *arguments, size_t count);
static qa_session_qvm_presentation_context presentation_context_of(void *ctx, qa_actor_id viewer) {
  P *p = ctx;
  current(p);
  if (p->frame_count != 0) qa_range_error("Cannot read presentation during an original source call");
  if (!p->has_client_bindings || !qa_qvm_mod_client_bindings_live(&p->client_bindings, viewer) ||
      !qa_qvm_mod_client_bindings_admitted(&p->client_bindings, viewer))
    return NULL;
  int64_t client_number;
  if (!qa_qvm_mod_client_bindings_slot(&p->client_bindings, viewer, &client_number))
    qa_range_error("Presentation viewer has no admitted source client slot");
  bool weapon = p->services->weapons != NULL &&
                p->services->weapons->presented(p->services->weapons->ctx, viewer,
                                                 p->artifact->module.id);
  qa_qvm_mod_provider_presentation_context *context = p->presentation_context;
  memset(context, 0, sizeof *context);
  context->weapon_presented = weapon;
  if (text_eq(presentation_runtime(p->declaration), "qvm-scene")) {
    CpScene *state = scene_publication(p);
    const qa_qvm_mod_scene_client *player = NULL;
    for (size_t i = 0; i < state->client_count; i++)
      if (actor_eq(state->clients[i].actor, viewer)) player = &state->clients[i];
    if (player == NULL) return NULL;
    context->client_number = player->slot;
    context->game_state = &state->game_state;
    context->game_state_revision = state->game_state_revision;
    context->server_time = state->server_time;
    context->player_state = player->state;
    return context;
  }
  context->client_number = client_number;
  context->game_state = game_state_of(p);
  context->game_state_revision = p->presentation_revision;
  context->server_time = (int64_t)trunc(seconds_of(p->services) * 1000);
  qa_qvm_writable_view view;
  view_of(p, player_address(p, viewer), qa_qvm_player_state_bytes(p->declaration->abi_profile), &view);
  qa_qvm_read_source_player_state(&view, p->declaration->abi_profile, &context->player_state);
  return context;
}
static bool presentation_files(void *ctx, qa_qvm_presentation_files *out) {
  P *p = ctx;
  current(p);
  if (p->mounts == NULL) return false;
  out->mounts = p->mounts;
  out->writable = p->writable;
  return true;
}
static void presentation_command(void *ctx, qa_actor_id viewer,
                                 const char *const *arguments, size_t count) {
  presentation_client_command(ctx, viewer, arguments, count);
}
static const qa_qvm_presentation_binding *presentation_bindings(void *ctx, size_t *out_count) {
  P *p = ctx;
  current(p);
  if (p->projection_count > p->bindings_scratch_capacity) {
    p->bindings_scratch =
        xrealloc(p->bindings_scratch, p->projection_count * sizeof *p->bindings_scratch);
    p->bindings_scratch_capacity = p->projection_count;
  }
  size_t count = 0;
  for (size_t i = 0; i < p->projection_count; i++) {
    qa_actor_id actor = p->projections[i].actor;
    if (!is_live(p, actor) || retired_has(p, actor)) continue;
    p->bindings_scratch[count].actor = actor;
    p->bindings_scratch[count].slot = (double)p->projections[i].slot;
    p->bindings_scratch[count].owned = owned_has(p, actor);
    count++;
  }
  *out_count = count;
  return p->bindings_scratch;
}
static bool presentation_actor(void *ctx, double slot, qa_actor_id *out) {
  P *p = ctx;
  current(p);
  return actor_at(p, (int64_t)slot, out) && is_live(p, *out);
}
static bool presentation_scene(void *ctx, qa_qvm_scene_pair *out) {
  P *p = ctx;
  if (!text_eq(presentation_runtime(p->declaration), "qvm-scene"))
    qa_range_error("Event-only component has no source scene publication");
  build_scene_view(p, scene_publication(p), &p->scene_view, &p->scene_view_clients,
                   &p->scene_view_client_capacity, &p->scene_view_entities,
                   &p->scene_view_entity_capacity, &p->scene_view_commands,
                   &p->scene_view_command_capacity);
  out->current = p->scene_view;
  if (p->scene_baseline == NULL) {
    out->baseline = NULL;
  } else {
    build_scene_view(p, p->scene_baseline, &p->scene_baseline_view, &p->scene_baseline_clients,
                     &p->scene_baseline_client_capacity, &p->scene_baseline_entities,
                     &p->scene_baseline_entity_capacity, &p->scene_baseline_commands,
                     &p->scene_baseline_command_capacity);
    out->baseline = &p->scene_baseline_view;
  }
  return true;
}
static bool presentation_live(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  return !p->closed && is_live(p, actor) && projection_has(p, actor) && !retired_has(p, actor);
}
static void presentation_current(void *ctx) { current(ctx); }

/* ---- source actor lifecycle ---- */
static int64_t entity_slot(P *p, qa_actor_id actor) {
  if (p->declaration->entity_record == NULL) qa_range_error("QVM mod has no engine entity record");
  (void)pointer_for(p, &actor, p->declaration->entity_record);
  int64_t slot;
  if (!projection_slot(p, actor, &slot)) qa_range_error("Missing QVM actor slot");
  return slot;
}
typedef struct {
  P *provider;
  int64_t slot;
  qa_actor_id ground;
} BodyCell;
static void entity_fields(P *p, int64_t slot, qa_qvmy_entity_state_fields *out) {
  qa_qvm_writable_view view;
  view_of(p, entity_address(p, slot), qa_qvmy_entity_state_bytes(p->declaration->abi_profile), &view);
  qa_qvmy_read_source_entity_state(&view, p->declaration->abi_profile, out);
}
static void set_entity_fields(P *p, int64_t slot, const qa_qvmy_entity_state_fields *fields) {
  qa_qvm_writable_view view;
  view_of(p, entity_address(p, slot), qa_qvmy_entity_state_bytes(p->declaration->abi_profile), &view);
  qa_qvmy_write_source_entity_state(&view, fields, p->declaration->abi_profile);
}
static qa_body_state body_read(void *ctx) {
  BodyCell *cell = ctx;
  P *p = cell->provider;
  qa_qvm_shared_entity entity;
  entity_of(p, cell->slot, &entity);
  qa_qvmy_entity_state_fields fields;
  entity_fields(p, cell->slot, &fields);
  qa_body_state state;
  state.origin = qa_qvm_shared_entity_get_current_origin(&entity);
  state.angles = qa_qvm_shared_entity_get_current_angles(&entity);
  state.velocity = fields.pos.delta;
  state.bounds.min = qa_qvm_shared_entity_get_mins(&entity);
  state.bounds.max = qa_qvm_shared_entity_get_maxs(&entity);
  state.ground = actor_at(p, fields.ground_entity_num, &cell->ground) ? &cell->ground : NULL;
  return state;
}
static void body_write(void *ctx, qa_body_state body) {
  BodyCell *cell = ctx;
  P *p = cell->provider;
  qa_qvm_shared_entity entity;
  entity_of(p, cell->slot, &entity);
  qa_qvm_shared_entity_set_current_origin(&entity, body.origin);
  qa_qvm_shared_entity_set_current_angles(&entity, body.angles);
  qa_qvmy_entity_state_fields fields;
  entity_fields(p, cell->slot, &fields);
  fields.pos.delta = body.velocity;
  fields.ground_entity_num = body.ground == NULL ? 1023 : (int32_t)entity_slot(p, *body.ground);
  set_entity_fields(p, cell->slot, &fields);
  qa_qvm_shared_entity_set_mins(&entity, body.bounds.min);
  qa_qvm_shared_entity_set_maxs(&entity, body.bounds.max);
}
static void body_linked(void *ctx, qa_linked_body body) {
  BodyCell *cell = ctx;
  P *p = cell->provider;
  qa_qvm_shared_entity entity;
  entity_of(p, cell->slot, &entity);
  qa_qvm_shared_entity_set_linked(&entity, true);
  qa_qvm_shared_entity_set_linkcount(&entity, (int32_t)body.link_count);
  qa_qvm_shared_entity_set_absmin(&entity, body.absolute_bounds.min);
  qa_qvm_shared_entity_set_absmax(&entity, body.absolute_bounds.max);
}
static bool inline_model_of(P *p, double index, qa_bounds *out_bounds,
                            qa_content_id *out_content, const char **out_family) {
  const qa_session_mod_engine_services *engine = p->services->engine;
  if (engine == NULL || engine->inline_model == NULL || engine->presentation_map == NULL)
    qa_range_error("QVM inline models require destination map model services");
  return engine->inline_model(engine->ctx, index, out_bounds, out_content, out_family);
}
static void bind_owned(P *p, qa_owned_actor actor, int64_t slot) {
  const qa_session_mod_engine_services *engine = p->services->engine;
  if (engine == NULL || engine->physics == NULL)
    qa_range_error("QVM source actors require destination collision services");
  owned_set(p, actor);
  BodyCell *cell = xrealloc(NULL, sizeof *cell);
  cell->provider = p;
  cell->slot = slot;
  qa_body_state_binding binding = {cell, body_read, body_write, body_linked};
  qa_shared_body_table_rebind(p->services->bodies, actor, binding);
  /* physics.bindSource / setCollision target the opaque landed
   * physics fragment and remain unresolved. */
  if (p->has_actor_semantics) qa_qvm_mod_actors_admit(&p->actor_semantics, actor);
  qa_qvm_shared_entity entity;
  entity_of(p, slot, &entity);
  if (qa_qvm_shared_entity_get_linked(&entity)) {
    qa_qvmy_entity_state_fields fields;
    entity_fields(p, slot, &fields);
    qa_body_state_result read = qa_shared_body_table_read(p->services->bodies, actor.id);
    qa_body_state state;
    if (read.has_value) state = read.value;
    else {
      state.origin = qa_qvm_shared_entity_get_current_origin(&entity);
      state.angles = qa_qvm_shared_entity_get_current_angles(&entity);
      state.velocity = fields.pos.delta;
      state.bounds.min = qa_qvm_shared_entity_get_mins(&entity);
      state.bounds.max = qa_qvm_shared_entity_get_maxs(&entity);
      state.ground = NULL;
    }
    qa_body_link_state link_state;
    memset(&link_state, 0, sizeof link_state);
    link_state.link_count = qa_qvm_shared_entity_get_linkcount(&entity);
    link_state.has_linked = true;
    link_state.linked.actor = actor.id;
    link_state.linked.state = state;
    link_state.linked.absolute_bounds.min = qa_qvm_shared_entity_get_absmin(&entity);
    link_state.linked.absolute_bounds.max = qa_qvm_shared_entity_get_absmax(&entity);
    link_state.linked.link_count = link_state.link_count;
    qa_shared_body_table_restore_link_state(p->services->bodies, actor, link_state);
  }
}
static void release_source(P *p, int64_t slot) {
  const qa_qvm_mod_source_actors *source = p->declaration->source_actors;
  if (source == NULL) return;
  size_t count = (size_t)source->release.argument + 1;
  int32_t *words = xrealloc(NULL, count * sizeof *words);
  for (size_t i = 0; i < count; i++) words[i] = 0;
  words[(size_t)source->release.argument] = (int32_t)entity_address(p, slot);
  (void)qa_qvm_module_call(&p->module, words, count, (int64_t)source->release.entry);
  free(words);
}
static void adopt_source(P *p, int32_t pointer) {
  const qa_qvm_mod_source_actors *source = p->declaration->source_actors;
  if (source == NULL) qa_range_error("Missing QVM source actor declaration");
  int64_t slot = pointer_slot(p, pointer);
  int64_t reserved = p->declaration->clients == NULL ? 0 : (int64_t)p->declaration->clients->maximum;
  if (slot < reserved) qa_range_error("QVM allocator returned a reserved client row");
  qa_actor_id existing;
  if (actor_at(p, slot, &existing) || read_word(p, pointer + (int64_t)source->inuse) == 0)
    qa_range_error("Authored QVM allocator returned an occupied or inactive entity");
  qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(
      p->services->actors, p->artifact->module.id, slot, "qvm:mod-actor");
  projection_set(p, actor.id, slot);
  bind_owned(p, actor, slot);
}
static void retire_source(P *p, int64_t slot) {
  const qa_qvm_mod_source_actors *source = p->declaration->source_actors;
  qa_actor_id actor;
  if (source == NULL || !actor_at(p, slot, &actor) ||
      read_word(p, entity_address(p, slot) + (int64_t)source->inuse) != 0)
    return;
  qa_owned_actor owner;
  if (!owned_get(p, actor, &owner))
    qa_range_error("QVM source removed a foreign actor without its owner continuation");
  removing_add(p, actor);
  qa_session_actor_registry_release(p->services->actors, owner);
  removing_delete(p, actor);
}
static void complete_direct_lifecycle(P *p, const qa_qvm_mod_source_call *call,
                                      const int32_t *words, size_t count, int32_t result) {
  const qa_qvm_mod_source_actors *source = p->declaration->source_actors;
  if (source == NULL) return;
  if (call->entry == source->allocate) adopt_source(p, result);
  else if (call->entry == source->release.entry) {
    size_t argument = (size_t)source->release.argument;
    if (argument >= count) qa_range_error("Missing source release argument");
    retire_source(p, pointer_slot(p, words[argument]));
  }
}
typedef struct {
  P *provider;
  bool allocate;
} HookCell;
static int32_t lifecycle_hook(void *ctx, qa_qvmy_function_call *call) {
  HookCell *cell = ctx;
  P *p = cell->provider;
  const qa_qvm_mod_source_actors *source = p->declaration->source_actors;
  if (cell->allocate) {
    int32_t pointer = call->proceed(call->call_ctx);
    adopt_source(p, pointer);
    return pointer;
  }
  size_t offset = (size_t)source->release.argument * 4;
  const uint8_t *b = call->words + offset;
  int32_t pointer = (int32_t)((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 |
                              (uint32_t)b[3] << 24);
  int64_t slot = pointer_slot(p, pointer);
  qa_actor_id actor;
  bool has_actor = actor_at(p, slot, &actor);
  if (has_actor && !owned_has(p, actor))
    qa_range_error("QVM source removal of a foreign actor requires its owner continuation");
  if (has_actor && p->has_actor_semantics) qa_qvm_mod_actors_before_release(&p->actor_semantics, actor);
  int32_t result = call->proceed(call->call_ctx);
  retire_source(p, slot);
  return result;
}
static void bind_lifecycle(P *p) {
  const qa_qvm_mod_source_actors *source = p->declaration->source_actors;
  if (source == NULL) return;
  double entries[2] = {source->allocate, source->release.entry};
  for (size_t i = 0; i < 2; i++) {
    HookCell *cell = xrealloc(NULL, sizeof *cell);
    cell->provider = p;
    cell->allocate = i == 0;
    qa_guest_callback_reference reference;
    memset(&reference, 0, sizeof reference);
    reference.kind = QA_GUEST_CALLBACK_QVM;
    reference.module = p->artifact->module;
    reference.instruction_index = entries[i];
    size_t id = qa_qvm_module_bind_function(&p->module, &reference, lifecycle_hook, cell);
    if (p->hook_count == p->hook_capacity) {
      p->hook_capacity = p->hook_capacity == 0 ? 4 : p->hook_capacity * 2;
      p->hooks = xrealloc(p->hooks, p->hook_capacity * sizeof *p->hooks);
    }
    p->hooks[p->hook_count++] = id;
  }
}
static void emit_source_event(P *p) {
  /* Q3SourceEvent emission targets the opaque landed engine events
   * fragment; only the TS missing-service error is reproducible. */
  (void)p;
  qa_range_error("QVM presentation requires destination event services");
}
static void link_entity(P *p, int64_t slot) {
  qa_qvm_shared_entity entity;
  entity_of(p, slot, &entity);
  qa_actor_id actor;
  if (!actor_at(p, slot, &actor))
    qa_range_error("QVM linked an entity without its authored allocation");
  qa_owned_actor owner;
  if (!resolve_owned(p, actor, &owner)) qa_range_error("QVM linked a stale entity");
  const qa_session_mod_engine_services *engine = p->services->engine;
  if (engine == NULL || engine->physics == NULL)
    qa_range_error("QVM entity links require destination collision services");
  qa_qvm_entity_collision_model model = qa_qvm_shared_entity_get_model(&entity);
  if (text_eq(model.kind, "inline")) (void)inline_model_of(p, model.index, NULL, NULL, NULL);
  qa_qvmy_entity_state_fields fields;
  entity_fields(p, slot, &fields);
  qa_vec3 mins = qa_qvm_shared_entity_get_mins(&entity);
  qa_vec3 maxs = qa_qvm_shared_entity_get_maxs(&entity);
  int32_t contents = qa_qvm_shared_entity_get_contents(&entity);
  /* TS byte(): Math.max(1, Math.min(255, Math.trunc(v))). */
  double zb = trunc((double)(float)(maxs.z + 32));
  double nb = trunc(-mins.z), xb = trunc(maxs.x);
  int32_t bzb = (int32_t)(zb < 1 ? 1 : zb > 255 ? 255 : zb);
  int32_t bnb = (int32_t)(nb < 1 ? 1 : nb > 255 ? 255 : nb);
  int32_t bxb = (int32_t)(xb < 1 ? 1 : xb > 255 ? 255 : xb);
  fields.solid = text_eq(model.kind, "inline") ? 0xffffff
                 : (contents & (1 | 0x2000000)) == 0 ? 0
                 : (bzb << 16) | (bnb << 8) | bxb;
  set_entity_fields(p, slot, &fields);
  qa_shared_body_table_unlink(p->services->bodies, owner);
  qa_qvm_shared_entity_set_linked(&entity, false);
  /* physics.setCollision for owned actors is unresolved (opaque). */
  qa_shared_body_table_link(p->services->bodies, owner, NULL);
  qa_linked_body_result linked = qa_shared_body_table_linked(p->services->bodies, actor);
  if (linked.has_value) {
    qa_qvm_shared_entity_set_linked(&entity, true);
    qa_qvm_shared_entity_set_linkcount(&entity, (int32_t)linked.value.link_count);
    qa_qvm_shared_entity_set_absmin(&entity, linked.value.absolute_bounds.min);
    qa_qvm_shared_entity_set_absmax(&entity, linked.value.absolute_bounds.max);
  }
}
static void publish(P *p) {
  p->scene_dirty = true;
  publish_events(p);
  if (text_eq(presentation_runtime(p->declaration), "qvm-scene")) return;
  double event_type_limit = p->declaration->source_actors == NULL
                                ? INFINITY : p->declaration->source_actors->event_entity_type;
  for (size_t i = 0; i < p->owned_count; i++) {
    qa_actor_id actor = p->owned[i].id;
    int64_t slot;
    if (!projection_slot(p, actor, &slot)) continue;
    qa_qvm_shared_entity entity;
    entity_of(p, slot, &entity);
    if (!qa_qvm_shared_entity_get_linked(&entity)) continue;
    qa_qvmy_entity_state_fields fields;
    entity_fields(p, slot, &fields);
    if (fields.event == 0 && (double)fields.e_type < event_type_limit) {
      event_key_delete(p, actor);
      continue;
    }
    char key[64];
    snprintf(key, sizeof key, "%d:%d", fields.event, fields.e_type);
    if (text_eq(event_key_get(p, actor), key)) continue;
    event_key_set(p, actor, key);
    if (p->services->engine == NULL) emit_source_event(p);
    /* Entity-event emission into the opaque engine events fragment
     * is unresolved; the snapshot/key bookkeeping above matches TS. */
    emit_source_event(p);
  }
}

/* ---- engine and spatial syscalls ---- */
static qa_vec3 guest_vector(const qa_qvm_host_call *call, int32_t address) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(call->guest, address, 12, &view);
  qa_vec3 result = {(double)qa_qvm_writable_view_get_float32(&view, 0, true),
                    (double)qa_qvm_writable_view_get_float32(&view, 4, true),
                    (double)qa_qvm_writable_view_get_float32(&view, 8, true)};
  return result;
}
static bool engine_syscall(P *p, const qa_qvm_host_call *call, int32_t *out) {
  if (!text_eq(call->kind, "engine") || !text_eq(call->role, "qagame")) return false;
  switch (call->code) {
    case QA_QVM_GAME_IMPORT_G_LOCATE_GAME_DATA: {
      const qa_qvm_mod_actor_record *entity = entity_record_of(p);
      int32_t w1 = qa_qvm_host_call_word(call, 1), w2 = qa_qvm_host_call_word(call, 2);
      int32_t w3 = qa_qvm_host_call_word(call, 3), w4 = qa_qvm_host_call_word(call, 4);
      int32_t w5 = qa_qvm_host_call_word(call, 5);
      bool client_ok = false;
      for (size_t i = 0; i < p->declaration->actor_record_count; i++) {
        const qa_qvm_mod_actor_record *record = &p->declaration->actor_records[i];
        if ((int32_t)record->address == w4 && (int32_t)record->stride == w5 &&
            (p->declaration->clients == NULL ||
             text_eq(record->id, p->declaration->clients->player_state_record)))
          client_ok = true;
      }
      if (w1 != (int32_t)entity->address || w3 != (int32_t)entity->stride || w2 < 0 ||
          w2 > (int32_t)entity->capacity || !client_ok)
        qa_range_error("Authored QVM engine arrays differ from their declared layout");
      *out = 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_G_LINKENTITY:
    case QA_QVM_GAME_IMPORT_G_UNLINKENTITY: {
      int64_t slot = pointer_slot(p, qa_qvm_host_call_word(call, 1));
      if (call->code == QA_QVM_GAME_IMPORT_G_UNLINKENTITY) {
        qa_actor_id actor;
        if (actor_at(p, slot, &actor)) {
          qa_owned_actor owner;
          if (resolve_owned(p, actor, &owner)) qa_shared_body_table_unlink(p->services->bodies, owner);
        }
        qa_qvm_shared_entity entity;
        entity_of(p, slot, &entity);
        qa_qvm_shared_entity_set_linked(&entity, false);
        *out = 0;
        return true;
      }
      link_entity(p, slot);
      *out = 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_G_SET_BRUSH_MODEL: {
      int32_t name_word = qa_qvm_host_call_word(call, 2);
      if (name_word == 0) qa_range_error("SV_SetBrushModel: NULL");
      char *name = qa_qvm_memory_read_string(call->guest, name_word);
      if (name[0] != '*') {
        char message[512];
        snprintf(message, sizeof message, "SV_SetBrushModel: %s is not a brush model", name);
        free(name);
        qa_range_error("%s", message);
      }
      int64_t slot = pointer_slot(p, qa_qvm_host_call_word(call, 1));
      int32_t index = qa_native_atoi(name + 1);
      free(name);
      qa_bounds bounds;
      (void)inline_model_of(p, index, &bounds, NULL, NULL);
      qa_qvm_shared_entity entity;
      entity_of(p, slot, &entity);
      qa_qvm_entity_collision_model model = {"inline", index};
      qa_qvm_shared_entity_set_model(&entity, model);
      qa_qvm_shared_entity_set_mins(&entity, bounds.min);
      qa_qvm_shared_entity_set_maxs(&entity, bounds.max);
      qa_qvm_shared_entity_set_contents(&entity, -1);
      qa_actor_id actor;
      qa_owned_actor owner;
      if (!actor_at(p, slot, &actor) || !resolve_owned(p, actor, &owner))
        qa_range_error("QVM brush model has no live actor");
      if (!owned_has(p, owner.id)) {
        qa_body_state_result body = qa_shared_body_table_read(p->services->bodies, owner.id);
        if (!body.has_value) qa_range_error("QVM brush model requires an existing body");
        qa_body_state next = body.value;
        next.bounds = bounds;
        qa_shared_body_table_write(p->services->bodies, owner, next);
        const qa_session_mod_engine_services *engine = p->services->engine;
        if (engine == NULL || engine->physics == NULL)
          qa_range_error("QVM brush models require destination collision services");
        /* physics.setCollision is unresolved (opaque fragment). */
      }
      link_entity(p, slot);
      *out = 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_G_SEND_SERVER_COMMAND: {
      if (qa_qvm_host_call_word(call, 1) != -1)
        qa_range_error("Targeted QVM component commands require declared source client bindings");
      emit_source_event(p);
      *out = 0;
      return true;
    }
    case QA_QVM_GAME_IMPORT_G_ADJUST_AREA_PORTAL_STATE:
      /* Q3GuestWorld portals and scene topology are unresolved. */
      qa_range_error("QVM portal state requires destination topology services");
    case QA_QVM_GAME_IMPORT_G_AREAS_CONNECTED:
      qa_range_error("QVM area connectivity requires destination topology services");
    default:
      return false;
  }
}
static bool spatial_syscall(P *p, const qa_qvm_host_call *call, int32_t *out) {
  if (!text_eq(call->kind, "engine") || !text_eq(call->role, "qagame")) return false;
  const qa_session_mod_engine_services *engine = p->services->engine;
  if (call->code == QA_QVM_GAME_IMPORT_G_ENTITIES_IN_BOX) {
    if (engine == NULL || engine->area_entities == NULL)
      qa_range_error("QVM area queries require destination spatial services");
    qa_bounds bounds = {guest_vector(call, qa_qvm_host_call_word(call, 1)),
                        guest_vector(call, qa_qvm_host_call_word(call, 2))};
    size_t count = 0;
    const qa_actor_id *found = engine->area_entities(engine->ctx, bounds, &count);
    int32_t maximum = qa_qvm_host_call_word(call, 4);
    size_t kept = maximum < 0 ? count : (count < (size_t)maximum ? count : (size_t)maximum);
    if (kept != 0) {
      qa_qvm_writable_view view;
      qa_qvm_memory_data_view(call->guest, qa_qvm_host_call_word(call, 3), (int64_t)kept * 4, &view);
      for (size_t i = 0; i < kept; i++)
        qa_qvm_writable_view_set_int32(&view, (int64_t)i * 4, (int32_t)entity_slot(p, found[i]), true);
    }
    *out = (int32_t)kept;
    return true;
  }
  if (call->code == QA_QVM_GAME_IMPORT_G_ENTITY_CONTACT ||
      call->code == QA_QVM_GAME_IMPORT_G_ENTITY_CONTACTCAPSULE) {
    int64_t slot = pointer_slot(p, qa_qvm_host_call_word(call, 3));
    qa_qvm_shared_entity entity;
    entity_of(p, slot, &entity);
    qa_qvm_entity_collision_model model = qa_qvm_shared_entity_get_model(&entity);
    qa_bounds bounds = {guest_vector(call, qa_qvm_host_call_word(call, 1)),
                        guest_vector(call, qa_qvm_host_call_word(call, 2))};
    bool capsule = call->code == QA_QVM_GAME_IMPORT_G_ENTITY_CONTACTCAPSULE;
    if (text_eq(model.kind, "inline"))
      qa_range_error("QVM entity contact requires destination scene services");
    qa_bounds target = {qa_qvm_shared_entity_get_mins(&entity),
                        qa_qvm_shared_entity_get_maxs(&entity)};
    qa_collision_counters counters;
    qa_collision_counters_init(&counters);
    qa_collision_box_storage storage;
    memset(&storage, 0, sizeof storage);
    qa_temporary_collision_model *temporary = text_eq(model.kind, "capsule")
        ? qa_create_capsule_model(target, &counters, &storage)
        : qa_create_box_model(target, &counters, &storage);
    qa_vec3 zero = {0, 0, 0};
    qa_temporary_trace_query query;
    memset(&query, 0, sizeof query);
    query.start = zero;
    query.end = zero;
    query.mask = -1;
    query.shape.kind = capsule ? "capsule" : "box";
    query.shape.mins = bounds.min;
    query.shape.maxs = bounds.max;
    qa_source_trace_result result = qa_temporary_model_transformed_trace_source(
        temporary, query, qa_qvm_shared_entity_get_current_origin(&entity),
        qa_qvm_shared_entity_get_current_angles(&entity));
    qa_temporary_collision_model_destroy(temporary);
    *out = (result.start_solid || result.all_solid) ? 1 : 0;
    return true;
  }
  if (call->code == QA_QVM_GAME_IMPORT_G_TRACE || call->code == QA_QVM_GAME_IMPORT_G_TRACECAPSULE)
    qa_range_error("QVM trace requires destination scene services");
  if (call->code == QA_QVM_GAME_IMPORT_G_POINT_CONTENTS)
    qa_range_error("QVM point contents requires destination scene services");
  return false;
}
static void command_port_of(P *p) {
  if (p->commands == NULL)
    qa_range_error("QVM mod console execution requires its component command service");
}
typedef struct {
  P *provider;
} SyscallCtx;
static void syscall_print(void *ctx, const char *text) {
  P *p = ((SyscallCtx *)ctx)->provider;
  if (p->services->engine == NULL || p->services->engine->print == NULL)
    qa_range_error("QVM print requires destination engine services");
  p->services->engine->print(p->services->engine->ctx, text);
}
static double syscall_milliseconds(void *ctx) {
  return trunc(seconds_of(((SyscallCtx *)ctx)->provider->services) * 1000);
}
static const char *const *syscall_arguments(void *ctx, size_t *out_count) {
  (void)ctx;
  *out_count = 0;
  return NULL;
}
static double syscall_real_time(void *ctx, bool has_output, qa_qvm_calendar *out) {
  (void)ctx;
  (void)has_output;
  (void)out;
  qa_range_error("QVM real-time service is not bound");
}
static void console_execute_now(void *ctx, const char *text) {
  P *p = ((SyscallCtx *)ctx)->provider;
  command_port_of(p);
  (void)qa_mod_command_port_execute_now(p->commands, text, NULL);
}
static void console_append(void *ctx, const char *text) {
  P *p = ((SyscallCtx *)ctx)->provider;
  command_port_of(p);
  qa_mod_command_port_append(p->commands, text, NULL);
}
static void console_insert(void *ctx, const char *text) {
  P *p = ((SyscallCtx *)ctx)->provider;
  command_port_of(p);
  qa_mod_command_port_insert(p->commands, text, NULL);
}
static const char *client_get_userinfo(void *ctx, double slot) {
  P *p = ((SyscallCtx *)ctx)->provider;
  return p->has_client_bindings
             ? qa_qvm_mod_client_bindings_get_userinfo(&p->client_bindings, (int64_t)slot) : "";
}
static void client_set_userinfo(void *ctx, double slot, const char *value) {
  P *p = ((SyscallCtx *)ctx)->provider;
  if (p->has_client_bindings) qa_qvm_mod_client_bindings_set_userinfo(&p->client_bindings, (int64_t)slot, value);
}
static qa_qvmx_wire_user_command client_get_command(void *ctx, double slot) {
  P *p = ((SyscallCtx *)ctx)->provider;
  if (!p->has_client_bindings) qa_range_error("Missing QVM clients");
  qa_q3_wire_user_command command =
      qa_qvm_mod_client_bindings_get_user_command(&p->client_bindings, (int64_t)slot);
  qa_qvmx_wire_user_command result;
  memset(&result, 0, sizeof result);
  result.server_time = (double)command.server_time;
  for (size_t i = 0; i < 3; i++) result.angles[i] = (double)command.angles[i];
  result.forwardmove = (double)command.forwardmove;
  result.rightmove = (double)command.rightmove;
  result.upmove = (double)command.upmove;
  result.buttons = (double)command.buttons;
  result.weapon = (double)command.weapon;
  return result;
}
static void client_send_command(void *ctx, double slot, const char *text) {
  P *p = ((SyscallCtx *)ctx)->provider;
  if (p->has_client_bindings)
    qa_qvm_mod_client_bindings_send_server_command(&p->client_bindings, (int64_t)slot, text);
}
static void client_drop(void *ctx, double slot, const char *reason) {
  P *p = ((SyscallCtx *)ctx)->provider;
  if (p->has_client_bindings) qa_qvm_mod_client_bindings_drop_client(&p->client_bindings, (int64_t)slot, reason);
}
static qa_qvm_host_result provider_syscall(void *ctx, const qa_qvm_host_call *call) {
  P *p = ctx;
  current(p);
  flush(p);
  publish_events(p);
  SyscallCtx syscall_ctx = {p};
  qa_qvm_host_result result = 0;
  bool handled = false;
  qa_qvm_common_services common;
  memset(&common, 0, sizeof common);
  common.role = "qagame";
  common.cvars = p->cvars;
  common.ctx = &syscall_ctx;
  common.print = syscall_print;
  common.milliseconds = syscall_milliseconds;
  common.arguments = syscall_arguments;
  common.commands.ctx = &syscall_ctx;
  common.commands.execute_now = console_execute_now;
  common.commands.append = console_append;
  common.commands.insert = console_insert;
  common.real_time = syscall_real_time;
  handled = qa_qvm_common_syscall(call, common, &result);
  if (!handled) handled = qa_qvm_server_information_syscall(call, &p->information, &result);
  if (!handled)
    handled = qa_qvm_entity_token_syscall(
        call, qa_qvm_entity_tokens_as_services(&p->entity_tokens), &result);
  if (!handled && p->has_client_bindings) {
    qa_qvm_client_game_services services;
    memset(&services, 0, sizeof services);
    services.abi_profile = p->declaration->abi_profile;
    services.max_clients = p->declaration->clients == NULL ? 0 : p->declaration->clients->maximum;
    services.ctx = &syscall_ctx;
    services.get_userinfo = client_get_userinfo;
    services.set_userinfo = client_set_userinfo;
    services.get_user_command = client_get_command;
    services.send_server_command = client_send_command;
    services.drop_client = client_drop;
    handled = qa_qvm_client_game_syscall(call, services, &result);
  }
  if (!handled && p->files != NULL) handled = qa_qvm_file_syscall(call, p->files, &result);
  if (!handled) handled = engine_syscall(p, call, &result);
  if (!handled) handled = spatial_syscall(p, call, &result);
  if (!handled) qa_qvm_reject_syscall(call);
  current(p);
  refresh(p);
  return result;
}

/* ---- public provider methods ---- */
void qa_qvm_mod_provider_bind_commands(qa_qvm_mod_provider *p, qa_mod_command_port *commands) {
  current(p);
  if (p->commands != NULL) qa_range_error("QVM mod commands are already bound");
  p->commands = commands;
}
static void presentation_client_command(P *p, qa_actor_id actor,
                                        const char *const *arguments, size_t count) {
  current(p);
  if (!p->has_client_bindings || !qa_qvm_mod_client_bindings_live(&p->client_bindings, actor) ||
      !qa_qvm_mod_client_bindings_admitted(&p->client_bindings, actor))
    qa_range_error("Component client command requires an admitted live source client");
  int64_t slot;
  bool projected = projection_slot(p, actor, &slot);
  bool player = false;
  if (projected) {
    size_t total = qa_qvm_mod_client_bindings_players(&p->client_bindings, NULL, 0);
    if (total > p->client_scratch_capacity) {
      p->client_scratch = xrealloc(p->client_scratch, total * sizeof *p->client_scratch);
      p->client_scratch_capacity = total;
    }
    total = qa_qvm_mod_client_bindings_players(&p->client_bindings, p->client_scratch,
                                               p->client_scratch_capacity);
    for (size_t i = 0; i < total; i++)
      if (actor_eq(p->client_scratch[i].actor, actor) && p->client_scratch[i].slot == slot &&
          p->client_scratch[i].admitted)
        player = true;
  }
  if (!projected || !player)
    qa_range_error("Component client command has no current source projection");
  qa_qvm_mod_value arguments_[2];
  memset(arguments_, 0, sizeof arguments_);
  arguments_[0].kind = "int32";
  arguments_[0].value.kind = "float";
  arguments_[0].value.number_value = QA_QVM_GAME_EXPORT_GAME_CLIENT_COMMAND;
  arguments_[1].kind = "int32";
  arguments_[1].value.kind = "float";
  arguments_[1].value.number_value = (double)slot;
  qa_qvm_mod_source_call call = {0, arguments_, 2, NULL, 0, "void"};
  qa_qvm_mod_provider_input inputs[2];
  inputs[0].name = "self";
  inputs[0].value.kind = "actor";
  inputs[0].value.actor_value = &actor;
  inputs[1].name = "time";
  inputs[1].value.kind = "float";
  inputs[1].value.number_value = seconds_of(p->services);
  Execution *execution = begin_call(p, &call, inputs, 2);
  (void)qa_qvm_module_command(&p->module, execution->words, execution->word_count, arguments, count);
  finish_call(p, execution, true);
  finish_retired(p);
  if (!p->closed && p->frame_count == 0) publish(p);
}
bool qa_qvm_mod_provider_console_command(qa_qvm_mod_provider *p, qa_command_invocation *command) {
  current(p);
  qa_command_invocation_assert_active(command);
  qa_qvm_mod_value argument;
  memset(&argument, 0, sizeof argument);
  argument.kind = "int32";
  argument.value.kind = "float";
  argument.value.number_value = QA_QVM_GAME_EXPORT_GAME_CONSOLE_COMMAND;
  qa_qvm_mod_source_call call = {0, &argument, 1, NULL, 0, "int32"};
  Execution *execution = begin_call(p, &call, NULL, 0);
  bool result = qa_qvm_module_command(&p->module, execution->words, execution->word_count,
                                      (const char *const *)command->argv, command->argc) != 0;
  finish_call(p, execution, true);
  finish_retired(p);
  if (!p->closed && p->frame_count == 0) publish(p);
  return result;
}
char *qa_qvm_mod_provider_read_script(qa_qvm_mod_provider *p, const char *name) {
  current(p);
  if (p->mounts == NULL) return NULL;
  qa_content_opened_resource resource;
  if (!qa_content_mounted_open(p->mounts, name, NULL, NULL, &resource)) return NULL;
  current(p);
  char *text = xrealloc(NULL, resource.byte_length + 1);
  memcpy(text, resource.bytes, resource.byte_length);
  text[resource.byte_length] = '\0';
  free(resource.bytes);
  return text;
}
static void remember_defaults(P *p) {
  for (size_t i = 0; i < p->default_count; i++) free(p->defaults[i].bytes);
  free(p->defaults);
  p->default_count = p->declaration->actor_record_count;
  p->defaults = xrealloc(NULL, (p->default_count == 0 ? 1 : p->default_count) * sizeof *p->defaults);
  for (size_t i = 0; i < p->default_count; i++) {
    const qa_qvm_mod_actor_record *record = &p->declaration->actor_records[i];
    size_t length = (size_t)(record->stride * record->capacity);
    p->defaults[i].id = record->id;
    p->defaults[i].length = length;
    p->defaults[i].bytes = xrealloc(NULL, length == 0 ? 1 : length);
    memcpy(p->defaults[i].bytes, mem(p)->bytes + (int64_t)record->address, length);
  }
}
static void validate_current(void *ctx) { current(ctx); }
void qa_qvm_mod_provider_initialize(qa_qvm_mod_provider *p) {
  qa_qvm_mod_provider_reserve_protection(p);
  size_t store_count = p->declaration->source_actors == NULL
                           ? 0 : p->declaration->source_actors->initial_stores.count;
  if (store_count != 0) {
    int64_t *stores = xrealloc(NULL, store_count * sizeof *stores);
    qa_qvm_actor_store *out = xrealloc(NULL, store_count * sizeof *out);
    for (size_t i = 0; i < store_count; i++)
      stores[i] = (int64_t)p->declaration->source_actors->initial_stores.values[i];
    qa_qvm_actor_bootstrap(stores, store_count, p->artifact->instructions,
                           p->artifact->instruction_count,
                           (int64_t)p->artifact->initialized_data_length, p->artifact->data_length,
                           p->artifact->bss_length, p->declaration->actor_records,
                           p->declaration->actor_record_count, out);
    for (size_t i = 0; i < store_count; i++) write_word(p, out[i].address, (int32_t)out[i].value);
    free(stores);
    free(out);
  }
  for (size_t i = 0; i < p->declaration->initialize_count; i++) {
    const qa_qvm_mod_source_call *call = &p->declaration->initialize[i];
    qa_qvm_mod_provider_input input = {"time", {"float", seconds_of(p->services), NULL, {0, 0, 0}, NULL}};
    Execution *execution = begin_call(p, call, &input, 1);
    int32_t result = qa_qvm_module_call_async(&p->module, execution->words, execution->word_count,
                                              (int64_t)call->entry, validate_current, p);
    complete_direct_lifecycle(p, call, execution->words, execution->word_count, result);
    finish_call(p, execution, true);
  }
  remember_defaults(p);
  if (p->has_client_bindings) qa_qvm_mod_client_bindings_start(&p->client_bindings);
  publish(p);
}
void qa_qvm_mod_provider_reserve_protection(qa_qvm_mod_provider *p) {
  for (size_t i = 0; i < p->protection_count; i++)
    qa_qvm_mod_protection_reserve(p->protection_channels[i]);
}
static int64_t match_current(MatchCell *cell) {
  P *p = cell->provider;
  qa_owned_actor owned;
  if (p->closed || !owned_get(p, cell->actor, &owned) ||
      !actor_eq(owned.id, cell->owned.id) || !is_live(p, cell->actor))
    qa_range_error("QVM match actor was retired");
  return (int64_t)cell->record->address + cell->slot * (int64_t)cell->record->stride;
}
static double match_read(MatchCell *cell, const qa_qvm_mod_actor_field *field) {
  return read_scalar_at(cell->provider, match_current(cell) + (int64_t)field->offset, field->encoding);
}
static void match_write(MatchCell *cell, const qa_qvm_mod_actor_field *field, double value) {
  int64_t address = match_current(cell) + (int64_t)field->offset;
  if (text_eq(field->encoding, "float32")) {
    qa_qvm_writable_view view;
    view_of(cell->provider, address, 4, &view);
    qa_qvm_writable_view_set_float32(&view, 0, value, true);
  } else {
    write_word(cell->provider, address, (int32_t)qa_source_score(value));
  }
}
static const char *match_team(void *ctx) {
  MatchCell *cell = ctx;
  if (cell->team == NULL) return NULL;
  return qa_source_team(cell->team->team_values, cell->team->team_value_count, match_read(cell, cell->team));
}
static double match_score(void *ctx) {
  MatchCell *cell = ctx;
  if (cell->score == NULL) qa_range_error("QVM match actor has no score field");
  return match_read(cell, cell->score);
}
static void match_set_team(void *ctx, const char *value) {
  MatchCell *cell = ctx;
  if (cell->team == NULL) qa_range_error("QVM match actor has no team field");
  match_write(cell, cell->team,
              qa_original_team(cell->team->team_values, cell->team->team_value_count, value));
}
static void match_set_score(void *ctx, double value) {
  MatchCell *cell = ctx;
  if (cell->score == NULL) qa_range_error("QVM match actor has no score field");
  match_write(cell, cell->score, value);
}
static qa_source_match_player_result match_resolve(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  qa_source_match_player_result result;
  memset(&result, 0, sizeof result);
  qa_owned_actor owned;
  int64_t slot;
  const qa_qvm_mod_actor_record *record =
      p->declaration->entity_record == NULL
          ? NULL : record_named(p->declaration, p->declaration->entity_record);
  if (p->closed || !owned_get(p, actor, &owned) || !projection_slot(p, actor, &slot) ||
      record == NULL)
    return result;
  MatchCell *cells = p->match_cells;
  MatchCell *cell = NULL;
  for (size_t i = 0; i < p->match_cell_count; i++)
    if (actor_eq(cells[i].actor, actor)) cell = &cells[i];
  if (cell == NULL) {
    if (p->match_cell_count == p->match_cell_capacity) {
      p->match_cell_capacity = p->match_cell_capacity == 0 ? 8 : p->match_cell_capacity * 2;
      p->match_cells = xrealloc(p->match_cells, p->match_cell_capacity * sizeof(MatchCell));
      cells = p->match_cells;
    }
    cell = &cells[p->match_cell_count++];
    memset(cell, 0, sizeof *cell);
    cell->provider = p;
    cell->actor = actor;
    cell->owned = owned;
    cell->record = record;
    cell->slot = slot;
    for (size_t i = 0; i < record->field_count; i++) {
      if (text_eq(record->fields[i].binding, "team")) cell->team = &record->fields[i];
      if (text_eq(record->fields[i].binding, "score")) cell->score = &record->fields[i];
    }
  }
  result.has_value = true;
  result.value.ctx = cell;
  result.value.owner = p->artifact->module.id;
  result.value.team = match_team;
  result.value.score = match_score;
  result.value.set_team = match_set_team;
  result.value.set_score = match_set_score;
  return result;
}
void qa_qvm_mod_provider_activate_protection(qa_qvm_mod_provider *p) {
  if (p->objectives != NULL) qa_mod_source_objectives_activate(p->objectives);
  bool has_match_fields = false;
  for (size_t i = 0; i < p->declaration->actor_record_count; i++)
    for (size_t f = 0; f < p->declaration->actor_records[i].field_count; f++)
      if (text_eq(p->declaration->actor_records[i].fields[f].binding, "team") ||
          text_eq(p->declaration->actor_records[i].fields[f].binding, "score"))
        has_match_fields = true;
  if (!p->has_release_match && has_match_fields) {
    const qa_source_match_services *match = p->services->match;
    if (match == NULL) qa_range_error("Declared match fields require destination match services");
    p->release_match = match->bind_source(match->ctx, p->artifact->module.id, match_resolve, p);
    p->has_release_match = true;
  }
  for (size_t i = 0; i < p->protection_count; i++)
    qa_qvm_mod_protection_activate(p->protection_channels[i]);
  qa_qvm_mod_pickups_activate(&p->pickups);
}
typedef struct {
  P *provider;
  double time, elapsed;
} FrameCtx;
static void frame_invoke_actor(void *ctx, const qa_qvm_mod_source_call *call, qa_actor_id actor) {
  FrameCtx *frame = ctx;
  qa_qvm_mod_provider_input inputs[3];
  inputs[0].name = "self";
  inputs[0].value.kind = "actor";
  inputs[0].value.actor_value = &actor;
  inputs[1].name = "time";
  inputs[1].value.kind = "float";
  inputs[1].value.number_value = frame->time;
  inputs[2].name = "elapsed";
  inputs[2].value.kind = "float";
  inputs[2].value.number_value = frame->elapsed;
  (void)qa_qvm_mod_provider_invoke(frame->provider, call, inputs, 3);
}
static void actor_frame_invoke(void *ctx) {
  FrameCtx *frame = ctx;
  P *p = frame->provider;
  const qa_qvm_mod_source_actors *source = p->declaration->source_actors;
  if (source == NULL || source->frame == NULL)
    qa_range_error("Original actor frame declaration disappeared");
  qa_qvm_mod_provider_input inputs[2];
  inputs[0].name = "time";
  inputs[0].value.kind = "float";
  inputs[0].value.number_value = frame->time;
  inputs[1].name = "elapsed";
  inputs[1].value.kind = "float";
  inputs[1].value.number_value = frame->elapsed;
  (void)qa_qvm_mod_provider_invoke(p, &source->frame->call, inputs, 2);
}
void qa_qvm_mod_provider_advance(qa_qvm_mod_provider *p, qa_frame_context frame) {
  current(p);
  double time = frame.time.kind == QA_SOURCE_TIME_SECONDS ? frame.time.value : frame.time.value / 1000;
  double elapsed = frame.elapsed.kind == QA_SOURCE_TIME_SECONDS ? frame.elapsed.value
                                                                 : frame.elapsed.value / 1000;
  FrameCtx ctx = {p, time, elapsed};
  const qa_qvm_mod_source_actors *source = p->declaration->source_actors;
  if (source != NULL && source->update != NULL) {
    /* Owned actors in ascending projection slot order. */
    size_t count = p->owned_count;
    qa_actor_id *order = xrealloc(NULL, (count == 0 ? 1 : count) * sizeof *order);
    for (size_t i = 0; i < count; i++) order[i] = p->owned[i].id;
    for (size_t i = 1; i < count; i++)
      for (size_t j = i; j > 0; j--) {
        int64_t a = 0, b = 0;
        (void)projection_slot(p, order[j - 1], &a);
        (void)projection_slot(p, order[j], &b);
        if (a <= b) break;
        qa_actor_id swap = order[j - 1];
        order[j - 1] = order[j];
        order[j] = swap;
      }
    for (size_t i = 0; i < count; i++) {
      if (p->closed) { free(order); return; }
      if (is_live(p, order[i])) frame_invoke_actor(&ctx, source->update, order[i]);
    }
    free(order);
  }
  bool clients = true;
  if (p->has_actor_frame)
    clients = qa_qvm_mod_actor_frame_runtime_run(&p->actor_frame, actor_frame_invoke, &ctx);
  if (!p->closed && clients && p->has_client_bindings)
    qa_qvm_mod_client_bindings_frame(&p->client_bindings, frame_invoke_actor, &ctx);
  if (!p->closed) publish(p);
}
const qa_session_simulation_presentation *qa_qvm_mod_provider_presentations(
    qa_qvm_mod_provider *p, size_t *out_count) {
  if (p->projection_count > p->presentations_scratch_capacity) {
    p->presentations_scratch =
        xrealloc(p->presentations_scratch, p->projection_count * sizeof *p->presentations_scratch);
    p->presentations_scratch_capacity = p->projection_count;
  }
  size_t count = 0;
  for (size_t i = 0; i < p->projection_count; i++) {
    qa_actor_id actor = p->projections[i].actor;
    int64_t slot = p->projections[i].slot;
    if (!owned_has(p, actor)) continue;
    qa_qvm_shared_entity entity;
    entity_of(p, slot, &entity);
    qa_qvm_entity_collision_model model = qa_qvm_shared_entity_get_model(&entity);
    qa_qvmy_entity_state_fields fields;
    entity_fields(p, slot, &fields);
    const char *path = NULL;
    char inline_path[32];
    if (text_eq(model.kind, "inline")) {
      (void)inline_model_of(p, model.index, NULL, NULL, NULL);
      snprintf(inline_path, sizeof inline_path, "*%d", fields.modelindex);
      path = inline_path;
    } else {
      path = configstring_of(p, 32 + fields.modelindex);
    }
    if (!qa_qvm_shared_entity_get_linked(&entity) ||
        (qa_qvm_shared_entity_get_sv_flags(&entity) & 1) != 0 || fields.modelindex == 0 ||
        path == NULL || path[0] == '\0')
      continue;
    /* The landed presentation shape carries only actor/viewWeapon;
     * path/frame/origin/etc. stay unresolved (header note). */
    p->presentations_scratch[count].actor = actor;
    p->presentations_scratch[count].view_weapon = false;
    p->presentations_scratch[count].opaque = NULL;
    count++;
  }
  *out_count = count;
  return p->presentations_scratch;
}

/* ---- subsystem adapters ---- */
static qa_actor_id reference_saved(P *p, qa_saved_actor_id saved) {
  if (p->services->reference_saved != NULL)
    return p->services->reference_saved(p->services->ctx, saved);
  return qa_session_actor_registry_reference_saved(p->services->actors, saved, "current");
}
static bool objectives_current(void *ctx) { return !((P *)ctx)->closed; }
static qa_qvm_mod_objective_address objective_scalar_address(
    const qa_qvm_mod_objective_scalar *storage) {
  qa_qvm_mod_objective_address address;
  memset(&address, 0, sizeof address);
  address.is_global_pointer = storage->address_is_global_pointer;
  address.address = storage->address;
  address.pointer.is_global = storage->address_is_global_pointer;
  address.pointer.address = storage->address;
  address.pointer.indirections = storage->indirections;
  address.pointer.offset = storage->offset;
  return address;
}
static const char *objectives_location(void *ctx, const qa_source_objective_declaration *declaration) {
  P *p = ctx;
  const qa_qvm_mod_objective_scalar *storage = declaration->state_storage;
  qa_qvm_mod_objective_address state = objective_scalar_address(storage);
  const qa_qvm_mod_objective_address *addresses[3] = {&state, declaration->carrier,
                                                      declaration->target};
  free(p->objective_location);
  p->objective_location = xrealloc(NULL, 128);
  int written = 0;
  for (size_t i = 0; i < 3; i++) {
    if (i != 0) written += snprintf(p->objective_location + written, 128 - (size_t)written, "|");
    if (addresses[i] == NULL)
      written += snprintf(p->objective_location + written, 128 - (size_t)written, "none");
    else
      written += snprintf(p->objective_location + written, 128 - (size_t)written, "%lld",
                           (long long)objective_address_of(p, addresses[i]));
  }
  return p->objective_location;
}
static double objectives_read_scalar(void *ctx, const void *storage_opaque) {
  P *p = ctx;
  const qa_qvm_mod_objective_scalar *storage = storage_opaque;
  qa_qvm_mod_objective_address address = objective_scalar_address(storage);
  qa_qvm_writable_view view;
  view_of(p, objective_address_of(p, &address), 4, &view);
  return text_eq(storage->encoding, "float32")
             ? (double)qa_qvm_writable_view_get_float32(&view, 0, true)
             : (double)qa_qvm_writable_view_get_int32(&view, 0, true);
}
static void objectives_write_scalar(void *ctx, const void *storage_opaque, double value) {
  P *p = ctx;
  const qa_qvm_mod_objective_scalar *storage = storage_opaque;
  qa_qvm_mod_objective_address address = objective_scalar_address(storage);
  write_word(p, objective_address_of(p, &address), scalar_of(value, storage->encoding));
}
static bool objectives_read_actor(void *ctx, const void *storage_opaque, qa_actor_id *out) {
  P *p = ctx;
  const qa_qvm_mod_objective_address *address = storage_opaque;
  int32_t pointer = read_word(p, objective_address_of(p, address));
  if (pointer == 0) return false;
  if (!actor_at(p, pointer_slot(p, pointer), out) || !is_live(p, *out))
    qa_range_error("QVM objective references an absent source actor");
  return true;
}
static void objectives_write_actor(void *ctx, const void *storage_opaque,
                                   const qa_actor_id *actor) {
  P *p = ctx;
  const qa_qvm_mod_objective_address *address = storage_opaque;
  if (actor != NULL && p->declaration->entity_record == NULL)
    qa_range_error("QVM objective requires its declared entity record");
  int32_t pointer = actor == NULL || p->declaration->entity_record == NULL
                        ? 0 : (int32_t)pointer_for(p, actor, p->declaration->entity_record);
  write_word(p, objective_address_of(p, address), pointer);
}
static void objectives_invoke(void *ctx, const void *call,
                              const qa_mod_objective_input_entry *inputs, size_t count) {
  (void)qa_qvm_mod_provider_invoke(ctx, call, (const qa_qvm_mod_provider_input *)inputs, count);
}
static double objectives_seconds(void *ctx) { return seconds_of(((P *)ctx)->services); }

static qa_mod_client_output_lease outputs_claim(void *ctx, qa_provider_id owner,
                                                const qa_mod_client_output_channel *channels,
                                                size_t channel_count) {
  P *p = ctx;
  return p->services->clients->claim_outputs(p->services->clients->ctx, owner, channels,
                                             channel_count);
}
static double outputs_scalar(void *ctx, qa_actor_id actor, const void *field_opaque) {
  P *p = ctx;
  const qa_qvm_mod_protection_scalar *field = field_opaque;
  qa_qvm_mod_record_offset offset = {field->record, field->offset};
  return read_scalar_at(p, client_output_address(p, actor, &offset), field->encoding);
}
static qa_vec3 outputs_vector(void *ctx, qa_actor_id actor, const void *field_opaque) {
  return read_vector(ctx, client_output_address(ctx, actor, field_opaque));
}

static bool actors_owned_has(void *ctx, qa_actor_id actor) { return owned_has(ctx, actor); }
static int32_t actors_pointer(void *ctx, const qa_actor_id *actor) {
  P *p = ctx;
  if (p->declaration->entity_record == NULL)
    qa_range_error("QVM actor semantics require an entity record");
  return (int32_t)pointer_for(p, actor, p->declaration->entity_record);
}
static bool actors_actor(void *ctx, int32_t pointer, qa_actor_id *out) {
  P *p = ctx;
  if (pointer == 0) return false;
  int64_t slot = pointer_slot(p, pointer);
  if (actor_at(p, slot, out)) return true;
  if (slot == 1022 && p->services->engine != NULL && p->services->engine->world != NULL)
    return p->services->engine->world(p->services->engine->ctx, out);
  return false;
}
static int32_t actors_invoke(void *ctx, const qa_qvm_mod_source_call *call,
                             const qa_qvm_mod_actor_input *inputs, size_t input_count) {
  return invoke_adapter(ctx, call, inputs, input_count);
}
static int32_t actors_scratch(void *ctx, int64_t size, qa_qvm_mod_actor_scratch_fn execute,
                              void *execute_ctx) {
  P *p = ctx;
  int64_t previous = p->scratch;
  int32_t result = execute(execute_ctx, (int32_t)allocate_scratch(p, size));
  p->scratch = previous;
  return result;
}

static int64_t input_pointer(void *ctx, qa_actor_id actor, const char *record) {
  return pointer_for(ctx, &actor, record);
}
static bool input_live(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  return !p->closed && is_live(p, actor) && !retired_has(p, actor) &&
         p->has_client_bindings && qa_qvm_mod_client_bindings_live(&p->client_bindings, actor);
}
static void input_player_state(void *ctx, qa_actor_id actor, qa_q3_player_state *out) {
  P *p = ctx;
  qa_qvm_writable_view view;
  view_of(p, player_address(p, actor), qa_qvm_player_state_bytes(p->declaration->abi_profile), &view);
  qa_qvm_read_player_state(&view, p->declaration->abi_profile, out);
}

static void clients_project(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  for (size_t i = 0; i < p->protection_count; i++)
    qa_qvm_mod_protection_reserve_actor(p->protection_channels[i], actor);
  if (p->declaration->entity_record == NULL) qa_range_error("Missing QVM client entity record");
  (void)pointer_for(p, &actor, p->declaration->entity_record);
  if (p->has_player_events)
    qa_qvm_mod_player_events_track(&p->player_events, actor, player_address(p, actor), NULL);
}
static void clients_admitted(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  if (p->client_outputs != NULL) qa_source_mod_client_outputs_publish(p->client_outputs, actor);
  if (p->has_items) qa_qvm_mod_items_admit(&p->items, actor);
  for (size_t i = 0; i < p->protection_count; i++)
    qa_qvm_mod_protection_bind_actor(p->protection_channels[i], actor);
  qa_qvm_mod_pickups_bind_actor(&p->pickups, actor);
}
static void clients_release(void *ctx, qa_actor_id actor) { release_projection(ctx, actor); }
typedef struct {
  P *provider;
  const qa_qvm_mod_source_call *call;
  qa_actor_id actor;
  qa_mod_client_application *application;
} ClientInvoke;
static int32_t client_run(void *ctx) {
  ClientInvoke *run = ctx;
  P *p = run->provider;
  if (run->application == NULL) {
    qa_qvm_mod_provider_input inputs[2];
    inputs[0].name = "self";
    inputs[0].value.kind = "actor";
    inputs[0].value.actor_value = &run->actor;
    inputs[1].name = "time";
    inputs[1].value.kind = "float";
    inputs[1].value.number_value = seconds_of(p->services);
    (void)qa_qvm_mod_provider_invoke(p, run->call, inputs, 2);
  } else {
    qa_mod_client_input_values values = qa_mod_client_input_values_of(run->application);
    (void)qa_qvm_mod_provider_invoke(p, run->call,
                                     (const qa_qvm_mod_provider_input *)values.entries,
                                     values.count);
  }
  return 0;
}
static void clients_invoke(void *ctx, const qa_qvm_mod_source_call *call, qa_actor_id actor,
                           qa_mod_client_application *application) {
  P *p = ctx;
  ClientInvoke run = {p, call, actor, application};
  if (application != NULL && p->has_items && qa_qvm_mod_items_applies(&p->items, call)) {
    if (p->weapon_input_count == p->weapon_input_capacity) {
      p->weapon_input_capacity = p->weapon_input_capacity == 0 ? 8 : p->weapon_input_capacity * 2;
      p->weapon_inputs = xrealloc(p->weapon_inputs, p->weapon_input_capacity * sizeof(WeaponInput));
    }
    WeaponInput *scope = &weapon_inputs(p)[p->weapon_input_count++];
    scope->actor = actor;
    scope->frame = NULL;
    (void)qa_qvm_mod_items_apply(&p->items, application, client_run, &run);
    p->weapon_input_count--;
  } else {
    (void)client_run(&run);
  }
}
typedef struct {
  void (*input_remove)(void *ctx);
  void *input_ctx;
  void (*items_remove)(void *ctx);
  void *items_ctx;
} OpenCleanup;
static void open_cleanup_run(void *ctx) {
  OpenCleanup *cleanup = ctx;
  if (cleanup->items_remove != NULL) cleanup->items_remove(cleanup->items_ctx);
  if (cleanup->input_remove != NULL) cleanup->input_remove(cleanup->input_ctx);
  free(cleanup);
}
static void clients_open_input(void *ctx, qa_mod_client_application *application,
                               void (**cleanup)(void *cleanup_ctx), void **cleanup_ctx) {
  P *p = ctx;
  OpenCleanup *cell = xrealloc(NULL, sizeof *cell);
  memset(cell, 0, sizeof *cell);
  qa_qvm_mod_input_open(&p->input, application, &cell->input_remove, &cell->input_ctx);
  if (p->has_items)
    qa_qvm_mod_items_open(&p->items, application, &cell->items_remove, &cell->items_ctx);
  *cleanup = open_cleanup_run;
  *cleanup_ctx = cell;
}
static void clients_output(void *ctx, const qa_qvm_mod_input_output *outputs, size_t output_count,
                           qa_mod_client_application *application, qa_qvm_client_invoke_fn run,
                           void *run_ctx, qa_mod_client_input_output **out_outputs,
                           size_t *out_count) {
  P *p = ctx;
  *out_outputs = qa_qvm_mod_input_run_output(&p->input, outputs, output_count, application, run,
                                             run_ctx, out_count);
}
static size_t clients_reserved(void *ctx, int64_t *out, size_t capacity) {
  P *p = ctx;
  size_t count = 0;
  for (size_t i = 0; i < p->projection_count; i++) {
    if (out != NULL && count < capacity) out[count] = p->projections[i].slot;
    count++;
  }
  return count;
}
static void clients_player_state(void *ctx, qa_actor_id actor, qa_q3_player_state *out) {
  input_player_state(ctx, actor, out);
}
static bool clients_requested(void *ctx, qa_actor_id actor, double *out) {
  P *p = ctx;
  if (!p->has_items) return false;
  int64_t value;
  if (!qa_qvm_mod_items_requested(&p->items, actor, &value)) return false;
  *out = (double)value;
  return true;
}
static void clients_send(void *ctx, const char *text, const qa_actor_id *recipient) {
  P *p = ctx;
  if (p->services->engine == NULL)
    qa_range_error("QVM server command requires destination presentation");
  scene_command_push(p, text, recipient);
  /* Destination event emission is unresolved (opaque events). */
  emit_source_event(p);
}

static void protection_current(void *ctx) { current(ctx); }
static bool protection_eligible(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  return projection_has(p, actor) && !retired_has(p, actor) && p->has_client_bindings &&
         qa_qvm_mod_client_bindings_admitted(&p->client_bindings, actor);
}
static int64_t protection_pointer(void *ctx, qa_actor_id actor, const char *record) {
  P *p = ctx;
  if (!projection_has(p, actor))
    qa_range_error("QVM protection source projection is unavailable");
  return pointer_for(p, &actor, record);
}
static int32_t protection_invoke(void *ctx, const qa_qvm_mod_source_call *call,
                                 const qa_qvm_protection_input *inputs, size_t input_count) {
  return invoke_adapter(ctx, call, (const qa_qvm_mod_provider_input *)inputs, input_count);
}
static size_t protection_pickups(void *ctx, qa_actor_id actor, qa_protection_channel channel,
                                 const qa_original_pickup_rule **out, size_t capacity) {
  return qa_qvm_mod_pickups_protection(&((P *)ctx)->pickups, actor, channel, out, capacity);
}

static bool pickups_eligible(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  return !p->closed && projection_has(p, actor) && !retired_has(p, actor) &&
         p->has_client_bindings && qa_qvm_mod_client_bindings_admitted(&p->client_bindings, actor);
}
static qa_original_pickup_decision pickups_context(
    void *ctx, const qa_qvm_mod_pickup *definition, const qa_original_pickup_offer *offer,
    const qa_qvm_pickup_input *inputs, size_t input_count, qa_qvm_mod_pickup_execute_fn execute,
    void *execute_ctx) {
  return pickup_context(ctx, definition, offer, inputs, input_count, execute, execute_ctx);
}
typedef struct {
  P *provider;
  qa_actor_id actor;
  const qa_original_pickup_execution *execution;
  qa_qvm_mod_pickup_execute_fn execute;
  void *execute_ctx;
} PickupObserve;
static qa_armor_stage_result pickup_observe_execute(void *ctx) {
  PickupObserve *observe = ctx;
  qa_original_pickup_decision decision =
      observe_pickup(observe->provider, observe->actor, observe->execution, observe->execute,
                     observe->execute_ctx);
  qa_armor_stage_result result = {(double)decision};
  return result;
}
static qa_original_pickup_decision pickups_observe(
    void *ctx, qa_actor_id actor, const qa_original_pickup_execution *execution,
    qa_qvm_mod_pickup_execute_fn execute, void *execute_ctx) {
  P *p = ctx;
  PickupObserve observe = {p, actor, execution, execute, execute_ctx};
  if (p->protection_count == 0)
    return observe_pickup(p, actor, execution, execute, execute_ctx);
  qa_protection_observer observer = execution->observer;
  qa_armor_stage_result result = qa_qvm_mod_protection_observe(
      p->protection_channels[0], actor, observer, pickup_observe_execute, &observe);
  return (qa_original_pickup_decision)result.saved;
}
static int32_t pickups_invoke(void *ctx, const qa_qvm_mod_source_call *call,
                              const qa_qvm_pickup_input *inputs, size_t input_count) {
  return invoke_adapter(ctx, call, (const qa_qvm_mod_provider_input *)inputs, input_count);
}

static bool items_pickup(void *ctx, qa_original_pickup_execution *out) {
  PickupScope *scope = pickup_scope(ctx);
  if (scope == NULL) return false;
  *out = scope->execution;
  return true;
}
static int64_t items_pointer(void *ctx, qa_actor_id actor, const char *record) {
  P *p = ctx;
  if (!projection_has(p, actor)) qa_range_error("QVM item actor has no source projection");
  return pointer_for(p, &actor, record);
}
static bool items_live(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  return !p->closed && is_live(p, actor) && projection_has(p, actor) && !retired_has(p, actor) &&
         p->has_client_bindings && qa_qvm_mod_client_bindings_admitted(&p->client_bindings, actor);
}
static int32_t items_invoke(void *ctx, qa_actor_id actor, const qa_qvm_mod_source_call *call) {
  P *p = ctx;
  qa_qvm_mod_provider_input inputs[2];
  inputs[0].name = "self";
  inputs[0].value.kind = "actor";
  inputs[0].value.actor_value = &actor;
  inputs[1].name = "time";
  inputs[1].value.kind = "float";
  inputs[1].value.number_value = seconds_of(p->services);
  return invoke_adapter(p, call, inputs, 2);
}
static int32_t items_ground(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  qa_body_state_result body = qa_shared_body_table_read(p->services->bodies, actor);
  qa_client_id client;
  bool has_client = p->services->clients != NULL &&
                    p->services->clients->for_actor(p->services->clients->ctx, actor, &client);
  if (!body.has_value || !has_client)
    qa_range_error("QVM weapon posture lost its authoritative client");
  if (body.value.ground != NULL) {
    qa_actor_id world;
    bool has_world = p->services->engine != NULL && p->services->engine->world != NULL &&
                     p->services->engine->world(p->services->engine->ctx, &world);
    if (!has_world || !actor_eq(world, *body.value.ground))
      return (int32_t)entity_slot(p, *body.value.ground);
  }
  bool grounded = p->services->clients->grounded != NULL &&
                  p->services->clients->grounded(p->services->clients->ctx, client);
  return grounded ? 1022 : 1023;
}

static qa_actor_id events_reference_saved(void *ctx, qa_saved_actor_id saved) {
  return reference_saved(ctx, saved);
}
static bool events_live(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  return !p->closed && is_live(p, actor) && projection_has(p, actor) && !retired_has(p, actor) &&
         p->has_client_bindings && qa_qvm_mod_client_bindings_live(&p->client_bindings, actor);
}
static qa_vec3 events_origin(void *ctx, qa_actor_id actor) {
  P *p = ctx;
  qa_body_state_result body = qa_shared_body_table_read(p->services->bodies, actor);
  if (!body.has_value) qa_range_error("QVM player event requires a live destination body");
  return body.value.origin;
}
static double events_time(void *ctx) {
  return trunc(seconds_of(((P *)ctx)->services) * 1000);
}
static void events_emit(void *ctx, const qa_qvmy_source_player_event *event) {
  (void)event;
  emit_source_event(ctx);
}

static bool actor_frame_owned(void *ctx, int32_t pointer) {
  P *p = ctx;
  const qa_qvm_mod_actor_record *record = entity_record_of(p);
  int64_t offset = pointer - (int64_t)record->address;
  if (offset < 0 || offset % (int64_t)record->stride != 0 ||
      offset / (int64_t)record->stride >= (int64_t)record->capacity)
    qa_range_error("Original actor loop escaped its declared source array");
  qa_actor_id actor;
  if (!actor_at(p, offset / (int64_t)record->stride, &actor)) return false;
  qa_owned_actor owner;
  if (!owned_get(p, actor, &owner)) return false;
  qa_owned_actor resolved;
  return resolve_owned(p, actor, &resolved) && actor_eq(resolved.id, owner.id) &&
         !retired_has(p, owner.id);
}

/* Mirrored subsystem services built from ModHostServices. */
static size_t service_clients(void *ctx, qa_actor_id *out, size_t capacity) {
  P *p = ctx;
  size_t count = 0;
  const qa_mod_client_identity *identities =
      p->services->clients->clients(p->services->clients->ctx, &count);
  for (size_t i = 0; i < count && i < capacity; i++) out[i] = identities[i].actor;
  return count;
}
static bool service_for_actor(void *ctx, qa_actor_id actor, qa_client_id *out) {
  P *p = ctx;
  return p->services->clients->for_actor(p->services->clients->ctx, actor, out);
}
static bool service_actor(void *ctx, qa_client_id client, qa_actor_id *out) {
  P *p = ctx;
  return p->services->clients->actor(p->services->clients->ctx, client, out);
}
static bool service_resolve_owned(void *ctx, qa_actor_id actor, qa_owned_actor *out) {
  return resolve_owned(ctx, actor, out);
}
static bool service_is_live(void *ctx, qa_actor_id actor) { return is_live(ctx, actor); }
static qa_source_time service_time(void *ctx) {
  P *p = ctx;
  return p->services->time(p->services->ctx);
}
static qa_protection_binding protection_binding_of(const qa_qvmy_protection_binding *binding,
                                                   qa_protection_channel channel,
                                                   const qa_item_id **items_out,
                                                   size_t *item_count_out) {
  qa_protection_binding result;
  memset(&result, 0, sizeof result);
  result.ctx = binding->ctx;
  result.claim.owner = binding->owner;
  result.claim.rule = binding->rule;
  result.claim.admission.kind = binding->admission.kind;
  result.claim.admission.owner = binding->admission.owner;
  result.channel = channel;
  result.pickups = binding->pickups;
  result.pickup_count = binding->pickup_count;
  result.read_regular = binding->read_regular;
  result.read_powered = binding->read_powered;
  result.validate_write_regular = binding->validate_regular;
  result.validate_write_powered = binding->validate_powered;
  result.write_regular = binding->write_regular;
  result.write_powered = binding->write_powered;
  result.absorb = binding->absorb;
  *items_out = NULL;
  *item_count_out = 0;
  (void)items_out;
  return result;
}
static size_t service_reserve_protection(void *ctx, qa_owned_actor owned,
                                         qa_protection_channel channel,
                                         const qa_qvmy_protection_binding *claim_binding) {
  P *p = ctx;
  const qa_item_id *items;
  size_t item_count;
  qa_protection_binding binding = protection_binding_of(claim_binding, channel, &items, &item_count);
  qa_protection_reservation reservation = qa_gameplay_authority_reserve_protection(
      p->services->combat, owned, channel, binding.claim);
  ReservationCell *cells = p->reservations;
  for (size_t i = 0; i < p->reservation_count; i++)
    if (!cells[i].used) {
      cells[i].used = true;
      cells[i].reservation = reservation;
      return i;
    }
  if (p->reservation_count == p->reservation_capacity) {
    p->reservation_capacity = p->reservation_capacity == 0 ? 8 : p->reservation_capacity * 2;
    p->reservations = xrealloc(p->reservations, p->reservation_capacity * sizeof(ReservationCell));
    cells = p->reservations;
  }
  size_t id = p->reservation_count++;
  cells[id].used = true;
  cells[id].reservation = reservation;
  return id;
}
static void service_bind_reservation(void *ctx, size_t id,
                                     const qa_qvmy_protection_binding *binding) {
  P *p = ctx;
  ReservationCell *cells = p->reservations;
  qa_protection_reservation *reservation = &cells[id].reservation;
  const qa_item_id *items;
  size_t item_count;
  qa_protection_binding converted =
      protection_binding_of(binding, reservation->channel, &items, &item_count);
  converted.inventory_items = items;
  converted.inventory_item_count = item_count;
  (void)reservation->bind(reservation->ctx, converted);
}
static void service_close_reservation(void *ctx, size_t id) {
  P *p = ctx;
  ReservationCell *cells = p->reservations;
  cells[id].reservation.close(cells[id].reservation.ctx);
  cells[id].used = false;
}
static size_t service_bind_pickup(void *ctx, qa_owned_actor owned, qa_provider_id owner,
                                  const qa_original_pickup_rule *rules, size_t rule_count) {
  P *p = ctx;
  qa_inventory_pickup_binding binding = {owner, rules, rule_count};
  int64_t id = qa_shared_inventory_table_bind_pickup(p->services->inventory, owned, binding);
  PickupBindingCell *cells = p->pickup_bindings;
  for (size_t i = 0; i < p->pickup_binding_count; i++)
    if (!cells[i].used) {
      cells[i].used = true;
      cells[i].actor = owned;
      cells[i].id = id;
      return i;
    }
  if (p->pickup_binding_count == p->pickup_binding_capacity) {
    p->pickup_binding_capacity = p->pickup_binding_capacity == 0 ? 8 : p->pickup_binding_capacity * 2;
    p->pickup_bindings = xrealloc(p->pickup_bindings, p->pickup_binding_capacity * sizeof(PickupBindingCell));
    cells = p->pickup_bindings;
  }
  size_t index = p->pickup_binding_count++;
  cells[index].used = true;
  cells[index].actor = owned;
  cells[index].id = id;
  return index;
}
static void service_unbind_pickup(void *ctx, size_t id) {
  P *p = ctx;
  PickupBindingCell *cells = p->pickup_bindings;
  qa_shared_inventory_table_unbind_pickup(p->services->inventory, cells[id].actor, cells[id].id);
  cells[id].used = false;
}
static qa_actor_id service_reference_saved(void *ctx, qa_saved_actor_id saved) {
  return reference_saved(ctx, saved);
}
typedef struct {
  const qa_qvmy_item_lease_binding *binding;
  qa_inventory_entry *entries;
  size_t capacity;
} ItemStateCell;
static const qa_inventory_entry *item_state_read(void *ctx, size_t *out_count) {
  ItemStateCell *cell = ctx;
  size_t count = cell->binding->read(cell->binding->ctx, NULL, 0);
  if (count > cell->capacity) {
    cell->entries = xrealloc(cell->entries, count * sizeof *cell->entries);
    cell->capacity = count;
  }
  *out_count = cell->binding->read(cell->binding->ctx, cell->entries, cell->capacity);
  return cell->entries;
}
static bool item_state_entry(void *ctx, qa_item_id item, qa_inventory_entry *out) {
  ItemStateCell *cell = ctx;
  return cell->binding->entry(cell->binding->ctx, item, out);
}
static void item_state_write(void *ctx, qa_inventory_entry entry) {
  ItemStateCell *cell = ctx;
  cell->binding->write(cell->binding->ctx, entry);
}
static bool item_state_capacity(void *ctx, qa_item_id item) {
  ItemStateCell *cell = ctx;
  return cell->binding->mutable_capacity(cell->binding->ctx, item);
}
static qa_qvmy_item_lease service_bind_items(void *ctx, qa_owned_actor owner,
                                             const qa_qvmy_item_lease_binding *binding) {
  P *p = ctx;
  ItemStateCell *cell = xrealloc(NULL, sizeof *cell);
  memset(cell, 0, sizeof *cell);
  cell->binding = binding;
  qa_inventory_item_binding converted;
  memset(&converted, 0, sizeof converted);
  converted.owner = binding->owner;
  converted.items = (const qa_source_item_admission *)binding->items;
  converted.item_count = binding->item_count;
  converted.state.ctx = cell;
  converted.state.read = item_state_read;
  converted.state.entry = item_state_entry;
  converted.state.write = item_state_write;
  converted.state.mutable_capacity = item_state_capacity;
  converted.invoke_ctx = binding->ctx;
  converted.invoke = binding->invoke;
  qa_source_item_lease lease =
      qa_shared_inventory_table_bind_items(p->services->inventory, owner, converted);
  qa_qvmy_item_lease result = {lease.ctx, lease.current, lease.stored, lease.close};
  return result;
}
static double service_inventory_count(void *ctx, qa_actor_id actor, qa_item_id item) {
  return qa_shared_inventory_table_count(((P *)ctx)->services->inventory, actor, item);
}
static bool service_has_weapons(void *ctx) { return ((P *)ctx)->services->weapons != NULL; }
static bool service_weapon_selected(void *ctx, qa_actor_id actor, qa_provider_id provider) {
  P *p = ctx;
  return p->services->weapons->selected(p->services->weapons->ctx, actor, provider);
}
static bool service_weapon_bind(void *ctx, qa_owned_actor owner, qa_source_weapon_binding binding,
                                qa_source_weapon_unbind *out) {
  P *p = ctx;
  if (p->services->weapons == NULL) return false;
  *out = p->services->weapons->bind(p->services->weapons->ctx, owner, binding);
  return true;
}
static bool service_body_read(void *ctx, qa_actor_id actor, qa_body_snapshot *out) {
  P *p = ctx;
  qa_body_state_result body = qa_shared_body_table_read(p->services->bodies, actor);
  if (!body.has_value) return false;
  out->actor = actor;
  out->body = body.value;
  return true;
}
static bool service_player_view(void *ctx, qa_actor_id actor, qa_mod_client_player_view *out) {
  P *p = ctx;
  qa_client_id client;
  if (p->services->clients == NULL || p->services->clients->player_view == NULL ||
      !p->services->clients->for_actor(p->services->clients->ctx, actor, &client))
    return false;
  *out = p->services->clients->player_view(p->services->clients->ctx, client);
  return true;
}

/* ---- host checkpoint encoding ---- */
static qa_save_value *save_object(void) {
  qa_save_value *value = qa_save_value_object();
  if (value == NULL) qa_range_error("QVM memory allocation failed");
  return value;
}
static qa_save_value *save_array(void) {
  qa_save_value *value = qa_save_value_array();
  if (value == NULL) qa_range_error("QVM memory allocation failed");
  return value;
}
static void save_set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) {
    qa_save_value_free(child);
    qa_range_error("QVM memory allocation failed");
  }
}
static void save_push(qa_save_value *array, qa_save_value *child) {
  if (child == NULL || !qa_save_value_push(array, child)) {
    qa_save_value_free(child);
    qa_range_error("QVM memory allocation failed");
  }
}
static qa_save_value *saved_actor_value(qa_saved_actor_id saved) {
  qa_save_value *value = save_object();
  save_set(value, "slot", qa_save_value_number(saved.slot));
  save_set(value, "generation", qa_save_value_number(saved.generation));
  return value;
}
static qa_save_value *mount_value(const qa_mount_identity *identity, bool archive,
                                  qa_archive_format format, const char *path,
                                  qa_content_digest digest) {
  qa_save_value *value = save_object();
  qa_save_value *identity_value = save_object();
  save_set(identity_value, "id", qa_save_value_string(identity->id));
  save_set(identity_value, "content", qa_save_value_string(identity->content));
  save_set(identity_value, "generation", qa_save_value_number((double)identity->generation));
  save_set(value, "identity", identity_value);
  save_set(value, "kind", qa_save_value_string(archive ? "archive" : "loose"));
  if (archive) {
    const char *formats[] = {"pak", "pk3", "kpf", "zip"};
    save_set(value, "format", qa_save_value_string(formats[format]));
    save_set(value, "archivePath", qa_save_value_string(path));
    save_set(value, "archiveDigest", qa_save_value_string(digest));
  } else {
    save_set(value, "rootPath", qa_save_value_string(path));
  }
  return value;
}
/* Inverse of persistence/recipe.ts qa_read_resource (no landed
 * writer exists; formats mirrored from the reader). */
static qa_save_value *resource_value(const qa_resolved_resource_reference *resource) {
  qa_save_value *value = save_object();
  save_set(value, "id", qa_save_value_string(resource->id));
  save_set(value, "requestedPath", qa_save_value_string(resource->requested_path));
  qa_save_value *provenance = save_object();
  save_set(provenance, "memberPath", qa_save_value_string(resource->provenance.member_path));
  if (resource->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE) {
    const qa_archive_mount *mount = resource->provenance.archive_mount;
    save_set(provenance, "kind", qa_save_value_string("archive"));
    save_set(provenance, "mount",
             mount_value(&mount->identity, true, mount->format, mount->archive_path,
                         mount->archive_digest));
    save_set(provenance, "memberIndex",
             qa_save_value_number((double)resource->provenance.member_index));
  } else {
    const qa_loose_mount *mount = resource->provenance.loose_mount;
    save_set(provenance, "kind", qa_save_value_string("loose"));
    save_set(provenance, "mount",
             mount_value(&mount->identity, false, QA_ARCHIVE_FORMAT_PAK, mount->root_path, NULL));
  }
  save_set(value, "provenance", provenance);
  save_set(value, "digest", qa_save_value_string(resource->digest));
  save_set(value, "byteLength", qa_save_value_number((double)resource->byte_length));
  qa_save_value *resolution = save_object();
  save_set(resolution, "plan", qa_save_value_string(resource->resolution.plan));
  switch (resource->resolution.kind) {
    case QA_RESOURCE_RESOLUTION_DEFAULT_ORDER:
      save_set(resolution, "kind", qa_save_value_string("default-order"));
      save_set(resolution, "rank", qa_save_value_number((double)resource->resolution.rank));
      break;
    case QA_RESOURCE_RESOLUTION_PREFIX_ORDER:
      save_set(resolution, "kind", qa_save_value_string("prefix-order"));
      save_set(resolution, "rank", qa_save_value_number((double)resource->resolution.rank));
      save_set(resolution, "prefix", qa_save_value_string(resource->resolution.prefix));
      break;
    case QA_RESOURCE_RESOLUTION_LINK:
      save_set(resolution, "kind", qa_save_value_string("link"));
      save_set(resolution, "sourcePrefix", qa_save_value_string(resource->resolution.source_prefix));
      save_set(resolution, "targetPath", qa_save_value_string(resource->resolution.target_path));
      break;
  }
  save_set(value, "resolution", resolution);
  return value;
}
static qa_save_value *files_value(P *p) {
  if (p->files == NULL) return qa_save_value_null();
  qa_qvm_files_checkpoint checkpoint;
  if (!qa_qvm_files_capture_checkpoint(p->files, &checkpoint))
    qa_range_error("QVM memory allocation failed");
  qa_save_value *value = save_object();
  qa_save_value *handles = save_array();
  for (size_t i = 0; i < checkpoint.handle_count; i++) {
    const qa_qvm_file_handle_checkpoint *entry = &checkpoint.handles[i];
    qa_save_value *handle = save_object();
    save_set(handle, "slot", qa_save_value_number(entry->slot));
    save_set(handle, "kind", qa_save_value_string(entry->is_write ? "write" : "read"));
    if (entry->is_write) {
      qa_save_value *file = save_object();
      save_set(file, "path", qa_save_value_string(entry->file.path));
      const char *mode = entry->file.mode == QA_WRITABLE_WRITE ? "write"
                         : entry->file.mode == QA_WRITABLE_APPEND ? "append" : "append-sync";
      save_set(file, "mode", qa_save_value_string(mode));
      save_set(file, "position", qa_save_value_number((double)entry->file.position));
      save_set(handle, "file", file);
    } else {
      qa_save_value *resource = save_object();
      save_set(resource, "reference", resource_value(&entry->reference));
      save_set(resource, "bytes", qa_save_value_bytes(entry->bytes, entry->byte_length));
      save_set(handle, "resource", resource);
      save_set(handle, "position", qa_save_value_number(entry->position));
    }
    save_push(handles, handle);
  }
  save_set(value, "handles", handles);
  qa_qvm_files_checkpoint_destroy(&checkpoint);
  return value;
}
static qa_save_value *player_events_value(P *p) {
  qa_qvm_player_events_checkpoint checkpoint;
  qa_qvm_mod_player_events_checkpoint(&p->player_events, &checkpoint);
  qa_save_value *value = save_object();
  save_set(value, "nextOrder", qa_save_value_number((double)checkpoint.next_order));
  qa_save_value *clients = save_array();
  for (size_t i = 0; i < checkpoint.client_count; i++) {
    const qa_qvm_saved_event_cursor *cursor = &checkpoint.clients[i];
    qa_save_value *entry = save_object();
    save_set(entry, "actor", saved_actor_value(cursor->actor));
    save_set(entry, "external", qa_save_value_number(cursor->external));
    save_set(entry, "externalTime", qa_save_value_number(cursor->external_time));
    save_set(entry, "sequence", qa_save_value_number(cursor->sequence));
    save_set(entry, "observedSequence", qa_save_value_number(cursor->observed_sequence));
    save_set(entry, "externalOrder",
             cursor->has_external_order ? qa_save_value_number((double)cursor->external_order)
                                        : qa_save_value_null());
    qa_save_value *predictable = save_array();
    for (size_t j = 0; j < cursor->predictable_count; j++) {
      qa_save_value *item = save_object();
      save_set(item, "sequence", qa_save_value_number(cursor->predictable[j].sequence));
      save_set(item, "order", qa_save_value_number((double)cursor->predictable[j].order));
      save_push(predictable, item);
    }
    save_set(entry, "predictable", predictable);
    save_push(clients, entry);
  }
  save_set(value, "clients", clients);
  qa_qvm_player_events_checkpoint_free(&checkpoint);
  return value;
}
static qa_save_value *game_state_value(const qa_q3_source_game_state_record *state) {
  qa_qvm_presentation_game_state captured;
  qa_qvm_capture_presentation_game_state(state, &captured);
  qa_save_value *value = save_object();
  qa_save_value *offsets = save_array();
  for (size_t i = 0; i < captured.string_offset_count; i++)
    save_push(offsets, qa_save_value_number(captured.string_offsets[i]));
  save_set(value, "stringOffsets", offsets);
  save_set(value, "stringData", qa_save_value_bytes(captured.string_data, captured.string_data_length));
  save_set(value, "dataCount", qa_save_value_number((double)captured.data_count));
  qa_qvm_presentation_game_state_free(&captured);
  return value;
}
static qa_qvm_host_checkpoint host_checkpoint(void *ctx) {
  P *p = ctx;
  qa_save_value *root = save_object();
  save_set(root, "version", qa_save_value_number(1));
  qa_save_value *projections = save_array();
  for (size_t i = 0; i < p->projection_count; i++) {
    qa_save_value *entry = save_object();
    save_set(entry, "actor", saved_actor_value(qa_saved_actor_id_of(p->projections[i].actor)));
    save_set(entry, "slot", qa_save_value_number((double)p->projections[i].slot));
    save_set(entry, "owned", qa_save_value_boolean(owned_has(p, p->projections[i].actor)));
    const char *event = event_key_get(p, p->projections[i].actor);
    save_set(entry, "event", event == NULL ? qa_save_value_null() : qa_save_value_string(event));
    save_push(projections, entry);
  }
  save_set(root, "projections", projections);
  save_set(root, "nextSlot", qa_save_value_number((double)p->next_slot));
  qa_save_value *client_slots = save_array();
  if (p->has_client_bindings) {
    size_t count = qa_qvm_mod_client_bindings_checkpoint(&p->client_bindings, NULL, 0);
    if (count > p->client_scratch_capacity) {
      p->client_scratch = xrealloc(p->client_scratch, count * sizeof *p->client_scratch);
      p->client_scratch_capacity = count;
    }
    count = qa_qvm_mod_client_bindings_checkpoint(&p->client_bindings, p->client_scratch,
                                                  p->client_scratch_capacity);
    for (size_t i = 0; i < count; i++) {
      qa_save_value *entry = save_object();
      save_set(entry, "actor", saved_actor_value(qa_saved_actor_id_of(p->client_scratch[i].actor)));
      save_set(entry, "slot", qa_save_value_number((double)p->client_scratch[i].slot));
      save_set(entry, "admitted", qa_save_value_boolean(p->client_scratch[i].admitted));
      save_push(client_slots, entry);
    }
  }
  save_set(root, "clientSlots", client_slots);
  if (p->has_player_events) save_set(root, "playerEvents", player_events_value(p));
  if (p->has_items) save_set(root, "items", qa_qvm_mod_items_checkpoint(&p->items));
  qa_save_value *defaults = save_array();
  for (size_t i = 0; i < p->default_count; i++) {
    qa_save_value *entry = save_object();
    save_set(entry, "id", qa_save_value_string(p->defaults[i].id));
    save_set(entry, "bytes", qa_save_value_bytes(p->defaults[i].bytes, p->defaults[i].length));
    save_push(defaults, entry);
  }
  save_set(root, "defaults", defaults);
  qa_save_value *configstrings = save_array();
  for (size_t i = 0; i < p->configstring_count; i++) {
    qa_save_value *entry = save_object();
    save_set(entry, "index", qa_save_value_number((double)p->configstrings[i].index));
    save_set(entry, "value", qa_save_value_string(p->configstrings[i].value));
    save_push(configstrings, entry);
  }
  save_set(root, "configstrings", configstrings);
  save_set(root, "cvars", qa_cvar_registry_capture_save_state(p->cvars));
  save_set(root, "files", files_value(p));
  save_set(root, "portals", qa_save_value_null());
  save_set(root, "entityTokens", qa_qvm_entity_tokens_capture(&p->entity_tokens));
  qa_save_value *presentation = save_object();
  save_set(presentation, "revision", qa_save_value_number((double)p->presentation_revision));
  save_set(presentation, "state",
           p->has_presentation_state ? game_state_value(&p->presentation_state)
                                     : qa_save_value_null());
  save_set(presentation, "sceneRevision", qa_save_value_number((double)p->scene_revision));
  save_set(presentation, "dirty", qa_save_value_boolean(p->scene_dirty));
  save_set(presentation, "commandSequence", qa_save_value_number((double)p->scene_command_sequence));
  qa_save_value *commands = save_array();
  size_t command_count = 0;
  SceneCommand *scene_commands = qa_bounded_queue_to_array(&p->scene_commands, &command_count);
  for (size_t i = 0; i < command_count; i++) {
    qa_save_value *entry = save_object();
    save_set(entry, "sequence", qa_save_value_number((double)scene_commands[i].sequence));
    save_set(entry, "recipient",
             scene_commands[i].has_recipient
                 ? saved_actor_value(qa_saved_actor_id_of(scene_commands[i].recipient))
                 : qa_save_value_null());
    save_set(entry, "text", qa_save_value_string(scene_commands[i].text));
    save_push(commands, entry);
  }
  free(scene_commands);
  save_set(presentation, "commands", commands);
  save_set(presentation, "current",
           p->scene_state == NULL ? qa_save_value_null()
                                  : qa_qvm_capture_mod_scene_publication(p->scene_state,
                                                                         p->declaration->abi_profile));
  save_set(presentation, "baseline",
           p->scene_baseline == NULL ? qa_save_value_null()
                                     : qa_qvm_capture_mod_scene_publication(
                                           p->scene_baseline, p->declaration->abi_profile));
  save_set(root, "presentation", presentation);
  free(p->host_state_bytes);
  p->host_state_bytes = qa_encode_checkpoint_value(root, &p->host_state_length);
  qa_save_value_free(root);
  if (p->host_state_bytes == NULL) qa_range_error("QVM memory allocation failed");
  qa_qvm_host_checkpoint result;
  memset(&result, 0, sizeof result);
  result.state.module = p->artifact->module;
  result.state.format = "qvm:mod-host-v1";
  result.state.bytes.bytes = p->host_state_bytes;
  result.state.bytes.length = p->host_state_length;
  return result;
}
static qa_actor_id resolve_saved(void *ctx, qa_saved_actor_id saved) {
  return reference_saved(ctx, saved);
}
static CpScene *read_scene_or_null(P *p, const qa_save_reader *reader) {
  if (qa_save_reader_is_null(reader)) return NULL;
  CpScene *scene = xrealloc(NULL, sizeof *scene);
  memset(scene, 0, sizeof *scene);
  qa_qvm_read_mod_scene_publication(reader, p->declaration->abi_profile, resolve_saved, p, scene);
  return scene;
}
static qa_qvm_files *new_files(P *p) {
  qa_qvm_file_services services = {p->mounts, p->writable, p, engine_print, objective_current};
  return qa_qvm_files_create(services);
}
static void clear_scene_commands(P *p) {
  size_t count = 0;
  SceneCommand *commands = qa_bounded_queue_to_array(&p->scene_commands, &count);
  for (size_t i = 0; i < count; i++) free(commands[i].text);
  free(commands);
  qa_bounded_queue_clear(&p->scene_commands);
}
static void host_restore(void *ctx, const qa_qvm_host_checkpoint *state) {
  P *p = ctx;
  qa_save_value *decoded = qa_decode_checkpoint_value(state->state.bytes.bytes,
                                                      state->state.bytes.length);
  if (decoded == NULL) qa_range_error("QVM memory allocation failed");
  qa_save_value_free(p->saved_items);
  p->saved_items = decoded; /* whole tree kept for items restore */
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "mod-host"))
    qa_range_error("QVM memory allocation failed");
  qa_save_reader field;
  reader_field(&reader, "entityTokens", &field);
  qa_qvm_entity_tokens_restore(&p->entity_tokens, field.value);
  qa_save_reader_destroy(&field);
  reader_field(&reader, "portals", &field);
  if (!qa_save_reader_is_undefined(&field) && !qa_save_reader_is_null(&field))
    qa_range_error("Saved QVM mod portals require destination topology");
  qa_save_reader_destroy(&field);
  qa_qvm_files *files = p->mounts == NULL ? NULL : new_files(p);
  if (p->mounts != NULL && files == NULL) qa_range_error("QVM memory allocation failed");
  reader_field(&reader, "files", &field);
  if (files != NULL) qa_qvm_files_restore_checkpoint(files, field.value);
  qa_save_reader_destroy(&field);
  reader_field(&reader, "cvars", &field);
  qa_cvar_registry_restore_save_state(p->cvars, field.value);
  qa_save_reader_destroy(&field);
  if (p->files != NULL) {
    qa_qvm_files_close_all(p->files);
    qa_qvm_files_destroy(p->files);
  }
  p->files = files;
  reader_field(&reader, "nextSlot", &field);
  p->next_slot = (int64_t)qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  for (size_t i = 0; i < p->default_count; i++) free(p->defaults[i].bytes);
  free(p->defaults);
  p->defaults = NULL;
  p->default_count = 0;
  reader_field(&reader, "defaults", &field);
  p->default_count = qa_save_reader_list_count(&field);
  p->defaults = xrealloc(NULL, (p->default_count == 0 ? 1 : p->default_count) * sizeof *p->defaults);
  for (size_t i = 0; i < p->default_count; i++) {
    qa_save_reader entry, sub;
    reader_item(&field, i, &entry);
    reader_field(&entry, "id", &sub);
    const char *id = qa_save_reader_string(&sub);
    /* Defaults are keyed by declaration order in hostImage; match
     * the record by id so the borrowed id stays declaration-owned. */
    const qa_qvm_mod_actor_record *record = record_named(p->declaration, id);
    p->defaults[i].id = record == NULL ? id : record->id;
    qa_save_reader_destroy(&sub);
    reader_field(&entry, "bytes", &sub);
    size_t length = 0;
    const uint8_t *bytes = qa_save_reader_bytes(&sub, &length);
    p->defaults[i].length = length;
    p->defaults[i].bytes = xrealloc(NULL, length == 0 ? 1 : length);
    memcpy(p->defaults[i].bytes, bytes, length);
    qa_save_reader_destroy(&sub);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&field);
  if (p->has_actor_semantics) qa_qvm_mod_actors_clear_actors(&p->actor_semantics);
  p->owned_count = 0;
  p->projection_count = 0;
  for (size_t i = 0; i < p->event_key_count; i++) free(p->event_keys[i].key);
  p->event_key_count = 0;
  if (p->has_client_bindings) {
    reader_field(&reader, "clientSlots", &field);
    size_t count = qa_save_reader_is_undefined(&field) ? 0 : qa_save_reader_list_count(&field);
    qa_qvm_mod_client_slot *slots =
        xrealloc(NULL, (count == 0 ? 1 : count) * sizeof *slots);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry, sub;
      reader_item(&field, i, &entry);
      reader_field(&entry, "actor", &sub);
      slots[i].actor = reference_saved(p, qa_read_saved_actor(&sub));
      qa_save_reader_destroy(&sub);
      reader_field(&entry, "slot", &sub);
      slots[i].slot = (int64_t)qa_save_reader_integer_min(&sub, 0);
      qa_save_reader_destroy(&sub);
      reader_field(&entry, "admitted", &sub);
      slots[i].admitted = qa_save_reader_boolean(&sub);
      qa_save_reader_destroy(&sub);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&field);
    qa_qvm_mod_client_bindings_restore(&p->client_bindings, slots, count);
    free(slots);
  }
  for (size_t i = 0; i < p->configstring_count; i++) free(p->configstrings[i].value);
  p->configstring_count = 0;
  reader_field(&reader, "configstrings", &field);
  size_t config_count = qa_save_reader_list_count(&field);
  for (size_t i = 0; i < config_count; i++) {
    qa_save_reader entry, sub;
    reader_item(&field, i, &entry);
    reader_field(&entry, "index", &sub);
    int64_t index = (int64_t)qa_save_reader_integer(&sub);
    qa_save_reader_destroy(&sub);
    reader_field(&entry, "value", &sub);
    const char *value = qa_save_reader_string(&sub);
    if (p->configstring_count == p->configstring_capacity) {
      p->configstring_capacity = p->configstring_capacity == 0 ? 16 : p->configstring_capacity * 2;
      p->configstrings = xrealloc(p->configstrings, p->configstring_capacity * sizeof *p->configstrings);
    }
    p->configstrings[p->configstring_count].index = index;
    p->configstrings[p->configstring_count].value = xstrdup(value);
    p->configstring_count++;
    qa_save_reader_destroy(&sub);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&field);
  bind_server_variables(p, p->cvars);
  p->presentation_generation++;
  p->presentation_source.generation = (double)p->presentation_generation;
  reader_field(&reader, "presentation", &field);
  if (qa_save_reader_is_undefined(&field)) {
    p->presentation_revision++;
    if (p->has_presentation_state) {
      qa_q3_source_game_state_record_destroy(&p->presentation_state);
      p->has_presentation_state = false;
    }
    free_cp_scene(p->scene_state);
    p->scene_state = NULL;
    free_cp_scene(p->scene_baseline);
    p->scene_baseline = NULL;
    p->scene_dirty = true;
    p->scene_revision = 0;
    p->scene_command_sequence = 0;
    clear_scene_commands(p);
  } else {
    qa_save_reader sub;
    reader_field(&field, "revision", &sub);
    p->presentation_revision = (int64_t)qa_save_reader_integer_min(&sub, 0);
    qa_save_reader_destroy(&sub);
    reader_field(&field, "state", &sub);
    if (p->has_presentation_state) {
      qa_q3_source_game_state_record_destroy(&p->presentation_state);
      p->has_presentation_state = false;
    }
    if (!qa_save_reader_is_null(&sub)) {
      qa_qvm_read_presentation_game_state(&sub, &p->presentation_state);
      p->has_presentation_state = true;
    }
    qa_save_reader_destroy(&sub);
    reader_field(&field, "sceneRevision", &sub);
    p->scene_revision = (int64_t)qa_save_reader_integer_min(&sub, 0);
    qa_save_reader_destroy(&sub);
    reader_field(&field, "dirty", &sub);
    p->scene_dirty = qa_save_reader_boolean(&sub);
    qa_save_reader_destroy(&sub);
    reader_field(&field, "commandSequence", &sub);
    p->scene_command_sequence = (int64_t)qa_save_reader_integer_min(&sub, 0);
    qa_save_reader_destroy(&sub);
    clear_scene_commands(p);
    reader_field(&field, "commands", &sub);
    size_t count = qa_save_reader_list_count(&sub);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry, part;
      reader_item(&sub, i, &entry);
      reader_field(&entry, "sequence", &part);
      int64_t sequence = (int64_t)qa_save_reader_integer_min(&part, 1);
      qa_save_reader_destroy(&part);
      reader_field(&entry, "text", &part);
      const char *text = qa_save_reader_string(&part);
      qa_save_reader_destroy(&part);
      reader_field(&entry, "recipient", &part);
      SceneCommand command;
      memset(&command, 0, sizeof command);
      command.sequence = sequence;
      if (!qa_save_reader_is_null(&part)) {
        command.has_recipient = true;
        command.recipient = reference_saved(p, qa_read_saved_actor(&part));
      }
      qa_save_reader_destroy(&part);
      qa_save_reader_destroy(&entry);
      const SceneCommand *newest = qa_bounded_queue_peek_newest(&p->scene_commands);
      if (sequence > p->scene_command_sequence || sequence <= p->scene_command_sequence - 64 ||
          sequence <= (newest == NULL ? 0 : newest->sequence))
        qa_save_reader_fail(&field, "invalid saved source command ring");
      command.text = xstrdup(text);
      qa_bounded_queue_push(&p->scene_commands, &command);
    }
    qa_save_reader_destroy(&sub);
    reader_field(&field, "current", &sub);
    CpScene *current_scene = read_scene_or_null(p, &sub);
    qa_save_reader_destroy(&sub);
    reader_field(&field, "baseline", &sub);
    CpScene *baseline = read_scene_or_null(p, &sub);
    qa_save_reader_destroy(&sub);
    free_cp_scene(p->scene_state);
    free_cp_scene(p->scene_baseline);
    p->scene_state = current_scene;
    p->scene_baseline = baseline;
    if (current_scene != NULL && current_scene->revision != p->scene_revision)
      qa_save_reader_fail(&field, "invalid saved source publication revision");
  }
  qa_save_reader_destroy(&field);
  reader_field(&reader, "projections", &field);
  size_t count = qa_save_reader_list_count(&field);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, sub;
    reader_item(&field, i, &entry);
    reader_field(&entry, "actor", &sub);
    qa_saved_actor_id saved = qa_read_saved_actor(&sub);
    qa_save_reader_destroy(&sub);
    reader_field(&entry, "slot", &sub);
    int64_t slot = (int64_t)qa_save_reader_integer_min(&sub, 0);
    qa_save_reader_destroy(&sub);
    reader_field(&entry, "owned", &sub);
    bool owned = qa_save_reader_boolean(&sub);
    qa_save_reader_destroy(&sub);
    reader_field(&entry, "event", &sub);
    const char *event = qa_save_reader_is_null(&sub) ? NULL : qa_save_reader_string(&sub);
    qa_actor_id actor = reference_saved(p, saved);
    projection_set(p, actor, slot);
    if (event != NULL) event_key_set(p, actor, event);
    qa_save_reader_destroy(&sub);
    qa_save_reader_destroy(&entry);
    if (owned) {
      qa_owned_actor owner;
      if (!resolve_owned(p, actor, &owner)) qa_range_error("Missing restored QVM source actor");
      bind_owned(p, owner, slot);
    }
  }
  qa_save_reader_destroy(&field);
  if (p->has_player_events) {
    reader_field(&reader, "playerEvents", &field);
    qa_qvm_player_events_checkpoint events;
    bool has_events = qa_qvm_read_player_events(&field, &events);
    size_t player_count = 0;
    qa_qvm_mod_client_slot *players = NULL;
    if (p->has_client_bindings) {
      player_count = qa_qvm_mod_client_bindings_players(&p->client_bindings, NULL, 0);
      players = xrealloc(NULL, (player_count == 0 ? 1 : player_count) * sizeof *players);
      player_count = qa_qvm_mod_client_bindings_players(&p->client_bindings, players, player_count);
    }
    qa_qvm_player_address *addresses =
        xrealloc(NULL, (player_count == 0 ? 1 : player_count) * sizeof *addresses);
    for (size_t i = 0; i < player_count; i++) {
      addresses[i].actor = players[i].actor;
      addresses[i].address = player_address(p, players[i].actor);
    }
    qa_qvm_mod_player_events_restore(&p->player_events, has_events ? &events : NULL,
                                     addresses, player_count);
    free(addresses);
    free(players);
    if (has_events) qa_qvm_player_events_checkpoint_free(&events);
    qa_save_reader_destroy(&field);
  }
  qa_save_reader_destroy(&reader);
}

/* ---- provider checkpoint / restore / close ---- */
const qa_guest_checkpoint *qa_qvm_mod_provider_checkpoint(qa_qvm_mod_provider *p) {
  current(p);
  qa_qvm_mod_pickups_assert_idle(&p->pickups);
  for (size_t i = 0; i < p->protection_count; i++)
    qa_qvm_mod_protection_assert_idle(p->protection_channels[i]);
  if (p->has_checkpoint) {
    free((void *)p->checkpoint_storage.data.bytes);
    free((void *)p->checkpoint_storage.host_state.bytes.bytes);
    memset(&p->checkpoint_storage, 0, sizeof p->checkpoint_storage);
  }
  qa_qvm_module_checkpoint(&p->module, &p->checkpoint_storage);
  /* The module checkpoint copies host-state bytes; provider scratch
   * (p->host_state_bytes) stays owned by the host checkpoint call. */
  p->has_checkpoint = true;
  return &p->checkpoint_storage;
}
void qa_qvm_mod_provider_restore(qa_qvm_mod_provider *p, const qa_guest_checkpoint *checkpoint) {
  current(p);
  qa_qvm_validate_mod_checkpoint(p->artifact, p->declaration, checkpoint);
  qa_qvm_mod_pickups_assert_idle(&p->pickups);
  for (size_t i = 0; i < p->protection_count; i++)
    qa_qvm_mod_protection_assert_idle(p->protection_channels[i]);
  HostImage saved;
  host_image(checkpoint, p->declaration, &saved);
  for (size_t i = 0; i < saved.projection_count; i++) {
    qa_owned_actor actor;
    bool resolved = qa_session_actor_registry_resolve_saved(p->services->actors,
                                                            saved.projections[i].actor, &actor);
    bool bad = !resolved;
    if (!bad && saved.projections[i].owned) {
      qa_actor_source_result source =
          qa_session_actor_registry_source_of(p->services->actors, actor.id);
      if (!text_eq(actor.owner, p->artifact->module.id) || !source.has_value ||
          source.slot != saved.projections[i].slot)
        bad = true;
    }
    if (bad) qa_range_error("Saved QVM mod actor is unavailable or has the wrong owner");
    if (saved.projections[i].owned) {
      const qa_qvm_mod_source_actors *source = p->declaration->source_actors;
      if (source == NULL ||
          checkpoint_word(checkpoint, entity_address(p, saved.projections[i].slot) +
                                          (int64_t)source->inuse) == 0)
        qa_range_error("Saved QVM source actor is inactive");
    }
  }
  size_t owned_saved = 0;
  for (size_t i = 0; i < saved.projection_count; i++)
    if (saved.projections[i].owned) owned_saved++;
  size_t owned_now = 0;
  (void)qa_session_actor_registry_owned_by(p->services->actors, p->artifact->module.id, &owned_now);
  if (owned_saved != owned_now)
    qa_range_error("QVM mod source ownership differs from the saved world");
  {
    qa_cvar_registry *candidate = new_cvars(p);
    if (candidate == NULL) qa_range_error("QVM memory allocation failed");
    qa_cvar_registry_restore_save_state(candidate, saved.cvars);
    qa_cvar_registry_destroy(candidate);
  }
  if (p->mounts != NULL) {
    qa_qvm_files *files = new_files(p);
    if (files == NULL) qa_range_error("QVM memory allocation failed");
    qa_qvm_files_restore_checkpoint(files, saved.files);
    qa_qvm_files_close_all(files);
    qa_qvm_files_destroy(files);
  } else if (saved.files != NULL && saved.files->kind != QA_SAVE_NULL) {
    qa_range_error("Saved QVM mod filesystem is unavailable");
  }
  bool active = qa_qvm_mod_pickups_get_is_active(&p->pickups);
  for (size_t i = 0; i < p->protection_count; i++)
    if (qa_qvm_mod_protection_runtime_get_is_active(p->protection_channels[i])) active = true;
  qa_qvm_mod_pickups_close(&p->pickups);
  if (p->has_items) qa_qvm_mod_items_clear(&p->items);
  for (size_t i = 0; i < p->protection_count; i++)
    qa_qvm_mod_protection_close(p->protection_channels[i]);
  qa_qvm_mod_provider_reserve_protection(p);
  if (p->has_player_events) qa_qvm_mod_player_events_close(&p->player_events);
  if (p->client_outputs != NULL) qa_source_mod_client_outputs_clear(p->client_outputs);
  host_image_free(&saved);
  qa_qvm_module_restore(&p->module, checkpoint);
  if (p->objectives != NULL) qa_mod_source_objectives_restored(p->objectives);
  if (text_eq(presentation_runtime(p->declaration), "qvm-scene")) {
    CpScene *current_scene = scene_publication(p);
    qa_save_value *encoded =
        qa_qvm_capture_mod_scene_publication(current_scene, p->declaration->abi_profile);
    if (encoded == NULL) qa_range_error("QVM memory allocation failed");
    qa_save_reader reader;
    if (!qa_save_reader_init(&reader, encoded, "scene-baseline"))
      qa_range_error("QVM memory allocation failed");
    CpScene *baseline = read_scene_or_null(p, &reader);
    qa_save_reader_destroy(&reader);
    qa_save_value_free(encoded);
    free_cp_scene(p->scene_baseline);
    p->scene_baseline = baseline;
  }
  if (p->has_client_bindings) qa_qvm_mod_client_bindings_start(&p->client_bindings);
  if (p->has_client_bindings) {
    size_t count = qa_qvm_mod_client_bindings_players(&p->client_bindings, NULL, 0);
    if (count > p->client_scratch_capacity) {
      p->client_scratch = xrealloc(p->client_scratch, count * sizeof *p->client_scratch);
      p->client_scratch_capacity = count;
    }
    count = qa_qvm_mod_client_bindings_players(&p->client_bindings, p->client_scratch,
                                               p->client_scratch_capacity);
    for (size_t i = 0; i < count; i++) {
      if (p->client_scratch[i].admitted && p->client_outputs != NULL)
        qa_source_mod_client_outputs_publish(p->client_outputs, p->client_scratch[i].actor);
      if (p->has_items) qa_qvm_mod_items_admit(&p->items, p->client_scratch[i].actor);
    }
  }
  if (p->has_items) {
    const qa_save_value *items =
        p->saved_items == NULL ? NULL : qa_save_value_get(p->saved_items, "items");
    if (items == NULL) qa_range_error("Saved QVM items lack their source request checkpoint");
    qa_qvm_mod_items_restore(&p->items, items);
  }
  if (active) qa_qvm_mod_provider_activate_protection(p);
}
void qa_qvm_mod_provider_close(qa_qvm_mod_provider *p) {
  if (p->objectives != NULL) qa_mod_source_objectives_close(p->objectives);
  if (p->has_release_match && p->release_match.invoke != NULL)
    p->release_match.invoke(p->release_match.ctx);
  p->has_release_match = false;
  if (p->closed) return;
  qa_qvm_mod_pickups_close(&p->pickups);
  if (p->has_actor_frame) qa_qvm_mod_actor_frame_runtime_close(&p->actor_frame);
  if (p->has_items) qa_qvm_mod_items_close(&p->items);
  for (size_t i = 0; i < p->protection_count; i++)
    qa_qvm_mod_protection_close(p->protection_channels[i]);
  if (p->client_outputs != NULL) qa_source_mod_client_outputs_close(p->client_outputs);
  p->closed = true;
  if (p->has_player_events) qa_qvm_mod_player_events_close(&p->player_events);
  for (size_t i = 0; i < p->owned_count; i++) {
    qa_owned_actor actor = p->owned[i].actor;
    if (is_live(p, actor.id)) qa_session_actor_registry_release(p->services->actors, actor);
  }
  if (p->has_release_subscription)
    qa_session_actor_registry_off_release(p->services->actors, p->release_subscription);
  p->has_release_subscription = false;
  if (p->has_client_bindings) qa_qvm_mod_client_bindings_close(&p->client_bindings);
  qa_qvm_mod_input_close(&p->input);
  if (p->commands != NULL) qa_mod_command_port_close(p->commands);
  if (p->has_actor_semantics) qa_qvm_mod_actors_close(&p->actor_semantics);
  for (size_t i = 0; i < p->hook_count; i++) qa_qvm_module_unbind(&p->module, p->hooks[i]);
  p->projection_count = 0;
  p->retired_count = 0;
  p->owned_count = 0;
  for (size_t i = 0; i < p->event_key_count; i++) free(p->event_keys[i].key);
  p->event_key_count = 0;
  if (p->files != NULL) qa_qvm_files_close_all(p->files);
  if (p->module_live) qa_qvm_module_retire(&p->module);
}
static void on_actor_release(void *ctx, qa_owned_actor actor) {
  P *p = ctx;
  qa_actor_id id = actor.id;
  p->scene_dirty = true;
  if (p->has_player_events) qa_qvm_mod_player_events_release(&p->player_events, id);
  client_outputs_release(p, id);
  qa_qvm_mod_pickups_release(&p->pickups, id);
  if (p->has_items) qa_qvm_mod_items_release(&p->items, id);
  for (size_t i = 0; i < p->protection_count; i++)
    qa_qvm_mod_protection_release(p->protection_channels[i], id);
  int64_t slot;
  bool has_slot = projection_slot(p, id, &slot);
  bool owned = owned_has(p, id);
  owned_delete(p, id);
  event_key_delete(p, id);
  if (p->has_client_bindings) qa_qvm_mod_client_bindings_forget(&p->client_bindings, id);
  if (owned) {
    projection_delete(p, id);
    if (has_slot && !p->closed && !removing_has(p, id)) release_source(p, slot);
  } else if (has_slot) {
    if (p->declaration->source_actors != NULL)
      write_word(p, entity_address(p, slot) + (int64_t)p->declaration->source_actors->inuse, 0);
    release_projection(p, id);
  }
}

/* ---- constructor ---- */
bool qa_qvm_mod_provider_init(qa_qvm_mod_provider *p, const qa_qvmy_artifact *artifact,
                              const qa_qvm_mod_callback_declaration *declaration,
                              qa_mod_host_services *services,
                              qa_qvm_mod_provider_current_fn assert_current,
                              void *assert_current_ctx, qa_content_id content,
                              qa_content_mounted *mounts, qa_user_file_store *writable,
                              qa_qvmy_interpreter_runtime *interpreter,
                              const qa_guest_memory *guest_memory) {
  memset(p, 0, sizeof *p);
  p->artifact = artifact;
  p->declaration = declaration;
  p->services = services;
  p->assert_current = assert_current;
  p->assert_current_ctx = assert_current_ctx;
  p->content = content;
  p->mounts = mounts;
  p->writable = writable;
  p->scene_dirty = true;
  qa_qvm_validate_mod(artifact, declaration);
  {
    qa_mod_objective_storage storage = {p, objectives_current, objectives_location,
                                        objectives_read_scalar, objectives_write_scalar,
                                        objectives_read_actor, objectives_write_actor,
                                        objectives_invoke, objectives_seconds};
    p->objectives = qa_mod_source_objectives_create(artifact->module.id, declaration->objectives,
                                                    declaration->objective_count,
                                                    services->match, storage);
    if (p->objectives == NULL) return false;
  }
  if (declaration->clients != NULL && services->clients == NULL)
    qa_range_error("QVM source clients require destination client identity services");
  if (declaration->source_actors != NULL &&
      (services->engine == NULL || services->engine->physics == NULL))
    qa_range_error("QVM source actors require destination collision services");
  {
    bool has_claim = services->clients != NULL && services->clients->claim_outputs != NULL;
    p->client_outputs = qa_source_mod_client_outputs_create(
        artifact->module.id, declaration->clients == NULL ? NULL : declaration->clients->outputs,
        declaration->clients == NULL ? 0 : declaration->clients->output_count, has_claim,
        has_claim ? outputs_claim : NULL, p, outputs_scalar, outputs_vector, p);
    if (p->client_outputs == NULL) return false;
  }
  int64_t source_end = artifact->data_length + artifact->literal_length + artifact->bss_length;
  p->scratch_start = (source_end + 15) / 16 * 16;
  p->scratch = p->scratch_start;
  p->cvars = new_cvars(p);
  if (p->cvars == NULL) return false;
  {
    const char *spawn = declaration->has_spawn_entities && declaration->spawn_entities != NULL
                            ? declaration->spawn_entities : "";
    if (!qa_qvm_entity_tokens_init(&p->entity_tokens, spawn, strlen(spawn))) return false;
  }
  p->information.abi_profile = declaration->abi_profile;
  p->information.cvars = p->cvars;
  p->information.configstrings_ctx = p;
  p->information.configstring_get = information_get;
  p->information.configstring_set = information_set;
  if (mounts != NULL) {
    p->files = new_files(p);
    if (p->files == NULL) return false;
  }
  p->frames = xrealloc(NULL, 64 * sizeof(Frame));
  memset(p->frames, 0, 64 * sizeof(Frame));
  p->frame_capacity = 64;
  if (!qa_bounded_queue_init(&p->scene_commands, 64, sizeof(SceneCommand))) return false;
  p->presentation_context = xrealloc(NULL, sizeof *p->presentation_context);
  memset(p->presentation_context, 0, sizeof *p->presentation_context);
  p->host_state.ctx = p;
  p->host_state.checkpoint = host_checkpoint;
  p->host_state.restore = host_restore;
  {
    qa_qvm_module_options options;
    memset(&options, 0, sizeof options);
    options.artifact = artifact;
    options.host = provider_syscall;
    options.host_ctx = p;
    options.host_state = &p->host_state;
    options.interpreter = interpreter;
    options.guest_memory = guest_memory;
    qa_qvm_module_init(&p->module, &options);
    p->module_live = true;
  }
  if (!text_eq(presentation_runtime(declaration), "qvm-scene")) {
    qa_qvm_mod_player_events_operations operations;
    memset(&operations, 0, sizeof operations);
    operations.memory = mem(p);
    operations.module = artifact->module;
    operations.abi_profile = declaration->abi_profile;
    operations.ctx = p;
    operations.reference_saved = events_reference_saved;
    operations.live = events_live;
    operations.origin = events_origin;
    operations.time = events_time;
    operations.emit = events_emit;
    qa_qvm_mod_player_events_init(&p->player_events, &operations);
    p->has_player_events = true;
  }
  p->protection_services.ctx = p;
  p->protection_services.has_clients = services->clients != NULL;
  p->protection_services.clients = service_clients;
  p->protection_services.for_actor = service_for_actor;
  p->protection_services.actor = service_actor;
  p->protection_services.resolve_owned = service_resolve_owned;
  p->protection_services.is_live = service_is_live;
  p->protection_services.reserve_protection = service_reserve_protection;
  p->protection_services.bind_reservation = service_bind_reservation;
  p->protection_services.close_reservation = service_close_reservation;
  p->protection_services.time = service_time;
  p->pickup_services.ctx = p;
  p->pickup_services.has_clients = services->clients != NULL;
  p->pickup_services.clients = service_clients;
  p->pickup_services.resolve_owned = service_resolve_owned;
  p->pickup_services.is_live = service_is_live;
  p->pickup_services.bind_pickup = service_bind_pickup;
  p->pickup_services.unbind_pickup = service_unbind_pickup;
  p->item_services.ctx = p;
  p->item_services.resolve_owned = service_resolve_owned;
  p->item_services.reference_saved = service_reference_saved;
  p->item_services.bind_items = service_bind_items;
  p->item_services.inventory_count = service_inventory_count;
  p->item_services.has_weapons = service_has_weapons;
  p->item_services.weapon_selected = service_weapon_selected;
  p->item_services.weapon_bind = service_weapon_bind;
  p->item_services.body_read = service_body_read;
  p->item_services.player_view = service_player_view;
  {
    qa_qvm_mod_protection_operations operations = {p, protection_current, protection_eligible,
                                                   protection_pointer, protection_invoke,
                                                   protection_pickups};
    p->protection_count = declaration->protection_count;
    p->protection_storage =
        xrealloc(NULL, (p->protection_count == 0 ? 1 : p->protection_count) *
                           sizeof *p->protection_storage);
    p->protection_channels =
        xrealloc(NULL, (p->protection_count == 0 ? 1 : p->protection_count) *
                           sizeof *p->protection_channels);
    memset(&p->protection_component, 0, sizeof p->protection_component);
    qa_qvm_mod_protection_create(&p->protection_component, p->protection_storage,
                                 p->protection_channels, declaration->protection,
                                 p->protection_count, mem(p), &p->protection_services,
                                 artifact->module.id, &operations);
    /* Runtimes borrow the operations pointer; keep an owned copy. */
    qa_qvm_mod_protection_operations *owned_operations =
        xrealloc(NULL, sizeof *owned_operations);
    *owned_operations = operations;
    for (size_t i = 0; i < p->protection_count; i++)
      p->protection_channels[i]->operations = owned_operations;
  }
  {
    qa_qvm_mod_pickup_operations *operations = xrealloc(NULL, sizeof *operations);
    operations->ctx = p;
    operations->current = protection_current;
    operations->eligible = pickups_eligible;
    operations->context = pickups_context;
    operations->observe = pickups_observe;
    operations->invoke = pickups_invoke;
    qa_qvm_mod_pickups_init(&p->pickups, declaration->pickups, declaration->pickup_count,
                            &p->pickup_services, artifact->module.id, operations);
  }
  if (declaration->items != NULL) {
    qa_qvm_mod_item_operations *operations = xrealloc(NULL, sizeof *operations);
    operations->ctx = p;
    operations->pointer = items_pointer;
    operations->live = items_live;
    operations->invoke = items_invoke;
    operations->ground = items_ground;
    operations->pickup = items_pickup;
    qa_qvm_mod_items_runtime_init(&p->items, declaration->items, &p->module, artifact,
                                  &p->item_services, artifact->module.id, content, operations);
    p->has_items = true;
  }
  if ((declaration->source_actors != NULL && declaration->source_actors->callbacks != NULL) ||
      declaration->combat != NULL) {
    qa_qvm_mod_actors_options *options = xrealloc(NULL, sizeof *options);
    memset(options, 0, sizeof *options);
    options->module = &p->module;
    options->declaration = declaration;
    options->services = services;
    options->ctx = p;
    options->owned_has = actors_owned_has;
    options->pointer = actors_pointer;
    options->actor = actors_actor;
    options->invoke = actors_invoke;
    options->scratch = actors_scratch;
    qa_qvm_mod_actors_init(&p->actor_semantics, options);
    p->has_actor_semantics = true;
  }
  {
    qa_qvm_mod_input_operations *operations = xrealloc(NULL, sizeof *operations);
    memset(operations, 0, sizeof *operations);
    operations->module = &p->module;
    operations->abi_profile = declaration->abi_profile;
    operations->records = declaration->actor_records;
    operations->record_count = declaration->actor_record_count;
    operations->ctx = p;
    operations->pointer = input_pointer;
    operations->live = input_live;
    operations->player_state = input_player_state;
    size_t output_count = 0;
    if (declaration->clients != NULL)
      for (size_t i = 0; i < declaration->clients->input_count; i++)
        if (text_eq(declaration->clients->input[i].phase, "before"))
          output_count += declaration->clients->input[i].output_count;
    qa_qvm_mod_input_output *outputs =
        xrealloc(NULL, (output_count == 0 ? 1 : output_count) * sizeof *outputs);
    size_t written = 0;
    if (declaration->clients != NULL)
      for (size_t i = 0; i < declaration->clients->input_count; i++) {
        const qa_mod_client_input_binding *binding = &declaration->clients->input[i];
        if (!text_eq(binding->phase, "before")) continue;
        const qa_qvm_mod_input_output *source_outputs = binding->outputs;
        for (size_t o = 0; o < binding->output_count; o++) outputs[written++] = source_outputs[o];
      }
    qa_qvm_mod_input_init(&p->input, operations, outputs, written);
  }
  if (declaration->clients != NULL && services->clients != NULL) {
    qa_qvm_mod_client_operations *operations = xrealloc(NULL, sizeof *operations);
    memset(operations, 0, sizeof *operations);
    operations->content = content;
    operations->services = services->clients;
    operations->declaration = declaration->clients;
    operations->ctx = p;
    operations->project = clients_project;
    operations->admitted = clients_admitted;
    operations->release = clients_release;
    operations->invoke = clients_invoke;
    operations->open_input = clients_open_input;
    operations->output = clients_output;
    operations->reserved_slots = clients_reserved;
    operations->player_state = clients_player_state;
    operations->requested_weapon = clients_requested;
    operations->send = clients_send;
    qa_qvm_mod_client_bindings_init(&p->client_bindings, operations);
    p->has_client_bindings = true;
  }
  if (declaration->source_actors != NULL && declaration->source_actors->frame != NULL) {
    qa_qvm_mod_actor_frame_runtime_init(&p->actor_frame, declaration->source_actors->frame,
                                        &p->module, artifact->instructions,
                                        artifact->instruction_count, actor_frame_owned, p);
    p->has_actor_frame = true;
  }
  remember_defaults(p);
  p->release_subscription = qa_session_actor_registry_on_release(services->actors, on_actor_release, p);
  p->has_release_subscription = true;
  memset(&p->presentation_source, 0, sizeof p->presentation_source);
  p->presentation_source.ctx = p;
  p->presentation_source.module = artifact->module;
  p->presentation_source.abi_profile = declaration->abi_profile;
  p->presentation_source.generation = (double)p->presentation_generation;
  p->presentation_source.context = presentation_context_of;
  p->presentation_source.scene = presentation_scene;
  p->presentation_source.files = presentation_files;
  p->presentation_source.client_command = presentation_command;
  p->presentation_source.bindings = presentation_bindings;
  p->presentation_source.actor = presentation_actor;
  p->presentation_source.live = presentation_live;
  p->presentation_source.assert_current = presentation_current;
  bind_lifecycle(p);
  return true;
}
void qa_qvm_mod_provider_destroy(qa_qvm_mod_provider *p) {
  if (p->module_live && !p->closed) qa_qvm_mod_provider_close(p);
  if (p->has_checkpoint) {
    free((void *)p->checkpoint_storage.data.bytes);
    free((void *)p->checkpoint_storage.host_state.bytes.bytes);
  }
  if (p->objectives != NULL) qa_mod_source_objectives_destroy(p->objectives);
  if (p->client_outputs != NULL) qa_source_mod_client_outputs_destroy(p->client_outputs);
  if (p->cvars != NULL) qa_cvar_registry_destroy(p->cvars);
  qa_qvm_entity_tokens_destroy(&p->entity_tokens);
  if (p->files != NULL) qa_qvm_files_destroy(p->files);
  if (p->has_player_events) qa_qvm_mod_player_events_destroy(&p->player_events);
  if (p->has_actor_semantics) qa_qvm_mod_actors_destroy(&p->actor_semantics);
  qa_qvm_mod_input_destroy(&p->input);
  if (p->has_client_bindings) qa_qvm_mod_client_bindings_destroy(&p->client_bindings);
  for (size_t i = 0; i < p->protection_count; i++)
    qa_qvm_mod_protection_runtime_destroy(p->protection_channels[i]);
  qa_qvm_mod_protection_component_destroy(&p->protection_component);
  free(p->protection_storage);
  free(p->protection_channels);
  qa_qvm_mod_pickups_destroy(&p->pickups);
  if (p->has_items) qa_qvm_mod_items_runtime_destroy(&p->items);
  if (p->has_actor_frame) qa_qvm_mod_actor_frame_runtime_destroy(&p->actor_frame);
  if (p->has_presentation_state) qa_q3_source_game_state_record_destroy(&p->presentation_state);
  free_cp_scene(p->scene_state);
  free_cp_scene(p->scene_baseline);
  clear_scene_commands(p);
  qa_bounded_queue_destroy(&p->scene_commands);
  for (size_t i = 0; i < p->frame_capacity; i++) {
    free(frames(p)[i].observations);
    free(frames(p)[i].pending);
  }
  free(p->frames);
  for (size_t i = 0; i < p->pickup_scope_count; i++) free(pickup_scopes(p)[i]);
  free(p->pickup_scopes);
  free(p->weapon_inputs);
  free(p->projections);
  free(p->retired);
  free(p->owned);
  free(p->removing);
  for (size_t i = 0; i < p->event_key_count; i++) free(p->event_keys[i].key);
  free(p->event_keys);
  for (size_t i = 0; i < p->default_count; i++) free(p->defaults[i].bytes);
  free(p->defaults);
  for (size_t i = 0; i < p->configstring_count; i++) free(p->configstrings[i].value);
  free(p->configstrings);
  free(p->match_cells);
  free(p->hooks);
  free(p->reservations);
  free(p->pickup_bindings);
  free(p->bindings_scratch);
  free(p->presentations_scratch);
  free(p->objective_location);
  free(p->configstring_scratch);
  free(p->host_state_bytes);
  free(p->client_scratch);
  free(p->scene_view_clients);
  free(p->scene_view_entities);
  free(p->scene_view_commands);
  free(p->scene_baseline_clients);
  free(p->scene_baseline_entities);
  free(p->scene_baseline_commands);
  free(p->presentation_context);
  qa_save_value_free(p->saved_items);
  if (p->module_live) qa_qvm_module_destroy(&p->module);
  memset(p, 0, sizeof *p);
}
