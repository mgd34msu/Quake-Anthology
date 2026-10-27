/* Port of src/compat/qvm/game-combat-binding.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/game_combat_binding.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/entity_record.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/world/gameplay/armor.h"

typedef struct intercept_entry {
  qa_owned_actor actor;
  qa_armor_stage_intercept_fn intercept;
  void *intercept_ctx;
  bool occupied;
} intercept_entry;

typedef struct incoming_damage {
  bool has;
  qa_damage_request request;
  qa_source_damage_observer observer;
  qa_source_damage_result result;
} incoming_damage;

struct qa_qvm_combat_bindings {
  qa_qvm_combat_bindings_options options;
  qa_qvm_game_combat source_combat;
  qa_qvm_damage_scopes *scopes;
  qa_owned_actor admitted[1024];
  bool admitted_present[1024];
  qa_owned_actor proxies[1024];
  bool proxies_present[1024];
  intercept_entry protection_powered[64];
  intercept_entry protection_regular[64];
  size_t removals[8];
  size_t removal_count;
  int64_t release_subscription;
  bool has_armor_hook;
  size_t armor_removal;
  incoming_damage incoming;
  double mode_words[16];
  bool closed;
};

static bool is_integer(double v) { return isfinite(v) && v == trunc(v); }
static bool same_vector(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool actor_id_equal(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token &&
         a.session.token == b.session.token;
}
static bool owned_equal(qa_owned_actor a, qa_owned_actor b) {
  return actor_id_equal(a.id, b.id) && a.token == b.token && strcmp(a.owner, b.owner) == 0;
}

static int32_t call_word(qa_qvmy_function_call *call, double word_index) {
  int64_t offset = (int64_t)word_index * 4;
  if (offset < 0 || offset > (int64_t)call->words_length - 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  const uint8_t *p = call->words + offset;
  uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                  ((uint32_t)p[3] << 24);
  return (int32_t)bits;
}
static void set_call_word(qa_qvmy_function_call *call, double word_index, int32_t value) {
  int64_t offset = (int64_t)word_index * 4;
  if (offset < 0 || offset > (int64_t)call->words_length - 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  uint8_t *p = (uint8_t *)call->words + offset;
  uint32_t bits = (uint32_t)value;
  p[0] = (uint8_t)bits; p[1] = (uint8_t)(bits >> 8); p[2] = (uint8_t)(bits >> 16); p[3] = (uint8_t)(bits >> 24);
}

static bool profile_live(qa_qvm_combat_bindings *bindings, qa_owned_actor actor) {
  if (bindings->closed) return false;
  double slot = 0;
  if (!bindings->options.slot(bindings->options.ctx, actor.id, &slot)) return false;
  qa_qvm_game_damage_state state;
  if (!qa_qvm_game_combat_state(&bindings->source_combat, slot, &state)) return false;
  if (bindings->options.source != NULL) {
    qa_owned_actor resolved;
    if (!qa_session_actor_registry_resolve_owned(bindings->options.source->actors, actor.id,
                                                 &resolved) ||
        !owned_equal(resolved, actor))
      return false;
  }
  return true;
}

static bool live_cb(void *ctx, qa_owned_actor actor) { return profile_live(ctx, actor); }

typedef struct admit_ctx {
  qa_qvm_combat_bindings *bindings;
  double slot;
  qa_owned_actor actor;
} admit_ctx;

static void remove_unused_armor_hook(qa_qvm_combat_bindings *bindings) {
  for (size_t i = 0; i < 64; i++)
    if (bindings->protection_powered[i].occupied || bindings->protection_regular[i].occupied) return;
  if (bindings->has_armor_hook) {
    qa_qvm_module_unbind(&bindings->options.game->module, bindings->armor_removal);
    bindings->has_armor_hook = false;
  }
}

static qa_armor_state bindings_armor(qa_qvm_combat_bindings *bindings, double slot);
static qa_armor_state armor_cb(void *ctx, double slot) { return bindings_armor(ctx, slot); }

static const qa_qvm_armor_tiers *active_tiers(qa_qvm_combat_bindings *bindings) {
  const qa_qvm_game_armor_definition *armor = &bindings->options.definition.armor;
  if (!armor->has_tiers) return NULL;
  const qa_qvm_armor_tiers *tiers = &armor->tiers;
  for (size_t i = 0; i < tiers->when_any_count; i++) {
    qa_qvm_writable_view view;
    qa_qvm_memory_view(bindings->options.game->module.memory, (int64_t)tiers->when_any[i].offset, 4,
                       0, &view);
    int32_t value = qa_qvm_writable_view_get_int32(&view, 0, true);
    bool matches = strcmp(tiers->when_any[i].comparison, "equal") == 0
                       ? value == (int32_t)tiers->when_any[i].value
                       : value != (int32_t)tiers->when_any[i].value;
    if (matches) return tiers;
  }
  return NULL;
}

static double protection_fraction(qa_qvm_combat_bindings *bindings, const double *stats,
                                  const qa_qvm_armor_tiers *tiers) {
  if (tiers == NULL) return bindings->options.definition.armor.protection;
  for (size_t i = 0; i < tiers->value_count; i++)
    if (tiers->values[i].tier == stats[(size_t)tiers->stat]) return tiers->values[i].protection;
  return tiers->fallback;
}

static qa_armor_state bindings_armor(qa_qvm_combat_bindings *bindings, double slot) {
  qa_armor_state state;
  memset(&state, 0, sizeof state);
  state.regular.kind = QA_REGULAR_ARMOR_NONE;
  state.powered.kind = "none";
  if (slot >= qa_qvm_game_data_get_num_clients(&bindings->options.game->data)) return state;
  qa_q3_player_state player;
  double stats[16], persistent[16], powerups[16], ammo[16];
  qa_qvm_game_data_copy_player_state(&bindings->options.game->data, slot, &player, stats, persistent,
                                     powerups, ammo);
  state.regular.kind = QA_REGULAR_ARMOR_Q3;
  state.regular.points = stats[(size_t)bindings->options.definition.armor.points_stat];
  state.regular.protection = protection_fraction(bindings, stats, active_tiers(bindings));
  return state;
}

typedef struct armor_write {
  double points;
  bool has_tier;
  double tier_stat, tier_value;
} armor_write;

static bool armor_write_compute(qa_qvm_combat_bindings *bindings, double slot, qa_armor_state armor,
                                armor_write *out) {
  if (strcmp(armor.powered.kind, "none") != 0 ||
      (armor.regular.kind != QA_REGULAR_ARMOR_NONE && armor.regular.kind != QA_REGULAR_ARMOR_Q3))
    qa_range_error("Native Q3 armor requires Q3 armor values");
  if (slot >= qa_qvm_game_data_get_num_clients(&bindings->options.game->data)) {
    if (armor.regular.kind != QA_REGULAR_ARMOR_NONE)
      qa_range_error("Source non-client has no player armor");
    return false;
  }
  double points = armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : armor.regular.points;
  if (!is_integer(points) || points < 0 || points > 2147483647.0)
    qa_range_error("Source armor points require a nonnegative signed integer");
  out->points = points;
  out->has_tier = false;
  if (armor.regular.kind == QA_REGULAR_ARMOR_NONE) return true;
  qa_q3_player_state player;
  double stats[16], persistent[16], powerups[16], ammo[16];
  qa_qvm_game_data_copy_player_state(&bindings->options.game->data, slot, &player, stats, persistent,
                                     powerups, ammo);
  const qa_qvm_armor_tiers *tiers = active_tiers(bindings);
  double requested = qa_float32(armor.regular.protection);
  if (requested == protection_fraction(bindings, stats, tiers)) return true;
  const qa_qvm_armor_tier_value *selected = NULL;
  if (tiers != NULL)
    for (size_t i = 0; i < tiers->value_count; i++)
      if (tiers->values[i].protection == requested) selected = &tiers->values[i];
  if (tiers == NULL || selected == NULL)
    qa_range_error("Requested protection is not representable by the source armor mode");
  out->has_tier = true;
  out->tier_stat = tiers->stat;
  out->tier_value = selected->tier;
  return true;
}

qa_armor_state qa_qvm_combat_bindings_normalize_legacy_armor(qa_qvm_combat_bindings *bindings,
                                                             qa_actor_id actor,
                                                             qa_armor_state saved) {
  double slot = 0;
  bool has_slot = bindings->options.slot(bindings->options.ctx, actor, &slot);
  if (!has_slot || slot >= 1022) qa_range_error("Legacy QVM armor has no source actor");
  bool matches;
  if (slot < qa_qvm_game_data_get_num_clients(&bindings->options.game->data)) {
    qa_q3_player_state player;
    double stats[16], persistent[16], powerups[16], ammo[16];
    qa_qvm_game_data_copy_player_state(&bindings->options.game->data, slot, &player, stats,
                                       persistent, powerups, ammo);
    matches = saved.regular.kind == QA_REGULAR_ARMOR_Q3 && saved.regular.points == stats[3] &&
              saved.regular.protection == 0.66;
  } else {
    matches = saved.regular.kind == QA_REGULAR_ARMOR_NONE;
  }
  if (!matches || strcmp(saved.powered.kind, "none") != 0)
    qa_range_error("Legacy QVM armor disagrees with the original source projection");
  return bindings_armor(bindings, slot);
}

bool qa_qvm_combat_bindings_notarget(qa_qvm_combat_bindings *bindings, qa_actor_id actor, bool *out) {
  double slot = 0;
  if (!bindings->options.slot(bindings->options.ctx, actor, &slot)) return false;
  qa_qvm_writable_view view;
  qa_qvm_game_data_entity_bytes(&bindings->options.game->data, slot, &view);
  *out = (qa_qvm_writable_view_get_int32(&view, (int64_t)bindings->options.definition.reactions.flags,
                                         true) &
          qa_int32(bindings->options.definition.state.flags.notarget)) != 0;
  return true;
}

static qa_damage_outcome bindings_damage(qa_qvm_combat_bindings *bindings, qa_damage_request input);

static qa_combat_state admit_read(void *ctx) {
  admit_ctx *admit = ctx;
  qa_qvm_combat_bindings *bindings = admit->bindings;
  qa_qvm_primary_combat_profile *definition = &bindings->options.definition;
  qa_qvm_game_damage_state state;
  bool has_state = qa_qvm_game_combat_state(&bindings->source_combat, admit->slot, &state);
  qa_qvm_writable_view view;
  qa_qvm_game_data_entity_bytes(&bindings->options.game->data, admit->slot, &view);
  int32_t flags = qa_qvm_writable_view_get_int32(&view, (int64_t)definition->reactions.flags, true);
  bool has_team = false;
  int32_t team = 0;
  if (admit->slot < qa_qvm_game_data_get_num_clients(&bindings->options.game->data)) {
    qa_qvm_writable_view player;
    qa_qvm_game_data_public_player_bytes(&bindings->options.game->data, admit->slot, &player);
    team = qa_qvm_writable_view_get_int32(&player, 248 + (int64_t)definition->state.team.persistent_stat * 4, true);
    has_team = true;
  }
  const qa_qvm_combat_mass *declared = &definition->state.mass;
  double mass = declared->is_constant ? declared->value
                : strcmp(declared->storage, "float32") == 0
                    ? qa_qvm_writable_view_get_float32(&view, (int64_t)declared->offset, true)
                    : qa_qvm_writable_view_get_int32(&view, (int64_t)declared->offset, true);
  if (!isfinite(mass) || mass < 0) qa_range_error("Source mass is not representable by shared combat");
  const char *team_name = NULL;
  if (has_team)
    for (size_t i = 0; i < definition->state.team.value_count; i++)
      if (definition->state.team.values[i].value == team)
        team_name = definition->state.team.values[i].team;
  qa_combat_state result;
  memset(&result, 0, sizeof result);
  result.health = has_state ? state.health : 0;
  result.armor = bindings_armor(bindings, admit->slot);
  result.mass = mass;
  result.can_take_damage = has_state && state.damageable;
  result.invulnerable = (flags & qa_int32(definition->state.flags.invulnerable)) != 0;
  result.has_no_knockback = true;
  result.no_knockback = (flags & qa_int32(definition->state.flags.no_knockback)) != 0;
  result.team = team_name;
  return result;
}

static qa_damage_outcome admit_source_damage(void *ctx, qa_damage_request request) {
  admit_ctx *admit = ctx;
  return bindings_damage(admit->bindings, request);
}

static void admit_validate_armor(void *ctx, qa_armor_state armor) {
  admit_ctx *admit = ctx;
  armor_write write;
  (void)armor_write_compute(admit->bindings, admit->slot, armor, &write);
}

static void admit_write_health(void *ctx, double health) {
  admit_ctx *admit = ctx;
  qa_qvm_combat_bindings *bindings = admit->bindings;
  qa_qvm_writable_view view;
  qa_qvm_game_data_entity_bytes(&bindings->options.game->data, admit->slot, &view);
  qa_qvm_writable_view_set_int32(&view, (int64_t)bindings->options.definition.base.field_health,
                                 qa_int32(health), true);
  if (admit->slot < qa_qvm_game_data_get_num_clients(&bindings->options.game->data)) {
    qa_qvm_writable_view player;
    qa_qvm_game_data_public_player_bytes(&bindings->options.game->data, admit->slot, &player);
    qa_qvm_writable_view_set_int32(
        &player, 184 + (int64_t)bindings->options.definition.state.health_stat * 4,
        qa_int32(health), true);
  }
}

static void admit_write_armor(void *ctx, qa_armor_state armor) {
  admit_ctx *admit = ctx;
  armor_write write;
  if (!armor_write_compute(admit->bindings, admit->slot, armor, &write)) return;
  qa_qvm_writable_view state;
  qa_qvm_game_data_public_player_bytes(&admit->bindings->options.game->data, admit->slot, &state);
  qa_qvm_writable_view_set_int32(
      &state, 184 + (int64_t)admit->bindings->options.definition.armor.points_stat * 4,
      qa_int32(write.points), true);
  if (write.has_tier)
    qa_qvm_writable_view_set_int32(&state, 184 + (int64_t)write.tier_stat * 4,
                                   qa_int32(write.tier_value), true);
}

static void admit_write_traits(void *ctx, qa_combat_traits traits) {
  admit_ctx *admit = ctx;
  qa_qvm_combat_bindings *bindings = admit->bindings;
  qa_qvm_primary_combat_profile *definition = &bindings->options.definition;
  qa_qvm_writable_view view;
  qa_qvm_game_data_entity_bytes(&bindings->options.game->data, admit->slot, &view);
  qa_qvm_writable_view_set_int32(&view, (int64_t)definition->base.field_takedamage,
                                 traits.can_take_damage ? 1 : 0, true);
  const qa_qvm_combat_mass *declared = &definition->state.mass;
  if (declared->is_constant) {
    if (traits.mass != declared->value)
      qa_range_error("Source mass is constant in the guest profile");
  } else if (strcmp(declared->storage, "float32") == 0) {
    qa_qvm_writable_view_set_float32(&view, (int64_t)declared->offset, qa_float32(traits.mass), true);
  } else {
    qa_qvm_writable_view_set_int32(&view, (int64_t)declared->offset, (int32_t)trunc(traits.mass), true);
  }
  int32_t flags = qa_qvm_writable_view_get_int32(&view, (int64_t)definition->reactions.flags, true);
  flags = traits.invulnerable ? flags | qa_int32(definition->state.flags.invulnerable)
                              : flags & ~qa_int32(definition->state.flags.invulnerable);
  if (traits.has_no_knockback)
    flags = traits.no_knockback ? flags | qa_int32(definition->state.flags.no_knockback)
                                : flags & ~qa_int32(definition->state.flags.no_knockback);
  qa_qvm_writable_view_set_int32(&view, (int64_t)definition->reactions.flags, flags, true);
  if (admit->slot < qa_qvm_game_data_get_num_clients(&bindings->options.game->data)) {
    const qa_qvm_combat_team *team = NULL;
    if (traits.team != NULL)
      for (size_t i = 0; i < definition->state.team.value_count; i++)
        if (strcmp(definition->state.team.values[i].team, traits.team) == 0)
          team = &definition->state.team.values[i];
    if (team != NULL) {
      qa_qvm_writable_view player;
      qa_qvm_game_data_public_player_bytes(&bindings->options.game->data, admit->slot, &player);
      qa_qvm_writable_view_set_int32(
          &player, 248 + (int64_t)definition->state.team.persistent_stat * 4, qa_int32(team->value),
          true);
    } else if (traits.team != NULL) {
      qa_range_error("Source team is not representable in the guest profile");
    }
  } else if (traits.team != NULL) {
    qa_range_error("Source team is not representable for a non-client guest entity");
  }
}

typedef struct stage_ctx {
  qa_qvm_combat_bindings *bindings;
  qa_owned_actor actor;
  qa_protection_channel channel;
} stage_ctx;

static int32_t check_armor_hook(void *ctx, qa_qvmy_function_call *call);
static qa_armor_stage_removal armor_stage_bind(void *ctx, qa_armor_stage_intercept_fn intercept,
                                               void *intercept_ctx);
static void stage_remove(void *ctx) {
  stage_ctx *stage = ctx;
  qa_qvm_combat_bindings *bindings = stage->bindings;
  intercept_entry *entries = stage->channel == QA_PROTECTION_POWERED
                                 ? bindings->protection_powered
                                 : bindings->protection_regular;
  for (size_t i = 0; i < 64; i++)
    if (entries[i].occupied && owned_equal(entries[i].actor, stage->actor) &&
        entries[i].intercept_ctx == ((stage_ctx *)ctx)->bindings /* marker unused */)
      entries[i].occupied = false;
  /* Remove by actor when the intercept matches the stored one. */
  for (size_t i = 0; i < 64; i++)
    if (entries[i].occupied && owned_equal(entries[i].actor, stage->actor)) entries[i].occupied = false;
  remove_unused_armor_hook(bindings);
  free(stage);
}

static qa_source_armor_stage make_armor_stage(qa_qvm_combat_bindings *bindings,
                                              qa_owned_actor actor, qa_protection_channel channel) {
  stage_ctx *stage = malloc(sizeof *stage);
  if (stage == NULL) qa_range_error("out of memory");
  stage->bindings = bindings;
  stage->actor = actor;
  stage->channel = channel;
  qa_source_armor_stage result = {stage, armor_stage_bind};
  return result;
}

static qa_armor_stage_removal armor_stage_bind(void *ctx, qa_armor_stage_intercept_fn intercept,
                                               void *intercept_ctx) {
  stage_ctx *stage = ctx;
  qa_qvm_combat_bindings *bindings = stage->bindings;
  intercept_entry *entries = stage->channel == QA_PROTECTION_POWERED
                                 ? bindings->protection_powered
                                 : bindings->protection_regular;
  if (!profile_live(bindings, stage->actor)) qa_range_error("Source armor owner is retired");
  for (size_t i = 0; i < 64; i++)
    if (entries[i].occupied && owned_equal(entries[i].actor, stage->actor))
      qa_range_error("Source %s armor stage already has an owner",
                     stage->channel == QA_PROTECTION_POWERED ? "powered" : "regular");
  size_t slot = 64;
  for (size_t i = 0; i < 64; i++)
    if (!entries[i].occupied) { slot = i; break; }
  if (slot == 64) qa_range_error("out of memory");
  entries[slot].actor = stage->actor;
  entries[slot].intercept = intercept;
  entries[slot].intercept_ctx = intercept_ctx;
  entries[slot].occupied = true;
  if (!bindings->has_armor_hook) {
    qa_guest_callback_reference reference;
    memset(&reference, 0, sizeof reference);
    reference.kind = QA_GUEST_CALLBACK_QVM;
    reference.module = bindings->options.definition.base.module;
    reference.instruction_index = bindings->options.definition.armor.check_armor;
    bindings->armor_removal = qa_qvm_module_bind_function(&bindings->options.game->module,
                                                          &reference, check_armor_hook, bindings);
    bindings->has_armor_hook = true;
  }
  stage_ctx *removal = malloc(sizeof *removal);
  if (removal == NULL) qa_range_error("out of memory");
  *removal = *stage;
  qa_armor_stage_removal result = {removal, stage_remove};
  return result;
}

void qa_qvm_combat_bindings_admit(qa_qvm_combat_bindings *bindings, qa_owned_actor actor) {
  double slot = 0;
  if (!bindings->options.slot(bindings->options.ctx, actor.id, &slot) || slot >= 1022) return;
  bindings->admitted[(size_t)slot] = actor;
  bindings->admitted_present[(size_t)slot] = true;
  admit_ctx *admit = malloc(sizeof *admit);
  if (admit == NULL) qa_range_error("out of memory");
  admit->bindings = bindings;
  admit->slot = slot;
  admit->actor = actor;
  qa_combat_state_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.ctx = admit;
  binding.source_damage = admit_source_damage;
  binding.read = admit_read;
  binding.write_health = admit_write_health;
  binding.write_armor = admit_write_armor;
  binding.validate_armor = admit_validate_armor;
  binding.write_traits = admit_write_traits;
  binding.protection_regular.declared = true;
  binding.protection_regular.owner = bindings->options.definition.base.module.id;
  binding.protection_powered.declared = true;
  binding.protection_powered.owner_is_null = true;
  if (bindings->options.source != NULL) {
    binding.protection_regular.has_stage = true;
    binding.protection_regular.stage = make_armor_stage(bindings, actor, QA_PROTECTION_REGULAR);
    binding.protection_powered.has_stage = true;
    binding.protection_powered.stage = make_armor_stage(bindings, actor, QA_PROTECTION_POWERED);
  }
  qa_combat_state_result existing = qa_gameplay_authority_read(bindings->options.combat, actor.id);
  if (!existing.has_value) qa_gameplay_authority_bind(bindings->options.combat, actor, binding);
  else qa_gameplay_authority_rebind(bindings->options.combat, actor, binding);
}

static qa_vec3 bindings_vector(qa_qvm_combat_bindings *bindings, int32_t word) {
  qa_vec3 zero = {0, 0, 0};
  if (word == 0) return zero;
  qa_qvm_writable_view view;
  qa_qvm_memory_view(bindings->options.game->module.memory, word, 12, 0, &view);
  qa_vec3 v = {qa_qvm_writable_view_get_float32(&view, 0, true),
               qa_qvm_writable_view_get_float32(&view, 4, true),
               qa_qvm_writable_view_get_float32(&view, 8, true)};
  return v;
}

static intercept_entry *find_intercept(intercept_entry *entries, qa_owned_actor actor) {
  for (size_t i = 0; i < 64; i++)
    if (entries[i].occupied && owned_equal(entries[i].actor, actor)) return &entries[i];
  return NULL;
}

typedef struct proceed_ctx { qa_qvmy_function_call *call; } proceed_ctx;
static double proceed_original(void *ctx) {
  proceed_ctx *proceed = ctx;
  return proceed->call->proceed(proceed->call->call_ctx);
}
static double zero_original(void *ctx) { (void)ctx; return 0; }

static double role_index_of(qa_qvm_combat_call call, const char *role) {
  for (size_t i = 0; i < call.role_count; i++)
    if (strcmp(call.roles[i].role, role) == 0) return call.roles[i].index;
  return -1;
}

static int32_t check_armor_hook(void *ctx, qa_qvmy_function_call *call) {
  qa_qvm_combat_bindings *bindings = ctx;
  qa_qvm_primary_combat_profile *definition = &bindings->options.definition;
  qa_qvm_combat_call roles = definition->armor.call;
  qa_qvm_combat_call damage = definition->base.damage_call;
  qa_qvm_damage_frame *frame = qa_qvm_damage_scopes_current(
      bindings->scopes, call_word(call, role_index_of(roles, "target")));
  if (frame == NULL) return call->proceed(call->call_ctx);
  intercept_entry *power = find_intercept(bindings->protection_powered, frame->actor);
  intercept_entry *regular = find_intercept(bindings->protection_regular, frame->actor);
  if (power == NULL && regular == NULL) return call->proceed(call->call_ctx);
  if (!profile_live(bindings, frame->actor)) {
    qa_qvm_damage_scopes_cancel(bindings->scopes, frame, call);
  }
  int32_t amount = call_word(call, role_index_of(roles, "amount"));
  int32_t flags = call_word(call, role_index_of(roles, "flags"));
  qa_attack_damage_flags originating = qa_attack_damage_flags_of(frame->request);
  qa_armor_stage_input geometry_input;
  memset(&geometry_input, 0, sizeof geometry_input);
  geometry_input.request = frame->request;
  geometry_input.direction =
      bindings_vector(bindings, call_word(frame->call, role_index_of(damage, "direction")));
  geometry_input.point = bindings_vector(bindings, call_word(frame->call, role_index_of(damage, "point")));
  geometry_input.normal = frame->request.normal;
  geometry_input.amount = amount;
  geometry_input.flags.has_stage = true;
  geometry_input.flags.stage = "power";
  geometry_input.flags.no_armor = (flags & qa_int32(definition->damage_flags.no_armor)) != 0;
  geometry_input.flags.no_power_armor = originating.flags.no_power_armor;
  geometry_input.flags.no_regular_armor = false;
  geometry_input.flags.energy = originating.flags.energy;
  double saved = power == NULL ? 0 : power->intercept(power->intercept_ctx, geometry_input, zero_original, NULL);
  if (!profile_live(bindings, frame->actor)) qa_qvm_damage_scopes_cancel(bindings->scopes, frame, call);
  if (!isfinite(saved) || saved < 0 || saved > (amount > 0 ? amount : 0))
    qa_range_error("QVM powered armor savings exceed the current source damage");
  int32_t power_saved = (int32_t)trunc(saved);
  int32_t remaining = amount - power_saved;
  set_call_word(call, role_index_of(roles, "amount"), remaining);
  double regular_saved;
  if (regular == NULL) {
    regular_saved = call->proceed(call->call_ctx);
  } else {
    geometry_input.amount = remaining;
    geometry_input.flags.stage = "regular";
    geometry_input.flags.no_armor =
        (call_word(call, role_index_of(roles, "flags")) & qa_int32(definition->damage_flags.no_armor)) != 0;
    geometry_input.flags.no_power_armor = originating.flags.no_power_armor;
    geometry_input.flags.no_regular_armor = originating.flags.no_regular_armor;
    geometry_input.flags.energy = originating.flags.energy;
    geometry_input.flags.has_regular_protection_scale = true;
    geometry_input.flags.regular_protection_scale =
        originating.flags.has_regular_protection_scale ? originating.flags.regular_protection_scale : 1;
    proceed_ctx proceed = {call};
    regular_saved = regular->intercept(regular->intercept_ctx, geometry_input, proceed_original, &proceed);
  }
  set_call_word(call, role_index_of(roles, "amount"), amount);
  if (!profile_live(bindings, frame->actor)) qa_qvm_damage_scopes_cancel(bindings->scopes, frame, call);
  if (!isfinite(regular_saved) || regular_saved < 0 || regular_saved > (remaining > 0 ? remaining : 0))
    qa_range_error("QVM regular armor savings exceed the current source damage");
  return power_saved + (int32_t)trunc(regular_saved);
}

static bool owned_from_slot(qa_qvm_combat_bindings *bindings, qa_actor_id id, qa_owned_actor *out) {
  double slot = 0;
  if (!bindings->options.slot(bindings->options.ctx, id, &slot)) return false;
  if (slot < 0 || slot >= 1024 || !bindings->admitted_present[(size_t)slot]) return false;
  qa_owned_actor owned = bindings->admitted[(size_t)slot];
  if (!actor_id_equal(owned.id, id) || !profile_live(bindings, owned)) return false;
  *out = owned;
  return true;
}

static bool source_actor(qa_qvm_combat_bindings *bindings, int32_t pointer, qa_owned_actor *out) {
  if (bindings->options.source == NULL || pointer == 0) return false;
  double slot = qa_qvm_game_data_number_from_pointer(&bindings->options.game->data, pointer);
  qa_qvm_game_damage_state state;
  if (slot >= 1022 || !qa_qvm_game_combat_state(&bindings->source_combat, slot, &state)) return false;
  return bindings->options.source->actor(bindings->options.source->ctx, slot, out);
}

static bool text_equal(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static bool request_equal(const qa_damage_request *a, const qa_damage_request *b) {
  if (!actor_id_equal(a->target, b->target) || a->amount != b->amount || a->knockback != b->knockback ||
      !same_vector(a->direction, b->direction) || !same_vector(a->point, b->point) ||
      !same_vector(a->normal, b->normal) || !text_equal(a->delivery, b->delivery))
    return false;
  const qa_attack_provenance *pa = &a->attack, *pb = &b->attack;
  bool attacker = (pa->attacker == NULL) == (pb->attacker == NULL) &&
                  (pa->attacker == NULL || actor_id_equal(*pa->attacker, *pb->attacker));
  bool inflictor = (pa->inflictor == NULL) == (pb->inflictor == NULL) &&
                   (pa->inflictor == NULL || actor_id_equal(*pa->inflictor, *pb->inflictor));
  return attacker && inflictor && text_equal(pa->weapon, pb->weapon) &&
         pa->cause.kind == pb->cause.kind && pa->cause.means_of_death == pb->cause.means_of_death &&
         pa->cause.damage_flags == pb->cause.damage_flags;
}

static qa_qvm_game_damage lower_request(qa_qvm_combat_bindings *bindings, qa_damage_request request,
                                        double slot, double original_flags) {
  qa_qvm_primary_combat_profile *definition = &bindings->options.definition;
  const qa_attack_cause *cause = &request.attack.cause;
  qa_qvm_game_damage lowered;
  memset(&lowered, 0, sizeof lowered);
  lowered.target = slot;
  if (request.attack.attacker != NULL) {
    double attacker_slot = 0;
    lowered.has_attacker =
        bindings->options.slot(bindings->options.ctx, *request.attack.attacker, &attacker_slot);
    lowered.attacker = attacker_slot;
    (void)lowered;
  }
  bool has_inflictor_slot = false;
  double inflictor_slot = 0;
  if (request.attack.inflictor != NULL)
    has_inflictor_slot =
        bindings->options.slot(bindings->options.ctx, *request.attack.inflictor, &inflictor_slot);
  qa_body_state_result body;
  memset(&body, 0, sizeof body);
  if (request.attack.inflictor != NULL)
    body = qa_shared_body_table_read(bindings->options.bodies, *request.attack.inflictor);
  if (has_inflictor_slot) {
    lowered.inflictor_kind = QA_QVM_INFLICTOR_ENTITY;
    lowered.inflictor_slot = inflictor_slot;
  } else if (body.has_value) {
    lowered.inflictor_kind = QA_QVM_INFLICTOR_FOREIGN;
    lowered.inflictor_body = body.value;
  } else {
    lowered.inflictor_kind = QA_QVM_INFLICTOR_NONE;
  }
  lowered.direction = request.direction;
  lowered.point = request.point;
  lowered.amount = trunc(request.amount);
  lowered.flags = qa_qvm_source_damage_flags(definition->damage_flags, request, original_flags);
  const char *weapon = request.attack.weapon;
  bool grapple_weapon = weapon != NULL &&
                        (strcmp(weapon, "q3:weapon_grapplinghook") == 0 ||
                         strcmp(weapon, "q2:weapon_grapple") == 0 ||
                         strcmp(weapon, "q2:weapon_hook") == 0 || strcmp(weapon, "ctf:grapple") == 0);
  lowered.method = grapple_weapon ? definition->grapple_damage_method
                   : cause->kind == QA_ATTACK_CAUSE_Q3 ? cause->means_of_death
                   : cause->kind == QA_ATTACK_CAUSE_Q2 && cause->means_of_death == 56
                       ? definition->grapple_damage_method
                       : 0;
  return lowered;
}

typedef struct invoke_words_ctx {
  qa_qvm_combat_bindings *bindings;
  qa_qvmy_function_call *call;
  qa_damage_request effective, original;
  qa_source_damage_observer observer;
  qa_source_damage_result result;
} invoke_words_ctx;

static void enter_damage_invoke(void *ctx, const double *words, size_t count) {
  (void)count;
  invoke_words_ctx *invoke = ctx;
  qa_qvm_combat_bindings *bindings = invoke->bindings;
  qa_qvm_combat_call roles = bindings->options.definition.base.damage_call;
  struct { double index; int32_t word; } saved[16];
  size_t saved_count = 0;
  for (size_t i = 0; i < roles.role_count; i++) {
    saved[saved_count].index = roles.roles[i].index;
    saved[saved_count].word = call_word(invoke->call, roles.roles[i].index);
    saved_count++;
  }
  for (size_t i = 0; i < saved_count; i++) {
    double index = saved[i].index;
    if ((index == role_index_of(roles, "direction") &&
         same_vector(invoke->effective.direction, invoke->original.direction)) ||
        (index == role_index_of(roles, "point") &&
         same_vector(invoke->effective.point, invoke->original.point)) ||
        (index == role_index_of(roles, "attacker") &&
         ((invoke->effective.attack.attacker == NULL) ==
              (invoke->original.attack.attacker == NULL) &&
          (invoke->effective.attack.attacker == NULL ||
           actor_id_equal(*invoke->effective.attack.attacker, *invoke->original.attack.attacker)))) ||
        (index == role_index_of(roles, "inflictor") &&
         ((invoke->effective.attack.inflictor == NULL) ==
              (invoke->original.attack.inflictor == NULL) &&
          (invoke->effective.attack.inflictor == NULL ||
           actor_id_equal(*invoke->effective.attack.inflictor, *invoke->original.attack.inflictor)))))
      continue;
    if (index < 0 || index >= (double)count)
      qa_range_error("Source damage role lost its declared argument");
    set_call_word(invoke->call, index, qa_int32(words[(size_t)index]));
  }
  qa_owned_actor actor;
  double slot = 0;
  if (owned_from_slot(bindings, invoke->effective.target, &actor) &&
      bindings->options.slot(bindings->options.ctx, invoke->effective.target, &slot))
    invoke->result =
        qa_qvm_damage_scopes_run(bindings->scopes, invoke->call, actor, slot, invoke->effective,
                                 invoke->observer);
  for (size_t i = 0; i < saved_count; i++) set_call_word(invoke->call, saved[i].index, saved[i].word);
}

typedef struct composed_ctx {
  qa_qvm_combat_bindings *bindings;
  qa_qvmy_function_call *call;
  qa_damage_request original;
  int32_t flags;
} composed_ctx;

static qa_source_damage_result composed_execute(void *ctx, qa_source_damage_observer observer,
                                                qa_damage_request effective) {
  composed_ctx *composed = ctx;
  qa_qvm_combat_bindings *bindings = composed->bindings;
  qa_source_damage_result none = {0, "none"};
  qa_owned_actor actor;
  double slot = 0;
  if (!owned_from_slot(bindings, effective.target, &actor) ||
      !bindings->options.slot(bindings->options.ctx, effective.target, &slot))
    return none;
  if (request_equal(&effective, &composed->original))
    return qa_qvm_damage_scopes_run(bindings->scopes, composed->call, actor, slot, effective, observer);
  invoke_words_ctx invoke = {bindings, composed->call, effective, composed->original, observer, none};
  qa_qvm_damage_invoke damage_invoke = {&invoke, enter_damage_invoke};
  qa_qvm_game_combat_damage(&bindings->source_combat,
                            lower_request(bindings, effective, slot, composed->flags),
                            &damage_invoke);
  return invoke.result;
}

static qa_damage_outcome composed_source_damage(void *ctx, qa_damage_request composed_request) {
  composed_ctx *composed = ctx;
  return qa_gameplay_authority_run_source_damage(composed->bindings->options.combat, composed_request,
                                                 composed_execute, composed);
}

static int32_t enter_damage_hook(void *ctx, qa_qvmy_function_call *call) {
  qa_qvm_combat_bindings *bindings = ctx;
  incoming_damage incoming = bindings->incoming;
  bindings->incoming.has = false;
  if (incoming.has) {
    qa_owned_actor actor;
    double slot = 0;
    if (owned_from_slot(bindings, incoming.request.target, &actor) &&
        bindings->options.slot(bindings->options.ctx, incoming.request.target, &slot))
      incoming.result = qa_qvm_damage_scopes_run(bindings->scopes, call, actor, slot,
                                                  incoming.request, incoming.observer);
    bindings->incoming.result = incoming.result;
    return 0;
  }
  if (bindings->options.source == NULL) return call->proceed(call->call_ctx);
  qa_qvm_combat_call roles = bindings->options.definition.base.damage_call;
  qa_owned_actor target;
  if (!source_actor(bindings, call_word(call, role_index_of(roles, "target")), &target))
    return call->proceed(call->call_ctx);
  qa_owned_actor inflictor_owned, attacker_owned;
  bool has_inflictor = source_actor(bindings, call_word(call, role_index_of(roles, "inflictor")), &inflictor_owned);
  bool has_attacker = source_actor(bindings, call_word(call, role_index_of(roles, "attacker")), &attacker_owned);
  qa_actor_id inflictor_id = has_inflictor ? inflictor_owned.id : target.id;
  qa_actor_id attacker_id = has_attacker ? attacker_owned.id : target.id;
  int32_t flags = call_word(call, role_index_of(roles, "flags"));
  int32_t amount = call_word(call, role_index_of(roles, "amount"));
  qa_damage_request request;
  memset(&request, 0, sizeof request);
  request.target = target.id;
  request.amount = amount;
  request.knockback =
      (flags & qa_int32(bindings->options.definition.damage_flags.no_knockback)) != 0 ||
              call_word(call, role_index_of(roles, "direction")) == 0
          ? 0
          : amount;
  request.direction = bindings_vector(bindings, call_word(call, role_index_of(roles, "direction")));
  request.point = bindings_vector(bindings, call_word(call, role_index_of(roles, "point")));
  request.delivery =
      (flags & qa_int32(bindings->options.definition.damage_flags.radius)) != 0 ? "radius" : "direct";
  bindings->options.source->provenance(bindings->options.source->ctx,
                                       has_attacker ? &attacker_id : NULL,
                                       has_inflictor ? &inflictor_id : NULL, target.id, &request.attack);
  request.attack.attacker = has_attacker ? &attacker_id : NULL;
  request.attack.inflictor = has_inflictor ? &inflictor_id : NULL;
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q3;
  request.attack.cause.means_of_death = call_word(call, role_index_of(roles, "method"));
  request.attack.cause.damage_flags =
      qa_qvm_canonical_damage_flags(bindings->options.definition.damage_flags, flags);
  composed_ctx composed = {bindings, call, request, flags};
  (void)qa_gameplay_authority_apply(bindings->options.combat, request, composed_source_damage, &composed);
  return 0;
}

typedef struct damage_execute_ctx {
  qa_qvm_combat_bindings *bindings;
} damage_execute_ctx;

typedef struct module_words_ctx {
  qa_qvm_combat_bindings *bindings;
  incoming_damage *incoming;
} module_words_ctx;

static void module_words_invoke(void *ctx, const double *words, size_t count) {
  module_words_ctx *invoke = ctx;
  qa_qvm_combat_bindings *bindings = invoke->bindings;
  incoming_damage previous = bindings->incoming;
  bindings->incoming = *invoke->incoming;
  bindings->incoming.has = true;
  int32_t *call_words = malloc((count == 0 ? 1 : count) * sizeof(int32_t));
  if (call_words == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) call_words[i] = qa_int32(words[i]);
  (void)qa_qvm_module_call(&bindings->options.game->module, call_words, count,
                           (int64_t)bindings->options.definition.base.callback_damage);
  free(call_words);
  invoke->incoming->result = bindings->incoming.result;
  bindings->incoming = previous;
}

static qa_source_damage_result damage_execute(void *ctx, qa_source_damage_observer observer,
                                              qa_damage_request request) {
  damage_execute_ctx *execute = ctx;
  qa_qvm_combat_bindings *bindings = execute->bindings;
  incoming_damage incoming;
  memset(&incoming, 0, sizeof incoming);
  incoming.request = request;
  incoming.observer = observer;
  incoming.result.reaction = "none";
  double slot = 0;
  if (!bindings->options.slot(bindings->options.ctx, request.target, &slot)) return incoming.result;
  module_words_ctx invoke = {bindings, &incoming};
  qa_qvm_damage_invoke damage_invoke = {&invoke, module_words_invoke};
  qa_qvm_game_combat_damage(&bindings->source_combat, lower_request(bindings, request, slot, 0),
                            &damage_invoke);
  return incoming.result;
}

static qa_damage_outcome bindings_damage(qa_qvm_combat_bindings *bindings, qa_damage_request input) {
  damage_execute_ctx execute = {bindings};
  return qa_gameplay_authority_run_source_damage(bindings->options.combat, input, damage_execute,
                                                 &execute);
}

static int32_t free_hook(void *ctx, qa_qvmy_function_call *call) {
  qa_qvm_combat_bindings *bindings = ctx;
  int32_t pointer = call_word(call, 0);
  double slot = qa_qvm_game_data_number_from_pointer(&bindings->options.game->data, pointer);
  bool has_actor = slot >= 0 && slot < 1024 && bindings->admitted_present[(size_t)slot];
  qa_owned_actor actor;
  if (has_actor) actor = bindings->admitted[(size_t)slot];
  int32_t result = call->proceed(call->call_ctx);
  if (has_actor) {
    qa_owned_actor resolved;
    qa_qvm_game_damage_state state;
    if (qa_session_actor_registry_resolve_owned(bindings->options.source->actors, actor.id, &resolved) &&
        owned_equal(resolved, actor) &&
        !qa_qvm_game_combat_state(&bindings->source_combat, slot, &state))
      qa_session_actor_registry_release(bindings->options.source->actors, actor);
  }
  if (bindings->options.source->after_free != NULL)
    bindings->options.source->after_free(bindings->options.source->ctx, pointer, call);
  return result;
}

static void free_entity(qa_qvm_combat_bindings *bindings, double slot) {
  qa_qvm_game_data_state checkpoint = qa_qvm_game_data_checkpoint(&bindings->options.game->data);
  double pointer = checkpoint.entities_word + slot * bindings->options.definition.base.entity_stride;
  int32_t words[] = {qa_int32(pointer)};
  (void)qa_qvm_module_call(&bindings->options.game->module, words, 1,
                           (int64_t)bindings->options.definition.base.callback_free);
}

static void release_listener(void *ctx, qa_owned_actor actor) {
  qa_qvm_combat_bindings *bindings = ctx;
  for (size_t i = 0; i < 64; i++) {
    if (bindings->protection_powered[i].occupied &&
        owned_equal(bindings->protection_powered[i].actor, actor))
      bindings->protection_powered[i].occupied = false;
    if (bindings->protection_regular[i].occupied &&
        owned_equal(bindings->protection_regular[i].actor, actor))
      bindings->protection_regular[i].occupied = false;
  }
  for (size_t slot = 0; slot < 1024; slot++)
    if (bindings->admitted_present[slot] && owned_equal(bindings->admitted[slot], actor))
      bindings->admitted_present[slot] = false;
  for (size_t slot = 0; slot < 1024; slot++) {
    if (bindings->proxies_present[slot] && owned_equal(bindings->proxies[slot], actor)) {
      bindings->proxies_present[slot] = false;
      if (!bindings->closed) free_entity(bindings, (double)slot);
    }
  }
  remove_unused_armor_hook(bindings);
}

double qa_qvm_combat_bindings_spawn_proxy_entity(qa_qvm_combat_bindings *bindings) {
  if (bindings->closed) qa_range_error("Source combat bindings are retired");
  int32_t pointer = qa_qvm_module_call(&bindings->options.game->module, NULL, 0,
                                       (int64_t)bindings->options.definition.base.callback_allocate);
  double slot = qa_qvm_game_data_number_from_pointer(&bindings->options.game->data, pointer);
  if (slot < 0 || slot >= 1022 || bindings->proxies_present[(size_t)slot] ||
      bindings->admitted_present[(size_t)slot])
    qa_range_error("Source proxy allocation returned an unusable slot");
  qa_qvm_writable_view view;
  qa_qvm_game_data_entity_bytes(&bindings->options.game->data, slot, &view);
  qa_qvm_writable_view_set_int32(&view, (int64_t)bindings->options.definition.base.field_takedamage, 1, true);
  return slot;
}

void qa_qvm_combat_bindings_bind_proxy(qa_qvm_combat_bindings *bindings, double slot,
                                       qa_owned_actor actor) {
  if (slot >= 0 && slot < 1024 && bindings->proxies_present[(size_t)slot])
    qa_range_error("Source proxy slot is already bound");
  double actor_slot = 0;
  if (!bindings->options.slot(bindings->options.ctx, actor.id, &actor_slot) || actor_slot != slot)
    qa_range_error("Source proxy actor has no matching guest slot");
  qa_combat_state_result current = qa_gameplay_authority_read(bindings->options.combat, actor.id);
  bindings->proxies[(size_t)slot] = actor;
  bindings->proxies_present[(size_t)slot] = true;
  qa_qvm_combat_bindings_admit(bindings, actor);
  if (current.has_value) {
    qa_qvm_writable_view view;
    qa_qvm_game_data_entity_bytes(&bindings->options.game->data, slot, &view);
    qa_qvm_writable_view_set_int32(&view, (int64_t)bindings->options.definition.base.field_health,
                                   (int32_t)trunc(current.value.health), true);
  }
}

void qa_qvm_combat_bindings_sync_proxy_entity(qa_qvm_combat_bindings *bindings, double slot,
                                              qa_body_state body) {
  if (slot < 0 || slot >= 1024 || !bindings->proxies_present[(size_t)slot]) return;
  qa_qvm_shared_entity *entity = qa_qvm_game_data_entity(&bindings->options.game->data, slot);
  qa_qvm_shared_entity_set_current_origin(entity, body.origin);
  qa_qvm_shared_entity_set_current_angles(entity, body.angles);
  qa_qvm_shared_entity_set_mins(entity, body.bounds.min);
  qa_qvm_shared_entity_set_maxs(entity, body.bounds.max);
  qa_vec3 absmin = {body.origin.x + body.bounds.min.x, body.origin.y + body.bounds.min.y,
                    body.origin.z + body.bounds.min.z};
  qa_vec3 absmax = {body.origin.x + body.bounds.max.x, body.origin.y + body.bounds.max.y,
                    body.origin.z + body.bounds.max.z};
  qa_qvm_shared_entity_set_absmin(entity, absmin);
  qa_qvm_shared_entity_set_absmax(entity, absmax);
  qa_qvmx_view state_view = {entity->s.view.bytes + entity->s.view.start, (size_t)entity->s.view.length};
  qa_qvm_entity_state borrowed = qa_qvm_borrow_entity_state(state_view, entity->profile);
  qa_qvm_trajectory pos = qa_qvm_entity_state_get_pos(&borrowed);
  pos.base = body.origin;
  pos.delta = body.velocity;
  qa_qvm_entity_state_set_pos(&borrowed, pos);
}

void qa_qvm_combat_bindings_close(qa_qvm_combat_bindings *bindings) {
  if (bindings->closed) return;
  bindings->closed = true;
  memset(bindings->protection_powered, 0, sizeof bindings->protection_powered);
  memset(bindings->protection_regular, 0, sizeof bindings->protection_regular);
  memset(bindings->admitted_present, 0, sizeof bindings->admitted_present);
  memset(bindings->proxies_present, 0, sizeof bindings->proxies_present);
  remove_unused_armor_hook(bindings);
  /* TS removals.runAll(): forward order. */
  for (size_t i = 0; i < bindings->removal_count; i++)
    qa_qvm_module_unbind(&bindings->options.game->module, bindings->removals[i]);
  bindings->removal_count = 0;
  if (bindings->release_subscription != 0 && bindings->options.source != NULL)
    qa_session_actor_registry_off_release(bindings->options.source->actors,
                                          bindings->release_subscription);
}

static void check_stat(double index) {
  if (!is_integer(index) || index < 0 || index >= 16)
    qa_range_error("Source armor stat is outside the public player record");
}
static void check_protection(double value) {
  if (!isfinite(value) || value < 0 || value > 1 || qa_float32(value) != value)
    qa_range_error("Source armor protection must be a binary32 fraction");
}
static void check_mask(double value) {
  if (!is_integer(value) || value <= 0 || value > 4294967295.0 ||
      ((uint32_t)value & ((uint32_t)value - 1)) != 0)
    qa_range_error("Source combat flags require individual 32-bit masks");
}

qa_qvm_combat_bindings *qa_qvm_combat_bindings_create(qa_qvm_combat_bindings_options options) {
  qa_qvm_combat_bindings *bindings = calloc(1, sizeof *bindings);
  if (bindings == NULL) return NULL;
  bindings->options = options;
  qa_qvm_primary_combat_profile *definition = &options.definition;
  qa_qvm_game_combat_definition base = definition->base;
  if (!qa_qvm_game_combat_init(&bindings->source_combat, options.game, options.artifact, base)) {
    free(bindings);
    return NULL;
  }
  double reaction_fields[] = {definition->reactions.flags, definition->reactions.pain,
                              definition->reactions.die};
  for (size_t i = 0; i < 3; i++)
    if (!is_integer(reaction_fields[i]) || reaction_fields[i] < 0 ||
        (int64_t)reaction_fields[i] % 4 != 0 || reaction_fields[i] + 4 > definition->base.entity_stride)
      qa_range_error("Source combat reaction field is outside its entity record");
  const qa_qvm_image *image = &options.artifact->image;
  double check_armor = definition->armor.check_armor;
  if (!is_integer(check_armor) || check_armor < 0 || check_armor >= (double)image->instruction_count ||
      image->instructions[(size_t)check_armor].opcode != QA_QVM_OP_ENTER)
    qa_range_error("Source CheckArmor declaration is not a function entry");
  double client = definition->field_client;
  if (!is_integer(client) || (int64_t)client % 4 != 0 ||
      client < qa_qvm_shared_entity_bytes(definition->base.abi_profile) ||
      client + 4 > definition->base.entity_stride)
    qa_range_error("Source combat requires its declared Q3 client pointer");
  qa_qvm_validate_combat_call(definition->armor.call,
                              (double)(image->data_length + image->literal_length + image->bss_length));
  qa_qvm_reaction_call reaction_calls[] = {definition->reactions.pain_call, definition->reactions.die_call};
  for (size_t i = 0; i < 2; i++) {
    double positions[] = {reaction_calls[i].role_target, reaction_calls[i].role_amount};
    qa_qvm_validate_combat_positions(positions, 2, reaction_calls[i].arguments);
  }
  check_stat(definition->armor.points_stat);
  check_protection(definition->armor.protection);
  check_stat(definition->state.health_stat);
  check_stat(definition->state.team.persistent_stat);
  double flag_groups[2][5] = {
      {definition->state.flags.notarget, definition->state.flags.invulnerable,
       definition->state.flags.no_knockback, 0, 0},
      {definition->damage_flags.radius, definition->damage_flags.no_armor,
       definition->damage_flags.no_knockback, definition->damage_flags.no_protection,
       definition->damage_flags.no_team_protection}};
  size_t flag_counts[2] = {3, 5};
  for (int group = 0; group < 2; group++) {
    for (size_t i = 0; i < flag_counts[group]; i++) {
      check_mask(flag_groups[group][i]);
      for (size_t j = i + 1; j < flag_counts[group]; j++)
        if (flag_groups[group][i] == flag_groups[group][j])
          qa_range_error("Source combat flags overlap");
    }
  }
  for (size_t i = 0; i < definition->state.team.value_count; i++) {
    const qa_qvm_combat_team *value = &definition->state.team.values[i];
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) duplicate = duplicate || definition->state.team.values[j].value == value->value;
    const char *colon = strchr(value->team, ':');
    if (!is_integer(value->value) || value->value < -2147483648.0 || value->value > 2147483647.0 ||
        duplicate || colon == NULL || colon == value->team || colon[1] == '\0')
      qa_range_error("Source team mappings require unique values and explicit identities");
  }
  const qa_qvm_combat_mass *mass = &definition->state.mass;
  if (mass->is_constant) {
    if (!isfinite(mass->value) || mass->value < 0)
      qa_range_error("Source mass must be finite and nonnegative");
  } else if (!is_integer(mass->offset) || (int64_t)mass->offset % 4 != 0 ||
             mass->offset < qa_qvm_shared_entity_bytes(definition->base.abi_profile) ||
             mass->offset + 4 > definition->base.entity_stride) {
    qa_range_error("Source mass field is outside its entity record");
  }
  if (definition->armor.has_tiers) {
    const qa_qvm_armor_tiers *tiers = &definition->armor.tiers;
    check_stat(tiers->stat);
    check_protection(tiers->fallback);
    if (tiers->stat == definition->armor.points_stat || tiers->when_any_count == 0 ||
        tiers->value_count == 0)
      qa_range_error("Source armor tier declaration is incomplete or aliases its points");
    for (size_t i = 0; i < tiers->value_count; i++) {
      bool duplicate = false;
      for (size_t j = 0; j < i; j++) duplicate = duplicate || tiers->values[j].tier == tiers->values[i].tier;
      if (!is_integer(tiers->values[i].tier) || tiers->values[i].tier < -2147483648.0 ||
          tiers->values[i].tier > 2147483647.0 || duplicate)
        qa_range_error("Source armor tiers require unique signed integer values");
      check_protection(tiers->values[i].protection);
    }
    double total = (double)(image->data_length + image->literal_length + image->bss_length);
    for (size_t i = 0; i < tiers->when_any_count; i++) {
      if (!is_integer(tiers->when_any[i].offset) || tiers->when_any[i].offset < 0 ||
          (int64_t)tiers->when_any[i].offset % 4 != 0 || tiers->when_any[i].offset + 4 > total)
        qa_range_error("Source armor mode word is outside source data");
      if (!is_integer(tiers->when_any[i].value) || tiers->when_any[i].value < -2147483648.0 ||
          tiers->when_any[i].value > 2147483647.0)
        qa_range_error("Source armor mode comparison requires a signed integer");
    }
  }
  qa_qvm_damage_scope_options scope_options;
  memset(&scope_options, 0, sizeof scope_options);
  scope_options.game = options.game;
  scope_options.health = definition->base.field_health;
  scope_options.target_argument = role_index_of(definition->base.damage_call, "target");
  scope_options.points_stat = definition->armor.points_stat;
  scope_options.has_tier_stat = definition->armor.has_tiers;
  if (definition->armor.has_tiers) scope_options.tier_stat = definition->armor.tiers.stat;
  double mode_words[16];
  size_t mode_count = 0;
  if (definition->armor.has_tiers)
    for (size_t i = 0; i < definition->armor.tiers.when_any_count && mode_count < 16; i++)
      mode_words[mode_count++] = definition->armor.tiers.when_any[i].offset;
  memcpy(bindings->mode_words, mode_words, mode_count * sizeof(double));
  scope_options.mode_words = bindings->mode_words;
  scope_options.mode_word_count = mode_count;
  scope_options.reactions.pain = definition->reactions.pain;
  scope_options.reactions.die = definition->reactions.die;
  scope_options.reactions.pain_call = definition->reactions.pain_call;
  scope_options.reactions.die_call = definition->reactions.die_call;
  scope_options.ctx = bindings;
  scope_options.armor = armor_cb;
  scope_options.live = live_cb;
  bindings->scopes = qa_qvm_damage_scopes_create(scope_options);
  if (bindings->scopes == NULL) {
    qa_qvm_game_combat_destroy(&bindings->source_combat);
    free(bindings);
    return NULL;
  }
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = definition->base.module;
  reference.instruction_index = definition->base.callback_damage;
  bindings->removals[bindings->removal_count++] =
      qa_qvm_module_bind_invocation(&options.game->module, &reference, enter_damage_hook, bindings);
  if (options.source != NULL) {
    reference.instruction_index = definition->base.callback_free;
    bindings->removals[bindings->removal_count++] =
        qa_qvm_module_bind_invocation(&options.game->module, &reference, free_hook, bindings);
    bindings->release_subscription = qa_session_actor_registry_on_release(
        options.source->actors, release_listener, bindings);
  }
  return bindings;
}

void qa_qvm_combat_bindings_destroy(qa_qvm_combat_bindings *bindings) {
  if (bindings == NULL) return;
  qa_qvm_combat_bindings_close(bindings);
  qa_qvm_damage_scopes_destroy(bindings->scopes);
  qa_qvm_game_combat_destroy(&bindings->source_combat);
  free(bindings);
}
