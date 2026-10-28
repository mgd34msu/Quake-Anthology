/* Port of src/ui/settings/services.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/services.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/cvars/index.h"
#include "qa/foundation/support.h"

static char *dup_str(const char *text) {
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
static void set_str(char **slot, const char *value) {
  char *copy = dup_str(value);
  if (copy == NULL) qa_range_error("Out of memory");
  free(*slot);
  *slot = copy;
}
static double js_number_of(const char *text) {
  if (text == NULL || text[0] == '\0') return text != NULL ? 0 : NAN;
  char *end = NULL;
  double value = strtod(text, &end);
  if (end == text || *end != '\0') return NAN;
  return value;
}
static void js_string(char *buf, size_t size, double value) {
  if (isnan(value)) snprintf(buf, size, "NaN");
  else if (value == floor(value) && fabs(value) < 1e21)
    snprintf(buf, size, "%.0f", value);
  else snprintf(buf, size, "%.17g", value);
}
static void cvar_set(qa_cvar_registry *registry, const char *name,
                     const char *value) {
  qa_cvar_snapshot out;
  if (qa_cvar_registry_set(registry, name, value, false, &out))
    qa_cvar_read_free(&out);
}
static qa_ui_setting_binding binding_base(const char *id, const char *label,
                                          const char *category,
                                          const char *kind, void *ctx) {
  qa_ui_setting_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.id = id;
  binding.label = label;
  binding.category = category;
  binding.kind = kind;
  binding.ctx = ctx;
  return binding;
}
static void *xcalloc_state(size_t size) {
  void *state = calloc(1, size);
  if (state == NULL) qa_range_error("Out of memory");
  return state;
}

/* ---- bindEffectsVolume ---- */

typedef struct effects_state {
  qa_unified_audio *audio;
  double (*read)(void *ctx);
  void *read_ctx;
  qa_ui_setting_binding items[1];
} effects_state;

static bool effects_enabled(void *v) {
  effects_state *s = v;
  return qa_unified_audio_output_state_of(s->audio)
      != QA_UNIFIED_AUDIO_CLOSED;
}
static double effects_read(void *v) {
  effects_state *s = v;
  return s->read(s->read_ctx);
}
static void effects_write(void *v, double value) {
  effects_state *s = v;
  qa_unified_audio_set_effects_volume(s->audio, value);
}
static void effects_teardown(void *state) { free(state); }

qa_ui_setting_bindings qa_ui_bind_effects_volume(
    qa_unified_audio *audio, double (*read)(void *ctx), void *ctx) {
  effects_state *s = xcalloc_state(sizeof *s);
  s->audio = audio;
  s->read = read;
  s->read_ctx = ctx;
  s->items[0] = binding_base("ui:audio:effects", "Sound volume", "audio",
                             "slider", s);
  s->items[0].group = "Output";
  s->items[0].minimum = 0;
  s->items[0].maximum = 1;
  s->items[0].step = 0.05;
  s->items[0].enabled = effects_enabled;
  s->items[0].read_num = effects_read;
  s->items[0].write_num = effects_write;
  return qa_ui_setting_bindings_own(s->items, 1, s, effects_teardown);
}

/* ---- bindRendererSettings ---- */

typedef struct renderer_state {
  qa_ui_renderer_settings_options options;
  char *active, *draft;
  qa_ui_setting_binding items[3];
  qa_ui_choice choices[2];
} renderer_state;

static void renderer_refresh(renderer_state *s) {
  const char *current = s->options.current(s->options.ctx);
  if (strcmp(current, s->active) != 0) {
    set_str(&s->active, current);
    set_str(&s->draft, current);
  }
}
static bool renderer_is_enabled(renderer_state *s) {
  return s->options.enabled == NULL || s->options.enabled(s->options.ctx);
}
static bool renderer_enabled(void *v) { return renderer_is_enabled(v); }
static bool worker_read(void *v) {
  renderer_state *s = v;
  return s->options.has_worker && s->options.worker_read(s->options.ctx);
}
static void worker_write(void *v, bool value) {
  renderer_state *s = v;
  if (!renderer_is_enabled(s)) return;
  const char *error = s->options.has_worker
      ? s->options.worker_write(s->options.ctx, value) : NULL;
  if (error == NULL)
    error = s->options.apply(s->options.ctx,
                             s->options.current(s->options.ctx));
  s->options.report(s->options.ctx,
                    error != NULL ? error : "Render worker change queued.");
}
static const char *renderer_read(void *v) {
  renderer_state *s = v;
  renderer_refresh(s);
  return s->draft;
}
static const qa_ui_choice *renderer_choices(void *v, size_t *out_count) {
  renderer_state *s = v;
  s->choices[0].id = "cpu";
  s->choices[0].label = "CPU";
  s->choices[1].id = "gl";
  s->choices[1].label = "OpenGL";
  *out_count = 2;
  return s->choices;
}
static void renderer_write(void *v, const char *value) {
  renderer_state *s = v;
  renderer_refresh(s);
  if (strcmp(value, "cpu") != 0 && strcmp(value, "gl") != 0)
    qa_range_error("Unknown renderer");
  set_str(&s->draft, value);
}
static bool renderer_apply_enabled(void *v) {
  renderer_state *s = v;
  renderer_refresh(s);
  return renderer_is_enabled(s) && strcmp(s->draft, s->active) != 0;
}
static void renderer_activate(void *v) {
  renderer_state *s = v;
  renderer_refresh(s);
  if (!renderer_is_enabled(s) || strcmp(s->draft, s->active) == 0) return;
  const char *error = s->options.apply(s->options.ctx, s->draft);
  char message[64];
  snprintf(message, sizeof message, "Renderer change queued: %s.",
           strcmp(s->draft, "gl") == 0 ? "OpenGL" : "CPU");
  s->options.report(s->options.ctx, error != NULL ? error : message);
}
static void renderer_teardown(void *state) {
  renderer_state *s = state;
  free(s->active);
  free(s->draft);
  free(s);
}

qa_ui_setting_bindings qa_ui_bind_renderer_settings(
    const qa_ui_renderer_settings_options *options) {
  renderer_state *s = xcalloc_state(sizeof *s);
  s->options = *options;
  const char *current = options->current(options->ctx);
  s->active = dup_str(current);
  s->draft = dup_str(current);
  if (s->active == NULL || s->draft == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  if (options->has_worker) {
    s->items[at] = binding_base("ui:video:render-worker", "Render worker",
                                "display", "toggle", s);
    s->items[at].enabled = renderer_enabled;
    s->items[at].read_bool = worker_read;
    s->items[at].write_bool = worker_write;
    at++;
  }
  s->items[at] = binding_base("ui:video:renderer", "Renderer", "display",
                              "choice", s);
  s->items[at].enabled = renderer_enabled;
  s->items[at].read_str = renderer_read;
  s->items[at].choices = renderer_choices;
  s->items[at].write_str = renderer_write;
  at++;
  s->items[at] = binding_base("ui:video:renderer-apply", "Apply renderer",
                              "display", "button", s);
  s->items[at].enabled = renderer_apply_enabled;
  s->items[at].activate = renderer_activate;
  at++;
  return qa_ui_setting_bindings_own(s->items, at, s, renderer_teardown);
}

/* ---- bindNativeVideoSettings ---- */

void qa_ui_apply_window_mutation(const qa_ui_window_mutation *mutation) {
  if (mutation->is_fullscreen)
    qa_sdl_window_set_fullscreen(mutation->window, mutation->fullscreen);
  else
    qa_sdl_window_set_size(mutation->window, mutation->width,
                           mutation->height);
}

typedef struct video_state {
  qa_sdl_window *(*current_window)(void *ctx);
  void *window_ctx;
  qa_cvar_registry *registry;
  void (*report)(void *ctx, const char *message);
  void *report_ctx;
  void (*mutate)(void *ctx, const qa_ui_window_mutation *mutation);
  void *mutate_ctx;
  qa_sdl_window *draft_window;
  char *width, *height;
  qa_ui_setting_binding items[8];
  qa_ui_choice *choices;
  char **choice_strings;
  size_t choice_count;
  char read_scratch[64];
} video_state;

static qa_sdl_window *video_window(video_state *s) {
  qa_sdl_window *current = s->current_window(s->window_ctx);
  if (current != s->draft_window) {
    s->draft_window = current;
    qa_sdl_size size = qa_sdl_window_logical_size(current);
    char buf[32];
    js_string(buf, sizeof buf, (double)size.width);
    set_str(&s->width, buf);
    js_string(buf, sizeof buf, (double)size.height);
    set_str(&s->height, buf);
  }
  return current;
}
static void video_mutate(video_state *s, qa_ui_window_mutation mutation) {
  if (s->mutate != NULL) s->mutate(s->mutate_ctx, &mutation);
  else qa_ui_apply_window_mutation(&mutation);
}
static bool valid_size(double w, double h) {
  const double limit = 9007199254740991.0; /* 2^53 - 1 */
  bool ints = isfinite(w) && isfinite(h) && w == trunc(w) && h == trunc(h)
      && fabs(w) <= limit && fabs(h) <= limit;
  return ints && w <= 8192 && h <= 8192 && w >= 320 && h >= 200;
}
/* TS resize(): the RangeError propagates to apply()'s catch in TS;
 * here callers that wrap with report use resize_reported. */
static const char *video_resize(video_state *s, double w, double h) {
  if (!valid_size(w, h))
    return "Use a width of 320-8192 and a height of 200-8192 pixels.";
  qa_ui_window_mutation mutation;
  memset(&mutation, 0, sizeof mutation);
  mutation.window = video_window(s);
  mutation.is_fullscreen = false;
  mutation.width = w;
  mutation.height = h;
  video_mutate(s, mutation);
  char buf[32];
  js_string(buf, sizeof buf, w);
  set_str(&s->width, buf);
  js_string(buf, sizeof buf, h);
  set_str(&s->height, buf);
  return NULL;
}
static void video_report(video_state *s, const char *error) {
  if (error != NULL) s->report(s->report_ctx, error);
}

typedef struct mode_size { double width, height; } mode_size;
static int mode_order(const void *a, const void *b) {
  const mode_size *x = a, *y = b;
  if (x->width != y->width) return x->width < y->width ? -1 : 1;
  if (x->height != y->height) return x->height < y->height ? -1 : 1;
  return 0;
}
static void video_free_choices(video_state *s) {
  for (size_t i = 0; i < s->choice_count * 2; i++) free(s->choice_strings[i]);
  free(s->choice_strings);
  free(s->choices);
  s->choices = NULL;
  s->choice_strings = NULL;
  s->choice_count = 0;
}
static const qa_ui_choice *video_choices(video_state *s, size_t *out_count) {
  static const mode_size fixed[] = {
    { 640, 480 }, { 800, 600 }, { 960, 600 }, { 1024, 768 },
    { 1280, 720 }, { 1280, 800 }, { 1600, 900 }, { 1920, 1080 },
    { 2560, 1440 }, { 3440, 1440 }, { 3840, 2160 },
  };
  qa_sdl_window *window = video_window(s);
  size_t mode_count = 0;
  qa_sdl_display_mode *modes =
      qa_sdl_window_display_modes(window, &mode_count);
  qa_sdl_size logical = qa_sdl_window_logical_size(window);
  size_t total = mode_count + 1 + sizeof fixed / sizeof fixed[0];
  mode_size *all = malloc(total * sizeof *all);
  if (all == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (size_t i = 0; i < mode_count; i++) {
    all[at].width = (double)modes[i].width;
    all[at].height = (double)modes[i].height;
    at++;
  }
  free(modes);
  all[at].width = (double)logical.width;
  all[at].height = (double)logical.height;
  at++;
  for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++)
    all[at++] = fixed[i];
  /* TS Map keyed by "WxH": later entries replace earlier ones, and
   * the surviving order is then sorted, so dedupe is enough. */
  size_t unique = 0;
  for (size_t i = 0; i < at; i++) {
    bool seen = false;
    for (size_t j = 0; j < unique; j++)
      if (all[j].width == all[i].width && all[j].height == all[i].height)
        seen = true;
    if (!seen) all[unique++] = all[i];
  }
  qsort(all, unique, sizeof *all, mode_order);
  video_free_choices(s);
  s->choices = calloc(unique, sizeof *s->choices);
  s->choice_strings = calloc(unique * 2, sizeof *s->choice_strings);
  if (s->choices == NULL || s->choice_strings == NULL)
    qa_range_error("Out of memory");
  s->choice_count = unique;
  for (size_t i = 0; i < unique; i++) {
    char wbuf[32], hbuf[32];
    js_string(wbuf, sizeof wbuf, all[i].width);
    js_string(hbuf, sizeof hbuf, all[i].height);
    char id[80], label[80];
    snprintf(id, sizeof id, "%sx%s", wbuf, hbuf);
    snprintf(label, sizeof label, "%s x %s", wbuf, hbuf);
    s->choice_strings[i * 2] = dup_str(id);
    s->choice_strings[i * 2 + 1] = dup_str(label);
    if (s->choice_strings[i * 2] == NULL
        || s->choice_strings[i * 2 + 1] == NULL)
      qa_range_error("Out of memory");
    s->choices[i].id = s->choice_strings[i * 2];
    s->choices[i].label = s->choice_strings[i * 2 + 1];
  }
  free(all);
  *out_count = unique;
  return s->choices;
}

static bool always_enabled(void *v) {
  (void)v;
  return true;
}
static double brightness_read(void *v) {
  video_state *s = v;
  return qa_cvar_registry_variable_value(s->registry, "r_gamma");
}
static void brightness_write(void *v, double value) {
  video_state *s = v;
  char buf[64];
  js_string(buf, sizeof buf, value);
  cvar_set(s->registry, "r_gamma", buf);
}
static bool brightness_reset_enabled(void *v) {
  video_state *s = v;
  return qa_cvar_registry_variable_value(s->registry, "r_gamma") != 1;
}
static void brightness_reset(void *v) {
  video_state *s = v;
  cvar_set(s->registry, "r_gamma", "1");
}
static bool fullscreen_read(void *v) {
  video_state *s = v;
  return qa_sdl_window_fullscreen(video_window(s));
}
static void fullscreen_write(void *v, bool value) {
  video_state *s = v;
  qa_ui_window_mutation mutation;
  memset(&mutation, 0, sizeof mutation);
  mutation.window = video_window(s);
  mutation.is_fullscreen = true;
  mutation.fullscreen = value;
  video_mutate(s, mutation);
}
static bool windowed_enabled(void *v) {
  video_state *s = v;
  return !qa_sdl_window_fullscreen(video_window(s));
}
static const char *resolution_read(void *v) {
  video_state *s = v;
  qa_sdl_size size = qa_sdl_window_logical_size(video_window(s));
  char wbuf[32], hbuf[32];
  js_string(wbuf, sizeof wbuf, (double)size.width);
  js_string(hbuf, sizeof hbuf, (double)size.height);
  snprintf(s->read_scratch, sizeof s->read_scratch, "%sx%s", wbuf, hbuf);
  return s->read_scratch;
}
static const qa_ui_choice *resolution_choices(void *v, size_t *out_count) {
  return video_choices(v, out_count);
}
static void resolution_write(void *v, const char *value) {
  video_state *s = v;
  size_t count = 0;
  const qa_ui_choice *choices = video_choices(s, &count);
  const qa_ui_choice *selected = NULL;
  for (size_t i = 0; i < count; i++)
    if (strcmp(choices[i].id, value) == 0) selected = &choices[i];
  if (selected == NULL) {
    video_report(s, "Unavailable resolution");
    return;
  }
  /* selected.id.split("x").map(Number) */
  const char *x = strchr(selected->id, 'x');
  if (x == NULL) {
    video_report(s, "Invalid resolution");
    return;
  }
  char wpart[32];
  size_t wlen = (size_t)(x - selected->id);
  if (wlen >= sizeof wpart) wlen = sizeof wpart - 1;
  memcpy(wpart, selected->id, wlen);
  wpart[wlen] = '\0';
  video_report(s, video_resize(s, js_number_of(wpart), js_number_of(x + 1)));
}
static const char *width_read(void *v) {
  video_state *s = v;
  video_window(s);
  return s->width;
}
static void width_write(void *v, const char *value) {
  video_state *s = v;
  video_window(s);
  set_str(&s->width, value);
}
static const char *height_read(void *v) {
  video_state *s = v;
  video_window(s);
  return s->height;
}
static void height_write(void *v, const char *value) {
  video_state *s = v;
  video_window(s);
  set_str(&s->height, value);
}
static bool custom_apply_enabled(void *v) {
  video_state *s = v;
  return !qa_sdl_window_fullscreen(video_window(s))
      && valid_size(js_number_of(s->width), js_number_of(s->height));
}
static void custom_apply(void *v) {
  video_state *s = v;
  video_report(s, video_resize(s, js_number_of(s->width),
                               js_number_of(s->height)));
}
static bool vsync_enabled(void *v) {
  video_state *s = v;
  return qa_sdl_window_backend(video_window(s)) == QA_SDL_BACKEND_GL
      && s->registry != NULL;
}
static bool vsync_read(void *v) {
  video_state *s = v;
  return qa_sdl_window_backend(video_window(s)) == QA_SDL_BACKEND_GL
      && (s->registry != NULL
              ? qa_cvar_registry_variable_value(s->registry,
                                                "r_swapInterval")
              : 0)
             != 0;
}
static void vsync_write(void *v, bool value) {
  video_state *s = v;
  if (qa_sdl_window_backend(video_window(s)) == QA_SDL_BACKEND_GL
      && s->registry != NULL)
    cvar_set(s->registry, "r_swapInterval", value ? "1" : "0");
}
static void video_teardown(void *state) {
  video_state *s = state;
  video_free_choices(s);
  free(s->width);
  free(s->height);
  free(s);
}

qa_ui_setting_bindings qa_ui_bind_native_video_settings(
    qa_sdl_window *(*current_window)(void *ctx), void *window_ctx,
    qa_cvar_registry *registry,
    void (*report)(void *ctx, const char *message), void *report_ctx,
    void (*mutate)(void *ctx, const qa_ui_window_mutation *mutation),
    void *mutate_ctx) {
  video_state *s = xcalloc_state(sizeof *s);
  s->current_window = current_window;
  s->window_ctx = window_ctx;
  s->registry = registry;
  s->report = report;
  s->report_ctx = report_ctx;
  s->mutate = mutate;
  s->mutate_ctx = mutate_ctx;
  /* TS evaluates currentWindow() eagerly for the draft size text. */
  video_window(s);
  size_t at = 0;
  if (registry != NULL) {
    s->items[at] = binding_base("ui:video:brightness", "Brightness",
                                "display", "slider", s);
    s->items[at].minimum = 0.5;
    s->items[at].maximum = 3;
    s->items[at].step = 0.05;
    s->items[at].enabled = always_enabled;
    s->items[at].read_num = brightness_read;
    s->items[at].write_num = brightness_write;
    at++;
    s->items[at] = binding_base("ui:video:brightness-reset",
                                "Reset brightness", "display", "button", s);
    s->items[at].enabled = brightness_reset_enabled;
    s->items[at].activate = brightness_reset;
    at++;
  }
  s->items[at] = binding_base("ui:video:fullscreen",
                              "Borderless fullscreen", "display",
                              "toggle", s);
  s->items[at].enabled = always_enabled;
  s->items[at].read_bool = fullscreen_read;
  s->items[at].write_bool = fullscreen_write;
  at++;
  s->items[at] = binding_base("ui:video:resolution", "Window resolution",
                              "display", "choice", s);
  s->items[at].enabled = windowed_enabled;
  s->items[at].read_str = resolution_read;
  s->items[at].choices = resolution_choices;
  s->items[at].write_str = resolution_write;
  at++;
  s->items[at] = binding_base("ui:video:custom-width",
                              "Custom width (320-8192)", "display",
                              "text-entry", s);
  s->items[at].maximum_length = 4;
  s->items[at].enabled = windowed_enabled;
  s->items[at].read_str = width_read;
  s->items[at].write_str = width_write;
  at++;
  s->items[at] = binding_base("ui:video:custom-height",
                              "Custom height (200-8192)", "display",
                              "text-entry", s);
  s->items[at].maximum_length = 4;
  s->items[at].enabled = windowed_enabled;
  s->items[at].read_str = height_read;
  s->items[at].write_str = height_write;
  at++;
  s->items[at] = binding_base("ui:video:custom-apply",
                              "Apply custom window size", "display",
                              "button", s);
  s->items[at].enabled = custom_apply_enabled;
  s->items[at].activate = custom_apply;
  at++;
  s->items[at] = binding_base("ui:video:vsync", "Vertical sync", "display",
                              "toggle", s);
  s->items[at].enabled = vsync_enabled;
  s->items[at].read_bool = vsync_read;
  s->items[at].write_bool = vsync_write;
  at++;
  return qa_ui_setting_bindings_own(s->items, at, s, video_teardown);
}

/* ---- NativeLanguageSettings ---- */

static const qa_ui_choice *language_choices(void *v, size_t *out_count) {
  qa_ui_native_language_settings *s = v;
  *out_count = s->available_count;
  return s->choice_view;
}
static const char *language_read(void *v) {
  qa_ui_native_language_settings *s = v;
  return s->selection;
}
static bool language_enabled(void *v) {
  qa_ui_native_language_settings *s = v;
  return !s->loading;
}
static void language_write(void *v, const char *value) {
  qa_ui_native_language_settings *s = v;
  /* TS write: this.select(value).catch(this.failed). An unknown id
   * is a rejected promise there, so it also goes to failed — but
   * the C select aborts for it (header contract); mirror by
   * reporting through failed here instead of aborting. */
  const qa_ui_language_choice *found = NULL;
  for (size_t i = 0; i < s->available_count; i++)
    if (strcmp(s->available[i].id, value) == 0) found = &s->available[i];
  if (found == NULL) {
    char message[256];
    snprintf(message, sizeof message, "Language is not installed: %s",
             value);
    if (s->failed != NULL) s->failed(s->failed_ctx, message);
    return;
  }
  const char *error = qa_ui_native_language_settings_select(s, value);
  if (error != NULL && s->failed != NULL) s->failed(s->failed_ctx, error);
}

bool qa_ui_native_language_settings_init(
    qa_ui_native_language_settings *settings,
    qa_localization_catalog *localization,
    const qa_ui_language_choice *available, size_t available_count,
    const char *current,
    void (*failed)(void *ctx, const char *message), void *failed_ctx) {
  memset(settings, 0, sizeof *settings);
  settings->localization = localization;
  settings->available_count = available_count;
  settings->failed = failed;
  settings->failed_ctx = failed_ctx;
  if (available_count > 0) {
    settings->available =
        malloc(available_count * sizeof *settings->available);
    if (settings->available == NULL) return false;
    memcpy(settings->available, available,
           available_count * sizeof *settings->available);
    settings->choice_view =
        malloc(available_count * sizeof *settings->choice_view);
    if (settings->choice_view == NULL) {
      free(settings->available);
      settings->available = NULL;
      return false;
    }
    for (size_t i = 0; i < available_count; i++) {
      settings->choice_view[i].id = available[i].id;
      settings->choice_view[i].label = available[i].label;
    }
  }
  settings->selection = dup_str(current);
  if (settings->selection == NULL) {
    free(settings->available);
    free(settings->choice_view);
    settings->available = NULL;
    settings->choice_view = NULL;
    return false;
  }
  settings->binding = binding_base("ui:language:selection", "Language",
                                   "language", "choice", settings);
  settings->binding.choices = language_choices;
  settings->binding.read_str = language_read;
  settings->binding.enabled = language_enabled;
  settings->binding.write_str = language_write;
  return true;
}

void qa_ui_native_language_settings_destroy(
    qa_ui_native_language_settings *settings) {
  free(settings->available);
  free(settings->choice_view);
  free(settings->selection);
  settings->available = NULL;
  settings->choice_view = NULL;
  settings->available_count = 0;
  settings->selection = NULL;
}

const char *qa_ui_native_language_settings_select(
    qa_ui_native_language_settings *settings, const char *id) {
  const qa_ui_language_choice *found = NULL;
  for (size_t i = 0; i < settings->available_count; i++)
    if (strcmp(settings->available[i].id, id) == 0)
      found = &settings->available[i];
  if (found == NULL)
    qa_range_error("Language is not installed: %s", id);
  if (settings->loading) return NULL;
  settings->loading = true;
  qa_loc_load_tier primary, fallback;
  memset(&primary, 0, sizeof primary);
  memset(&fallback, 0, sizeof fallback);
  const char *error = found->load(found->ctx, &primary, &fallback);
  if (error == NULL) {
    qa_localization_table_load_ordered(&settings->localization->table,
                                       primary, fallback, NULL);
    set_str(&settings->selection, id);
  }
  settings->loading = false;
  return error;
}

const qa_ui_setting_binding *qa_ui_native_language_settings_binding(
    qa_ui_native_language_settings *settings) {
  return &settings->binding;
}
