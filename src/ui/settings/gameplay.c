/* Port of src/ui/settings/gameplay.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/gameplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"

/* Q1 netquake shirt/pants color choice over the packed color cvar. */
typedef struct color_ctx {
  qa_cvar_registry *client;
  const char *name;
  int shift;
  qa_ui_choice colors[14];
  char ids[14][4];
  char scratch[8];
} color_ctx;
static bool color_enabled(void *ctx) { (void)ctx; return true; }
static const char *color_read(void *v) {
  color_ctx *c = v;
  char *text = qa_cvar_registry_variable_string(c->client, c->name);
  if (text == NULL) qa_range_error("Out of memory");
  int32_t value = qa_native_atoi(text);
  free(text);
  int field = (value >> c->shift) & 15;
  if (field > 13) field = 13;
  char *rendered = qa_input_js_number((double)field);
  if (rendered == NULL) qa_range_error("Out of memory");
  snprintf(c->scratch, sizeof c->scratch, "%s", rendered);
  free(rendered);
  return c->scratch;
}
static void color_write(void *v, const char *value) {
  color_ctx *c = v;
  bool known = false;
  for (int i = 0; i < 14; i++)
    if (strcmp(c->colors[i].id, value) == 0) known = true;
  if (!known) qa_range_error("Unknown Quake color");
  char *text = qa_cvar_registry_variable_string(c->client, c->name);
  if (text == NULL) qa_range_error("Out of memory");
  int32_t previous = qa_native_atoi(text);
  free(text);
  int32_t next = (previous & ~(15 << c->shift))
                 | ((int32_t)qa_input_js_parse_number(value) << c->shift);
  char *rendered = qa_input_js_number((double)next);
  if (rendered == NULL) qa_range_error("Out of memory");
  qa_cvar_snapshot out;
  if (qa_cvar_registry_set(c->client, c->name, rendered, false, &out))
    qa_cvar_read_free(&out);
  free(rendered);
}
static const qa_ui_choice *color_choices(void *v, size_t *out_count) {
  color_ctx *c = v;
  *out_count = 14;
  return c->colors;
}
static bool never_enabled(void *ctx) { (void)ctx; return false; }
static void noop_activate(void *ctx) { (void)ctx; }

typedef struct gameplay_owner {
  qa_ui_setting_binding *items;
  size_t count, capacity;
  color_ctx colors[2];
  bool is_cvar[40];
} gameplay_owner;
static void gameplay_teardown(void *v) {
  gameplay_owner *o = v;
  for (size_t i = 0; i < o->count && i < 40; i++)
    if (o->is_cvar[i]) qa_ui_cvar_setting_binding_dispose(&o->items[i]);
  free(o->items);
  free(o);
}
static qa_ui_setting_binding *gameplay_push(gameplay_owner *o, bool is_cvar) {
  if (o->count == o->capacity) {
    size_t cap = o->capacity == 0 ? 8 : o->capacity * 2;
    qa_ui_setting_binding *next = realloc(o->items, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    o->items = next;
    o->capacity = cap;
  }
  if (o->count < 40) o->is_cvar[o->count] = is_cvar;
  return &o->items[o->count++];
}
static bool registry_has(qa_cvar_registry *registry, const char *name) {
  qa_cvar_read read;
  bool found = qa_cvar_registry_find(registry, name, &read);
  if (found) qa_cvar_read_free(&read);
  return found;
}
static void gameplay_cvar(gameplay_owner *o, qa_cvar_registry *registry,
                          qa_ui_cvar_setting_spec *spec) {
  qa_ui_setting_binding *slot = gameplay_push(o, true);
  if (!qa_ui_bind_cvar_setting(registry, spec, NULL, slot))
    qa_range_error("Out of memory");
}

qa_ui_setting_bindings qa_ui_bind_gameplay_settings(
    const qa_ui_gameplay_settings_source *source) {
  gameplay_owner *o = calloc(1, sizeof *o);
  if (o == NULL) qa_range_error("Out of memory");
  qa_cvar_registry *client =
      source->client != NULL ? source->client : source->cvars;
  static const struct { const char *name, *label; } toggles[5] = {
    { "sv_autosave", "Autosave on level load" },
    { "cg_drawGun", "Draw weapon" },
    { "cg_simpleItems", "Simple items" },
    { "cg_marks", "Wall marks" },
    { "cg_drawCrosshairNames", "Target names" },
  };
  for (int i = 0; i < 5; i++) {
    qa_cvar_registry *cvars =
        strcmp(toggles[i].name, "sv_autosave") == 0 ? source->cvars : client;
    if (!registry_has(cvars, toggles[i].name)) continue;
    qa_ui_cvar_setting_spec spec;
    memset(&spec, 0, sizeof spec);
    spec.name = toggles[i].name;
    spec.label = toggles[i].label;
    spec.kind = "toggle";
    spec.category = "accessibility";
    gameplay_cvar(o, cvars, &spec);
  }
  qa_command_dialect dialect = qa_cvar_registry_dialect(client);
  const char *auto_switch =
      dialect == QA_COMMAND_DIALECT_Q3 ? "cg_autoswitch"
      : dialect == QA_COMMAND_DIALECT_Q2_RERELEASE ? "autoswitch"
      : dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE
              || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD
          ? "qts_weapon_autoswitch"
          : NULL;
  if (auto_switch != NULL && registry_has(client, auto_switch)) {
    bool source_owned = strcmp(auto_switch, "qts_weapon_autoswitch") == 0
                        && !(source->has_weapon_pickup_policy
                             && source->weapon_pickup_shared);
    /* TS tests `weaponPickupPolicy !== "shared"`: absent is not
     * "shared", so source-owned shows unless explicitly shared. */
    if (strcmp(auto_switch, "qts_weapon_autoswitch") == 0 && source_owned) {
      qa_ui_setting_binding *b = gameplay_push(o, false);
      memset(b, 0, sizeof *b);
      b->id = "ui:gameplay:source-weapon-switching";
      b->label = "This game controls weapon pickup switching";
      b->kind = "button";
      b->category = "input";
      b->group = "Weapons";
      b->enabled = never_enabled;
      b->activate = noop_activate;
    } else if (dialect == QA_COMMAND_DIALECT_Q3) {
      qa_ui_cvar_setting_spec spec;
      memset(&spec, 0, sizeof spec);
      spec.name = auto_switch;
      spec.label = "Switch to picked-up weapons";
      spec.kind = "toggle";
      spec.category = "input";
      spec.group = "Weapons";
      gameplay_cvar(o, client, &spec);
    } else {
      static const qa_ui_choice q2_choices[4] = {
        { "0", "Smart" }, { "1", "Always" },
        { "2", "Except consumable weapons" }, { "3", "Never" }
      };
      static const qa_ui_choice q1_choices[3] = {
        { "always", "Always" }, { "new", "New weapons" },
        { "never", "Never" }
      };
      qa_ui_cvar_setting_spec spec;
      memset(&spec, 0, sizeof spec);
      spec.name = auto_switch;
      spec.label = "Switch to picked-up weapons";
      spec.kind = "choice";
      spec.category = "input";
      spec.group = "Weapons";
      if (dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) {
        spec.choices = q2_choices;
        spec.choice_count = 4;
      } else {
        spec.choices = q1_choices;
        spec.choice_count = 3;
      }
      gameplay_cvar(o, client, &spec);
    }
  }
  const char *player_name =
      dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE
              && !registry_has(client, "name")
          ? "_cl_name"
          : "name";
  typedef struct name_label { const char *name, *label; } name_label;
  const name_label identity_q3[3] = {
    { player_name, "Player name" }, { "model", "Player model / skin" },
    { "headmodel", "Head model / skin" }
  };
  const name_label identity_q2[2] = {
    { player_name, "Player name" },
    { "skin", "Player skin (model/skin)" }
  };
  const name_label identity_q1[1] = {
    { player_name, "Player name" }
  };
  const name_label *identity = identity_q1;
  size_t identity_count = 1;
  if (dialect == QA_COMMAND_DIALECT_Q3) {
    identity = identity_q3;
    identity_count = 3;
  } else if (dialect == QA_COMMAND_DIALECT_Q2_CLASSIC
             || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) {
    identity = identity_q2;
    identity_count = 2;
  }
  for (size_t i = 0; i < identity_count; i++) {
    if (!registry_has(client, identity[i].name)) continue;
    qa_ui_cvar_setting_spec spec;
    memset(&spec, 0, sizeof spec);
    spec.name = identity[i].name;
    spec.label = identity[i].label;
    spec.kind = "text-entry";
    spec.maximum_length =
        dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE ? 15 : 63;
    spec.submit_only = true;
    spec.category = "network";
    gameplay_cvar(o, client, &spec);
  }
  color_ctx *colors = &o->colors[0];
  for (int slot = 0; slot < 2; slot++) {
    for (int i = 0; i < 14; i++) {
      snprintf(o->colors[slot].ids[i], sizeof o->colors[slot].ids[i],
               "%d", i);
      o->colors[slot].colors[i].id = o->colors[slot].ids[i];
      o->colors[slot].colors[i].label = o->colors[slot].ids[i];
    }
  }
  colors[0].client = client;
  colors[1].client = client;
  if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE) {
    const char *name = registry_has(client, "color") ? "color" : "_cl_color";
    if (registry_has(client, name)) {
      static const struct { const char *id, *label; int shift; } fields[2] = {
        { "shirt", "Shirt color", 4 }, { "pants", "Pants color", 0 }
      };
      for (int i = 0; i < 2; i++) {
        color_ctx *c = &o->colors[i];
        c->name = name;
        c->shift = fields[i].shift;
        qa_ui_setting_binding *b = gameplay_push(o, false);
        memset(b, 0, sizeof *b);
        b->id = i == 0 ? "ui:gameplay:shirt-color"
                       : "ui:gameplay:pants-color";
        b->label = fields[i].label;
        b->kind = "choice";
        b->category = "network";
        b->ctx = c;
        b->enabled = color_enabled;
        b->choices = color_choices;
        b->read_str = color_read;
        b->write_str = color_write;
      }
    }
  } else if (dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD) {
    static const struct { const char *name, *label; } fields[2] = {
      { "topcolor", "Shirt color" }, { "bottomcolor", "Pants color" }
    };
    for (int i = 0; i < 2; i++) {
      if (!registry_has(client, fields[i].name)) continue;
      qa_ui_cvar_setting_spec spec;
      memset(&spec, 0, sizeof spec);
      spec.name = fields[i].name;
      spec.label = fields[i].label;
      spec.kind = "choice";
      spec.choices = o->colors[0].colors;
      spec.choice_count = 14;
      spec.category = "network";
      gameplay_cvar(o, client, &spec);
    }
  } else if (dialect == QA_COMMAND_DIALECT_Q2_CLASSIC
             || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) {
    if (registry_has(client, "hand")) {
      static const qa_ui_choice hands[3] = {
        { "0", "Right" }, { "1", "Left" }, { "2", "Center" }
      };
      qa_ui_cvar_setting_spec spec;
      memset(&spec, 0, sizeof spec);
      spec.name = "hand";
      spec.label = "Weapon hand";
      spec.kind = "choice";
      spec.choices = hands;
      spec.choice_count = 3;
      spec.category = "network";
      gameplay_cvar(o, client, &spec);
    }
    if (registry_has(client, "fov")) {
      qa_ui_cvar_setting_spec spec;
      memset(&spec, 0, sizeof spec);
      spec.name = "fov";
      spec.label = "Field of view";
      spec.kind = "slider";
      spec.minimum = 1;
      spec.maximum = 160;
      spec.step = 1;
      spec.category = "video";
      gameplay_cvar(o, client, &spec);
    }
  }
  return qa_ui_setting_bindings_own(o->items, o->count, o, gameplay_teardown);
}

void qa_ui_reset_gameplay_settings(
    const qa_ui_gameplay_settings_source *source) {
  qa_cvar_registry *client =
      source->client != NULL ? source->client : source->cvars;
  qa_ui_setting_bindings bindings = qa_ui_bind_gameplay_settings(source);
  for (size_t i = 0; i < bindings.count; i++) {
    const char *id = bindings.items[i].id;
    const char *name;
    if (strcmp(id, "ui:gameplay:shirt-color") == 0
        || strcmp(id, "ui:gameplay:pants-color") == 0) {
      name = registry_has(client, "color") ? "color" : "_cl_color";
    } else if (strncmp(id, "ui:settings:", 12) == 0) {
      name = id + 12;
    } else {
      name = id; /* TS replace() leaves other ids whole. */
    }
    qa_cvar_registry *cvars =
        strcmp(name, "sv_autosave") == 0 ? source->cvars : client;
    qa_cvar_read read;
    if (qa_cvar_registry_find(cvars, name, &read)) {
      qa_cvar_snapshot out;
      if (qa_cvar_registry_set(cvars, name, read.reset_value, false, &out))
        qa_cvar_read_free(&out);
      qa_cvar_read_free(&read);
    }
  }
  qa_ui_setting_bindings_free(&bindings);
}
