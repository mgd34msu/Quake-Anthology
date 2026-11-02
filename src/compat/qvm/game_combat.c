/* Port of src/compat/qvm/game-combat.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/game_combat.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/entity_record.h"
#include "qa/compat/qvm/shared_entity_record.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/world/gameplay/armor.h"

static bool is_integer(double v) { return isfinite(v) && v == trunc(v); }

size_t qa_qvm_combat_word_count(qa_qvm_combat_call call) {
  return call.role_count + call.extra_count;
}

void qa_qvm_combat_words(qa_qvm_combat_call call, const qa_qvm_combat_role_value *values,
                         size_t value_count, double *out) {
  size_t count = qa_qvm_combat_word_count(call);
  for (size_t i = 0; i < count; i++) out[i] = 0;
  for (size_t i = 0; i < call.extra_count; i++) {
    const qa_qvm_combat_extra *extra = &call.extras[i];
    out[(size_t)extra->index] = strcmp(extra->kind, "float32") == 0
                                    ? (double)(int32_t)qa_float32_to_bits(extra->value)
                                    : extra->value;
  }
  for (size_t i = 0; i < value_count; i++) {
    for (size_t r = 0; r < call.role_count; r++)
      if (strcmp(call.roles[r].role, values[i].role) == 0)
        out[(size_t)call.roles[r].index] = values[i].value;
  }
}

double qa_qvm_canonical_damage_flags(qa_qvm_damage_flags masks, double flags) {
  int32_t word = qa_int32(flags);
  return ((word & qa_int32(masks.radius)) != 0 ? 1 : 0) |
         ((word & qa_int32(masks.no_armor)) != 0 ? 2 : 0) |
         ((word & qa_int32(masks.no_knockback)) != 0 ? 4 : 0) |
         ((word & qa_int32(masks.no_protection)) != 0 ? 8 : 0) |
         ((word & qa_int32(masks.no_team_protection)) != 0 ? 16 : 0);
}

double qa_qvm_source_damage_flags(qa_qvm_damage_flags masks, qa_damage_request request,
                                  double original) {
  qa_attack_damage_flags flags = qa_attack_damage_flags_of(request);
  int32_t declared = qa_int32(masks.radius) | qa_int32(masks.no_armor) |
                     qa_int32(masks.no_knockback) | qa_int32(masks.no_protection) |
                     qa_int32(masks.no_team_protection);
  int32_t result = (qa_int32(original) & ~declared) |
                   (strcmp(request.delivery, "radius") == 0 ? qa_int32(masks.radius) : 0) |
                   (flags.flags.no_armor ? qa_int32(masks.no_armor) : 0) |
                   (flags.no_knockback ? qa_int32(masks.no_knockback) : 0) |
                   (flags.no_protection ? qa_int32(masks.no_protection) : 0) |
                   (flags.no_team_protection ? qa_int32(masks.no_team_protection) : 0);
  return result;
}

void qa_qvm_validate_combat_positions(const double *positions, size_t position_count, double words) {
  if (!is_integer(words) || words < (double)position_count || words > QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS)
    qa_range_error("Source combat call exceeds the QVM OP_ARG capacity or omits required arguments");
  for (size_t i = 0; i < position_count; i++) {
    for (size_t j = i + 1; j < position_count; j++)
      if (positions[i] == positions[j])
        qa_range_error("Source combat argument positions must cover each declared role exactly once within the original call");
    if (!is_integer(positions[i]) || positions[i] < 0 || positions[i] >= words)
      qa_range_error("Source combat argument positions must cover each declared role exactly once within the original call");
  }
}

void qa_qvm_validate_combat_call(qa_qvm_combat_call call, double data_bytes) {
  size_t count = qa_qvm_combat_word_count(call);
  double *positions = malloc((count == 0 ? 1 : count) * sizeof(double));
  if (positions == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < call.role_count; i++) positions[i] = call.roles[i].index;
  for (size_t i = 0; i < call.extra_count; i++) positions[call.role_count + i] = call.extras[i].index;
  qa_qvm_validate_combat_positions(positions, count, (double)count);
  free(positions);
  for (size_t i = 0; i < call.extra_count; i++) {
    const qa_qvm_combat_extra *extra = &call.extras[i];
    if (strcmp(extra->kind, "float32") == 0) {
      if (!isfinite(extra->value) || !isfinite((double)qa_float32(extra->value)))
        qa_range_error("Source combat extra requires a finite binary32 value");
    } else if (strcmp(extra->kind, "address") == 0) {
      if (!is_integer(extra->value) || extra->value < 0 || extra->value >= data_bytes)
        qa_range_error("Source combat extra address is outside its artifact data");
    } else if (!is_integer(extra->value) || extra->value < -2147483648.0 ||
               extra->value > 2147483647.0) {
      qa_range_error("Source combat extra requires a signed integer word");
    }
  }
}

static bool same_text(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

bool qa_qvm_game_combat_init(qa_qvm_game_combat *combat, qa_qvm_game *game,
                             const qa_resolved_qvm_artifact *artifact,
                             qa_qvm_game_combat_definition definition) {
  const qa_module_identity *expected = &definition.module;
  const qa_module_identity *actual = &qa_qvm_module_get_profile(&game->module)->module;
  qa_qvm_abi_profile profile = qa_qvm_module_get_abi_profile(&game->module);
  if (!same_text(expected->id, actual->id) || !same_text(expected->digest, actual->digest) ||
      !same_text(expected->revision, actual->revision) ||
      !same_text(expected->artifact_path, actual->artifact_path) ||
      !same_text(artifact->module.digest, actual->digest) ||
      !same_text(artifact->module.id, actual->id) || !same_text(definition.abi_profile, profile))
    qa_range_error("Source combat declaration differs from its executable");
  double offsets[] = {definition.field_inuse, definition.field_health, definition.field_takedamage,
                      definition.field_parent};
  for (size_t i = 0; i < 4; i++)
    if (!is_integer(offsets[i]) || (int64_t)offsets[i] % 4 != 0 ||
        offsets[i] < qa_qvm_shared_entity_bytes(definition.abi_profile) ||
        offsets[i] + 4 > definition.entity_stride)
      qa_range_error("Source combat field is outside its declared entity record");
  double entries[] = {definition.callback_allocate, definition.callback_free,
                      definition.callback_damage};
  for (size_t i = 0; i < 3; i++) {
    bool entry_ok = is_integer(entries[i]) && entries[i] >= 0 &&
                    entries[i] < (double)artifact->image.instruction_count &&
                    artifact->image.instructions[(size_t)entries[i]].opcode == QA_QVM_OP_ENTER;
    if (!entry_ok) qa_range_error("Source combat callback is not a function entry");
  }
  const qa_qvm_image *image = &artifact->image;
  qa_qvm_validate_combat_call(definition.damage_call,
                              (double)(image->data_length + image->literal_length + image->bss_length));
  combat->game = game;
  combat->definition = definition;
  combat->argument_count = qa_qvm_combat_word_count(definition.damage_call);
  combat->arguments = malloc((combat->argument_count == 0 ? 1 : combat->argument_count) * sizeof(double));
  if (combat->arguments == NULL) return false;
  const qa_qvm_combat_role_value zeros[] = {{"target", 0}, {"inflictor", 0}, {"attacker", 0},
                                            {"direction", 0}, {"point", 0}, {"amount", 0},
                                            {"flags", 0}, {"method", 0}};
  qa_qvm_combat_words(definition.damage_call, zeros, 8, combat->arguments);
  int64_t data_total = image->data_length + image->literal_length + image->bss_length;
  combat->scratch = (data_total + 15) / 16 * 16;
  if (combat->scratch + 24 > image->allocated_data_length - 65536)
    qa_range_error("Source combat requires scratch outside source data and stack");
  return true;
}

void qa_qvm_game_combat_destroy(qa_qvm_game_combat *combat) {
  if (combat == NULL) return;
  free(combat->arguments);
  combat->arguments = NULL;
}

static void entity_view(qa_qvm_game_combat *combat, double slot, qa_qvm_writable_view *out) {
  if (qa_qvm_game_data_get_entity_stride_bytes(&combat->game->data) != combat->definition.entity_stride ||
      qa_qvm_game_data_get_client_stride_bytes(&combat->game->data) != combat->definition.client_stride)
    qa_range_error("Source combat declaration differs from located records");
  qa_qvm_game_data_entity_bytes(&combat->game->data, slot, out);
}

static double entity_pointer(qa_qvm_game_combat *combat, double slot) {
  qa_qvm_writable_view view;
  entity_view(combat, slot, &view);
  (void)view;
  qa_qvm_game_data_state state = qa_qvm_game_data_checkpoint(&combat->game->data);
  return state.entities_word + slot * combat->definition.entity_stride;
}

bool qa_qvm_game_combat_state(qa_qvm_game_combat *combat, double slot,
                              qa_qvm_game_damage_state *out) {
  qa_qvm_writable_view view;
  entity_view(combat, slot, &view);
  if (qa_qvm_writable_view_get_int32(&view, (int64_t)combat->definition.field_inuse, true) == 0)
    return false;
  out->health = qa_qvm_writable_view_get_int32(&view, (int64_t)combat->definition.field_health, true);
  out->damageable =
      qa_qvm_writable_view_get_int32(&view, (int64_t)combat->definition.field_takedamage, true) != 0;
  return true;
}

static double role_index(qa_qvm_combat_call call, const char *role) {
  for (size_t i = 0; i < call.role_count; i++)
    if (strcmp(call.roles[i].role, role) == 0) return call.roles[i].index;
  return -1;
}

void qa_qvm_game_combat_damage(qa_qvm_game_combat *combat, qa_qvm_game_damage hit,
                               const qa_qvm_damage_invoke *invoke) {
  double words_to_check[] = {hit.amount, hit.flags, hit.method};
  for (size_t i = 0; i < 3; i++)
    if (!is_integer(words_to_check[i]) || words_to_check[i] < -2147483648.0 ||
        words_to_check[i] > 2147483647.0)
      qa_range_error("Source damage arguments require signed integer words");
  qa_qvm_game_damage_state state;
  if (!qa_qvm_game_combat_state(combat, hit.target, &state) || !state.damageable) return;
  qa_qvm_memory *memory = combat->game->module.memory;
  uint8_t saved[24];
  memcpy(saved, memory->bytes + combat->scratch, 24);
  qa_qvm_writable_view scratch;
  qa_qvm_memory_data_view(memory, combat->scratch, 24, &scratch);
  qa_qvm_writable_view_set_float32(&scratch, 0, qa_float32(hit.direction.x), true);
  qa_qvm_writable_view_set_float32(&scratch, 4, qa_float32(hit.direction.y), true);
  qa_qvm_writable_view_set_float32(&scratch, 8, qa_float32(hit.direction.z), true);
  qa_qvm_writable_view_set_float32(&scratch, 12, qa_float32(hit.point.x), true);
  qa_qvm_writable_view_set_float32(&scratch, 16, qa_float32(hit.point.y), true);
  qa_qvm_writable_view_set_float32(&scratch, 20, qa_float32(hit.point.z), true);
  bool has_temporary = false;
  int32_t temporary = 0;
  double attacker = hit.has_attacker ? entity_pointer(combat, hit.attacker) : 0;
  double inflictor = 0;
  if (hit.inflictor_kind == QA_QVM_INFLICTOR_ENTITY) {
    inflictor = entity_pointer(combat, hit.inflictor_slot);
  } else if (hit.inflictor_kind == QA_QVM_INFLICTOR_FOREIGN) {
    temporary = qa_qvm_module_call(&combat->game->module, NULL, 0,
                                   (int64_t)combat->definition.callback_allocate);
    has_temporary = true;
    double slot = qa_qvm_game_data_number_from_pointer(&combat->game->data, temporary);
    qa_qvm_shared_entity *entity = qa_qvm_game_data_entity(&combat->game->data, slot);
    qa_qvm_writable_view view;
    entity_view(combat, slot, &view);
    qa_qvm_writable_view_set_int32(&view, (int64_t)combat->definition.field_parent,
                                   qa_int32(attacker), true);
    qa_qvm_shared_entity_set_current_origin(entity, hit.inflictor_body.origin);
    qa_qvm_shared_entity_set_current_angles(entity, hit.inflictor_body.angles);
    qa_qvm_shared_entity_set_mins(entity, hit.inflictor_body.bounds.min);
    qa_qvm_shared_entity_set_maxs(entity, hit.inflictor_body.bounds.max);
    /* entity.s.pos with base/delta replaced (entity-record.ts surface). */
    qa_qvmx_view state_view = entity->s.view;
    qa_qvm_entity_state borrowed = qa_qvm_borrow_entity_state(state_view, entity->profile);
    qa_qvm_trajectory pos = qa_qvm_entity_state_get_pos(&borrowed);
    pos.base = hit.inflictor_body.origin;
    pos.delta = hit.inflictor_body.velocity;
    qa_qvm_entity_state_set_pos(&borrowed, pos);
    inflictor = temporary;
  }
  double *words = malloc((combat->argument_count == 0 ? 1 : combat->argument_count) * sizeof(double));
  if (words == NULL) qa_range_error("out of memory");
  memcpy(words, combat->arguments, combat->argument_count * sizeof(double));
  qa_qvm_combat_call call = combat->definition.damage_call;
  words[(size_t)role_index(call, "target")] = entity_pointer(combat, hit.target);
  words[(size_t)role_index(call, "inflictor")] = inflictor;
  words[(size_t)role_index(call, "attacker")] = attacker;
  words[(size_t)role_index(call, "direction")] = (double)combat->scratch;
  words[(size_t)role_index(call, "point")] = (double)combat->scratch + 12;
  words[(size_t)role_index(call, "amount")] = hit.amount;
  words[(size_t)role_index(call, "flags")] = hit.flags;
  words[(size_t)role_index(call, "method")] = hit.method;
  if (invoke != NULL) {
    invoke->call(invoke->ctx, words, combat->argument_count);
  } else {
    int32_t *call_words = malloc((combat->argument_count == 0 ? 1 : combat->argument_count) * sizeof(int32_t));
    if (call_words == NULL) qa_range_error("out of memory");
    for (size_t i = 0; i < combat->argument_count; i++) call_words[i] = qa_int32(words[i]);
    (void)qa_qvm_module_call(&combat->game->module, call_words, combat->argument_count,
                             (int64_t)combat->definition.callback_damage);
    free(call_words);
  }
  free(words);
  if (has_temporary) {
    int32_t free_words[] = {temporary};
    (void)qa_qvm_module_call(&combat->game->module, free_words, 1,
                             (int64_t)combat->definition.callback_free);
  }
  qa_qvm_memory_write_bytes(memory, combat->scratch, saved, 24);
}
