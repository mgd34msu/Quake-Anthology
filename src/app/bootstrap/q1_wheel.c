/* Port of src/app/bootstrap/q1-wheel.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q1_wheel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
static const qa_item_id base_items[] = {"q1:weapon/axe", "q1:weapon/shotgun", "q1:weapon/supershotgun",
    "q1:weapon/nailgun", "q1:weapon/supernailgun", "q1:weapon/grenadelauncher",
    "q1:weapon/rocketlauncher", "q1:weapon/lightning"};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
qa_item_id qa_app_q1_wheel_slot_item(const qa_ui_q1_wheel_slot *slot, const char *campaign) {
  if (!slot->has_impulse) return NULL;
  double impulse = slot->impulse;
  if (impulse >= 1 && impulse <= 8) return base_items[(int)impulse - 1];
  if (is(campaign, "hipnotic"))
    return impulse == 225 ? "q1:weapon/hipnotic:laser" : impulse == 226 ? "q1:weapon/hipnotic:mjolnir"
        : impulse == 227 ? "q1:weapon/hipnotic:proximity" : impulse == 228 ? "q1:weapon/grenadelauncher" : NULL;
  if (is(campaign, "mg3")) return impulse == 225 ? "q1:weapon/mg3:laser" : NULL;
  if (is(campaign, "ctf")) return impulse == 22 ? "q1:weapon/ctf:grapple" : NULL;
  if (is(campaign, "rogue")) {
    if (impulse == 22) return "q1:weapon/rogue:grapple";
    static const qa_item_id powered[] = {"q1:weapon/rogue:lava-nailgun", "q1:weapon/rogue:lava-supernailgun",
        "q1:weapon/rogue:multi-grenade", "q1:weapon/rogue:multi-rocket", "q1:weapon/rogue:plasma"};
    if (impulse >= 60 && impulse <= 64) return powered[(int)impulse - 60];
    if (impulse >= 65 && impulse <= 68) return base_items[(int)impulse - 62];
  }
  return NULL;
}
typedef struct image_entry { char *path; qa_resource_id id; } image_entry;
struct qa_app_application_q1_wheel {
  char *content; /* owned content id, NULL = TS null */
  char *campaign;
  qa_ui_q1_wheel_parse parsed;
  bool has_parsed;
  image_entry *images; size_t image_count, image_capacity;
};
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
qa_app_application_q1_wheel *qa_app_application_q1_wheel_create(void) {
  qa_app_application_q1_wheel *wheel = calloc(1, sizeof *wheel);
  if (wheel == NULL) qa_range_error("Out of memory");
  wheel->campaign = dup_text("");
  return wheel;
}
static void clear_images(qa_app_application_q1_wheel *wheel) {
  for (size_t i = 0; i < wheel->image_count; i++) free(wheel->images[i].path);
  wheel->image_count = 0;
}
void qa_app_application_q1_wheel_destroy(qa_app_application_q1_wheel *wheel) {
  if (wheel == NULL) return;
  free(wheel->content); free(wheel->campaign);
  if (wheel->has_parsed) qa_ui_q1_wheel_parse_free(&wheel->parsed);
  clear_images(wheel); free(wheel->images); free(wheel);
}
static void set_image(qa_app_application_q1_wheel *wheel, const char *path, qa_resource_id id) {
  if (wheel->image_count == wheel->image_capacity) {
    size_t cap = wheel->image_capacity == 0 ? 8 : wheel->image_capacity * 2;
    image_entry *next = realloc(wheel->images, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    wheel->images = next; wheel->image_capacity = cap;
  }
  wheel->images[wheel->image_count].path = dup_text(path);
  wheel->images[wheel->image_count].id = id;
  wheel->image_count++;
}
static qa_resource_id get_image(const qa_app_application_q1_wheel *wheel, const char *path) {
  for (size_t i = 0; i < wheel->image_count; i++)
    if (strcmp(wheel->images[i].path, path) == 0) return wheel->images[i].id;
  return NULL;
}
typedef struct icon_load { qa_app_weapon_hud_assets *icons; qa_weapon_hud_icon icon; qa_resource_id id; } icon_load;
static void icon_load_run(void *ctx) {
  icon_load *load = ctx;
  load->id = qa_app_weapon_hud_assets_load(load->icons, load->icon);
}
static char *joined_text(const char *a, const char *b, const char *c, const char *d, const char *e) {
  size_t length = strlen(a) + strlen(b) + strlen(c) + strlen(d) + strlen(e) + 1;
  char *out = malloc(length);
  if (out == NULL) qa_range_error("Out of memory");
  snprintf(out, length, "%s%s%s%s%s", a, b, c, d, e);
  return out;
}
static void clear_parsed(qa_app_application_q1_wheel *wheel) {
  if (wheel->has_parsed) qa_ui_q1_wheel_parse_free(&wheel->parsed);
  memset(&wheel->parsed, 0, sizeof wheel->parsed);
  wheel->has_parsed = false;
}
void qa_app_application_q1_wheel_prepare(qa_app_application_q1_wheel *wheel, qa_app_application_assets *assets,
    qa_app_weapon_hud_assets *icons, const qa_app_sim_player_ui *player) {
  if (player->weapon_status == NULL) {
    clear_parsed(wheel);
    free(wheel->content); wheel->content = NULL;
    return;
  }
  qa_content_id content = player->weapon_status->source.content;
  if (wheel->content != NULL && strcmp(wheel->content, content) == 0) return;
  free(wheel->content); wheel->content = dup_text(content);
  clear_parsed(wheel);
  clear_images(wheel);
  const qa_catalog_discovered_product *product =
      qa_content_catalog_product(qa_app_application_assets_catalog(assets), content);
  if (product->expectation.family != QA_GAME_FAMILY_Q1 || !is(product->expectation.edition, "rerelease")) return;
  free(wheel->campaign); wheel->campaign = dup_text(product->expectation.campaign);
  qa_app_provider_scene_assets provider = qa_app_application_assets_provider(assets, content);
  qa_content_opened_resource file;
  if (!qa_content_mounted_open(provider.mounts, "wwheel.txt", NULL, NULL, &file)) return;
  /* new TextDecoder().decode(bytes): the C text is the bytes (invalid UTF-8 is not replaced). */
  char *text = malloc(file.byte_length + 1);
  if (text == NULL) qa_range_error("Out of memory");
  memcpy(text, file.bytes, file.byte_length); text[file.byte_length] = '\0';
  free(file.bytes);
  qa_ui_q1_wheel_parse parsed;
  memset(&parsed, 0, sizeof parsed);
  bool ok = qa_ui_parse_q1_weapon_wheel(text, &parsed);
  free(text);
  if (!ok) qa_range_error("Out of memory");
  if (parsed.error_count != 0) {
    /* The wheel is optional mod data: a bad one leaves the default wheel. */
    size_t length = 1;
    for (size_t i = 0; i < parsed.error_count; i++) length += strlen(parsed.errors[i]) + 2;
    char *errors = calloc(length, 1);
    if (errors == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < parsed.error_count; i++) {
      if (i != 0) strcat(errors, "; ");
      strcat(errors, parsed.errors[i]);
    }
    qa_ui_q1_wheel_parse_free(&parsed);
    char *key = joined_text("q1-wheel:", content, "", "", "");
    char *message = joined_text("Ignoring invalid authored weapon wheel: ", errors, "\n", "", "");
    qa_app_application_assets_warn_once(assets, key, message);
    free(key); free(message); free(errors);
    return;
  }
  wheel->parsed = parsed; wheel->has_parsed = true;
  for (size_t i = 0; i < wheel->parsed.slot_count; i++) {
    const char *paths[2] = {wheel->parsed.slots[i].icon, wheel->parsed.slots[i].selected_icon};
    for (size_t p = 0; p < 2; p++) {
      if (paths[p] == NULL || get_image(wheel, paths[p]) != NULL) continue;
      icon_load load;
      memset(&load, 0, sizeof load);
      load.icons = icons;
      load.icon.kind = QA_WEAPON_HUD_ICON_IMAGE;
      load.icon.resource.content = content;
      load.icon.resource.path = paths[p];
      qa_caught_error caught;
      if (qa_try(icon_load_run, &load, &caught)) { set_image(wheel, paths[p], load.id); continue; }
      char *key = joined_text("q1-wheel-icon:", content, "/", paths[p], "");
      char *message = joined_text("Weapon wheel icon ", paths[p], " unavailable: ", caught.message, "\n");
      qa_app_application_assets_warn_once(assets, key, message);
      free(key); free(message);
    }
  }
}
static const qa_app_sim_player_ui_item *find_item(const qa_app_sim_player_ui *player, qa_item_id id, bool owned_only) {
  if (id == NULL) return NULL;
  for (size_t i = 0; i < player->item_count; i++)
    if (strcmp(player->items[i].id, id) == 0 && (!owned_only || player->items[i].owned)) return &player->items[i];
  return NULL;
}
static qa_item_id slot_item(const qa_app_application_q1_wheel *wheel,
    const qa_ui_q1_wheel_slot *slot, const qa_app_sim_player_ui *player) {
  if (is(wheel->campaign, "mg3") && slot->has_impulse && slot->impulse == 1
      && find_item(player, "q1:weapon/mg3:mjolnir", true) != NULL)
    return "q1:weapon/mg3:mjolnir";
  return qa_app_q1_wheel_slot_item(slot, wheel->campaign);
}
qa_item_id qa_app_application_q1_wheel_switch_weapon(qa_app_application_q1_wheel *wheel,
    const qa_app_sim_player_ui *player, double first, double second) {
  const qa_app_sim_player_ui_item *resolved[2] = {NULL, NULL};
  double ordinals[2] = {first, second};
  for (size_t k = 0; k < 2; k++) {
    double ordinal = ordinals[k];
    if (!isfinite(ordinal) || ordinal != trunc(ordinal) || ordinal < 0) continue;
    const qa_ui_q1_wheel_slot *slot = NULL;
    for (size_t i = 0; i < wheel->parsed.slot_count && slot == NULL; i++)
      if (wheel->parsed.slots[i].slot == ordinal) slot = &wheel->parsed.slots[i];
    qa_item_id id;
    if (slot == NULL) {
      double index = ordinal == 7 ? 0 : ordinal + 1;
      id = index >= 0 && index < 8 ? base_items[(int)index] : NULL;
    } else id = slot_item(wheel, slot, player);
    resolved[k] = find_item(player, id, true);
  }
  const qa_app_sim_player_ui_item *a = resolved[0], *b = resolved[1];
  const qa_app_sim_player_ui_item *chosen =
      a != NULL && player->active_weapon != NULL && strcmp(a->id, player->active_weapon) == 0
          ? (b != NULL ? b : a) : (a != NULL ? a : b);
  return chosen != NULL ? chosen->id : NULL;
}
typedef struct items_ctx {
  qa_app_application_q1_wheel *wheel;
  const qa_app_sim_player_ui *player;
  char **label_scratch; size_t label_count, label_cap;
} items_ctx;
static const qa_app_sim_player_ui_item *ctx_item(items_ctx *ctx, const qa_ui_q1_wheel_slot *slot) {
  return find_item(ctx->player, slot_item(ctx->wheel, slot, ctx->player), false);
}
static double ctx_entity_float(void *v, double offset) {
  items_ctx *ctx = v;
  for (size_t i = 0; i < ctx->wheel->parsed.slot_count; i++) {
    const qa_ui_q1_wheel_slot *slot = &ctx->wheel->parsed.slots[i];
    if (slot->has_entity_variable_byte_offset && slot->entity_variable_byte_offset == offset) {
      const qa_app_sim_player_ui_item *item = ctx_item(ctx, slot);
      return item != NULL && item->has_count ? item->count : 0;
    }
  }
  return 0;
}
static const char *ctx_label(void *v, const qa_ui_q1_wheel_slot *slot) {
  items_ctx *ctx = v;
  const qa_app_sim_player_ui_item *item = ctx_item(ctx, slot);
  if (item != NULL && item->label != NULL) return item->label;
  char *label = malloc(48);
  if (label == NULL) qa_range_error("Out of memory");
  snprintf(label, 48, "Slot %s", QA_JS_NUMBER(slot->slot + 1));
  if (ctx->label_count + 1 >= ctx->label_cap) {
    ctx->label_cap = ctx->label_cap ? ctx->label_cap * 2 : 8;
    char **grown = realloc(ctx->label_scratch, ctx->label_cap * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    ctx->label_scratch = grown;
  }
  ctx->label_scratch[ctx->label_count++] = label;
  ctx->label_scratch[ctx->label_count] = NULL;
  return label;
}
static qa_resource_id ctx_image(void *v, const char *path) {
  items_ctx *ctx = v;
  return get_image(ctx->wheel, path);
}
bool qa_app_application_q1_wheel_items(qa_app_application_q1_wheel *wheel,
    const qa_app_sim_player_ui *player, qa_app_q1_wheel_items_result *out) {
  memset(out, 0, sizeof *out);
  if (wheel->parsed.slot_count == 0) return false;
  double bits = 0;
  for (size_t i = 0; i < wheel->parsed.slot_count; i++) {
    const qa_ui_q1_wheel_slot *slot = &wheel->parsed.slots[i];
    const qa_app_sim_player_ui_item *item = find_item(player, slot_item(wheel, slot, player), false);
    bits = qa_js_bit_or(bits, item != NULL && item->owned ? (slot->has_weapon_bits ? slot->weapon_bits : 0) : 0);
  }
  items_ctx ctx = {wheel, player, NULL, 0, 0};
  ctx.label_cap = 8;
  ctx.label_scratch = calloc(ctx.label_cap, sizeof *ctx.label_scratch);
  if (ctx.label_scratch == NULL) qa_range_error("Out of memory");
  qa_ui_q1_wheel_state state = {&ctx, bits, ctx_entity_float, ctx_label, ctx_image};
  if (!qa_ui_q1_wheel_items_for(wheel->parsed.slots, wheel->parsed.slot_count, &state, &out->owned))
    qa_range_error("Out of memory");
  out->items = out->owned.items;
  out->count = out->owned.count;
  out->labels = (const char **)ctx.label_scratch;
  for (size_t i = 0; i < out->count; i++) {
    const qa_ui_q1_wheel_slot *slot = NULL;
    for (size_t s = 0; s < wheel->parsed.slot_count && slot == NULL; s++)
      if (wheel->parsed.slots[s].slot == out->items[i].source_ordinal) slot = &wheel->parsed.slots[s];
    const qa_app_sim_player_ui_item *selected = slot != NULL ? find_item(player, slot_item(wheel, slot, player), false) : NULL;
    if (selected != NULL) out->items[i].id = selected->id;
    out->items[i].owned = selected != NULL ? selected->owned : false;
    out->items[i].has_ammo = selected != NULL ? selected->has_ammo : false;
  }
  return true;
}
void qa_app_q1_wheel_items_result_destroy(qa_app_q1_wheel_items_result *result) {
  if (result == NULL) return;
  qa_ui_q1_wheel_items_free(&result->owned);
  if (result->labels != NULL) {
    for (size_t i = 0; result->labels[i] != NULL; i++) free((void *)result->labels[i]);
    free((void *)result->labels);
  }
  memset(result, 0, sizeof *result);
}
