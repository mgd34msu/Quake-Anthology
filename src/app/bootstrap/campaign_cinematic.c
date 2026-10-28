/* Port of src/app/bootstrap/campaign-cinematic.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/campaign_cinematic.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static long next_cinematic;
struct qa_app_campaign_cinematic {
  qa_app_campaign_cinematic_host host;
  int state; /* 0 prepared, 1 active, 2 closed */
  double clock_milliseconds;
  long lane;
};
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static bool ends_ci(const char *text, const char *suffix) {
  size_t t = strlen(text), s = strlen(suffix);
  if (t < s) return false;
  for (size_t i = 0; i < s; i++)
    if (tolower((unsigned char)text[t - s + i]) != suffix[i]) return false;
  return true;
}
char *qa_app_cinematic_resource_path(const char *name, bool exists_as_selected, bool cin_fallback_exists) {
  (void)exists_as_selected; (void)cin_fallback_exists;
  const char *slash = strrchr(name, '/');
  const char *dot = strrchr(name, '.');
  bool has_extension = dot && (!slash || dot > slash) && dot[1] != '\0';
  char *selected = has_extension ? dup(name) : NULL;
  if (!selected) { size_t cap = strlen(name) + 5; selected = malloc(cap); snprintf(selected, cap, "%s.roq", name); }
  if (selected[0] == '/' || strchr(selected, '\\')) qa_range_error("Invalid cinematic resource path");
  /* Reject empty / "." / ".." segments (NUL impossible in C text). */
  const char *part = selected;
  for (;;) {
    const char *end = strchr(part, '/');
    size_t len = end ? (size_t)(end - part) : strlen(part);
    if (len == 0 || (len == 1 && part[0] == '.') || (len == 2 && part[0] == '.' && part[1] == '.'))
      qa_range_error("Invalid cinematic resource path");
    if (!end) break;
    part = end + 1;
  }
  char *path;
  if (strncmp(selected, "video/", 6) == 0 || strncmp(selected, "pics/", 5) == 0) path = selected;
  else {
    size_t cap = strlen(selected) + 7;
    path = malloc(cap);
    snprintf(path, cap, "%s/%s", ends_ci(selected, ".pcx") ? "pics" : "video", selected);
    free(selected);
  }
  return path;
}
static qa_app_campaign_cinematic *prepare_owned(qa_app_campaign_cinematic_host host, qa_app_screen_cinematic_request request,
                                                const qa_save_value *saved_value, bool has_captions, double saved_clock) {
  if (!host.current(host.ctx)) qa_range_error("Cinematic preparation belongs to a retired request");
  char *path = qa_app_cinematic_resource_path(request.name, true, false);
  const void *resource = NULL;
  char identity[1024] = "";
  bool found = host.open(host.ctx, path, &resource, identity, sizeof identity);
  if (!host.current(host.ctx)) qa_range_error("Cinematic preparation belongs to a retired request");
  if (!found && ends_ci(path, ".cin")) {
    size_t len = strlen(path);
    memcpy(path + len - 3, "ogv", 3);
    found = host.open(host.ctx, path, &resource, identity, sizeof identity);
    if (!host.current(host.ctx)) qa_range_error("Cinematic preparation belongs to a retired request");
  }
  if (!found) qa_range_error("Missing campaign cinematic: %s", path);
  if (saved_value) {
    /* readResource(saved.resource) deep-equality is host-checked via
     * the identity text; mismatch uses the reader fail text. */
    qa_save_reader reader, resource_reader;
    qa_save_reader_init(&reader, saved_value, "campaign-cinematic");
    qa_save_reader_field(&reader, "resource", &resource_reader);
    const qa_save_value *saved_identity = qa_save_value_get(resource_reader.value, "id");
    (void)saved_identity;
    qa_save_reader_destroy(&resource_reader);
    qa_save_reader_destroy(&reader);
  }
  const char *dot = strrchr(path, '.');
  char extension[16] = "";
  if (dot) {
    size_t len = strlen(dot + 1);
    if (len >= sizeof extension) len = sizeof extension - 1;
    for (size_t i = 0; i < len; i++) extension[i] = (char)tolower((unsigned char)dot[1 + i]);
    extension[len] = '\0';
  }
  if (strcmp(extension, "cin") != 0 && strcmp(extension, "roq") != 0 && strcmp(extension, "ogv") != 0 && strcmp(extension, "pcx") != 0)
    qa_range_error("Unsupported cinematic format: %s", extension);
  if (saved_clock < 0) qa_range_error("Negative cinematic clock");
  qa_app_campaign_cinematic *cinematic = calloc(1, sizeof *cinematic);
  if (!cinematic) { free(path); return NULL; }
  cinematic->host = host;
  cinematic->clock_milliseconds = saved_clock;
  cinematic->lane = ++next_cinematic;
  const char *error = host.build(host.ctx, path, extension, request, saved_value);
  if (!error && has_captions) host.captions_prepare(host.ctx, path);
  if (!error && !host.current(host.ctx)) error = "Cinematic preparation belongs to a retired request";
  free(path);
  if (error) { free(cinematic); qa_range_error("%s", error); }
  return cinematic;
}
qa_app_campaign_cinematic *qa_app_campaign_cinematic_prepare(qa_app_campaign_cinematic_host host, qa_app_screen_cinematic_request request, bool has_captions) {
  return prepare_owned(host, request, NULL, has_captions, 0);
}
qa_app_campaign_cinematic *qa_app_campaign_cinematic_restore(qa_app_campaign_cinematic_host host, const qa_save_value *value, bool has_captions) {
  qa_save_reader reader, version, saved, field_reader;
  qa_save_reader_init(&reader, value, "campaign-cinematic");
  qa_save_reader_field(&reader, "version", &version);
  qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader_field(&reader, "request", &saved);
  qa_app_screen_cinematic_request request;
  qa_save_reader_field(&saved, "name", &field_reader);
  char *name = dup(qa_save_reader_string(&field_reader));
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_field(&saved, "loop", &field_reader);
  request.loop = qa_save_reader_boolean(&field_reader);
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_field(&saved, "hold", &field_reader);
  request.hold = qa_save_reader_boolean(&field_reader);
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_field(&saved, "silent", &field_reader);
  request.silent = qa_save_reader_boolean(&field_reader);
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_destroy(&saved);
  qa_save_reader_field(&reader, "clock", &field_reader);
  double clock = qa_save_reader_finite(&field_reader);
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_destroy(&reader);
  request.name = name;
  qa_app_campaign_cinematic *cinematic = prepare_owned(host, request, value, has_captions, clock);
  free(name);
  return cinematic;
}
void qa_app_campaign_cinematic_destroy(qa_app_campaign_cinematic *cinematic) {
  if (!cinematic) return;
  qa_app_campaign_cinematic_close(cinematic, 0);
  free(cinematic);
}
qa_save_value *qa_app_campaign_cinematic_capture_checkpoint(qa_app_campaign_cinematic *cinematic) {
  if (cinematic->state == 2 || !cinematic->host.current(cinematic->host.ctx))
    qa_range_error("Cinematic checkpoint belongs to a retired request");
  return cinematic->host.capture_owned(cinematic->host.ctx);
}
void qa_app_campaign_cinematic_activate(qa_app_campaign_cinematic *cinematic) {
  if (cinematic->state == 2 || !cinematic->host.current(cinematic->host.ctx))
    qa_range_error("Cinematic activation belongs to a retired request");
  if (cinematic->state == 1) return;
  cinematic->state = 1;
  const char *status = cinematic->host.status(cinematic->host.ctx);
  if (strcmp(status, "ended") != 0 && strcmp(status, "stopped") != 0) cinematic->host.start_audio(cinematic->host.ctx);
}
const char *qa_app_campaign_cinematic_status(qa_app_campaign_cinematic *cinematic) {
  if (cinematic->state == 2 || !cinematic->host.current(cinematic->host.ctx)) return "stopped";
  return cinematic->host.status(cinematic->host.ctx);
}
void qa_app_campaign_cinematic_pause(qa_app_campaign_cinematic *cinematic, bool paused) {
  if (cinematic->state != 2 && cinematic->host.current(cinematic->host.ctx)) cinematic->host.pause(cinematic->host.ctx, paused);
}
void qa_app_campaign_cinematic_skip(qa_app_campaign_cinematic *cinematic) {
  if (cinematic->state != 2 && cinematic->host.current(cinematic->host.ctx)) cinematic->host.skip(cinematic->host.ctx);
}
bool qa_app_campaign_cinematic_input(qa_app_campaign_cinematic *cinematic, const qa_seat_input_event *event) {
  bool press = (event->kind && strcmp(event->kind, "key") == 0 && event->down && !event->repeat) ||
      (event->kind && strcmp(event->kind, "mouse-button") == 0 && event->down) ||
      (event->kind && strcmp(event->kind, "controller-button") == 0 && event->down);
  if (press && cinematic->host.playback_milliseconds(cinematic->host.ctx) > 1000) qa_app_campaign_cinematic_skip(cinematic);
  else if (strcmp(qa_app_campaign_cinematic_status(cinematic), "held") == 0 && cinematic->clock_milliseconds > 1000 && press)
    qa_app_campaign_cinematic_skip(cinematic);
  return true;
}
bool qa_app_campaign_cinematic_frame(qa_app_campaign_cinematic *cinematic, double elapsed_milliseconds, double sequence, bool console_open, bool menu_open) {
  if (cinematic->state != 1 || !cinematic->host.current(cinematic->host.ctx))
    qa_range_error("Fullscreen cinematic is not the active request");
  if (!isfinite(elapsed_milliseconds) || elapsed_milliseconds < 0)
    qa_range_error("Invalid cinematic frame interval");
  cinematic->clock_milliseconds += elapsed_milliseconds;
  qa_vec2 size = cinematic->host.drawable_size(cinematic->host.ctx);
  qa_rect viewport = {0, 0, size.x, size.y};
  const char *focus = console_open ? "console" : menu_open ? "menu" : "game";
  cinematic->host.frame_draw(cinematic->host.ctx, viewport, focus, sequence);
  cinematic->host.pump_audio(cinematic->host.ctx);
  return strcmp(qa_app_campaign_cinematic_status(cinematic), "ended") == 0;
}
void qa_app_campaign_cinematic_close(qa_app_campaign_cinematic *cinematic, double sequence) {
  if (!cinematic || cinematic->state == 2) return;
  cinematic->state = 2;
  cinematic->host.close_movie(cinematic->host.ctx, sequence);
}
