/* Port of src/ui/settings/input-routing.ts. GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_INPUT_ROUTING_H
#define QA_UI_SETTINGS_INPUT_ROUTING_H

#include <stddef.h>

#include "qa/input/router.h"
#include "qa/platform/controller.h"
#include "qa/ui/settings/index.h"

/** devices returns a borrowed array. */
qa_ui_setting_bindings qa_ui_bind_input_routing_settings(
    qa_input_router *(*router)(void *ctx), void *router_ctx,
    const qa_controller_device *(*devices)(void *ctx, size_t *out_count),
    void *devices_ctx);

#endif
