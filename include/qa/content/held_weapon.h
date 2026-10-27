/*
 * Port of src/content/held-weapon.ts: readHeldWeaponDeclaration and
 * readHeldWeaponFile. Results use the contracts
 * qa_held_weapon_declaration; strings and the optional part arrays
 * are malloc'd copies freed by qa_content_held_weapon_free (the
 * reader tree is not retained). False only on OOM; validation aborts
 * with the exact TS SaveReader/Error messages. The file form parses
 * JSON via qa_parse_json_value (no fatal UTF-8 check).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_HELD_WEAPON_H
#define QA_CONTENT_HELD_WEAPON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/held_weapon.h"
#include "qa/persistence/value.h"

bool qa_read_held_weapon_declaration(const qa_save_reader *reader,
                                     qa_held_weapon_declaration *out);
bool qa_read_held_weapon_file(const uint8_t *bytes, size_t length,
                              qa_held_weapon_declaration *out);
void qa_content_held_weapon_free(qa_held_weapon_declaration *value);

#endif
