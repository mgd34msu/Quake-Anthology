/* Port of src/content/catalog/timing.ts. The numeric profile id is
 * malloc'd ("<family>:binary32"); free with
 * qa_native_provider_timing_free. GPL-2.0-or-later. */
#ifndef QA_CONTENT_CATALOG_TIMING_H
#define QA_CONTENT_CATALOG_TIMING_H

#include <stdbool.h>

#include "qa/contracts/content.h"

/* TS nativeProviderTiming. out->numeric.id is malloc'd (NULL on
 * OOM: returns false). */
bool qa_native_provider_timing(qa_provider_reference provider,
                               qa_game_family family, bool rerelease,
                               qa_provider_timing *out);
void qa_native_provider_timing_free(qa_provider_timing *timing);

#endif
