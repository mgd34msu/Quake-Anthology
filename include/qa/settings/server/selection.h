/*
 * Port of src/settings/server/selection.ts: only collections with an
 * installed source consumer are admitted; equipment remains
 * recipe-owned. Both entry points return the malloc'd
 * qa_collect_server_settings result (borrowed definition pointers;
 * free the array). No match means an empty result.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_SELECTION_H
#define QA_SETTINGS_SERVER_SELECTION_H

#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/settings/server/types.h"

const qa_server_setting_definition **qa_server_definitions_for_recipe(
    const qa_executable_recipe *recipe, size_t *out_count);
const qa_server_setting_definition **qa_server_definitions_for_selection(
    qa_provider_reference source, qa_provider_reference match,
    qa_provider_reference combat, size_t *out_count);

#endif
