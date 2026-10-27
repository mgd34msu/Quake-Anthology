/* Port of src/compat/q2/rerelease/native-weapon-declaration.ts.
 * GPL-2.0-or-later. PeFile uses the landed qa_compat_q2_pe_file
 * mirror (native-primary-validation precedent); the image ABI kind
 * string is passed alongside. */
#ifndef QA_COMPAT_Q2_RERELEASE_NATIVE_WEAPON_DECLARATION_H
#define QA_COMPAT_Q2_RERELEASE_NATIVE_WEAPON_DECLARATION_H
#include <stdbool.h>
#include "qa/compat/q2/native_primary_validation.h"
#include "qa/contracts/native_weapon_behavior.h"
#include "qa/persistence/value.h"

/* False only on OOM. out's strings and lists are malloc'd; release
 * with qa_compat_q2_native_weapon_declaration_free. module NULL is
 * the TS undefined. */
bool qa_compat_q2_read_native_weapon_declaration(const qa_save_value *value, const qa_module_identity *module,
                                                 qa_native_weapon_behavior_declaration *out);
void qa_compat_q2_native_weapon_declaration_free(qa_native_weapon_behavior_declaration *d);
/* Malloc'd JSON (TS JSON.stringify of the re-read declaration). */
char *qa_compat_q2_serialize_native_weapon_declaration(const qa_native_weapon_behavior_declaration *value);
bool qa_compat_q2_same_native_weapon_declaration(const qa_native_weapon_behavior_declaration *left,
                                                 const qa_native_weapon_behavior_declaration *right);
void qa_compat_q2_validate_native_weapon_image(const qa_native_weapon_behavior_declaration *profile,
                                               const char *image_abi_kind, qa_compat_q2_pe_file image);
#endif
