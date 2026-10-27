/*
 * Port of src/persistence/recipe.ts: boundary readers for content
 * identities, provider references, resolved resources, character
 * and equipment selections, and the executable recipe.
 * Ownership: returned structs borrow strings/bytes from the
 * checkpoint tree, except identity strings built by qa_create_*
 * (recipe/mount/mount-plan/resource ids) which are malloc'd and
 * freed by the caller, and the mount pointed to by a resource
 * provenance, which is a single malloc'd allocation the caller
 * frees. List fields are malloc'd arrays the caller frees.
 *
 * BLOCKED: qa_read_recipe is not yet ported. It needs
 * readGameplayMod (src/persistence/mods.ts): persistence/mods.h
 * exists but declares no qa_read_gameplay_mod yet, although its
 * blocker, qa_read_gameplay_mod_declaration from
 * src/content/mods/declaration.ts, has landed
 * (include/qa/content/mods/declaration.h). The weapon-behavior
 * reader's readSavedNativeWeaponDeclaration now exists as
 * qa_read_saved_native_weapon_declaration (native_weapon.h), which
 * takes an injected qa_native_weapon_profiles table a future
 * qa_read_recipe signature must thread through. The exported
 * readers below have no such dependency and are complete.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_RECIPE_H
#define QA_PERSISTENCE_RECIPE_H

#include "qa/contracts/content.h"
#include "qa/persistence/value.h"

qa_content_id qa_read_content_id(const qa_save_reader *reader);
qa_provider_reference qa_read_provider(const qa_save_reader *reader);
qa_resolved_resource_reference qa_read_resource(const qa_save_reader *reader);
qa_character_selection qa_read_character(const qa_save_reader *reader);
qa_equipment_selection qa_read_equipment(const qa_save_reader *reader);

#endif
