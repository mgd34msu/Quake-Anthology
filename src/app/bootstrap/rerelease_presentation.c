/* Port of src/app/bootstrap/rerelease-presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/rerelease_presentation.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q2_localization.h"
#include "qa/content/roots.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/materials/sky.h"
#include "qa/ui/settings/language.h"
#include "qa/ui/settings/services.h"
#include <strings.h>
#include "qa/foundation/js.h"
#define QA_APP_RERELEASE_RING ((size_t)8192)
typedef struct name_entry { double slot; char *name; } name_entry;
/* One LanguageChoice's load(): the loc files it opened stay with it until its next load (the catalog borrows them
 * while loading). */
typedef struct language_load {
  qa_content_mounted *mounts;
  char *language;
  qa_content_opened_resource files[4]; bool opened[4]; /* primary, primary mod, english, english mod */
  qa_byte_view primary_mod, fallback_mod;
} language_load;
/* TS seat.catalogs entry: the NativeLanguageSettings of one content. Heap-stable (settings point at localization). */
typedef struct catalog_entry {
  char *content;
  qa_localization_catalog localization;
  qa_ui_native_language_settings settings;
  qa_ui_language_choice *choices; size_t choice_count;
  char **labels;
  language_load *loads;
} catalog_entry;
/* The binding languageBinding returns: { ...binding, read, write } over the seat. */
typedef struct language_binding {
  qa_app_rerelease_presentation *presentation;
  qa_seat_id seat; qa_actor_id actor;
  char *content;
  qa_ui_setting_binding base;
  void (*failed)(void *ctx, const char *error); void *failed_ctx;
} language_binding;
typedef struct seat_state {
  qa_app_rerelease_seat binding;
  char *language;
  qa_app_rerelease_fog fog;
  bool fog_received;
  char *story;
  bool has_story_source; char *story_content; char *story_text;
  catalog_entry **catalogs; size_t catalog_count, catalog_cap;
  qa_actor_id *hidden; size_t hidden_count, hidden_cap;
  name_entry *names; size_t name_count, name_cap;
  bool has_sky; char *sky_content; char *sky_name;
  qa_renderer_image sky_images[6];
  qa_q2_sky_view sky_view;
} seat_state;
struct qa_app_rerelease_presentation {
  qa_app_application_assets *assets;
  /* Cleared catalogs and returned bindings stay alive with the presentation (TS GC: a binding keeps its settings). */
  catalog_entry **retired; size_t retired_count, retired_cap;
  language_binding **bindings; size_t binding_count, binding_cap;
  qa_cvar_registry *language_settings;
  seat_state *seats; size_t seat_count;
  name_entry *names; size_t name_count, name_cap;
  /* TS EventRing<RereleaseSource | q2-player>(8192) and
   * EventRing<SimulationPresentationEvent>(8192): owned copies. */
  qa_app_sim_presentation_event *pending; size_t pending_head, pending_count;
  qa_app_sim_presentation_event *prints; size_t print_head, print_count;
  qa_app_sim_presentation_event *drained; size_t drained_count, drained_cap;
  bool has_sky; char *sky_content; char *sky_name;
  qa_renderer_image sky_images[6];
  qa_q2_sky_view sky_view;
  /* Sky image cache (content+name -> images). */
  struct sky_cache { char *content; char *name; qa_renderer_image images[6]; } *skies;
  size_t sky_count, sky_cap;
  /* Localized string scratch for bindings. */
  char *language_scratch;
};
struct qa_app_rerelease_refresh {
  qa_app_rerelease_presentation *presentation;
  bool has_next; qa_renderer_image next_images[6];
  struct { size_t seat; bool has_sky; qa_renderer_image images[6]; } *seat_skies;
  size_t seat_count;
};
static char *xdup(const char *s) { char *r = qa_input_dup(s ? s : ""); if (!r) qa_range_error("Out of memory"); return r; }
static void set_str(char **slot, const char *value) { free(*slot); *slot = xdup(value); }
char *qa_app_q2_player_name_tokens(const char *text, const double *slots, const char *const *names, size_t count) {
  size_t cap = strlen(text) * 2 + 64;
  char *out = calloc(cap, 1);
  if (!out) qa_range_error("Out of memory");
  size_t used = 0;
  for (const char *p = text; *p;) {
    if (p[0] == '#' && p[1] == '#' && p[2] == 'P' && isdigit((unsigned char)p[3])) {
      const char *digits = p + 3;
      /* /##P([0-9]+)/ then Number(digits): strtod also read "12.5" and "1e3". */
      const char *end = digits;
      while (*end >= '0' && *end <= '9') end++;
      double slot = qa_js_parse_int(digits, 10);
      const char *replacement = "";
      for (size_t i = 0; i < count; i++) if (slots[i] == slot) replacement = names[i];
      size_t add = strlen(replacement);
      while (used + add + 1 > cap) { cap *= 2; out = realloc(out, cap); if (!out) qa_range_error("Out of memory"); }
      memcpy(out + used, replacement, add); used += add;
      p = end;
    } else {
      if (used + 2 > cap) { cap *= 2; out = realloc(out, cap); if (!out) qa_range_error("Out of memory"); }
      out[used++] = *p++;
    }
  }
  out[used] = '\0';
  return out;
}
static seat_state make_seat(qa_app_rerelease_seat binding) {
  seat_state s; memset(&s, 0, sizeof s);
  s.binding = binding;
  qa_app_rerelease_fog_init(&s.fog);
  s.language = xdup(binding.language ? binding.language : "english");
  s.story = xdup("");
  return s;
}
static void free_catalog(catalog_entry *entry) {
  qa_ui_native_language_settings_destroy(&entry->settings);
  qa_localization_catalog_destroy(&entry->localization);
  for (size_t i = 0; i < entry->choice_count; i++) {
    language_load *load = &entry->loads[i];
    for (int f = 0; f < 4; f++) if (load->opened[f]) free(load->files[f].bytes);
    free(load->language);
    free(entry->labels[i]);
  }
  free(entry->loads); free(entry->labels); free(entry->choices);
  free(entry->content);
  free(entry);
}
static void push_pointer(void ***items, size_t *count, size_t *cap, void *item) {
  if (*count == *cap) {
    size_t grown = *cap ? *cap * 2 : 4;
    void **next = realloc(*items, grown * sizeof **items);
    if (!next) qa_range_error("Out of memory");
    *items = next; *cap = grown;
  }
  (*items)[(*count)++] = item;
}
/* TS seat.catalogs.clear(): the entries retire with the presentation. */
static void clear_catalogs(qa_app_rerelease_presentation *p, seat_state *seat) {
  for (size_t i = 0; i < seat->catalog_count; i++)
    push_pointer((void ***)&p->retired, &p->retired_count, &p->retired_cap, seat->catalogs[i]);
  seat->catalog_count = 0;
}
static void free_seat(qa_app_rerelease_presentation *p, seat_state *s) {
  clear_catalogs(p, s);
  free(s->catalogs);
  free(s->language); free(s->story); free(s->story_content); free(s->story_text);
  free(s->hidden);
  for (size_t i = 0; i < s->name_count; i++) free(s->names[i].name);
  free(s->names);
  free(s->sky_content); free(s->sky_name);
}
qa_app_rerelease_presentation *qa_app_rerelease_presentation_create(qa_app_application_assets *assets,
    const qa_app_rerelease_seat *seats, size_t seat_count, qa_cvar_registry *language_settings) {
  qa_app_rerelease_presentation *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->assets = assets;
  p->language_settings = language_settings;
  p->seats = calloc(seat_count ? seat_count : 1, sizeof *p->seats);
  if (!p->seats) qa_range_error("Out of memory");
  p->seat_count = seat_count;
  p->pending = calloc(QA_APP_RERELEASE_RING, sizeof *p->pending);
  p->prints = calloc(QA_APP_RERELEASE_RING, sizeof *p->prints);
  if (!p->pending || !p->prints) qa_range_error("Out of memory");
  for (size_t i = 0; i < seat_count; i++) {
    p->seats[i] = make_seat(seats[i]);
  }
  return p;
}
void qa_app_rerelease_presentation_destroy(qa_app_rerelease_presentation *p) {
  if (!p) return;
  for (size_t i = 0; i < p->seat_count; i++) free_seat(p, &p->seats[i]);
  free(p->seats);
  for (size_t i = 0; i < p->retired_count; i++) free_catalog(p->retired[i]);
  free(p->retired);
  for (size_t i = 0; i < p->binding_count; i++) { free(p->bindings[i]->content); free(p->bindings[i]); }
  free(p->bindings);
  for (size_t i = 0; i < p->name_count; i++) free(p->names[i].name);
  free(p->names);
  for (size_t i = 0; i < p->pending_count; i++)
    qa_app_sim_presentation_event_release(&p->pending[(p->pending_head + i) % QA_APP_RERELEASE_RING]);
  for (size_t i = 0; i < p->print_count; i++)
    qa_app_sim_presentation_event_release(&p->prints[(p->print_head + i) % QA_APP_RERELEASE_RING]);
  for (size_t i = 0; i < p->drained_count; i++) qa_app_sim_presentation_event_release(&p->drained[i]);
  free(p->pending); free(p->prints); free(p->drained);
  free(p->sky_content); free(p->sky_name);
  for (size_t i = 0; i < p->sky_count; i++) { free(p->skies[i].content); free(p->skies[i].name); }
  free(p->skies);
  free(p->language_scratch);
  free(p);
}
/* TS Pick<ApplicationAssets, "provider">: the assets themselves or a prepared image refresh. */
typedef struct providers { void *ctx; qa_app_provider_scene_assets (*provider)(void *ctx, qa_content_id content); } providers;
static qa_app_provider_scene_assets assets_provider(void *ctx, qa_content_id content) {
  return qa_app_application_assets_provider(ctx, content);
}
static qa_app_provider_scene_assets images_provider(void *ctx, qa_content_id content) {
  return qa_app_prepared_images_provider(ctx, content);
}
static void load_sky_images(providers source, qa_content_id content, const char *name, qa_renderer_image out[6]);
qa_app_rerelease_refresh *qa_app_rerelease_prepare_image_refresh(qa_app_rerelease_presentation *p, qa_app_prepared_images *images) {
  providers source = {images, images_provider};
  qa_app_rerelease_refresh *refresh = calloc(1, sizeof *refresh);
  if (!refresh) qa_range_error("Out of memory");
  refresh->presentation = p;
  refresh->has_next = p->has_sky;
  if (p->has_sky) load_sky_images(source, p->sky_content, p->sky_name, refresh->next_images);
  refresh->seat_skies = calloc(p->seat_count ? p->seat_count : 1, sizeof *refresh->seat_skies);
  if (!refresh->seat_skies) qa_range_error("Out of memory");
  refresh->seat_count = p->seat_count;
  for (size_t i = 0; i < p->seat_count; i++) {
    refresh->seat_skies[i].seat = i;
    refresh->seat_skies[i].has_sky = p->seats[i].has_sky;
    if (p->seats[i].has_sky) load_sky_images(source, p->seats[i].sky_content, p->seats[i].sky_name, refresh->seat_skies[i].images);
  }
  return refresh;
}
void qa_app_rerelease_refresh_publish(qa_app_rerelease_refresh *refresh) {
  qa_app_rerelease_presentation *p = refresh->presentation;
  for (size_t i = 0; i < p->sky_count; i++) { free(p->skies[i].content); free(p->skies[i].name); }
  p->sky_count = 0;
  if (refresh->has_next) {
    memcpy(p->sky_images, refresh->next_images, sizeof p->sky_images);
    p->sky_view.images = p->sky_images;
  } else p->has_sky = false;
  for (size_t i = 0; i < refresh->seat_count && i < p->seat_count; i++) {
    if (refresh->seat_skies[i].has_sky) {
      memcpy(p->seats[i].sky_images, refresh->seat_skies[i].images, sizeof p->seats[i].sky_images);
      p->seats[i].sky_view.images = p->seats[i].sky_images;
    } else p->seats[i].has_sky = false;
  }
  free(refresh->seat_skies);
  free(refresh);
}
void qa_app_rerelease_refresh_discard(qa_app_rerelease_refresh *refresh) {
  if (refresh == NULL) return;
  free(refresh->seat_skies);
  free(refresh);
}
void qa_app_rerelease_publish_seats(qa_app_rerelease_presentation *p, const qa_app_rerelease_seat *seats, size_t seat_count) {
  seat_state *next = calloc(seat_count ? seat_count : 1, sizeof *next);
  if (!next) qa_range_error("Out of memory");
  bool *retained = calloc(p->seat_count ? p->seat_count : 1, sizeof *retained);
  if (!retained) qa_range_error("Out of memory");
  for (size_t i = 0; i < seat_count; i++) {
    long found = -1;
    for (size_t j = 0; j < p->seat_count; j++)
      if (!retained[j] && qa_same_seat(p->seats[j].binding.seat, seats[i].seat) && qa_same_actor(p->seats[j].binding.actor, seats[i].actor)) found = (long)j;
    if (found >= 0) {
      next[i] = p->seats[found];
      next[i].sky_view.images = next[i].sky_images; /* the view points into the moved seat's own images */
      retained[found] = true;
    }
    else {
      next[i] = make_seat(seats[i]);
    }
  }
  for (size_t j = 0; j < p->seat_count; j++) if (!retained[j]) free_seat(p, &p->seats[j]);
  free(retained);
  free(p->seats);
  p->seats = next;
  p->seat_count = seat_count;
}
static void admit_pending(qa_app_rerelease_presentation *p, const qa_app_sim_presentation_event *event) {
  if (p->pending_count == QA_APP_RERELEASE_RING) qa_range_error("Event ring capacity exhausted (%zu undrained records); drain before admitting more", (size_t)QA_APP_RERELEASE_RING);
  if (!qa_app_sim_presentation_event_copy(&p->pending[(p->pending_head + p->pending_count) % QA_APP_RERELEASE_RING], event))
    qa_range_error("Out of memory");
  p->pending_count++;
}
static bool hidden_contains(seat_state *seat, qa_actor_id item) {
  for (size_t i = 0; i < seat->hidden_count; i++) if (qa_same_actor(seat->hidden[i], item)) return true;
  return false;
}
void qa_app_rerelease_receive(qa_app_rerelease_presentation *p, const qa_app_sim_presentation_event *events, size_t count) {
  for (size_t e = 0; e < count; e++) {
    const qa_app_sim_presentation_event *source = &events[e];
    if (strcmp(source->kind, "q2-player") == 0 && strcmp(source->q2_player->kind, "userinfo") == 0) admit_pending(p, source);
    if (strcmp(source->kind, "q2-rerelease") != 0) continue;
    const qa_q2_rerelease_event *event = source->q2_rerelease;
    if (strcmp(event->kind, "item-visibility") == 0) {
      for (size_t i = 0; i < p->seat_count; i++) {
        seat_state *seat = &p->seats[i];
        if (!qa_same_actor(seat->binding.actor, event->actor)) continue;
        if (source->has_recipient && !qa_same_actor(source->recipient, seat->binding.actor)) continue;
        for (size_t j = 0; j < seat->hidden_count; j++) if (qa_same_actor(seat->hidden[j], event->item)) {
          memmove(&seat->hidden[j], &seat->hidden[j + 1], (seat->hidden_count - j - 1) * sizeof(qa_actor_id));
          seat->hidden_count--;
          break;
        }
        if (!event->visible && !hidden_contains(seat, event->item)) {
          if (seat->hidden_count == seat->hidden_cap) { seat->hidden_cap = seat->hidden_cap ? seat->hidden_cap * 2 : 8; seat->hidden = realloc(seat->hidden, seat->hidden_cap * sizeof *seat->hidden); if (!seat->hidden) qa_range_error("Out of memory"); }
          seat->hidden[seat->hidden_count++] = event->item;
        }
      }
    }
    if (strcmp(event->kind, "fog") == 0) {
      for (size_t i = 0; i < p->seat_count; i++) {
        seat_state *seat = &p->seats[i];
        if (!qa_same_actor(seat->binding.actor, event->actor)) continue;
        if (source->has_recipient && !qa_same_actor(source->recipient, seat->binding.actor)) continue;
        qa_app_rerelease_fog_receive(&seat->fog, event->fog_value, event->transition_milliseconds, source->seconds);
        seat->fog_received = true;
      }
    } else if (strcmp(event->kind, "story") == 0 || strcmp(event->kind, "localized-print") == 0 || strcmp(event->kind, "sky") == 0) {
      admit_pending(p, source);
    }
  }
}
const char *qa_app_rerelease_selected_language(qa_app_rerelease_presentation *p, qa_seat_id id) {
  if (p->language_settings == NULL) {
    for (size_t i = 0; i < p->seat_count; i++) if (qa_same_seat(p->seats[i].binding.seat, id)) return p->seats[i].language;
    return "english";
  }
  free(p->language_scratch);
  p->language_scratch = qa_ui_read_seat_language(p->language_settings, id.index);
  return p->language_scratch ? p->language_scratch : "english";
}
static seat_state *find_seat(qa_app_rerelease_presentation *p, qa_seat_id id) {
  for (size_t i = 0; i < p->seat_count; i++) if (qa_same_seat(p->seats[i].binding.seat, id)) return &p->seats[i];
  return NULL;
}
static void sync_language(qa_app_rerelease_presentation *p, seat_state *seat) {
  const char *language = qa_app_rerelease_selected_language(p, seat->binding.seat);
  if (strcmp(language, seat->language) != 0) {
    set_str(&seat->language, language);
    clear_catalogs(p, seat);
  }
}
/* LanguageChoice.load: localization/loc_<language>.txt and _mod.txt, with the english pair as the fallback. */
static const char *language_load_files(void *ctx, qa_loc_load_tier *out_primary, qa_loc_load_tier *out_fallback) {
  language_load *load = ctx;
  for (int f = 0; f < 4; f++) {
    if (load->opened[f]) free(load->files[f].bytes);
    load->opened[f] = false;
    memset(&load->files[f], 0, sizeof load->files[f]);
  }
  bool english = strcmp(load->language, "english") == 0;
  size_t size = strlen(load->language) + 32;
  char *path = malloc(size);
  if (!path) qa_range_error("Out of memory");
  snprintf(path, size, "localization/loc_%s.txt", load->language);
  load->opened[0] = qa_content_mounted_open(load->mounts, path, NULL, NULL, &load->files[0]);
  snprintf(path, size, "localization/loc_%s_mod.txt", load->language);
  load->opened[1] = qa_content_mounted_open(load->mounts, path, NULL, NULL, &load->files[1]);
  free(path);
  if (!english) {
    load->opened[2] = qa_content_mounted_open(load->mounts, "localization/loc_english.txt", NULL, NULL, &load->files[2]);
    load->opened[3] = qa_content_mounted_open(load->mounts, "localization/loc_english_mod.txt", NULL, NULL, &load->files[3]);
  }
  /* primary: { base: primary?.bytes ?? null, mods: mod === null ? [] : [mod.bytes] }, fallback likewise. */
  memset(out_primary, 0, sizeof *out_primary);
  memset(out_fallback, 0, sizeof *out_fallback);
  out_primary->has_base = load->opened[0];
  if (load->opened[0]) out_primary->base = (qa_byte_view){load->files[0].bytes, load->files[0].byte_length};
  if (load->opened[1]) {
    load->primary_mod = (qa_byte_view){load->files[1].bytes, load->files[1].byte_length};
    out_primary->mods = &load->primary_mod; out_primary->mod_count = 1;
  }
  out_fallback->has_base = load->opened[2];
  if (load->opened[2]) out_fallback->base = (qa_byte_view){load->files[2].bytes, load->files[2].byte_length};
  if (load->opened[3]) {
    load->fallback_mod = (qa_byte_view){load->files[3].bytes, load->files[3].byte_length};
    out_fallback->mods = &load->fallback_mod; out_fallback->mod_count = 1;
  }
  return NULL;
}
static void language_failed_ignored(void *ctx, const char *message) { (void)ctx; (void)message; } /* () => undefined */
static int compare_text(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static bool has_text(char **items, size_t count, const char *text) {
  for (size_t i = 0; i < count; i++) if (strcmp(items[i], text) == 0) return true;
  return false;
}
/* The pending catalog's construction: NativeLanguageSettings over the provider's localization files, selected. */
static catalog_entry *create_catalog(qa_app_rerelease_presentation *p, seat_state *seat, qa_content_id content, const char *selection) {
  qa_app_provider_scene_assets provider = qa_app_application_assets_provider(p->assets, content);
  size_t file_count = 0;
  char **files = qa_content_mounted_list_files(provider.mounts, "localization", ".txt", &file_count);
  if (!files) qa_range_error("Out of memory");
  char **languages = calloc(file_count + 2, sizeof *languages);
  if (!languages) qa_range_error("Out of memory");
  size_t language_count = 0;
  languages[language_count++] = xdup("english"); /* new Set(["english", languageSelection]) */
  if (!has_text(languages, language_count, selection)) languages[language_count++] = xdup(selection);
  for (size_t i = 0; i < file_count; i++) {
    /* /^loc_([a-z]+)\.txt$/iu, then toLowerCase() */
    const char *file = files[i];
    size_t length = strlen(file);
    if (length < 9 || strncasecmp(file, "loc_", 4) != 0 || strcasecmp(file + length - 4, ".txt") != 0) continue;
    size_t letters = length - 8;
    bool alpha = true;
    for (size_t j = 0; j < letters; j++) if (!isalpha((unsigned char)file[4 + j])) alpha = false;
    if (!alpha) continue;
    char *language = malloc(letters + 1);
    if (!language) qa_range_error("Out of memory");
    for (size_t j = 0; j < letters; j++) language[j] = (char)tolower((unsigned char)file[4 + j]);
    language[letters] = '\0';
    if (has_text(languages, language_count, language)) { free(language); continue; }
    char **grown = realloc(languages, (language_count + 2) * sizeof *languages);
    if (!grown) qa_range_error("Out of memory");
    languages = grown;
    languages[language_count++] = language;
  }
  qa_content_free_string_list(files, file_count);
  qsort(languages, language_count, sizeof *languages, compare_text); /* [...languages].sort(): ASCII ids */
  catalog_entry *entry = calloc(1, sizeof *entry);
  if (!entry) qa_range_error("Out of memory");
  entry->content = xdup(content);
  entry->choice_count = language_count;
  entry->choices = calloc(language_count, sizeof *entry->choices);
  entry->labels = calloc(language_count, sizeof *entry->labels);
  entry->loads = calloc(language_count, sizeof *entry->loads);
  if (!entry->choices || !entry->labels || !entry->loads) qa_range_error("Out of memory");
  for (size_t i = 0; i < language_count; i++) {
    language_load *load = &entry->loads[i];
    load->mounts = provider.mounts;
    load->language = languages[i]; /* taken */
    entry->labels[i] = xdup(load->language);
    entry->labels[i][0] = (char)toupper((unsigned char)entry->labels[i][0]); /* charAt(0).toUpperCase() + slice(1) */
    entry->choices[i] = (qa_ui_language_choice){load->language, entry->labels[i], load, language_load_files};
  }
  free(languages);
  qa_localization_catalog_init(&entry->localization, seat->binding.seat,
      provider.family == QA_GAME_FAMILY_Q1 ? QA_LOC_Q1_RERELEASE : QA_LOC_Q2_RERELEASE);
  if (!qa_ui_native_language_settings_init(&entry->settings, &entry->localization, entry->choices, entry->choice_count, selection,
          language_failed_ignored, NULL))
    qa_range_error("Out of memory");
  const char *error = qa_ui_native_language_settings_select(&entry->settings, selection);
  if (error != NULL) {
    char message[1024];
    snprintf(message, sizeof message, "%s", error);
    free_catalog(entry); /* TS: a rejected pending catalog leaves seat.catalogs */
    qa_raise("Error", 0, 0, "%s", message);
  }
  return entry;
}
static seat_state *find_seat(qa_app_rerelease_presentation *p, qa_seat_id id);
/* TS catalog(seat, content). Seats are found again by identity after any call that can publish seats. */
static catalog_entry *catalog_for(qa_app_rerelease_presentation *p, seat_state *seat, qa_content_id content) {
  sync_language(p, seat);
  char *selection = xdup(seat->language);
  for (size_t i = 0; i < seat->catalog_count; i++)
    if (strcmp(seat->catalogs[i]->content, content) == 0) {
      catalog_entry *result = seat->catalogs[i];
      bool current = strcmp(qa_app_rerelease_selected_language(p, seat->binding.seat), selection) == 0;
      free(selection);
      return current ? result : catalog_for(p, seat, content);
    }
  catalog_entry *created = create_catalog(p, seat, content, selection);
  push_pointer((void ***)&seat->catalogs, &seat->catalog_count, &seat->catalog_cap, created);
  bool current = strcmp(qa_app_rerelease_selected_language(p, seat->binding.seat), selection) == 0;
  free(selection);
  return current ? created : catalog_for(p, seat, content);
}
bool qa_app_rerelease_item_visible(qa_app_rerelease_presentation *p, qa_actor_id actor, qa_actor_id item) {
  for (size_t i = 0; i < p->seat_count; i++)
    if (qa_same_actor(p->seats[i].binding.actor, actor)) return !hidden_contains(&p->seats[i], item);
  return true;
}
char *qa_app_rerelease_source_localize(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content, const char *text, const char *const *args, size_t arg_count) {
  seat_state *seat = find_seat(p, id);
  if (seat == NULL) qa_raise("Error", 0, 0, "Unknown localization seat");
  catalog_entry *catalog = catalog_for(p, seat, content);
  char *result = qa_app_q2_localized_text(&catalog->localization.table, text, args, arg_count);
  if (!result) qa_range_error("Out of memory");
  return result;
}
qa_app_rerelease_localizer *qa_app_rerelease_source_localizer(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content) {
  seat_state *seat = find_seat(p, id);
  if (seat == NULL) qa_raise("Error", 0, 0, "Unknown localization seat");
  return (qa_app_rerelease_localizer *)catalog_for(p, seat, content);
}
char *qa_app_rerelease_localizer_text(qa_app_rerelease_localizer *localizer, const char *text, const char *const *args, size_t arg_count) {
  char *result = qa_app_q2_localized_text(&((catalog_entry *)localizer)->localization.table, text, args, arg_count);
  if (!result) qa_range_error("Out of memory");
  return result;
}
char *qa_app_rerelease_localize_message(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content, const char *text, const char *const *args, size_t arg_count) {
  seat_state *seat = find_seat(p, id);
  if (seat == NULL) qa_raise("Error", 0, 0, "Unknown localization seat");
  catalog_entry *catalog = catalog_for(p, seat, content);
  char *localized = qa_app_q2_localized_text(&catalog->localization.table, text, args, arg_count);
  if (!localized) qa_range_error("Out of memory");
  /* new Map([...this.names, ...seat.names]): global names overridden by seat names. */
  size_t total = p->name_count + seat->name_count;
  double *slots = calloc(total ? total : 1, sizeof *slots);
  const char **names = calloc(total ? total : 1, sizeof *names);
  if (!slots || !names) qa_range_error("Out of memory");
  size_t n = 0;
  for (size_t i = 0; i < p->name_count; i++) { slots[n] = p->names[i].slot; names[n] = p->names[i].name; n++; }
  for (size_t i = 0; i < seat->name_count; i++) {
    bool replaced = false;
    for (size_t j = 0; j < n; j++) if (slots[j] == seat->names[i].slot) { names[j] = seat->names[i].name; replaced = true; }
    if (!replaced) { slots[n] = seat->names[i].slot; names[n] = seat->names[i].name; n++; }
  }
  char *result = qa_app_q2_player_name_tokens(localized, slots, names, n);
  free(slots); free(names); free(localized);
  return result;
}
/* languageBinding's { ...binding, read, write }. */
static bool binding_enabled(void *ctx) {
  language_binding *b = ctx;
  return b->base.enabled == NULL || b->base.enabled(b->base.ctx);
}
static const qa_ui_choice *binding_choices(void *ctx, size_t *out_count) {
  language_binding *b = ctx;
  return b->base.choices(b->base.ctx, out_count);
}
static const char *binding_read(void *ctx) {
  language_binding *b = ctx;
  return qa_app_rerelease_selected_language(b->presentation, b->seat);
}
static seat_state *find_bound_seat(language_binding *b) {
  for (size_t i = 0; i < b->presentation->seat_count; i++) {
    seat_state *seat = &b->presentation->seats[i];
    if (qa_same_seat(seat->binding.seat, b->seat) && qa_same_actor(seat->binding.actor, b->actor)) return seat;
  }
  return NULL;
}
typedef struct reload_call { language_binding *binding; } reload_call;
static void binding_reload(void *ctx) {
  language_binding *b = ((reload_call *)ctx)->binding;
  seat_state *seat = find_bound_seat(b);
  if (seat != NULL) (void)catalog_for(b->presentation, seat, b->content);
}
static void binding_write(void *ctx, const char *value) {
  language_binding *b = ctx;
  size_t count = 0;
  const qa_ui_choice *choices = b->base.choices(b->base.ctx, &count);
  bool installed = false;
  for (size_t i = 0; i < count; i++) if (strcmp(choices[i].id, value) == 0) installed = true;
  if (!installed) qa_raise("Error", 0, 0, "Language is not installed: %s", value);
  /* The closure's seat object: a seat publishSeats dropped no longer reaches any catalog or view. */
  seat_state *seat = find_bound_seat(b);
  if (b->presentation->language_settings != NULL) qa_ui_write_seat_language(b->presentation->language_settings, b->seat.index, value);
  else if (seat != NULL) set_str(&seat->language, value);
  if (seat != NULL) clear_catalogs(b->presentation, seat);
  /* this.catalog(seat, content).catch(failed) */
  reload_call call = {b};
  qa_caught_error caught;
  if (!qa_try(binding_reload, &call, &caught)) {
    /* failed(error): the C callback receives String(error), "<name>: <message>". */
    char text[QA_CAUGHT_MESSAGE_CAPACITY + 64];
    snprintf(text, sizeof text, "%s: %s", caught.name, caught.message);
    b->failed(b->failed_ctx, text);
  }
}
bool qa_app_rerelease_language_binding(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content,
                                       void (*failed)(void *ctx, const char *error), void *failed_ctx, qa_ui_setting_binding *out) {
  qa_game_family family = qa_app_application_assets_provider(p->assets, content).family;
  if (family != QA_GAME_FAMILY_Q1 && family != QA_GAME_FAMILY_Q2) return false;
  seat_state *seat = find_seat(p, id);
  if (seat == NULL) qa_raise("Error", 0, 0, "Unknown localization seat");
  catalog_entry *settings = catalog_for(p, seat, content);
  const qa_ui_setting_binding *binding = qa_ui_native_language_settings_binding(&settings->settings);
  if (strcmp(binding->kind, "choice") != 0) qa_raise("Error", 0, 0, "Language setting must be a choice");
  language_binding *record = calloc(1, sizeof *record);
  if (!record) qa_range_error("Out of memory");
  record->presentation = p;
  record->seat = id;
  record->actor = seat->binding.actor;
  record->content = xdup(content);
  record->base = *binding;
  record->failed = failed; record->failed_ctx = failed_ctx;
  push_pointer((void ***)&p->bindings, &p->binding_count, &p->binding_cap, record);
  *out = *binding;
  out->ctx = record;
  out->enabled = binding_enabled;
  out->choices = binding_choices;
  out->read_str = binding_read;
  out->write_str = binding_write;
  return true;
}
/* TS loadSkyImages(providers, content, name): textures.load(env/<name><suffix>, { family: "q2", wrap: "clamp",
 * mipmap: false, usage: "sky" }) ?? textures.missing. */
static void load_sky_images(providers source, qa_content_id content, const char *name, qa_renderer_image out[6]) {
  qa_app_provider_scene_assets provider = source.provider(source.ctx, content);
  qa_scene_texture_load_options options = {true, false, "clamp", "q2", "sky"};
  for (int i = 0; i < 6; i++) {
    size_t size = strlen(name) + 16;
    char *path = malloc(size);
    if (!path) qa_range_error("Out of memory");
    snprintf(path, size, "env/%s%s", name, qa_sky_face_suffixes[i]);
    const qa_scene_texture *image = qa_scene_texture_loader_load(provider.textures, path, &options);
    free(path);
    out[i] = (image != NULL ? image : provider.textures->missing)->image;
  }
}
/* TS skyImages(content, name): the assets' loads, cached per content and name until an image refresh publishes. */
static void sky_images(qa_app_rerelease_presentation *p, qa_content_id content, const char *name, qa_renderer_image out[6]) {
  for (size_t i = 0; i < p->sky_count; i++)
    if (strcmp(p->skies[i].content, content) == 0 && strcmp(p->skies[i].name, name) == 0) {
      memcpy(out, p->skies[i].images, sizeof p->skies[i].images);
      return;
    }
  qa_renderer_image images[6];
  load_sky_images((providers){p->assets, assets_provider}, content, name, images);
  if (p->sky_count == p->sky_cap) { p->sky_cap = p->sky_cap ? p->sky_cap * 2 : 8; p->skies = realloc(p->skies, p->sky_cap * sizeof *p->skies); if (!p->skies) qa_range_error("Out of memory"); }
  struct sky_cache *cache = &p->skies[p->sky_count++];
  cache->content = xdup(content);
  cache->name = xdup(name);
  memcpy(cache->images, images, sizeof images);
  memcpy(out, images, sizeof images);
}
static void set_name(name_entry **arr, size_t *count, size_t *cap, double slot, const char *name) {
  for (size_t i = 0; i < *count; i++) if ((*arr)[i].slot == slot) { set_str(&(*arr)[i].name, name); return; }
  if (*count == *cap) { *cap = *cap ? *cap * 2 : 8; *arr = realloc(*arr, *cap * sizeof **arr); if (!*arr) qa_range_error("Out of memory"); }
  (*arr)[*count].slot = slot;
  (*arr)[*count].name = xdup(name);
  (*count)++;
}
static void delete_name(name_entry *arr, size_t *count, double slot) {
  for (size_t i = 0; i < *count; i++) if (arr[i].slot == slot) {
    free(arr[i].name);
    memmove(&arr[i], &arr[i + 1], (*count - i - 1) * sizeof *arr);
    (*count)--;
    return;
  }
}
/* TS prints.admit({ kind: "q2-player", content, seconds, sequence,
 * recipient?, sourceEntity?, event: { kind: "print", target, level, text } }). */
static void admit_print(qa_app_rerelease_presentation *p, const qa_app_sim_presentation_event *source,
                        qa_actor_id target, const char *level, const char *text) {
  if (p->print_count == QA_APP_RERELEASE_RING) qa_range_error("Event ring capacity exhausted (%zu undrained records); drain before admitting more", (size_t)QA_APP_RERELEASE_RING);
  qa_q2_player_event print; memset(&print, 0, sizeof print);
  print.kind = "print"; print.has_target = true; print.target = target; print.level = level; print.text = text;
  qa_app_sim_presentation_event record; memset(&record, 0, sizeof record);
  record.kind = "q2-player"; record.q2_player = &print;
  record.content = source->content; record.seconds = source->seconds; record.sequence = source->sequence;
  record.has_recipient = source->has_recipient; record.recipient = source->recipient;
  record.has_source_entity = source->has_source_entity; record.source_entity = source->source_entity;
  if (!qa_app_sim_presentation_event_copy(&p->prints[(p->print_head + p->print_count) % QA_APP_RERELEASE_RING], &record))
    qa_range_error("Out of memory");
  p->print_count++;
}
void qa_app_rerelease_prepare(qa_app_rerelease_presentation *p) {
  while (p->pending_count > 0) {
    qa_app_sim_presentation_event owned = p->pending[p->pending_head];
    memset(&p->pending[p->pending_head], 0, sizeof owned);
    p->pending_head = (p->pending_head + 1) % QA_APP_RERELEASE_RING;
    p->pending_count--;
    const qa_app_sim_presentation_event *source = &owned;
    if (strcmp(source->kind, "q2-player") == 0) {
      const qa_q2_player_event *player = source->q2_player;
      if (strcmp(player->kind, "userinfo") == 0) {
        if (!source->has_recipient) {
          set_name(&p->names, &p->name_count, &p->name_cap, player->slot, player->name);
          for (size_t i = 0; i < p->seat_count; i++) delete_name(p->seats[i].names, &p->seats[i].name_count, player->slot);
        } else {
          for (size_t i = 0; i < p->seat_count; i++)
            if (qa_same_actor(source->recipient, p->seats[i].binding.actor))
              set_name(&p->seats[i].names, &p->seats[i].name_count, &p->seats[i].name_cap, player->slot, player->name);
        }
      }
      qa_app_sim_presentation_event_release(&owned);
      continue;
    }
    const qa_q2_rerelease_event *event = source->q2_rerelease;
    if (strcmp(event->kind, "sky") == 0) {
      qa_renderer_image images[6];
      sky_images(p, source->content, event->sky_name, images);
      qa_q2_sky_view sky = {NULL, 6, event->rotation, event->rotation != 0 && event->auto_rotate, event->axis};
      if (!source->has_recipient) {
        p->has_sky = true;
        set_str(&p->sky_content, source->content);
        set_str(&p->sky_name, event->sky_name);
        memcpy(p->sky_images, images, sizeof images);
        p->sky_view = sky;
        p->sky_view.images = p->sky_images;
        for (size_t i = 0; i < p->seat_count; i++) p->seats[i].has_sky = false;
      } else {
        for (size_t i = 0; i < p->seat_count; i++) {
          seat_state *seat = &p->seats[i];
          if (!qa_same_actor(source->recipient, seat->binding.actor)) continue;
          seat->has_sky = true;
          set_str(&seat->sky_content, source->content);
          set_str(&seat->sky_name, event->sky_name);
          memcpy(seat->sky_images, images, sizeof images);
          seat->sky_view = sky;
          seat->sky_view.images = seat->sky_images;
        }
      }
      qa_app_sim_presentation_event_release(&owned);
      continue;
    }
    bool story = strcmp(event->kind, "story") == 0;
    if (!story && strcmp(event->kind, "localized-print") != 0) { qa_app_sim_presentation_event_release(&owned); continue; }
    for (size_t i = 0; i < p->seat_count; i++) {
      seat_state *seat = &p->seats[i];
      if (source->has_recipient && !qa_same_actor(source->recipient, seat->binding.actor)) continue;
      if (!story && event->has_actor && !qa_same_actor(event->actor, seat->binding.actor)) continue;
      char *text = qa_app_rerelease_localize_message(p, seat->binding.seat, source->content, event->text,
                                                     story ? NULL : event->args, story ? 0 : event->arg_count);
      if (story) {
        set_str(&seat->story, text);
        seat->has_story_source = true;
        set_str(&seat->story_content, source->content);
        set_str(&seat->story_text, event->text);
      } else admit_print(p, source, seat->binding.actor, event->level, text);
      free(text);
    }
    qa_app_sim_presentation_event_release(&owned);
  }
  for (size_t i = 0; i < p->seat_count; i++) {
    seat_state *seat = &p->seats[i];
    if (!seat->has_story_source) continue;
    char *text = qa_app_rerelease_localize_message(p, seat->binding.seat, seat->story_content, seat->story_text, NULL, 0);
    set_str(&seat->story, text);
    free(text);
  }
}
const qa_app_sim_presentation_event *qa_app_rerelease_drain_prints(qa_app_rerelease_presentation *p, size_t *out_count) {
  for (size_t i = 0; i < p->drained_count; i++) qa_app_sim_presentation_event_release(&p->drained[i]);
  p->drained_count = 0;
  while (p->print_count > 0) {
    if (p->drained_count == p->drained_cap) { p->drained_cap = p->drained_cap ? p->drained_cap * 2 : 8; p->drained = realloc(p->drained, p->drained_cap * sizeof *p->drained); if (!p->drained) qa_range_error("Out of memory"); }
    p->drained[p->drained_count++] = p->prints[p->print_head];
    memset(&p->prints[p->print_head], 0, sizeof *p->prints);
    p->print_head = (p->print_head + 1) % QA_APP_RERELEASE_RING;
    p->print_count--;
  }
  *out_count = p->drained_count;
  return p->drained;
}
bool qa_app_rerelease_story_active(qa_app_rerelease_presentation *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->seat_count; i++)
    if (qa_same_actor(p->seats[i].binding.actor, actor) && p->seats[i].story[0] != '\0') return true;
  return false;
}
qa_app_rerelease_view qa_app_rerelease_view_of(qa_app_rerelease_presentation *p, qa_actor_id actor, double seconds) {
  qa_app_rerelease_view view; memset(&view, 0, sizeof view);
  seat_state *seat = NULL;
  for (size_t i = 0; i < p->seat_count; i++) if (qa_same_actor(p->seats[i].binding.actor, actor)) { seat = &p->seats[i]; }
  if (seat != NULL && seat->fog_received) { view.has_fog = true; view.fog.fog = qa_app_rerelease_fog_current(&seat->fog, seconds); }
  view.sky = (seat != NULL && seat->has_sky) ? &seat->sky_view : p->has_sky ? &p->sky_view : NULL;
  return view;
}
void qa_app_rerelease_draw_story(qa_app_rerelease_presentation *p, qa_actor_id actor, qa_app_rerelease_draw draw, double scale) {
  seat_state *seat = NULL;
  for (size_t i = 0; i < p->seat_count; i++) if (qa_same_actor(p->seats[i].binding.actor, actor)) seat = &p->seats[i];
  if (seat == NULL || seat->story[0] == '\0') return;
  if (!qa_same_seat(draw.text_seat, seat->binding.seat)) qa_raise("Error", 0, 0, "Story font belongs to another seat");
  double width = 0, height = 0;
  draw.layout(draw.ctx, seat->story, scale, &width, &height);
  double max_width = width > 1 ? width : 1;
  draw.draw(draw.ctx, seat->story, scale, max_width, (draw.width - width) / 2, (draw.height - height) / 2);
}
