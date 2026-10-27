/*
 * Port of src/contracts/held-weapon.ts (types only).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_HELD_WEAPON_H
#define QA_CONTRACTS_HELD_WEAPON_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

typedef struct qa_held_weapon_part {
  qa_string_view_list digests;
  qa_double_view vertices;
} qa_held_weapon_part;

typedef struct qa_held_weapon_model {
  bool has_digest;
  qa_content_digest digest;
  const char *path;
  double reference_frame;
  qa_model_transform grip;
  const char *fallback;             /* optional */
  const qa_held_weapon_part *part;  /* optional */
} qa_held_weapon_model;

/* TS HeldWeaponDeclaration: {kind:"none"} | {kind:"model", model}. */
typedef struct qa_held_weapon_declaration {
  bool is_model;
  qa_held_weapon_model model;
} qa_held_weapon_declaration;

#endif
