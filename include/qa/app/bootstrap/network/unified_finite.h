/* Port of src/app/bootstrap/network/unified-finite.ts over qa_save_value trees. Every tree node
 * is a plain record or array in TS terms (class instances and typed arrays never reach a tree
 * as objects: bytes are a leaf kind and are not searched). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_FINITE_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_FINITE_H
#include <stdbool.h>
#include "qa/persistence/value.h"
/* True when a record or array holds NaN or an infinity within 32 levels (TS depth > 32 stops). */
bool qa_app_unified_has_non_finite(const qa_save_value *value, int depth);
/* In place: each non-finite number within the TS depth bound becomes zero. */
void qa_app_unified_zero_non_finite(qa_save_value *value, int depth);
#endif
