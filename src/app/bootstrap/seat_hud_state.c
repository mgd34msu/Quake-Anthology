/* Port of src/app/bootstrap/seat-hud-state.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/seat_hud_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/composition/q1/types.h"
#include "qa/content/q1/addons/ctf/types.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/campaign.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
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
  qa_app_sim_presentation_event pending[8192]; size_t pending_head, pending_count; /* owned copies */
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
  char **missing; size_t missing_count, missing_cap; /* TS missingPictures */
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
  for (size_t i = 0; i < h->missing_count; i++) free(h->missing[i]);
  free(h->missing);
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
  for (size_t i = 0; i < h->pending_count; i++) qa_app_sim_presentation_event_release(&h->pending[(h->pending_head + i) % 8192]);
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
  double length = qa_js_hypot3(v.x, v.y, v.z);
  if (length == 0 || isnan(length)) length = 1; /* Math.hypot(...) || 1 */
  qa_vec3 r = {v.x / length, v.y / length, v.z / length};
  return r;
}
static void apply(qa_app_seat_source_hud *h, const qa_app_sim_presentation_event *source) {
  char buf[4096];
  if (strcmp(source->kind, "q2") == 0 && strcmp(source->q2->kind, "pickup") == 0 && same_actor(source->q2->player, h->actor)) {
    h->has_pickup = true;
    set_str(&h->pickup_name, source->q2->name);
    set_str(&h->pickup_content, source->content);
    set_str(&h->pickup_path, source->q2->icon);
    h->pickup_icon = NULL;
    h->pickup_expires = source->seconds * 1000 + 3000;
  }
  if (strcmp(source->kind, "q2") == 0 && strcmp(source->q2->kind, "damage-indicator") == 0 && same_actor(source->q2->actor, h->actor)) {
    bool was_full = h->damage_count >= 32;
    qa_ui_hud_damage_indicator entry; memset(&entry, 0, sizeof entry);
    entry.origin = source->q2->origin; entry.amount = source->q2->amount; entry.expires_milliseconds = source->seconds * 1000 + 800;
    if (h->damage_count < 32) h->damage[h->damage_count++] = entry;
    else { memmove(&h->damage[0], &h->damage[1], 31 * sizeof entry); h->damage[31] = entry; }
    if (!was_full && h->damage_count > 8) { memmove(&h->damage[0], &h->damage[1], (h->damage_count - 1) * sizeof entry); h->damage_count--; }
  }
  if (strcmp(source->kind, "q2") == 0 && strcmp(source->q2->kind, "help") == 0) {
    bool found = false;
    for (size_t i = 0; i < h->help_count; i++) if (h->help_text[i].slot == source->q2->slot) { set_str(&h->help_text[i].text, source->q2->text); found = true; }
    if (!found) {
      if (h->help_count == h->help_cap) { h->help_cap = h->help_cap ? h->help_cap * 2 : 8; h->help_text = realloc(h->help_text, h->help_cap * sizeof *h->help_text); if (!h->help_text) qa_range_error("Out of memory"); }
      h->help_text[h->help_count].slot = source->q2->slot;
      h->help_text[h->help_count].text = xdup(source->q2->text);
      h->help_count++;
    }
  }
  if (strcmp(source->kind, "q2-player") == 0 && strcmp(source->q2_player->kind, "inventory") == 0 && same_actor(source->q2_player->actor, h->actor)) {
    for (size_t i = 0; i < h->inventory_count; i++) { free(h->inventory[i].id); free(h->inventory[i].label); }
    free(h->inventory); h->inventory = NULL; h->inventory_count = 0;
    if (source->q2_player->has_visible && !source->q2_player->visible) h->inventory_null = true;
    else {
      h->inventory_null = false;
      size_t cap = 0;
      for (size_t i = 0; i < source->q2_player->entry_count; i++) {
        if (source->q2_player->entries[i].count <= 0) continue;
        const char *label = NULL;
        for (size_t j = 0; j < source->q2_player->label_count; j++) if (strcmp(source->q2_player->labels[j].item, source->q2_player->entries[i].item) == 0) label = source->q2_player->labels[j].name;
        char *fallback = NULL;
        if (label == NULL) {
          const char *item = source->q2_player->entries[i].item;
          if (strncmp(item, "q2:", 3) == 0) item += 3;
          fallback = xdup(item);
          for (char *p = fallback; *p; p++) if (*p == '_') *p = ' ';
          label = fallback;
        }
        if (h->inventory_count == cap) { cap = cap ? cap * 2 : 8; h->inventory = realloc(h->inventory, cap * sizeof *h->inventory); if (!h->inventory) qa_range_error("Out of memory"); }
        inv_item *item = &h->inventory[h->inventory_count++];
        item->id = xdup(source->q2_player->entries[i].item);
        item->label = xdup(label);
        free(fallback);
        item->value = (qa_ui_hud_inventory_item){item->id, item->label, source->q2_player->entries[i].count,
                                                 source->q2_player->has_selected && source->q2_player->selected != NULL && strcmp(source->q2_player->entries[i].item, source->q2_player->selected) == 0, NULL, NULL};
      }
    }
    if (!h->inventory_null) h->help_visible = false;
  }
  if (strcmp(source->kind, "q2-player") == 0 && strcmp(source->q2_player->kind, "help") == 0 && same_actor(source->q2_player->actor, h->actor)) {
    h->help_visible = source->q2_player->visible;
    if (h->help_visible) { h->inventory_null = true; h->score_visible = false; }
  }
  if (strcmp(source->kind, "q2-player") == 0 && strcmp(source->q2_player->kind, "view") == 0 && same_actor(source->q2_player->actor, h->actor)) {
    long layouts = qa_js_to_int32(source->q2_player->view.layouts); /* layouts & 2 */
    if ((layouts & 2) == 0) h->inventory_null = true;
    else if (!h->inventory_null) {
      for (size_t i = 0; i < h->inventory_count; i++)
        h->inventory[i].value.selected = source->q2_player->view.selected_item != NULL && strcmp(h->inventory[i].id, source->q2_player->view.selected_item) == 0;
    }
    if ((layouts & 1) == 0) { h->help_visible = false; h->score_visible = false; }
  }
  if (strcmp(source->kind, "q2-player") == 0 && strcmp(source->q2_player->kind, "scoreboard") == 0 && same_actor(source->q2_player->actor, h->actor)) {
    for (size_t i = 0; i < h->score_row_count; i++) free(h->score_rows[i]);
    free(h->score_rows);
    h->score_rows = calloc(source->q2_player->row_count ? source->q2_player->row_count : 1, sizeof *h->score_rows);
    if (!h->score_rows) qa_range_error("Out of memory");
    h->score_row_count = source->q2_player->row_count;
    for (size_t i = 0; i < source->q2_player->row_count; i++) {
      char *score = qa_input_js_number(source->q2_player->rows[i].score);
      char *ping = qa_input_js_number(source->q2_player->rows[i].ping);
      char *minutes = qa_input_js_number(source->q2_player->rows[i].minutes);
      snprintf(buf, sizeof buf, "%s  %s  %sms  %sm%s", score, source->q2_player->rows[i].name, ping, minutes, source->q2_player->rows[i].spectator ? "  Spectator" : "");
      free(score); free(ping); free(minutes);
      h->score_rows[i] = xdup(buf);
    }
    h->score_visible = true; h->help_visible = false; h->inventory_null = true;
  }
  if (strcmp(source->kind, "q2-rerelease") == 0) {
    const qa_q2_rerelease_event *event = source->q2_rerelease;
    if (strcmp(event->kind, "mission-objective") == 0 && same_actor(event->actor, h->actor)) {
      set_str(&h->objective, event->text);
      admit_print(h, event->text, source->seconds);
    }
    if (strcmp(event->kind, "mission-status") == 0 && same_actor(event->actor, h->actor)) h->mission_visible = event->icon_visible;
    if ((strcmp(event->kind, "poi") == 0 || strcmp(event->kind, "keyed-poi") == 0) && same_actor(event->actor, h->actor)) {
      double key = strcmp(event->kind, "poi") == 0 ? 1 : event->key;
      double now = source->seconds * 1000;
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
        entry->flags = strcmp(event->kind, "poi") == 0 ? 1 : event->flags;
        entry->content = xdup(source->content);
        entry->path = xdup(event->image);
        entry->image = NULL;
        entry->origin = event->position;
        entry->color = (double)(qa_js_to_int32(event->color) & 255);
        entry->tint = (qa_vec4){1, 1, 1, 1};
        entry->expires = now + event->duration;
      }
    }
    if (strcmp(event->kind, "remove-poi") == 0 && same_actor(event->actor, h->actor) && event->key != 0) {
      for (size_t i = 0; i < h->poi_count; i++) if (h->pois[i].key == event->key) {
        free(h->pois[i].content); free(h->pois[i].path);
        memmove(&h->pois[i], &h->pois[i + 1], (h->poi_count - i - 1) * sizeof(poi));
        h->poi_count--;
        break;
      }
    }
    if (strcmp(event->kind, "directional-damage") == 0 && same_actor(event->actor, h->actor)) {
      if (!h->has_damage_picture || strcmp(h->damage_content, source->content) != 0) {
        h->has_damage_picture = true;
        set_str(&h->damage_content, source->content);
        h->damage_image = NULL; h->damage_width = 0; h->damage_height = 0;
      }
      double now = source->seconds * 1000;
      qa_vec3 direction = event->direction;
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
      qa_vec3 color = normalize3((qa_vec3){(event->health ? 1 : 0) + (event->armor ? 1 : 0), (event->shield ? 1 : 0) + (event->armor ? 1 : 0), event->armor ? 1 : 0});
      qa_ui_hud_damage_indicator entry; memset(&entry, 0, sizeof entry);
      entry.is_direction = true;
      entry.direction = direction;
      entry.amount = event->damage + (retain ? retained.amount : 0);
      entry.color = normalize3((qa_vec3){color.x + (retain ? retained.color.x : 0), color.y + (retain ? retained.color.y : 0), color.z + (retain ? retained.color.z : 0)});
      entry.health = event->health || (retain && retained.health);
      entry.armor = event->armor || (retain && retained.armor);
      entry.shield = event->shield || (retain && retained.shield);
      entry.expires_milliseconds = now + 1000;
      h->damage[index] = entry;
    }
    if (strcmp(event->kind, "help-path") == 0 && same_actor(event->actor, h->actor)) {
      h->has_path = true;
      h->path_origin = event->position;
      h->path_direction = event->direction;
      h->path_expires = source->seconds * 1000 + 10000;
    }
    if (strcmp(event->kind, "end-of-unit") == 0) {
      for (size_t i = 0; i < h->report_line_count; i++) free(h->report_lines[i]);
      free(h->report_lines);
      /* Stable sort by visitOrder (insertion sort on a copied index list). */
      size_t *order = calloc(event->level_count ? event->level_count : 1, sizeof *order);
      if (!order) qa_range_error("Out of memory");
      for (size_t i = 0; i < event->level_count; i++) order[i] = i;
      for (size_t i = 1; i < event->level_count; i++) {
        size_t at = order[i]; size_t j = i;
        while (j > 0 && event->levels[order[j - 1]].visit_order > event->levels[at].visit_order) { order[j] = order[j - 1]; j--; }
        order[j] = at;
      }
      h->report_lines = calloc(event->level_count ? event->level_count : 1, sizeof *h->report_lines);
      if (!h->report_lines) qa_range_error("Out of memory");
      h->report_line_count = event->level_count;
      for (size_t i = 0; i < event->level_count; i++) {
        const qa_q2_rerelease_level_entry *level = &event->levels[order[i]];
        char *killed = qa_input_js_number(level->killed_monsters);
        char *total_m = qa_input_js_number(level->total_monsters);
        char *found = qa_input_js_number(level->found_secrets);
        char *total_s = qa_input_js_number(level->total_secrets);
        double minutes = floor(level->time / 60);
        /* String(Math.floor(time % 60)).padStart(2, "0") */
        char secs[48];
        qa_net_js_number(floor(fmod(level->time, 60)), secs + 1, sizeof secs - 1);
        const char *secs_text = secs + 1;
        if (strlen(secs_text) < 2) { secs[0] = '0'; secs_text = secs; }
        snprintf(buf, sizeof buf, "%s: %s/%s kills  %s/%s secrets  %s:%s",
                 level->name && level->name[0] ? level->name : level->map, killed, total_m, found, total_s,
                 QA_JS_NUMBER(minutes), secs_text);
        free(killed); free(total_m); free(found); free(total_s);
        h->report_lines[i] = xdup(buf);
      }
      free(order);
      h->has_report = true;
      h->report_ready = event->button_time * 1000;
    }
    if (strcmp(event->kind, "healthbar") == 0 && same_actor(event->actor, h->actor)) {
      if (!event->visible) {
        for (size_t i = 0; i < h->bar_count; i++) if (h->bars[i].slot == event->slot) {
          free(h->bars[i].id); free(h->bars[i].label);
          memmove(&h->bars[i], &h->bars[i + 1], (h->bar_count - i - 1) * sizeof(bar));
          h->bar_count--;
          break;
        }
      } else {
        bar *slot = NULL;
        for (size_t i = 0; i < h->bar_count; i++) if (h->bars[i].slot == event->slot) slot = &h->bars[i];
        if (slot == NULL) {
          if (h->bar_count == h->bar_cap) { h->bar_cap = h->bar_cap ? h->bar_cap * 2 : 8; h->bars = realloc(h->bars, h->bar_cap * sizeof *h->bars); if (!h->bars) qa_range_error("Out of memory"); }
          slot = &h->bars[h->bar_count++];
          memset(slot, 0, sizeof *slot);
          slot->slot = event->slot;
          char *n = qa_input_js_number(event->slot);
          snprintf(buf, sizeof buf, "boss:%s", n); free(n);
          slot->id = xdup(buf);
        }
        set_str(&slot->label, event->name);
        slot->value = (qa_ui_hud_health_bar){slot->id, slot->label, event->fraction, 1, {0.8, 0.12, 0.08, 1}};
      }
    }
    if (strcmp(event->kind, "help-computer") == 0 && same_actor(event->actor, h->actor)) {
      h->help_visible = event->visible;
      if (event->visible) { h->inventory_null = true; h->score_visible = false; }
      h->has_help_computer = true;
      set_str(&h->help_primary, event->primary);
      set_str(&h->help_secondary, event->secondary);
    }
  }
  if (strcmp(source->kind, "q1-composition") == 0) {
    const qa_comp_q1_event *event = source->q1_composition;
    if (event->kind == QA_COMP_Q1_EVENT_CLIENT) {
      bool found = false;
      for (size_t i = 0; i < h->client_count; i++) if (h->clients[i].slot == (double)event->client.slot) {
        set_str(&h->clients[i].name, event->client.name);
        h->clients[i].frags = event->client.frags; h->clients[i].team = event->client.team; h->clients[i].observer = event->client.observer;
        found = true;
      }
      if (!found) {
        if (h->client_count == h->client_cap) { h->client_cap = h->client_cap ? h->client_cap * 2 : 8; h->clients = realloc(h->clients, h->client_cap * sizeof *h->clients); if (!h->clients) qa_range_error("Out of memory"); }
        h->clients[h->client_count] = (client_entry){(double)event->client.slot, xdup(event->client.name), event->client.frags, event->client.team, event->client.observer};
        h->client_count++;
      }
    }
    if (event->kind == QA_COMP_Q1_EVENT_CLIENT_LEFT) {
      for (size_t i = 0; i < h->client_count; i++) if (h->clients[i].slot == (double)event->slot) {
        free(h->clients[i].name);
        memmove(&h->clients[i], &h->clients[i + 1], (h->client_count - i - 1) * sizeof(client_entry));
        h->client_count--;
        break;
      }
    }
    if (event->kind == QA_COMP_Q1_EVENT_CTF_STATUS && same_actor(event->actor, h->actor)) {
      h->ctf = true; h->red = event->status->red; h->blue = event->status->blue; h->flags = (double)event->status->flags; h->rune_items = (double)event->status->rune_items;
    }
    if (event->kind == QA_COMP_Q1_EVENT_CTF_CAPTURE) {
      h->ctf = true;
      bool red = event->team != NULL && strcmp(event->team, "red") == 0;
      if (red) h->red = event->total; else h->blue = event->total;
      set_str(&h->capture, red ? "Red captured the flag" : "Blue captured the flag");
      h->capture_until = source->seconds * 1000 + 3000;
    }
  }
}
static bool needs_localize(const qa_app_sim_presentation_event *source) {
  return strcmp(source->kind, "q2-rerelease") == 0
      && (strcmp(source->q2_rerelease->kind, "healthbar") == 0 || strcmp(source->q2_rerelease->kind, "help-computer") == 0
          || strcmp(source->q2_rerelease->kind, "mission-objective") == 0);
}
void qa_app_seat_source_hud_receive(qa_app_seat_source_hud *h, const qa_app_sim_presentation_event *source) {
  if (h->localize != NULL && needs_localize(source)) {
    if (h->pending_count == 8192) qa_range_error("Event ring capacity exhausted (%zu undrained records); drain before admitting more", (size_t)8192);
    if (!qa_app_sim_presentation_event_copy(&h->pending[(h->pending_head + h->pending_count) % 8192], source))
      qa_range_error("Out of memory");
    h->pending_count++;
  } else apply(h, source);
}
/* path.startsWith("/") ? path.slice(1) : `pics/${path}.pcx`. Malloc'd. */
static char *icon_path(const char *path) {
  if (path[0] == '/') return xdup(path + 1);
  size_t length = strlen(path) + sizeof "pics/.pcx";
  char *out = malloc(length);
  if (out == NULL) qa_range_error("Out of memory");
  snprintf(out, length, "pics/%s.pcx", path);
  return out;
}
typedef struct load_attempt { qa_app_weapon_hud_assets *icons; qa_weapon_hud_icon icon; qa_resource_id result; } load_attempt;
static void load_attempt_run(void *ctx) {
  load_attempt *attempt = ctx;
  attempt->result = qa_app_weapon_hud_assets_load(attempt->icons, attempt->icon);
}
/* TS loadOptional: pictures named by the game or server are cosmetic; an
 * absent one draws nothing instead of failing the frame. */
static qa_resource_id load_optional(qa_app_seat_source_hud *h, qa_app_weapon_hud_assets *icons, qa_content_id content, const char *path) {
  size_t length = strlen(content) + strlen(path) + 2;
  char *key = malloc(length);
  if (key == NULL) qa_range_error("Out of memory");
  snprintf(key, length, "%s/%s", content, path);
  for (size_t i = 0; i < h->missing_count; i++)
    if (strcmp(h->missing[i], key) == 0) { free(key); return NULL; }
  load_attempt attempt; memset(&attempt, 0, sizeof attempt);
  attempt.icons = icons;
  attempt.icon.kind = QA_WEAPON_HUD_ICON_IMAGE;
  attempt.icon.resource.content = content;
  attempt.icon.resource.path = path;
  qa_caught_error caught;
  if (qa_try(load_attempt_run, &attempt, &caught)) { free(key); return attempt.result; }
  if (h->missing_count == h->missing_cap) {
    h->missing_cap = h->missing_cap ? h->missing_cap * 2 : 8;
    h->missing = realloc(h->missing, h->missing_cap * sizeof *h->missing);
    if (h->missing == NULL) qa_range_error("Out of memory");
  }
  h->missing[h->missing_count++] = key;
  return NULL;
}
void qa_app_seat_source_hud_prepare(qa_app_seat_source_hud *h, qa_app_weapon_hud_assets *icons) {
  while (h->pending_count > 0) {
    qa_app_sim_presentation_event owned = h->pending[h->pending_head];
    memset(&h->pending[h->pending_head], 0, sizeof owned);
    h->pending_head = (h->pending_head + 1) % 8192;
    h->pending_count--;
    if (strcmp(owned.kind, "q2-rerelease") != 0 || h->localize == NULL) { apply(h, &owned); qa_app_sim_presentation_event_release(&owned); continue; }
    /* TS apply({ ...source, event: { ...event, <localized fields> } }). */
    qa_q2_rerelease_event event = *owned.q2_rerelease;
    qa_app_sim_presentation_event localized = owned;
    localized.storage = NULL;
    localized.q2_rerelease = &event;
    if (strcmp(event.kind, "healthbar") == 0) {
      char *text = h->localize(h->localize_ctx, owned.content, event.name, NULL, 0);
      event.name = text;
      apply(h, &localized);
      free(text);
    } else if (strcmp(event.kind, "help-computer") == 0) {
      char *primary = h->localize(h->localize_ctx, owned.content, event.primary, NULL, 0);
      char *secondary = h->localize(h->localize_ctx, owned.content, event.secondary, NULL, 0);
      event.primary = primary; event.secondary = secondary;
      apply(h, &localized);
      free(primary); free(secondary);
    } else if (strcmp(event.kind, "mission-objective") == 0) {
      char *text = h->localize(h->localize_ctx, owned.content, event.text, event.args, event.arg_count);
      event.text = text; event.args = NULL; event.arg_count = 0;
      apply(h, &localized);
      free(text);
    } else apply(h, &owned);
    qa_app_sim_presentation_event_release(&owned);
  }
  for (size_t i = 0; i < h->poi_count; i++) {
    poi *entry = &h->pois[i];
    if (entry->image != NULL) continue;
    char *path = icon_path(entry->path);
    qa_resource_id image = load_optional(h, icons, entry->content, path);
    free(path);
    if (image == NULL) continue;
    entry->image = image;
    const qa_picture_asset *picture = qa_app_weapon_hud_assets_picture(icons, image);
    if (picture != NULL && picture->kind == QA_PICTURE_IMAGE) {
      entry->width = picture->image.image.width; entry->height = picture->image.image.height;
    }
    const qa_palette *palette = qa_app_application_assets_provider(qa_app_weapon_hud_assets_assets(icons), entry->content).palette;
    if (palette != NULL) {
      /* (palette.colors[index] ?? 255) / 255 */
      double channel[3];
      for (int c = 0; c < 3; c++) {
        double index = entry->color * 3 + c;
        channel[c] = (index < (double)palette->colors.length ? palette->colors.bytes[(size_t)index] : 255) / 255.0;
      }
      entry->tint = (qa_vec4){channel[0], channel[1], channel[2], 1};
    }
  }
  if (h->has_damage_picture && h->damage_image == NULL) {
    qa_resource_id image = load_optional(h, icons, h->damage_content, "pics/damage_indicator.pcx");
    const qa_picture_asset *picture = image == NULL ? NULL : qa_app_weapon_hud_assets_picture(icons, image);
    h->damage_image = image;
    if (picture != NULL && picture->kind == QA_PICTURE_IMAGE) {
      h->damage_width = picture->image.image.width; h->damage_height = picture->image.image.height;
    }
  }
  if (h->has_pickup && h->pickup_icon == NULL && h->pickup_path[0] != '\0') {
    char *path = icon_path(h->pickup_path);
    h->pickup_icon = load_optional(h, icons, h->pickup_content, path);
    free(path);
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
        h->pois[i].width, h->pois[i].height, h->pois[i].tint, (qa_js_to_int32(h->pois[i].flags) & 1) != 0, h->pois[i].expires};
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
    long flags = qa_js_to_int32(h->flags); /* flags & 7, (flags >> 3) & 7 */
    snprintf(buf, sizeof buf, "Red flag %s - Blue flag %s", flag_state(flags & 7), flag_state((flags >> 3) & 7));
    prompts[prompt_count++] = (qa_ui_hud_prompt){scratch_str(h, buf), "", NULL};
    const struct { long bit; const char *label; } runes[4] = {{32, "Resistance"}, {64, "Strength"}, {128, "Haste"}, {256, "Regeneration"}};
    for (int i = 0; i < 4; i++) if ((qa_js_to_int32(h->rune_items) & runes[i].bit) != 0) prompts[prompt_count++] = (qa_ui_hud_prompt){runes[i].label, "", NULL};
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
