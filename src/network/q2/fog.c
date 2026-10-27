/* Port of src/network/q2/fog.ts. GPL-2.0-or-later. */
#include "qa/network/q2/fog.h"

#include <string.h>

void qa_q2_svc_fog_data_init(qa_q2_svc_fog_data *data) { memset(data, 0, sizeof *data); }
