/* Port of src/content/mods/item-actions.ts. GPL-2.0-or-later. */
#include "qa/content/mods/item_actions.h"
#include "qa/content/mods/declaration.h"

#include <stdlib.h>

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}

qa_source_item_action_calls qa_read_item_actions(
    const qa_save_reader *reader,
    void (*read_call)(void *ctx, const qa_save_reader *reader, void *out),
    void *read_ctx, size_t call_elem_size) {
  qa_save_reader use = field(reader, "use"), drop = field(reader, "drop");
  bool has_use = !qa_save_reader_is_undefined(&use);
  bool has_drop = !qa_save_reader_is_undefined(&drop);
  if (!has_use && !has_drop) {
    qa_save_reader_destroy(&use);
    qa_save_reader_destroy(&drop);
    qa_save_reader_fail(reader, "Item actions require an original use or drop call");
  }
  qa_source_item_action_calls calls = {NULL, NULL};
  if (has_use) {
    void *storage = qa_mod_declaration_malloc(call_elem_size);
    if (storage == NULL) qa_save_reader_fail(&use, "out of memory");
    read_call(read_ctx, &use, storage);
    calls.use_call = storage;
  }
  if (has_drop) {
    void *storage = qa_mod_declaration_malloc(call_elem_size);
    if (storage == NULL) qa_save_reader_fail(&drop, "out of memory");
    read_call(read_ctx, &drop, storage);
    calls.drop_call = storage;
  }
  qa_save_reader_destroy(&use);
  qa_save_reader_destroy(&drop);
  return calls;
}
