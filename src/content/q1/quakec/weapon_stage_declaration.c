/* Port of src/content/q1/quakec/weapon-stage-declaration.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/quakec/weapon_stage_declaration.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/mods/source_call.h"

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
static double field_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer(&f);
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

qa_qc_weapon_stage_declaration qa_q1qc_read_weapon_stage_declaration(const qa_save_reader *reader) {
  qa_qc_weapon_stage_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  declaration.dispatcher = field_string(reader, "dispatcher");
  {
    qa_save_reader list = field(reader, "continuations");
    size_t count = qa_save_reader_list_count(&list);
    const char **values = xmalloc_at(&list, count * sizeof *values);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      values[i] = qa_save_reader_string(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    declaration.continuations.values = values;
    declaration.continuations.count = count;
  }
  {
    qa_save_reader list = field(reader, "repeats");
    size_t count = qa_save_reader_list_count(&list);
    qa_qc_weapon_stage_repeat *repeats = xmalloc_at(&list, count * sizeof *repeats);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      qa_qc_weapon_stage_repeat repeat;
      memset(&repeat, 0, sizeof repeat);
      repeat.function = field_string(&entry, "function");
      repeat.entry = field_integer_min(&entry, "entry", 0);
      repeat.exit = field_integer_min(&entry, "exit", 0);
      qa_save_reader result = field(&entry, "result");
      repeat.result_word = field_integer_min(&result, "word", 28);
      qa_save_reader value = field(&result, "value");
      static const double CHOICES[2] = {0, 1};
      repeat.result_value = (int)qa_save_reader_choice_number(&value, CHOICES, 2);
      qa_save_reader_destroy(&value);
      qa_save_reader_destroy(&result);
      qa_save_reader statements = field(&entry, "statements");
      size_t statement_count = qa_save_reader_list_count(&statements);
      qa_qc_statement *words = xmalloc_at(&statements, statement_count * sizeof *words);
      for (size_t s = 0; s < statement_count; s++) {
        qa_save_reader statement = item(&statements, s);
        words[s].opcode = field_integer_min(&statement, "opcode", 0);
        words[s].a = field_integer(&statement, "a");
        words[s].b = field_integer(&statement, "b");
        words[s].c = field_integer(&statement, "c");
        qa_save_reader_destroy(&statement);
      }
      qa_save_reader_destroy(&statements);
      repeat.statements = words;
      repeat.statement_count = statement_count;
      repeats[i] = repeat;
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    declaration.repeats = repeats;
    declaration.repeat_count = count;
  }
  return declaration;
}
void qa_q1qc_weapon_stage_declaration_destroy(qa_qc_weapon_stage_declaration *declaration) {
  free((void *)declaration->continuations.values);
  for (size_t i = 0; i < declaration->repeat_count; i++)
    free((void *)declaration->repeats[i].statements);
  free((void *)declaration->repeats);
  memset(declaration, 0, sizeof *declaration);
}
static void destroy_call(qa_mod_source_call *call) {
  free((void *)call->arguments);
  free((void *)call->globals);
  memset(call, 0, sizeof *call);
}
static qa_qc_named_call read_named_call(const qa_save_reader *reader) {
  qa_qc_named_call result;
  memset(&result, 0, sizeof result);
  /* TS: typeof value === "string" ? value.string() : readModSourceCall(value). */
  if (reader->value != NULL && reader->value->kind == QA_SAVE_STRING) {
    result.is_call = false;
    result.function = qa_save_reader_string(reader);
  } else {
    result.is_call = true;
    result.call = qa_read_mod_source_call(reader);
  }
  return result;
}
qa_qc_primary_weapon_stage_declaration qa_q1qc_read_primary_weapon_stage(const qa_save_reader *reader) {
  qa_qc_primary_weapon_stage_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  qa_save_reader client = field(reader, "client");
  qa_save_reader objectives = field(&client, "objectives");
  qa_save_reader kind_reader = field(&objectives, "kind");
  static const char *const KINDS[2] = {"none", "call"};
  const char *kind = qa_save_reader_choice_string(&kind_reader, KINDS, 2);
  qa_save_reader_destroy(&kind_reader);
  qa_save_reader call_field = field(&objectives, "call");
  qa_save_reader function_field = field(&objectives, "function");
  bool has_call = !qa_save_reader_is_undefined(&call_field);
  bool has_function = !qa_save_reader_is_undefined(&function_field);
  if (strcmp(kind, "call") == 0 && has_call && has_function)
    qa_save_reader_fail(&objectives, "client objectives must declare one original call");
  declaration.stage = qa_q1qc_read_weapon_stage_declaration(reader);
  qa_save_reader spawn = field(&client, "spawn");
  declaration.client_spawn = read_named_call(&spawn);
  qa_save_reader_destroy(&spawn);
  qa_save_reader select_spawn = field(&client, "selectSpawn");
  declaration.client_select_spawn = read_named_call(&select_spawn);
  qa_save_reader_destroy(&select_spawn);
  declaration.client_objectives.kind = kind;
  if (strcmp(kind, "call") == 0) {
    if (!has_call) {
      declaration.client_objectives.function = qa_save_reader_string(&function_field);
    } else {
      declaration.client_objectives.call = qa_read_mod_source_call(&call_field);
    }
  }
  qa_save_reader_destroy(&function_field);
  qa_save_reader_destroy(&call_field);
  qa_save_reader_destroy(&objectives);
  qa_save_reader_destroy(&client);
  return declaration;
}
void qa_q1qc_primary_weapon_stage_destroy(qa_qc_primary_weapon_stage_declaration *declaration) {
  qa_q1qc_weapon_stage_declaration_destroy(&declaration->stage);
  destroy_call(&declaration->client_spawn.call);
  destroy_call(&declaration->client_select_spawn.call);
  destroy_call(&declaration->client_objectives.call);
  memset(declaration, 0, sizeof *declaration);
}
