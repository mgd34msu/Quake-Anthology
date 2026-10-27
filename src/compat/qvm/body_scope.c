/* Port of src/compat/qvm/body-scope.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/body_scope.h"

#include <math.h>
#include <stdlib.h>

#include "qa/foundation/support.h"

static bool is_integer(double v) { return isfinite(v) && v == trunc(v); }
static bool is_safe_integer(double v) {
  return is_integer(v) && v >= -9007199254740991.0 && v <= 9007199254740991.0;
}

static bool valid_call(const qa_qvm_image *image, double player_entry, double end,
                       double mesh_entry, int64_t index) {
  if (index <= 0 || index >= (int64_t)image->instruction_count) return false;
  const qa_qvm_instruction *target = &image->instructions[index - 1];
  return (double)index > player_entry && (double)index < end &&
         image->instructions[index].opcode == QA_QVM_OP_CALL &&
         target->opcode == QA_QVM_OP_CONST && (double)target->operand == mesh_entry;
}

bool qa_qvm_qualify_body_calls(const qa_qvm_image *image, qa_qvm_scene_body body,
                               qa_qvm_body_calls *out) {
  qa_qvm_scene_entry_arg player = body.player;
  qa_qvm_scene_body_mesh mesh = body.mesh;
  double arguments[3] = {player.centity_argument, mesh.entity_argument, mesh.state_argument};
  bool bad_argument = mesh.shader_offset != 112;
  for (int i = 0; i < 3; i++)
    bad_argument = bad_argument || !is_integer(arguments[i]) || arguments[i] < 0 ||
                   arguments[i] >= QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS;
  if (bad_argument) qa_range_error("Source body arguments differ from the original refEntity ABI");
  bool entries_ok =
      is_integer(player.entry) && player.entry >= 0 && player.entry < (double)image->instruction_count &&
      image->instructions[(size_t)player.entry].opcode == QA_QVM_OP_ENTER &&
      is_integer(mesh.entry) && mesh.entry >= 0 && mesh.entry < (double)image->instruction_count &&
      image->instructions[(size_t)mesh.entry].opcode == QA_QVM_OP_ENTER;
  if (!entries_ok) qa_range_error("Source body scope requires original function entries");
  int64_t end = (int64_t)player.entry + 1;
  while (end < (int64_t)image->instruction_count &&
         image->instructions[end].opcode != QA_QVM_OP_ENTER)
    end++;
  size_t capacity = 8, count = 0;
  qa_qvm_body_call *entries = malloc(capacity * sizeof *entries);
  if (entries == NULL) return false;
  if (mesh.parts == NULL) {
    for (int64_t index = (int64_t)player.entry + 1; index < end; index++) {
      if (!valid_call(image, player.entry, (double)end, mesh.entry, index)) continue;
      if (count == capacity) {
        capacity *= 2;
        qa_qvm_body_call *grown = realloc(entries, capacity * sizeof *grown);
        if (grown == NULL) { free(entries); return false; }
        entries = grown;
      }
      entries[count].call = (double)index;
      entries[count].part = "body";
      count++;
    }
  } else {
    for (size_t i = 0; i < mesh.part_count; i++) {
      qa_qvm_body_part_call row = mesh.parts[i];
      bool duplicate = false;
      for (size_t j = 0; j < count; j++) duplicate = duplicate || entries[j].call == row.call;
      if (!is_safe_integer(row.call) ||
          !valid_call(image, player.entry, (double)end, mesh.entry, (int64_t)row.call) ||
          duplicate) {
        free(entries);
        qa_range_error("Source body part does not name a distinct original player-to-mesh call");
      }
      if (count == capacity) {
        capacity *= 2;
        qa_qvm_body_call *grown = realloc(entries, capacity * sizeof *grown);
        if (grown == NULL) { free(entries); return false; }
        entries = grown;
      }
      entries[count].call = row.call;
      entries[count].part = row.part;
      count++;
    }
  }
  if (count == 0) {
    free(entries);
    qa_range_error("Source body scope has no qualified original mesh calls");
  }
  out->entries = entries;
  out->count = count;
  return true;
}

void qa_qvm_body_calls_destroy(qa_qvm_body_calls *calls) {
  if (calls == NULL) return;
  free(calls->entries);
  calls->entries = NULL;
  calls->count = 0;
}
