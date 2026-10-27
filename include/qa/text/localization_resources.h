/*
 * Port of src/text/localization-resources.ts: layered loading of
 * loc_<language>.txt (plus the _mod overlay) over English.
 * C accommodations: the TS returns a freshly allocated catalog/table;
 * here the caller provides the destination, which these functions
 * initialize and merge into. The async reader is a synchronous
 * function pointer returning a malloc'd buffer (freed here) or NULL
 * for a missing resource. Load order stays sequential and
 * deterministic, as the TS comment requires. Profile strings are
 * qa_loc_profile (see text/localization.h).
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_LOCALIZATION_RESOURCES_H
#define QA_TEXT_LOCALIZATION_RESOURCES_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/text/localization.h"

/* TS LocalizationReader. Malloc'd result or NULL (TS null). */
typedef uint8_t *(*qa_localization_read_fn)(void *ctx, const char *path,
                                            size_t *out_length);

/* TS loadLocalizationResources. Initializes *out, then merges English,
 * its mod overlay, then (unless language is "english") the selected
 * language and its overlay. */
void qa_load_localization_resources(qa_seat_id seat, const char *language,
                                    qa_localization_read_fn read, void *read_ctx,
                                    qa_loc_profile profile,
                                    qa_localization_catalog *out);
/* TS loadServerLocalizationResources: the same layering, no seat. */
void qa_load_server_localization_resources(const char *language,
                                           qa_localization_read_fn read,
                                           void *read_ctx,
                                           qa_loc_profile profile,
                                           qa_localization_table *out);

#endif
