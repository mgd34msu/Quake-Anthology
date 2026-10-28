/* Port of src/app/bootstrap/q1-localization.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q1_localization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q1/base/finales.h"
#include "qa/content/q1/base/messages.h"
#include "qa/content/q1/missionpacks/world/finale_text.h"
#include "qa/foundation/support.h"
/* content/q1/foundation/text.h is landed without its .c in this
 * tree; classicQ1Text is inlined here from the landed finale and
 * obituary helpers plus the TS classicQ1Messages table. Supersede
 * this with qa_q1_classic_text once text.c lands. */
static const struct { const char *key, *value; } classic_messages[] = {
  {"$qc_found_secret", "You found a secret area!"}, {"$qc_more_go", "There are more to go..."},
  {"$qc_three_more", "Only 3 more to go..."}, {"$qc_two_more", "Only 2 more to go..."},
  {"$qc_one_more", "Only 1 more to go..."}, {"$qc_sequence_completed", "Sequence completed!"},
  {"$qc_need_gold_key", "You need the gold key"}, {"$qc_need_gold_runekey", "You need the gold runekey"},
  {"$qc_need_gold_keycard", "You need the gold keycard"}, {"$qc_need_silver_key", "You need the silver key"},
  {"$qc_need_silver_runekey", "You need the silver runekey"}, {"$qc_need_silver_keycard", "You need the silver keycard"},
  {"$qc_already_have_rune", "You already have a rune\n"}, {"$qc_rune_resistance", "Earth Magic\n\nRESISTANCE"},
  {"$qc_rune_strength", "Black Magic\n\nSTRENGTH"}, {"$qc_rune_haste", "Hell Magic\n\nHASTE"},
  {"$qc_rune_regeneration", "Edler Magic\n\nRegeneration"},
  {"$qc_color_games", "You were told you can't change teams.\nGo play color games somewhere else.\n"},
  {"$qc_cannot_change_teams", "You cannot change teams.\n"}, {"$qc_ctf_disabled", "Capture the Flag is not enabled.\n"},
  {"$qc_flag_missing", "The flag is missing!\n"}, {"$qc_flag_at_base", "The flag is at base!\n"},
  {"$qc_flag_lying_about", "The flag is lying about!\n"}, {"$qc_you_have_flag", "You have the flag!\n"},
  {"$qc_flag_screwed_up", "The flag is screwed up!\n"}, {"$qc_you_have_enemy_flag", "You have the enemy flag.\n"},
  {"$qc_flag_returned", "The flag has been returned!\n"}, {"$qc_your_flag_returned_base", "Your flag has been returned to base!\n"},
  {"$qc_enemy_flag_returned_base", "Enemy flag has been returned to base!\n"},
  {"$qc_your_team_captured", "Your team captured the flag!\n"}, {"$qc_your_flag_captured", "Your flag was captured!\n"},
  {"$qc_flag_taken", "The flag has been taken!\n"}, {"$qc_your_flag_taken", "Your flag has been taken!\n"},
  {"$qc_enemy_killed_bonus", "Enemy flag carrier killed: {0} bonus frags\n"},
  {"$qc_enemy_killed_no_bonus", "Enemy flag carrier killed, no bonus\n"},
  {"$qc_has_token", "{0} has the tag token!\n"}, {"$qc_lost_token", "{0} lost the tag token!\n"},
  {"$qc_got_token", "{0} got the tag token!\n"}, {"$qc_got_quad", "You got the Quad Damage\n"},
};
static const char *classic_message(const char *key) {
  for (size_t i = 0; i < sizeof classic_messages / sizeof classic_messages[0]; i++)
    if (strcmp(classic_messages[i].key, key) == 0) return classic_messages[i].value;
  return NULL;
}
#include "qa/text/localization.h"
typedef struct catalog_entry {
  char *content;
  char *language;
  qa_localization_catalog catalog;
} catalog_entry;
struct qa_app_q1_message_localization {
  qa_seat_id seat;
  qa_app_q1_localization_assets assets;
  qa_app_q1_language_fn language_fn;
  void *language_ctx;
  catalog_entry *entries; size_t count, capacity;
};
static char *dup_text(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
qa_app_q1_message_localization *qa_app_q1_message_localization_create(qa_seat_id seat,
    qa_app_q1_localization_assets assets, qa_app_q1_language_fn language, void *language_ctx) {
  qa_app_q1_message_localization *localization = calloc(1, sizeof *localization);
  if (localization == NULL) qa_range_error("Out of memory");
  localization->seat = seat;
  localization->assets = assets;
  localization->language_fn = language;
  localization->language_ctx = language_ctx;
  return localization;
}
void qa_app_q1_message_localization_destroy(qa_app_q1_message_localization *localization) {
  if (localization == NULL) return;
  for (size_t i = 0; i < localization->count; i++) {
    free(localization->entries[i].content);
    free(localization->entries[i].language);
    qa_localization_catalog_destroy(&localization->entries[i].catalog);
  }
  free(localization->entries);
  free(localization);
}
typedef struct tier_bytes {
  uint8_t *base; size_t base_length;
  uint8_t *mod; size_t mod_length;
} tier_bytes;
static tier_bytes load_tier(qa_content_mounted *mounts, const char *name) {
  tier_bytes tier = {0};
  char path[256];
  qa_content_opened_resource resource;
  snprintf(path, sizeof path, "localization/loc_%s.txt", name);
  if (qa_content_mounted_open(mounts, path, NULL, NULL, &resource)) {
    tier.base = resource.bytes; tier.base_length = resource.byte_length;
  }
  snprintf(path, sizeof path, "localization/loc_%s_mod.txt", name);
  if (qa_content_mounted_open(mounts, path, NULL, NULL, &resource)) {
    tier.mod = resource.bytes; tier.mod_length = resource.byte_length;
  }
  return tier;
}
static qa_localization_table *table_for(qa_app_q1_message_localization *localization,
    qa_content_id content, const char *language) {
  for (size_t i = 0; i < localization->count; i++)
    if (strcmp(localization->entries[i].content, content) == 0
        && strcmp(localization->entries[i].language, language) == 0)
      return &localization->entries[i].catalog.table;
  if (localization->count == localization->capacity) {
    size_t cap = localization->capacity == 0 ? 4 : localization->capacity * 2;
    catalog_entry *next = realloc(localization->entries, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    localization->entries = next; localization->capacity = cap;
  }
  catalog_entry *entry = &localization->entries[localization->count];
  memset(entry, 0, sizeof *entry);
  entry->content = dup_text(content);
  entry->language = dup_text(language);
  qa_localization_catalog_init(&entry->catalog, localization->seat, QA_LOC_Q1_RERELEASE);
  localization->count++;
  qa_content_mounted *mounts = localization->assets.for_content(localization->assets.ctx, content);
  tier_bytes primary = load_tier(mounts, language);
  tier_bytes fallback = {0};
  if (strcmp(language, "english") != 0) fallback = load_tier(mounts, "english");
  qa_loc_load_tier primary_tier = {primary.base != NULL, {primary.base, primary.base_length}, NULL, 0};
  qa_byte_view primary_mod = {primary.mod, primary.mod_length};
  if (primary.mod != NULL) { primary_tier.mods = &primary_mod; primary_tier.mod_count = 1; }
  qa_loc_load_tier fallback_tier = {fallback.base != NULL, {fallback.base, fallback.base_length}, NULL, 0};
  qa_byte_view fallback_mod = {fallback.mod, fallback.mod_length};
  if (fallback.mod != NULL) { fallback_tier.mods = &fallback_mod; fallback_tier.mod_count = 1; }
  qa_localization_table_load_ordered(&entry->catalog.table, primary_tier, fallback_tier, NULL);
  if (primary.base == NULL && fallback.base == NULL) {
    if (fallback.mod != NULL) qa_localization_table_merge(&entry->catalog.table, fallback_mod, NULL);
    if (primary.mod != NULL) qa_localization_table_merge(&entry->catalog.table, primary_mod, NULL);
  }
  free(primary.base); free(primary.mod); free(fallback.base); free(fallback.mod);
  return &entry->catalog.table;
}
static char *arg_text(const qa_q1_msg_arg *arg, char *scratch, size_t size) {
  if (!arg->is_number) return (char *)arg->text;
  if (arg->number == (double)(long long)arg->number)
    snprintf(scratch, size, "%lld", (long long)arg->number);
  else snprintf(scratch, size, "%g", arg->number);
  return scratch;
}
/* TS classicQ1Text (inlined, see above). Malloc'd result. */
static char *classic_text(const char *value, const qa_q1_msg_arg *args, size_t arg_count) {
  const char *format = classic_message(value);
  char *obituary = NULL;
  if (format == NULL) {
    const char **texts = calloc(arg_count == 0 ? 1 : arg_count, sizeof *texts);
    char (*scratch)[64] = calloc(arg_count == 0 ? 1 : arg_count, sizeof *scratch);
    if (texts == NULL || scratch == NULL) qa_range_error("Out of memory");
    size_t size = strlen(value) + arg_count * 128 + 256;
    obituary = malloc(size);
    if (obituary == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < arg_count; i++) {
      if (args[i].is_number) {
        if (args[i].number == (double)(long long)args[i].number)
          snprintf(scratch[i], 64, "%lld", (long long)args[i].number);
        else snprintf(scratch[i], 64, "%g", args[i].number);
        texts[i] = scratch[i];
      } else texts[i] = args[i].text;
    }
    qa_q1_classic_obituary_text(value, texts, arg_count, obituary, size);
    free(texts); free(scratch);
    format = qa_q1_finale_text("classic", qa_q1mp_mission_finale_text("classic", obituary));
  }
  size_t capacity = strlen(format) + arg_count * 128 + 64;
  char *out = malloc(capacity);
  if (out == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  for (const char *p = format; *p != '\0';) {
    if (*p == '{') {
      char *end = NULL;
      long index = strtol(p + 1, &end, 10);
      if (end != p + 1 && *end == '}') {
        const char *piece;
        char piece_scratch[64];
        size_t piece_len;
        if (index >= 0 && (size_t)index < arg_count) {
          const qa_q1_msg_arg *arg = &args[index];
          if (arg->is_number) {
            if (arg->number == (double)(long long)arg->number)
              snprintf(piece_scratch, sizeof piece_scratch, "%lld", (long long)arg->number);
            else snprintf(piece_scratch, sizeof piece_scratch, "%g", arg->number);
            piece = piece_scratch;
          } else {
            const char *mapped = classic_message(arg->text);
            piece = mapped != NULL ? mapped : arg->text;
          }
          piece_len = strlen(piece);
        } else { piece = p; piece_len = (size_t)(end + 1 - p); }
        if (at + piece_len + 1 > capacity) {
          capacity = (at + piece_len + 1) * 2;
          char *next = realloc(out, capacity);
          if (next == NULL) qa_range_error("Out of memory");
          out = next;
        }
        memcpy(out + at, piece, piece_len); at += piece_len;
        p = end + 1;
        continue;
      }
    }
    if (at + 2 > capacity) {
      capacity *= 2;
      char *next = realloc(out, capacity);
      if (next == NULL) qa_range_error("Out of memory");
      out = next;
    }
    out[at++] = *p++;
  }
  out[at] = '\0';
  free(obituary);
  return out;
}
static char *localize(qa_localization_table *table, bool rerelease, const char *value,
    const qa_q1_msg_arg *args, size_t arg_count) {
  bool dollar = value[0] == '$';
  const qa_loc_string *found = dollar ? qa_localization_table_find(table, value + 1) : NULL;
  if (!rerelease && (dollar ? found == NULL : true)) return classic_text(value, args, arg_count);
  if (dollar && found == NULL) return dup_text(value);
  const qa_loc_string *entry = dollar ? found : NULL;
  if (entry != NULL) {
    bool unknown_dollar = false;
    for (size_t i = 0; i < arg_count; i++) {
      if (!args[i].is_number && args[i].text != NULL && args[i].text[0] == '$'
          && qa_localization_table_find(table, args[i].text + 1) == NULL) unknown_dollar = true;
    }
    if (unknown_dollar) {
      size_t capacity = strlen(entry->format) + arg_count * 128 + 256;
      char *result = malloc(capacity);
      if (result == NULL) qa_range_error("Out of memory");
      size_t at = 0, start = 0;
      for (size_t i = 0; i < entry->argument_count; i++) {
        const qa_loc_arg *slot = &entry->arguments[i];
        if (slot->arg_index < 0 || (size_t)slot->arg_index >= arg_count) { free(result); return dup_text(value); }
        char scratch[64];
        const char *text = arg_text(&args[slot->arg_index], scratch, sizeof scratch);
        char *known_text = NULL;
        const char *piece = text;
        if (!(text[0] == '$' && qa_localization_table_find(table, text + 1) == NULL)) {
          known_text = qa_localization_table_localize(table, text, NULL, 0, true, 1024, NULL);
          if (known_text != NULL) piece = known_text;
        }
        size_t slice = slot->start - start, plen = strlen(piece);
        if (at + slice + plen + 1 > capacity) {
          capacity = (at + slice + plen + 1) * 2;
          char *next = realloc(result, capacity);
          if (next == NULL) qa_range_error("Out of memory");
          result = next;
        }
        memcpy(result + at, entry->format + start, slice); at += slice;
        memcpy(result + at, piece, plen); at += plen;
        free(known_text);
        start = slot->end;
      }
      size_t tail = strlen(entry->format + start);
      if (at + tail + 1 > capacity) {
        char *next = realloc(result, at + tail + 1);
        if (next == NULL) qa_range_error("Out of memory");
        result = next;
      }
      memcpy(result + at, entry->format + start, tail); at += tail;
      result[at] = '\0';
      return result;
    }
  }
  const char **texts = calloc(arg_count == 0 ? 1 : arg_count, sizeof *texts);
  char (*scratch)[64] = calloc(arg_count == 0 ? 1 : arg_count, sizeof *scratch);
  if (texts == NULL || scratch == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < arg_count; i++) texts[i] = arg_text(&args[i], scratch[i], 64);
  char *out = qa_localization_table_localize(table, value, texts, arg_count, true, 1024, NULL);
  free(texts); free(scratch);
  if (out == NULL) qa_range_error("Out of memory");
  return out;
}
char *qa_app_q1_message_localization_resolve(qa_app_q1_message_localization *localization,
    qa_content_id content, const char *text, const qa_q1_msg_arg *args, size_t arg_count,
    const qa_q1_message_part *parts, size_t part_count) {
  const qa_catalog_product_expectation *product =
      &qa_content_catalog_product(localization->assets.catalog, content)->expectation;
  if (product->family != QA_GAME_FAMILY_Q1) return dup_text(text);
  bool rerelease = strcmp(product->edition, "rerelease") == 0;
  if (!rerelease && text[0] != '$' && arg_count == 0 && part_count == 0) return dup_text(text);
  const char *language = localization->language_fn != NULL
      ? localization->language_fn(localization->language_ctx) : "english";
  qa_localization_table *table = table_for(localization, content, language);
  if (part_count == 0) return localize(table, rerelease, text, args, arg_count);
  size_t capacity = 256, at = 0;
  char *joined = malloc(capacity);
  if (joined == NULL) qa_range_error("Out of memory");
  joined[0] = '\0';
  for (size_t i = 0; i < part_count; i++) {
    char *piece = localize(table, rerelease, parts[i].text, parts[i].args, parts[i].arg_count);
    size_t plen = strlen(piece);
    if (at + plen + 1 > capacity) {
      capacity = (at + plen + 1) * 2;
      char *next = realloc(joined, capacity);
      if (next == NULL) qa_range_error("Out of memory");
      joined = next;
    }
    memcpy(joined + at, piece, plen); at += plen; joined[at] = '\0';
    free(piece);
  }
  return joined;
}
