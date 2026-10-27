/* Port of src/ui/settings/action-catalog.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/action_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/bindings.h"
#include "qa/input/support.h"

typedef struct catalog_owner {
  qa_ui_binding_action *items;
  char **strings;
  size_t string_count;
  const qa_weapon_binding_item *bindables;
  size_t bindable_count;
  struct wheel_ctx { const char *text; } wheels[2];
  struct item_ctx { struct catalog_owner *owner; const char *id; } *item_ctxs;
} catalog_owner;

static char *own(catalog_owner *o, const char *text) {
  char *copy = qa_input_dup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  char **next = realloc(o->strings,
                        (o->string_count + 1) * sizeof *next);
  if (next == NULL) qa_range_error("Out of memory");
  o->strings = next;
  o->strings[o->string_count++] = copy;
  return copy;
}
static bool wheel_matches(void *v, qa_input_binding_target target) {
  struct wheel_ctx *c = v;
  if (target.is_action) return false;
  char *canonical = qa_canonical_wheel_command(target.text);
  bool equal = canonical != NULL && strcmp(canonical, c->text) == 0;
  free(canonical);
  return equal;
}
static bool scores_matches(void *ctx, qa_input_binding_target target) {
  (void)ctx;
  return !target.is_action
         && (strcmp(target.text, "+scores") == 0
             || strcmp(target.text, "+showscores") == 0);
}
static bool item_matches(void *v, qa_input_binding_target target) {
  struct item_ctx *c = v;
  if (target.is_action) return false;
  const char *found = qa_weapon_binding_item_for_text(
      target.text, c->owner->bindables, c->owner->bindable_count);
  return found != NULL && strcmp(found, c->id) == 0;
}

void qa_ui_binding_actions_free(qa_ui_binding_actions *actions) {
  if (actions == NULL || actions->owner == NULL) return;
  catalog_owner *o = actions->owner;
  for (size_t i = 0; i < o->string_count; i++) free(o->strings[i]);
  free(o->strings);
  free(o->item_ctxs);
  free(o->items);
  free(o);
  actions->items = NULL;
  actions->count = 0;
  actions->owner = NULL;
}

qa_ui_binding_actions qa_ui_shared_binding_actions(
    qa_command_dialect dialect, const qa_weapon_binding_item *items,
    size_t item_count, qa_ui_binding_capabilities capabilities) {
  bool q1 = dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE
            || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  const struct { const char *id, *label, *text; } rows[18] = {
    { "forward", "Move forward", "+forward" },
    { "back", "Move back", "+back" },
    { "left", "Strafe left", "+moveleft" },
    { "right", "Strafe right", "+moveright" },
    { "jump", "Jump / swim up", q1 ? "+jump" : "+moveup" },
    { "down", "Crouch / swim down", "+movedown" },
    { "walk", "Walk / run modifier", "+speed" },
    { "turn-left", "Turn left", "+left" },
    { "turn-right", "Turn right", "+right" },
    { "look-up", "Look up", "+lookup" },
    { "look-down", "Look down", "+lookdown" },
    { "attack", "Fire primary weapon", "+attack" },
    { "use", "Use / activate", "+use" },
    { "next-weapon", "Next weapon", "weapnext" },
    { "previous-weapon", "Previous weapon", "weapprev" },
    { "weapon-wheel", "Weapon wheel", "+weaponwheel" },
    { "powerup-wheel", "Powerup wheel", "+powerupwheel" },
    { "console", "Toggle console", "toggleconsole" },
  };
  size_t total = 18 + 1 + (capabilities.chat ? 2 : 0)
                 + (capabilities.offhand_grapple ? 1 : 0)
                 + (capabilities.offhand_grenades ? 1 : 0) + item_count;
  catalog_owner *o = calloc(1, sizeof *o);
  if (o == NULL) qa_range_error("Out of memory");
  o->items = calloc(total, sizeof *o->items);
  if (o->items == NULL) qa_range_error("Out of memory");
  o->bindables = items;
  o->bindable_count = item_count;
  if (item_count > 0) {
    o->item_ctxs = calloc(item_count, sizeof *o->item_ctxs);
    if (o->item_ctxs == NULL) qa_range_error("Out of memory");
  }
  size_t at = 0;
  int wheel = 0;
  for (int i = 0; i < 18; i++) {
    qa_ui_binding_action *a = &o->items[at++];
    a->id = rows[i].id;
    a->label = rows[i].label;
    a->target.is_action = false;
    a->target.text = rows[i].text;
    if (strcmp(rows[i].id, "weapon-wheel") == 0
        || strcmp(rows[i].id, "powerup-wheel") == 0) {
      o->wheels[wheel].text = rows[i].text;
      a->ctx = &o->wheels[wheel];
      a->matches = wheel_matches;
      wheel++;
    }
  }
  {
    qa_ui_binding_action *a = &o->items[at++];
    a->id = "scores";
    a->label = "Show scores";
    a->target.is_action = false;
    a->target.text =
        capabilities.score_command != NULL ? capabilities.score_command
                                           : "+scores";
    a->matches = scores_matches;
  }
  if (capabilities.chat) {
    qa_ui_binding_action *a = &o->items[at++];
    a->id = "chat";
    a->label = "Chat";
    a->target.text = "messagemode";
    a = &o->items[at++];
    a->id = "team-chat";
    a->label = "Team chat";
    a->target.text = "messagemode2";
  }
  if (capabilities.offhand_grapple) {
    qa_ui_binding_action *a = &o->items[at++];
    a->id = "grapple";
    a->label = "Offhand grapple (hold)";
    a->target.text = "+grapple";
  }
  if (capabilities.offhand_grenades) {
    qa_ui_binding_action *a = &o->items[at++];
    a->id = "grenade";
    a->label = "Cook / throw offhand grenade";
    a->target.text = "+grenade";
  }
  for (size_t i = 0; i < item_count; i++) {
    char buffer[512];
    qa_ui_binding_action *a = &o->items[at];
    snprintf(buffer, sizeof buffer, "item:%s", items[i].id);
    a->id = own(o, buffer);
    snprintf(buffer, sizeof buffer, "%s %s",
             items[i].kind == QA_WEAPON_BINDING_WEAPON ? "Select" : "Use",
             items[i].label);
    a->label = own(o, buffer);
    snprintf(buffer, sizeof buffer, "use %s", items[i].id);
    a->target.is_action = false;
    a->target.text = own(o, buffer);
    o->item_ctxs[i].owner = o;
    o->item_ctxs[i].id = items[i].id;
    a->ctx = &o->item_ctxs[i];
    a->matches = item_matches;
    at++;
  }
  qa_ui_binding_actions out = { o->items, at, o };
  return out;
}
