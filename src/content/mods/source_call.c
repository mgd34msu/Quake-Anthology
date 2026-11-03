/* Port of src/content/mods/source-call.ts. GPL-2.0-or-later. */
#include "qa/content/mods/source_call.h"
#include "qa/content/mods/declaration.h"

#include <stdlib.h>
#include <string.h>

#include "qa/persistence/shared.h"

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
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

qa_mod_callback_value qa_read_mod_source_value(const qa_save_reader *reader) {
  static const char *const kinds[] = {"input", "float", "string", "vector"};
  static const char *const inputs[] = {
      "self", "other", "activator", "attacker", "inflictor", "amount",
      "damage-flags", "regular-protection-scale", "knockback", "point",
      "direction", "normal", "item", "time", "elapsed", "result",
      "view-angles", "attack", "jump", "impulse", "forward-move",
      "side-move", "up-move", "pickup-count", "pickup-has-count",
      "pickup-dropped"};
  qa_mod_callback_value result;
  memset(&result, 0, sizeof result);
  result.kind = field_choice(reader, "kind", kinds, 4);
  if (strcmp(result.kind, "input") == 0) {
    result.name = field_choice(reader, "name", inputs, sizeof inputs / sizeof inputs[0]);
  } else if (strcmp(result.kind, "float") == 0) {
    qa_save_reader f = field(reader, "value");
    result.number_value = qa_save_reader_number(&f);
    qa_save_reader_destroy(&f);
  } else if (strcmp(result.kind, "string") == 0) {
    result.string_value = field_string(reader, "value");
  } else {
    qa_save_reader f = field(reader, "value");
    result.vector_value = qa_read_vector(&f);
    qa_save_reader_destroy(&f);
  }
  return result;
}

qa_mod_source_call qa_read_mod_source_call(const qa_save_reader *reader) {
  qa_mod_source_call call;
  memset(&call, 0, sizeof call);
  call.function = field_string(reader, "function");
  {
    qa_save_reader list = field(reader, "arguments");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_callback_value *values =
        count > 0 ? (qa_mod_callback_value *)qa_mod_declaration_malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&list, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      values[i] = qa_read_mod_source_value(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    call.arguments = values;
    call.argument_count = count;
  }
  {
    qa_save_reader list = field(reader, "globals");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_named_value *values =
        count > 0 ? (qa_mod_named_value *)qa_mod_declaration_malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&list, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      values[i].name = field_string(&entry, "name");
      qa_save_reader f = field(&entry, "value");
      values[i].value = qa_read_mod_source_value(&f);
      qa_save_reader_destroy(&f);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    call.globals = values;
    call.global_count = count;
  }
  return call;
}
