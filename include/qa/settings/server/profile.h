/*
 * Port of src/settings/server/profile.ts: collection/deduplication,
 * the shared parseServerSetting boundary, bound read/write, and
 * server profile parse/capture/apply/save/load. C accommodations:
 *  - parseServerProfile takes the TS `unknown` as a qa_save_value
 *    tree; apply/save validate an already-parsed qa_server_profile
 *    with the same checks and messages.
 *  - parseServerSetting returns a malloc'd normalized value and
 *    aborts with the TS RangeError message; captureServerProfile
 *    uses the same checks non-fatally, mirroring its RangeError
 *    catch (a source may author values outside the menu choices).
 *  - Text-entry length counts bytes; the TS counts UTF-16 units,
 *    which can differ for non-ASCII text.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_PROFILE_H
#define QA_SETTINGS_SERVER_PROFILE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/persistence/value.h"
#include "qa/settings/config.h"
#include "qa/settings/server/types.h"

void qa_server_profile_free(qa_server_profile *profile);

/* Malloc'd array of borrowed definition pointers. Aborts on a
 * duplicate id (TS Error) or an invalid default (TS RangeError).
 * NULL only on OOM. */
const qa_server_setting_definition **qa_collect_server_settings(
    const qa_server_setting_collection *collections, size_t collection_count,
    size_t *out_count);

/** Configuration files, console bridges and UI writes share this
 * boundary. Malloc'd normalized value; NULL only on OOM. */
char *qa_parse_server_setting(const qa_server_setting_definition *definition,
                              const char *input);

qa_server_setting_status qa_read_server_setting(
    const qa_bound_server_setting *binding);
qa_server_setting_status qa_write_server_setting(
    const qa_bound_server_setting *binding, const char *value);

void qa_parse_server_profile(const qa_save_value *value,
                             const qa_server_setting_definition *const *definitions,
                             size_t definition_count, qa_server_profile *out);
void qa_capture_server_profile(const qa_bound_server_setting *bindings,
                               size_t binding_count, qa_server_profile *out);
void qa_apply_server_profile(const qa_server_profile *profile,
                             const qa_bound_server_setting *bindings,
                             size_t binding_count);
bool qa_save_server_profile(const qa_config_store *store, const char *name,
                            const qa_server_profile *profile,
                            const qa_server_setting_definition *const *definitions,
                            size_t definition_count);
/* False is the TS null (absent or undecodable profile file). */
bool qa_load_server_profile(const qa_config_store *store, const char *name,
                            const qa_server_setting_definition *const *definitions,
                            size_t definition_count, qa_server_profile *out);

#endif
