/* Port of src/content/q1/quakec/damage-call.ts. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/damage_call.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static const char *const ROLE_NAMES[] = {"self", "inflictor", "attacker", "amount"};
static const char *const INPUTS[] = {"self", "inflictor", "attacker", "amount",
                                     "knockback", "point", "direction", "normal", "time"};

static void *xmalloc(size_t size) {
  void *value = malloc(size != 0 ? size : 1);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
static void push_location(qa_q1qc_role_locations *locations, qa_q1qc_damage_role_location location) {
  locations->items = realloc(locations->items, (locations->count + 1) * sizeof *locations->items);
  if (locations->items == NULL) qa_range_error("out of memory");
  locations->items[locations->count++] = location;
}
void qa_q1qc_damage_call_layout_destroy(qa_q1qc_damage_call_layout *layout) {
  if (layout == NULL) return;
  free(layout->parameters);
  for (int i = 0; i < 4; i++) free(layout->roles[i].items);
  free(layout);
}
bool qa_q1qc_damage_roles_equal(const qa_q1qc_damage_call_layout *a,
                                const qa_q1qc_damage_call_layout *b) {
  for (int role = 0; role < 4; role++) {
    if (a->roles[role].count != b->roles[role].count) return false;
    for (size_t i = 0; i < a->roles[role].count; i++) {
      const qa_q1qc_damage_role_location *x = &a->roles[role].items[i];
      const qa_q1qc_damage_role_location *y = &b->roles[role].items[i];
      if (x->is_global != y->is_global) return false;
      if (x->is_global ? x->word != y->word : x->index != y->index || x->frame_word != y->frame_word)
        return false;
    }
  }
  return true;
}
static bool role_of(const char *name, qa_q1qc_damage_role *out) {
  for (int i = 0; i < 4; i++)
    if (strcmp(name, ROLE_NAMES[i]) == 0) { *out = (qa_q1qc_damage_role)i; return true; }
  return false;
}

qa_q1qc_damage_call_layout *qa_q1qc_damage_call_layout_create(
    const qa_q1qc_program *program, const qa_mod_source_call *declaration) {
  const qa_q1qc_function *fn = qa_q1qc_function_named(
      program, declaration != NULL ? declaration->function : "T_Damage");
  if (fn->first_statement <= 0 || fn->named_builtin)
    qa_q1qc_program_error(program, "Unsupported QC damage call: requires an original bytecode function");
  if (declaration != NULL)
    qa_q1qc_validate_source_call(program, declaration, INPUTS, 9, "combat damage");
  qa_q1qc_damage_call_layout *layout = xmalloc(sizeof *layout);
  memset(layout, 0, sizeof *layout);
  layout->function_index = fn->index;
  layout->parameters = xmalloc(fn->parameter_size_count * sizeof *layout->parameters);
  int64_t word = fn->parameter_start;
  for (size_t index = 0; index < fn->parameter_size_count; index++) {
    int64_t size = fn->parameter_sizes[index];
    const char *type = NULL;
    for (size_t g = 0; g < program->global_count; g++) {
      if (program->globals[g].offset == word && strcmp(program->globals[g].type, "void") != 0) {
        type = program->globals[g].type;
        break;
      }
    }
    if (type == NULL || (strcmp(type, "entity") != 0 && strcmp(type, "float") != 0 &&
                         strcmp(type, "vector") != 0 && strcmp(type, "string") != 0 &&
                         strcmp(type, "function") != 0)) {
      qa_q1qc_damage_call_layout_destroy(layout);
      qa_q1qc_program_error(program, "Unsupported QC damage call: untyped original parameter");
    }
    if (size != (strcmp(type, "vector") == 0 ? 3 : 1)) {
      qa_q1qc_damage_call_layout_destroy(layout);
      qa_q1qc_program_error(program, "Unsupported QC damage call: original parameter width");
    }
    const qa_mod_callback_value *value = declaration != NULL && index < declaration->argument_count
                                             ? &declaration->arguments[index] : NULL;
    qa_q1qc_damage_role role;
    bool has_role = false;
    if (declaration == NULL) {
      if (index < 4) { role = (qa_q1qc_damage_role)index; has_role = true; }
    } else if (value != NULL && strcmp(value->kind, "input") == 0) {
      has_role = role_of(value->name, &role);
    }
    if (value != NULL && strcmp(qa_q1qc_source_value_type(value), type) != 0) {
      qa_q1qc_damage_call_layout_destroy(layout);
      qa_q1qc_program_error(program, "Unsupported QC damage call: declaration differs from original parameter types");
    }
    if (has_role) {
      const char *expected = role == QA_Q1QC_ROLE_AMOUNT ? "float" : "entity";
      if (strcmp(type, expected) != 0) {
        qa_q1qc_damage_call_layout_destroy(layout);
        qa_q1qc_program_error(program, "Unsupported QC damage call: invalid %s parameter type", ROLE_NAMES[role]);
      }
      qa_q1qc_damage_role_location location = {.is_global = false, .index = (int64_t)index, .frame_word = word};
      push_location(&layout->roles[role], location);
    }
    layout->parameters[layout->parameter_count++] = type;
    word += size;
  }
  if (declaration != NULL) {
    for (size_t i = 0; i < declaration->global_count; i++) {
      const qa_mod_named_value *global = &declaration->globals[i];
      const qa_q1qc_definition *definition = qa_q1qc_global_by_name(program, global->name);
      if (definition == NULL) {
        qa_q1qc_damage_call_layout_destroy(layout);
        qa_q1qc_program_error(program, "Unsupported QC damage call: missing declared global");
      }
      int64_t width = strcmp(definition->type, "vector") == 0 ? 3 : 1;
      if (definition->offset < 28 ||
          (definition->offset < fn->parameter_start + fn->local_words &&
           definition->offset + width > fn->parameter_start)) {
        qa_q1qc_damage_call_layout_destroy(layout);
        qa_q1qc_program_error(program, "Unsupported QC damage call: declared global overlaps call staging or the callee frame");
      }
      qa_q1qc_damage_role role;
      if (strcmp(global->value.kind, "input") == 0 && role_of(global->value.name, &role)) {
        qa_q1qc_damage_role_location location = {.is_global = true, .word = definition->offset};
        push_location(&layout->roles[role], location);
      }
    }
  }
  int64_t global_words[64];
  int global_roles[64];
  size_t global_count = 0;
  for (int role = 0; role < 4; role++) {
    if (layout->roles[role].count == 0) {
      qa_q1qc_damage_call_layout_destroy(layout);
      qa_q1qc_program_error(program, "Unsupported QC damage call: missing %s input", ROLE_NAMES[role]);
    }
    for (size_t i = 0; i < layout->roles[role].count; i++) {
      const qa_q1qc_damage_role_location *location = &layout->roles[role].items[i];
      if (!location->is_global) continue;
      bool seen = false;
      for (size_t g = 0; g < global_count; g++) {
        if (global_words[g] == location->word) {
          seen = true;
          if (global_roles[g] != role) {
            qa_q1qc_damage_call_layout_destroy(layout);
            qa_q1qc_program_error(program, "Unsupported QC damage call: distinct damage roles overlap one source global");
          }
        }
      }
      if (!seen && global_count < 64) {
        global_words[global_count] = location->word;
        global_roles[global_count] = role;
        global_count++;
      }
    }
  }
  layout->declaration = declaration;
  layout->has_declaration = declaration != NULL;
  return layout;
}
qa_q1qc_damage_call_layout *qa_q1qc_standard_damage_call_layout(int64_t function_index,
                                                                int64_t parameter_start) {
  qa_q1qc_damage_call_layout *layout = xmalloc(sizeof *layout);
  memset(layout, 0, sizeof *layout);
  layout->function_index = function_index;
  static const char *const PARAMETERS[] = {"entity", "entity", "entity", "float"};
  layout->parameters = xmalloc(4 * sizeof *layout->parameters);
  for (int i = 0; i < 4; i++) {
    layout->parameters[i] = PARAMETERS[i];
    qa_q1qc_damage_role_location location = {.is_global = false, .index = i,
                                             .frame_word = parameter_start + i};
    push_location(&layout->roles[i], location);
  }
  layout->parameter_count = 4;
  return layout;
}
static int64_t staging_word(const qa_q1qc_damage_role_location *location) {
  return location->is_global ? location->word : 4 + location->index * 3;
}
static bool same_number(double a, double b) {
  /* TS Object.is on numbers (distinguishes -0, equates NaN). */
  if (isnan(a) && isnan(b)) return true;
  if (a == 0 && b == 0) return signbit(a) == signbit(b);
  return a == b;
}
qa_q1qc_damage_call_values qa_q1qc_read_damage_call(qa_q1qc_machine *machine,
                                                    const qa_q1qc_damage_call_layout *layout) {
  double values[4] = {0, 0, 0, 0};
  for (int role = 0; role < 4; role++) {
    bool have = false;
    double captured = 0;
    for (size_t i = 0; i < layout->roles[role].count; i++) {
      int64_t word = staging_word(&layout->roles[role].items[i]);
      double value = role == QA_Q1QC_ROLE_AMOUNT ? qa_q1qc_words_float(&machine->globals, word)
                                                  : (double)qa_q1qc_words_int(&machine->globals, word);
      if (have && !same_number(captured, value))
        qa_q1qc_error("progs.dat", "Original QC %s aliases disagree", ROLE_NAMES[role]);
      captured = value;
      have = true;
    }
    if (!have) qa_q1qc_error("progs.dat", "Missing qualified QC %s", ROLE_NAMES[role]);
    values[role] = captured;
  }
  qa_q1qc_damage_call_values result = {values[0], values[1], values[2], values[3]};
  return result;
}
typedef struct project_ctx {
  qa_q1qc_machine *machine;
  const qa_q1qc_damage_call_layout *layout;
  qa_q1qc_damage_call_values captured;
  qa_q1qc_damage_call_values effective;
  int64_t saved_words[64];
  int32_t saved_values[64];
  size_t saved_count;
} project_ctx;
static void project_prepare(void *ctx, qa_q1qc_machine *vm) {
  (void)vm;
  project_ctx *project = ctx;
  const qa_q1qc_damage_call_layout *layout = project->layout;
  double captured[4] = {project->captured.self, project->captured.inflictor,
                        project->captured.attacker, project->captured.amount};
  double effective[4] = {project->effective.self, project->effective.inflictor,
                         project->effective.attacker, project->effective.amount};
  for (int role = 0; role < 4; role++) {
    double value = role == QA_Q1QC_ROLE_AMOUNT ? (double)(float)effective[role] : effective[role];
    for (size_t i = 0; i < layout->roles[role].count; i++) {
      const qa_q1qc_damage_role_location *location = &layout->roles[role].items[i];
      int64_t word = staging_word(location);
      double current = !location->is_global ? captured[role]
                       : role == QA_Q1QC_ROLE_AMOUNT
                           ? qa_q1qc_words_float(&project->machine->globals, word)
                           : (double)qa_q1qc_words_int(&project->machine->globals, word);
      if (same_number(current, value)) continue;
      if (location->is_global) {
        bool seen = false;
        for (size_t s = 0; s < project->saved_count; s++) if (project->saved_words[s] == word) seen = true;
        if (!seen && project->saved_count < 64) {
          project->saved_words[project->saved_count] = word;
          project->saved_values[project->saved_count] = qa_q1qc_words_int(&project->machine->globals, word);
          project->saved_count++;
        }
      }
      if (role == QA_Q1QC_ROLE_AMOUNT) qa_q1qc_words_set_float(&project->machine->globals, word, value);
      else qa_q1qc_words_set_int(&project->machine->globals, word, (int64_t)value);
    }
  }
}
void qa_q1qc_project_damage_call(qa_q1qc_machine *machine,
                                 const qa_q1qc_damage_call_layout *layout,
                                 qa_q1qc_damage_call_values captured,
                                 qa_q1qc_damage_call_values effective,
                                 qa_q1qc_function_execution execute) {
  project_ctx project = {.machine = machine, .layout = layout, .captured = captured,
                         .effective = effective, .saved_count = 0};
  execute.run(execute.ctx, project_prepare, &project);
  /* TS restores saved globals in a finally. */
  for (size_t i = 0; i < project.saved_count; i++)
    qa_q1qc_words_set_int(&machine->globals, project.saved_words[i], project.saved_values[i]);
}
