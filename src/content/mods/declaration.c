/* Port of src/content/mods/declaration.ts. GPL-2.0-or-later. */
#include "qa/content/mods/declaration.h"

#include <string.h>

#include "qa/content/mods/callbacks.h"
#include "qa/content/mods/native_callbacks.h"
#include "qa/content/mods/qvm_callbacks.h"

qa_mod_declaration qa_read_gameplay_mod_declaration(const qa_save_reader *reader) {
  static const char *const runtimes[] = {"quakec", "qvm", "native"};
  qa_save_reader runtime;
  if (!qa_save_reader_field(reader, "runtime", &runtime))
    qa_save_reader_fail(reader, "out of memory");
  const char *kind = qa_save_reader_choice_string(&runtime, runtimes, 3);
  qa_save_reader_destroy(&runtime);
  qa_mod_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  if (strcmp(kind, "quakec") == 0) {
    declaration.runtime = QA_MOD_RUNTIME_QUAKEC;
    declaration.as.quakec = qa_read_quakec_mod_declaration(reader);
  } else if (strcmp(kind, "qvm") == 0) {
    declaration.runtime = QA_MOD_RUNTIME_QVM;
    declaration.as.qvm = qa_read_qvm_mod_declaration(reader);
  } else {
    declaration.runtime = QA_MOD_RUNTIME_NATIVE;
    declaration.as.native = qa_read_native_mod_declaration(reader);
  }
  return declaration;
}

qa_mod_declaration qa_parse_gameplay_mod_declaration(const uint8_t *bytes,
                                                     size_t length) {
  /* TS: JSON.parse(new TextDecoder("utf-8", {fatal:true}).decode(bytes));
   * the parsed tree leaks because declarations borrow from it. */
  qa_mod_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return declaration;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, NULL)) return declaration;
  declaration = qa_read_gameplay_mod_declaration(&reader);
  qa_save_reader_destroy(&reader);
  return declaration;
}
