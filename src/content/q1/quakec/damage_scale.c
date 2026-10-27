/* Port of src/content/q1/quakec/damage-scale.ts. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/damage_scale.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/quakec/armor_stage.h"
#include "qa/content/q1/quakec/damage_call.h"
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
static noreturn void reject_scale(const qa_q1qc_program *program, const char *reason) {
  qa_q1qc_program_error(program, "Unsupported QC damage scale: %s", reason);
}
static bool is_integer(double value) { return value == (double)(int64_t)value; }

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
static void wset_free(wset *set) { free(set->items); set->items = NULL; set->count = set->capacity = 0; }

typedef enum value_kind { VALUE_ACTOR, VALUE_SCALAR, VALUE_SCALED } value_kind;
typedef struct scale_value {
  value_kind kind;
  bool has_constant; double constant;
  int64_t *origins; size_t origin_count, origin_capacity;
  int operations; bool identity; bool powers_of_two;
} scale_value;
static scale_value value_actor(void) { scale_value v = {VALUE_ACTOR, false, 0, NULL, 0, 0, 0, false, false}; return v; }
static scale_value value_scalar(void) { scale_value v = {VALUE_SCALAR, false, 0, NULL, 0, 0, 0, false, false}; return v; }
static scale_value value_scaled(int operations, bool identity, bool powers) {
  scale_value v = {VALUE_SCALED, false, 0, NULL, 0, 0, operations, identity, powers};
  return v;
}
static void value_free(scale_value *value) { free(value->origins); value->origins = NULL; value->origin_count = value->origin_capacity = 0; }
static scale_value value_clone(const scale_value *value) {
  scale_value copy = *value;
  copy.origins = NULL; copy.origin_count = 0; copy.origin_capacity = 0;
  for (size_t i = 0; i < value->origin_count; i++) {
    if (copy.origin_count == copy.origin_capacity) {
      copy.origin_capacity = copy.origin_capacity == 0 ? 4 : copy.origin_capacity * 2;
      copy.origins = xrealloc(copy.origins, copy.origin_capacity * sizeof *copy.origins);
    }
    copy.origins[copy.origin_count++] = value->origins[i];
  }
  return copy;
}
static void value_push_origin(scale_value *value, int64_t word) {
  if (value->origin_count == value->origin_capacity) {
    value->origin_capacity = value->origin_capacity == 0 ? 4 : value->origin_capacity * 2;
    value->origins = xrealloc(value->origins, value->origin_capacity * sizeof *value->origins);
  }
  value->origins[value->origin_count++] = word;
}

/* word -> value map; null marker entries serve the TS context map. */
typedef struct value_entry { int64_t word; bool is_null; scale_value value; } value_entry;
typedef struct value_map { value_entry *items; size_t count, capacity; } value_map;
static value_entry *value_map_find(value_map *map, int64_t word) {
  for (size_t i = 0; i < map->count; i++) if (map->items[i].word == word) return &map->items[i];
  return NULL;
}
static void value_map_set(value_map *map, int64_t word, scale_value value) {
  value_entry *existing = value_map_find(map, word);
  if (existing != NULL) { value_free(&existing->value); existing->is_null = false; existing->value = value; return; }
  if (map->count == map->capacity) {
    map->capacity = map->capacity == 0 ? 8 : map->capacity * 2;
    map->items = xrealloc(map->items, map->capacity * sizeof *map->items);
  }
  map->items[map->count++] = (value_entry){word, false, value};
}
static void value_map_set_null(value_map *map, int64_t word) {
  value_entry *existing = value_map_find(map, word);
  if (existing != NULL) { value_free(&existing->value); existing->is_null = true; existing->value = value_scalar(); return; }
  if (map->count == map->capacity) {
    map->capacity = map->capacity == 0 ? 8 : map->capacity * 2;
    map->items = xrealloc(map->items, map->capacity * sizeof *map->items);
  }
  map->items[map->count++] = (value_entry){word, true, value_scalar()};
}
static value_map value_map_clone(const value_map *map) {
  value_map copy = {NULL, 0, 0};
  for (size_t i = 0; i < map->count; i++) {
    if (map->items[i].is_null) value_map_set_null(&copy, map->items[i].word);
    else value_map_set(&copy, map->items[i].word, value_clone(&map->items[i].value));
  }
  return copy;
}
static void value_map_free(value_map *map) {
  for (size_t i = 0; i < map->count; i++) value_free(&map->items[i].value);
  free(map->items);
  map->items = NULL; map->count = map->capacity = 0;
}

typedef struct scale_ctx {
  const qa_q1qc_program *program;
  const qa_mod_qc_damage_scale *source;
  const qa_q1qc_function *fn;
  int64_t damage, parameter_end;
  bool transform;
  value_map context;
  wset prefix_writes, named, mutable, scratch, constants;
} scale_ctx;

static scale_value merge_values(scale_ctx *ctx, const scale_value *left, const scale_value *right) {
  if (left->kind != right->kind) {
    if (ctx->transform && left->kind != VALUE_ACTOR && right->kind != VALUE_ACTOR)
      return value_scaled(0, false, false);
    reject_scale(ctx->program, "control flow replaces damage with an independent value");
  }
  if (left->kind == VALUE_ACTOR) return value_actor();
  if (left->kind == VALUE_SCALAR) {
    bool same = left->has_constant == right->has_constant &&
                (!left->has_constant || left->constant == right->constant);
    if (!same) return value_scalar();
    scale_value merged = value_clone(left);
    if (left->origin_count + right->origin_count != 0)
      for (size_t i = 0; i < right->origin_count; i++) value_push_origin(&merged, right->origins[i]);
    return merged;
  }
  return value_scaled(left->operations > right->operations ? left->operations : right->operations,
                      left->identity && right->identity,
                      left->powers_of_two && right->powers_of_two);
}
static scale_value read_value(scale_ctx *ctx, value_map *values, int64_t word) {
  value_entry *known = value_map_find(values, word);
  if (known != NULL && !known->is_null) return value_clone(&known->value);
  const qa_q1qc_program *program = ctx->program;
  if (word < 28 || word * 4 + 4 > (int64_t)program->initial_globals_length)
    reject_scale(program, "unavailable original word");
  bool in_frame = word >= ctx->fn->parameter_start &&
                  word < ctx->fn->parameter_start + ctx->fn->local_words;
  if (in_frame || (!wset_has(&ctx->named, word) && wset_has(&ctx->mutable, word)))
    reject_scale(program, "undeclared local or target/inflictor dependency");
  value_entry *supplied_entry = value_map_find(&ctx->context, word);
  if (supplied_entry != NULL && supplied_entry->is_null)
    reject_scale(program, "non-attacker declared entity context");
  if (supplied_entry != NULL) return value_clone(&supplied_entry->value);
  if (wset_has(&ctx->prefix_writes, word))
    reject_scale(program, "unexecuted source context assignment");
  const qa_q1qc_definition *time_global = qa_q1qc_global_by_name(program, "time");
  if (time_global != NULL && time_global->offset == word)
    reject_scale(program, "current source time must be declared");
  for (size_t g = 0; g < program->global_count; g++)
    if (program->globals[g].offset == word && strcmp(program->globals[g].type, "entity") == 0)
      reject_scale(program, "non-attacker entity context");
  if (wset_has(&ctx->mutable, word)) return value_scalar();
  scale_value constant = value_scalar();
  constant.has_constant = true;
  constant.constant = (double)qa_q1qc_initial_float32(program, word);
  value_push_origin(&constant, word);
  return constant;
}
static scale_value independent_value(scale_ctx *ctx, value_map *values, int64_t word) {
  scale_value value = read_value(ctx, values, word);
  if (value.kind == VALUE_SCALED) {
    value_free(&value);
    reject_scale(ctx->program, "nonlinear damage-dependent predicate or address");
  }
  return value;
}
static void write_value(scale_ctx *ctx, value_map *values, int64_t word, scale_value value) {
  const qa_q1qc_program *program = ctx->program;
  bool in_local_tail = word >= ctx->parameter_end &&
                       word < ctx->fn->parameter_start + ctx->fn->local_words;
  if (word < 28 || word * 4 + 4 > (int64_t)program->initial_globals_length ||
      (word != ctx->damage &&
       ((word >= ctx->fn->parameter_start && word < ctx->parameter_end) ||
        (wset_has(&ctx->named, word) && !in_local_tail)))) {
    value_free(&value);
    reject_scale(program, "source write outside private frame");
  }
  if (word == ctx->damage &&
      (value.kind == VALUE_ACTOR || (!ctx->transform && value.kind != VALUE_SCALED))) {
    value_free(&value);
    reject_scale(program, "damage overwritten by independent value");
  }
  wset_add(&ctx->scratch, word);
  value_map_set(values, word, value);
}

typedef struct pending_entry { int64_t pc; value_map values; } pending_entry;
typedef struct pending_map { pending_entry *items; size_t count, capacity; } pending_map;
static void pending_edge(scale_ctx *ctx, pending_map *pending, int64_t from, int64_t to,
                         value_map *values) {
  if (to <= from || to < (int64_t)ctx->source->entry || to > (int64_t)ctx->source->exit)
    reject_scale(ctx->program, "escaping or backward branch");
  pending_entry *previous = NULL;
  for (size_t i = 0; i < pending->count; i++) if (pending->items[i].pc == to) previous = &pending->items[i];
  if (previous == NULL) {
    if (pending->count == pending->capacity) {
      pending->capacity = pending->capacity == 0 ? 8 : pending->capacity * 2;
      pending->items = xrealloc(pending->items, pending->capacity * sizeof *pending->items);
    }
    pending->items[pending->count++] = (pending_entry){to, value_map_clone(values)};
    return;
  }
  value_map merged = {NULL, 0, 0};
  for (size_t i = 0; i < previous->values.count; i++) {
    value_entry *next = value_map_find(values, previous->values.items[i].word);
    if (next == NULL) continue;
    value_map_set(&merged, previous->values.items[i].word,
                  merge_values(ctx, &previous->values.items[i].value, &next->value));
  }
  value_map_free(&previous->values);
  previous->values = merged;
}
static bool is_binary(qa_q1qc_opcode opcode) {
  switch (opcode) {
    case QA_Q1QC_OP_ADD_F: case QA_Q1QC_OP_SUB_F: case QA_Q1QC_OP_MUL_F: case QA_Q1QC_OP_DIV_F:
    case QA_Q1QC_OP_EQ_F: case QA_Q1QC_OP_NE_F: case QA_Q1QC_OP_EQ_S: case QA_Q1QC_OP_NE_S:
    case QA_Q1QC_OP_LE: case QA_Q1QC_OP_GE: case QA_Q1QC_OP_LT: case QA_Q1QC_OP_GT:
    case QA_Q1QC_OP_AND: case QA_Q1QC_OP_OR: case QA_Q1QC_OP_BIT_AND: case QA_Q1QC_OP_BIT_OR:
      return true;
    default: return false;
  }
}

void qa_q1qc_damage_scale_destroy(qa_q1qc_damage_scale *scale) {
  if (scale == NULL) return;
  free(scale->scratch);
  free(scale->constants);
  free(scale);
}

qa_q1qc_damage_scale *qa_q1qc_damage_scale_create(const qa_q1qc_program *program,
                                                  const qa_mod_source_call *call,
                                                  const qa_mod_qc_damage_scale *source) {
  if (source == NULL) return NULL;
  const qa_q1qc_function *fn = qa_q1qc_function_named(program, source->function);
  int64_t end = qa_q1qc_function_end(program, fn);
  if (strcmp(source->function, call->function) != 0 || fn->first_statement <= 0 || fn->named_builtin ||
      !is_integer(source->entry) || !is_integer(source->exit) ||
      (int64_t)source->entry < fn->first_statement ||
      (int64_t)source->exit <= (int64_t)source->entry || (int64_t)source->exit >= end)
    reject_scale(program, "original damage region bounds");
  qa_q1qc_damage_call_layout *layout = qa_q1qc_damage_call_layout_create(program, call);
  qa_q1qc_role_locations amount = layout->roles[QA_Q1QC_ROLE_AMOUNT];
  if (amount.count != 1) reject_scale(program, "scale result requires one original float input");
  qa_q1qc_damage_role_location location = amount.items[0];
  int64_t damage = location.is_global ? location.word : location.frame_word;
  int64_t parameter_words = 0;
  for (size_t i = 0; i < fn->parameter_size_count; i++) parameter_words += fn->parameter_sizes[i];
  int64_t parameter_end = fn->parameter_start + parameter_words;
  if (!is_integer(source->damage) || (int64_t)source->damage != damage ||
      fn->local_words < parameter_end - fn->parameter_start)
    reject_scale(program, "original damage parameter");
  static const char *const AVAILABLE[] = {"self", "attacker", "inflictor", "amount", "time"};
  qa_q1qc_validate_source_call(program, call, AVAILABLE, 5, "damage scale");
  scale_ctx ctx = {0};
  ctx.program = program;
  ctx.source = source;
  ctx.fn = fn;
  ctx.damage = damage;
  ctx.parameter_end = parameter_end;
  ctx.transform = source->kind != NULL && strcmp(source->kind, "transform") == 0;
  for (size_t i = 0; i < call->global_count; i++) {
    const qa_mod_named_value *global = &call->globals[i];
    const qa_q1qc_definition *definition = qa_q1qc_global_by_name(program, global->name);
    if (definition == NULL) reject_scale(program, "missing declared context global");
    const qa_mod_callback_value *value = &global->value;
    if (strcmp(value->kind, "input") == 0) {
      if (strcmp(value->name, "attacker") == 0)
        value_map_set(&ctx.context, definition->offset, value_actor());
      else if (strcmp(value->name, "amount") == 0)
        value_map_set(&ctx.context, definition->offset, value_scaled(0, true, true));
      else if (strcmp(value->name, "time") == 0)
        value_map_set(&ctx.context, definition->offset, value_scalar());
      else value_map_set_null(&ctx.context, definition->offset);
    } else if (strcmp(value->kind, "float") == 0) {
      scale_value constant = value_scalar();
      constant.has_constant = true;
      constant.constant = (double)(float)value->number_value;
      value_map_set(&ctx.context, definition->offset, constant);
    } else {
      int64_t width = strcmp(definition->type, "vector") == 0 ? 3 : 1;
      for (int64_t w = 0; w < width; w++)
        value_map_set(&ctx.context, definition->offset + w, value_scalar());
    }
  }
  wset amount_inputs = {NULL, 0, 0};
  wset_add(&amount_inputs, damage);
  for (size_t i = 0; i < ctx.context.count; i++)
    if (!ctx.context.items[i].is_null && ctx.context.items[i].value.kind == VALUE_SCALED)
      wset_add(&amount_inputs, ctx.context.items[i].word);
  for (int64_t index = fn->first_statement; index < (int64_t)source->entry; index++) {
    const qa_q1qc_statement *statement = &program->statements[index];
    qa_q1qc_statement_access access = qa_q1qc_statement_access_of(*statement);
    for (size_t i = 0; i < access.read_count; i++)
      if (wset_has(&amount_inputs, access.read[i]))
        reject_scale(program, "damage-dependent policy precedes the scaling region");
    qa_q1qc_opcode opcode = statement->opcode;
    if ((opcode >= QA_Q1QC_OP_STORE_PF && opcode <= QA_Q1QC_OP_STORE_PFN) ||
        (opcode >= QA_Q1QC_OP_CALL0 && opcode <= QA_Q1QC_OP_CALL8) || opcode == QA_Q1QC_OP_STATE)
      reject_scale(program, "source side effects precede the scale query");
    bool self_store = opcode >= QA_Q1QC_OP_STORE_F && opcode <= QA_Q1QC_OP_STORE_FN &&
                      statement->a == statement->b;
    if (!self_store)
      for (size_t i = 0; i < access.write_count; i++) wset_add(&ctx.prefix_writes, access.write[i]);
  }
  wset_free(&amount_inputs);
  bool attacker_written = false;
  for (size_t i = 0; i < layout->roles[QA_Q1QC_ROLE_ATTACKER].count; i++) {
    const qa_q1qc_damage_role_location *at = &layout->roles[QA_Q1QC_ROLE_ATTACKER].items[i];
    if (wset_has(&ctx.prefix_writes, at->is_global ? at->word : at->frame_word)) attacker_written = true;
  }
  if (wset_has(&ctx.prefix_writes, damage) || attacker_written)
    reject_scale(program, "source changes scale arguments before the region");
  if ((int64_t)source->statement_count != (int64_t)source->exit - (int64_t)source->entry + 1)
    reject_scale(program, "incomplete original instructions");
  for (size_t offset = 0; offset < source->statement_count; offset++) {
    const qa_qc_statement *expected = &source->statements[offset];
    int64_t index = (int64_t)source->entry + (int64_t)offset;
    const qa_q1qc_statement *actual =
        index >= 0 && (size_t)index < program->statement_count ? &program->statements[index] : NULL;
    if (actual == NULL || (double)actual->opcode != expected->opcode ||
        (double)actual->a != expected->a || (double)actual->b != expected->b ||
        (double)actual->c != expected->c)
      reject_scale(program, "instructions differ from artifact");
  }
  for (size_t g = 0; g < program->global_count; g++) {
    if (program->globals[g].name[0] == '\0') continue;
    int64_t width = strcmp(program->globals[g].type, "vector") == 0 ? 3 : 1;
    for (int64_t i = 0; i < width; i++) wset_add(&ctx.named, program->globals[g].offset + i);
  }
  for (size_t i = 0; i < program->statement_count; i++) {
    qa_q1qc_statement_access access = qa_q1qc_statement_access_of(program->statements[i]);
    for (size_t w = 0; w < access.write_count; w++) wset_add(&ctx.mutable, access.write[w]);
  }
  value_map initial_values = {NULL, 0, 0};
  value_map_set(&initial_values, damage, value_scaled(0, true, true));
  for (size_t i = 0; i < layout->roles[QA_Q1QC_ROLE_ATTACKER].count; i++) {
    const qa_q1qc_damage_role_location *at = &layout->roles[QA_Q1QC_ROLE_ATTACKER].items[i];
    if (!at->is_global) value_map_set(&initial_values, at->frame_word, value_actor());
  }
  pending_map pending = {NULL, 0, 0};
  pending.items = xmalloc(sizeof *pending.items);
  pending.capacity = 1;
  pending.items[pending.count++] = (pending_entry){(int64_t)source->entry, initial_values};
  bool joined = false, multiplied = false;
  const char *kind = source->kind != NULL ? source->kind : "multiplier";
  while (pending.count != 0) {
    size_t min_at = 0;
    for (size_t i = 1; i < pending.count; i++)
      if (pending.items[i].pc < pending.items[min_at].pc) min_at = i;
    pending_entry current = pending.items[min_at];
    memmove(&pending.items[min_at], &pending.items[min_at + 1],
            (pending.count - min_at - 1) * sizeof *pending.items);
    pending.count--;
    int64_t pc = current.pc;
    value_map values = current.values;
    if (pc == (int64_t)source->exit) {
      value_entry *result = value_map_find(&values, damage);
      if (result == NULL || result->value.kind == VALUE_ACTOR ||
          (!ctx.transform && result->value.kind != VALUE_SCALED))
        reject_scale(program, "join does not retain scalar damage");
      if (strcmp(kind, "identity") == 0 &&
          (result->value.kind != VALUE_SCALED || !result->value.identity))
        reject_scale(program, "identity region changes its original damage input");
      joined = true;
      value_map_free(&values);
      continue;
    }
    if (pc < 0 || (size_t)pc >= program->statement_count) reject_scale(program, "missing source instruction");
    const qa_q1qc_statement *statement = &program->statements[pc];
    qa_q1qc_opcode opcode = statement->opcode;
    int64_t a = statement->a, b = statement->b, c = statement->c;
    bool fallthrough = true;
    if (opcode == QA_Q1QC_OP_IF || opcode == QA_Q1QC_OP_IF_NOT) {
      scale_value probe = ctx.transform ? read_value(&ctx, &values, a)
                                         : independent_value(&ctx, &values, a);
      value_free(&probe);
      pending_edge(&ctx, &pending, pc, pc + qa_q1qc_signed_branch(b), &values);
    } else if (opcode == QA_Q1QC_OP_GOTO) {
      pending_edge(&ctx, &pending, pc, pc + qa_q1qc_signed_branch(a), &values);
      fallthrough = false;
    } else if (opcode == QA_Q1QC_OP_LOAD_F || opcode == QA_Q1QC_OP_LOAD_S) {
      scale_value owner = read_value(&ctx, &values, a);
      value_kind owner_kind = owner.kind;
      value_free(&owner);
      if (owner_kind != VALUE_ACTOR) reject_scale(program, "entity reads must belong to the original attacker");
      scale_value field = independent_value(&ctx, &values, b);
      value_free(&field);
      write_value(&ctx, &values, c, value_scalar());
    } else if (opcode == QA_Q1QC_OP_STORE_F || opcode == QA_Q1QC_OP_STORE_ENT || opcode == QA_Q1QC_OP_STORE_S) {
      write_value(&ctx, &values, b, read_value(&ctx, &values, a));
    } else if (opcode == QA_Q1QC_OP_NOT_F || opcode == QA_Q1QC_OP_NOT_S || opcode == QA_Q1QC_OP_NOT_ENT) {
      scale_value value = ctx.transform ? read_value(&ctx, &values, a)
                                         : independent_value(&ctx, &values, a);
      if (value.kind == VALUE_SCALED) {
        scale_value negated = value_clone(&value);
        negated.identity = false;
        value_free(&value);
        write_value(&ctx, &values, c, negated);
      } else {
        value_free(&value);
        write_value(&ctx, &values, c, value_scalar());
      }
    } else if (is_binary(opcode)) {
      scale_value left = read_value(&ctx, &values, a);
      scale_value right = read_value(&ctx, &values, b);
      if (ctx.transform) {
        if (left.kind == VALUE_ACTOR || right.kind == VALUE_ACTOR) {
          value_free(&left); value_free(&right);
          reject_scale(program, "damage arithmetic cannot consume entity references");
        }
        bool any_scaled = left.kind == VALUE_SCALED || right.kind == VALUE_SCALED;
        value_free(&left); value_free(&right);
        write_value(&ctx, &values, c, any_scaled ? value_scaled(0, false, false) : value_scalar());
      } else if (left.kind != VALUE_SCALED && right.kind != VALUE_SCALED) {
        value_free(&left); value_free(&right);
        write_value(&ctx, &values, c, value_scalar());
      } else {
        const scale_value *scaled = left.kind == VALUE_SCALED ? &left : &right;
        const scale_value *factor = left.kind == VALUE_SCALAR ? &left : &right;
        if (scaled->kind != VALUE_SCALED || factor->kind != VALUE_SCALAR ||
            (opcode != QA_Q1QC_OP_MUL_F && opcode != QA_Q1QC_OP_DIV_F) ||
            (opcode == QA_Q1QC_OP_DIV_F && left.kind != VALUE_SCALED)) {
          value_free(&left); value_free(&right);
          reject_scale(program, "nonmultiplicative damage transformation");
        }
        bool power = factor->has_constant && factor->constant > 0 &&
                     log2(factor->constant) == floor(log2(factor->constant));
        if (opcode == QA_Q1QC_OP_DIV_F && !power) {
          value_free(&left); value_free(&right);
          reject_scale(program, "division requires an exactly representable reciprocal");
        }
        if (power)
          for (size_t i = 0; i < factor->origin_count; i++)
            wset_add(&ctx.constants, factor->origins[i]);
        bool increasing = power && factor->has_constant &&
                          (opcode == QA_Q1QC_OP_DIV_F ? factor->constant <= 1 : factor->constant >= 1);
        if (scaled->operations > 0 && !(scaled->powers_of_two && increasing)) {
          value_free(&left); value_free(&right);
          reject_scale(program, "multiple rounded scales cannot be represented by one factor");
        }
        scale_value product = value_scaled(scaled->operations + 1,
                                           scaled->identity && factor->has_constant && factor->constant == 1,
                                           scaled->powers_of_two && increasing);
        multiplied = true;
        value_free(&left); value_free(&right);
        write_value(&ctx, &values, c, product);
      }
    } else {
      reject_scale(program, "region must contain only source reads and scalar frame operations");
    }
    if (fallthrough) pending_edge(&ctx, &pending, pc, pc + 1, &values);
    value_map_free(&values);
  }
  free(pending.items);
  if (!joined || (strcmp(kind, "identity") != 0 && !ctx.transform && !multiplied))
    reject_scale(program, "region has no original multiplicative result");
  for (int64_t pc = fn->first_statement; pc < end; pc++) {
    if (pc >= (int64_t)source->entry && pc < (int64_t)source->exit) continue;
    if (pc < 0 || (size_t)pc >= program->statement_count) continue;
    const qa_q1qc_statement *statement = &program->statements[pc];
    int64_t destination = statement->opcode == QA_Q1QC_OP_GOTO
                              ? pc + qa_q1qc_signed_branch(statement->a)
                          : statement->opcode == QA_Q1QC_OP_IF || statement->opcode == QA_Q1QC_OP_IF_NOT
                              ? pc + qa_q1qc_signed_branch(statement->b) : -1;
    if (destination > (int64_t)source->entry && destination < (int64_t)source->exit)
      reject_scale(program, "incoming interior branch");
    if (pc < (int64_t)source->entry && destination >= (int64_t)source->exit)
      reject_scale(program, "source path bypasses scaling before continuing damage");
    if (pc >= (int64_t)source->exit && destination >= fn->first_statement &&
        destination <= (int64_t)source->entry)
      reject_scale(program, "source path repeats scaling");
  }
  wset private_writes = {NULL, 0, 0};
  for (size_t i = 0; i < ctx.scratch.count; i++)
    if (ctx.scratch.items[i] != damage) wset_add(&private_writes, ctx.scratch.items[i]);
  bool dead = qa_q1qc_region_private_writes_are_dead(program, (int64_t)source->exit, end,
                                                     private_writes.items, private_writes.count);
  wset_free(&private_writes);
  if (!dead) reject_scale(program, "private scale outputs remain live after suppression");
  qa_q1qc_damage_scale *scale = xmalloc(sizeof *scale);
  memset(scale, 0, sizeof *scale);
  scale->kind = kind;
  scale->call = *call;
  scale->scratch_count = ctx.scratch.count;
  scale->scratch = xmalloc(ctx.scratch.count * sizeof *scale->scratch);
  memcpy(scale->scratch, ctx.scratch.items, ctx.scratch.count * sizeof *scale->scratch);
  scale->constant_count = ctx.constants.count;
  scale->constants = xmalloc(ctx.constants.count * sizeof *scale->constants);
  for (size_t i = 0; i < ctx.constants.count; i++) {
    scale->constants[i].word = ctx.constants.items[i];
    scale->constants[i].bits = qa_q1qc_initial_int32(program, ctx.constants.items[i]);
  }
  scale->region = (qa_q1qc_inline_region){fn->index, (int64_t)source->entry, (int64_t)source->exit,
                                          true, true, damage,
                                          location.is_global ? "global" : NULL};
  qa_q1qc_damage_call_layout_destroy(layout);
  value_map_free(&ctx.context);
  wset_free(&ctx.prefix_writes);
  wset_free(&ctx.named);
  wset_free(&ctx.mutable);
  wset_free(&ctx.scratch);
  wset_free(&ctx.constants);
  return scale;
}

typedef struct scale_reference_ctx {
  qa_q1qc_machine *machine;
  qa_actor_id owner;
  int64_t actor_reference;
} scale_reference_ctx;
static int64_t scale_reference(void *ctx, const qa_actor_id *actor) {
  scale_reference_ctx *reference = ctx;
  if (actor == NULL) return qa_q1qc_entities_reference(reference->machine->entities, 0);
  if (!qa_q1qc_actor_equal(*actor, reference->owner))
    qa_q1qc_error("progs.dat", "QC damage scale changed its source attacker");
  return reference->actor_reference;
}
typedef struct scale_execute_ctx {
  qa_q1qc_machine *machine;
  qa_q1qc_inline_region region;
} scale_execute_ctx;
static double scale_execute_region(void *ctx, int64_t argument_count) {
  scale_execute_ctx *execute = ctx;
  return execute->machine->execute_region(execute->machine->ctx, execute->region, argument_count);
}
double qa_q1qc_evaluate_damage_scale(qa_q1qc_machine *machine,
                                     const qa_q1qc_damage_scale *scale,
                                     qa_actor_id owner, int64_t actor_reference, double seconds) {
  if (strcmp(scale->kind, "transform") == 0)
    qa_q1qc_error("progs.dat", "Original QC amount transform cannot be queried as a multiplier");
  return qa_q1qc_evaluate_damage_amount(machine, scale, owner, actor_reference, seconds, 1);
}
double qa_q1qc_evaluate_damage_amount(qa_q1qc_machine *machine,
                                      const qa_q1qc_damage_scale *scale,
                                      qa_actor_id owner, int64_t actor_reference,
                                      double seconds, double amount) {
  qa_actor_id owner_id = owner;
  qa_mod_runtime_value actor_value = {"actor", 0, NULL, {0, 0, 0}, &owner_id};
  qa_mod_runtime_value amount_value = {"float", amount, NULL, {0, 0, 0}, NULL};
  qa_mod_runtime_value time_value = {"float", seconds, NULL, {0, 0, 0}, NULL};
  qa_q1qc_source_input inputs[5] = {{"self", actor_value}, {"attacker", actor_value},
                                    {"inflictor", actor_value}, {"amount", amount_value},
                                    {"time", time_value}};
  for (size_t i = 0; i < scale->constant_count; i++)
    if (qa_q1qc_words_int(&machine->globals, scale->constants[i].word) != scale->constants[i].bits)
      qa_q1qc_error("progs.dat", "Original QC scaling constant changed after qualification");
  int32_t *saved = xmalloc(scale->scratch_count * sizeof *saved);
  for (size_t i = 0; i < scale->scratch_count; i++)
    saved[i] = qa_q1qc_words_int(&machine->globals, scale->scratch[i]);
  scale_reference_ctx reference = {machine, owner, actor_reference};
  scale_execute_ctx execute = {machine, scale->region};
  double result = qa_q1qc_with_source_call(machine, &scale->call, inputs, 5,
                                           scale_reference, &reference,
                                           scale_execute_region, &execute);
  /* TS restores scratch words in a finally. */
  for (size_t i = 0; i < scale->scratch_count; i++)
    qa_q1qc_words_set_int(&machine->globals, scale->scratch[i], saved[i]);
  free(saved);
  if (!isfinite(result) || result < 0)
    qa_q1qc_error("progs.dat", "Original QC damage amount is not finite and nonnegative");
  return result;
}
