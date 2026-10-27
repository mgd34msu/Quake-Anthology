/* Port of src/content/q1/quakec/pickup-callers.ts. GPL-2.0-or-later. */
#include "qa/content/q1/quakec/pickup_callers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *field_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *field_choice(const qa_save_reader *reader, const char *name,
                                const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_integer_min(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer_min(&f, minimum);
  qa_save_reader_destroy(&f);
  return value;
}
static void *xmalloc_at(const qa_save_reader *reader, size_t size) {
  void *value = malloc(size != 0 ? size : 1);
  if (value == NULL) qa_save_reader_fail(reader, "out of memory");
  return value;
}
static void *xmalloc(size_t size) {
  void *value = malloc(size != 0 ? size : 1);
  if (value == NULL) qa_range_error("out of memory");
  return value;
}

static qa_qc_pickup_scalar read_scalar(const qa_save_reader *reader) {
  static const char *const KINDS[2] = {"field", "global"};
  const char *kind = field_choice(reader, "kind", KINDS, 2);
  qa_qc_pickup_scalar scalar;
  memset(&scalar, 0, sizeof scalar);
  if (strcmp(kind, "field") == 0) scalar.name = field_string(reader, "name");
  else { scalar.is_global = true; scalar.word = field_integer_min(reader, "word", 0); }
  return scalar;
}
static qa_qc_pickup_item read_item(const qa_save_reader *reader) {
  qa_qc_pickup_item result;
  memset(&result, 0, sizeof result);
  qa_save_reader item_reader = field(reader, "item");
  result.item = qa_namespaced(&item_reader);
  qa_save_reader_destroy(&item_reader);
  qa_save_reader resource_reader = field(reader, "resource");
  if (!qa_save_reader_is_null(&resource_reader)) {
    qa_pickup_resource *resource = xmalloc_at(reader, sizeof *resource);
    memset(resource, 0, sizeof *resource);
    static const char *const KINDS[2] = {"protection", "inventory"};
    const char *kind = field_choice(&resource_reader, "kind", KINDS, 2);
    if (strcmp(kind, "protection") == 0) {
      resource->kind = QA_PICKUP_RESOURCE_PROTECTION;
      static const char *const CHANNELS[2] = {"regular", "powered"};
      resource->channel = strcmp(field_choice(&resource_reader, "channel", CHANNELS, 2), "powered") == 0
                              ? QA_PROTECTION_POWERED : QA_PROTECTION_REGULAR;
    } else {
      resource->kind = QA_PICKUP_RESOURCE_INVENTORY;
      qa_save_reader nested = field(&resource_reader, "item");
      resource->item = qa_namespaced(&nested);
      qa_save_reader_destroy(&nested);
    }
    result.resource = resource;
  }
  qa_save_reader_destroy(&resource_reader);
  qa_save_reader count_reader = field(reader, "count");
  if (!qa_save_reader_is_undefined(&count_reader)) {
    qa_qc_pickup_scalar *count = xmalloc_at(reader, sizeof *count);
    *count = read_scalar(&count_reader);
    result.count = count;
  }
  qa_save_reader_destroy(&count_reader);
  return result;
}
qa_qc_pickup_caller_declaration qa_q1qc_read_pickup_caller(const qa_save_reader *reader) {
  qa_qc_pickup_caller_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  declaration.function = field_string(reader, "function");
  qa_save_reader descriptor = field(reader, "descriptor");
  static const char *const KINDS[3] = {"constant", "string", "float"};
  const char *kind = field_choice(&descriptor, "kind", KINDS, 3);
  declaration.descriptor.kind = kind;
  if (strcmp(kind, "constant") == 0) {
    declaration.descriptor.constant_item = read_item(&descriptor);
  } else {
    declaration.descriptor.field = field_string(&descriptor, "field");
    qa_save_reader values = field(&descriptor, "values");
    size_t count = qa_save_reader_list_count(&values);
    if (strcmp(kind, "string") == 0) {
      qa_qc_pickup_item_string_value *entries = xmalloc_at(&values, count * sizeof *entries);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&values, i);
        entries[i].item = read_item(&entry);
        entries[i].value = field_string(&entry, "value");
        qa_save_reader_destroy(&entry);
      }
      declaration.descriptor.string_values = entries;
      declaration.descriptor.string_value_count = count;
    } else {
      qa_qc_pickup_item_float_value *entries = xmalloc_at(&values, count * sizeof *entries);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&values, i);
        entries[i].item = read_item(&entry);
        qa_save_reader value = field(&entry, "value");
        entries[i].value = qa_save_reader_finite(&value);
        qa_save_reader_destroy(&value);
        qa_save_reader_destroy(&entry);
      }
      declaration.descriptor.float_values = entries;
      declaration.descriptor.float_value_count = count;
    }
    qa_save_reader_destroy(&values);
  }
  qa_save_reader_destroy(&descriptor);
  qa_save_reader dropped = field(reader, "dropped");
  if (!qa_save_reader_is_undefined(&dropped)) {
    qa_qc_pickup_scalar *scalar = xmalloc_at(reader, sizeof *scalar);
    *scalar = read_scalar(&dropped);
    declaration.dropped = scalar;
  }
  qa_save_reader_destroy(&dropped);
  qa_save_reader regions = field(reader, "regions");
  size_t region_count = qa_save_reader_list_count(&regions);
  qa_qc_pickup_region *entries = xmalloc_at(&regions, region_count * sizeof *entries);
  for (size_t i = 0; i < region_count; i++) {
    qa_save_reader region = item(&regions, i);
    memset(&entries[i], 0, sizeof entries[i]);
    entries[i].entry = field_integer_min(&region, "entry", 0);
    entries[i].exit = field_integer_min(&region, "exit", 0);
    qa_save_reader statements = field(&region, "statements");
    size_t statement_count = qa_save_reader_list_count(&statements);
    qa_qc_statement *words = xmalloc_at(&statements, statement_count * sizeof *words);
    for (size_t s = 0; s < statement_count; s++) {
      qa_save_reader statement = item(&statements, s);
      words[s].opcode = field_integer_min(&statement, "opcode", 0);
      words[s].a = field_integer_min(&statement, "a", 0);
      words[s].b = field_integer_min(&statement, "b", 0);
      words[s].c = field_integer_min(&statement, "c", 0);
      qa_save_reader_destroy(&statement);
    }
    qa_save_reader_destroy(&statements);
    entries[i].statements = words;
    entries[i].statement_count = statement_count;
    qa_save_reader operation = field(&region, "operation");
    static const char *const OPS[4] = {"decision", "admission", "grant", "consume"};
    entries[i].operation_kind = field_choice(&operation, "kind", OPS, 4);
    if (strcmp(entries[i].operation_kind, "decision") == 0) {
      entries[i].operation_word = field_integer_min(&operation, "word", 0);
      qa_save_reader accepted = field(&operation, "accepted");
      entries[i].operation_accepted = qa_save_reader_finite(&accepted);
      qa_save_reader_destroy(&accepted);
    }
    qa_save_reader_destroy(&operation);
    qa_save_reader_destroy(&region);
  }
  qa_save_reader_destroy(&regions);
  declaration.regions = entries;
  declaration.region_count = region_count;
  return declaration;
}
static void destroy_item(const qa_qc_pickup_item *item) {
  free((void *)item->resource);
  free((void *)item->count);
}
static noreturn void fail_caller(const qa_q1qc_program *program, const char *reason) {
  qa_q1qc_program_error(program, "Invalid declared pickup caller: %s", reason);
}
static bool is_integer(double value) { return value == (double)(int64_t)value; }
static void check_global_word(const qa_q1qc_program *program, double word) {
  if (!is_integer(word) || word < 0 || word * 4 >= (double)program->initial_globals_length)
    fail_caller(program, "global word is outside source memory");
}
static void check_field_type(const qa_q1qc_program *program, const char *name, const char *type) {
  const qa_q1qc_definition *definition = qa_q1qc_field_by_name(program, name);
  if (definition == NULL || strcmp(definition->type, type) != 0) {
    char reason[256];
    (void)snprintf(reason, sizeof reason, "source field %s is not %s", name, type);
    fail_caller(program, reason);
  }
}
static qa_qc_pickup_scalar validated_scalar(const qa_q1qc_program *program,
                                            const qa_qc_pickup_scalar *input) {
  if (!input->is_global) check_field_type(program, input->name, "float");
  else check_global_word(program, input->word);
  return *input;
}
static qa_q1qc_pickup_descriptor describe_item(const qa_q1qc_program *program,
                                               const qa_qc_pickup_item *entry) {
  qa_q1qc_pickup_descriptor descriptor = {0};
  descriptor.item = entry->item;
  if (entry->resource != NULL) {
    descriptor.has_resource = true;
    descriptor.resource = *entry->resource;
  }
  if (entry->count != NULL) {
    descriptor.has_count = true;
    descriptor.count = validated_scalar(program, entry->count);
  }
  return descriptor;
}
qa_q1qc_pickup_stage *qa_q1qc_declared_pickup_stages(
    const qa_q1qc_program *program,
    const qa_qc_pickup_caller_declaration *declarations, size_t declaration_count,
    size_t *out_count) {
  *out_count = 0;
  qa_q1qc_pickup_stage *stages = xmalloc(declaration_count * sizeof *stages);
  memset(stages, 0, declaration_count * sizeof *stages);
  int64_t *callers = xmalloc(declaration_count * sizeof *callers);
  size_t caller_count = 0;
  for (size_t d = 0; d < declaration_count; d++) {
    const qa_qc_pickup_caller_declaration *declaration = &declarations[d];
    const qa_q1qc_function *fn = qa_q1qc_function_named(program, declaration->function);
    bool duplicate = false;
    for (size_t i = 0; i < caller_count; i++) if (callers[i] == fn->index) duplicate = true;
    if (fn->first_statement <= 0 || fn->named_builtin || fn->parameter_size_count != 0 || duplicate)
      fail_caller(program, "duplicate or non-touch source function");
    callers[caller_count++] = fn->index;
    int64_t end = qa_q1qc_function_end(program, fn);
    const qa_qc_pickup_descriptor *descriptor = &declaration->descriptor;
    bool is_constant = strcmp(descriptor->kind, "constant") == 0;
    if (!is_constant) {
      check_field_type(program, descriptor->field, descriptor->kind);
      bool is_string = strcmp(descriptor->kind, "string") == 0;
      size_t count = is_string ? descriptor->string_value_count : descriptor->float_value_count;
      if (count == 0) fail_caller(program, "empty or ambiguous source item descriptors");
      for (size_t i = 0; i < count; i++)
        for (size_t j = i + 1; j < count; j++) {
          bool same = is_string
                          ? strcmp(descriptor->string_values[i].value,
                                   descriptor->string_values[j].value) == 0
                          : descriptor->float_values[i].value == descriptor->float_values[j].value;
          if (same) fail_caller(program, "empty or ambiguous source item descriptors");
        }
      if (!is_string)
        for (size_t i = 0; i < count; i++)
          if ((double)(float)descriptor->float_values[i].value != descriptor->float_values[i].value)
            fail_caller(program, "item discriminator is not an exact source float");
    }
    /* Stable sort of region indices by entry (TS [...regions].sort). */
    size_t *order = xmalloc(declaration->region_count * sizeof *order);
    for (size_t i = 0; i < declaration->region_count; i++) order[i] = i;
    for (size_t i = 1; i < declaration->region_count; i++) {
      size_t key = order[i];
      size_t j = i;
      while (j > 0 && declaration->regions[order[j - 1]].entry > declaration->regions[key].entry) {
        order[j] = order[j - 1];
        j--;
      }
      order[j] = key;
    }
    bool has_boundary = false;
    for (size_t i = 0; i < declaration->region_count; i++) {
      const char *op = declaration->regions[i].operation_kind;
      if (strcmp(op, "decision") == 0 || strcmp(op, "admission") == 0) has_boundary = true;
    }
    if (!has_boundary) fail_caller(program, "missing recipient admission boundary");
    int64_t previous_end = fn->first_statement;
    for (size_t r = 0; r < declaration->region_count; r++) {
      const qa_qc_pickup_region *region = &declaration->regions[order[r]];
      if (!is_integer(region->entry) || !is_integer(region->exit) ||
          (int64_t)region->entry < previous_end || (int64_t)region->exit <= (int64_t)region->entry ||
          (int64_t)region->exit >= end)
        fail_caller(program, "overlapping or out-of-function regions");
      previous_end = (int64_t)region->exit;
      if ((int64_t)region->statement_count != (int64_t)region->exit - (int64_t)region->entry + 1)
        fail_caller(program, "instruction proof must include the region and its join");
      for (size_t offset = 0; offset < region->statement_count; offset++) {
        const qa_qc_statement *expected = &region->statements[offset];
        int64_t index = (int64_t)region->entry + (int64_t)offset;
        const qa_q1qc_statement *actual =
            index >= 0 && (size_t)index < program->statement_count ? &program->statements[index] : NULL;
        if (actual == NULL || (double)actual->opcode != expected->opcode ||
            (double)actual->a != expected->a || (double)actual->b != expected->b ||
            (double)actual->c != expected->c)
          fail_caller(program, "instruction proof differs from original artifact");
      }
      if (strcmp(region->operation_kind, "decision") == 0) {
        check_global_word(program, region->operation_word);
        const qa_q1qc_statement *join = &program->statements[(size_t)region->exit];
        if ((double)join->a != region->operation_word ||
            (join->opcode != QA_Q1QC_OP_IF && join->opcode != QA_Q1QC_OP_IF_NOT))
          fail_caller(program, "decision join does not consume the declared source predicate");
        if ((double)(float)region->operation_accepted != region->operation_accepted)
          fail_caller(program, "accepted decision is not an exact source float");
      }
      for (int64_t at = fn->first_statement; at < end; at++) {
        if (at < 0 || (size_t)at >= program->statement_count)
          fail_caller(program, "missing source instruction");
        const qa_q1qc_statement *statement = &program->statements[at];
        bool has_target = statement->opcode == QA_Q1QC_OP_GOTO ||
                          statement->opcode == QA_Q1QC_OP_IF || statement->opcode == QA_Q1QC_OP_IF_NOT;
        if (!has_target) continue;
        int64_t target = at + qa_q1qc_signed_branch(statement->opcode == QA_Q1QC_OP_GOTO
                                                        ? statement->a : statement->b);
        if ((at < (int64_t)region->entry || at >= (int64_t)region->exit) &&
            target > (int64_t)region->entry && target < (int64_t)region->exit)
          fail_caller(program, "source branch enters the middle of a declared region");
      }
    }
    qa_q1qc_pickup_stage *stage = &stages[d];
    stage->function_index = fn->index;
    stage->descriptor_kind = descriptor->kind;
    if (is_constant) {
      stage->values = xmalloc(sizeof *stage->values);
      stage->values[0] = describe_item(program, &descriptor->constant_item);
      stage->values[0].value_number = 0;
      stage->value_count = 1;
    } else {
      stage->descriptor_field = descriptor->field;
      bool is_string = strcmp(descriptor->kind, "string") == 0;
      size_t count = is_string ? descriptor->string_value_count : descriptor->float_value_count;
      stage->values = xmalloc(count * sizeof *stage->values);
      for (size_t i = 0; i < count; i++) {
        if (is_string) {
          stage->values[i] = describe_item(program, &descriptor->string_values[i].item);
          stage->values[i].value_is_string = true;
          stage->values[i].value_string = descriptor->string_values[i].value;
        } else {
          stage->values[i] = describe_item(program, &descriptor->float_values[i].item);
          stage->values[i].value_number = descriptor->float_values[i].value;
        }
      }
      stage->value_count = count;
    }
    if (declaration->dropped != NULL) {
      stage->has_dropped = true;
      stage->dropped = validated_scalar(program, declaration->dropped);
    }
    stage->regions = xmalloc(declaration->region_count * sizeof *stage->regions);
    for (size_t r = 0; r < declaration->region_count; r++) {
      const qa_qc_pickup_region *region = &declaration->regions[order[r]];
      qa_q1qc_pickup_region built = {{fn->index, (int64_t)region->entry, (int64_t)region->exit,
                                      true, false, 0, NULL},
                                     false, {region->operation_kind, 0, 0, NULL, NULL}};
      if (strcmp(region->operation_kind, "decision") == 0) {
        built.operation.word = (int64_t)region->operation_word;
        built.operation.accepted = region->operation_accepted;
      }
      stage->regions[r] = built;
    }
    stage->region_count = declaration->region_count;
    free(order);
  }
  free(callers);
  *out_count = declaration_count;
  return stages;
}
void qa_q1qc_pickup_caller_declaration_destroy(qa_qc_pickup_caller_declaration *declaration) {
  destroy_item(&declaration->descriptor.constant_item);
  for (size_t i = 0; i < declaration->descriptor.string_value_count; i++)
    destroy_item(&declaration->descriptor.string_values[i].item);
  free((void *)declaration->descriptor.string_values);
  for (size_t i = 0; i < declaration->descriptor.float_value_count; i++)
    destroy_item(&declaration->descriptor.float_values[i].item);
  free((void *)declaration->descriptor.float_values);
  free((void *)declaration->dropped);
  for (size_t i = 0; i < declaration->region_count; i++)
    free((void *)declaration->regions[i].statements);
  free((void *)declaration->regions);
  memset(declaration, 0, sizeof *declaration);
}
