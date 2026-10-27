/* Port of src/compat/q2/native-primary-validation.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_PRIMARY_VALIDATION_H
#define QA_COMPAT_Q2_NATIVE_PRIMARY_VALIDATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary.h"
#include "qa/foundation/support.h"

/* TS PeFile (guest/pe/format.ts) mirrored minimal shape; guest PE
 * header conflicts with contracts headers in one TU. */
typedef struct qa_compat_q2_pe_section { double rva, mapped_size; const char *permissions; } qa_compat_q2_pe_section;
typedef struct qa_compat_q2_pe_file { double image_size; int64_t pointer_bytes; const qa_compat_q2_pe_section *sections; size_t section_count; } qa_compat_q2_pe_file;
void qa_compat_q2_validate_native_primary(const qa_compat_q2_native_primary_profile *profile, qa_compat_q2_pe_file pe, double entity_bytes);
#endif
