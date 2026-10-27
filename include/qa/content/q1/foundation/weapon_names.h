/* Port of src/content/q1/foundation/weapon-names.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_WEAPON_NAMES_H
#define QA_CONTENT_Q1_FOUNDATION_WEAPON_NAMES_H

#include <stddef.h>

#include "qa/content/q1/foundation/types.h"

/* Writes into out for generated names; returns out or a static string. */
const char *qa_q1_weapon_display_name(qa_q1_weapon weapon, char *out, size_t out_size);

#endif
