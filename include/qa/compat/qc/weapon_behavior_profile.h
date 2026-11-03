/* Port of src/compat/qc/weapon-behavior-profile.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QC_WEAPON_BEHAVIOR_PROFILE_H
#define QA_COMPAT_QC_WEAPON_BEHAVIOR_PROFILE_H
#include <stddef.h>
#include "qa/compat/qc/program.h"
typedef struct qa_compat_qc_weapon_field_spec { const char *name, *type; } qa_compat_qc_weapon_field_spec;
extern const qa_compat_qc_weapon_field_spec qa_compat_qc_weapon_behavior_fields[12];
extern const qa_compat_qc_weapon_field_spec qa_compat_qc_weapon_behavior_globals[2];
/* Malloc'd error string (caller frees) or NULL when capable. */
char *qa_compat_qc_weapon_behavior_capability_error(const qa_compat_qc_program *program);
#endif
