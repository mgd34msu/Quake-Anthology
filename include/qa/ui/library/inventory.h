/*
 * Port of src/ui/library/inventory.ts: registerInventoryMenu.
 * items returns a borrowed array (NULL is the TS null) valid until
 * its next call; command receives the TS (name, args) pair.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_LIBRARY_INVENTORY_H
#define QA_UI_LIBRARY_INVENTORY_H

#include <stddef.h>

#include "qa/ui/common/controller.h"
#include "qa/ui/hud/index.h"
#include "qa/ui/settings/index.h"

/** TS command(name, args) callback. */
typedef void (*qa_ui_inventory_command_fn)(void *ctx, const char *name,
                                           const char *const *args,
                                           size_t arg_count);
/** TS items() callback: NULL return is TS null. */
typedef const qa_ui_hud_inventory_item *(*qa_ui_inventory_items_fn)(
    void *ctx, size_t *out_count);

/** TS registerInventoryMenu(controller, items, command). */
qa_ui_settings_menus qa_ui_register_inventory_menu(
    qa_native_ui_controller *controller, qa_ui_inventory_items_fn items,
    void *items_ctx, qa_ui_inventory_command_fn command, void *command_ctx);

#endif
