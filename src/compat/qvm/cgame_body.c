/* Port of src/compat/qvm/cgame-body.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/cgame_body.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

#define MAX_RANGES 64

typedef struct body_range {
  int64_t start, end, entity, state;
  bool hidden;
  const qa_qvm_body_submission *declaration;
  qa_qvm_body_calls calls;
} body_range;

typedef struct body_mesh_active {
  body_range *range;
  int32_t pointer;
  qa_qvm_body_part part;
  int32_t shader;
} body_mesh_active;

struct qa_qvm_body_submissions {
  qa_qvm_module *module;
  const qa_qvm_image *image;
  const qa_qvm_body_submission *declarations;
  size_t declaration_count;
  qa_qvm_body_hidden_fn hidden;
  bool has_capture;
  qa_qvm_body_capture capture;
  qa_qvm_body_calls *calls; /* per declaration */
  body_range ranges[MAX_RANGES];
  size_t range_count;
  body_mesh_active meshes[MAX_RANGES];
  size_t mesh_count;
  size_t *removals;
  size_t removal_count, removal_capacity;
};

static bool is_integer(double v) { return isfinite(v) && v == trunc(v); }
static bool is_safe_integer(double v) {
  return is_integer(v) && v >= -9007199254740991.0 && v <= 9007199254740991.0;
}

qa_qvm_body_submissions *qa_qvm_body_submissions_create(
    qa_qvm_module *module, const qa_qvm_image *image, bool artifact_is_bytecode_cgame,
    const qa_qvm_body_submission *declarations, size_t declaration_count,
    qa_qvm_body_hidden_fn hidden, const qa_qvm_body_capture *capture) {
  if (!artifact_is_bytecode_cgame) qa_range_error("Body submissions require authored cgame bytecode");
  qa_qvm_body_submissions *submissions = calloc(1, sizeof *submissions);
  if (submissions == NULL) return NULL;
  submissions->module = module;
  submissions->image = image;
  submissions->declarations = declarations;
  submissions->declaration_count = declaration_count;
  submissions->hidden = hidden;
  if (capture != NULL) {
    submissions->has_capture = true;
    submissions->capture = *capture;
  }
  submissions->calls = calloc(declaration_count == 0 ? 1 : declaration_count, sizeof(qa_qvm_body_calls));
  if (submissions->calls == NULL) {
    free(submissions);
    return NULL;
  }
  for (size_t i = 0; i < declaration_count; i++) {
    const qa_qvm_body_submission *declaration = &declarations[i];
    bool seen = false;
    for (size_t j = 0; j < i; j++) seen = seen || declarations[j].entry == declaration->entry;
    const qa_qvm_instruction *instruction =
        is_integer(declaration->entry) && declaration->entry >= 0 &&
                declaration->entry < (double)image->instruction_count
            ? &image->instructions[(size_t)declaration->entry]
            : NULL;
    if (instruction == NULL || instruction->opcode != QA_QVM_OP_ENTER || instruction->operand < 8 ||
        seen)
      qa_range_error("Cgame body declaration requires a unique source function entry");
    double arguments[3];
    size_t argument_count = 0;
    arguments[argument_count++] = declaration->actor_argument;
    if (declaration->has_when) arguments[argument_count++] = declaration->when_argument;
    if (declaration->reference_is_argument) arguments[argument_count++] = declaration->reference_index;
    for (size_t a = 0; a < argument_count; a++)
      if (!is_integer(arguments[a]) || arguments[a] < 0 ||
          arguments[a] >= QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS)
        qa_range_error("Cgame body argument is outside the source ABI");
    if (!is_safe_integer(declaration->entity_number_offset) || declaration->entity_number_offset < 0 ||
        (int64_t)declaration->entity_number_offset % 4 != 0)
      qa_range_error("Cgame body entity number requires an aligned field offset");
    if (declaration->has_mesh) {
      qa_qvm_scene_body body = {{declaration->entry, declaration->actor_argument},
                                declaration->mesh};
      if (!qa_qvm_qualify_body_calls(image, body, &submissions->calls[i])) {
        qa_qvm_body_submissions_destroy(submissions);
        return NULL;
      }
    }
  }
  return submissions;
}

void qa_qvm_body_submissions_destroy(qa_qvm_body_submissions *submissions) {
  if (submissions == NULL) return;
  for (size_t i = 0; i < submissions->declaration_count; i++)
    qa_qvm_body_calls_destroy(&submissions->calls[i]);
  free(submissions->calls);
  free(submissions->removals);
  free(submissions);
}

bool qa_qvm_body_submissions_get_captures_player_meshes(
    const qa_qvm_body_submissions *submissions) {
  for (size_t i = 0; i < submissions->declaration_count; i++)
    if (submissions->declarations[i].has_mesh) return true;
  return false;
}

static void run_removals(qa_qvm_body_submissions *submissions) {
  while (submissions->removal_count > 0)
    qa_qvm_module_unbind(submissions->module, submissions->removals[--submissions->removal_count]);
}

static void push_removal(qa_qvm_body_submissions *submissions, size_t id) {
  if (submissions->removal_count == submissions->removal_capacity) {
    size_t capacity = submissions->removal_capacity == 0 ? 8 : submissions->removal_capacity * 2;
    size_t *grown = realloc(submissions->removals, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    submissions->removals = grown;
    submissions->removal_capacity = capacity;
  }
  submissions->removals[submissions->removal_count++] = id;
}

static int32_t call_word(const qa_qvmy_function_call *call, int64_t byte_offset) {
  if (byte_offset < 0 || byte_offset > (int64_t)call->words_length - 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  const uint8_t *p = call->words + byte_offset;
  uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                  ((uint32_t)p[3] << 24);
  return (int32_t)bits;
}

typedef struct hook_ctx {
  qa_qvm_body_submissions *submissions;
  size_t declaration_index;
} hook_ctx;

static int32_t body_hook(void *ctx, qa_qvmy_function_call *call) {
  hook_ctx *hook = ctx;
  qa_qvm_body_submissions *submissions = hook->submissions;
  const qa_qvm_body_submission *declaration = &submissions->declarations[hook->declaration_index];
  if (declaration->has_when &&
      call_word(call, (int64_t)declaration->when_argument * 4) != (int32_t)declaration->when_equals)
    return call->proceed_async(call->call_ctx);
  qa_qvm_writable_view entity_view;
  qa_qvm_memory_view(call->guest, call_word(call, (int64_t)declaration->actor_argument * 4), 4,
                     (int64_t)declaration->entity_number_offset, &entity_view);
  int32_t entity = qa_qvm_writable_view_get_int32(&entity_view, 0, true);
  bool hidden = submissions->hidden.call(submissions->hidden.ctx, entity);
  bool selected = submissions->has_capture &&
                  submissions->capture.selected(submissions->capture.ctx, entity);
  if (!hidden && !selected) return call->proceed_async(call->call_ctx);
  if (selected && !declaration->has_mesh && !hidden) return call->proceed_async(call->call_ctx);
  int64_t caller_stack = call->words_offset - 8;
  int64_t frame_bytes =
      submissions->image->instructions[(size_t)declaration->entry].operand;
  int64_t start;
  if (!declaration->reference_is_argument) {
    start = caller_stack - frame_bytes;
  } else {
    qa_qvm_memory_span span = qa_qvm_memory_get_span(
        call->guest, call_word(call, (int64_t)declaration->reference_index * 4),
        QA_QVM_REF_ENTITY_BYTES, 0);
    start = span.byte_offset;
  }
  if (submissions->range_count >= MAX_RANGES) qa_range_error("out of memory");
  body_range *range = &submissions->ranges[submissions->range_count];
  range->start = start;
  range->end = !declaration->reference_is_argument ? caller_stack : start + QA_QVM_REF_ENTITY_BYTES;
  range->entity = entity;
  range->state = call_word(call, (int64_t)declaration->actor_argument * 4) +
                 (int64_t)declaration->entity_number_offset;
  range->hidden = hidden;
  range->declaration = declaration;
  range->calls = submissions->calls[hook->declaration_index];
  submissions->range_count++;
  int32_t result = call->proceed_async(call->call_ctx);
  if (submissions->range_count == 0 || &submissions->ranges[submissions->range_count - 1] != range)
    qa_range_error("Cgame body submission scopes unwound out of order");
  submissions->range_count--;
  return result;
}

typedef struct mesh_hook_ctx {
  qa_qvm_body_submissions *submissions;
  qa_qvm_scene_body_mesh mesh;
} mesh_hook_ctx;

static int32_t mesh_hook(void *ctx, qa_qvmy_function_call *call) {
  mesh_hook_ctx *hook = ctx;
  qa_qvm_body_submissions *submissions = hook->submissions;
  qa_qvm_scene_body_mesh *mesh = &hook->mesh;
  body_range *range = submissions->range_count == 0
                          ? NULL
                          : &submissions->ranges[submissions->range_count - 1];
  const char *part = NULL;
  if (range != NULL && call->has_caller_instruction) {
    for (size_t i = 0; i < range->calls.count; i++)
      if (range->calls.entries[i].call == (double)call->caller_instruction)
        part = range->calls.entries[i].part;
  }
  if (range == NULL || part == NULL || !range->declaration->has_mesh ||
      range->declaration->mesh.entry != mesh->entry ||
      call_word(call, (int64_t)mesh->state_argument * 4) != range->state)
    return call->proceed_async(call->call_ctx);
  int32_t pointer = call_word(call, (int64_t)mesh->entity_argument * 4);
  qa_qvm_writable_view shader_view;
  qa_qvm_memory_view(call->guest, pointer + (int64_t)mesh->shader_offset, 4, 0, &shader_view);
  if (submissions->mesh_count >= MAX_RANGES) qa_range_error("out of memory");
  body_mesh_active *active = &submissions->meshes[submissions->mesh_count];
  active->range = range;
  active->pointer = pointer;
  active->part = part;
  active->shader = qa_qvm_writable_view_get_int32(&shader_view, 0, true);
  submissions->mesh_count++;
  int32_t result = call->proceed_async(call->call_ctx);
  if (submissions->mesh_count == 0 || &submissions->meshes[submissions->mesh_count - 1] != active)
    qa_range_error("Cgame body mesh scopes unwound out of order");
  submissions->mesh_count--;
  return result;
}

void qa_qvm_body_submissions_enable(qa_qvm_body_submissions *submissions, bool active) {
  if (submissions->range_count != 0)
    qa_range_error("Cannot change body submissions during source rendering");
  if (!active) {
    run_removals(submissions);
    return;
  }
  if (submissions->removal_count != 0) return;
  double mesh_entries[64];
  size_t mesh_entry_count = 0;
  for (size_t i = 0; i < submissions->declaration_count; i++) {
    const qa_qvm_body_submission *declaration = &submissions->declarations[i];
    const qa_qvm_instruction *instruction =
        &submissions->image->instructions[(size_t)declaration->entry];
    if (instruction->opcode != QA_QVM_OP_ENTER) {
      run_removals(submissions);
      qa_range_error("Cgame body entry changed");
    }
    hook_ctx *hook = malloc(sizeof *hook);
    if (hook == NULL) qa_range_error("out of memory");
    hook->submissions = submissions;
    hook->declaration_index = i;
    qa_guest_callback_reference reference;
    memset(&reference, 0, sizeof reference);
    reference.kind = QA_GUEST_CALLBACK_QVM;
    reference.module = qa_qvm_module_get_profile(submissions->module)->module;
    reference.instruction_index = declaration->entry;
    push_removal(submissions,
                 qa_qvm_module_bind_function(submissions->module, &reference, body_hook, hook));
    if (declaration->has_mesh) {
      bool seen = false;
      for (size_t j = 0; j < mesh_entry_count; j++)
        seen = seen || mesh_entries[j] == declaration->mesh.entry;
      if (!seen) {
        mesh_entries[mesh_entry_count++] = declaration->mesh.entry;
        mesh_hook_ctx *mesh = malloc(sizeof *mesh);
        if (mesh == NULL) qa_range_error("out of memory");
        mesh->submissions = submissions;
        mesh->mesh = declaration->mesh;
        qa_guest_callback_reference mesh_reference;
        memset(&mesh_reference, 0, sizeof mesh_reference);
        mesh_reference.kind = QA_GUEST_CALLBACK_QVM;
        mesh_reference.module = qa_qvm_module_get_profile(submissions->module)->module;
        mesh_reference.instruction_index = declaration->mesh.entry;
        push_removal(submissions, qa_qvm_module_bind_function(submissions->module, &mesh_reference,
                                                              mesh_hook, mesh));
      }
    }
  }
}

bool qa_qvm_body_submissions_suppress(qa_qvm_body_submissions *submissions,
                                      const qa_qvm_host_call *call) {
  if (submissions->range_count == 0 || strcmp(call->kind, "engine") != 0 ||
      strcmp(call->role, "cgame") != 0 || call->code != QA_QVM_CGAME_IMPORT_CG_R_ADDREFENTITYTOSCENE)
    return false;
  qa_qvm_memory_span span =
      qa_qvm_memory_get_span(call->guest, qa_qvm_host_call_word(call, 1), QA_QVM_REF_ENTITY_BYTES, 0);
  int64_t start = span.byte_offset;
  body_range *range = NULL;
  for (size_t i = submissions->range_count; i-- > 0;) {
    body_range *candidate = &submissions->ranges[i];
    if (start >= candidate->start && start + QA_QVM_REF_ENTITY_BYTES <= candidate->end) {
      range = candidate;
      break;
    }
  }
  if (range == NULL) return false;
  body_mesh_active *part = submissions->mesh_count == 0
                               ? NULL
                               : &submissions->meshes[submissions->mesh_count - 1];
  if (!range->hidden && part != NULL && part->range == range &&
      part->pointer == qa_qvm_host_call_word(call, 1) && submissions->has_capture &&
      submissions->capture.selected(submissions->capture.ctx, (double)range->entity)) {
    qa_qvm_writable_view view;
    qa_qvm_memory_view(call->guest, qa_qvm_host_call_word(call, 1), QA_QVM_REF_ENTITY_BYTES, 0,
                       &view);
    qa_qvm_source_ref_entity_record source;
    qa_qvm_read_ref_entity(&view, &source);
    if (strcmp(source.kind, "model") == 0 &&
        submissions->capture.submit(submissions->capture.ctx, (double)range->entity, part->part,
                                    &source, source.custom_shader == part->shader))
      return true;
  }
  return range->hidden;
}

void qa_qvm_body_submissions_close(qa_qvm_body_submissions *submissions) {
  qa_qvm_body_submissions_enable(submissions, false);
}
