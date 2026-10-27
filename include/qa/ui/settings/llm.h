/* Port of src/ui/settings/llm.ts. LlmSettingsUi is the landed
 * settings service itself; async operations run synchronously and
 * refreshModels' AbortController is a state-owned abort signal
 * that is aborted and reset at the TS abort points (a synchronous
 * refresh cannot be interrupted). GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_LLM_H
#define QA_UI_SETTINGS_LLM_H

#include "qa/llm/settings.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/settings/index.h"

/** TS {root, dispose()} over the shared SettingsMenus shape. */
typedef qa_ui_settings_menus qa_ui_llm_settings_menus;

qa_ui_llm_settings_menus qa_ui_register_llm_settings_menu(
    qa_native_ui_controller *controller, qa_llm_settings_service *service);

#endif
