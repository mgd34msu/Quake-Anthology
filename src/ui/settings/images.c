/* Port of src/ui/settings/images.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/images.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"

/* The five usage toggles read/write through the mask text binding;
 * TS bitwise ops are int32. */
typedef struct usage_ctx {
  qa_ui_setting_binding *mask;
  int32_t bit;
} usage_ctx;
static bool usage_read(void *v) {
  usage_ctx *c = v;
  double value = qa_input_js_parse_number(
      c->mask->read_str(c->mask->ctx));
  return (qa_input_to_int32(value) & c->bit) != 0;
}
static void usage_write(void *v, bool on) {
  usage_ctx *c = v;
  int32_t current = qa_input_to_int32(
      qa_input_js_parse_number(c->mask->read_str(c->mask->ctx)));
  int32_t next = on ? current | c->bit : current & ~c->bit;
  char *text = qa_input_js_number((double)next);
  if (text == NULL) qa_range_error("Out of memory");
  c->mask->write_str(c->mask->ctx, text);
  free(text);
}
static bool usage_enabled(void *v) {
  usage_ctx *c = v;
  return c->mask->enabled(c->mask->ctx);
}

typedef struct image_owner {
  qa_ui_setting_binding items[8];
  usage_ctx usages[5];
  char *ids[5];
} image_owner;
static void image_teardown(void *v) {
  image_owner *o = v;
  qa_ui_cvar_setting_binding_dispose(&o->items[0]);
  qa_ui_cvar_setting_binding_dispose(&o->items[1]);
  qa_ui_cvar_setting_binding_dispose(&o->items[2]);
  for (int i = 0; i < 5; i++) free(o->ids[i]);
  free(o);
}

qa_ui_setting_bindings qa_ui_bind_image_settings(qa_cvar_registry *registry) {
  static const qa_ui_choice replacement_choices[3] = {
    { "0", "Disabled" }, { "1", "Replace classic formats" },
    { "2", "Replace all formats" }
  };
  static const struct { const char *usage, *label; int32_t bit; } usages[5] = {
    { "skin", "Replace model skins", 1 },
    { "sprite", "Replace sprites", 2 },
    { "wall", "Replace wall textures", 4 },
    { "picture", "Replace pictures", 8 },
    { "sky", "Replace sky images", 16 },
  };
  image_owner *o = calloc(1, sizeof *o);
  if (o == NULL) qa_range_error("Out of memory");
  qa_ui_cvar_setting_spec spec;
  memset(&spec, 0, sizeof spec);
  spec.category = "video";
  spec.name = "r_override_textures";
  spec.label = "Replacement images";
  spec.kind = "choice";
  spec.choices = replacement_choices;
  spec.choice_count = 3;
  if (!qa_ui_bind_cvar_setting(registry, &spec, NULL, &o->items[0]))
    qa_range_error("Out of memory");
  memset(&spec, 0, sizeof spec);
  spec.category = "video";
  spec.name = "r_texture_formats";
  spec.label = "Formats (source = default)";
  spec.kind = "text-entry";
  spec.maximum_length = 128;
  if (!qa_ui_bind_cvar_setting(registry, &spec, NULL, &o->items[1]))
    qa_range_error("Out of memory");
  memset(&spec, 0, sizeof spec);
  spec.category = "video";
  spec.name = "r_texture_overrides";
  spec.label = "Image types";
  spec.kind = "text-entry";
  spec.maximum_length = 12;
  if (!qa_ui_bind_cvar_setting(registry, &spec, NULL, &o->items[2]))
    qa_range_error("Out of memory");
  if (strcmp(o->items[2].kind, "text-entry") != 0)
    qa_range_error("Image mask requires a numeric text binding");
  for (int i = 0; i < 5; i++) {
    o->usages[i].mask = &o->items[2];
    o->usages[i].bit = usages[i].bit;
    size_t size = strlen("ui:settings:image-") + strlen(usages[i].usage) + 1;
    o->ids[i] = malloc(size);
    if (o->ids[i] == NULL) qa_range_error("Out of memory");
    snprintf(o->ids[i], size, "ui:settings:image-%s", usages[i].usage);
    qa_ui_setting_binding *b = &o->items[3 + i];
    memset(b, 0, sizeof *b);
    b->id = o->ids[i];
    b->label = usages[i].label;
    b->category = "video";
    b->kind = "toggle";
    b->ctx = &o->usages[i];
    b->enabled = usage_enabled;
    b->read_bool = usage_read;
    b->write_bool = usage_write;
  }
  return qa_ui_setting_bindings_own(o->items, 8, o, image_teardown);
}

typedef struct model_owner { qa_ui_setting_binding items[4]; } model_owner;
static void model_teardown(void *v) {
  model_owner *o = v;
  for (int i = 0; i < 4; i++)
    qa_ui_cvar_setting_binding_dispose(&o->items[i]);
  free(o);
}
qa_ui_setting_bindings qa_ui_bind_model_settings(qa_cvar_registry *registry) {
  static const struct { const char *name, *label; } controls[3] = {
    { "r_enhancedmodels", "Q1 enhanced models" },
    { "gl_md5_load", "Load Q2 enhanced models" },
    { "gl_md5_use", "Draw Q2 enhanced models" },
  };
  model_owner *o = calloc(1, sizeof *o);
  if (o == NULL) qa_range_error("Out of memory");
  for (int i = 0; i < 3; i++) {
    qa_ui_cvar_setting_spec spec;
    memset(&spec, 0, sizeof spec);
    spec.name = controls[i].name;
    spec.label = controls[i].label;
    spec.category = "video";
    spec.kind = "toggle";
    if (!qa_ui_bind_cvar_setting(registry, &spec, NULL, &o->items[i]))
      qa_range_error("Out of memory");
  }
  qa_ui_cvar_setting_spec spec;
  memset(&spec, 0, sizeof spec);
  spec.name = "r_model_distance";
  spec.label = "Model range (map units)";
  spec.category = "video";
  spec.kind = "text-entry";
  spec.maximum_length = 16;
  spec.submit_only = true;
  if (!qa_ui_bind_cvar_setting(registry, &spec, NULL, &o->items[3]))
    qa_range_error("Out of memory");
  return qa_ui_setting_bindings_own(o->items, 4, o, model_teardown);
}
