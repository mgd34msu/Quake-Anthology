/* Port of src/network/common/value.ts. TS `unknown` type predicates have
 * no runtime C counterpart; the C form classifies a tagged value. A NULL
 * tag means "not an object". GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_VALUE_H
#define QA_NETWORK_COMMON_VALUE_H
#include <stdbool.h>
/* kind: 0=other/null, 1=array, 2=record(object), 3=readable-stream. */
bool qa_net_is_unknown_array(int kind);
bool qa_net_is_record(int kind);
bool qa_net_is_readable_stream(int kind);
#endif
