/* Port of src/content/q1/quakec/armor-stage.ts. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/armor_stage.h"

#include <math.h>
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

/* Insertion-ordered word set (TS Set<number>). */
typedef struct wset { int64_t *items; size_t count, capacity; } wset;
static bool wset_has(const wset *set, int64_t word) {
  for (size_t i = 0; i < set->count; i++) if (set->items[i] == word) return true;
  return false;
}
static void wset_add(wset *set, int64_t word) {
  if (wset_has(set, word)) return;
  if (set->count == set->capacity) {
    set->capacity = set->capacity == 0 ? 16 : set->capacity * 2;
    set->items = xrealloc(set->items, set->capacity * sizeof *set->items);
  }
  set->items[set->count++] = word;
}
static void wset_remove(wset *set, int64_t word) {
  for (size_t i = 0; i < set->count; i++) {
    if (set->items[i] != word) continue;
    memmove(&set->items[i], &set->items[i + 1], (set->count - i - 1) * sizeof *set->items);
    set->count--;
    return;
  }
}
static wset wset_clone(const wset *set) {
  wset copy = {NULL, 0, 0};
  for (size_t i = 0; i < set->count; i++) wset_add(&copy, set->items[i]);
  return copy;
}
static bool wset_subset(const wset *inner, const wset *outer) {
  for (size_t i = 0; i < inner->count; i++) if (!wset_has(outer, inner->items[i])) return false;
  return true;
}
static void wset_free(wset *set) { free(set->items); set->items = NULL; set->count = set->capacity = 0; }

bool qa_q1qc_access_has(const int64_t *words, size_t count, int64_t word) {
  for (size_t i = 0; i < count; i++) if (words[i] == word) return true;
  return false;
}
static void push_words(int64_t *out, size_t *count, size_t capacity, int64_t first, int64_t length) {
  for (int64_t i = 0; i < length; i++)
    if (*count < capacity) out[(*count)++] = first + i;
}
qa_q1qc_statement_access qa_q1qc_statement_access_of(qa_q1qc_statement statement) {
  qa_q1qc_statement_access result = {0};
  qa_q1qc_opcode opcode = statement.opcode;
  int64_t a = statement.a, b = statement.b, c = statement.c;
  if (opcode == QA_Q1QC_OP_DONE || opcode == QA_Q1QC_OP_RETURN) {
    push_words(result.read, &result.read_count, 32, a, 3);
    push_words(result.write, &result.write_count, 8, 1, 3);
    return result;
  }
  if (opcode == QA_Q1QC_OP_GOTO) return result;
  if (opcode == QA_Q1QC_OP_IF || opcode == QA_Q1QC_OP_IF_NOT) {
    push_words(result.read, &result.read_count, 32, a, 1);
    return result;
  }
  if (opcode >= QA_Q1QC_OP_CALL0 && opcode <= QA_Q1QC_OP_CALL8) {
    push_words(result.read, &result.read_count, 32, a, 1);
    push_words(result.read, &result.read_count, 32, 4, (int64_t)(opcode - QA_Q1QC_OP_CALL0) * 3);
    push_words(result.write, &result.write_count, 8, 1, 3);
    return result;
  }
  if (opcode == QA_Q1QC_OP_STATE) {
    push_words(result.read, &result.read_count, 32, a, 1);
    push_words(result.read, &result.read_count, 32, b, 1);
    return result;
  }
  if (opcode >= QA_Q1QC_OP_STORE_F && opcode <= QA_Q1QC_OP_STORE_FN) {
    int64_t width = opcode == QA_Q1QC_OP_STORE_V ? 3 : 1;
    push_words(result.read, &result.read_count, 32, a, width);
    push_words(result.write, &result.write_count, 8, b, width);
    return result;
  }
  if (opcode >= QA_Q1QC_OP_STORE_PF && opcode <= QA_Q1QC_OP_STORE_PFN) {
    push_words(result.read, &result.read_count, 32, a, opcode == QA_Q1QC_OP_STORE_PV ? 3 : 1);
    push_words(result.read, &result.read_count, 32, b, 1);
    return result;
  }
  if (opcode >= QA_Q1QC_OP_NOT_F && opcode <= QA_Q1QC_OP_NOT_FN) {
    push_words(result.read, &result.read_count, 32, a, opcode == QA_Q1QC_OP_NOT_V ? 3 : 1);
    push_words(result.write, &result.write_count, 8, c, 1);
    return result;
  }
  bool vector_a = opcode == QA_Q1QC_OP_MUL_V || opcode == QA_Q1QC_OP_MUL_VF ||
                  opcode == QA_Q1QC_OP_ADD_V || opcode == QA_Q1QC_OP_SUB_V ||
                  opcode == QA_Q1QC_OP_EQ_V || opcode == QA_Q1QC_OP_NE_V;
  bool vector_b = opcode == QA_Q1QC_OP_MUL_V || opcode == QA_Q1QC_OP_MUL_FV ||
                  opcode == QA_Q1QC_OP_ADD_V || opcode == QA_Q1QC_OP_SUB_V ||
                  opcode == QA_Q1QC_OP_EQ_V || opcode == QA_Q1QC_OP_NE_V;
  bool vector_result = opcode == QA_Q1QC_OP_MUL_FV || opcode == QA_Q1QC_OP_MUL_VF ||
                       opcode == QA_Q1QC_OP_ADD_V || opcode == QA_Q1QC_OP_SUB_V ||
                       opcode == QA_Q1QC_OP_LOAD_V;
  push_words(result.read, &result.read_count, 32, a, vector_a ? 3 : 1);
  push_words(result.read, &result.read_count, 32, b, vector_b ? 3 : 1);
  push_words(result.write, &result.write_count, 8, c, vector_result ? 3 : 1);
  return result;
}

/* TS writtenWords WeakMap, as a small per-program cache (leaked). */
typedef struct written_entry { const qa_q1qc_program *program; wset written; } written_entry;
static written_entry WRITTEN[16];
static size_t WRITTEN_COUNT;
static const wset *written_words(const qa_q1qc_program *program) {
  for (size_t i = 0; i < WRITTEN_COUNT; i++)
    if (WRITTEN[i].program == program) return &WRITTEN[i].written;
  wset written = {NULL, 0, 0};
  for (size_t i = 0; i < program->statement_count; i++) {
    qa_q1qc_statement_access access = qa_q1qc_statement_access_of(program->statements[i]);
    for (size_t w = 0; w < access.write_count; w++) wset_add(&written, access.write[w]);
  }
  if (WRITTEN_COUNT < 16) {
    WRITTEN[WRITTEN_COUNT].program = program;
    WRITTEN[WRITTEN_COUNT].written = written;
    return &WRITTEN[WRITTEN_COUNT++].written;
  }
  /* Cache full: keep the first slot's set replaced (correct, slower). */
  wset_free(&WRITTEN[0].written);
  WRITTEN[0].program = program;
  WRITTEN[0].written = written;
  return &WRITTEN[0].written;
}
static const qa_q1qc_function *constant_callee(const qa_q1qc_program *program,
                                               const qa_q1qc_statement *statement) {
  if (wset_has(written_words(program), statement->a)) return NULL;
  if (statement->a < 0 || (size_t)statement->a * 4 + 4 > program->initial_globals_length)
    qa_q1qc_program_error(program, "callee word outside initial globals");
  int64_t index = qa_q1qc_initial_int32(program, statement->a);
  if (index < 0 || (size_t)index >= program->function_count) return NULL;
  const qa_q1qc_function *fn = &program->functions[index];
  const qa_q1qc_definition *global = qa_q1qc_global_by_name(program, fn->name);
  return global != NULL && global->offset == statement->a ? fn : NULL;
}
static qa_q1qc_statement_access source_access(const qa_q1qc_program *program,
                                              const qa_q1qc_statement *statement) {
  qa_q1qc_statement_access result = qa_q1qc_statement_access_of(*statement);
  if (statement->opcode < QA_Q1QC_OP_CALL0 || statement->opcode > QA_Q1QC_OP_CALL8) return result;
  const qa_q1qc_function *fn = constant_callee(program, statement);
  if (fn == NULL || (int64_t)fn->parameter_size_count != (int64_t)(statement->opcode - QA_Q1QC_OP_CALL0))
    return result;
  qa_q1qc_statement_access refined = {0};
  push_words(refined.read, &refined.read_count, 32, statement->a, 1);
  for (size_t i = 0; i < fn->parameter_size_count; i++)
    push_words(refined.read, &refined.read_count, 32, 4 + (int64_t)i * 3, fn->parameter_sizes[i]);
  refined.write_count = result.write_count;
  memcpy(refined.write, result.write, result.write_count * sizeof *refined.write);
  return refined;
}

typedef struct flow_task {
  int64_t index, limit, local_start, local_end;
  bool called;
  wset remaining;
} flow_task;
typedef struct seen_entry { int64_t key; wset words; } seen_entry;
typedef struct entered_entry { int64_t function_index; wset words; } entered_entry;

static bool safe_flow(const qa_q1qc_program *program, int64_t start, int64_t limit,
                      wset dirty, const int64_t *output) {
  flow_task *pending = NULL;
  size_t pending_count = 0, pending_capacity = 0;
  seen_entry *seen = NULL;
  size_t seen_count = 0, seen_capacity = 0;
  entered_entry *entered = NULL;
  size_t entered_count = 0, entered_capacity = 0;
  bool ok = true;
#define FLOW_PUSH(task_value)                                                     \
  do {                                                                            \
    if (pending_count == pending_capacity) {                                      \
      pending_capacity = pending_capacity == 0 ? 16 : pending_capacity * 2;       \
      pending = xrealloc(pending, pending_capacity * sizeof *pending);            \
    }                                                                             \
    pending[pending_count++] = (task_value);                                      \
  } while (0)
  flow_task first = {start, limit, 0, 0, false, dirty};
  FLOW_PUSH(first);
  while (pending_count != 0) {
    flow_task current = pending[--pending_count];
    if (current.remaining.count == 0) { wset_free(&current.remaining); continue; }
    if (!current.called && current.index == current.limit && output != NULL) {
      if (wset_has(&current.remaining, *output)) ok = false;
      wset_free(&current.remaining);
      if (!ok) break;
      continue;
    }
    if (current.index < 0 || current.index >= current.limit) {
      wset_free(&current.remaining);
      ok = false;
      break;
    }
    int64_t key = current.index * 2 + (current.called ? 1 : 0);
    seen_entry *previous = NULL;
    for (size_t i = 0; i < seen_count; i++) if (seen[i].key == key) previous = &seen[i];
    if (previous != NULL && wset_subset(&current.remaining, &previous->words)) {
      wset_free(&current.remaining);
      continue;
    }
    if (previous == NULL) {
      if (seen_count == seen_capacity) {
        seen_capacity = seen_capacity == 0 ? 16 : seen_capacity * 2;
        seen = xrealloc(seen, seen_capacity * sizeof *seen);
      }
      seen[seen_count].key = key;
      seen[seen_count].words = (wset){NULL, 0, 0};
      previous = &seen[seen_count++];
    }
    for (size_t i = 0; i < current.remaining.count; i++) wset_add(&previous->words, current.remaining.items[i]);
    if ((size_t)current.index >= program->statement_count) {
      wset_free(&current.remaining);
      ok = false;
      break;
    }
    const qa_q1qc_statement *statement = &program->statements[current.index];
    qa_q1qc_statement_access access = source_access(program, statement);
    bool tainted = false;
    for (size_t i = 0; i < access.read_count; i++)
      if (wset_has(&current.remaining, access.read[i])) tainted = true;
    bool writes_outside = false;
    for (size_t i = 0; i < access.write_count; i++)
      if (access.write[i] >= 28 && (access.write[i] < current.local_start || access.write[i] >= current.local_end))
        writes_outside = true;
    if (tainted && ((current.called && writes_outside) || access.write_count == 0 ||
                    statement->opcode == QA_Q1QC_OP_RETURN || statement->opcode == QA_Q1QC_OP_DONE ||
                    (statement->opcode >= QA_Q1QC_OP_CALL0 && statement->opcode <= QA_Q1QC_OP_CALL8))) {
      wset_free(&current.remaining);
      ok = false;
      break;
    }
    bool any_high = false;
    for (size_t i = 0; i < current.remaining.count; i++)
      if (current.remaining.items[i] >= 28) any_high = true;
    if (statement->opcode >= QA_Q1QC_OP_CALL0 && statement->opcode <= QA_Q1QC_OP_CALL8 && any_high) {
      const qa_q1qc_function *only = constant_callee(program, statement);
      size_t candidate_count = only != NULL ? 1 : program->function_count;
      for (size_t ci = 0; ci < candidate_count; ci++) {
        const qa_q1qc_function *callee = only != NULL ? only : &program->functions[ci];
        if (callee->first_statement <= 0 || callee->named_builtin) continue;
        int64_t parameter_words = 0;
        for (size_t p = 0; p < callee->parameter_size_count; p++) parameter_words += callee->parameter_sizes[p];
        int64_t parameter_end = callee->parameter_start + parameter_words;
        wset incoming = {NULL, 0, 0};
        for (size_t i = 0; i < current.remaining.count; i++) {
          int64_t word = current.remaining.items[i];
          if (word < callee->parameter_start || word >= parameter_end) wset_add(&incoming, word);
        }
        entered_entry *prior = NULL;
        for (size_t i = 0; i < entered_count; i++)
          if (entered[i].function_index == callee->index) prior = &entered[i];
        if (prior != NULL && wset_subset(&incoming, &prior->words)) { wset_free(&incoming); continue; }
        if (prior == NULL) {
          if (entered_count == entered_capacity) {
            entered_capacity = entered_capacity == 0 ? 8 : entered_capacity * 2;
            entered = xrealloc(entered, entered_capacity * sizeof *entered);
          }
          entered[entered_count].function_index = callee->index;
          entered[entered_count].words = (wset){NULL, 0, 0};
          prior = &entered[entered_count++];
        }
        for (size_t i = 0; i < incoming.count; i++) wset_add(&prior->words, incoming.items[i]);
        flow_task call_task = {callee->first_statement, qa_q1qc_function_end(program, callee),
                               callee->parameter_start, callee->parameter_start + callee->local_words,
                               true, incoming};
        FLOW_PUSH(call_task);
      }
    }
    wset next = {NULL, 0, 0};
    for (size_t i = 0; i < current.remaining.count; i++)
      if (!qa_q1qc_access_has(access.write, access.write_count, current.remaining.items[i]))
        wset_add(&next, current.remaining.items[i]);
    if (statement->opcode >= QA_Q1QC_OP_STORE_F && statement->opcode <= QA_Q1QC_OP_STORE_FN) {
      for (size_t offset = 0; offset < access.write_count; offset++)
        if (wset_has(&current.remaining, statement->a + (int64_t)offset))
          wset_add(&next, statement->b + (int64_t)offset);
    } else if (tainted) {
      for (size_t i = 0; i < access.write_count; i++) wset_add(&next, access.write[i]);
    }
    wset_free(&current.remaining);
    if (statement->opcode == QA_Q1QC_OP_RETURN || statement->opcode == QA_Q1QC_OP_DONE) {
      wset_free(&next);
      continue;
    }
    int64_t successors[2];
    size_t successor_count = 1;
    if (statement->opcode == QA_Q1QC_OP_GOTO) {
      successors[0] = current.index + qa_q1qc_signed_branch(statement->a);
    } else if (statement->opcode == QA_Q1QC_OP_IF || statement->opcode == QA_Q1QC_OP_IF_NOT) {
      successors[0] = current.index + 1;
      successors[1] = current.index + qa_q1qc_signed_branch(statement->b);
      successor_count = 2;
    } else {
      successors[0] = current.index + 1;
    }
    for (size_t i = 0; i < successor_count; i++) {
      flow_task task = {successors[i], current.limit, current.local_start, current.local_end,
                        current.called, wset_clone(&next)};
      FLOW_PUSH(task);
    }
    wset_free(&next);
  }
  for (size_t i = 0; i < pending_count; i++) wset_free(&pending[i].remaining);
  free(pending);
  for (size_t i = 0; i < seen_count; i++) wset_free(&seen[i].words);
  free(seen);
  for (size_t i = 0; i < entered_count; i++) wset_free(&entered[i].words);
  free(entered);
  return ok;
#undef FLOW_PUSH
}

bool qa_q1qc_region_private_writes_are_dead(const qa_q1qc_program *program,
                                            int64_t start, int64_t end,
                                            const int64_t *writes, size_t write_count) {
  wset set = {NULL, 0, 0};
  for (size_t i = 0; i < write_count; i++) wset_add(&set, writes[i]);
  return safe_flow(program, start, end, set, NULL);
}

typedef struct collect_ctx {
  const qa_q1qc_program *program;
  wset writes;
  wset visiting;
} collect_ctx;
static bool collect_writes(collect_ctx *ctx, int64_t start, int64_t limit,
                           int64_t local_start, int64_t local_end) {
  static const int64_t RETURN_ONLY_BUILTINS[] = {-7, -9, -12, -13, -36, -37, -38, -43, -51};
  const qa_q1qc_program *program = ctx->program;
  for (int64_t index = start; index < limit; index++) {
    if (index < 0 || (size_t)index >= program->statement_count) return false;
    const qa_q1qc_statement *statement = &program->statements[index];
    qa_q1qc_statement_access access = qa_q1qc_statement_access_of(*statement);
    for (size_t i = 0; i < access.write_count; i++)
      if (access.write[i] < local_start || access.write[i] >= local_end)
        wset_add(&ctx->writes, access.write[i]);
    if (statement->opcode < QA_Q1QC_OP_CALL0 || statement->opcode > QA_Q1QC_OP_CALL8) continue;
    const qa_q1qc_function *fn = constant_callee(program, statement);
    if (fn == NULL) return false;
    if (fn->named_builtin || fn->first_statement < 0) {
      bool allowed = false;
      for (size_t i = 0; i < sizeof RETURN_ONLY_BUILTINS / sizeof *RETURN_ONLY_BUILTINS; i++)
        if (RETURN_ONLY_BUILTINS[i] == fn->first_statement) allowed = true;
      if (!allowed) return false;
      continue;
    }
    if (wset_has(&ctx->visiting, fn->index)) return false;
    wset_add(&ctx->visiting, fn->index);
    bool ok = collect_writes(ctx, fn->first_statement, qa_q1qc_function_end(program, fn),
                             fn->parameter_start, fn->parameter_start + fn->local_words);
    wset_remove(&ctx->visiting, fn->index);
    if (!ok) return false;
  }
  return true;
}
static bool replacement_safe(const qa_q1qc_program *program,
                             const qa_mod_qc_armor_stage *source, int64_t end) {
  collect_ctx ctx = {program, {NULL, 0, 0}, {NULL, 0, 0}};
  if (!collect_writes(&ctx, (int64_t)source->entry, (int64_t)source->exit, 0, 0)) {
    wset_free(&ctx.writes);
    wset_free(&ctx.visiting);
    return false;
  }
  wset_free(&ctx.visiting);
  wset_remove(&ctx.writes, (int64_t)source->saved);
  const qa_q1qc_function *owner = qa_q1qc_function_named(program, source->function);
  for (size_t i = 0; i < ctx.writes.count; i++) {
    int64_t word = ctx.writes.items[i];
    if (word < 28 || (word >= owner->parameter_start && word < owner->parameter_start + owner->local_words))
      continue;
    for (size_t g = 0; g < program->global_count; g++)
      if (program->globals[g].offset == word && program->globals[g].name[0] != '\0') {
        wset_free(&ctx.writes);
        return false;
      }
  }
  return safe_flow(program, (int64_t)source->exit, end, ctx.writes, NULL);
}
static bool standalone_safe(const qa_q1qc_program *program,
                            const qa_mod_qc_armor_stage *source) {
  const qa_q1qc_function *fn = qa_q1qc_function_named(program, source->function);
  int64_t parameters = 0;
  for (size_t i = 0; i < fn->parameter_size_count; i++) parameters += fn->parameter_sizes[i];
  wset named = {NULL, 0, 0};
  for (size_t g = 0; g < program->global_count; g++) {
    if (program->globals[g].name[0] == '\0') continue;
    int64_t width = strcmp(program->globals[g].type, "vector") == 0 ? 3 : 1;
    for (int64_t i = 0; i < width; i++) wset_add(&named, program->globals[g].offset + i);
  }
  wset missing = {NULL, 0, 0};
  for (int64_t word = fn->parameter_start + parameters;
       word < fn->parameter_start + parameters + (fn->local_words - parameters); word++)
    wset_add(&missing, word);
  for (int64_t index = (int64_t)source->entry; index < (int64_t)source->exit; index++) {
    if (index < 0 || (size_t)index >= program->statement_count) continue;
    qa_q1qc_statement_access access = source_access(program, &program->statements[index]);
    for (size_t i = 0; i < access.read_count; i++)
      if (access.read[i] >= 28 && !wset_has(&named, access.read[i])) wset_add(&missing, access.read[i]);
  }
  wset_free(&named);
  for (int64_t word = fn->parameter_start; word < fn->parameter_start + parameters; word++)
    wset_remove(&missing, word);
  int64_t saved = (int64_t)source->saved;
  return safe_flow(program, (int64_t)source->entry, (int64_t)source->exit, missing, &saved);
}

typedef struct original_stage {
  const char *digest;
  int64_t values[5];
} original_stage;
static const original_stage ORIGINALS[] = {
  {"sha256:f2619787f9aa0f057246eea1665b622b4691b5c5a800b1a46133d1fe8b771580", {1431, 1455, 1580, 1583, 1588}},
  {"sha256:ff51cb5e77360d72b93487d89198dcf94629b92f8bae100fc6ea48a6c12a7830", {377, 401, 855, 858, 863}},
  {"sha256:35a2fdc3acb04bdafe8d0269f5327cd1d5b47971f1ef024429f1572d3201dc82", {2008, 2032, 2061, 2064, 2069}},
  {"sha256:f3610ade82495b6b064f3ba5894d9f40ac9406f92c5f24a1946e87138b4fdf0a", {5145, 5171, 3678, 3681, 3686}},
  {"sha256:36616cd101dfdb1cf1cabfd02c9c50a6bb6b69eaff7d61000f44eeff4291be12", {3060, 3086, 2397, 2400, 2405}},
  {"sha256:f9a2d64e84a6530281c016f1a5557924370fdd9a1a10eb6e785d491352ffacf0", {1317, 1337, 3662, 3665, 3670}},
  {"sha256:39418aa9a7cfcccbc3c195cd757c9f6a20c0b10ba40ce74333de4277144dcb16", {1913, 1933, 4598, 4601, 4606}},
  {"sha256:b828d7dd7150688e5b562cb4ab65f0ec208cebe97804d39f18fd69b7664627ac", {4932, 4954, 5553, 5556, 5561}},
  {"sha256:88b440d1d73ebfcab39d94e78309ba9856012e181b6c66df2cf31018b6563a13", {2493, 2515, 1936, 1939, 1944}},
  {"sha256:1f410f927c1b634f74f538becf8c1c2d04cbdfd3a74759c8fafc425b5fdcb2c0", {2484, 2504, 4443, 4446, 4451}},
  {"sha256:9ed7d5be3f348f02bd009519be7161baaf41a9e8e77eb769d187afab997f3dd9", {2573, 2593, 6214, 6217, 6222}},
};
static const qa_mod_qc_armor_regular_scale SCALES_A[] = {{"superlavaspike_touch", 32005, 0.5}};
static const qa_mod_qc_armor_regular_scale SCALES_B[] = {{"superlavaspike_touch", 31488, 0.5}};

static noreturn void reject_stage(const qa_q1qc_program *program, const char *reason) {
  qa_q1qc_program_error(program, "Unsupported QC armor stage: %s", reason);
}
static bool is_integer(double value) { return value == (double)(int64_t)value; }
static void check_local(const qa_q1qc_program *program, const qa_q1qc_function *fn,
                        double word_value, const char *type) {
  bool ok = is_integer(word_value);
  int64_t word = (int64_t)word_value;
  if (ok && (word < fn->parameter_start || word >= fn->parameter_start + fn->local_words)) ok = false;
  if (ok) {
    bool typed = false;
    for (size_t g = 0; g < program->global_count; g++)
      if (program->globals[g].offset == word && strcmp(program->globals[g].type, type) == 0) typed = true;
    ok = typed;
  }
  if (!ok) reject_stage(program, "typed frame words");
}

void qa_q1qc_armor_stage_destroy(qa_q1qc_armor_stage *stage) {
  if (stage == NULL) return;
  free(stage->statements_copy);
  free(stage->scales_copy);
  free(stage);
}
qa_q1qc_armor_stage *qa_q1qc_armor_stage_create(const qa_q1qc_program *program,
                                                const qa_mod_qc_armor_stage *declared) {
  const original_stage *original = NULL;
  for (size_t i = 0; i < sizeof ORIGINALS / sizeof *ORIGINALS; i++)
    if (program->digest != NULL && strcmp(program->digest, ORIGINALS[i].digest) == 0) original = &ORIGINALS[i];
  const qa_mod_qc_armor_regular_scale *scales = NULL;
  size_t scale_count = 0;
  if (program->digest != NULL &&
      strcmp(program->digest, "sha256:f3610ade82495b6b064f3ba5894d9f40ac9406f92c5f24a1946e87138b4fdf0a") == 0) {
    scales = SCALES_A; scale_count = 1;
  } else if (program->digest != NULL &&
             strcmp(program->digest, "sha256:b828d7dd7150688e5b562cb4ab65f0ec208cebe97804d39f18fd69b7664627ac") == 0) {
    scales = SCALES_B; scale_count = 1;
  }
  if (declared == NULL && original == NULL) return NULL;
  qa_mod_qc_armor_stage synthesized = {0};
  qa_qc_statement *synthesized_statements = NULL;
  const qa_mod_qc_armor_stage *source = declared;
  if (source == NULL) {
    synthesized.function = "T_Damage";
    synthesized.entry = (double)original->values[0];
    synthesized.exit = (double)original->values[1];
    synthesized.target = (double)original->values[2];
    synthesized.damage = (double)original->values[3];
    synthesized.saved = (double)original->values[4];
    synthesized.flags = (qa_mod_qc_armor_flags){false, 0, 0, 0, 0, 0};
    int64_t first = original->values[0], last = original->values[1];
    size_t count = 0;
    if (first >= 0 && (size_t)first <= program->statement_count) {
      int64_t stop = last + 1;
      if (stop > (int64_t)program->statement_count) stop = (int64_t)program->statement_count;
      if (stop > first) count = (size_t)(stop - first);
    }
    synthesized_statements = xmalloc(count * sizeof *synthesized_statements);
    for (size_t i = 0; i < count; i++) {
      const qa_q1qc_statement *actual = &program->statements[first + (int64_t)i];
      synthesized_statements[i] = (qa_qc_statement){(double)actual->opcode, (double)actual->a,
                                                    (double)actual->b, (double)actual->c};
    }
    synthesized.statements = synthesized_statements;
    synthesized.statement_count = count;
    synthesized.regular_scale = scales;
    synthesized.regular_scale_count = scale_count;
    source = &synthesized;
  }
  const qa_q1qc_function *fn = qa_q1qc_function_named(program, source->function);
  int64_t end = qa_q1qc_function_end(program, fn);
  if (fn->named_builtin || fn->first_statement <= 0 || !is_integer(source->entry) ||
      !is_integer(source->exit) || (int64_t)source->entry < fn->first_statement ||
      (int64_t)source->exit <= (int64_t)source->entry || (int64_t)source->exit >= end)
    reject_stage(program, "region bounds");
  check_local(program, fn, source->target, "entity");
  check_local(program, fn, source->damage, "float");
  check_local(program, fn, source->saved, "float");
  {
    wset scale_sites = {NULL, 0, 0};
    for (size_t i = 0; i < source->regular_scale_count; i++) {
      const qa_mod_qc_armor_regular_scale *scale = &source->regular_scale[i];
      const qa_q1qc_function *caller = qa_q1qc_function_named(program, scale->caller);
      const qa_q1qc_statement *statement =
          is_integer(scale->statement) && (int64_t)scale->statement >= 0 &&
                  (size_t)scale->statement < program->statement_count
              ? &program->statements[(size_t)scale->statement] : NULL;
      int64_t caller_end = qa_q1qc_function_end(program, caller);
      const qa_q1qc_definition *damage_global = qa_q1qc_global_by_name(program, "T_Damage");
      bool bad = !is_integer(scale->statement) || (int64_t)scale->statement < caller->first_statement ||
                 (int64_t)scale->statement >= caller_end || statement == NULL ||
                 statement->opcode != QA_Q1QC_OP_CALL4 || damage_global == NULL ||
                 statement->a != damage_global->offset ||
                 !isfinite(scale->scale) || scale->scale < 0 ||
                 wset_has(&scale_sites, (int64_t)scale->statement);
      if (bad) { wset_free(&scale_sites); reject_stage(program, "original regular armor scale call site"); }
      wset_add(&scale_sites, (int64_t)scale->statement);
    }
    wset_free(&scale_sites);
  }
  if (source->target == source->damage || source->target == source->saved ||
      source->damage == source->saved)
    reject_stage(program, "overlapping frame words");
  if (source->flags.is_bits) {
    check_local(program, fn, source->flags.word, "float");
    double masks[4] = {source->flags.no_armor, source->flags.no_power_armor,
                       source->flags.no_regular_armor, source->flags.energy};
    for (int i = 0; i < 4; i++)
      if (!is_integer(masks[i]) || masks[i] < 0 || masks[i] > 0x7fffff)
        reject_stage(program, "source flag masks");
  }
  if ((int64_t)source->statement_count != (int64_t)source->exit - (int64_t)source->entry + 1)
    reject_stage(program, "incomplete instruction declaration");
  for (size_t offset = 0; offset < source->statement_count; offset++) {
    const qa_qc_statement *expected = &source->statements[offset];
    int64_t index = (int64_t)source->entry + (int64_t)offset;
    const qa_q1qc_statement *actual =
        index >= 0 && (size_t)index < program->statement_count ? &program->statements[index] : NULL;
    if (actual == NULL || (double)actual->opcode != expected->opcode ||
        (double)actual->a != expected->a || (double)actual->b != expected->b ||
        (double)actual->c != expected->c)
      reject_stage(program, "instruction declaration differs from artifact");
  }
  const qa_q1qc_statement *first = &program->statements[(size_t)source->entry];
  const qa_q1qc_statement *join = &program->statements[(size_t)source->exit];
  const qa_q1qc_definition *field = qa_q1qc_global_by_name(program, "armortype");
  bool entry_ok = (double)first->a == source->target &&
                  ((first->opcode == QA_Q1QC_OP_LOAD_F && field != NULL && first->b == field->offset) ||
                   ((first->opcode == QA_Q1QC_OP_STORE_F || first->opcode == QA_Q1QC_OP_STORE_ENT) &&
                    first->b == 4));
  if (!entry_ok) reject_stage(program, "entry must precede regular armor reads or call staging");
  if (join->opcode != QA_Q1QC_OP_SUB_F || (double)join->a != source->damage ||
      (double)join->b != source->saved)
    reject_stage(program, "damage-minus-savings join");
  {
    int64_t *pending = NULL;
    size_t pending_count = 0, pending_capacity = 0;
    wset visited = {NULL, 0, 0};
    pending = xrealloc(pending, sizeof *pending);
    pending_capacity = 1;
    pending[pending_count++] = (int64_t)source->entry;
    while (pending_count != 0) {
      int64_t index = pending[--pending_count];
      if (index == (int64_t)source->exit || wset_has(&visited, index)) continue;
      if (index < (int64_t)source->entry || index > (int64_t)source->exit) {
        free(pending); wset_free(&visited);
        reject_stage(program, "control flow leaves regular armor region");
      }
      wset_add(&visited, index);
      if (index < 0 || (size_t)index >= program->statement_count) {
        free(pending); wset_free(&visited);
        reject_stage(program, "missing statement");
      }
      const qa_q1qc_statement *statement = &program->statements[index];
      qa_q1qc_opcode opcode = statement->opcode;
      if (opcode == QA_Q1QC_OP_DONE || opcode == QA_Q1QC_OP_RETURN || opcode == QA_Q1QC_OP_STATE) {
        free(pending); wset_free(&visited);
        reject_stage(program, "region exits or changes actor state");
      }
      int64_t destination = -1;
      if (opcode >= QA_Q1QC_OP_STORE_F && opcode <= QA_Q1QC_OP_STORE_FN) destination = statement->b;
      else if ((opcode >= QA_Q1QC_OP_MUL_F && opcode <= QA_Q1QC_OP_ADDRESS) ||
               (opcode >= QA_Q1QC_OP_NOT_F && opcode <= QA_Q1QC_OP_NOT_FN) ||
               opcode >= QA_Q1QC_OP_AND) destination = statement->c;
      int64_t width = opcode == QA_Q1QC_OP_STORE_V || opcode == QA_Q1QC_OP_MUL_FV ||
                              opcode == QA_Q1QC_OP_MUL_VF || opcode == QA_Q1QC_OP_ADD_V ||
                              opcode == QA_Q1QC_OP_SUB_V || opcode == QA_Q1QC_OP_LOAD_V
                          ? 3 : 1;
      int64_t damage_word = (int64_t)source->damage, target_word = (int64_t)source->target;
      if ((damage_word >= destination && damage_word < destination + width) ||
          (target_word >= destination && target_word < destination + width)) {
        free(pending); wset_free(&visited);
        reject_stage(program, "region changes captured actor or damage local");
      }
      int64_t successors[2];
      size_t successor_count = 1;
      if (opcode == QA_Q1QC_OP_GOTO) successors[0] = index + qa_q1qc_signed_branch(statement->a);
      else if (opcode == QA_Q1QC_OP_IF || opcode == QA_Q1QC_OP_IF_NOT) {
        successors[0] = index + 1;
        successors[1] = index + qa_q1qc_signed_branch(statement->b);
        successor_count = 2;
      } else successors[0] = index + 1;
      for (size_t i = 0; i < successor_count; i++)
        if (successors[i] <= index) {
          free(pending); wset_free(&visited);
          reject_stage(program, "looping armor region");
        }
      for (size_t i = 0; i < successor_count; i++) {
        if (pending_count == pending_capacity) {
          pending_capacity *= 2;
          pending = xrealloc(pending, pending_capacity * sizeof *pending);
        }
        pending[pending_count++] = successors[i];
      }
    }
    free(pending);
    wset_free(&visited);
  }
  bool replaceable = replacement_safe(program, source, end);
  qa_q1qc_armor_stage *stage = xmalloc(sizeof *stage);
  memset(stage, 0, sizeof *stage);
  stage->source = *source;
  stage->statements_copy = xmalloc(source->statement_count * sizeof *stage->statements_copy);
  memcpy(stage->statements_copy, source->statements,
         source->statement_count * sizeof *stage->statements_copy);
  stage->source.statements = stage->statements_copy;
  if (source->regular_scale_count != 0) {
    stage->scales_copy = xmalloc(source->regular_scale_count * sizeof *stage->scales_copy);
    memcpy(stage->scales_copy, source->regular_scale,
           source->regular_scale_count * sizeof *stage->scales_copy);
    stage->source.regular_scale = stage->scales_copy;
  }
  stage->region = (qa_q1qc_inline_region){fn->index, (int64_t)source->entry,
                                           (int64_t)source->exit, replaceable, false, 0, NULL};
  if (replaceable && standalone_safe(program, source)) {
    stage->region.has_standalone = true;
    stage->region.standalone_saved = (int64_t)source->saved;
  }
  free(synthesized_statements);
  return stage;
}
