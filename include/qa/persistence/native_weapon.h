/*
 * Port of src/persistence/native-weapon.ts: only the former built-in
 * profile can supply declaration identity omitted by old saves.
 * The three compat functions this reader calls --
 * readNativeWeaponDeclaration (TS owner:
 * src/compat/q2/rerelease/native-weapon-declaration.ts) and
 * builtInRereleaseWeaponDeclaration / rereleaseWeaponDefinition
 * (TS owner: src/compat/q2/rerelease/weapon-behavior-profile.ts) --
 * have no C port yet, so they are injected through the profiles
 * table below (closures become fn + ctx). When the compat port
 * lands, app wiring passes its functions here. Returned
 * declarations borrow strings/lists from wherever the injected
 * functions built them. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_NATIVE_WEAPON_H
#define QA_PERSISTENCE_NATIVE_WEAPON_H

#include <stdbool.h>

#include "qa/contracts/native_weapon_behavior.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/persistence/value.h"

/** TS compat profile lookup surface (see header comment). */
typedef struct qa_native_weapon_profiles {
  void *ctx;
  /* TS builtInRereleaseWeaponDeclaration: false when it returns null. */
  bool (*built_in_declaration)(void *ctx, qa_module_identity module,
                               qa_native_weapon_behavior_declaration *out);
  /* TS readNativeWeaponDeclaration: aborts (SaveReader fail) on a
   * malformed declaration, as the TS reader throws. */
  qa_native_weapon_behavior_declaration (*read_declaration)(
      void *ctx, const qa_save_value *value, qa_module_identity module);
  /* TS rereleaseWeaponDefinition: false when it returns null. */
  bool (*weapon_definition)(void *ctx, qa_module_identity module,
                            const qa_native_weapon_behavior_declaration *declaration,
                            qa_weapon_behavior_definition *out);
} qa_native_weapon_profiles;

qa_native_weapon_behavior_declaration qa_read_saved_native_weapon_declaration(
    const qa_save_reader *reader, const qa_native_weapon_profiles *profiles,
    qa_weapon_behavior_definition definition);

#endif
