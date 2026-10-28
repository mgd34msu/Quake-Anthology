/* Port of src/ui/settings/llm.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/llm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/llm/errors.h"
#include "qa/llm/models.h"
#include "qa/llm/request.h"
#include "qa/ui/common/layout.h"

typedef struct llm_state llm_state;
typedef struct lctx { llm_state *state; int action; const char *model_id; } lctx;
struct llm_state {
  qa_native_ui_controller *controller;
  qa_llm_settings_service *service;
  char *key;
  char *model;    /* NULL = TS null */
  char *base_url; /* NULL = TS null */
  bool busy;
  char *status;
  long generation;
  bool has_effort; /* false = TS undefined */
  char *effort;    /* NULL (with has_effort) = TS null */
  qa_llm_abort_signal discovery;
  bool has_discovery;
  long page;
  int handles[2];
  qa_ui_control controls[20];
  lctx ctxs[20];
  qa_ui_choice effort_choices[40];
  char model_label[640];
  char *owned[48];
  size_t owned_count;
  char *signin_selected_model; /* factory-time snapshot for L_SIGNIN */
  char title[64];
};

enum { L_NONE, L_PROVIDER, L_MODEL, L_EFFORT, L_SIGNIN, L_CANCEL_SIGNIN,
       L_SIGNOUT, L_KEY, L_REMOVE_KEY, L_BASE_URL, L_SAVE, L_REFRESH,
       L_BACK, L_MODEL_ITEM, L_PREV, L_NEXT };

static void set_owned(char **slot, const char *value) {
  free(*slot);
  *slot = value == NULL ? NULL : qa_input_dup(value);
  if (value != NULL && *slot == NULL) qa_range_error("Out of memory");
}
static void clear_drafts(llm_state *s) {
  set_owned(&s->key, "");
  set_owned(&s->model, NULL);
  set_owned(&s->base_url, NULL);
  s->has_effort = false;
  set_owned(&s->effort, NULL);
}
static void set_status(llm_state *s, const char *value) {
  set_owned(&s->status, value);
}
static void abort_discovery(llm_state *s) {
  if (s->has_discovery) s->discovery.aborted = true;
  s->has_discovery = false;
}
static bool provider_configured(const qa_llm_settings_snapshot *snap,
                                qa_llm_provider provider) {
  switch (provider) {
    case QA_LLM_PROVIDER_SUBSCRIPTION:
      return snap->providers.subscription.configured;
    case QA_LLM_PROVIDER_CHATGPT_API:
      return snap->providers.chatgpt_api.configured;
    default:
      return snap->providers.other_api.configured;
  }
}
static const char *provider_model(const qa_llm_settings_snapshot *snap,
                                  qa_llm_provider provider) {
  switch (provider) {
    case QA_LLM_PROVIDER_SUBSCRIPTION:
      return snap->providers.subscription.model;
    case QA_LLM_PROVIDER_CHATGPT_API:
      return snap->providers.chatgpt_api.model;
    default:
      return snap->providers.other_api.model;
  }
}
static void refresh_models(llm_state *s) {
  abort_discovery(s);
  qa_llm_settings_snapshot snap;
  qa_llm_settings_read(s->service, &snap);
  qa_llm_provider provider = snap.provider;
  bool configured = provider_configured(&snap, provider);
  qa_llm_settings_snapshot_free(&snap);
  if (!configured) return;
  memset(&s->discovery, 0, sizeof s->discovery);
  s->has_discovery = true;
  qa_llm_model_list out;
  memset(&out, 0, sizeof out);
  qa_llm_error error;
  qa_llm_error_clear(&error);
  (void)qa_llm_settings_refresh_models(s->service, provider, &s->discovery,
                                       &out, &error); /* TS .catch(()=>{}) */
  qa_llm_model_list_free(&out);
}
/* Synchronous run(): op returns false + error on failure. The
 * success callback codes mirror the TS closures per call site. */
enum success_kind { SUCCESS_DEFAULT, SUCCESS_PROVIDER, SUCCESS_SIGNIN,
                    SUCCESS_SAVE };
typedef struct run_args {
  int op;
  const char *text;
  const char *text2;
  bool flag;
  qa_llm_provider provider;
} run_args;
static bool do_op(llm_state *s, const run_args *args, qa_llm_error *error) {
  switch (args->op) {
    case 0: /* selectProvider */
      return qa_llm_settings_select_provider(s->service, args->provider,
                                             error);
    case 1: /* signInSubscription */
      return qa_llm_settings_sign_in_subscription(s->service, error);
    case 2: /* removeCredential */
      return qa_llm_settings_remove_credential(s->service, args->provider,
                                               error);
    default: { /* save sequence */
      if (args->provider == QA_LLM_PROVIDER_OTHER_API) {
        if (!qa_llm_settings_save_other_service(s->service, args->text2,
                                                 args->text,
                                                 "openai-chat-completions",
                                                 error))
          return false;
      } else if (args->text[0] != '\0') {
        if (!qa_llm_settings_set_model(s->service, args->provider,
                                        args->text, error))
          return false;
      }
      return true;
    }
  }
}
static void run(llm_state *s, run_args *args, enum success_kind success);
static void run(llm_state *s, run_args *args, enum success_kind success) {
  if (s->busy) return;
  long current = s->generation;
  s->busy = true;
  set_status(s, "Working...");
  qa_llm_error error;
  qa_llm_error_clear(&error);
  bool ok = do_op(s, args, &error);
  if (current == s->generation) {
    if (!ok) {
      /* TS keeps only LlmSettingsError messages; every landed
       * settings failure is an LlmSettingsError. */
      set_status(s, error.message[0] != '\0'
                        ? error.message
                        : "Operation failed. Check settings and retry.");
    } else if (success == SUCCESS_DEFAULT) {
      clear_drafts(s);
      set_status(s, "Saved");
    }
    /* Other success closures run at their call sites below. */
  }
  if (current == s->generation) s->busy = false;
  if (ok && current == s->generation && success != SUCCESS_DEFAULT) {
    if (success == SUCCESS_PROVIDER) {
      set_status(s, "");
      refresh_models(s);
    }
  }
}

static bool blank_trim(const char *text) {
  char *trimmed = qa_input_js_trim(text);
  if (trimmed == NULL) qa_range_error("Out of memory");
  bool empty = trimmed[0] == '\0';
  free(trimmed);
  return empty;
}

static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  lctx *c = v;
  llm_state *s = c->state;
  switch (c->action) {
    case L_MODEL:
      s->page = 0;
      qa_native_ui_controller_open_menu(s->controller,
                                        "menu:settings:llm-models");
      break;
    case L_SIGNIN: {
      run_args args = { 1, NULL, NULL, false, QA_LLM_PROVIDER_SUBSCRIPTION };
      long gen = s->generation;
      bool was_busy = s->busy;
      run(s, &args, SUCCESS_SIGNIN);
      if (!was_busy && gen == s->generation && !s->busy
          && strcmp(s->status, "Working...") == 0) {
        /* Success closure: TS reads the factory-time selected model. */
        const char *selected = s->signin_selected_model != NULL
                                   ? s->signin_selected_model
                                   : "";
        set_owned(&s->key, "");
        const char *draft = s->model != NULL ? s->model : selected;
        if (blank_trim(draft))
          set_status(s, "Signed in. Select a model, then save settings.");
        else if (s->model != NULL)
          set_status(s, "Signed in. Save settings to use this model.");
        else
          set_status(s, "Signed in.");
        refresh_models(s);
      }
      break;
    }
    case L_CANCEL_SIGNIN:
      s->generation++;
      qa_llm_settings_cancel_sign_in(s->service);
      s->busy = false;
      clear_drafts(s);
      set_status(s, "Sign-in canceled");
      break;
    case L_SIGNOUT:
    case L_REMOVE_KEY: {
      qa_llm_settings_snapshot snap;
      qa_llm_settings_read(s->service, &snap);
      run_args args = { 2, NULL, NULL, false, snap.provider };
      qa_llm_settings_snapshot_free(&snap);
      run(s, &args, SUCCESS_DEFAULT);
      break;
    }
    case L_SAVE: {
      qa_llm_settings_snapshot snap;
      qa_llm_settings_read(s->service, &snap);
      qa_llm_provider provider = snap.provider;
      const char *selected = provider_model(&snap, provider);
      char *draft_key = qa_input_dup(s->key);
      char *draft_model = qa_input_dup(s->model != NULL ? s->model
                                                        : selected);
      char *draft_base = qa_input_dup(
          s->base_url != NULL ? s->base_url
                              : snap.providers.other_api.base_url);
      bool has_effort = s->has_effort;
      char *draft_effort = s->effort != NULL ? qa_input_dup(s->effort) : NULL;
      bool catalog_idle =
          snap.catalogs[provider].status == QA_LLM_CATALOG_IDLE;
      qa_llm_settings_snapshot_free(&snap);
      if (draft_key == NULL || draft_model == NULL || draft_base == NULL)
        qa_range_error("Out of memory");
      set_owned(&s->key, "");
      if (!s->busy) {
        long gen = s->generation;
        s->busy = true;
        set_status(s, "Working...");
        qa_llm_error error;
        qa_llm_error_clear(&error);
        bool ok = true;
        if (provider == QA_LLM_PROVIDER_OTHER_API)
          ok = qa_llm_settings_save_other_service(
              s->service, draft_base, draft_model,
              "openai-chat-completions", &error);
        else if (draft_model[0] != '\0')
          ok = qa_llm_settings_set_model(s->service, provider, draft_model,
                                          &error);
        if (ok && has_effort)
          ok = qa_llm_settings_set_reasoning_effort(s->service, provider,
                                                    draft_effort, &error);
        if (ok && provider != QA_LLM_PROVIDER_SUBSCRIPTION
            && draft_key[0] != '\0')
          ok = qa_llm_settings_save_api_key(s->service, provider, draft_key,
                                            &error);
        if (gen == s->generation) {
          if (ok) {
            bool changed_connection = s->base_url != NULL;
            clear_drafts(s);
            set_status(s, draft_model[0] == '\0'
                              ? "Saved. Select a model to use console requests."
                              : "Saved");
            if (draft_key[0] != '\0' || changed_connection || catalog_idle)
              refresh_models(s);
          } else {
            set_status(s, error.message[0] != '\0'
                              ? error.message
                              : "Operation failed. Check settings and retry.");
          }
          s->busy = false;
        }
      }
      free(draft_key);
      free(draft_model);
      free(draft_base);
      free(draft_effort);
      break;
    }
    case L_REFRESH:
      refresh_models(s);
      break;
    case L_BACK:
      qa_native_ui_controller_close_menu(s->controller);
      break;
    case L_MODEL_ITEM:
      set_owned(&s->model, c->model_id);
      s->has_effort = true;
      set_owned(&s->effort, NULL);
      set_status(s, "Save settings to use this model.");
      qa_native_ui_controller_close_menu(s->controller);
      break;
    case L_PREV: s->page--; break;
    case L_NEXT: s->page++; break;
    default: break;
  }
}
static void on_select(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  lctx *c = v;
  llm_state *s = c->state;
  if (c->action == L_PROVIDER) {
    if (s->busy) return;
    qa_llm_provider provider;
    if (!qa_llm_provider_parse(value, &provider)) return;
    clear_drafts(s);
    abort_discovery(s);
    run_args args = { 0, NULL, NULL, false, provider };
    run(s, &args, SUCCESS_PROVIDER);
  } else if (c->action == L_EFFORT) {
    s->has_effort = true;
    set_owned(&s->effort, value[0] == '\0' ? NULL : value);
  }
}
static void on_change_text(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  lctx *c = v;
  if (c->action == L_KEY) set_owned(&c->state->key, value);
  else if (c->action == L_BASE_URL) set_owned(&c->state->base_url, value);
}
static void on_submit_noop(void *v, qa_seat_id seat, const char *value) {
  (void)v; (void)seat; (void)value;
}
static void clear_owned(llm_state *s) {
  for (size_t i = 0; i < s->owned_count; i++) free(s->owned[i]);
  s->owned_count = 0;
}
static char *own(llm_state *s, const char *text) {
  if (s->owned_count >= 48) qa_range_error("Out of memory");
  char *copy = qa_input_dup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  s->owned[s->owned_count++] = copy;
  return copy;
}
static qa_ui_control button(llm_state *s, size_t slot, const char *id,
                            const char *label, double row, int action,
                            bool enabled) {
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = id;
  control.kind = "button";
  control.label = label;
  control.rect = qa_ui_menu_row_default(row);
  control.visible = true;
  control.enabled = enabled;
  s->ctxs[slot].state = s;
  s->ctxs[slot].action = action;
  s->ctxs[slot].model_id = NULL;
  control.ctx = &s->ctxs[slot];
  control.activate_seat = on_activate;
  return control;
}
static qa_ui_control field(llm_state *s, size_t slot, const char *id,
                           const char *label, double row, const char *text,
                           int action, bool masked) {
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = id;
  control.kind = "text-entry";
  control.label = label;
  control.rect = qa_ui_menu_row_default(row);
  control.visible = true;
  control.enabled = !s->busy;
  control.text = text;
  control.has_masked = true;
  control.masked = masked;
  control.maximum_length = masked ? 4096 : 512;
  s->ctxs[slot].state = s;
  s->ctxs[slot].action = action;
  s->ctxs[slot].model_id = NULL;
  control.ctx = &s->ctxs[slot];
  control.change_text = on_change_text;
  control.submit_text = on_submit_noop;
  return control;
}

/* /.{1,54}(?:\s|$)|.{1,54}/g over bytes. */
static size_t wrap54(const char *text, char *out) {
  size_t length = strlen(text);
  if (length == 0) { out[0] = '\0'; return 0; }
  size_t best = 0;
  for (size_t i = 1; i <= 54 && i < length; i++)
    if (qa_input_js_space((unsigned char)text[i])) best = i + 1;
  size_t take = length <= 54 ? length : best > 0 ? best : 54;
  memcpy(out, text, take);
  out[take] = '\0';
  return take;
}

static void menu_open(void *v, qa_seat_id seat) {
  (void)seat;
  llm_state *s = v;
  clear_drafts(s);
  set_status(s, "");
  refresh_models(s);
}
static void menu_close(void *v, qa_seat_id seat) {
  (void)seat;
  llm_state *s = v;
  abort_discovery(s);
  s->generation++;
  clear_drafts(s);
  s->busy = false;
  set_status(s, "");
  qa_llm_settings_cancel_sign_in(s->service);
}
static qa_ui_menu factory(void *v) {
  llm_state *s = v;
  clear_owned(s);
  qa_llm_settings_snapshot snap;
  qa_llm_settings_read(s->service, &snap);
  qa_llm_provider provider = snap.provider;
  const char *selected_model = provider_model(&snap, provider);
  bool configured = provider_configured(&snap, provider);
  bool pending = snap.subscription_auth == QA_LLM_SUBSCRIPTION_AUTH_PENDING;
  const qa_llm_model_catalog *catalog = &snap.catalogs[provider];
  const char *chosen = s->model != NULL ? s->model : selected_model;
  const qa_llm_model *metadata = NULL;
  for (size_t i = 0; i < catalog->models.count; i++)
    if (strcmp(catalog->models.items[i].id, chosen) == 0)
      metadata = &catalog->models.items[i];
  qa_ui_control *controls = s->controls;
  size_t at = 0;
  static const qa_ui_choice provider_choices[3] = {
    { "chatgpt-subscription", "ChatGPT Subscription" },
    { "chatgpt-api", "ChatGPT API" }, { "other-api", "Other API" }
  };
  qa_ui_control provider_ctl;
  memset(&provider_ctl, 0, sizeof provider_ctl);
  provider_ctl.id = "ui:llm:provider";
  provider_ctl.kind = "choice";
  provider_ctl.label = "Provider";
  provider_ctl.rect = qa_ui_menu_row_default(0);
  provider_ctl.visible = true;
  provider_ctl.enabled = !pending;
  provider_ctl.selected = own(s, qa_llm_provider_name(provider));
  provider_ctl.choices = provider_choices;
  provider_ctl.choice_count = 3;
  s->ctxs[at].state = s;
  s->ctxs[at].action = L_PROVIDER;
  s->ctxs[at].model_id = NULL;
  provider_ctl.ctx = &s->ctxs[at];
  provider_ctl.select_choice = on_select;
  controls[at++] = provider_ctl;
  controls[at] = button(s, at, "ui:llm:status",
                        pending ? "Waiting for browser sign-in"
                                : configured ? "Credential stored"
                                             : "No credential stored",
                        1, L_NONE, false);
  at++;
  snprintf(s->model_label, sizeof s->model_label, "Model: %s",
           chosen[0] != '\0' ? chosen : "Select a model");
  controls[at] = button(s, at, "ui:llm:model", s->model_label, 2, L_MODEL,
                        !s->busy);
  at++;
  qa_ui_control effort;
  memset(&effort, 0, sizeof effort);
  effort.id = "ui:llm:effort";
  effort.kind = "choice";
  effort.label = "Reasoning effort";
  effort.rect = qa_ui_menu_row_default(3);
  effort.visible = true;
  size_t effort_count = metadata != NULL ? metadata->reasoning_effort_count
                                         : 0;
  effort.enabled = !s->busy && effort_count > 0;
  const char *current_effort = !s->has_effort ? snap.reasoning_effort
                                              : s->effort;
  effort.selected = current_effort != NULL ? own(s, current_effort) : "";
  size_t ec = 0;
  s->effort_choices[ec++] = (qa_ui_choice){ "", "Model default" };
  for (size_t i = 0; i < effort_count && ec < 40; i++) {
    char *id = own(s, metadata->reasoning_efforts[i]);
    s->effort_choices[ec++] = (qa_ui_choice){ id, id };
  }
  effort.choices = s->effort_choices;
  effort.choice_count = ec;
  s->ctxs[at].state = s;
  s->ctxs[at].action = L_EFFORT;
  s->ctxs[at].model_id = NULL;
  effort.ctx = &s->ctxs[at];
  effort.select_choice = on_select;
  controls[at++] = effort;
  if (provider == QA_LLM_PROVIDER_SUBSCRIPTION) {
    set_owned(&s->signin_selected_model, selected_model);
    controls[at] = button(s, at, "ui:llm:signin", "Sign in with ChatGPT", 4,
                          L_SIGNIN, !s->busy && !pending);
    at++;
    controls[at] = button(s, at, "ui:llm:cancel-signin", "Cancel sign-in",
                          5, L_CANCEL_SIGNIN, pending);
    at++;
    controls[at] = button(s, at, "ui:llm:signout", "Sign out", 6, L_SIGNOUT,
                          !s->busy && configured);
    at++;
  } else {
    controls[at] = field(s, at, "ui:llm:key", "Paste API key", 4, s->key,
                        L_KEY, true);
    at++;
    controls[at] = button(s, at, "ui:llm:remove-key", "Remove stored key",
                          5, L_REMOVE_KEY, !s->busy && configured);
    at++;
    if (provider == QA_LLM_PROVIDER_OTHER_API) {
      controls[at] = field(
          s, at, "ui:llm:base-url", "Base URL", 6,
          s->base_url != NULL ? s->base_url
                              : own(s, snap.providers.other_api.base_url),
          L_BASE_URL, false);
      at++;
      controls[at] = button(s, at, "ui:llm:transport",
                            "OpenAI-compatible Chat Completions", 7,
                            L_NONE, false);
      at++;
    }
  }
  char message_buffer[1024];
  const char *message;
  if (s->busy) message = s->status;
  else if (catalog->status == QA_LLM_CATALOG_LOADING)
    message = "Loading models... Settings can still be saved.";
  else if (catalog->status == QA_LLM_CATALOG_ERROR)
    message = catalog->message != NULL ? catalog->message : "";
  else if (s->status[0] != '\0') message = s->status;
  else if (snap.subscription_auth == QA_LLM_SUBSCRIPTION_AUTH_ERROR)
    message = snap.subscription_auth_message != NULL
                  ? snap.subscription_auth_message
                  : "";
  else if (snap.error_count > 0) message = snap.errors[0].message;
  else
    message = chosen[0] == '\0'
                  ? "Select a model after signing in or saving an API key."
                  : "Console: llm_ask or llm_exec";
  snprintf(message_buffer, sizeof message_buffer, "%s", message);
  controls[at] = button(s, at, "ui:llm:save", "Save settings", 8, L_SAVE,
                        !s->busy && !pending);
  /* TS remaps save/refresh rects after building the array. */
  {
    qa_ui_menu_row_options save_opts = { false, false, true, false,
                                         0, 0, 248, 0 };
    controls[at].rect = qa_ui_menu_row(8, save_opts);
  }
  at++;
  controls[at] = button(s, at, "ui:llm:refresh", "Refresh models", 8,
                        L_REFRESH, !s->busy && !pending && configured);
  {
    qa_ui_menu_row_options refresh_opts = { true, false, true, false,
                                            328, 0, 248, 0 };
    controls[at].rect = qa_ui_menu_row(8, refresh_opts);
  }
  at++;
  const char *rest = message_buffer;
  for (int line = 0; line < 2 && rest[0] != '\0' && at < 19; line++) {
    char buffer[64];
    size_t used = wrap54(rest, buffer);
    char *trimmed = qa_input_js_trim(buffer);
    if (trimmed == NULL) qa_range_error("Out of memory");
    char id[48];
    snprintf(id, sizeof id, "ui:llm:message:%d", line);
    controls[at] = button(s, at, own(s, id), own(s, trimmed), 9 + line,
                          L_NONE, false);
    free(trimmed);
    at++;
    rest += used;
  }
  controls[at] = button(s, at, "ui:llm:back", "Back", 11, L_BACK, true);
  at++;
  qa_llm_settings_snapshot_free(&snap);
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:settings:llm";
  menu.title = "LLM options";
  menu.full_screen = false;
  menu.controls = controls;
  menu.control_count = at;
  menu.ctx = s;
  menu.open = menu_open;
  menu.close = menu_close;
  return menu;
}

static qa_ui_menu models_factory(void *v) {
  llm_state *s = v;
  clear_owned(s);
  qa_llm_settings_snapshot snap;
  qa_llm_settings_read(s->service, &snap);
  const qa_llm_model_catalog *catalog = &snap.catalogs[snap.provider];
  size_t count = catalog->models.count;
  long pages = (long)((count + 7) / 8);
  if (pages < 1) pages = 1;
  if (s->page > pages - 1) s->page = pages - 1;
  if (s->page < 0) s->page = 0;
  qa_ui_control *controls = s->controls;
  size_t at = 0;
  size_t first = (size_t)s->page * 8;
  for (size_t i = first; i < count && i < first + 8 && at < 8; i++) {
    char id[48];
    snprintf(id, sizeof id, "ui:llm-models:item:%zu", i - first);
    char *model_id = own(s, catalog->models.items[i].id);
    controls[at] = button(s, at, own(s, id), model_id, (double)(i - first),
                          L_MODEL_ITEM, true);
    s->ctxs[at].model_id = model_id;
    at++;
  }
  if (count == 0) {
    controls[at] = button(
        s, at, "ui:llm-models:empty",
        catalog->status == QA_LLM_CATALOG_LOADING
            ? "Loading models..."
            : "No models loaded. Use Refresh models.",
        0, L_NONE, false);
    at++;
  }
  controls[at] = button(s, at, "ui:llm-models:previous", "Previous page", 8,
                        L_PREV, s->page > 0);
  at++;
  controls[at] = button(s, at, "ui:llm-models:next", "Next page", 9, L_NEXT,
                        s->page + 1 < pages);
  at++;
  controls[at] = button(s, at, "ui:llm-models:back", "Back", 11, L_BACK,
                        true);
  at++;
  snprintf(s->title, sizeof s->title, "Models (%ld/%ld)", s->page + 1, pages);
  qa_llm_settings_snapshot_free(&snap);
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:settings:llm-models";
  menu.title = s->title;
  menu.full_screen = false;
  menu.controls = controls;
  menu.control_count = at;
  return menu;
}

static void llm_dispose(qa_ui_settings_menus *menus) {
  llm_state *s = menus->state;
  if (s == NULL) return;
  qa_native_ui_controller_unregister(s->controller, s->handles[1]);
  qa_native_ui_controller_unregister(s->controller, s->handles[0]);
  abort_discovery(s);
  qa_llm_settings_cancel_sign_in(s->service);
  clear_drafts(s);
  clear_owned(s);
  free(s->key);
  free(s->model);
  free(s->base_url);
  free(s->status);
  free(s->effort);
  free(s->signin_selected_model);
  free(s);
  menus->state = NULL;
}

qa_ui_llm_settings_menus qa_ui_register_llm_settings_menu(
    qa_native_ui_controller *controller, qa_llm_settings_service *service) {
  llm_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->controller = controller;
  s->service = service;
  s->handles[0] = -1;
  s->handles[1] = -1;
  clear_drafts(s);
  set_status(s, "");
  int root = qa_native_ui_controller_register(controller, "menu:settings:llm",
                                              factory, s);
  if (root < 0) qa_range_error("Out of memory");
  s->handles[0] = root;
  int models = qa_native_ui_controller_register(
      controller, "menu:settings:llm-models", models_factory, s);
  if (models < 0) qa_range_error("Out of memory");
  s->handles[1] = models;
  qa_ui_llm_settings_menus menus = { "menu:settings:llm", s, llm_dispose };
  return menus;
}
