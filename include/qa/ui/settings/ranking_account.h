/* Port of src/ui/settings/ranking-account.ts. Async operations are
 * synchronous (see rankings.h); busy/generation guards are kept.
 * GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_RANKING_ACCOUNT_H
#define QA_UI_SETTINGS_RANKING_ACCOUNT_H

#include "qa/ui/common/controller.h"
#include "qa/ui/settings/index.h"
#include "qa/ui/settings/rankings.h"

/** current returns the actions or NULL (TS null). */
qa_ui_settings_menus qa_ui_register_ranking_account_menu(
    qa_native_ui_controller *controller,
    const qa_ui_ranking_account_actions *(*current)(void *ctx), void *ctx);

#endif
