/*
 * Port of src/settings/server/cvars.ts: cvarServerSettingsOwner, the
 * ServerSettingsOwner backed by a CvarRegistry's current and latched
 * values (including mixed-timing bit masks). The owner storage is
 * caller-allocated; owner.ctx points back at the storage struct.
 * Reads of unregistered cvars abort with the TS Error message.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_CVARS_H
#define QA_SETTINGS_SERVER_CVARS_H

#include <stdbool.h>

#include "qa/core/cvars/index.h"
#include "qa/settings/server/types.h"

typedef struct qa_cvar_server_owner {
  qa_server_settings_owner owner;
  qa_cvar_registry *cvars; /* borrowed */
  bool initializing;
} qa_cvar_server_owner;

/* TS cvarServerSettingsOwner(cvars, initializing = false). */
void qa_cvar_server_settings_owner_init(qa_cvar_server_owner *storage,
                                        qa_cvar_registry *cvars,
                                        bool initializing);

#endif
