/* Port of src/bots/navigation/aas-optimize.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_AAS_OPTIMIZE_H
#define QA_BOTS_NAVIGATION_AAS_OPTIMIZE_H
#include "qa/bots/navigation/aas.h"
#include <stdbool.h>
/* Deep-ish copy: compacted arrays owned by out; other arrays deep-copied too. False only on OOM. */
bool qa_optimize_aas(qa_aas_asset *out, const qa_aas_asset *asset);
#endif
