/* Port of src/content/q1/quakec/weapon-stage.ts. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/weapon_stage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *value = malloc(size != 0 ? size : 1);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
static void *xrealloc(void *ptr, size_t size) {
  void *value = realloc(ptr, size != 0 ? size : 1);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}
typedef struct stage_builder {
  qa_q1qc_weapon_stage stage;
  size_t continuation_capacity, repeat_capacity;
} stage_builder;
static void builder_continuation(stage_builder *builder, int64_t function_index) {
  if (qa_q1qc_weapon_stage_has_continuation(&builder->stage, function_index)) return;
  if (builder->stage.continuation_count == builder->continuation_capacity) {
    builder->continuation_capacity =
        builder->continuation_capacity == 0 ? 8 : builder->continuation_capacity * 2;
    builder->stage.continuations = xrealloc(
        builder->stage.continuations,
        builder->continuation_capacity * sizeof *builder->stage.continuations);
  }
  builder->stage.continuations[builder->stage.continuation_count++] = function_index;
}
static void builder_repeat(stage_builder *builder, qa_q1qc_repeat_gate gate) {
  if (builder->stage.repeat_count == builder->repeat_capacity) {
    builder->repeat_capacity = builder->repeat_capacity == 0 ? 4 : builder->repeat_capacity * 2;
    builder->stage.repeats = xrealloc(builder->stage.repeats,
                                      builder->repeat_capacity * sizeof *builder->stage.repeats);
  }
  builder->stage.repeats[builder->stage.repeat_count++] = gate;
}
static qa_q1qc_weapon_stage *builder_finish(stage_builder *builder) {
  qa_q1qc_weapon_stage *stage = xmalloc(sizeof *stage);
  *stage = builder->stage;
  return stage;
}
bool qa_q1qc_weapon_stage_has_continuation(const qa_q1qc_weapon_stage *stage, int64_t function_index) {
  for (size_t i = 0; i < stage->continuation_count; i++)
    if (stage->continuations[i] == function_index) return true;
  return false;
}
void qa_q1qc_weapon_stage_destroy(qa_q1qc_weapon_stage *stage) {
  if (stage == NULL) return;
  free(stage->continuations);
  free(stage->repeats);
  free(stage);
}

static bool returns_value(const qa_q1qc_program *program, const qa_q1qc_function *fn,
                          int64_t *out_count, bool derived_entity) {
  int64_t end = qa_q1qc_function_end(program, fn);
  int64_t count = 0;
  bool bad_entity = false;
  for (int64_t i = fn->first_statement; i < end; i++) {
    const qa_q1qc_statement *statement = &program->statements[i];
    if ((statement->opcode != QA_Q1QC_OP_RETURN && statement->opcode != QA_Q1QC_OP_DONE) ||
        statement->a == 0) continue;
    count++;
    if (derived_entity && statement->a != 1) {
      bool entity = false;
      for (size_t g = 0; g < program->global_count; g++)
        if (program->globals[g].offset == statement->a &&
            strcmp(program->globals[g].type, "entity") == 0) entity = true;
      if (!entity) bad_entity = true;
    }
    if (!derived_entity) {
      bool entity = false;
      for (size_t g = 0; g < program->global_count; g++)
        if (program->globals[g].offset == statement->a &&
            strcmp(program->globals[g].type, "entity") == 0) entity = true;
      if (!entity) bad_entity = true;
    }
  }
  *out_count = count;
  return bad_entity;
}
static const qa_q1qc_function *stage_function(const qa_q1qc_program *program, const char *name) {
  const qa_q1qc_function *value = qa_q1qc_function_named_or_null(program, name);
  if (value == NULL || value->index == 0 || value->first_statement <= 0 || value->named_builtin ||
      value->parameter_size_count != 0) return NULL;
  int64_t count;
  (void)returns_value(program, value, &count, true);
  if (count != 0) return NULL;
  return value;
}
static bool named_has(const qa_q1qc_program *program, int64_t word) {
  for (size_t g = 0; g < program->global_count; g++) {
    const qa_q1qc_definition *global = &program->globals[g];
    if (global->name[0] == '\0' || strcmp(global->name, "IMMEDIATE") == 0) continue;
    int64_t width = strcmp(global->type, "vector") == 0 ? 3 : 1;
    if (word >= global->offset && word < global->offset + width) return true;
  }
  return false;
}
static bool is_temporary(const qa_q1qc_program *program, int64_t word,
                         const qa_q1qc_function *owner) {
  return word >= 28 && word * 4 < (int64_t)program->initial_globals_length &&
         (!named_has(program, word) ||
          (word >= owner->parameter_start && word < owner->parameter_start + owner->local_words));
}
static bool is_pure(qa_q1qc_opcode opcode) {
  switch (opcode) {
    case QA_Q1QC_OP_LOAD_F: case QA_Q1QC_OP_NOT_F: case QA_Q1QC_OP_EQ_F: case QA_Q1QC_OP_NE_F:
    case QA_Q1QC_OP_LE: case QA_Q1QC_OP_GE: case QA_Q1QC_OP_LT: case QA_Q1QC_OP_GT:
    case QA_Q1QC_OP_AND: case QA_Q1QC_OP_OR: case QA_Q1QC_OP_BIT_AND: case QA_Q1QC_OP_BIT_OR:
    case QA_Q1QC_OP_ADD_F: case QA_Q1QC_OP_SUB_F: case QA_Q1QC_OP_MUL_F: case QA_Q1QC_OP_DIV_F:
      return true;
    default: return false;
  }
}
static bool derived_client_call(const qa_q1qc_program *program, const char *name,
                                const char *result, qa_q1qc_client_stage_call *out) {
  const qa_q1qc_function *fn = qa_q1qc_function_named_or_null(program, name);
  if (fn == NULL || fn->first_statement <= 0 || fn->named_builtin) return false;
  int64_t count;
  bool bad = returns_value(program, fn, &count, true);
  if (strcmp(result, "void") == 0 ? count != 0 : count == 0 || bad) return false;
  static const char *const AVAILABLE[] = {"self", "time"};
  char error[512];
  qa_mod_source_call empty = {name, NULL, 0, NULL, 0};
  memset(out, 0, sizeof *out);
  out->function_index = fn->index;
  if (qa_q1qc_validate_source_call_check(program, &empty, AVAILABLE, 2, "client stage",
                                         error, sizeof error)) {
    out->call = empty;
    return true;
  }
  out->arg_storage[0] = (qa_mod_callback_value){"input", "self", 0, NULL, {0, 0, 0}};
  qa_mod_source_call with_self = {name, out->arg_storage, 1, NULL, 0};
  if (qa_q1qc_validate_source_call_check(program, &with_self, AVAILABLE, 2, "client stage",
                                         error, sizeof error)) {
    out->call = with_self;
    return true;
  }
  return false;
}
static void derived_repeats(const qa_q1qc_program *program, stage_builder *builder) {
  typedef struct range { int64_t entry, exit; } range;
  range *ranges = NULL;
  size_t range_count = 0, range_capacity = 0;
  size_t repeat_start = builder->stage.repeat_count;
  (void)repeat_start;
  for (size_t ci = 0; ci < builder->stage.continuation_count; ci++) {
    const qa_q1qc_function *owner =
        qa_q1qc_function_at_or_null(program, builder->stage.continuations[ci]);
    if (owner == NULL) continue;
    int64_t end = qa_q1qc_function_end(program, owner);
    for (int64_t entry = owner->first_statement; entry < end; entry++) {
      const qa_q1qc_statement *first = &program->statements[entry];
      if (first->opcode != QA_Q1QC_OP_LOAD_F || !is_temporary(program, first->c, owner)) continue;
      int64_t written[256];
      size_t written_count = 0;
      written[written_count++] = first->c;
      int64_t exit = entry + 1;
      while (exit < end) {
        const qa_q1qc_statement *statement = &program->statements[exit];
        if (!is_pure(statement->opcode) || !is_temporary(program, statement->c, owner)) break;
        bool seen = false;
        for (size_t w = 0; w < written_count; w++) if (written[w] == statement->c) seen = true;
        if (!seen && written_count < 256) written[written_count++] = statement->c;
        exit++;
      }
      if (exit <= entry || exit + 2 >= end) continue;
      const qa_q1qc_statement *join = &program->statements[exit];
      const qa_q1qc_statement *release = &program->statements[exit + 1];
      const qa_q1qc_statement *returned = &program->statements[exit + 2];
      bool join_written = false;
      for (size_t w = 0; w < written_count; w++) if (written[w] == join->a) join_written = true;
      if ((join->opcode != QA_Q1QC_OP_IF && join->opcode != QA_Q1QC_OP_IF_NOT) ||
          qa_q1qc_signed_branch(join->b) != 3 || !join_written ||
          !is_temporary(program, join->a, owner) || release->opcode != QA_Q1QC_OP_CALL0 ||
          returned->opcode != QA_Q1QC_OP_RETURN || returned->a != 0) continue;
      bool overlap = false;
      for (size_t r = 0; r < range_count; r++)
        if (entry <= ranges[r].exit && exit >= ranges[r].entry) overlap = true;
      if (overlap) continue;
      if (release->a < 0 || release->a * 4 + 4 > (int64_t)program->initial_globals_length) continue;
      const qa_q1qc_function *callee =
          qa_q1qc_function_at_or_null(program, qa_q1qc_initial_int32(program, release->a));
      if (callee == NULL || stage_function(program, callee->name) == NULL) continue;
      if (range_count == range_capacity) {
        range_capacity = range_capacity == 0 ? 8 : range_capacity * 2;
        ranges = xrealloc(ranges, range_capacity * sizeof *ranges);
      }
      ranges[range_count++] = (range){entry, exit};
      qa_q1qc_repeat_gate gate = {{owner->index, entry, exit, true, false, 0, NULL},
                                  join->a, join->opcode == QA_Q1QC_OP_IF_NOT ? 1 : 0};
      builder_repeat(builder, gate);
    }
  }
  free(ranges);
}
qa_q1qc_weapon_stage *qa_q1qc_derived_weapon_stage(const qa_q1qc_program *program) {
  stage_builder builder = {0};
  const qa_q1qc_function *dispatcher = stage_function(program, "W_WeaponFrame");
  builder.stage.dispatcher = dispatcher != NULL ? dispatcher->index : -1;
  typedef struct family { const char *name; int count; } family;
  static const family FAMILIES[8] = {{"shot", 6}, {"axe", 4}, {"axeb", 4}, {"axec", 4},
                                     {"axed", 4}, {"nail", 2}, {"light", 2}, {"rocket", 6}};
  for (int f = 0; f < 8; f++) {
    for (int ordinal = 1; ordinal <= FAMILIES[f].count + 4; ordinal++) {
      char name[64];
      (void)snprintf(name, sizeof name, "player_%s%d", FAMILIES[f].name, ordinal);
      const qa_q1qc_function *fn = stage_function(program, name);
      if (fn == NULL) {
        if (ordinal > FAMILIES[f].count) break;
        continue;
      }
      builder_continuation(&builder, fn->index);
    }
  }
  qa_q1qc_client_stage_call spawn, select_spawn;
  bool has_spawn = derived_client_call(program, "PutClientInServer", "void", &spawn);
  bool has_select = derived_client_call(program, "SelectSpawnPoint", "entity", &select_spawn);
  if (has_spawn && has_select) {
    builder.stage.has_client = true;
    builder.stage.client.spawn = spawn;
    builder.stage.client.select_spawn = select_spawn;
    builder.stage.client.objectives_kind = "none";
  }
  derived_repeats(program, &builder);
  return builder_finish(&builder);
}

qa_q1qc_weapon_stage *qa_q1qc_declared_weapon_stage(const qa_q1qc_program *program,
                                                    const qa_qc_weapon_stage_declaration *declared) {
  /* TS fn(): plain Error throws (qa_range_error preserves the abort). */
  stage_builder builder = {0};
  const qa_q1qc_function **resolved =
      xmalloc((declared->continuations.count + 1) * sizeof *resolved);
  size_t resolved_count = 0;
  const qa_q1qc_function *dispatcher = NULL;
  for (size_t pass = 0; pass < declared->continuations.count + 1; pass++) {
    const char *name = pass == 0 ? declared->dispatcher : declared->continuations.values[pass - 1];
    const qa_q1qc_function *value = qa_q1qc_function_named_or_null(program, name);
    if (value == NULL)
      qa_q1qc_program_error(program, "missing function %s", name);
    if (value->index == 0 || value->first_statement <= 0 || value->named_builtin ||
        value->parameter_size_count != 0)
      qa_range_error("QC weapon stage requires original parameterless source functions");
    int64_t count;
    (void)returns_value(program, value, &count, true);
    if (count != 0) qa_range_error("QC weapon stage function returns a source value");
    resolved[resolved_count++] = value;
    if (pass == 0) dispatcher = value;
    else builder_continuation(&builder, value->index);
  }
  builder.stage.dispatcher = dispatcher->index;
  if (builder.stage.continuation_count == 0 ||
      builder.stage.continuation_count != declared->continuations.count ||
      qa_q1qc_weapon_stage_has_continuation(&builder.stage, dispatcher->index)) {
    free(resolved);
    qa_range_error("QC weapon continuation declarations overlap or are empty");
  }
  typedef struct range { int64_t entry, exit; } range;
  range *ranges = xmalloc(declared->repeat_count * sizeof *ranges);
  size_t range_count = 0;
  for (size_t i = 0; i < declared->repeat_count; i++) {
    const qa_qc_weapon_stage_repeat *source = &declared->repeats[i];
    const qa_q1qc_function *owner = NULL;
    for (size_t r = 0; r < resolved_count; r++)
      if (strcmp(resolved[r]->name, source->function) == 0) owner = resolved[r];
    if (owner == NULL) {
      /* TS fn(source.function) resolves (and validates) by name. */
      const qa_q1qc_function *value = qa_q1qc_function_named_or_null(program, source->function);
      if (value == NULL) qa_q1qc_program_error(program, "missing function %s", source->function);
      if (value->index == 0 || value->first_statement <= 0 || value->named_builtin ||
          value->parameter_size_count != 0)
        qa_range_error("QC weapon stage requires original parameterless source functions");
      int64_t count;
      (void)returns_value(program, value, &count, true);
      if (count != 0) qa_range_error("QC weapon stage function returns a source value");
      owner = value;
    }
    int64_t end = qa_q1qc_function_end(program, owner);
    bool overlap = false;
    for (size_t r = 0; r < range_count; r++)
      if ((int64_t)source->entry <= ranges[r].exit && (int64_t)source->exit >= ranges[r].entry)
        overlap = true;
    if (!qa_q1qc_weapon_stage_has_continuation(&builder.stage, owner->index) || overlap ||
        source->entry != (double)(int64_t)source->entry ||
        source->exit != (double)(int64_t)source->exit ||
        (int64_t)source->entry < owner->first_statement ||
        (int64_t)source->exit <= (int64_t)source->entry || (int64_t)source->exit + 2 >= end ||
        source->result_word != (double)(int64_t)source->result_word ||
        !is_temporary(program, (int64_t)source->result_word, owner) ||
        (source->result_value != 0 && source->result_value != 1) ||
        (int64_t)source->statement_count != (int64_t)source->exit - (int64_t)source->entry + 1)
      qa_range_error("QC weapon repeat boundary is outside its original continuation");
    ranges[range_count++] = (range){(int64_t)source->entry, (int64_t)source->exit};
    bool result_written = false;
    for (size_t offset = 0; offset < source->statement_count; offset++) {
      const qa_qc_statement *expected = &source->statements[offset];
      int64_t index = (int64_t)source->entry + (int64_t)offset;
      const qa_q1qc_statement *actual =
          index >= 0 && (size_t)index < program->statement_count ? &program->statements[index] : NULL;
      if (actual == NULL || (double)actual->opcode != expected->opcode ||
          (double)actual->a != expected->a || (double)actual->b != expected->b ||
          (double)actual->c != expected->c)
        qa_range_error("QC weapon repeat boundary differs from its declared original instructions");
      if (offset == source->statement_count - 1) continue;
      if (!is_pure(actual->opcode) || !is_temporary(program, actual->c, owner))
        qa_range_error("QC weapon repeat must be a pure scalar predicate using source temporaries");
      if ((double)actual->c == source->result_word) result_written = true;
    }
    const qa_q1qc_statement *join = &program->statements[(size_t)source->exit];
    const qa_q1qc_statement *release = &program->statements[(size_t)source->exit + 1];
    const qa_q1qc_statement *returned = &program->statements[(size_t)source->exit + 2];
    if (!result_written || (double)join->a != source->result_word ||
        join->opcode != (source->result_value == 0 ? QA_Q1QC_OP_IF : QA_Q1QC_OP_IF_NOT) ||
        qa_q1qc_signed_branch(join->b) != 3 || release->opcode != QA_Q1QC_OP_CALL0 ||
        returned->opcode != QA_Q1QC_OP_RETURN || returned->a != 0)
      qa_range_error("QC weapon predicate does not join its original release-call and return branch");
    if (release->a < 0 || release->a * 4 + 4 > (int64_t)program->initial_globals_length)
      qa_range_error("QC weapon release call is outside source globals");
    const qa_q1qc_function *callee =
        qa_q1qc_function_at(program, qa_q1qc_initial_int32(program, release->a));
    const qa_q1qc_function *callee_checked = qa_q1qc_function_named_or_null(program, callee->name);
    if (callee_checked == NULL || callee_checked->index == 0 ||
        callee_checked->first_statement <= 0 || callee_checked->named_builtin ||
        callee_checked->parameter_size_count != 0)
      qa_range_error("QC weapon stage requires original parameterless source functions");
    int64_t count;
    (void)returns_value(program, callee_checked, &count, true);
    if (count != 0) qa_range_error("QC weapon stage function returns a source value");
    qa_q1qc_repeat_gate gate = {{owner->index, (int64_t)source->entry, (int64_t)source->exit,
                                 true, false, 0, NULL},
                                (int64_t)source->result_word, source->result_value};
    builder_repeat(&builder, gate);
  }
  free(ranges);
  free(resolved);
  return builder_finish(&builder);
}

typedef struct pinned_ctx { const qa_q1qc_program *program; } pinned_ctx;
static void pinned_statement(pinned_ctx *ctx, int64_t index, qa_q1qc_opcode opcode,
                             int64_t a, int64_t b) {
  const qa_q1qc_statement *actual =
      index >= 0 && (size_t)index < ctx->program->statement_count
          ? &ctx->program->statements[index] : NULL;
  if (actual == NULL || actual->opcode != opcode || actual->a != a || actual->b != b)
    qa_q1qc_error("progs.dat",
                  "Original weapon stage statement %lld differs from its qualified artifact",
                  (long long)index);
}
static int64_t pinned_fn(pinned_ctx *ctx, const char *name, int64_t index, int64_t first) {
  const qa_q1qc_function *original = qa_q1qc_function_named(ctx->program, name);
  if (original->index != index || original->first_statement != first ||
      original->local_words != 0 || original->parameter_size_count != 0 || original->named_builtin)
    qa_q1qc_error("progs.dat", "Unsupported original weapon stage %s", name);
  return index;
}
static qa_q1qc_client_stage_call pinned_client(pinned_ctx *ctx, const char *name, int64_t index,
                                               int64_t first, int64_t parameter_start,
                                               int64_t locals) {
  const qa_q1qc_function *original = qa_q1qc_function_named(ctx->program, name);
  if (original->index != index || original->first_statement != first ||
      original->parameter_start != parameter_start || original->local_words != locals ||
      original->parameter_size_count != 0 || original->named_builtin)
    qa_q1qc_error("progs.dat", "Unsupported original client stage %s", name);
  qa_q1qc_client_stage_call call;
  memset(&call, 0, sizeof call);
  call.function_index = index;
  call.call = (qa_mod_source_call){name, NULL, 0, NULL, 0};
  return call;
}
static qa_q1qc_client_stage_call declared_client(const qa_q1qc_program *program,
                                                 const qa_qc_named_call *declaration,
                                                 const char *result) {
  static const char *const AVAILABLE[] = {"self", "time"};
  qa_q1qc_client_stage_call out;
  memset(&out, 0, sizeof out);
  qa_mod_source_call synthesized = {declaration->function, NULL, 0, NULL, 0};
  const qa_mod_source_call *call = declaration->is_call ? &declaration->call : &synthesized;
  qa_q1qc_validate_source_call(program, call, AVAILABLE, 2, "client stage");
  const qa_q1qc_function *fn = qa_q1qc_function_named(program, call->function);
  if (fn->first_statement <= 0 || fn->named_builtin)
    qa_q1qc_error("progs.dat", "QC client stage requires an original source function");
  int64_t count;
  bool bad = returns_value(program, fn, &count, false);
  if (strcmp(result, "void") == 0 ? count != 0 : count == 0 || bad)
    qa_q1qc_error("progs.dat", "QC client stage %s does not return %s", call->function, result);
  out.function_index = fn->index;
  out.call = *call;
  return out;
}

qa_q1qc_weapon_stage *qa_q1qc_weapon_stage_create(
    const qa_q1qc_program *program,
    const qa_qc_primary_weapon_stage_declaration *declared) {
  if (declared != NULL) {
    qa_q1qc_weapon_stage *stage = qa_q1qc_declared_weapon_stage(program, &declared->stage);
    stage->has_client = true;
    stage->client.spawn = declared_client(program, &declared->client_spawn, "void");
    stage->client.select_spawn = declared_client(program, &declared->client_select_spawn, "entity");
    stage->client.objectives_kind = declared->client_objectives.kind;
    if (strcmp(declared->client_objectives.kind, "call") == 0) {
      qa_qc_named_call objectives;
      memset(&objectives, 0, sizeof objectives);
      if (declared->client_objectives.function != NULL) {
        objectives.is_call = false;
        objectives.function = declared->client_objectives.function;
      } else {
        objectives.is_call = true;
        objectives.call = declared->client_objectives.call;
      }
      stage->client.objectives_call = declared_client(program, &objectives, "void");
    }
    return stage;
  }
  bool qw = program->digest != NULL &&
            strcmp(program->digest,
                   "sha256:ff51cb5e77360d72b93487d89198dcf94629b92f8bae100fc6ea48a6c12a7830") == 0;
  if (!qw && (program->digest == NULL ||
              strcmp(program->digest,
                     "sha256:f2619787f9aa0f057246eea1665b622b4691b5c5a800b1a46133d1fe8b771580") != 0))
    return qa_q1qc_derived_weapon_stage(program);
  pinned_ctx ctx = {program};
  stage_builder builder = {0};
  if (qw) {
    builder.stage.has_client = true;
    builder.stage.client.spawn = pinned_client(&ctx, "PutClientInServer", 193, 5554, 3621, 2);
    builder.stage.client.select_spawn = pinned_client(&ctx, "SelectSpawnPoint", 191, 5465, 3585, 9);
    builder.stage.client.objectives_kind = "none";
    builder.stage.dispatcher = pinned_fn(&ctx, "W_WeaponFrame", 170, 4618);
    (void)pinned_fn(&ctx, "player_run", 215, 7185);
    typedef struct family { const char *name; int64_t first_index; int64_t starts[6]; int count; } family;
    static const family FAMILIES[8] = {
      {"shot", 217, {7237, 7242, 7246, 7250, 7254, 7258}, 6},
      {"axe", 223, {7262, 7266, 7270, 7275}, 4}, {"axeb", 227, {7279, 7283, 7287, 7292}, 4},
      {"axec", 231, {7296, 7300, 7304, 7309}, 4}, {"axed", 235, {7313, 7317, 7321, 7326}, 4},
      {"nail", 239, {7330, 7356}, 2}, {"light", 241, {7382, 7405}, 2},
      {"rocket", 243, {7428, 7433, 7437, 7441, 7445, 7449}, 6}};
    for (int f = 0; f < 8; f++)
      for (int ordinal = 0; ordinal < FAMILIES[f].count; ordinal++) {
        char name[64];
        (void)snprintf(name, sizeof name, "player_%s%d", FAMILIES[f].name, ordinal + 1);
        builder_continuation(&builder, pinned_fn(&ctx, name, FAMILIES[f].first_index + ordinal,
                                                 FAMILIES[f].starts[ordinal]));
      }
    typedef struct gate { int64_t function_index, entry, held; bool impulse; } gate;
    static const gate GATES[4] = {{239, 7332, 4574, true}, {240, 7358, 4588, true},
                                  {241, 7384, 4603, false}, {242, 7407, 4615, false}};
    for (int g = 0; g < 4; g++) {
      int64_t entry = GATES[g].entry, held = GATES[g].held;
      typedef struct exact { int64_t offset; qa_q1qc_opcode opcode; int64_t a, b, c; } exact;
      exact checks[5] = {{0, QA_Q1QC_OP_LOAD_F, 28, 165, held},
                         {1, QA_Q1QC_OP_NOT_F, held, 0, held + 1},
                         {2, QA_Q1QC_OP_OR, held + 1, 3457, held + 2},
                         {3, QA_Q1QC_OP_LOAD_F, 28, 168, held + 3},
                         {4, QA_Q1QC_OP_OR, held + 2, held + 3, held + 4}};
      int check_count = GATES[g].impulse ? 5 : 3;
      for (int i = 0; i < check_count; i++) {
        pinned_statement(&ctx, entry + checks[i].offset, checks[i].opcode, checks[i].a, checks[i].b);
        if (program->statements[entry + checks[i].offset].c != checks[i].c)
          qa_q1qc_error("progs.dat", "QW weapon release temporary differs");
      }
      int64_t exit = entry + (GATES[g].impulse ? 5 : 3);
      int64_t released = held + (GATES[g].impulse ? 4 : 2);
      pinned_statement(&ctx, exit, QA_Q1QC_OP_IF_NOT, released, 3);
      pinned_statement(&ctx, exit + 1, QA_Q1QC_OP_CALL0, 2112, 0);
      pinned_statement(&ctx, exit + 2, QA_Q1QC_OP_RETURN, 0, 0);
      qa_q1qc_repeat_gate gate_row = {{GATES[g].function_index, entry, exit, true, false, 0, NULL},
                                 released, 1};
      builder_repeat(&builder, gate_row);
    }
    return builder_finish(&builder);
  }
  builder.stage.has_client = true;
  builder.stage.client.spawn = pinned_client(&ctx, "PutClientInServer", 229, 6083, 4114, 1);
  builder.stage.client.select_spawn = pinned_client(&ctx, "SelectSpawnPoint", 228, 6007, 4093, 3);
  builder.stage.client.objectives_kind = "none";
  builder.stage.dispatcher = pinned_fn(&ctx, "W_WeaponFrame", 206, 5047);
  (void)pinned_fn(&ctx, "player_run", 248, 7380);
  typedef struct family { const char *name; int64_t first_index; int64_t starts[6]; int count; } family;
  static const family FAMILIES[8] = {
    {"shot", 249, {7421, 7429, 7433, 7437, 7441, 7445}, 6},
    {"axe", 255, {7449, 7453, 7457, 7462}, 4}, {"axeb", 259, {7466, 7470, 7474, 7479}, 4},
    {"axec", 263, {7483, 7487, 7491, 7496}, 4}, {"axed", 267, {7500, 7504, 7508, 7513}, 4},
    {"nail", 271, {7517, 7543}, 2}, {"light", 273, {7569, 7594}, 2},
    {"rocket", 275, {7619, 7627, 7631, 7635, 7639, 7643}, 6}};
  for (int f = 0; f < 8; f++)
    for (int ordinal = 0; ordinal < FAMILIES[f].count; ordinal++) {
      char name[64];
      (void)snprintf(name, sizeof name, "player_%s%d", FAMILIES[f].name, ordinal + 1);
      builder_continuation(&builder, pinned_fn(&ctx, name, FAMILIES[f].first_index + ordinal,
                                               FAMILIES[f].starts[ordinal]));
    }
  static const int64_t GATES[4][3] = {{271, 7522, 4951}, {272, 7548, 4965},
                                      {273, 7574, 4980}, {274, 7599, 4994}};
  for (int g = 0; g < 4; g++) {
    int64_t entry = GATES[g][1], held = GATES[g][2];
    pinned_statement(&ctx, entry, QA_Q1QC_OP_LOAD_F, 28, 170);
    if (program->statements[entry].c != held)
      qa_q1qc_error("progs.dat", "Original weapon release temporary differs");
    pinned_statement(&ctx, entry + 1, QA_Q1QC_OP_NOT_F, held, 0);
    if (program->statements[entry + 1].c != held + 1)
      qa_q1qc_error("progs.dat", "Original weapon release result differs");
    pinned_statement(&ctx, entry + 2, QA_Q1QC_OP_IF_NOT, held + 1, 3);
    pinned_statement(&ctx, entry + 3, QA_Q1QC_OP_CALL0, 2597, 0);
    pinned_statement(&ctx, entry + 4, QA_Q1QC_OP_RETURN, 0, 0);
    qa_q1qc_repeat_gate gate_row = {{GATES[g][0], entry, entry + 2, true, false, 0, NULL},
                                    held + 1, 1};
    builder_repeat(&builder, gate_row);
  }
  return builder_finish(&builder);
}

typedef struct invoke_ctx {
  qa_q1qc_machine *machine;
  int64_t function_index;
} invoke_ctx;
static double invoke_execute(void *ctx, int64_t argument_count) {
  invoke_ctx *invoke = ctx;
  invoke->machine->execute(invoke->machine->ctx, invoke->function_index, argument_count);
  return (double)qa_q1qc_words_int(&invoke->machine->globals, 1);
}
double qa_q1qc_invoke_client_stage(qa_q1qc_machine *machine,
                                   const qa_q1qc_client_stage_call *stage,
                                   qa_actor_id actor, double time,
                                   qa_q1qc_actor_reference_fn reference, void *reference_ctx) {
  int64_t self_offset = qa_q1qc_machine_global_offset(machine, "self");
  int64_t other_offset = qa_q1qc_machine_global_offset(machine, "other");
  int32_t self = qa_q1qc_words_int(&machine->globals, self_offset);
  int32_t other = qa_q1qc_words_int(&machine->globals, other_offset);
  qa_actor_id actor_id = actor;
  qa_mod_runtime_value self_value = {"actor", 0, NULL, {0, 0, 0}, &actor_id};
  qa_mod_runtime_value time_value = {"float", time, NULL, {0, 0, 0}, NULL};
  qa_q1qc_source_input inputs[2] = {{"self", self_value}, {"time", time_value}};
  qa_q1qc_words_set_int(&machine->globals, self_offset, reference(reference_ctx, &actor));
  qa_q1qc_words_set_int(&machine->globals, other_offset, reference(reference_ctx, NULL));
  qa_q1qc_words_set_float(&machine->globals,
                          qa_q1qc_machine_global_offset(machine, "time"), time);
  invoke_ctx invoke = {machine, stage->function_index};
  double result = qa_q1qc_with_source_call(machine, &stage->call, inputs, 2,
                                           reference, reference_ctx, invoke_execute, &invoke);
  /* TS restores self/other in a finally. */
  qa_q1qc_words_set_int(&machine->globals, self_offset, self);
  qa_q1qc_words_set_int(&machine->globals, other_offset, other);
  return result;
}
int64_t qa_q1qc_client_stage_self(qa_q1qc_machine *machine,
                                  const qa_q1qc_client_stage_call *stage) {
  bool have = false;
  int64_t client = 0;
  for (size_t i = 0; i < stage->call.argument_count; i++) {
    const qa_mod_callback_value *value = &stage->call.arguments[i];
    if (strcmp(value->kind, "input") != 0 || strcmp(value->name, "self") != 0) continue;
    int64_t reference = qa_q1qc_machine_arg_int(machine, (int64_t)i);
    if (have && client != reference)
      qa_q1qc_error("progs.dat", "QC client call has conflicting source self inputs");
    client = reference;
    have = true;
  }
  for (size_t i = 0; i < stage->call.global_count; i++) {
    const qa_mod_named_value *global = &stage->call.globals[i];
    if (strcmp(global->value.kind, "input") != 0 || strcmp(global->value.name, "self") != 0) continue;
    int64_t reference = qa_q1qc_words_int(&machine->globals,
                                          qa_q1qc_machine_global_offset(machine, global->name));
    if (have && client != reference)
      qa_q1qc_error("progs.dat", "QC client call has conflicting source self inputs");
    client = reference;
    have = true;
  }
  if (have) return client;
  return qa_q1qc_words_int(&machine->globals, qa_q1qc_machine_global_offset(machine, "self"));
}

bool qa_q1qc_weapon_stage_settled(const qa_q1qc_weapon_stage_binding *binding, int64_t reference) {
  qa_q1qc_machine *vm = binding->machine(binding->machine_ctx);
  qa_q1qc_words words;
  qa_q1qc_entities_from_reference(vm->entities, reference, &words);
  int64_t think = qa_q1qc_words_int(&words, qa_q1qc_machine_field_offset(vm, "think"));
  return !qa_q1qc_weapon_stage_has_continuation(binding->stage, think);
}
static void weapon_function_run(void *ctx, qa_q1qc_call_site call,
                                qa_q1qc_function_execution execute) {
  qa_q1qc_weapon_function_composed *composed = ctx;
  if (call.function_index != composed->binding->stage->dispatcher) {
    composed->inner.run(composed->inner.ctx, call, execute);
    return;
  }
  qa_q1qc_machine *vm = composed->binding->machine(composed->binding->machine_ctx);
  int64_t self = qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "self"));
  if (composed->binding->selected(composed->binding->selected_ctx, self))
    qa_q1qc_execution_run(&execute);
  else {
    static const int32_t ZEROS[3] = {0, 0, 0};
    execute.skip(execute.ctx, ZEROS);
  }
}
void qa_q1qc_weapon_compose_functions(qa_q1qc_weapon_function_composed *composed,
                                      const qa_q1qc_weapon_stage_binding *binding,
                                      qa_q1qc_function_boundary inner) {
  memset(composed, 0, sizeof *composed);
  composed->binding = binding;
  composed->inner = inner;
  composed->functions = xmalloc((inner.function_count + 1) * sizeof *composed->functions);
  for (size_t i = 0; i < inner.function_count; i++) {
    bool seen = false;
    for (size_t j = 0; j < composed->function_count; j++)
      if (composed->functions[j] == inner.functions[i]) seen = true;
    if (!seen) composed->functions[composed->function_count++] = inner.functions[i];
  }
  bool seen = false;
  for (size_t j = 0; j < composed->function_count; j++)
    if (composed->functions[j] == binding->stage->dispatcher) seen = true;
  if (!seen) composed->functions[composed->function_count++] = binding->stage->dispatcher;
  composed->boundary = (qa_q1qc_function_boundary){composed, composed->functions,
                                                   composed->function_count, weapon_function_run};
}
void qa_q1qc_weapon_function_composed_destroy(qa_q1qc_weapon_function_composed *composed) {
  free(composed->functions);
  memset(composed, 0, sizeof *composed);
}
static void weapon_region_run(void *ctx, qa_q1qc_inline_region region,
                              qa_q1qc_inline_continuation execute) {
  qa_q1qc_weapon_region_composed *composed = ctx;
  const qa_q1qc_repeat_gate *gate = NULL;
  for (size_t i = 0; i < composed->binding->stage->repeat_count; i++)
    if (composed->binding->stage->repeats[i].region.entry == region.entry)
      gate = &composed->binding->stage->repeats[i];
  if (gate == NULL) {
    composed->inner.run(composed->inner.ctx, region, execute);
    return;
  }
  qa_q1qc_machine *vm = composed->binding->machine(composed->binding->machine_ctx);
  int64_t self = qa_q1qc_words_int(&vm->globals, qa_q1qc_machine_global_offset(vm, "self"));
  if (composed->binding->selected(composed->binding->selected_ctx, self)) {
    execute.run(execute.ctx);
    return;
  }
  execute.run(execute.ctx);
  qa_q1qc_words_set_float(&vm->globals, gate->released, gate->value);
}
void qa_q1qc_weapon_compose_regions(qa_q1qc_weapon_region_composed *composed,
                                    const qa_q1qc_weapon_stage_binding *binding,
                                    qa_q1qc_inline_boundary inner) {
  for (size_t g = 0; g < binding->stage->repeat_count; g++)
    for (size_t i = 0; i < inner.region_count; i++)
      if (inner.regions[i].entry == binding->stage->repeats[g].region.entry)
        qa_q1qc_error("progs.dat", "Original weapon repeat gate already has a boundary owner");
  memset(composed, 0, sizeof *composed);
  composed->binding = binding;
  composed->inner = inner;
  composed->regions = xmalloc((inner.region_count + binding->stage->repeat_count) *
                              sizeof *composed->regions);
  for (size_t i = 0; i < inner.region_count; i++)
    composed->regions[composed->region_count++] = inner.regions[i];
  for (size_t g = 0; g < binding->stage->repeat_count; g++)
    composed->regions[composed->region_count++] = binding->stage->repeats[g].region;
  composed->boundary = (qa_q1qc_inline_boundary){composed, composed->regions,
                                                 composed->region_count, weapon_region_run};
}
void qa_q1qc_weapon_region_composed_destroy(qa_q1qc_weapon_region_composed *composed) {
  free(composed->regions);
  memset(composed, 0, sizeof *composed);
}
