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
 * qa_read_recipe's weapon-behavior branch calls
 * qa_read_saved_native_weapon_declaration (native_weapon.h) with a
 * profiles table recipe.c builds from the landed compat readers
 * (qa/compat/q2/rerelease), mirroring TS recipe.ts's direct import
 * of native-weapon.ts. List/pointer fields follow the ownership
 * note at the top of this header; recipe.mods elements are single
 * malloc'd qa_resolved_gameplay_mod allocations (the contract type
 * is an array of pointers), as are each weapon behavior's
 * definition, QVM layout, and native declaration.
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
qa_executable_recipe qa_read_recipe(const qa_save_reader *reader);

#endif
