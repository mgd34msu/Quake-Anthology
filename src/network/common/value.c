/* Port of src/network/common/value.ts. GPL-2.0-or-later. */
#include "qa/network/common/value.h"
bool qa_net_is_unknown_array(int kind) { return kind == 1; }
bool qa_net_is_record(int kind) { return kind == 2; }
bool qa_net_is_readable_stream(int kind) { return kind == 3; }
