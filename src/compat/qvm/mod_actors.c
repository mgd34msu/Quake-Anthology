/* See include/qa/compat/qvm/mod_actors.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_actors.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/game_combat.h"
#include "qa/compat/qvm/memory.h"
#include "qa/compat/qvm/operations.h"
#include "qa/compat/qvm/trace_record.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"

typedef enum reaction_kind { REACTION_TOUCH, REACTION_USE, REACTION_PAIN, REACTION_DIE } reaction_kind;

typedef struct pointer_entry {
  int32_t pointer;
  qa_owned_actor actor;
} pointer_entry;

typedef struct actor_pointer_entry {
  qa_actor_id actor;
  int32_t pointer;
} actor_pointer_entry;

typedef struct damage_frame {
  qa_damage_request request;
  qa_actor_id attacker;  /* request.attack.attacker storage */
  qa_actor_id inflictor; /* request.attack.inflictor storage */
  qa_source_damage_observer observer;
  qa_combat_state before;
  bool has_velocity;
  qa_vec3 velocity;
  bool finished;
  qa_source_damage_result result;
} damage_frame;

typedef struct binding_node {
  qa_qvm_mod_actors *actors;
  qa_owned_actor actor;
} binding_node;

typedef struct request_box {
  qa_damage_request request;
  qa_actor_id attacker;
  qa_actor_id inflictor;
} request_box;

typedef int32_t (*damage_execute_fn)(void *ctx, qa_damage_request request);

static const qa_vec3 zero_vec = {0, 0, 0};

static const qa_qvm_combat_role_index legacy_damage_roles[] = {
    {"target", 0}, {"inflictor", 1}, {"attacker", 2}, {"direction", 3},
    {"point", 4},  {"amount", 5},    {"flags", 6},    {"method", 7}};
static const qa_qvm_combat_role_index legacy_touch_roles[] = {
    {"target", 0}, {"other", 1}, {"trace", 2}};
static const qa_qvm_combat_role_index legacy_use_roles[] = {
    {"target", 0}, {"other", 1}, {"activator", 2}};
static const qa_qvm_combat_role_index legacy_pain_roles[] = {
    {"target", 0}, {"attacker", 1}, {"amount", 2}};
static const qa_qvm_combat_role_index legacy_die_roles[] = {
    {"target", 0}, {"inflictor", 1}, {"attacker", 2}, {"amount", 3}, {"method", 4}};

#define ROLE_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

static void *xrealloc(void *old, size_t size) {
  void *result = realloc(old, size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

static bool str_eq(const char *a, const char *b) {
  if (a == b) return true;
  if (a == NULL || b == NULL) return false;
  return strcmp(a, b) == 0;
}

static bool num_eq(double a, double b) { return a == b || (isnan(a) && isnan(b)); }

static bool vec_eq(qa_vec3 a, qa_vec3 b) {
  return num_eq(a.x, b.x) && num_eq(a.y, b.y) && num_eq(a.z, b.z);
}

static bool same_owned(qa_owned_actor a, qa_owned_actor b) {
  return a.token == b.token && qa_same_actor(a.id, b.id) && str_eq(a.owner, b.owner);
}

static bool actor_ptr_eq(const qa_actor_id *a, const qa_actor_id *b) {
  if (a == NULL || b == NULL) return a == b;
  return qa_same_actor(*a, *b);
}

static bool request_deep_equal(const qa_damage_request *a, const qa_damage_request *b) {
  if (!qa_same_actor(a->target, b->target) || !num_eq(a->amount, b->amount) ||
      !num_eq(a->knockback, b->knockback) || !vec_eq(a->direction, b->direction) ||
      !vec_eq(a->point, b->point) || !vec_eq(a->normal, b->normal) ||
      !str_eq(a->delivery, b->delivery))
    return false;
  const qa_attack_provenance *pa = &a->attack, *pb = &b->attack;
  if (!num_eq(pa->sequence, pb->sequence) || pa->time.kind != pb->time.kind ||
      !num_eq(pa->time.value, pb->time.value) || !actor_ptr_eq(pa->attacker, pb->attacker) ||
      !actor_ptr_eq(pa->inflictor, pb->inflictor) ||
      pa->has_originating_projectile != pb->has_originating_projectile ||
      (pa->has_originating_projectile &&
       !qa_same_actor(pa->originating_projectile, pb->originating_projectile)) ||
      !str_eq(pa->weapon, pb->weapon) || !str_eq(pa->weapon_provider, pb->weapon_provider) ||
      pa->has_damage_powerup_owner != pb->has_damage_powerup_owner ||
      (pa->has_damage_powerup_owner &&
       !str_eq(pa->damage_powerup_owner, pb->damage_powerup_owner)) ||
      !str_eq(pa->combat_provider, pb->combat_provider) ||
      !str_eq(pa->inventory_provider, pb->inventory_provider) ||
      !str_eq(pa->movement_provider, pb->movement_provider))
    return false;
  const qa_attack_cause *ca = &pa->cause, *cb = &pb->cause;
  if (ca->kind != cb->kind || !str_eq(ca->death_type, cb->death_type) ||
      !str_eq(ca->armor_effect, cb->armor_effect) ||
      !num_eq(ca->means_of_death, cb->means_of_death) ||
      !num_eq(ca->damage_flags, cb->damage_flags) || ca->has_native != cb->has_native ||
      !str_eq(ca->hazard, cb->hazard))
    return false;
  if (ca->has_native) {
    const qa_q2_native_cause *na = &ca->native, *nb = &cb->native;
    if (!str_eq(na->edition, nb->edition) || !str_eq(na->game, nb->game) ||
        !num_eq(na->value, nb->value) || !num_eq(na->id, nb->id) ||
        na->friendly_fire != nb->friendly_fire || na->no_point_loss != nb->no_point_loss)
      return false;
  }
  return true;
}

static bool is_safe_integer(double value) {
  return isfinite(value) && trunc(value) == value && fabs(value) <= 9007199254740991.0;
}

static bool is_integer(double value) { return isfinite(value) && trunc(value) == value; }

static const qa_qvm_mod_actor_record *find_record(const qa_qvm_mod_callback_declaration *decl,
                                                  const char *id) {
  if (id == NULL) return NULL;
  for (size_t i = 0; i < decl->actor_record_count; i++)
    if (str_eq(decl->actor_records[i].id, id)) return &decl->actor_records[i];
  return NULL;
}

static void validate_field(const qa_qvm_mod_actor_record *record, double offset) {
  if (!is_safe_integer(offset) || offset < 0 || fmod(offset, 4.0) != 0 ||
      offset + 4 > record->stride)
    qa_range_error("QVM actor semantic field exceeds its declared record");
}

/* TS team template `${string}:${string}` per /^[^:]+:.+$/. */
static bool valid_team_name(const char *team) {
  if (team == NULL) return false;
  const char *colon = strchr(team, ':');
  if (colon == NULL || colon == team || colon[1] == '\0') return false;
  return strchr(colon + 1, '\n') == NULL && strchr(colon + 1, '\r') == NULL;
}

void qa_qvm_validate_mod_actors(const qa_resolved_qvm_artifact *artifact,
                                const qa_qvm_mod_callback_declaration *declaration) {
  const qa_qvm_mod_source_callbacks *callbacks =
      declaration->source_actors != NULL ? declaration->source_actors->callbacks : NULL;
  const qa_qvm_mod_combat *combat = declaration->combat;
  if (callbacks == NULL && combat == NULL) return;
  const qa_qvm_mod_actor_record *record = find_record(declaration, declaration->entity_record);
  if (declaration->source_actors == NULL || record == NULL)
    qa_range_error("QVM actor callbacks and combat require declared source actors");
  if (callbacks != NULL) {
    if (callbacks->has_touch) validate_field(record, callbacks->touch);
    if (callbacks->has_use) validate_field(record, callbacks->use);
    if (callbacks->has_pain) validate_field(record, callbacks->pain);
    if (callbacks->has_die) validate_field(record, callbacks->die);
  }
  if (combat == NULL) return;
  const qa_qvm_mod_combat_fields *fields = &combat->fields;
  bool entry_is_function = is_integer(fields->entry) && fields->entry >= 0 &&
                           artifact != NULL && fields->entry < (double)artifact->image.instruction_count &&
                           artifact->image.instructions[(size_t)fields->entry].opcode == QA_QVMY_OP_ENTER;
  if (!entry_is_function) qa_range_error("QVM damage entry is not a source function");
  validate_field(record, fields->health);
  validate_field(record, fields->takedamage);
  validate_field(record, fields->flags);
  double mask_limit = str_eq(combat->abi, "declared") ? 4294967295.0 : 2147483647.0;
  const double masks[] = {fields->godmode, fields->no_knockback};
  for (size_t i = 0; i < 2; i++)
    if (!is_integer(masks[i]) || masks[i] <= 0 || masks[i] > mask_limit)
      qa_range_error("Invalid QVM combat flag mask");
  if (callbacks == NULL) qa_range_error("QVM source damage requires declared reaction fields");
  if (str_eq(combat->abi, "declared")) {
    double data_bytes = artifact != NULL
                            ? (double)(artifact->image.data_length + artifact->image.literal_length +
                                       artifact->image.bss_length)
                            : 0;
    qa_qvm_validate_combat_call(combat->calls.damage, data_bytes);
    qa_qvm_validate_combat_call(combat->calls.touch, data_bytes);
    qa_qvm_validate_combat_call(combat->calls.use, data_bytes);
    qa_qvm_validate_combat_call(combat->calls.pain, data_bytes);
    qa_qvm_validate_combat_call(combat->calls.die, data_bytes);
    const double flag_masks[] = {combat->damage_flags.radius, combat->damage_flags.no_armor,
                                 combat->damage_flags.no_knockback,
                                 combat->damage_flags.no_protection,
                                 combat->damage_flags.no_team_protection};
    uint32_t used = 0;
    for (size_t i = 0; i < 5; i++) {
      double mask = flag_masks[i];
      uint32_t bits = (uint32_t)qa_int32(mask);
      if (!is_integer(mask) || mask <= 0 || mask > 4294967295.0 || (bits & (bits - 1u)) != 0 ||
          (used & bits) != 0)
        qa_range_error("QVM source damage flag masks overlap or are not single positive bits");
      used |= bits;
    }
    if (!combat->mass.is_constant)
      validate_field(record, combat->mass.offset);
    else if (!isfinite(combat->mass.value) || combat->mass.value < 0)
      qa_range_error("Invalid QVM source mass");
    if (fields->client == NULL && combat->team_count != 0)
      qa_range_error("QVM source team mapping requires a client team field");
    for (size_t i = 0; i < combat->team_count; i++) {
      double value = combat->teams[i].value;
      bool duplicate = false;
      for (size_t j = 0; j < i; j++)
        if (combat->teams[j].value == value) duplicate = true;
      if (!is_integer(value) || value < -2147483648.0 || value > 2147483647.0 || duplicate ||
          !valid_team_name(combat->teams[i].team))
        qa_range_error("Invalid QVM source team mapping");
    }
  }
  if (fields->client != NULL) {
    validate_field(record, fields->client->pointer);
    const qa_qvm_mod_actor_record *client = find_record(declaration, fields->client->record);
    if (client == NULL) qa_range_error("QVM combat client has no declared record");
    validate_field(client, fields->client->health);
    validate_field(client, fields->client->armor);
    validate_field(client, fields->client->team);
    if (!isfinite(fields->client->protection) || fields->client->protection < 0 ||
        fields->client->protection > 1)
      qa_range_error("Invalid QVM armor protection");
  }
}

static qa_qvm_combat_call call_for(const qa_qvm_mod_actors *actors, reaction_kind kind) {
  switch (kind) {
    case REACTION_TOUCH: return actors->calls.touch;
    case REACTION_USE: return actors->calls.use;
    case REACTION_PAIN: return actors->calls.pain;
    case REACTION_DIE: return actors->calls.die;
  }
  return actors->calls.touch;
}

static int64_t call_bytes_for(const qa_qvm_mod_actors *actors, reaction_kind kind) {
  switch (kind) {
    case REACTION_TOUCH: return actors->call_bytes_touch;
    case REACTION_USE: return actors->call_bytes_use;
    case REACTION_PAIN: return actors->call_bytes_pain;
    case REACTION_DIE: return actors->call_bytes_die;
  }
  return 0;
}

static int64_t role_index(qa_qvm_combat_call call, const char *role) {
  for (size_t i = 0; i < call.role_count; i++)
    if (str_eq(call.roles[i].role, role)) return (int64_t)call.roles[i].index;
  qa_range_error("Missing declared QVM damage argument");
}

static void require_arguments(int64_t bytes, size_t word_length) {
  if ((int64_t)word_length < bytes)
    qa_range_error("QVM combat declaration exceeds the actual source caller frame");
}

static int32_t bytes_i32(const uint8_t *bytes, size_t offset) {
  uint32_t bits = (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) |
                  ((uint32_t)bytes[offset + 2] << 16) | ((uint32_t)bytes[offset + 3] << 24);
  return (int32_t)bits;
}

static void bytes_set_i32(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits = (uint32_t)value;
  bytes[offset] = (uint8_t)bits;
  bytes[offset + 1] = (uint8_t)(bits >> 8);
  bytes[offset + 2] = (uint8_t)(bits >> 16);
  bytes[offset + 3] = (uint8_t)(bits >> 24);
}

static qa_qvm_memory *memory_of(qa_qvm_mod_actors *actors) { return actors->options.module->memory; }

static int32_t word_at(qa_qvm_mod_actors *actors, int64_t address) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(memory_of(actors), address, 4, 0, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}

static void store_word(qa_qvm_mod_actors *actors, int64_t address, int32_t value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(memory_of(actors), address, 4, 0, &view);
  qa_qvm_writable_view_set_int32(&view, 0, value, true);
}

static float float_at(qa_qvm_mod_actors *actors, int64_t address) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(memory_of(actors), address, 4, 0, &view);
  return qa_qvm_writable_view_get_float32(&view, 0, true);
}

static qa_vec3 vector_at(qa_qvm_mod_actors *actors, int64_t address) {
  if (address == 0) return zero_vec;
  qa_qvm_writable_view view;
  qa_qvm_memory_view(memory_of(actors), address, 12, 0, &view);
  qa_vec3 result = {qa_qvm_writable_view_get_float32(&view, 0, true),
                    qa_qvm_writable_view_get_float32(&view, 4, true),
                    qa_qvm_writable_view_get_float32(&view, 8, true)};
  return result;
}

static void write_vector(qa_qvm_mod_actors *actors, int64_t address, qa_vec3 value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(memory_of(actors), address, 12, 0, &view);
  qa_qvm_writable_view_set_float32(&view, 0, value.x, true);
  qa_qvm_writable_view_set_float32(&view, 4, value.y, true);
  qa_qvm_writable_view_set_float32(&view, 8, value.z, true);
}

static double seconds_of(qa_qvm_mod_actors *actors) {
  qa_source_time time = actors->options.services->time(actors->options.services->ctx);
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}

static int32_t pointer_for(qa_qvm_mod_actors *actors, const qa_actor_id *actor) {
  return actors->options.pointer(actors->options.ctx, actor);
}

/* TS client(): false return is the TS null client address. */
static bool client_at(qa_qvm_mod_actors *actors, int64_t pointer, int64_t *out) {
  const qa_qvm_mod_combat *combat = actors->options.declaration->combat;
  if (combat == NULL || combat->fields.client == NULL) return false;
  int64_t address = word_at(actors, pointer + (int64_t)combat->fields.client->pointer);
  if (address == 0) return false;
  const qa_qvm_mod_actor_record *record =
      find_record(actors->options.declaration, combat->fields.client->record);
  if (record == NULL || (double)address < record->address ||
      (double)address >= record->address + record->stride * record->capacity ||
      record->stride == 0 ||
      fmod((double)address - record->address, record->stride) != 0)
    qa_range_error("QVM combat client pointer is outside its source declaration");
  *out = address;
  return true;
}

static qa_armor_state armor_at(qa_qvm_mod_actors *actors, int64_t pointer) {
  qa_armor_state armor;
  memset(&armor, 0, sizeof armor);
  armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  armor.powered.kind = "none";
  const qa_qvm_mod_combat *combat = actors->options.declaration->combat;
  int64_t client = 0;
  if (combat != NULL && combat->fields.client != NULL && client_at(actors, pointer, &client)) {
    armor.regular.kind = QA_REGULAR_ARMOR_Q3;
    armor.regular.points = word_at(actors, client + (int64_t)combat->fields.client->armor);
    armor.regular.protection = combat->fields.client->protection;
  }
  return armor;
}

static qa_combat_state read_state(qa_qvm_mod_actors *actors, qa_actor_id actor) {
  const qa_qvm_mod_combat *combat = actors->options.declaration->combat;
  if (combat == NULL) qa_range_error("Missing QVM combat declaration");
  const qa_qvm_mod_combat_fields *fields = &combat->fields;
  int64_t pointer = pointer_for(actors, &actor);
  int32_t flags = word_at(actors, pointer + (int64_t)fields->flags);
  int64_t client = 0;
  bool has_client = client_at(actors, pointer, &client);
  int32_t team = !has_client || fields->client == NULL
                     ? 0
                     : word_at(actors, client + (int64_t)fields->client->team);
  qa_combat_state state;
  memset(&state, 0, sizeof state);
  state.health = word_at(actors, pointer + (int64_t)fields->health);
  state.can_take_damage = word_at(actors, pointer + (int64_t)fields->takedamage) != 0;
  state.armor = armor_at(actors, pointer);
  if (str_eq(combat->abi, "q3-g-damage"))
    state.mass = 200;
  else if (combat->mass.is_constant)
    state.mass = combat->mass.value;
  else if (str_eq(combat->mass.storage, "int32"))
    state.mass = word_at(actors, pointer + (int64_t)combat->mass.offset);
  else
    state.mass = float_at(actors, pointer + (int64_t)combat->mass.offset);
  state.invulnerable = (flags & qa_int32(fields->godmode)) != 0;
  state.has_no_knockback = true;
  state.no_knockback = (flags & qa_int32(fields->no_knockback)) != 0;
  const qa_qvm_mod_actor_record *entity =
      find_record(actors->options.declaration, actors->options.declaration->entity_record);
  bool team_binding = false;
  if (entity != NULL)
    for (size_t i = 0; i < entity->field_count; i++)
      if (str_eq(entity->fields[i].binding, "team")) team_binding = true;
  if (team_binding) {
    state.team = NULL;
    const qa_source_match_services *match = actors->options.services->match;
    if (match != NULL && match->player != NULL) {
      qa_source_match_player_result player = match->player(match->ctx, actor);
      if (player.has_value && player.value.team != NULL)
        state.team = player.value.team(player.value.ctx);
    }
  } else if (str_eq(combat->abi, "q3-g-damage")) {
    state.team = team == 1 ? "q3:1" : team == 2 ? "q3:2" : NULL;
  } else {
    state.team = NULL;
    for (size_t i = 0; i < combat->team_count; i++)
      if (combat->teams[i].value == (double)team) state.team = combat->teams[i].team;
  }
  return state;
}

static int64_t entry_for(qa_qvm_mod_actors *actors, qa_actor_id actor, reaction_kind kind) {
  const qa_qvm_mod_source_actors *source = actors->options.declaration->source_actors;
  if (source == NULL || source->callbacks == NULL) return 0;
  const qa_qvm_mod_source_callbacks *callbacks = source->callbacks;
  bool has = false;
  double offset = 0;
  switch (kind) {
    case REACTION_TOUCH: has = callbacks->has_touch; offset = callbacks->touch; break;
    case REACTION_USE: has = callbacks->has_use; offset = callbacks->use; break;
    case REACTION_PAIN: has = callbacks->has_pain; offset = callbacks->pain; break;
    case REACTION_DIE: has = callbacks->has_die; offset = callbacks->die; break;
  }
  if (!has) return 0;
  return word_at(actors, (int64_t)pointer_for(actors, &actor) + (int64_t)offset);
}

static int32_t invoke_call(qa_qvm_mod_actors *actors, int64_t entry, const double *words,
                           size_t word_count, const qa_qvm_mod_global_value *globals,
                           size_t global_count) {
  qa_qvm_mod_value *arguments = xmalloc((word_count == 0 ? 1 : word_count) * sizeof *arguments);
  for (size_t i = 0; i < word_count; i++) {
    memset(&arguments[i], 0, sizeof arguments[i]);
    arguments[i].kind = "int32";
    arguments[i].value.kind = "float";
    arguments[i].value.number_value = words[i];
  }
  qa_qvm_mod_source_call call;
  memset(&call, 0, sizeof call);
  call.entry = (double)entry;
  call.arguments = arguments;
  call.argument_count = word_count;
  call.globals = globals;
  call.global_count = global_count;
  call.returns = "int32";
  qa_qvm_mod_actor_input input;
  memset(&input, 0, sizeof input);
  input.name = "time";
  input.value.kind = "float";
  input.value.number_value = seconds_of(actors);
  int32_t result = actors->options.invoke(actors->options.ctx, &call, &input, 1);
  free(arguments);
  return result;
}

static double *combat_words(qa_qvm_combat_call call, const qa_qvm_combat_role_value *values,
                            size_t value_count, size_t *out_count) {
  size_t count = qa_qvm_combat_word_count(call);
  double *words = xmalloc((count == 0 ? 1 : count) * sizeof *words);
  qa_qvm_combat_words(call, values, value_count, words);
  *out_count = count;
  return words;
}

static void call_actor(qa_qvm_mod_actors *actors, qa_owned_actor actor, reaction_kind kind,
                       const double *words, size_t word_count) {
  int64_t entry = entry_for(actors, actor.id, kind);
  if (entry != 0) {
    const qa_qvm_mod_combat *combat = actors->options.declaration->combat;
    invoke_call(actors, entry, words, word_count,
                combat != NULL ? combat->fields.globals : NULL,
                combat != NULL ? combat->fields.global_count : 0);
  }
}

static pointer_entry *pointer_entries(qa_qvm_mod_actors *actors) {
  return (pointer_entry *)actors->pointers;
}

static actor_pointer_entry *actor_pointer_entries(qa_qvm_mod_actors *actors) {
  return (actor_pointer_entry *)actors->actor_pointers;
}

static damage_frame **frame_entries(qa_qvm_mod_actors *actors) {
  return (damage_frame **)actors->damage_frames;
}

static void set_pointer_actor(qa_qvm_mod_actors *actors, int32_t pointer, qa_owned_actor actor) {
  pointer_entry *entries = pointer_entries(actors);
  for (size_t i = 0; i < actors->pointer_count; i++)
    if (entries[i].pointer == pointer) { entries[i].actor = actor; return; }
  if (actors->pointer_count == actors->pointer_capacity) {
    actors->pointer_capacity = actors->pointer_capacity == 0 ? 16 : actors->pointer_capacity * 2;
    entries = xrealloc(entries, actors->pointer_capacity * sizeof *entries);
    actors->pointers = entries;
  }
  entries[actors->pointer_count].pointer = pointer;
  entries[actors->pointer_count].actor = actor;
  actors->pointer_count++;
}

static bool get_pointer_actor(qa_qvm_mod_actors *actors, int32_t pointer, qa_owned_actor *out) {
  pointer_entry *entries = pointer_entries(actors);
  for (size_t i = 0; i < actors->pointer_count; i++)
    if (entries[i].pointer == pointer) { *out = entries[i].actor; return true; }
  return false;
}

static void delete_pointer_actor(qa_qvm_mod_actors *actors, int32_t pointer) {
  pointer_entry *entries = pointer_entries(actors);
  for (size_t i = 0; i < actors->pointer_count; i++)
    if (entries[i].pointer == pointer) {
      entries[i] = entries[actors->pointer_count - 1];
      actors->pointer_count--;
      return;
    }
}

static void set_actor_pointer(qa_qvm_mod_actors *actors, qa_actor_id actor, int32_t pointer) {
  actor_pointer_entry *entries = actor_pointer_entries(actors);
  for (size_t i = 0; i < actors->actor_pointer_count; i++)
    if (qa_same_actor(entries[i].actor, actor)) { entries[i].pointer = pointer; return; }
  if (actors->actor_pointer_count == actors->actor_pointer_capacity) {
    actors->actor_pointer_capacity =
        actors->actor_pointer_capacity == 0 ? 16 : actors->actor_pointer_capacity * 2;
    entries = xrealloc(entries, actors->actor_pointer_capacity * sizeof *entries);
    actors->actor_pointers = entries;
  }
  entries[actors->actor_pointer_count].actor = actor;
  entries[actors->actor_pointer_count].pointer = pointer;
  actors->actor_pointer_count++;
}

static bool get_actor_pointer(qa_qvm_mod_actors *actors, qa_actor_id actor, int32_t *out) {
  actor_pointer_entry *entries = actor_pointer_entries(actors);
  for (size_t i = 0; i < actors->actor_pointer_count; i++)
    if (qa_same_actor(entries[i].actor, actor)) { *out = entries[i].pointer; return true; }
  return false;
}

static void delete_actor_pointer(qa_qvm_mod_actors *actors, qa_actor_id actor) {
  actor_pointer_entry *entries = actor_pointer_entries(actors);
  for (size_t i = 0; i < actors->actor_pointer_count; i++)
    if (qa_same_actor(entries[i].actor, actor)) {
      entries[i] = entries[actors->actor_pointer_count - 1];
      actors->actor_pointer_count--;
      return;
    }
}

static void on_release(void *ctx, qa_owned_actor actor) {
  qa_qvm_mod_actors *actors = ctx;
  int32_t pointer = 0;
  if (get_actor_pointer(actors, actor.id, &pointer)) {
    qa_owned_actor current;
    if (get_pointer_actor(actors, pointer, &current) && same_owned(current, actor))
      delete_pointer_actor(actors, pointer);
  }
  delete_actor_pointer(actors, actor.id);
}

static void push_frame(qa_qvm_mod_actors *actors, damage_frame *frame) {
  damage_frame **entries = frame_entries(actors);
  if (actors->damage_frame_count == actors->damage_frame_capacity) {
    actors->damage_frame_capacity =
        actors->damage_frame_capacity == 0 ? 8 : actors->damage_frame_capacity * 2;
    entries = xrealloc(entries, actors->damage_frame_capacity * sizeof *entries);
    actors->damage_frames = entries;
  }
  entries[actors->damage_frame_count++] = frame;
}

static bool body_velocity(qa_qvm_mod_actors *actors, qa_actor_id actor, qa_vec3 *out) {
  qa_body_state_result body =
      qa_shared_body_table_read(actors->options.services->bodies, actor);
  if (!body.has_value) return false;
  *out = body.value.velocity;
  return true;
}

static void flush_frame(qa_qvm_mod_actors *actors, damage_frame *frame) {
  if (frame->finished) return;
  qa_combat_state before = frame->before;
  bool had_velocity = frame->has_velocity;
  qa_vec3 velocity = frame->velocity;
  qa_combat_state after = read_state(actors, frame->request.target);
  qa_vec3 current_velocity = zero_vec;
  bool has_current = body_velocity(actors, frame->request.target, &current_velocity);
  damage_frame **entries = frame_entries(actors);
  for (size_t i = 0; i < actors->damage_frame_count; i++)
    if (qa_same_actor(entries[i]->request.target, frame->request.target)) {
      entries[i]->before = after;
      entries[i]->has_velocity = has_current;
      entries[i]->velocity = current_velocity;
    }
  qa_source_damage_observer *observer = &frame->observer;
  if (observer->stored != NULL) {
    if (before.armor.regular.kind == QA_REGULAR_ARMOR_Q3 &&
        after.armor.regular.kind == QA_REGULAR_ARMOR_Q3 &&
        before.armor.regular.points != after.armor.regular.points) {
      qa_damage_mutation mutation;
      memset(&mutation, 0, sizeof mutation);
      mutation.kind = QA_DAMAGE_MUTATION_ARMOR;
      mutation.armor_before = before.armor;
      mutation.armor_after = after.armor;
      observer->stored(observer->ctx, mutation);
    }
    if (before.health != after.health) {
      qa_damage_mutation mutation;
      memset(&mutation, 0, sizeof mutation);
      mutation.kind = QA_DAMAGE_MUTATION_HEALTH;
      mutation.health_before = before.health;
      mutation.health_after = after.health;
      observer->stored(observer->ctx, mutation);
    }
    if (had_velocity && has_current &&
        (velocity.x != current_velocity.x || velocity.y != current_velocity.y ||
         velocity.z != current_velocity.z)) {
      qa_damage_mutation mutation;
      memset(&mutation, 0, sizeof mutation);
      mutation.kind = QA_DAMAGE_MUTATION_SOURCE_VELOCITY;
      mutation.velocity_before = velocity;
      mutation.velocity_after = current_velocity;
      mutation.movement_provider = frame->request.attack.movement_provider;
      observer->stored(observer->ctx, mutation);
    }
  }
  frame->result.applied_damage =
      frame->result.applied_damage + before.health - after.health;
  frame->result.reaction = "none";
}

typedef struct run_ctx {
  qa_qvm_mod_actors *actors;
  damage_execute_fn execute;
  void *execute_ctx;
} run_ctx;

static qa_source_damage_result run_execute(void *ctx, qa_source_damage_observer observer,
                                           qa_damage_request request) {
  run_ctx *run = ctx;
  qa_qvm_mod_actors *actors = run->actors;
  damage_frame *frame = xmalloc(sizeof *frame);
  memset(frame, 0, sizeof *frame);
  frame->request = request;
  if (request.attack.attacker != NULL) {
    frame->attacker = *request.attack.attacker;
    frame->request.attack.attacker = &frame->attacker;
  }
  if (request.attack.inflictor != NULL) {
    frame->inflictor = *request.attack.inflictor;
    frame->request.attack.inflictor = &frame->inflictor;
  }
  frame->observer = observer;
  frame->before = read_state(actors, frame->request.target);
  frame->has_velocity = body_velocity(actors, frame->request.target, &frame->velocity);
  frame->result.applied_damage = 0;
  frame->result.reaction = "none";
  push_frame(actors, frame);
  run->execute(run->execute_ctx, frame->request);
  flush_frame(actors, frame);
  qa_source_damage_result result = frame->result;
  /* TS finally pops the stack top. */
  if (actors->damage_frame_count > 0) actors->damage_frame_count--;
  free(frame);
  return result;
}

static qa_damage_outcome damage_run(qa_qvm_mod_actors *actors, qa_damage_request input,
                                    damage_execute_fn execute, void *execute_ctx) {
  damage_frame **entries = frame_entries(actors);
  for (size_t i = actors->damage_frame_count; i-- > 0;)
    if (qa_same_actor(entries[i]->request.target, input.target))
      flush_frame(actors, entries[i]);
  run_ctx run = {actors, execute, execute_ctx};
  return qa_gameplay_authority_run_source_damage(actors->options.services->combat, input,
                                                 run_execute, &run);
}

static double *damage_words(qa_qvm_mod_actors *actors, qa_damage_request request,
                            int64_t address, double original_flags, size_t *out_count) {
  write_vector(actors, address, request.direction);
  write_vector(actors, address + 12, request.point);
  qa_qvm_combat_role_value values[8] = {
      {"target", pointer_for(actors, &request.target)},
      {"inflictor", pointer_for(actors, request.attack.inflictor)},
      {"attacker", pointer_for(actors, request.attack.attacker)},
      {"direction", (double)address},
      {"point", (double)(address + 12)},
      {"amount", trunc(request.amount)},
      {"flags", qa_qvm_source_damage_flags(actors->flags, request, original_flags)},
      {"method", request.attack.cause.kind == QA_ATTACK_CAUSE_Q3
                     ? request.attack.cause.means_of_death
                     : 0}};
  return combat_words(actors->calls.damage, values, 8, out_count);
}

typedef struct invoke_damage_ctx {
  qa_qvm_mod_actors *actors;
  qa_damage_request request;
} invoke_damage_ctx;

static int32_t invoke_damage_at(void *ctx, int32_t address) {
  invoke_damage_ctx *damage = ctx;
  const qa_qvm_mod_combat *combat = damage->actors->options.declaration->combat;
  size_t count = 0;
  double *words = damage_words(damage->actors, damage->request, address, 0, &count);
  int32_t result = invoke_call(damage->actors, (int64_t)combat->fields.entry, words, count,
                               combat->fields.globals, combat->fields.global_count);
  free(words);
  return result;
}

static int32_t invoke_damage_execute(void *ctx, qa_damage_request request) {
  qa_qvm_mod_actors *actors = ctx;
  if (actors->options.declaration->combat == NULL)
    qa_range_error("Missing QVM combat declaration");
  invoke_damage_ctx damage = {actors, request};
  return actors->options.scratch(actors->options.ctx, 24, invoke_damage_at, &damage);
}

typedef struct word_edit {
  int64_t index;
  int32_t word;
} word_edit;

static int32_t proceed_call(qa_qvmy_function_call *call) {
  return strcmp(call->execution, "synchronous") == 0 ? call->proceed(call->call_ctx)
                                                     : call->proceed_async(call->call_ctx);
}

/* The TS DataView over call words is mutable; the mirror keeps the
 * pointer const, so edits cast it back for the save/restore window. */
static int32_t proceed_with_edits(qa_qvmy_function_call *call, const word_edit *edits,
                                  size_t edit_count) {
  uint8_t *words = (uint8_t *)call->words;
  word_edit *saved = xmalloc((edit_count == 0 ? 1 : edit_count) * sizeof *saved);
  for (size_t i = 0; i < edit_count; i++) {
    saved[i].index = edits[i].index;
    saved[i].word = bytes_i32(words, (size_t)edits[i].index * 4);
  }
  for (size_t i = 0; i < edit_count; i++)
    bytes_set_i32(words, (size_t)edits[i].index * 4, edits[i].word);
  int32_t result = proceed_call(call);
  for (size_t i = 0; i < edit_count; i++)
    bytes_set_i32(words, (size_t)saved[i].index * 4, saved[i].word);
  free(saved);
  return result;
}

static int32_t call_word(qa_qvmy_function_call *call, int64_t index) {
  return bytes_i32(call->words, (size_t)index * 4);
}

static bool call_actor_at(qa_qvm_mod_actors *actors, qa_qvmy_function_call *call, int64_t index,
                          qa_actor_id *out) {
  return actors->options.actor(actors->options.ctx, call_word(call, index), out);
}

static bool callback_match(qa_qvm_mod_actors *actors, int64_t entry, const uint8_t *words,
                           size_t word_length, qa_owned_actor *out_owner,
                           reaction_kind *out_kind) {
  bool found = false;
  const reaction_kind kinds[] = {REACTION_TOUCH, REACTION_USE, REACTION_PAIN, REACTION_DIE};
  for (size_t i = 0; i < 4; i++) {
    qa_qvm_combat_call definition = call_for(actors, kinds[i]);
    int64_t offset = role_index(definition, "target") * 4;
    if (offset + 4 > (int64_t)word_length) continue;
    qa_owned_actor owner;
    if (!get_pointer_actor(actors, bytes_i32(words, (size_t)offset), &owner)) continue;
    if (entry_for(actors, owner.id, kinds[i]) != entry) continue;
    require_arguments(call_bytes_for(actors, kinds[i]), word_length);
    if (found) qa_range_error("QVM callback entry has ambiguous source signatures");
    found = true;
    *out_owner = owner;
    *out_kind = kinds[i];
  }
  return found;
}

typedef struct use_ctx {
  qa_qvm_mod_actors *actors;
  qa_qvmy_function_call *call;
  qa_owned_actor owner;
  bool has_other;
  qa_actor_id other;
  bool has_activator;
  qa_actor_id activator;
  int64_t role_target, role_other, role_activator;
} use_ctx;

static void use_execute(void *ctx, qa_owned_actor self, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  use_ctx *use = ctx;
  qa_qvm_mod_actors *actors = use->actors;
  bool same_other = (other == NULL && !use->has_other) ||
                    (other != NULL && use->has_other && qa_same_actor(*other, use->other));
  bool same_activator =
      (activator == NULL && !use->has_activator) ||
      (activator != NULL && use->has_activator && qa_same_actor(*activator, use->activator));
  word_edit edits[3] = {
      {use->role_target,
       same_owned(self, use->owner) ? call_word(use->call, use->role_target)
                                    : pointer_for(actors, &self.id)},
      {use->role_other, same_other ? call_word(use->call, use->role_other)
                                   : pointer_for(actors, other)},
      {use->role_activator, same_activator ? call_word(use->call, use->role_activator)
                                           : pointer_for(actors, activator)}};
  proceed_with_edits(use->call, edits, 3);
}

typedef struct touch_ctx {
  qa_qvm_mod_actors *actors;
  qa_qvmy_function_call *call;
  qa_owned_actor owner;
  qa_actor_id other;
  const qa_plane *plane;
  const qa_touch_surface *surface;
  int64_t role_target, role_other;
} touch_ctx;

static void touch_execute(void *ctx, qa_touch_contact effective) {
  touch_ctx *touch = ctx;
  if (effective.plane != touch->plane || effective.surface != touch->surface)
    qa_range_error("QVM source trace changes require callback replacement");
  word_edit edits[2] = {
      {touch->role_target,
       same_owned(effective.self, touch->owner)
           ? call_word(touch->call, touch->role_target)
           : pointer_for(touch->actors, &effective.self.id)},
      {touch->role_other,
       qa_same_actor(effective.other, touch->other)
           ? call_word(touch->call, touch->role_other)
           : pointer_for(touch->actors, &effective.other)}};
  proceed_with_edits(touch->call, edits, 2);
}

typedef struct pain_ctx {
  qa_qvm_mod_actors *actors;
  qa_qvmy_function_call *call;
  qa_owned_actor owner;
  bool has_attacker;
  qa_actor_id attacker;
  int64_t role_target, role_attacker, role_amount;
} pain_ctx;

static void pain_execute(void *ctx, qa_pain_reaction effective) {
  pain_ctx *pain = ctx;
  bool same_attacker =
      (effective.attacker == NULL && !pain->has_attacker) ||
      (effective.attacker != NULL && pain->has_attacker &&
       qa_same_actor(*effective.attacker, pain->attacker));
  word_edit edits[3] = {
      {pain->role_target,
       same_owned(effective.self, pain->owner) ? call_word(pain->call, pain->role_target)
                                               : pointer_for(pain->actors, &effective.self.id)},
      {pain->role_attacker, same_attacker ? call_word(pain->call, pain->role_attacker)
                                          : pointer_for(pain->actors, effective.attacker)},
      {pain->role_amount, (int32_t)trunc(effective.damage)}};
  proceed_with_edits(pain->call, edits, 3);
}

typedef struct die_ctx {
  qa_qvm_mod_actors *actors;
  qa_qvmy_function_call *call;
  qa_owned_actor owner;
  bool has_attacker;
  qa_actor_id attacker;
  bool has_inflictor;
  qa_actor_id inflictor;
  int64_t role_target, role_attacker, role_amount, role_inflictor;
} die_ctx;

static void die_execute(void *ctx, qa_death_reaction effective) {
  die_ctx *die = ctx;
  bool same_attacker =
      (effective.pain.attacker == NULL && !die->has_attacker) ||
      (effective.pain.attacker != NULL && die->has_attacker &&
       qa_same_actor(*effective.pain.attacker, die->attacker));
  bool same_inflictor =
      (effective.inflictor == NULL && !die->has_inflictor) ||
      (effective.inflictor != NULL && die->has_inflictor &&
       qa_same_actor(*effective.inflictor, die->inflictor));
  word_edit edits[4] = {
      {die->role_target,
       same_owned(effective.pain.self, die->owner) ? call_word(die->call, die->role_target)
                                                    : pointer_for(die->actors, &effective.pain.self.id)},
      {die->role_inflictor, same_inflictor ? call_word(die->call, die->role_inflictor)
                                           : pointer_for(die->actors, effective.inflictor)},
      {die->role_attacker, same_attacker ? call_word(die->call, die->role_attacker)
                                         : pointer_for(die->actors, effective.pain.attacker)},
      {die->role_amount, (int32_t)trunc(effective.pain.damage)}};
  proceed_with_edits(die->call, edits, 4);
}

static int32_t source_callback(qa_qvm_mod_actors *actors, int64_t entry,
                               qa_qvmy_function_call *call) {
  qa_owned_actor owner;
  reaction_kind kind;
  if (!callback_match(actors, entry, call->words, call->words_length, &owner, &kind))
    return proceed_call(call);
  qa_actor_callback_table *callbacks = actors->options.services->callbacks;
  if (callbacks == NULL) qa_range_error("Missing shared QVM actor callbacks");
  if (kind == REACTION_USE) {
    qa_qvm_combat_call roles = actors->calls.use;
    use_ctx use;
    memset(&use, 0, sizeof use);
    use.actors = actors;
    use.call = call;
    use.owner = owner;
    use.role_target = role_index(roles, "target");
    use.role_other = role_index(roles, "other");
    use.role_activator = role_index(roles, "activator");
    use.has_other = call_actor_at(actors, call, use.role_other, &use.other);
    use.has_activator = call_actor_at(actors, call, use.role_activator, &use.activator);
    qa_actor_callback_table_source_use(
        callbacks, owner, use.has_other ? &use.other : NULL,
        use.has_activator ? &use.activator : NULL, use_execute, &use);
    return 0;
  }
  if (kind == REACTION_TOUCH) {
    qa_qvm_combat_call roles = actors->calls.touch;
    int64_t role_other = role_index(roles, "other");
    qa_actor_id other;
    if (!call_actor_at(actors, call, role_other, &other)) return proceed_call(call);
    int32_t trace = call_word(call, role_index(roles, "trace"));
    qa_plane plane;
    qa_touch_contact contact;
    memset(&contact, 0, sizeof contact);
    contact.self = owner;
    contact.other = other;
    if (trace != 0) {
      plane.normal = vector_at(actors, (int64_t)trace + 24);
      plane.distance = float_at(actors, (int64_t)trace + 36);
      contact.plane = &plane;
    }
    touch_ctx touch;
    memset(&touch, 0, sizeof touch);
    touch.actors = actors;
    touch.call = call;
    touch.owner = owner;
    touch.other = other;
    touch.plane = contact.plane;
    touch.surface = contact.surface;
    touch.role_target = role_index(roles, "target");
    touch.role_other = role_other;
    qa_actor_callback_table_source_touch(callbacks, contact, touch_execute, &touch);
    return 0;
  }
  damage_frame *frame = NULL;
  damage_frame **entries = frame_entries(actors);
  for (size_t i = actors->damage_frame_count; i-- > 0;)
    if (qa_same_actor(entries[i]->request.target, owner.id)) { frame = entries[i]; break; }
  qa_qvm_combat_call roles = call_for(actors, kind);
  int64_t role_amount = role_index(roles, "amount");
  int64_t role_attacker = role_index(roles, "attacker");
  int32_t amount = call_word(call, role_amount);
  if (frame != NULL && !frame->finished) {
    flush_frame(actors, frame);
    frame->finished = true;
    frame->result.applied_damage = amount;
    frame->result.reaction = kind == REACTION_DIE ? "death" : "pain";
    if (frame->observer.before_reaction != NULL)
      frame->observer.before_reaction(frame->observer.ctx, frame->result);
  }
  qa_actor_id attacker;
  bool has_attacker = call_actor_at(actors, call, role_attacker, &attacker);
  qa_pain_reaction reaction;
  memset(&reaction, 0, sizeof reaction);
  reaction.self = owner;
  reaction.attack = frame != NULL ? &frame->request.attack : NULL;
  reaction.attacker = has_attacker ? &attacker : NULL;
  reaction.damage = amount;
  reaction.kick = frame != NULL ? frame->request.knockback : 0;
  if (kind == REACTION_PAIN) {
    pain_ctx pain;
    memset(&pain, 0, sizeof pain);
    pain.actors = actors;
    pain.call = call;
    pain.owner = owner;
    pain.has_attacker = has_attacker;
    pain.attacker = attacker;
    pain.role_target = role_index(roles, "target");
    pain.role_attacker = role_attacker;
    pain.role_amount = role_amount;
    qa_actor_callback_table_source_pain(callbacks, reaction, pain_execute, &pain);
  } else {
    qa_qvm_combat_call death_roles = actors->calls.die;
    qa_actor_id inflictor;
    int64_t role_inflictor = role_index(death_roles, "inflictor");
    bool has_inflictor = call_actor_at(actors, call, role_inflictor, &inflictor);
    qa_death_reaction death;
    memset(&death, 0, sizeof death);
    death.pain = reaction;
    death.inflictor = has_inflictor ? &inflictor : NULL;
    death.point = frame != NULL ? frame->request.point : zero_vec;
    die_ctx die;
    memset(&die, 0, sizeof die);
    die.actors = actors;
    die.call = call;
    die.owner = owner;
    die.has_attacker = has_attacker;
    die.attacker = attacker;
    die.has_inflictor = has_inflictor;
    die.inflictor = inflictor;
    die.role_target = role_index(roles, "target");
    die.role_attacker = role_attacker;
    die.role_amount = role_amount;
    die.role_inflictor = role_inflictor;
    qa_actor_callback_table_source_die(callbacks, death, die_execute, &die);
  }
  return 0;
}

static int32_t resolved_hook(void *ctx, qa_qvmy_function_call *call) {
  return source_callback((qa_qvm_mod_actors *)ctx, call->instruction_index, call);
}

static bool resolve_callback(void *ctx, int64_t instruction_index, int32_t first_argument,
                             const uint8_t *words, size_t words_length,
                             qa_qvmy_function_hook_fn *out_hook, void **out_hook_ctx) {
  (void)first_argument;
  qa_qvm_mod_actors *actors = ctx;
  qa_owned_actor owner;
  reaction_kind kind;
  if (!callback_match(actors, instruction_index, words, words_length, &owner, &kind))
    return false;
  *out_hook = resolved_hook;
  *out_hook_ctx = actors;
  return true;
}

static void source_request(qa_qvm_mod_actors *actors, qa_qvmy_function_call *call,
                           request_box *out) {
  require_arguments(actors->call_bytes_damage, call->words_length);
  qa_qvm_combat_call roles = actors->calls.damage;
  int64_t role_target = role_index(roles, "target");
  int64_t role_inflictor = role_index(roles, "inflictor");
  int64_t role_attacker = role_index(roles, "attacker");
  int64_t role_direction = role_index(roles, "direction");
  int64_t role_point = role_index(roles, "point");
  int64_t role_amount = role_index(roles, "amount");
  int64_t role_flags = role_index(roles, "flags");
  int64_t role_method = role_index(roles, "method");
  qa_actor_id target;
  if (!call_actor_at(actors, call, role_target, &target))
    qa_range_error("QVM damage target is not a canonical actor");
  const qa_mod_host_services *services = actors->options.services;
  if (services->damage_context == NULL)
    qa_range_error("QVM source damage requires canonical attack provenance");
  memset(out, 0, sizeof *out);
  qa_damage_request *request = &out->request;
  services->damage_context(services->ctx, actors->options.module->profile.module.id,
                           &request->attack.sequence, &request->attack.weapon_provider,
                           &request->attack.combat_provider,
                           &request->attack.inventory_provider,
                           &request->attack.movement_provider);
  request->attack.time = services->time(services->ctx);
  if (call_actor_at(actors, call, role_attacker, &out->attacker))
    request->attack.attacker = &out->attacker;
  if (call_actor_at(actors, call, role_inflictor, &out->inflictor))
    request->attack.inflictor = &out->inflictor;
  request->attack.weapon = NULL;
  request->attack.cause.kind = QA_ATTACK_CAUSE_Q3;
  int32_t flags = call_word(call, role_flags);
  int32_t amount = call_word(call, role_amount);
  request->attack.cause.damage_flags =
      qa_qvm_canonical_damage_flags(actors->flags, flags);
  request->attack.cause.means_of_death = call_word(call, role_method);
  request->target = target;
  request->amount = amount;
  request->knockback =
      (flags & qa_int32(actors->flags.no_knockback)) != 0 || call_word(call, role_direction) == 0
          ? 0
          : amount;
  request->direction = vector_at(actors, call_word(call, role_direction));
  request->point = vector_at(actors, call_word(call, role_point));
  request->normal = zero_vec;
  request->delivery = (flags & qa_int32(actors->flags.radius)) == 0 ? "direct" : "radius";
}

typedef struct replay_ctx {
  qa_qvm_mod_actors *actors;
  qa_qvmy_function_call *call;
  const request_box *original;
  qa_damage_request effective;
} replay_ctx;

static int32_t replay_at(void *ctx, int32_t address) {
  replay_ctx *replay = ctx;
  qa_qvm_mod_actors *actors = replay->actors;
  qa_qvm_combat_call roles = actors->calls.damage;
  int64_t role_direction = role_index(roles, "direction");
  int64_t role_point = role_index(roles, "point");
  int64_t role_attacker = role_index(roles, "attacker");
  int64_t role_inflictor = role_index(roles, "inflictor");
  int64_t role_flags = role_index(roles, "flags");
  const qa_damage_request *effective = &replay->effective;
  const qa_damage_request *original = &replay->original->request;
  size_t count = 0;
  double *words = damage_words(actors, *effective, address,
                               call_word(replay->call, role_flags), &count);
  word_edit *edits = xmalloc((roles.role_count == 0 ? 1 : roles.role_count) * sizeof *edits);
  size_t edit_count = 0;
  for (size_t i = 0; i < roles.role_count; i++) {
    int64_t index = (int64_t)roles.roles[i].index;
    if (index == role_direction && vec_eq(effective->direction, original->direction)) continue;
    if (index == role_point && vec_eq(effective->point, original->point)) continue;
    if (index == role_attacker &&
        actor_ptr_eq(effective->attack.attacker, original->attack.attacker))
      continue;
    if (index == role_inflictor &&
        actor_ptr_eq(effective->attack.inflictor, original->attack.inflictor))
      continue;
    if (index < 0 || (size_t)index >= count) {
      free(words);
      free(edits);
      qa_range_error("Missing declared QVM damage argument");
    }
    edits[edit_count].index = index;
    edits[edit_count].word = qa_int32(words[index]);
    edit_count++;
  }
  int32_t result = proceed_with_edits(replay->call, edits, edit_count);
  free(words);
  free(edits);
  return result;
}

static int32_t replay_execute(void *ctx, qa_damage_request effective) {
  replay_ctx *replay = ctx;
  if (request_deep_equal(&replay->original->request, &effective))
    return proceed_call(replay->call);
  replay->effective = effective;
  return replay->actors->options.scratch(replay->actors->options.ctx, 24, replay_at, replay);
}

typedef struct hook_ctx {
  qa_qvm_mod_actors *actors;
  qa_qvmy_function_call *call;
  const request_box *original;
} hook_ctx;

static qa_damage_outcome hook_source_damage(void *ctx, qa_damage_request composed) {
  hook_ctx *hook = ctx;
  replay_ctx replay;
  memset(&replay, 0, sizeof replay);
  replay.actors = hook->actors;
  replay.call = hook->call;
  replay.original = hook->original;
  return damage_run(hook->actors, composed, replay_execute, &replay);
}

static int32_t combat_hook(void *ctx, qa_qvmy_function_call *call) {
  qa_qvm_mod_actors *actors = ctx;
  request_box box;
  source_request(actors, call, &box);
  qa_gameplay_authority *combat = actors->options.services->combat;
  if (!actors->options.owned_has(actors->options.ctx, box.request.target)) {
    qa_gameplay_authority_apply(combat, box.request, NULL, NULL);
    return 0;
  }
  hook_ctx hook = {actors, call, &box};
  qa_gameplay_authority_apply(combat, box.request, hook_source_damage, &hook);
  return 0;
}

typedef struct touch_call_ctx {
  qa_qvm_mod_actors *actors;
  qa_touch_contact contact;
} touch_call_ctx;

static int32_t touch_at(void *ctx, int32_t address) {
  touch_call_ctx *touch = ctx;
  qa_qvm_mod_actors *actors = touch->actors;
  const qa_touch_contact *contact = &touch->contact;
  qa_vec3 normal = contact->plane != NULL ? contact->plane->normal : zero_vec;
  qa_body_state_result body =
      qa_shared_body_table_read(actors->options.services->bodies, contact->self.id);
  qa_qvm_trace_record record;
  memset(&record, 0, sizeof record);
  record.result.end = body.has_value ? body.value.origin : zero_vec;
  record.result.plane.plane.normal = normal;
  record.result.plane.plane.distance = contact->plane != NULL ? contact->plane->distance : 0;
  record.result.plane.type = normal.x == 1 ? 0 : normal.y == 1 ? 1 : normal.z == 1 ? 2 : 3;
  record.result.plane.signbits =
      (normal.x < 0 ? 1 : 0) + (normal.y < 0 ? 2 : 0) + (normal.z < 0 ? 4 : 0);
  record.entity_num = 1023;
  qa_qvm_writable_view view;
  qa_qvm_memory_view(memory_of(actors), address, QA_QVM_TRACE_BYTES, 0, &view);
  qa_qvm_write_trace_view(&view, &record);
  qa_qvm_combat_role_value values[3] = {
      {"target", pointer_for(actors, &contact->self.id)},
      {"other", pointer_for(actors, &contact->other)},
      {"trace", address}};
  size_t count = 0;
  double *words = combat_words(actors->calls.touch, values, 3, &count);
  call_actor(actors, contact->self, REACTION_TOUCH, words, count);
  free(words);
  return 0;
}

static void actor_touch(void *ctx, qa_touch_contact contact) {
  touch_call_ctx touch = {(qa_qvm_mod_actors *)ctx, contact};
  touch.actors->options.scratch(touch.actors->options.ctx, QA_QVM_TRACE_BYTES, touch_at,
                                 &touch);
}

static void actor_use(void *ctx, qa_owned_actor self, const qa_actor_id *other,
                      const qa_actor_id *activator) {
  qa_qvm_mod_actors *actors = ctx;
  qa_qvm_combat_role_value values[3] = {{"target", pointer_for(actors, &self.id)},
                                        {"other", pointer_for(actors, other)},
                                        {"activator", pointer_for(actors, activator)}};
  size_t count = 0;
  double *words = combat_words(actors->calls.use, values, 3, &count);
  call_actor(actors, self, REACTION_USE, words, count);
  free(words);
}

static void actor_pain(void *ctx, qa_pain_reaction reaction) {
  qa_qvm_mod_actors *actors = ctx;
  qa_qvm_combat_role_value values[3] = {
      {"target", pointer_for(actors, &reaction.self.id)},
      {"attacker", pointer_for(actors, reaction.attacker)},
      {"amount", trunc(reaction.damage)}};
  size_t count = 0;
  double *words = combat_words(actors->calls.pain, values, 3, &count);
  call_actor(actors, reaction.self, REACTION_PAIN, words, count);
  free(words);
}

static void actor_die(void *ctx, qa_death_reaction reaction) {
  qa_qvm_mod_actors *actors = ctx;
  qa_qvm_combat_role_value values[5] = {
      {"target", pointer_for(actors, &reaction.pain.self.id)},
      {"inflictor", pointer_for(actors, reaction.inflictor)},
      {"attacker", pointer_for(actors, reaction.pain.attacker)},
      {"amount", trunc(reaction.pain.damage)},
      {"method", reaction.pain.attack != NULL &&
                         reaction.pain.attack->cause.kind == QA_ATTACK_CAUSE_Q3
                     ? reaction.pain.attack->cause.means_of_death
                     : 0}};
  size_t count = 0;
  double *words = combat_words(actors->calls.die, values, 5, &count);
  call_actor(actors, reaction.pain.self, REACTION_DIE, words, count);
  free(words);
}

static qa_combat_state binding_read(void *ctx) {
  binding_node *node = ctx;
  return read_state(node->actors, node->actor.id);
}

static qa_damage_outcome binding_source_damage(void *ctx, qa_damage_request request) {
  binding_node *node = ctx;
  return damage_run(node->actors, request, invoke_damage_execute, node->actors);
}

static void binding_validate_armor(void *ctx, qa_armor_state armor) {
  (void)ctx;
  if (!str_eq(armor.powered.kind, "none") ||
      (armor.regular.kind != QA_REGULAR_ARMOR_NONE &&
       armor.regular.kind != QA_REGULAR_ARMOR_Q3))
    qa_range_error("Native Q3 armor requires Q3 armor values");
}

static void binding_write_health(void *ctx, double health) {
  binding_node *node = ctx;
  qa_qvm_mod_actors *actors = node->actors;
  const qa_qvm_mod_combat *combat = actors->options.declaration->combat;
  int64_t pointer = pointer_for(actors, &node->actor.id);
  store_word(actors, pointer + (int64_t)combat->fields.health, qa_int32(health));
  int64_t client = 0;
  if (client_at(actors, pointer, &client) && combat->fields.client != NULL)
    store_word(actors, client + (int64_t)combat->fields.client->health, qa_int32(health));
}

static void binding_write_armor(void *ctx, qa_armor_state armor) {
  binding_node *node = ctx;
  qa_qvm_mod_actors *actors = node->actors;
  const qa_qvm_mod_combat *combat = actors->options.declaration->combat;
  int64_t client = 0;
  bool has_client = client_at(actors, pointer_for(actors, &node->actor.id), &client);
  if (!has_client || combat->fields.client == NULL) {
    if (armor.regular.kind != QA_REGULAR_ARMOR_NONE || !str_eq(armor.powered.kind, "none"))
      qa_range_error("QVM actor has no declared armor store");
    return;
  }
  if (!str_eq(armor.powered.kind, "none") ||
      (armor.regular.kind != QA_REGULAR_ARMOR_NONE &&
       armor.regular.kind != QA_REGULAR_ARMOR_Q3))
    qa_range_error("QVM source armor requires Q3 armor values");
  store_word(actors, client + (int64_t)combat->fields.client->armor,
             armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : qa_int32(armor.regular.points));
}

static binding_node **binding_entries(qa_qvm_mod_actors *actors) {
  return (binding_node **)actors->bindings;
}

void qa_qvm_mod_actors_admit(qa_qvm_mod_actors *actors, qa_owned_actor actor) {
  const qa_qvm_mod_combat *combat = actors->options.declaration->combat;
  int32_t pointer = pointer_for(actors, &actor.id);
  set_pointer_actor(actors, pointer, actor);
  set_actor_pointer(actors, actor.id, pointer);
  if (combat != NULL) {
    binding_node *node = xmalloc(sizeof *node);
    node->actors = actors;
    node->actor = actor;
    if (actors->binding_count == actors->binding_capacity) {
      actors->binding_capacity = actors->binding_capacity == 0 ? 16 : actors->binding_capacity * 2;
      binding_node **entries = xrealloc(actors->bindings,
                                        actors->binding_capacity * sizeof *entries);
      actors->bindings = entries;
    }
    binding_entries(actors)[actors->binding_count++] = node;
    qa_combat_state_binding binding;
    memset(&binding, 0, sizeof binding);
    binding.ctx = node;
    binding.read = binding_read;
    binding.source_damage = binding_source_damage;
    binding.validate_armor = binding_validate_armor;
    binding.write_health = binding_write_health;
    binding.write_armor = binding_write_armor;
    qa_gameplay_authority_rebind(actors->options.services->combat, actor, binding);
  }
  if (actors->options.services->callbacks != NULL) {
    qa_actor_callbacks callbacks;
    memset(&callbacks, 0, sizeof callbacks);
    callbacks.ctx = actors;
    callbacks.touch = actor_touch;
    callbacks.use = actor_use;
    callbacks.pain = actor_pain;
    callbacks.die = actor_die;
    qa_actor_callback_table_bind(actors->options.services->callbacks, actor, callbacks);
  }
}

void qa_qvm_mod_actors_clear_actors(qa_qvm_mod_actors *actors) {
  actors->pointer_count = 0;
  actors->actor_pointer_count = 0;
}

void qa_qvm_mod_actors_before_release(qa_qvm_mod_actors *actors, qa_actor_id actor) {
  damage_frame **entries = frame_entries(actors);
  for (size_t i = actors->damage_frame_count; i-- > 0;)
    if (qa_same_actor(entries[i]->request.target, actor)) {
      flush_frame(actors, entries[i]);
      entries[i]->finished = true;
    }
}

void qa_qvm_mod_actors_close(qa_qvm_mod_actors *actors) {
  qa_qvm_module_unbind(actors->options.module, actors->resolver_id);
  qa_session_actor_registry_off_release(actors->options.services->actors, actors->release_id);
  qa_qvm_mod_actors_clear_actors(actors);
  if (actors->has_combat_hook) {
    qa_qvm_module_unbind(actors->options.module, actors->combat_hook);
    actors->has_combat_hook = false;
  }
}

static int64_t argument_bytes(qa_qvm_combat_call call) {
  return (int64_t)(call.role_count + call.extra_count) * 4;
}

void qa_qvm_mod_actors_init(qa_qvm_mod_actors *actors,
                            const qa_qvm_mod_actors_options *options) {
  memset(actors, 0, sizeof *actors);
  actors->options = *options;
  if (options->services->callbacks == NULL)
    qa_range_error("QVM actor semantics require shared actor callbacks");
  const qa_qvm_mod_combat *combat = options->declaration->combat;
  if (combat != NULL && str_eq(combat->abi, "declared")) {
    actors->calls = combat->calls;
    actors->flags = combat->damage_flags;
  } else {
    actors->calls.damage = (qa_qvm_combat_call){legacy_damage_roles, ROLE_COUNT(legacy_damage_roles), NULL, 0};
    actors->calls.touch = (qa_qvm_combat_call){legacy_touch_roles, ROLE_COUNT(legacy_touch_roles), NULL, 0};
    actors->calls.use = (qa_qvm_combat_call){legacy_use_roles, ROLE_COUNT(legacy_use_roles), NULL, 0};
    actors->calls.pain = (qa_qvm_combat_call){legacy_pain_roles, ROLE_COUNT(legacy_pain_roles), NULL, 0};
    actors->calls.die = (qa_qvm_combat_call){legacy_die_roles, ROLE_COUNT(legacy_die_roles), NULL, 0};
    actors->flags = (qa_qvm_damage_flags){1, 2, 4, 8, 16};
  }
  actors->call_bytes_damage = argument_bytes(actors->calls.damage);
  actors->call_bytes_touch = argument_bytes(actors->calls.touch);
  actors->call_bytes_use = argument_bytes(actors->calls.use);
  actors->call_bytes_pain = argument_bytes(actors->calls.pain);
  actors->call_bytes_die = argument_bytes(actors->calls.die);
  actors->resolver_id =
      qa_qvm_module_bind_function_resolver(options->module, resolve_callback, actors);
  actors->release_id =
      qa_session_actor_registry_on_release(options->services->actors, on_release, actors);
  if (combat != NULL) {
    qa_guest_callback_reference reference;
    memset(&reference, 0, sizeof reference);
    reference.kind = QA_GUEST_CALLBACK_QVM;
    reference.module = options->module->profile.module;
    reference.instruction_index = combat->fields.entry;
    actors->combat_hook =
        qa_qvm_module_bind_function(options->module, &reference, combat_hook, actors);
    actors->has_combat_hook = true;
  }
}

void qa_qvm_mod_actors_destroy(qa_qvm_mod_actors *actors) {
  binding_node **bindings = binding_entries(actors);
  for (size_t i = 0; i < actors->binding_count; i++) free(bindings[i]);
  free(actors->bindings);
  damage_frame **frames = frame_entries(actors);
  for (size_t i = 0; i < actors->damage_frame_count; i++) free(frames[i]);
  free(actors->damage_frames);
  free(actors->pointers);
  free(actors->actor_pointers);
  memset(actors, 0, sizeof *actors);
}

