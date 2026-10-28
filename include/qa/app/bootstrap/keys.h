/* Port of src/app/bootstrap/keys.ts. Async file writes become
 * synchronous. The TS authorization's live getters over the active
 * profile become qa_app_keys_authorization(), which (re)builds the
 * landed authorization for the currently published profile.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_KEYS_H
#define QA_APP_BOOTSTRAP_KEYS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/options.h"
#include "qa/content/catalog/index.h"
#include "qa/core/cvars/index.h"
#include "qa/core/q3_cd_key.h"
#include "qa/network/q3/client_authorization.h"
#include "qa/settings/config.h"
typedef struct qa_app_key_profile qa_app_key_profile;
qa_cvar_registry *qa_app_key_profile_cvars(const qa_app_key_profile *profile);
void qa_app_key_profile_set_cvars(qa_app_key_profile *profile, qa_cvar_registry *cvars);
const char *qa_app_key_profile_game_directory(const qa_app_key_profile *profile);
bool qa_app_key_profile_demo_restricted(const qa_app_key_profile *profile);
const qa_q3_cd_key_state *qa_app_key_profile_state(const qa_app_key_profile *profile);
void qa_app_key_profile_destroy(qa_app_key_profile *profile);
/* TS Q3CdKeyState.readUi passthrough (writes may be NULL). */
void qa_app_key_profile_read_ui(qa_app_key_profile *profile, int64_t unique_key_result,
    const char *game_directory, uint8_t *destination, size_t destination_length,
    const qa_q3_key_ui_writes *writes);
/* Aborts with the TS Error text when no mod write root exists. */
void qa_app_key_profile_write_ui(qa_app_key_profile *profile, double unique,
    const char *directory, const uint8_t *source, size_t source_length);
void qa_app_key_profile_read_authorization(qa_app_key_profile *profile,
    uint8_t *destination, size_t destination_length);
void qa_app_key_profile_save(qa_app_key_profile *profile);
typedef struct qa_app_keys qa_app_keys;
typedef void (*qa_app_keys_print_fn)(void *ctx, const char *text);
/* print is used for the authorization; may be NULL. */
qa_app_keys *qa_app_keys_create(qa_app_keys_print_fn print, void *print_ctx);
void qa_app_keys_destroy(qa_app_keys *keys);
/* Aborts with the TS Error text when no profile is published. */
qa_app_key_profile *qa_app_keys_active(qa_app_keys *keys);
/* Borrowed authorization for the active profile (rebuilt on publish). */
qa_q3_client_authorization *qa_app_keys_authorization(qa_app_keys *keys);
/* Returns a new profile, or NULL (TS null) for non-Q3 products. */
qa_app_key_profile *qa_app_keys_prepare(qa_app_keys *keys, const qa_app_options *options,
    const qa_content_catalog *catalog, qa_cvar_registry *cvars);
/* Takes ownership of profile (NULL clears). */
void qa_app_keys_publish(qa_app_keys *keys, qa_app_key_profile *profile, qa_cvar_registry *cvars);
void qa_app_keys_save(qa_app_keys *keys);
#endif
