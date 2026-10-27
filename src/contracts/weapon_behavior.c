/* Port of src/contracts/weapon-behavior.ts (runtime helpers). GPL-2.0-or-later. */
#include "qa/contracts/weapon_behavior.h"

#include <string.h>

static bool string_equal(const char *left, const char *right) {
  if (left == NULL || right == NULL) return left == right;
  return strcmp(left, right) == 0;
}

static bool same_module(qa_module_identity a, qa_module_identity b) {
  return string_equal(a.id, b.id) && string_equal(a.digest, b.digest) &&
         string_equal(a.artifact_path, b.artifact_path) &&
         string_equal(a.revision, b.revision);
}

static bool same_callback_value(qa_weapon_behavior_callback a,
                                qa_weapon_behavior_callback b) {
  if (!same_module(a.module, b.module)) return false;
  switch (a.kind) {
    case QA_WEAPON_CALLBACK_QUAKEC:
      return b.kind == QA_WEAPON_CALLBACK_QUAKEC &&
             a.function_index == b.function_index;
    case QA_WEAPON_CALLBACK_QVM:
      return b.kind == QA_WEAPON_CALLBACK_QVM &&
             a.instruction_index == b.instruction_index;
    case QA_WEAPON_CALLBACK_NATIVE_ARTIFACT:
      return b.kind == QA_WEAPON_CALLBACK_NATIVE_ARTIFACT &&
             a.image_offset == b.image_offset &&
             string_equal(a.abi.kind, b.abi.kind) &&
             string_equal(a.abi.call, b.abi.call) &&
             string_equal(a.abi.image, b.abi.image) &&
             a.abi.pointer_bytes == b.abi.pointer_bytes;
  }
  return false;
}

/* The nullable activate pair: TS returns a === b when either is null. */
static bool same_activate(qa_weapon_behavior_definition left,
                          qa_weapon_behavior_definition right) {
  if (left.has_activate != right.has_activate) return false;
  if (!left.has_activate) return true;
  return same_callback_value(left.activate, right.activate);
}

bool qa_same_weapon_behavior(qa_weapon_behavior_definition left,
                             qa_weapon_behavior_definition right) {
  /* TS deliberately does not compare `title`. */
  return string_equal(left.id, right.id) && string_equal(left.role, right.role) &&
         string_equal(left.aspect, right.aspect) &&
         same_module(left.module, right.module) &&
         same_callback_value(left.fire, right.fire) && same_activate(left, right);
}

bool qa_same_qvm_weapon_layout(qa_qvm_weapon_behavior_layout left,
                               qa_qvm_weapon_behavior_layout right) {
  return left.entity_stride == right.entity_stride &&
         left.level_time == right.level_time && left.allocate == right.allocate &&
         left.free == right.free && left.field_inuse == right.field_inuse &&
         left.field_nextthink == right.field_nextthink &&
         left.field_think == right.field_think &&
         left.field_health == right.field_health &&
         string_equal(left.fire_abi, right.fire_abi);
}
