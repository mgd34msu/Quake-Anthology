/* Port of src/app/bootstrap/view-settings.ts. Async load/save ->
 * synchronous (landed ConfigStore is synchronous). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_VIEW_SETTINGS_H
#define QA_APP_BOOTSTRAP_VIEW_SETTINGS_H
#include <stdbool.h>
#include "qa/core/cvars/index.h"
#include "qa/settings/config.h"
#include "qa/ui/settings/index.h"
typedef struct qa_app_view_settings qa_app_view_settings;
qa_app_view_settings *qa_app_view_settings_create(void (*changed)(void *ctx, double value), void *ctx);
void qa_app_view_settings_destroy(qa_app_view_settings *s);
double qa_app_view_settings_field_of_view(const qa_app_view_settings *s);
bool qa_app_view_settings_override(const qa_app_view_settings *s, double *out); /* false = TS null */
/* TS bindCvars returns remove closure; here qa_app_view_settings_unbind_cvars. */
void qa_app_view_settings_bind_cvars(qa_app_view_settings *s, qa_cvar_registry *cvars);
void qa_app_view_settings_unbind_cvars(qa_app_view_settings *s);
void qa_app_view_settings_set_field_of_view(qa_app_view_settings *s, double value);
qa_ui_setting_binding qa_app_view_settings_binding(qa_app_view_settings *s);
void qa_app_view_settings_load(qa_app_view_settings *s, const qa_config_store *store);
void qa_app_view_settings_save(qa_app_view_settings *s, const qa_config_store *store);
#endif
