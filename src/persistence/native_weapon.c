/* Port of src/persistence/native-weapon.ts. GPL-2.0-or-later. */
#include "qa/persistence/native_weapon.h"

#include <string.h>

qa_native_weapon_behavior_declaration qa_read_saved_native_weapon_declaration(
    const qa_save_reader *reader, const qa_native_weapon_profiles *profiles,
    qa_weapon_behavior_definition definition) {
  qa_native_weapon_behavior_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  if (qa_save_reader_is_undefined(reader)) {
    if (!profiles->built_in_declaration(profiles->ctx, definition.module,
                                        &declaration))
      qa_save_reader_fail(
          reader,
          "native weapon save has no retained declaration or matching "
          "legacy profile");
  } else {
    declaration =
        profiles->read_declaration(profiles->ctx, reader->value, definition.module);
  }
  qa_weapon_behavior_definition expected;
  memset(&expected, 0, sizeof expected);
  if (!profiles->weapon_definition(profiles->ctx, definition.module,
                                   &declaration, &expected) ||
      !qa_same_weapon_behavior(definition, expected))
    qa_save_reader_fail(
        reader, "native weapon declaration differs from saved behavior identity");
  return declaration;
}
