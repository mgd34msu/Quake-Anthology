/* Port of src/content/q3/base/settings.ts. GPL-2.0-or-later.
 * Snapshots are landed qa_cvar_snapshot copies, owned by the settings
 * object and freed on replace/destroy. capture_save_state returns landed
 * qa_q3_module_cvar_capture rows borrowing those strings. */
#ifndef QA_CONTENT_Q3_BASE_SETTINGS_H
#define QA_CONTENT_Q3_BASE_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/save_module_values.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/cvars/index.h"
#include "qa/persistence/value.h"

typedef struct qa_q3_base_cvar_definition {
  const char *name, *value; double flags; bool track, team_shader;
} qa_q3_base_cvar_definition;
/* TS q3GameCvarDefinitions: fresh array per call in TS; here a borrowed
 * static row set per product (rows are immutable). */
const qa_q3_base_cvar_definition *qa_q3_base_game_cvar_definitions(qa_q3_base_product product, size_t *out_count);

typedef struct qa_q3_base_settings_host {
  void *ctx;
  qa_cvar_registry *cvars;
  void (*send_server_command)(void *ctx, double client, const char *command);
  void (*remap_teams)(void *ctx);
} qa_q3_base_settings_host;

typedef struct qa_q3_base_game_settings qa_q3_base_game_settings;
qa_q3_base_game_settings *qa_q3_base_game_settings_create(qa_q3_base_settings_host host, qa_q3_base_product product);
void qa_q3_base_game_settings_destroy(qa_q3_base_game_settings *settings);
const qa_q3_base_cvar_definition *qa_q3_base_game_settings_definitions(const qa_q3_base_game_settings *settings, size_t *out_count);
/* Caller-owned array; rows borrow snapshot strings. NULL only on OOM. */
qa_q3_module_cvar_capture *qa_q3_base_game_settings_capture_save_state(qa_q3_base_game_settings *settings, size_t *out_count);
void qa_q3_base_game_settings_restore_save_state(qa_q3_base_game_settings *settings, const qa_save_value *value);
void qa_q3_base_game_settings_register(qa_q3_base_game_settings *settings, const char *build_date);
/* Borrowed snapshot, valid until update/restore/destroy. */
const qa_cvar_snapshot *qa_q3_base_game_settings_snapshot(qa_q3_base_game_settings *settings, const char *name);
double qa_q3_base_game_settings_integer(qa_q3_base_game_settings *settings, const char *name);
double qa_q3_base_game_settings_number(qa_q3_base_game_settings *settings, const char *name);
const char *qa_q3_base_game_settings_string(qa_q3_base_game_settings *settings, const char *name);
void qa_q3_base_game_settings_update(qa_q3_base_game_settings *settings);
#endif
