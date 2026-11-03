/* Port of src/content/mods/client-input.ts. GPL-2.0-or-later. */
#include "qa/content/mods/client_input.h"
#include "qa/content/mods/declaration.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

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
static const char *field_choice(const qa_save_reader *reader, const char *name,
                                const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static void *read_list(const qa_save_reader *reader, const char *name,
                       void (*read)(void *, const qa_save_reader *, void *),
                       void *ctx, size_t elem_size, size_t *out_count) {
  qa_save_reader list = field(reader, name);
  size_t count = qa_save_reader_list_count(&list);
  char *storage = count > 0 ? (char *)qa_mod_declaration_malloc(count * elem_size) : NULL;
  if (count > 0 && storage == NULL) qa_save_reader_fail(&list, "out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    read(ctx, &entry, storage + i * elem_size);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return storage;
}

qa_mod_client_input_binding *qa_read_mod_client_input(
    const qa_save_reader *reader,
    void (*read_call)(void *ctx, const qa_save_reader *reader, void *out),
    void *call_ctx, size_t call_elem_size,
    void (*read_output)(void *ctx, const qa_save_reader *reader, void *out),
    void *output_ctx, size_t output_elem_size, size_t *out_count) {
  static const char *const scopes[] = {"client-command", "movement-slice"};
  static const char *const phases[] = {"before", "after"};
  size_t count = qa_save_reader_list_count(reader);
  qa_mod_client_input_binding *bindings = count > 0
      ? (qa_mod_client_input_binding *)qa_mod_declaration_calloc(count, sizeof *bindings) : NULL;
  if (count > 0 && bindings == NULL) return NULL;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(reader, i);
    bindings[i].scope = field_choice(&entry, "scope", scopes, 2);
    bindings[i].phase = field_choice(&entry, "phase", phases, 2);
    bindings[i].calls = read_list(&entry, "calls", read_call, call_ctx,
                                  call_elem_size, &bindings[i].call_count);
    bindings[i].call_elem_size = call_elem_size;
    qa_save_reader outputs = field(&entry, "outputs");
    bool has_outputs = !qa_save_reader_is_undefined(&outputs);
    qa_save_reader_destroy(&outputs);
    if (has_outputs) {
      if (strcmp(bindings[i].phase, "before") != 0 || read_output == NULL) {
        qa_save_reader_destroy(&entry);
        /* TS throws a plain Error here. */
        qa_range_error("Input outputs require a declared before source adapter");
      }
      bindings[i].outputs = read_list(&entry, "outputs", read_output, output_ctx,
                                      output_elem_size, &bindings[i].output_count);
      bindings[i].output_elem_size = output_elem_size;
    }
    qa_save_reader_destroy(&entry);
  }
  *out_count = count;
  return bindings;
}
