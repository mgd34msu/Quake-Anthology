/* Port of src/app/bootstrap/seat-hud-state.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/seat_hud_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
typedef struct bar { double slot; char *id; char *label; qa_ui_hud_health_bar value; } bar;
typedef struct help_entry { double slot; char *text; } help_entry;
typedef struct client_entry { double slot; char *name; double frags, team; bool observer; } client_entry;
typedef struct poi {
  double key, flags, width, height;
  char *content; char *path; qa_resource_id image;
  qa_vec3 origin; double expires; double color; qa_vec4 tint;
} poi;
typedef struct inv_item { char *id; char *label; qa_ui_hud_inventory_item value; } inv_item;
struct qa_app_seat_source_hud {
  qa_actor_id actor;
  char *(*localize)(void *ctx, qa_content_id content, const char *text, const char *const *args, size_t arg_count);
  void *localize_ctx;
  qa_app_simulation_presentation_event pending[8192]; size_t pending_head, pending_count;
  struct { char *text; double seconds; } prints[1024]; size_t print_head, print_count;
  qa_app_objective_print *drained; size_t drained_count, drained_cap;
  char *objective;
  bool mission_visible;
  bar *bars; size_t bar_count, bar_cap;
  help_entry *help_text; size_t help_count, help_cap;
  bool help_visible;
  inv_item *inventory; size_t inventory_count; bool inventory_null;
  bool scores_held, score_visible;
  char **score_rows; size_t score_row_count;
  client_entry *clients; size_t client_count, client_cap;
  bool has_report; char **report_lines; size_t report_line_count; double report_ready;
  poi pois[32]; size_t poi_count;
  bool has_path; qa_vec3 path_origin, path_direction; double path_expires;
  double flags, rune_items;
  bool has_help_computer; char *help_primary, *help_secondary;
  double red, blue; bool ctf;
  bool has_pickup; char *pickup_name, *pickup_path; char *pickup_content; qa_resource_id pickup_icon; double pickup_expires;
  qa_ui_hud_damage_indicator damage[32]; size_t damage_count;
  bool has_damage_picture; char *damage_content; qa_resource_id damage_image; double damage_width, damage_height;
  char *capture; double capture_until;
  /* Presentation scratch. */
  qa_ui_hud_prompt *prompt_scratch; size_t prompt_cap;
  char **string_scratch; size_t string_scratch_count, string_scratch_cap;
  qa_ui_hud_health_bar *bar_scratch;
  qa_ui_hud_inventory_item *inv_scratch;
  qa_ui_hud_damage_indicator *damage_scratch;
  qa_ui_hud_point_of_interest *point_scratch;
  qa_ui_hud_help help_scratch;
  qa_ui_hud_objective objective_scratch;
  qa_app_hud_pickup pickup_scratch;
};
static char *xdup(const char *s) { char *r = qa_input_dup(s ? s : ""); if (!r) qa_range_error("Out of memory"); return r; }
static void set_str(char **slot, const char *value) { free(*slot); *slot = xdup(value); }
static char *scratch_str(qa_app_seat_source_hud *h, const char *value) {
  if (h->string_scratch_count == h->string_scratch_cap) { h->string_scratch_cap = h->string_scratch_cap ? h->string_scratch_cap * 2 : 16; h->string_scratch = realloc(h->string_scratch, h->string_scratch_cap * sizeof *h->string_scratch); if (!h->string_scratch) qa_range_error("Out of memory"); }
  char *copy = xdup(value);
  h->string_scratch[h->string_scratch_count++] = copy;
  return copy;
}
static void clear_scratch(qa_app_seat_source_hud *h) {
  for (size_t i = 0; i < h->string_scratch_count; i++) free(h->string_scratch[i]);
  h->string_scratch_count = 0;
}
static bool same_actor(qa_actor_id a, qa_actor_id b) { return qa_same_actor(a, b); }
qa_app_seat_source_hud *qa_app_seat_source_hud_create(qa_actor_id actor,
    char *(*localize)(void *ctx, qa_content_id content, const char *text, const char *const *args, size_t arg_count), void *localize_ctx) {
  qa_app_seat_source_hud *h = calloc(1, sizeof *h);
  if (!h) return NULL;
  h->actor = actor;
  h->localize = localize; h->localize_ctx = localize_ctx;
  h->objective = xdup("");
  h->capture = xdup("");
  h->inventory_null = true;
  return h;
}
void qa_app_seat_source_hud_destroy(qa_app_seat_source_hud *h) {
  if (!h) return;
  free(h->objective); free(h->capture);
  free(h->help_primary); free(h->help_secondary);
  free(h->pickup_name); free(h->pickup_path); free(h->pickup_content);
  free(h->damage_content);
  for (size_t i = 0; i < h->bar_count; i++) { free(h->bars[i].id); free(h->bars[i].label); }
  free(h->bars);
  for (size_t i = 0; i < h->help_count; i++) free(h->help_text[i].text);
  free(h->help_text);
  for (size_t i = 0; i < h->inventory_count; i++) { free(h->inventory[i].id); free(h->inventory[i].label); }
  free(h->inventory);
  for (size_t i = 0; i < h->score_row_count; i++) free(h->score_rows[i]);
  free(h->score_rows);
  for (size_t i = 0; i < h->client_count; i++) free(h->clients[i].name);
  free(h->clients);
  for (size_t i = 0; i < h->report_line_count; i++) free(h->report_lines[i]);
  free(h->report_lines);
  for (size_t i = 0; i < h->poi_count; i++) { free(h->pois[i].content); free(h->pois[i].path); }
  for (size_t i = 0; i < h->print_count; i++) free(h->prints[(h->print_head + i) % 1024].text);
  for (size_t i = 0; i < h->drained_count; i++) free((void *)h->drained[i].text);
  free(h->drained);
  clear_scratch(h);
  free(h->string_scratch); free(h->prompt_scratch); free(h->bar_scratch); free(h->inv_scratch); free(h->damage_scratch); free(h->point_scratch);
  free(h);
}
const qa_app_objective_print *qa_app_seat_source_hud_drain_objective_prints(qa_app_seat_source_hud *h, size_t *out_count) {
  for (size_t i = 0; i < h->drained_count; i++) free((void *)h->drained[i].text);
  h->drained_count = 0;
  while (h->print_count > 0) {
    if (h->drained_count == h->drained_cap) { h->drained_cap = h->drained_cap ? h->drained_cap * 2 : 8; h->drained = realloc(h->drained, h->drained_cap * sizeof *h->drained); if (!h->drained) qa_range_error("Out of memory"); }
    size_t at = h->print_head;
    h->drained[h->drained_count].text = h->prints[at].text;
    h->drained[h->drained_count].seconds = h->prints[at].seconds;
    h->drained_count++;
    h->print_head = (h->print_head + 1) % 1024;
    h->print_count--;
  }
  *out_count = h->drained_count;
  return h->drained;
}
bool qa_app_seat_source_hud_inventory_items(qa_app_seat_source_hud *h, const qa_ui_hud_inventory_item **out, size_t *out_count) {
  if (h->inventory_null) return false;
  free(h->inv_scratch);
  h->inv_scratch = calloc(h->inventory_count ? h->inventory_count : 1, sizeof *h->inv_scratch);
  if (!h->inv_scratch) qa_range_error("Out of memory");
  for (size_t i = 0; i < h->inventory_count; i++) h->inv_scratch[i] = h->inventory[i].value;
  *out = h->inv_scratch;
  *out_count = h->inventory_count;
  return true;
}
void qa_app_seat_source_hud_scores(qa_app_seat_source_hud *h, bool down) { h->scores_held = down; }
static void admit_print(qa_app_seat_source_hud *h, const char *text, double seconds) {
  if (h->print_count == 1024) qa_range_error("Event ring capacity exhausted (%zu undrained records); drain before admitting more", (size_t)1024);
  size_t at = (h->print_head + h->print_count) % 1024;
  h->prints[at].text = xdup(text);
  h->prints[at].seconds = seconds;
  h->print_count++;
}
static qa_vec3 normalize3(qa_vec3 v) {
  double length = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (length == 0) length = 1;
  qa_vec3 r = {v.x / length, v.y / length, v.z / length};
  return r;
}
static void apply(qa_app_seat_source_hud *h, qa_app_simulation_presentation_event source) {
  char buf[4096];
  if (strcmp(source.kind, "q2") == 0 && strcmp(source.event_kind, "pickup") == 0 && same_actor(source.player, h->actor)) {
    h->has_pickup = true;
    set_str(&h->pickup_name, source.name);
    set_str(&h->pickup_content, source.content);
    set_str(&h->pickup_path, source.icon);
    h->pickup_icon = NULL;
    h->pickup_expires = source.seconds * 1000 + 3000;
  }
  if (strcmp(source.kind, "q2") == 0 && strcmp(source.event_kind, "damage-indicator") == 0 && same_actor(source.actor, h->actor)) {
    bool was_full = h->damage_count >= 32;
    qa_ui_hud_damage_indicator entry; memset(&entry, 0, sizeof entry);
    entry.origin = source.origin; entry.amount = source.amount; entry.expires_milliseconds = source.seconds * 1000 + 800;
    if (h->damage_count < 32) h->damage[h->damage_count++] = entry;
    else { memmove(&h->damage[0], &h->damage[1], 31 * sizeof entry); h->damage[31] = entry; }
    if (!was_full && h->damage_count > 8) { memmove(&h->damage[0], &h->damage[1], (h->damage_count - 1) * sizeof entry); h->damage_count--; }
  }
  if (strcmp(source.kind, "q2") == 0 && strcmp(source.event_kind, "help") == 0) {
    bool found = false;
    for (size_t i = 0; i < h->help_count; i++) if (h->help_text[i].slot == source.slot) { set_str(&h->help_text[i].text, source.text); found = true; }
    if (!found) {
      if (h->help_count == h->help_cap) { h->help_cap = h->help_cap ? h->help_cap * 2 : 8; h->help_text = realloc(h->help_text, h->help_cap * sizeof *h->help_text); if (!h->help_text) qa_range_error("Out of memory"); }
      h->help_text[h->help_count].slot = source.slot;
      h->help_text[h->help_count].text = xdup(source.text);
      h->help_count++;
    }
  }
  if (strcmp(source.kind, "q2-player") == 0 && strcmp(source.event_kind, "inventory") == 0 && same_actor(source.actor, h->actor)) {
    for (size_t i = 0; i < h->inventory_count; i++) { free(h->inventory[i].id); free(h->inventory[i].label); }
    free(h->inventory); h->inventory = NULL; h->inventory_count = 0;
    if (source.has_visible && !source.visible) h->inventory_null = true;
    else {
      h->inventory_null = false;
      size_t cap = 0;
      for (size_t i = 0; i < source.entry_count; i++) {
        if (source.entries[i].count <= 0) continue;
        const char *label = NULL;
        for (size_t j = 0; j < source.label_count; j++) if (strcmp(source.labels[j].item, source.entries[i].item) == 0) label = source.labels[j].name;
        char *fallback = NULL;
        if (label == NULL) {
          const char *item = source.entries[i].item;
          if (strncmp(item, "q2:", 3) == 0) item += 3;
          fallback = xdup(item);
          for (char *p = fallback; *p; p++) if (*p == '_') *p = ' ';
          label = fallback;
        }
        if (h->inventory_count == cap) { cap = cap ? cap * 2 : 8; h->inventory = realloc(h->inventory, cap * sizeof *h->inventory); if (!h->inventory) qa_range_error("Out of memory"); }
        inv_item *item = &h->inventory[h->inventory_count++];
        item->id = xdup(source.entries[i].item);
        item->label = xdup(label);
        free(fallback);
        item->value = (qa_ui_hud_inventory_item){item->id, item->label, source.entries[i].count,
                                                 source.selected != NULL && strcmp(source.entries[i].item, source.selected) == 0, NULL, NULL};
      }
    }
    if (!h->inventory_null) h->help_visible = false;
  }
  if (strcmp(source.kind, "q2-player") == 0 && strcmp(source.event_kind, "help") == 0 && same_actor(source.actor, h->actor)) {
    h->help_visible = source.visible;
    if (h->help_visible) { h->inventory_null = true; h->score_visible = false; }
  }
  if (strcmp(source.kind, "q2-player") == 0 && strcmp(source.event_kind, "view") == 0 && same_actor(source.actor, h->actor)) {
    long layouts = (long)source.layouts;
    if ((layouts & 2) == 0) h->inventory_null = true;
    else if (!h->inventory_null) {
      for (size_t i = 0; i < h->inventory_count; i++)
        h->inventory[i].value.selected = source.selected_item != NULL && strcmp(h->inventory[i].id, source.selected_item) == 0;
    }
    if ((layouts & 1) == 0) { h->help_visible = false; h->score_visible = false; }
  }
  if (strcmp(source.kind, "q2-player") == 0 && strcmp(source.event_kind, "scoreboard") == 0 && same_actor(source.actor, h->actor)) {
    for (size_t i = 0; i < h->score_row_count; i++) free(h->score_rows[i]);
    free(h->score_rows);
    h->score_rows = calloc(source.row_count ? source.row_count : 1, sizeof *h->score_rows);
    if (!h->score_rows) qa_range_error("Out of memory");
    h->score_row_count = source.row_count;
    for (size_t i = 0; i < source.row_count; i++) {
      char *score = qa_input_js_number(source.rows[i].score);
      char *ping = qa_input_js_number(source.rows[i].ping);
      char *minutes = qa_input_js_number(source.rows[i].minutes);
      snprintf(buf, sizeof buf, "%s  %s  %sms  %sm%s", score, source.rows[i].name, ping, minutes, source.rows[i].spectator ? "  Spectator" : "");
      free(score); free(ping); free(minutes);
      h->score_rows[i] = xdup(buf);
    }
    h->score_visible = true; h->help_visible = false; h->inventory_null = true;
  }
  if (strcmp(source.kind, "q2-rerelease") == 0) {
    if (strcmp(source.event_kind, "mission-objective") == 0 && same_actor(source.actor, h->actor)) {
      set_str(&h->objective, source.text);
      admit_print(h, source.text, source.seconds);
    }
    if (strcmp(source.event_kind, "mission-status") == 0 && same_actor(source.actor, h->actor)) h->mission_visible = source.icon_visible;
    if ((strcmp(source.event_kind, "poi") == 0 || strcmp(source.event_kind, "keyed-poi") == 0) && same_actor(source.actor, h->actor)) {
      double key = strcmp(source.event_kind, "poi") == 0 ? 1 : source.key;
      double now = source.seconds * 1000;
      long index = -1;
      if (key != 0) for (size_t i = 0; i < h->poi_count; i++) if (h->pois[i].key == key) index = (long)i;
      if (index < 0) for (size_t i = 0; i < h->poi_count; i++) if (h->pois[i].expires <= now) { index = (long)i; break; }
      if (index < 0 && h->poi_count < 32) index = (long)h->poi_count++;
      if (index < 0) {
        double oldest = HUGE_VAL;
        for (size_t i = 0; i < h->poi_count; i++) if (h->pois[i].key == 0 && h->pois[i].expires < oldest) { oldest = h->pois[i].expires; index = (long)i; }
      }
      if (index >= 0) {
        poi *entry = &h->pois[index];
        free(entry->content); free(entry->path);
        entry->key = key; entry->width = 32; entry->height = 32;
        entry->flags = strcmp(source.event_kind, "poi") == 0 ? 1 : source.flags;
        entry->content = xdup(source.content);
        entry->path = xdup(source.image);
        entry->image = NULL;
        entry->origin = source.position;
        entry->color = (double)((long)source.color & 255);
        entry->tint = (qa_vec4){1, 1, 1, 1};
        entry->expires = now + source.duration;
      }
    }
    if (strcmp(source.event_kind, "remove-poi") == 0 && same_actor(source.actor, h->actor) && source.key != 0) {
      for (size_t i = 0; i < h->poi_count; i++) if (h->pois[i].key == source.key) {
        free(h->pois[i].content); free(h->pois[i].path);
        memmove(&h->pois[i], &h->pois[i + 1], (h->poi_count - i - 1) * sizeof(poi));
        h->poi_count--;
        break;
      }
    }
    if (strcmp(source.event_kind, "directional-damage") == 0 && same_actor(source.actor, h->actor)) {
      if (!h->has_damage_picture || strcmp(h->damage_content, source.content) != 0) {
        h->has_damage_picture = true;
        set_str(&h->damage_content, source.content);
        h->damage_image = NULL; h->damage_width = 0; h->damage_height = 0;
      }
      double now = source.seconds * 1000;
      qa_vec3 direction = source.direction;
      long index = -1;
      for (size_t i = 0; i < h->damage_count; i++) {
        qa_ui_hud_damage_indicator *value = &h->damage[i];
        double dot = value->direction.x * direction.x + value->direction.y * direction.y + value->direction.z * direction.z;
        if (value->expires_milliseconds <= now || (value->is_direction && dot >= 0.95)) { index = (long)i; break; }
      }
      if (index < 0) index = h->damage_count < 32 ? (long)h->damage_count++ : 0;
      const qa_ui_hud_damage_indicator *previous = index < (long)h->damage_count ? &h->damage[index] : NULL;
      bool retain = false;
      qa_ui_hud_damage_indicator retained; memset(&retained, 0, sizeof retained);
      if (previous != NULL && previous->expires_milliseconds > now && previous->is_direction
          && previous->direction.x * direction.x + previous->direction.y * direction.y + previous->direction.z * direction.z >= 0.95) {
        retain = true; retained = *previous;
      }
      qa_vec3 color = normalize3((qa_vec3){(source.health ? 1 : 0) + (source.armor ? 1 : 0), (source.shield ? 1 : 0) + (source.armor ? 1 : 0), source.armor ? 1 : 0});
      qa_ui_hud_damage_indicator entry; memset(&entry, 0, sizeof entry);
      entry.is_direction = true;
      entry.direction = direction;
      entry.amount = source.damage + (retain ? retained.amount : 0);
      entry.color = normalize3((qa_vec3){color.x + (retain ? retained.color.x : 0), color.y + (retain ? retained.color.y : 0), color.z + (retain ? retained.color.z : 0)});
      entry.health = source.health || (retain && retained.health);
      entry.armor = source.armor || (retain && retained.armor);
      entry.shield = source.shield || (retain && retained.shield);
      entry.expires_milliseconds = now + 1000;
      h->damage[index] = entry;
    }
    if (strcmp(source.event_kind, "help-path") == 0 && same_actor(source.actor, h->actor)) {
      h->has_path = true;
      h->path_origin = source.position;
      h->path_direction = source.direction;
      h->path_expires = source.seconds * 1000 + 10000;
    }
    if (strcmp(source.event_kind, "end-of-unit") == 0) {
      for (size_t i = 0; i < h->report_line_count; i++) free(h->report_lines[i]);
      free(h->report_lines);
      /* Stable sort by visitOrder (insertion sort on a copied index list). */
      size_t *order = calloc(source.level_count ? source.level_count : 1, sizeof *order);
      if (!order) qa_range_error("Out of memory");
      for (size_t i = 0; i < source.level_count; i++) order[i] = i;
      for (size_t i = 1; i < source.level_count; i++) {
        size_t at = order[i]; size_t j = i;
        while (j > 0 && source.levels[order[j - 1]].visit_order > source.levels[at].visit_order) { order[j] = order[j - 1]; j--; }
        order[j] = at;
      }
      h->report_lines = calloc(source.level_count ? source.level_count : 1, sizeof *h->report_lines);
      if (!h->report_lines) qa_range_error("Out of memory");
      h->report_line_count = source.level_count;
      for (size_t i = 0; i < source.level_count; i++) {
        const qa_app_hud_unit_level_mirror *level = &source.levels[order[i]];
        char *killed = qa_input_js_number(level->killed_monsters);
        char *total_m = qa_input_js_number(level->total_monsters);
        char *found = qa_input_js_number(level->found_secrets);
        char *total_s = qa_input_js_number(level->total_secrets);
        double minutes = floor(level->time / 60);
        double secs = floor(level->time - minutes * 60) /* Math.floor(time % 60) for positive times */;
        snprintf(buf, sizeof buf, "%s: %s/%s kills  %s/%s secrets  %.0f:%02.0f",
                 level->name && level->name[0] ? level->name : level->map, killed, total_m, found, total_s, minutes, secs);
        free(killed); free(total_m); free(found); free(total_s);
        h->report_lines[i] = xdup(buf);
      }
      free(order);
      h->has_report = true;
      h->report_ready = source.button_time * 1000;
    }
    if (strcmp(source.event_kind, "healthbar") == 0 && same_actor(source.actor, h->actor)) {
      if (!source.visible) {
        for (size_t i = 0; i < h->bar_count; i++) if (h->bars[i].slot == source.slot) {
          free(h->bars[i].id); free(h->bars[i].label);
          memmove(&h->bars[i], &h->bars[i + 1], (h->bar_count - i - 1) * sizeof(bar));
          h->bar_count--;
          break;
        }
      } else {
        bar *slot = NULL;
        for (size_t i = 0; i < h->bar_count; i++) if (h->bars[i].slot == source.slot) slot = &h->bars[i];
        if (slot == NULL) {
          if (h->bar_count == h->bar_cap) { h->bar_cap = h->bar_cap ? h->bar_cap * 2 : 8; h->bars = realloc(h->bars, h->bar_cap * sizeof *h->bars); if (!h->bars) qa_range_error("Out of memory"); }
          slot = &h->bars[h->bar_count++];
          memset(slot, 0, sizeof *slot);
          slot->slot = source.slot;
          char *n = qa_input_js_number(source.slot);
          snprintf(buf, sizeof buf, "boss:%s", n); free(n);
          slot->id = xdup(buf);
        }
        set_str(&slot->label, source.name);
        slot->value = (qa_ui_hud_health_bar){slot->id, slot->label, source.fraction, 1, {0.8, 0.12, 0.08, 1}};
      }
    }
    if (strcmp(source.event_kind, "help-computer") == 0 && same_actor(source.actor, h->actor)) {
      h->help_visible = source.visible;
      if (source.visible) { h->inventory_null = true; h->score_visible = false; }
      h->has_help_computer = true;
      set_str(&h->help_primary, source.primary);
      set_str(&h->help_secondary, source.secondary);
    }
  }
  if (strcmp(source.kind, "q1-composition") == 0) {
    if (strcmp(source.event_kind, "client") == 0) {
      bool found = false;
      for (size_t i = 0; i < h->client_count; i++) if (h->clients[i].slot == source.client_slot) {
        set_str(&h->clients[i].name, source.client_name);
        h->clients[i].frags = source.client_frags; h->clients[i].team = source.client_team; h->clients[i].observer = source.client_observer;
        found = true;
      }
      if (!found) {
        if (h->client_count == h->client_cap) { h->client_cap = h->client_cap ? h->client_cap * 2 : 8; h->clients = realloc(h->clients, h->client_cap * sizeof *h->clients); if (!h->clients) qa_range_error("Out of memory"); }
        h->clients[h->client_count] = (client_entry){source.client_slot, xdup(source.client_name), source.client_frags, source.client_team, source.client_observer};
        h->client_count++;
      }
    }
    if (strcmp(source.event_kind, "client-left") == 0) {
      for (size_t i = 0; i < h->client_count; i++) if (h->clients[i].slot == source.slot) {
        free(h->clients[i].name);
        memmove(&h->clients[i], &h->clients[i + 1], (h->client_count - i - 1) * sizeof(client_entry));
        h->client_count--;
        break;
      }
    }
    if (strcmp(source.event_kind, "ctf-status") == 0 && same_actor(source.actor, h->actor)) {
      h->ctf = true; h->red = source.ctf_red; h->blue = source.ctf_blue; h->flags = source.ctf_flags; h->rune_items = source.ctf_rune_items;
    }
    if (strcmp(source.event_kind, "ctf-capture") == 0) {
      h->ctf = true;
      bool red = source.ctf_team && strcmp(source.ctf_team, "red") == 0;
      if (red) h->red = source.ctf_total; else h->blue = source.ctf_total;
      set_str(&h->capture, red ? "Red captured the flag" : "Blue captured the flag");
      h->capture_until = source.seconds * 1000 + 3000;
    }
  }
}
static bool needs_localize(qa_app_simulation_presentation_event source) {
  return strcmp(source.kind, "q2-rerelease") == 0
      && (strcmp(source.event_kind, "healthbar") == 0 || strcmp(source.event_kind, "help-computer") == 0
          || strcmp(source.event_kind, "mission-objective") == 0);
}
void qa_app_seat_source_hud_receive(qa_app_seat_source_hud *h, qa_app_simulation_presentation_event source) {
  if (h->localize != NULL && needs_localize(source)) {
    if (h->pending_count == 8192) qa_range_error("Event ring capacity exhausted (%zu undrained records); drain before admitting more", (size_t)8192);
    h->pending[(h->pending_head + h->pending_count) % 8192] = source;
    h->pending_count++;
  } else apply(h, source);
}
void qa_app_seat_source_hud_prepare(qa_app_seat_source_hud *h, qa_app_weapon_hud_assets *icons) {
  while (h->pending_count > 0) {
    qa_app_simulation_presentation_event source = h->pending[h->pending_head];
    h->pending_head = (h->pending_head + 1) % 8192;
    h->pending_count--;
    if (strcmp(source.kind, "q2-rerelease") != 0 || h->localize == NULL) { apply(h, source); continue; }
    if (strcmp(source.event_kind, "healthbar") == 0) {
      char *text = h->localize(h->localize_ctx, source.content, source.name, NULL, 0);
      source.name = text;
      apply(h, source);
      free(text);
    } else if (strcmp(source.event_kind, "help-computer") == 0) {
      char *primary = h->localize(h->localize_ctx, source.content, source.primary, NULL, 0);
      char *secondary = h->localize(h->localize_ctx, source.content, source.secondary, NULL, 0);
      source.primary = primary; source.secondary = secondary;
      apply(h, source);
      free(primary); free(secondary);
    } else if (strcmp(source.event_kind, "mission-objective") == 0) {
      char *text = h->localize(h->localize_ctx, source.content, source.text, source.args, source.arg_count);
      source.text = text; source.args = NULL; source.arg_count = 0;
      apply(h, source);
      free(text);
    } else apply(h, source);
  }
  for (size_t i = 0; i < h->poi_count; i++) {
    poi *entry = &h->pois[i];
    if (entry->image != NULL) continue;
    char path[1024];
    snprintf(path, sizeof path, "pics/%s.pcx", entry->path);
    qa_weapon_hud_icon icon; memset(&icon, 0, sizeof icon);
    icon.kind = QA_WEAPON_HUD_ICON_IMAGE;
    icon.resource.content = entry->content;
    icon.resource.path = path;
    entry->image = qa_app_weapon_hud_assets_load(icons, icon);
    const qa_picture_asset *picture = qa_app_weapon_hud_assets_picture(icons, entry->image);
    if (picture != NULL && picture->kind == QA_PICTURE_IMAGE) {
      double w = 0, hh = 0;
      /* Image sizes come through the weapon-hud host; 32x32 default kept otherwise. */
      (void)w; (void)hh;
    }
    /* Palette tint stays white (TS provider palette access). */
  }
  if (h->has_damage_picture && h->damage_image == NULL) {
    qa_weapon_hud_icon icon; memset(&icon, 0, sizeof icon);
    icon.kind = QA_WEAPON_HUD_ICON_IMAGE;
    icon.resource.content = h->damage_content;
    icon.resource.path = "pics/damage_indicator.pcx";
    h->damage_image = qa_app_weapon_hud_assets_load(icons, icon);
  }
  if (h->has_pickup && h->pickup_icon == NULL && h->pickup_path[0] != '\0') {
    char path[1024];
    snprintf(path, sizeof path, "pics/%s.pcx", h->pickup_path);
    qa_weapon_hud_icon icon; memset(&icon, 0, sizeof icon);
    icon.kind = QA_WEAPON_HUD_ICON_IMAGE;
    icon.resource.content = h->pickup_content;
    icon.resource.path = path;
    h->pickup_icon = qa_app_weapon_hud_assets_load(icons, icon);
  }
}
const qa_ui_hud_point_of_interest *qa_app_seat_source_hud_points(qa_app_seat_source_hud *h, size_t *out_count) {
  free(h->point_scratch);
  h->point_scratch = calloc(h->poi_count ? h->poi_count : 1, sizeof *h->point_scratch);
  if (!h->point_scratch) qa_range_error("Out of memory");
  size_t n = 0;
  for (size_t i = 0; i < h->poi_count; i++) {
    if (h->pois[i].image == NULL) continue;
    h->point_scratch[n++] = (qa_ui_hud_point_of_interest){h->pois[i].key, h->pois[i].origin, h->pois[i].image,
        h->pois[i].width, h->pois[i].height, h->pois[i].tint, ((long)h->pois[i].flags & 1) != 0, h->pois[i].expires};
  }
  *out_count = n;
  return h->point_scratch;
}
static const char *flag_state(long bits) { return (bits & 4) != 0 ? "dropped" : (bits & 2) != 0 ? "carried" : "home"; }
qa_app_hud_presentation qa_app_seat_source_hud_presentation(qa_app_seat_source_hud *h, double now_milliseconds) {
  clear_scratch(h);
  qa_app_hud_presentation out; memset(&out, 0, sizeof out);
  char buf[1024];
  qa_ui_hud_prompt prompts[16];
  size_t prompt_count = 0;
  if (h->ctf) {
    char *red = qa_input_js_number(h->red), *blue = qa_input_js_number(h->blue);
    snprintf(buf, sizeof buf, "Red %s - Blue %s", red, blue); free(red); free(blue);
    prompts[prompt_count++] = (qa_ui_hud_prompt){scratch_str(h, buf), "", NULL};
  }
  if (h->mission_visible && h->objective[0] != '\0') prompts[prompt_count++] = (qa_ui_hud_prompt){"New objective", "", NULL};
  if (h->ctf) {
    long flags = (long)h->flags;
    snprintf(buf, sizeof buf, "Red flag %s - Blue flag %s", flag_state(flags & 7), flag_state((flags >> 3) & 7));
    prompts[prompt_count++] = (qa_ui_hud_prompt){scratch_str(h, buf), "", NULL};
    const struct { long bit; const char *label; } runes[4] = {{32, "Resistance"}, {64, "Strength"}, {128, "Haste"}, {256, "Regeneration"}};
    for (int i = 0; i < 4; i++) if (((long)h->rune_items & runes[i].bit) != 0) prompts[prompt_count++] = (qa_ui_hud_prompt){runes[i].label, "", NULL};
  }
  if (h->capture_until > now_milliseconds) prompts[prompt_count++] = (qa_ui_hud_prompt){scratch_str(h, h->capture), "", NULL};
  free(h->prompt_scratch);
  h->prompt_scratch = calloc(prompt_count ? prompt_count : 1, sizeof *h->prompt_scratch);
  if (!h->prompt_scratch) qa_range_error("Out of memory");
  memcpy(h->prompt_scratch, prompts, prompt_count * sizeof *prompts);
  out.prompts = h->prompt_scratch;
  out.prompt_count = prompt_count;
  /* scores */
  const char **score_lines = NULL; size_t score_count = 0; bool have_scores = false;
  const char **score_storage = NULL;
  if (h->score_visible) {
    have_scores = true;
    score_lines = (const char **)h->score_rows;
    score_count = h->score_row_count;
  } else if (h->scores_held && h->client_count > 0) {
    have_scores = true;
    size_t *order = calloc(h->client_count, sizeof *order);
    score_storage = calloc(h->client_count, sizeof *score_storage);
    if (!order || !score_storage) qa_range_error("Out of memory");
    for (size_t i = 0; i < h->client_count; i++) order[i] = i;
    for (size_t i = 1; i < h->client_count; i++) {
      size_t at = order[i]; size_t j = i;
      while (j > 0 && h->clients[order[j - 1]].frags < h->clients[at].frags) { order[j] = order[j - 1]; j--; }
      order[j] = at;
    }
    for (size_t i = 0; i < h->client_count; i++) {
      client_entry *client = &h->clients[order[i]];
      char *frags = qa_input_js_number(client->frags);
      char team[64] = "";
      if (client->team != 0) { char *t = qa_input_js_number(client->team); snprintf(team, sizeof team, "  Team %s", t); free(t); }
      snprintf(buf, sizeof buf, "%s  %s%s%s", frags, client->name, team, client->observer ? "  Spectator" : "");
      free(frags);
      score_storage[i] = scratch_str(h, buf);
    }
    free(order);
    score_lines = score_storage;
    score_count = h->client_count;
  }
  /* damage */
  free(h->damage_scratch);
  h->damage_scratch = calloc(h->damage_count ? h->damage_count : 1, sizeof *h->damage_scratch);
  if (!h->damage_scratch) qa_range_error("Out of memory");
  size_t damage_n = 0;
  for (size_t i = 0; i < h->damage_count; i++) {
    qa_ui_hud_damage_indicator entry = h->damage[i];
    if (entry.expires_milliseconds <= now_milliseconds) continue;
    if (entry.is_direction && h->has_damage_picture && h->damage_image != NULL && h->damage_width > 0) {
      entry.has_picture = true;
      entry.picture_image = h->damage_image;
      entry.picture_width = h->damage_width;
      entry.picture_height = h->damage_height;
    }
    h->damage_scratch[damage_n++] = entry;
  }
  out.damage_indicators = h->damage_scratch;
  out.damage_indicator_count = damage_n;
  out.has_help_path = h->has_path && h->path_expires > now_milliseconds;
  out.help_path_origin = h->path_origin;
  out.help_path_direction = h->path_direction;
  if (h->has_pickup) {
    h->pickup_scratch = (qa_app_hud_pickup){h->pickup_name, h->pickup_path, h->pickup_content, h->pickup_icon, h->pickup_expires};
    out.pickup = &h->pickup_scratch;
  }
  out.inventory_is_null = h->inventory_null;
  if (!h->inventory_null) {
    size_t count = 0;
    qa_app_seat_source_hud_inventory_items(h, &out.inventory, &count);
    out.inventory_count = count;
  }
  /* health bars sorted by slot */
  free(h->bar_scratch);
  h->bar_scratch = calloc(h->bar_count ? h->bar_count : 1, sizeof *h->bar_scratch);
  if (!h->bar_scratch) qa_range_error("Out of memory");
  size_t *order = calloc(h->bar_count ? h->bar_count : 1, sizeof *order);
  if (!order) qa_range_error("Out of memory");
  for (size_t i = 0; i < h->bar_count; i++) order[i] = i;
  for (size_t i = 1; i < h->bar_count; i++) {
    size_t at = order[i]; size_t j = i;
    while (j > 0 && h->bars[order[j - 1]].slot > h->bars[at].slot) { order[j] = order[j - 1]; j--; }
    order[j] = at;
  }
  for (size_t i = 0; i < h->bar_count; i++) h->bar_scratch[i] = h->bars[order[i]].value;
  free(order);
  out.health_bars = h->bar_scratch;
  out.health_bar_count = h->bar_count;
  /* help */
  out.help = NULL;
  if (h->has_report) {
    h->objective_scratch = (qa_ui_hud_objective){now_milliseconds >= h->report_ready ? "Press attack to continue" : "", false};
    h->help_scratch = (qa_ui_hud_help){"Unit complete", (const char *const *)h->report_lines, h->report_line_count, &h->objective_scratch, 1};
    out.help = &h->help_scratch;
  } else if (have_scores) {
    h->help_scratch = (qa_ui_hud_help){"Scores", score_lines, score_count, NULL, 0};
    out.help = &h->help_scratch;
  } else if (h->help_visible) {
    if (h->has_help_computer) {
      const char **lines = calloc(2, sizeof *lines);
      if (!lines) qa_range_error("Out of memory");
      size_t n = 0;
      if (h->help_primary && h->help_primary[0]) lines[n++] = h->help_primary;
      if (h->help_secondary && h->help_secondary[0]) lines[n++] = h->help_secondary;
      h->help_scratch = (qa_ui_hud_help){"Help computer", lines, n, NULL, 0};
      free((void *)lines); /* strings borrowed; array must outlive call: keep via scratch below */
      /* Rebuild with retained array. */
      const char **kept = calloc(2, sizeof *kept);
      if (!kept) qa_range_error("Out of memory");
      n = 0;
      if (h->help_primary && h->help_primary[0]) kept[n++] = h->help_primary;
      if (h->help_secondary && h->help_secondary[0]) kept[n++] = h->help_secondary;
      h->help_scratch.lines = kept;
      h->help_scratch.line_count = n;
      /* Leak the 2-entry array per presentation call is avoided by freeing next call: tracked in scratch strings as a block. */
      if (h->string_scratch_count == h->string_scratch_cap) { h->string_scratch_cap = h->string_scratch_cap ? h->string_scratch_cap * 2 : 16; h->string_scratch = realloc(h->string_scratch, h->string_scratch_cap * sizeof *h->string_scratch); }
      h->string_scratch[h->string_scratch_count++] = (char *)kept;
    } else {
      size_t *help_order = calloc(h->help_count ? h->help_count : 1, sizeof *help_order);
      const char **lines = calloc(h->help_count ? h->help_count : 1, sizeof *lines);
      if (!help_order || !lines) qa_range_error("Out of memory");
      for (size_t i = 0; i < h->help_count; i++) help_order[i] = i;
      for (size_t i = 1; i < h->help_count; i++) {
        size_t at = help_order[i]; size_t j = i;
        while (j > 0 && h->help_text[help_order[j - 1]].slot > h->help_text[at].slot) { help_order[j] = help_order[j - 1]; j--; }
        help_order[j] = at;
      }
      for (size_t i = 0; i < h->help_count; i++) lines[i] = h->help_text[help_order[i]].text;
      free(help_order);
      h->help_scratch = (qa_ui_hud_help){"Help computer", lines, h->help_count, NULL, 0};
      if (h->string_scratch_count == h->string_scratch_cap) { h->string_scratch_cap = h->string_scratch_cap ? h->string_scratch_cap * 2 : 16; h->string_scratch = realloc(h->string_scratch, h->string_scratch_cap * sizeof *h->string_scratch); }
      h->string_scratch[h->string_scratch_count++] = (char *)lines;
    }
    out.help = &h->help_scratch;
  }
  free((void *)score_storage);
  return out;
}
