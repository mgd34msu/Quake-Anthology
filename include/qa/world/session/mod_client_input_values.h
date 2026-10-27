/*
 * Port of src/world/session/mod-client-input-values.ts: the effective
 * command projected to callback input values. The TS Map becomes a
 * fixed ten-entry array in TS insertion order. The "self" actor
 * value points into the application (borrowed). GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_CLIENT_INPUT_VALUES_H
#define QA_WORLD_SESSION_MOD_CLIENT_INPUT_VALUES_H

#include <stddef.h>

#include "qa/contracts/mod_callbacks.h"
#include "qa/world/session/mod_clients.h"

typedef struct qa_mod_client_input_value_entry {
  qa_mod_callback_input name;
  qa_mod_runtime_value value;
} qa_mod_client_input_value_entry;

typedef struct qa_mod_client_input_values {
  qa_mod_client_input_value_entry entries[10];
  size_t count; /* always 10 */
} qa_mod_client_input_values;

qa_mod_client_input_values qa_mod_client_input_values_of(
    const qa_mod_client_application *application);

#endif
