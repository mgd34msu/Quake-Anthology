/*
 * Port of src/compat/qvm/legacy-presentation.ts: base-game enum
 * translation for the 1.16n/1.17 legacy ABI. TS number[] results that
 * return the input array unchanged return the input span; the
 * translated persistent array writes 16 slots to caller storage.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_LEGACY_PRESENTATION_H
#define QA_COMPAT_QVM_LEGACY_PRESENTATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/execution.h"
#include "qa/contracts/math.h"

int32_t qa_qvm_legacy_event(int32_t value, qa_qvm_abi_profile profile, bool reverse);
int32_t qa_qvm_legacy_entity_type(int32_t value, qa_qvm_abi_profile profile, bool reverse);
/* Writes 16 slots to out; missing input slots read as 0 (TS `?? 0`). */
void qa_qvm_legacy_persistent(const double *values, size_t count, qa_qvm_abi_profile profile, double out[16]);
int32_t qa_qvm_legacy_configstring(int32_t index, qa_qvm_abi_profile profile);
int32_t qa_qvm_legacy_powerup_bits(int32_t bits, qa_qvm_abi_profile profile);
/* Validates and returns the input span unchanged. */
qa_double_view qa_qvm_legacy_powerups(const double *values, size_t count, qa_qvm_abi_profile profile);
/* TS lenient forms (remote servers choose these values): unrepresentable
 * events and entity types become 0, powerup bits/slots past the legacy set drop. */
int32_t qa_qvm_legacy_event_ex(int32_t value, qa_qvm_abi_profile profile, bool reverse, bool lenient);
int32_t qa_qvm_legacy_entity_type_ex(int32_t value, qa_qvm_abi_profile profile, bool reverse, bool lenient);
int32_t qa_qvm_legacy_powerup_bits_ex(int32_t bits, qa_qvm_abi_profile profile, bool lenient);
void qa_qvm_legacy_powerups_lenient(const double *values, size_t count, qa_qvm_abi_profile profile, double *out);

#endif
