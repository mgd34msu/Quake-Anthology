/* Port of src/app/bootstrap/rerelease-presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/rerelease_presentation.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/materials/sky.h"
#include "qa/ui/settings/language.h"
typedef struct name_entry { double slot; char *name; } name_entry;
typedef struct seat_state {
  qa_app_rerelease_seat binding;
  char *language;
  qa_app_rerelease_fog fog;
  bool fog_received;
  char *story;
  bool has_story_source; char *story_content; char *story_text;
  qa_actor_id *hidden; size_t hidden_count, hidden_cap;
  name_entry *names; size_t name_count, name_cap;
  bool has_sky; char *sky_content; char *sky_name;
  qa_renderer_image sky_images[6];
  qa_q2_sky_view sky_view;
} seat_state;
struct qa_app_rerelease_presentation {
  qa_app_rerelease_host host;
  qa_cvar_registry *language_settings;
  seat_state *seats; size_t seat_count;
  name_entry *names; size_t name_count, name_cap;
  qa_app_rerelease_event pending[8192]; size_t pending_head, pending_count;
  qa_app_rerelease_print prints_storage[8192]; size_t print_head, print_count;
  qa_app_rerelease_print *drained; size_t drained_count, drained_cap;
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
      char *end = NULL;
      double slot = strtod(digits, &end);
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
static void free_seat(seat_state *s) {
  free(s->language); free(s->story); free(s->story_content); free(s->story_text);
  free(s->hidden);
  for (size_t i = 0; i < s->name_count; i++) free(s->names[i].name);
  free(s->names);
  free(s->sky_content); free(s->sky_name);
}
qa_app_rerelease_presentation *qa_app_rerelease_presentation_create(qa_app_rerelease_host host,
    const qa_app_rerelease_seat *seats, size_t seat_count, qa_cvar_registry *language_settings) {
  qa_app_rerelease_presentation *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->host = host;
  p->language_settings = language_settings;
  p->seats = calloc(seat_count ? seat_count : 1, sizeof *p->seats);
  if (!p->seats) qa_range_error("Out of memory");
  p->seat_count = seat_count;
  for (size_t i = 0; i < seat_count; i++) {
    p->seats[i] = make_seat(seats[i]);
  }
  return p;
}
void qa_app_rerelease_presentation_destroy(qa_app_rerelease_presentation *p) {
  if (!p) return;
  for (size_t i = 0; i < p->seat_count; i++) free_seat(&p->seats[i]);
  free(p->seats);
  for (size_t i = 0; i < p->name_count; i++) free(p->names[i].name);
  free(p->names);
  for (size_t i = 0; i < p->print_count; i++) free((void *)p->prints_storage[(p->print_head + i) % 8192].text);
  for (size_t i = 0; i < p->drained_count; i++) free((void *)p->drained[i].text);
  free(p->drained);
  free(p->sky_content); free(p->sky_name);
  for (size_t i = 0; i < p->sky_count; i++) { free(p->skies[i].content); free(p->skies[i].name); }
  free(p->skies);
  free(p->language_scratch);
  free(p);
}
static void load_sky_images(qa_app_rerelease_presentation *p, qa_content_id content, const char *name, qa_renderer_image out[6]);
qa_app_rerelease_refresh *qa_app_rerelease_prepare_image_refresh(qa_app_rerelease_presentation *p) {
  qa_app_rerelease_refresh *refresh = calloc(1, sizeof *refresh);
  if (!refresh) qa_range_error("Out of memory");
  refresh->presentation = p;
  refresh->has_next = p->has_sky;
  if (p->has_sky) load_sky_images(p, p->sky_content, p->sky_name, refresh->next_images);
  refresh->seat_skies = calloc(p->seat_count ? p->seat_count : 1, sizeof *refresh->seat_skies);
  if (!refresh->seat_skies) qa_range_error("Out of memory");
  refresh->seat_count = p->seat_count;
  for (size_t i = 0; i < p->seat_count; i++) {
    refresh->seat_skies[i].seat = i;
    refresh->seat_skies[i].has_sky = p->seats[i].has_sky;
    if (p->seats[i].has_sky) load_sky_images(p, p->seats[i].sky_content, p->seats[i].sky_name, refresh->seat_skies[i].images);
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
void qa_app_rerelease_publish_seats(qa_app_rerelease_presentation *p, const qa_app_rerelease_seat *seats, size_t seat_count) {
  seat_state *next = calloc(seat_count ? seat_count : 1, sizeof *next);
  if (!next) qa_range_error("Out of memory");
  bool *retained = calloc(p->seat_count ? p->seat_count : 1, sizeof *retained);
  if (!retained) qa_range_error("Out of memory");
  for (size_t i = 0; i < seat_count; i++) {
    long found = -1;
    for (size_t j = 0; j < p->seat_count; j++)
      if (!retained[j] && qa_same_seat(p->seats[j].binding.seat, seats[i].seat) && qa_same_actor(p->seats[j].binding.actor, seats[i].actor)) found = (long)j;
    if (found >= 0) { next[i] = p->seats[found]; retained[found] = true; }
    else {
      next[i] = make_seat(seats[i]);
    }
  }
  for (size_t j = 0; j < p->seat_count; j++) if (!retained[j]) free_seat(&p->seats[j]);
  free(retained);
  free(p->seats);
  p->seats = next;
  p->seat_count = seat_count;
}
static void admit_pending(qa_app_rerelease_presentation *p, qa_app_rerelease_event event) {
  if (p->pending_count == 8192) qa_range_error("Event ring capacity exhausted (%zu undrained records); drain before admitting more", (size_t)8192);
  p->pending[(p->pending_head + p->pending_count) % 8192] = event;
  p->pending_count++;
}
static bool hidden_contains(seat_state *seat, qa_actor_id item) {
  for (size_t i = 0; i < seat->hidden_count; i++) if (qa_same_actor(seat->hidden[i], item)) return true;
  return false;
}
void qa_app_rerelease_receive(qa_app_rerelease_presentation *p, const qa_app_rerelease_event *events, size_t count) {
  for (size_t e = 0; e < count; e++) {
    qa_app_rerelease_event source = events[e];
    if (strcmp(source.kind, "q2-player") == 0 && strcmp(source.event_kind, "userinfo") == 0) admit_pending(p, source);
    if (strcmp(source.kind, "q2-rerelease") != 0) continue;
    if (strcmp(source.event_kind, "item-visibility") == 0) {
      for (size_t i = 0; i < p->seat_count; i++) {
        seat_state *seat = &p->seats[i];
        if (!qa_same_actor(seat->binding.actor, source.actor)) continue;
        if (source.has_recipient && !qa_same_actor(source.recipient, seat->binding.actor)) continue;
        for (size_t j = 0; j < seat->hidden_count; j++) if (qa_same_actor(seat->hidden[j], source.item)) {
          memmove(&seat->hidden[j], &seat->hidden[j + 1], (seat->hidden_count - j - 1) * sizeof(qa_actor_id));
          seat->hidden_count--;
          break;
        }
        if (!source.visible && !hidden_contains(seat, source.item)) {
          if (seat->hidden_count == seat->hidden_cap) { seat->hidden_cap = seat->hidden_cap ? seat->hidden_cap * 2 : 8; seat->hidden = realloc(seat->hidden, seat->hidden_cap * sizeof *seat->hidden); if (!seat->hidden) qa_range_error("Out of memory"); }
          seat->hidden[seat->hidden_count++] = source.item;
        }
      }
    }
    if (strcmp(source.event_kind, "fog") == 0) {
      for (size_t i = 0; i < p->seat_count; i++) {
        seat_state *seat = &p->seats[i];
        if (!qa_same_actor(seat->binding.actor, source.actor)) continue;
        if (source.has_recipient && !qa_same_actor(source.recipient, seat->binding.actor)) continue;
        qa_app_rerelease_fog_receive(&seat->fog, source.fog_value, source.fog_transition_milliseconds, source.seconds);
        seat->fog_received = true;
      }
    } else if (strcmp(source.event_kind, "story") == 0 || strcmp(source.event_kind, "localized-print") == 0 || strcmp(source.event_kind, "sky") == 0) {
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
    p->host.catalogs_clear(p->host.ctx, seat->binding.seat);
  }
}
static const void *catalog_for(qa_app_rerelease_presentation *p, seat_state *seat, qa_content_id content) {
  sync_language(p, seat);
  return p->host.catalog(p->host.ctx, seat->binding.seat, seat->language, content);
}
bool qa_app_rerelease_item_visible(qa_app_rerelease_presentation *p, qa_actor_id actor, qa_actor_id item) {
  for (size_t i = 0; i < p->seat_count; i++)
    if (qa_same_actor(p->seats[i].binding.actor, actor)) return !hidden_contains(&p->seats[i], item);
  return true;
}
char *qa_app_rerelease_source_localize(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content, const char *text, const char *const *args, size_t arg_count) {
  seat_state *seat = find_seat(p, id);
  if (seat == NULL) qa_range_error("Unknown localization seat");
  const void *catalog = catalog_for(p, seat, content);
  char *result = p->host.localized_text(p->host.ctx, catalog, text, args, arg_count);
  if (!result) qa_range_error("Out of memory");
  return result;
}
char *qa_app_rerelease_localize_message(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content, const char *text, const char *const *args, size_t arg_count) {
  seat_state *seat = find_seat(p, id);
  if (seat == NULL) qa_range_error("Unknown localization seat");
  const void *catalog = catalog_for(p, seat, content);
  char *localized = p->host.localized_text(p->host.ctx, catalog, text, args, arg_count);
  if (!localized) qa_range_error("Out of memory");
  /* Map merge: global names overridden by seat names. */
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
static bool binding_read_choice(void *ctx) { (void)ctx; return false; }
bool qa_app_rerelease_language_binding(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content,
                                       void (*failed)(void *ctx, const char *error), void *failed_ctx, qa_ui_setting_binding *out) {
  (void)failed; (void)failed_ctx;
  const char *family = p->host.provider_family(p->host.ctx, content);
  if (strcmp(family, "q1") != 0 && strcmp(family, "q2") != 0) return false;
  seat_state *seat = find_seat(p, id);
  if (seat == NULL) qa_range_error("Unknown localization seat");
  const void *catalog = catalog_for(p, seat, content);
  if (!p->host.language_binding(p->host.ctx, catalog, out)) qa_range_error("Out of memory");
  if (strcmp(out->kind, "choice") != 0) qa_range_error("Language setting must be a choice");
  (void)binding_read_choice;
  return true;
}
static void load_sky_images(qa_app_rerelease_presentation *p, qa_content_id content, const char *name, qa_renderer_image out[6]) {
  for (size_t i = 0; i < p->sky_count; i++)
    if (strcmp(p->skies[i].content, content) == 0 && strcmp(p->skies[i].name, name) == 0) {
      memcpy(out, p->skies[i].images, sizeof p->skies[i].images);
      return;
    }
  qa_renderer_image images[6];
  for (int i = 0; i < 6; i++) {
    char path[1024];
    snprintf(path, sizeof path, "env/%s%s", name, qa_sky_face_suffixes[i]);
    if (!p->host.load_sky_image(p->host.ctx, content, path, &images[i]))
      images[i] = p->host.missing_image(p->host.ctx, content);
  }
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
static void admit_print_record(qa_app_rerelease_presentation *p, qa_app_rerelease_print record) {
  if (p->print_count == 8192) qa_range_error("Event ring capacity exhausted (%zu undrained records); drain before admitting more", (size_t)8192);
  p->prints_storage[(p->print_head + p->print_count) % 8192] = record;
  p->print_count++;
}
void qa_app_rerelease_prepare(qa_app_rerelease_presentation *p) {
  while (p->pending_count > 0) {
    qa_app_rerelease_event source = p->pending[p->pending_head];
    p->pending_head = (p->pending_head + 1) % 8192;
    p->pending_count--;
    if (strcmp(source.kind, "q2-player") == 0) {
      if (strcmp(source.event_kind, "userinfo") == 0) {
        if (!source.has_recipient) {
          set_name(&p->names, &p->name_count, &p->name_cap, source.slot, source.userinfo_name);
          for (size_t i = 0; i < p->seat_count; i++) delete_name(p->seats[i].names, &p->seats[i].name_count, source.slot);
        } else {
          for (size_t i = 0; i < p->seat_count; i++)
            if (qa_same_actor(source.recipient, p->seats[i].binding.actor))
              set_name(&p->seats[i].names, &p->seats[i].name_count, &p->seats[i].name_cap, source.slot, source.userinfo_name);
        }
      }
      continue;
    }
    if (strcmp(source.event_kind, "sky") == 0) {
      qa_renderer_image images[6];
      load_sky_images(p, source.content, source.name, images);
      qa_q2_sky_view sky = {NULL, 6, source.sky_rotation, source.sky_rotation != 0 && source.sky_auto_rotate, source.sky_axis};
      if (!source.has_recipient) {
        p->has_sky = true;
        set_str(&p->sky_content, source.content);
        set_str(&p->sky_name, source.name);
        memcpy(p->sky_images, images, sizeof images);
        p->sky_view = sky;
        p->sky_view.images = p->sky_images;
        for (size_t i = 0; i < p->seat_count; i++) p->seats[i].has_sky = false;
      } else {
        for (size_t i = 0; i < p->seat_count; i++) {
          seat_state *seat = &p->seats[i];
          if (!qa_same_actor(source.recipient, seat->binding.actor)) continue;
          seat->has_sky = true;
          set_str(&seat->sky_content, source.content);
          set_str(&seat->sky_name, source.name);
          memcpy(seat->sky_images, images, sizeof images);
          seat->sky_view = sky;
          seat->sky_view.images = seat->sky_images;
        }
      }
      continue;
    }
    if (strcmp(source.event_kind, "story") != 0 && strcmp(source.event_kind, "localized-print") != 0) continue;
    bool story = strcmp(source.event_kind, "story") == 0;
    for (size_t i = 0; i < p->seat_count; i++) {
      seat_state *seat = &p->seats[i];
      if (source.has_recipient && !qa_same_actor(source.recipient, seat->binding.actor)) continue;
      if (!story && source.has_actor && !qa_same_actor(source.actor, seat->binding.actor)) continue;
      char *text = qa_app_rerelease_localize_message(p, seat->binding.seat, source.content, source.text,
                                                     story ? NULL : source.args, story ? 0 : source.arg_count);
      if (story) {
        set_str(&seat->story, text);
        free(text);
        seat->has_story_source = true;
        set_str(&seat->story_content, source.content);
        set_str(&seat->story_text, source.text);
      } else {
        qa_app_rerelease_print record; memset(&record, 0, sizeof record);
        record.content = source.content;
        record.seconds = source.seconds;
        record.sequence = source.sequence;
        record.has_recipient = source.has_recipient;
        record.recipient = source.recipient;
        record.target = seat->binding.actor;
        record.level = source.level;
        record.text = text;
        admit_print_record(p, record);
      }
    }
  }
  for (size_t i = 0; i < p->seat_count; i++) {
    seat_state *seat = &p->seats[i];
    if (!seat->has_story_source) continue;
    char *text = qa_app_rerelease_localize_message(p, seat->binding.seat, seat->story_content, seat->story_text, NULL, 0);
    set_str(&seat->story, text);
    free(text);
  }
}
const qa_app_rerelease_print *qa_app_rerelease_drain_prints(qa_app_rerelease_presentation *p, size_t *out_count) {
  for (size_t i = 0; i < p->drained_count; i++) free((void *)p->drained[i].text);
  p->drained_count = 0;
  while (p->print_count > 0) {
    if (p->drained_count == p->drained_cap) { p->drained_cap = p->drained_cap ? p->drained_cap * 2 : 8; p->drained = realloc(p->drained, p->drained_cap * sizeof *p->drained); if (!p->drained) qa_range_error("Out of memory"); }
    p->drained[p->drained_count++] = p->prints_storage[p->print_head];
    p->print_head = (p->print_head + 1) % 8192;
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
  if (!qa_same_seat(draw.text_seat, seat->binding.seat)) qa_range_error("Story font belongs to another seat");
  double width = 0, height = 0;
  draw.layout(draw.ctx, seat->story, scale, &width, &height);
  double max_width = width > 1 ? width : 1;
  draw.draw(draw.ctx, seat->story, scale, max_width, (draw.width - width) / 2, (draw.height - height) / 2);
}
