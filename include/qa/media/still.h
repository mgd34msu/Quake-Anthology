/*
 * Port of src/media/still.ts: a PCX still as an image cinematic
 * source. The result owns its RGBA pixels; dispose it with
 * qa_cinematic_source_dispose. `source` NULL means
 * "<cinematic PCX>". False only on OOM.
 * GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_STILL_H
#define QA_MEDIA_STILL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/media/playback.h"

bool qa_cinematic_pcx(const uint8_t *bytes, size_t length,
                      const char *source, qa_cinematic_source *out);

#endif
