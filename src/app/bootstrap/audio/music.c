/* Port of src/app/bootstrap/audio/music.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/audio/music.h"
#include "qa/network/common/js_number.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/playlist.h"
#include "qa/foundation/support.h"
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static void *xmalloc(size_t n) { void *p = malloc(n == 0 ? 1 : n); if (p == NULL) qa_range_error("Out of memory"); return p; }
static char *xstrdup(const char *s) { char *p = strdup(s != NULL ? s : ""); if (p == NULL) qa_range_error("Out of memory"); return p; }
static const char *world_get(const qa_app_world_key_value *world, size_t count, bool has, const char *key) {
  if (!has) return NULL;
  for (size_t i = 0; i < count; i++) if (is(world[i].key, key)) return world[i].value;
  return NULL;
}
const char *qa_app_world_music_track(const qa_app_world_key_value *world, size_t world_count,
                                     bool has_world, qa_game_family family, const char *edition) {
  if (family == QA_GAME_FAMILY_Q3) {
    const char *m = world_get(world, world_count, has_world, "music");
    return m != NULL ? m : "";
  }
  const char *music = family == QA_GAME_FAMILY_Q2 && is(edition, "rerelease")
    ? world_get(world, world_count, has_world, "music") : NULL;
  if (music != NULL && music[0] != '\0') return music;
  const char *sounds = world_get(world, world_count, has_world, "sounds");
  return sounds != NULL ? sounds : "";
}
static bool own_tree_installed(const qa_content_catalog *catalog, const char *expectation_id, const char **out) {
  if (expectation_id == NULL) return false;
  for (size_t i = 0; i < catalog->product_count; i++) {
    const qa_catalog_discovered_product *p = &catalog->products[i];
    if (is(p->expectation.id, expectation_id) && p->availability.kind == QA_CATALOG_INSTALLED
        && (p->loose_root != NULL || p->user_content != NULL)) {
      *out = p->id;
      return true;
    }
  }
  return false;
}
bool qa_app_q1_music_fallback(qa_content_id content, const qa_content_catalog *catalog, const char **out) {
  static const char *const pairs[3][2] = {
    {"q1-classic-id1", "q1-rerelease-id1"},
    {"q1-classic-hipnotic", "q1-rerelease-hipnotic"},
    {"q1-classic-rogue", "q1-rerelease-rogue"}};
  const qa_catalog_discovered_product *selected = qa_content_catalog_product(catalog, content);
  const char *alternate = NULL;
  for (size_t i = 0; i < 3; i++)
    for (size_t j = 0; j < 2; j++)
      if (is(pairs[i][j], selected->expectation.id)) alternate = pairs[i][1 - j];
  return own_tree_installed(catalog, alternate, out);
}
bool qa_app_music_fallback(qa_content_id content, const qa_content_catalog *catalog, const char **out) {
  if (qa_app_q1_music_fallback(content, catalog, out)) return true;
  const qa_catalog_discovered_product *selected = qa_content_catalog_product(catalog, content);
  if (selected->expectation.family != QA_GAME_FAMILY_Q2 || !is(selected->expectation.edition, "classic"))
    return false;
  return own_tree_installed(catalog, "q2-rerelease-baseq2", out);
}
typedef struct current_track {
  qa_app_music_source source;
  char *content, *edition, *campaign;
  qa_sound_bank *bank;
  qa_app_open_music_track fallback;
  qa_music_player player;
  qa_cd_music cd;
  bool opener_fallback; /* opener.open currently the fallback */
  char *track;
  bool looping;
  char *authored_cue;
} current_track;
typedef struct automatic_state {
  char *cue;
  char **tracks; size_t track_count;
  char **bag; size_t bag_count;
  bool shuffle;
  int64_t completed;
} automatic_state;
struct qa_app_application_music {
  qa_unified_audio *engine;
  qa_app_music_print_fn print; void *print_ctx;
  qa_music_volume_mode volume_mode;
  qa_music_controls *controls;
  qa_music_controls owned_controls;
  double (*random)(void *ctx); void *random_ctx;
  current_track *current;
  automatic_state *automatic;
  int64_t request;
  double gain;
};
static double default_random(void *ctx) { (void)ctx; return (double)rand() / ((double)RAND_MAX + 1.0); }
static void print_f(qa_app_application_music *m, qa_app_music_print_fn print, void *pctx, const char *fmt, ...) {
  char buf[1024];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  (print != NULL ? print : m->print)(print != NULL ? pctx : m->print_ctx, buf);
}
/* JS String(number) for the gain/track values printed here. */
static void number_text(double value, char *buf, size_t size) {
  if (value == trunc(value) && fabs(value) < 1e15) snprintf(buf, size, "%lld", (long long)value);
  else snprintf(buf, size, "%s", QA_JS_NUMBER(value));
}
static qa_pcm_stream *bank_open(current_track *c, const char *path) {
  qa_pcm_stream *stream = NULL;
  return qa_sound_bank_open_music(c->bank, path, c->source.content, &stream) ? stream : NULL;
}
static qa_pcm_stream *opener_open(const char *path, void *ctx) {
  current_track *c = ctx;
  if (c->opener_fallback && c->fallback.open != NULL) return c->fallback.open(c->fallback.ctx, path);
  return bank_open(c, path);
}
qa_app_application_music *qa_app_application_music_create(
    qa_unified_audio *engine, qa_app_music_print_fn print, void *print_ctx,
    qa_music_volume_mode volume_mode, qa_music_controls *controls,
    double (*random)(void *ctx), void *random_ctx) {
  qa_app_application_music *m = calloc(1, sizeof *m);
  if (m == NULL) qa_range_error("Out of memory");
  m->engine = engine; m->print = print; m->print_ctx = print_ctx;
  m->volume_mode = volume_mode;
  if (controls != NULL) m->controls = controls;
  else { qa_music_controls_init(&m->owned_controls); m->controls = &m->owned_controls; }
  m->random = random != NULL ? random : default_random;
  m->random_ctx = random_ctx;
  m->gain = 0.25;
  return m;
}
static void free_automatic(qa_app_application_music *m) {
  automatic_state *a = m->automatic;
  if (a == NULL) return;
  free(a->cue);
  for (size_t i = 0; i < a->track_count; i++) free(a->tracks[i]);
  free(a->tracks);
  for (size_t i = 0; i < a->bag_count; i++) free(a->bag[i]);
  free(a->bag);
  free(a);
  m->automatic = NULL;
}
static void free_current(qa_app_application_music *m) {
  current_track *c = m->current;
  if (c == NULL) return;
  qa_cd_music_close(&c->cd);
  qa_music_player_destroy(&c->player);
  free(c->content); free(c->edition); free(c->campaign);
  free(c->track); free(c->authored_cue);
  free(c);
  m->current = NULL;
}
void qa_app_application_music_destroy(qa_app_application_music *music) {
  if (music == NULL) return;
  free_automatic(music);
  free_current(music);
  free(music);
}
qa_music_controls *qa_app_application_music_controls(qa_app_application_music *music) { return music->controls; }
double qa_app_application_music_volume(const qa_app_application_music *music) { return music->gain; }
void qa_app_application_music_set_volume(qa_app_application_music *music, double value) {
  if (!isfinite(value) || value < 0) qa_range_error("Invalid music volume");
  music->gain = value;
  if (music->current != NULL) qa_music_player_set_volume(&music->current->player, value);
}
static void clear_playback(qa_app_application_music *m) {
  m->request++;
  if (m->current != NULL) qa_cd_music_stop(&m->current->cd);
  qa_unified_audio_stop_music(m->engine, "world");
}
void qa_app_application_music_stop_playback(qa_app_application_music *music, const char *reason) {
  free_automatic(music);
  if (is(reason, "source") && music->current != NULL) {
    free(music->current->authored_cue);
    music->current->authored_cue = xstrdup("");
  }
  clear_playback(music);
}
void qa_app_application_music_invalidate_pending(qa_app_application_music *music) {
  music->request++;
  if (music->current != NULL) qa_cd_music_invalidate_pending(&music->current->cd);
}
void qa_app_application_music_stop(qa_app_application_music *music) {
  qa_app_application_music_stop_playback(music, "source");
  free_current(music);
}
/* TS player.onFault: a decoder failure stops the track and is reported. */
static void music_fault(void *ctx, const qa_caught_error *error) {
  qa_app_application_music *music = ctx;
  print_f(music, NULL, NULL, "WARNING: music playback stopped: %s\n", error->message);
}
void qa_app_application_music_select(qa_app_application_music *music, qa_app_music_source source,
                                     qa_sound_bank *bank, qa_app_open_music_track fallback) {
  if (music->current != NULL && is(music->current->source.content, source.content)
      && music->current->bank == bank) {
    current_track *c = music->current;
    free(c->content); free(c->edition); free(c->campaign);
    c->content = xstrdup(source.content); c->edition = xstrdup(source.edition); c->campaign = xstrdup(source.campaign);
    c->source = source;
    c->source.content = c->content; c->source.edition = c->edition; c->source.campaign = c->campaign;
    c->fallback = fallback;
    return;
  }
  qa_app_application_music_stop(music);
  current_track *c = calloc(1, sizeof *c);
  if (c == NULL) qa_range_error("Out of memory");
  c->content = xstrdup(source.content); c->edition = xstrdup(source.edition); c->campaign = xstrdup(source.campaign);
  c->source = source;
  c->source.content = c->content; c->source.edition = c->edition; c->source.campaign = c->campaign;
  c->bank = bank;
  c->fallback = fallback;
  c->track = xstrdup("");
  c->authored_cue = xstrdup("");
  qa_sound_family family = source.family == QA_GAME_FAMILY_Q1 ? QA_SOUND_FAMILY_Q1
    : source.family == QA_GAME_FAMILY_Q2 ? QA_SOUND_FAMILY_Q2 : QA_SOUND_FAMILY_Q3;
  qa_music_player_init(&c->player, qa_unified_audio_sample_rate(music->engine), family,
                       music->volume_mode, music->controls);
  qa_music_player_set_volume(&c->player, music->gain);
  c->player.on_fault = music_fault;
  c->player.on_fault_ctx = music;
  qa_cd_music_init(&c->cd, &c->player, opener_open, c);
  music->current = c;
}
static bool always_true(void *ctx) { (void)ctx; return true; }
static bool all_digits(const char *v) {
  if (*v == '\0') return false;
  for (; *v != '\0'; v++) if (!isdigit((unsigned char)*v)) return false;
  return true;
}
static bool ends_with_ci(const char *v, const char *suffix) {
  size_t a = strlen(v), b = strlen(suffix);
  if (a < b) return false;
  for (size_t i = 0; i < b; i++)
    if (tolower((unsigned char)v[a - b + i]) != tolower((unsigned char)suffix[i])) return false;
  return true;
}
static char *trimmed(const char *v) {
  while (isspace((unsigned char)*v)) v++;
  size_t len = strlen(v);
  while (len > 0 && isspace((unsigned char)v[len - 1])) len--;
  char *out = xmalloc(len + 1);
  memcpy(out, v, len); out[len] = '\0';
  return out;
}
static void close_stream_cleanup(void *stream) { qa_pcm_stream_close(stream); }
static void start_track(qa_app_application_music *m, const char *selected, bool looping,
                        bool numbered, bool (*active)(void *), void *active_ctx);
static qa_pcm_stream *open_named(qa_app_application_music *m, current_track *c, const char *name,
                                 int64_t request, bool (*active)(void *), void *active_ctx) {
  char *normalized = xstrdup(name);
  for (char *p = normalized; *p != '\0'; p++) if (*p == '\\') *p = '/';
  char *path;
  if (strncmp(normalized, "music/", 6) == 0) path = normalized;
  else {
    path = xmalloc(strlen(normalized) + 7);
    snprintf(path, strlen(normalized) + 7, "music/%s", normalized);
    free(normalized);
  }
  bool family_q3 = c->source.family == QA_GAME_FAMILY_Q3;
  char cand0[1024], cand1[1024];
  const char *candidates[2];
  size_t candidate_count;
  if (ends_with_ci(path, ".wav") || ends_with_ci(path, ".ogg")) {
    snprintf(cand0, sizeof cand0, "%s", path);
    candidates[0] = cand0; candidate_count = 1;
  } else if (family_q3) {
    snprintf(cand0, sizeof cand0, "%s.wav", path);
    snprintf(cand1, sizeof cand1, "%s.ogg", path);
    candidates[0] = cand0; candidates[1] = cand1; candidate_count = 2;
  } else {
    snprintf(cand0, sizeof cand0, "%s.ogg", path);
    snprintf(cand1, sizeof cand1, "%s.wav", path);
    candidates[0] = cand0; candidates[1] = cand1; candidate_count = 2;
  }
  qa_pcm_stream *stream = NULL;
  for (int which = 0; which < 2 && stream == NULL; which++) {
    if (which == 1 && (family_q3 || c->fallback.open == NULL)) continue;
    for (size_t i = 0; i < candidate_count; i++) {
      stream = which == 0 ? opener_open(candidates[i], c)
                          : c->fallback.open(c->fallback.ctx, candidates[i]);
      if (stream != NULL) break;
      if (request != m->request || !m->controls->enabled || !active(active_ctx)) { free(path); return NULL; }
    }
  }
  free(path);
  return stream;
}
/* Split selected on the TS /"[^"]*"|\S+/g tokens, quotes stripped. */
static size_t split_cue(const char *selected, char *tokens[2]) {
  size_t n = 0;
  const char *p = selected;
  while (*p != '\0' && n < 2) {
    while (isspace((unsigned char)*p)) p++;
    if (*p == '\0') break;
    const char *start = p;
    if (*p == '"') { p++; while (*p != '\0' && *p != '"') p++; if (*p == '"') p++; }
    else while (*p != '\0' && !isspace((unsigned char)*p)) p++;
    size_t len = (size_t)(p - start);
    char *token = xmalloc(len + 1);
    memcpy(token, start, len); token[len] = '\0';
    /* TS replace(/^"|"$/g, "") */
    if (token[0] == '"') memmove(token, token + 1, strlen(token));
    size_t tlen = strlen(token);
    if (tlen > 0 && token[tlen - 1] == '"') token[tlen - 1] = '\0';
    tokens[n++] = token;
  }
  return n;
}
static void start_track(qa_app_application_music *m, const char *selected, bool looping,
                        bool numbered, bool (*active)(void *), void *active_ctx) {
  current_track *c = m->current;
  if (c == NULL || !qa_cd_music_enabled(&c->cd) || !active(active_ctx)) return;
  char *bare = xstrdup(selected);
  if (ends_with_ci(bare, ".ogg") || ends_with_ci(bare, ".wav")) bare[strlen(bare) - 4] = '\0';
  bool numeric = all_digits(bare) && (c->source.family != QA_GAME_FAMILY_Q3 || numbered);
  bool has_mapped = numeric;
  int64_t mapped = 0;
  if (numeric) {
    double value = strtod(bare, NULL);
    mapped = c->source.family == QA_GAME_FAMILY_Q2
      ? qa_remap_q2_music_track((int64_t)value,
          is(c->source.edition, "rerelease")
            ? (qa_q2_soundtrack_profile){QA_Q2_SOUNDTRACK_REMASTERED, c->source.campaign}
            : (qa_q2_soundtrack_profile){QA_Q2_SOUNDTRACK_DISC, NULL})
      : (int64_t)value;
  }
  free(bare);
  if (is(c->track, selected) && c->looping == looping && qa_music_player_playing(&c->player)) {
    if (!has_mapped) return;
    int remapped[99];
    qa_cd_music_remapped_tracks(&c->cd, remapped);
    int64_t expect = mapped >= 1 && mapped <= 99 ? remapped[mapped - 1] : mapped;
    int64_t playing = 0;
    if (qa_cd_music_playing_track(&c->cd, &playing) && playing == expect) return;
    if (!qa_cd_music_playing_track(&c->cd, &playing)) return;
  }
  clear_playback(m);
  int64_t request = m->request;
  free(c->track); c->track = xstrdup(selected);
  c->looping = looping;
  c->opener_fallback = false;
  qa_audio_stream_target target = {"world", {QA_AUDIO_AUDIENCE_WORLD, {{NULL}, 0, NULL}}, 1};
  qa_unified_audio_attach_music(m->engine, target, &c->player);
  if (has_mapped) {
    bool played = qa_cd_music_play(&c->cd, mapped, looping);
    if (!played && c->source.family != QA_GAME_FAMILY_Q3 && c->fallback.open != NULL
        && request == m->request) {
      c->opener_fallback = true;
      played = qa_cd_music_play(&c->cd, mapped, looping);
    }
    if (request != m->request || !m->controls->enabled) return;
    if (!played) print_f(m, NULL, NULL, "Music unavailable: %s/%s\n", c->source.content, selected);
    return;
  }
  char *tokens[2] = {NULL, NULL};
  size_t token_count = split_cue(selected, tokens);
  const char *intro_name = token_count > 0 ? tokens[0] : "";
  const char *loop_name = token_count > 1 ? tokens[1] : NULL;
  qa_pcm_stream *intro = open_named(m, c, intro_name, request, active, active_ctx);
  if (intro == NULL) {
    if (request == m->request && active(active_ctx))
      print_f(m, NULL, NULL, "Music unavailable: %s/%s\n", c->source.content, intro_name);
    free(tokens[0]); free(tokens[1]);
    return;
  }
  if (request != m->request || !m->controls->enabled || !active(active_ctx)) {
    qa_pcm_stream_close(intro);
    free(tokens[0]); free(tokens[1]);
    return;
  }
  qa_pcm_stream *loop = NULL;
  /* A raise while opening the loop closes the intro before it propagates (TS try/catch). */
  qa_cleanup intro_guard, token0_guard, token1_guard;
  qa_cleanup_push(&token0_guard, free, tokens[0]);
  qa_cleanup_push(&token1_guard, free, tokens[1]);
  qa_cleanup_push(&intro_guard, close_stream_cleanup, intro);
  if (looping)
    loop = loop_name == NULL || loop_name[0] == '\0' || is(loop_name, intro_name)
      ? intro : open_named(m, c, loop_name, request, active, active_ctx);
  qa_cleanup_pop(&intro_guard);
  qa_cleanup_pop(&token1_guard);
  qa_cleanup_pop(&token0_guard);
  if (request != m->request || !m->controls->enabled || !active(active_ctx)) {
    qa_pcm_stream_close(intro);
    if (loop != intro && loop != NULL) qa_pcm_stream_close(loop);
    free(tokens[0]); free(tokens[1]);
    return;
  }
  c->looping = loop != NULL;
  qa_music_player_start(&c->player, intro, loop == intro ? intro : loop);
  free(tokens[0]); free(tokens[1]);
}
static void next_automatic_track(qa_app_application_music *m) {
  automatic_state *a = m->automatic;
  current_track *c = m->current;
  if (a == NULL || c == NULL) return;
  if (a->bag_count == 0) {
    size_t n = 0;
    char **bag = qa_app_shuffled_tracks((const char *const *)a->tracks, a->track_count,
                                        c->track, m->random, m->random_ctx, &n);
    for (size_t i = 0; i < a->bag_count; i++) free(a->bag[i]);
    free(a->bag);
    a->bag = bag; a->bag_count = n;
  }
  while (a->bag_count > 0 && m->automatic == a && m->controls->enabled) {
    char *track = a->bag[0];
    memmove(&a->bag[0], &a->bag[1], (a->bag_count - 1) * sizeof *a->bag);
    a->bag_count--;
    char *cue = qa_app_music_file_cue(track);
    start_track(m, cue, false, false, always_true, NULL);
    free(cue);
    free(track);
    if (qa_music_player_playing(&c->player)) return;
    a = m->automatic;
    if (a == NULL) return;
  }
}
void qa_app_application_music_music_command(qa_app_application_music *music,
    const char *const *args, size_t argc, qa_app_music_print_fn print, void *print_ctx) {
  bool bad = argc < 1 || argc > 2;
  for (size_t i = 0; i < argc && !bad; i++) {
    char *t = trimmed(args[i]);
    if (t[0] == '\0') bad = true;
    free(t);
  }
  if (bad) { print_f(music, print, print_ctx, "music <intro> [loop]\n"); return; }
  if (music->current == NULL) { print_f(music, print, print_ctx, "No soundtrack source selected.\n"); return; }
  free_automatic(music);
  char *first = qa_app_music_file_cue(args[0]);
  if (argc == 2) {
    char *second = qa_app_music_file_cue(args[1]);
    size_t len = strlen(first) + 1 + strlen(second) + 1;
    char *joined = xmalloc(len);
    snprintf(joined, len, "%s %s", first, second);
    free(first); free(second);
    start_track(music, joined, true, false, always_true, NULL);
    free(joined);
  } else {
    start_track(music, first, false, false, always_true, NULL);
    free(first);
  }
}
static char *lowered(const char *v) {
  char *out = xstrdup(v);
  for (char *p = out; *p != '\0'; p++) *p = (char)tolower((unsigned char)*p);
  return out;
}
void qa_app_application_music_cd_command(qa_app_application_music *music,
    const char *const *args, size_t argc, qa_app_music_print_fn print, void *print_ctx) {
  if (argc == 0) return;
  char *command = lowered(args[0]);
  current_track *c = music->current;
  if (is(command, "close") || is(command, "eject")) {
    print_f(music, print, print_ctx, "cd %s: disc tray operations are unavailable with file-backed music.\n", command);
    free(command); return;
  }
  if (is(command, "info")) {
    if (!music->controls->enabled) print_f(music, print, print_ctx, "CD music is disabled.\n");
    else if (c != NULL && qa_music_player_playing(&c->player)) {
      int64_t mapped = 0;
      bool has_mapped = qa_cd_music_playing_track(&c->cd, &mapped);
      char mapped_text[32], track_text[1100];
      snprintf(mapped_text, sizeof mapped_text, "%lld", (long long)mapped);
      if (has_mapped && !is(mapped_text, c->track))
        snprintf(track_text, sizeof track_text, "%s (mapped to %lld)", c->track, (long long)mapped);
      else snprintf(track_text, sizeof track_text, "%s", c->track);
      print_f(music, print, print_ctx, "%s %s track %s\n",
              c->player.paused ? "Paused" : "Currently", c->looping ? "looping" : "playing", track_text);
    } else print_f(music, print, print_ctx, "Not playing.\n");
    char gain[32]; number_text(music->gain, gain, sizeof gain);
    print_f(music, print, print_ctx, "Volume is %s\n", gain);
    free(command); return;
  }
  if (is(command, "on")) { music->controls->enabled = true; free(command); return; }
  if (is(command, "off")) {
    qa_app_application_music_stop_playback(music, "manual");
    music->controls->enabled = false; free(command); return;
  }
  if (is(command, "stop")) { qa_app_application_music_stop_playback(music, "manual"); free(command); return; }
  if (is(command, "reset")) {
    qa_app_application_music_stop_playback(music, "manual");
    qa_music_controls_reset(music->controls);
    free(command); return;
  }
  if (is(command, "remap")) {
    if (argc == 1) {
      int remapped[99];
      qa_music_controls_remapped_tracks(music->controls, remapped);
      for (size_t i = 0; i < 99; i++)
        if (remapped[i] != (int)i + 1)
          print_f(music, print, print_ctx, "  %zu -> %d\n", i + 1, remapped[i]);
      free(command); return;
    }
    size_t count = argc - 1;
    int64_t *tracks = xmalloc(count * sizeof *tracks);
    bool bad = count > 99;
    for (size_t i = 0; i < count; i++) {
      if (!all_digits(args[i + 1])) { bad = true; tracks[i] = 0; continue; }
      double v = strtod(args[i + 1], NULL);
      tracks[i] = (int64_t)v;
      if (v != trunc(v) || v < 0 || v > 255) bad = true;
    }
    if (bad) {
      print_f(music, print, print_ctx, "cd remap requires at most 99 track numbers from 0 through 255.\n");
      free(tracks); free(command); return;
    }
    qa_music_controls_set_remap(music->controls, tracks, count);
    free(tracks); free(command); return;
  }
  if (c == NULL) {
    if (!is(command, "pause") && !is(command, "resume"))
      print_f(music, print, print_ctx, "No soundtrack source selected.\n");
    free(command); return;
  }
  if (is(command, "play") || is(command, "loop")) {
    const char *argument = argc > 1 ? args[1] : "";
    double track = all_digits(argument) ? strtod(argument, NULL) : NAN;
    if (argc != 2 || !(track == trunc(track)) || track < 1 || track > 255) {
      print_f(music, print, print_ctx, "cd %s <track 1..255>\n", command);
      free(command); return;
    }
    free_automatic(music);
    char text[32]; number_text(track, text, sizeof text);
    start_track(music, text, is(command, "loop"), true, always_true, NULL);
    free(command); return;
  }
  if (is(command, "pause") || is(command, "resume")) {
    if (is(command, "pause")) qa_cd_music_pause(&c->cd); else qa_cd_music_resume(&c->cd);
    free(command); return;
  }
  print_f(music, print, print_ctx, "Unknown cd command: %s.\n", command);
  free(command);
}
void qa_app_application_music_play(qa_app_application_music *music, qa_app_music_source source,
    qa_sound_bank *bank, const char *track, qa_app_open_music_track fallback,
    const qa_app_music_playlist *playlist) {
  qa_app_application_music_select(music, source, bank, fallback);
  char *selected = trimmed(track);
  if (playlist != NULL && music->current != NULL && is(music->current->authored_cue, selected)
      && music->automatic == NULL) { free(selected); return; }
  if (music->current != NULL) {
    free(music->current->authored_cue);
    music->current->authored_cue = xstrdup(selected);
  }
  if (selected[0] == '\0' || is(selected, "0")) {
    free_automatic(music);
    clear_playback(music);
    free(selected); return;
  }
  current_track *c = music->current;
  if (c == NULL) { free(selected); return; }
  bool shuffle = source.family == QA_GAME_FAMILY_Q2 && playlist != NULL
    && playlist->shuffle && playlist->track_count > 0;
  if (music->automatic != NULL && is(music->automatic->cue, selected)
      && music->automatic->shuffle == shuffle && qa_music_player_playing(&c->player)) {
    free(selected); return;
  }
  free_automatic(music);
  automatic_state *a = calloc(1, sizeof *a);
  if (a == NULL) qa_range_error("Out of memory");
  a->cue = xstrdup(selected);
  a->shuffle = shuffle;
  a->completed = qa_music_player_completed_plays(&c->player);
  if (playlist != NULL) {
    a->tracks = xmalloc((playlist->track_count == 0 ? 1 : playlist->track_count) * sizeof *a->tracks);
    for (size_t i = 0; i < playlist->track_count; i++) a->tracks[i] = xstrdup(playlist->tracks[i]);
    a->track_count = playlist->track_count;
  }
  music->automatic = a;
  if (shuffle) next_automatic_track(music);
  else start_track(music, selected, true, false, always_true, NULL);
  free(selected);
}
void qa_app_application_music_play_tracks(qa_app_application_music *music, qa_app_music_source source,
    qa_sound_bank *bank, const char *intro, const char *loop,
    bool (*current)(void *ctx), void *current_ctx) {
  if (!current(current_ctx)) return;
  qa_app_application_music_select(music, source, bank, (qa_app_open_music_track){NULL, NULL});
  free_automatic(music);
  if (music->current != NULL) {
    free(music->current->authored_cue);
    music->current->authored_cue = xstrdup("");
  }
  if (intro[0] == '\0') { clear_playback(music); return; }
  char *intro_cue = qa_app_music_file_cue(intro);
  if (loop[0] == '\0') {
    start_track(music, intro_cue, true, false, current, current_ctx);
    free(intro_cue);
    return;
  }
  char *loop_cue = qa_app_music_file_cue(loop);
  size_t len = strlen(intro_cue) + 1 + strlen(loop_cue) + 1;
  char *joined = xmalloc(len);
  snprintf(joined, len, "%s %s", intro_cue, loop_cue);
  free(intro_cue); free(loop_cue);
  start_track(music, joined, true, false, current, current_ctx);
  free(joined);
}
void qa_app_application_music_update_automatic(qa_app_application_music *music, bool shuffle) {
  automatic_state *a = music->automatic;
  current_track *c = music->current;
  if (a == NULL || c == NULL || !music->controls->enabled || c->player.paused) return;
  bool enabled = c->source.family == QA_GAME_FAMILY_Q2 && shuffle && a->track_count > 0;
  if (enabled != a->shuffle) {
    a->shuffle = enabled;
    a->completed = qa_music_player_completed_plays(&c->player);
    if (enabled) next_automatic_track(music);
    else start_track(music, a->cue, true, false, always_true, NULL);
  } else if (enabled && a->completed != qa_music_player_completed_plays(&c->player)) {
    a->completed = qa_music_player_completed_plays(&c->player);
    next_automatic_track(music);
  }
}
