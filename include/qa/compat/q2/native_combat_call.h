/* Port of src/compat/q2/native-combat-call.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_COMBAT_CALL_H
#define QA_COMPAT_Q2_NATIVE_COMBAT_CALL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary_reader.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

typedef enum qa_compat_q2_native_combat_field { QA_Q2CF_TARGET,QA_Q2CF_INFLICTOR,QA_Q2CF_ATTACKER,QA_Q2CF_DIRECTION,QA_Q2CF_POINT,QA_Q2CF_NORMAL,QA_Q2CF_AMOUNT,QA_Q2CF_KNOCKBACK,QA_Q2CF_FLAGS,QA_Q2CF_CAUSE,QA_Q2CF_SPARKS,QA_Q2CF_KICK } qa_compat_q2_native_combat_field;
typedef enum qa_compat_q2_native_combat_operation { QA_Q2CO_DAMAGE,QA_Q2CO_REGULAR_ARMOR,QA_Q2CO_POWER_ARMOR,QA_Q2CO_PAIN,QA_Q2CO_DEATH,QA_Q2CO_DEFERRED } qa_compat_q2_native_combat_operation;
typedef enum qa_compat_q2_native_combat_argument_kind { QA_Q2CA_FIELD,QA_Q2CA_VALUE,QA_Q2CA_ADDRESS } qa_compat_q2_native_combat_argument_kind;
typedef struct qa_compat_q2_native_combat_argument {
  qa_compat_q2_native_combat_argument_kind kind; qa_compat_q2_native_combat_field field;
  qa_guest_value_layout layout; const uint8_t *bytes; size_t byte_count;
  const qa_native_mod_address *address; /* nullable (address kind) */
} qa_compat_q2_native_combat_argument;
typedef struct qa_compat_q2_native_combat_call { const char *convention; const qa_compat_q2_native_combat_argument *arguments; size_t argument_count; } qa_compat_q2_native_combat_call;
extern const qa_guest_layout qa_compat_q2_native_rerelease_mod_layout;
const qa_compat_q2_native_combat_field *qa_compat_q2_native_combat_fields(qa_compat_q2_native_combat_operation op, int64_t pointer_bytes, size_t *count);
/* TS stockNativeCombatCall into caller storage (max 10 args). */
qa_compat_q2_native_combat_call qa_compat_q2_stock_native_combat_call(qa_compat_q2_native_combat_operation op, bool has_abi, qa_native_call_abi abi, qa_compat_q2_native_combat_argument *storage);
/* TS nativeCombatSignature into caller parameter storage. */
qa_compat_q2_guest_call_signature qa_compat_q2_native_combat_signature(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation op, qa_native_call_abi abi, qa_guest_value_layout *storage);
void qa_compat_q2_validate_native_combat_call(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation op, qa_native_call_abi abi);
/* TS readNativeCombatCall: arguments/bytes/address arrays malloc'd
 * (free with qa_compat_q2_native_combat_call_free). False on OOM. */
bool qa_compat_q2_read_native_combat_call(const qa_save_reader *reader, qa_compat_q2_native_combat_operation op, qa_native_call_abi abi, qa_compat_q2_native_combat_call *out);
void qa_compat_q2_native_combat_call_free(qa_compat_q2_native_combat_call *call);
/* TS readNativeCombatArguments into caller storage (max 10). */
size_t qa_compat_q2_read_native_combat_arguments(const qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_operation op, const qa_guest_call_value *values, size_t count, int64_t pointer_bytes, qa_guest_call_value *out);
#endif
