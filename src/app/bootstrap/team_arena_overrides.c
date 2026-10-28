/* Port of src/app/bootstrap/team-arena-overrides.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/team_arena_overrides.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/team_arena_skirmish.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"

static void cvar_set_(qa_cvar_registry *r, const char *name, const char *value, bool force) {
  qa_cvar_snapshot out; memset(&out, 0, sizeof out);
  if (qa_cvar_registry_set(r, name, value, force, &out)) qa_cvar_read_free(&out);
}

static const char *const SCHEMA = "world:team-arena-overrides";
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static bool ci_eq(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
    char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
    if (ca != cb) return false;
    a++; b++;
  }
  return *a == *b;
}
void qa_app_team_arena_overrides_free(qa_app_team_arena_overrides *state) {
  for (size_t i = 0; i < state->seat_count; i++) { free(state->seats[i].baseline); free(state->seats[i].effective); }
  free(state->seats); memset(state, 0, sizeof *state);
}
static void require_seats(const qa_app_team_arena_overrides *state, const qa_app_override_seat *expected, size_t expected_count) {
  bool bad = state->seat_count != expected_count;
  for (size_t i = 0; i < state->seat_count && !bad; i++) {
    for (size_t j = i + 1; j < state->seat_count; j++)
      if (state->seats[i].seat == state->seats[j].seat || state->seats[i].client == state->seats[j].client) bad = true;
    bool found = false;
    for (size_t j = 0; j < expected_count; j++)
      if (expected[j].seat == state->seats[i].seat && expected[j].client == state->seats[i].client) found = true;
    if (!found) bad = true;
  }
  if (bad) qa_range_error("Saved Team Arena override seats do not match local clients");
}
static void require_seats_reg(const qa_app_team_arena_overrides *state, const qa_app_override_registry *expected, size_t count) {
  qa_app_override_seat *plain = calloc(count ? count : 1, sizeof *plain);
  if (!plain) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) plain[i] = (qa_app_override_seat){expected[i].seat, expected[i].client};
  require_seats(state, plain, count);
  free(plain);
}
qa_app_team_arena_overrides qa_app_capture_team_arena_overrides(const qa_app_override_registry *seats, size_t count) {
  qa_app_team_arena_overrides state; memset(&state, 0, sizeof state);
  state.phase = "active";
  state.seats = calloc(count ? count : 1, sizeof *state.seats);
  if (!state.seats) qa_range_error("Out of memory");
  state.seat_count = count;
  for (size_t i = 0; i < count; i++) {
    qa_cvar_read baseline; memset(&baseline, 0, sizeof baseline);
    if (!qa_cvar_registry_find(seats[i].cvars, "ui_drawTimer", &baseline))
      qa_range_error("Team Arena timer baseline is missing");
    state.seats[i].seat = seats[i].seat;
    state.seats[i].client = seats[i].client;
    state.seats[i].baseline = xdup(baseline.value);
    qa_cvar_read_free(&baseline);
    state.seats[i].effective = qa_cvar_registry_variable_string(seats[i].cvars, "cg_drawTimer");
    if (!state.seats[i].effective) qa_range_error("Out of memory");
  }
  require_seats_reg(&state, seats, count);
  return state;
}
bool qa_app_read_team_arena_overrides(const qa_save_image *image, const qa_app_override_seat *seats, size_t count, qa_app_team_arena_overrides *out) {
  const qa_provider_checkpoint *checkpoint = NULL;
  for (size_t i = 0; i < image->provider_count; i++)
    if (strcmp(image->providers[i].schema, SCHEMA) == 0) checkpoint = &image->providers[i];
  if (checkpoint == NULL) return false;
  bool owner = false;
  for (size_t i = 0; i < image->recipe.execution_count; i++) {
    const qa_resolved_execution_module *module = &image->recipe.execution[i];
    if (module->kind == QA_EXECUTION_MODULE_TYPESCRIPT && module->api.kind == QA_GAME_API_Q3 && module->api.as.q3.kind == QA_Q3_API_QAGAME) owner = true;
  }
  if (!owner) qa_range_error("Team Arena override checkpoint has no native Q3 owner");
  qa_save_value *value = qa_decode_checkpoint_value(checkpoint->bytes.bytes, checkpoint->bytes.length);
  if (value == NULL) qa_range_error("Out of memory");
  *out = qa_app_decode_team_arena_overrides(value, seats, count);
  qa_save_value_free(value);
  return true;
}
qa_app_team_arena_overrides qa_app_decode_team_arena_overrides(const qa_save_value *value, const qa_app_override_seat *seats, size_t count) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "Team Arena overrides")) qa_range_error("Out of memory");
  qa_save_reader phase_reader;
  if (!qa_save_reader_field(&reader, "phase", &phase_reader)) qa_range_error("Out of memory");
  const char *phase = qa_save_reader_string(&phase_reader);
  qa_app_team_arena_overrides state; memset(&state, 0, sizeof state);
  if (strcmp(phase, "active") == 0) state.phase = "active";
  else if (strcmp(phase, "released") == 0) state.phase = "released";
  else qa_save_reader_fail(&reader, "invalid override phase");
  qa_save_reader_destroy(&phase_reader);
  qa_save_reader seats_reader;
  if (!qa_save_reader_field(&reader, "seats", &seats_reader)) qa_range_error("Out of memory");
  size_t row_count = qa_save_reader_list_count(&seats_reader);
  state.seats = calloc(row_count ? row_count : 1, sizeof *state.seats);
  if (!state.seats) qa_range_error("Out of memory");
  state.seat_count = row_count;
  for (size_t i = 0; i < row_count; i++) {
    qa_save_reader row;
    if (!qa_save_reader_item(&seats_reader, i, &row)) qa_range_error("Out of memory");
    qa_save_reader f;
    if (!qa_save_reader_field(&row, "seat", &f)) qa_range_error("Out of memory");
    state.seats[i].seat = qa_save_reader_integer_min(&f, 0);
    qa_save_reader_destroy(&f);
    if (!qa_save_reader_field(&row, "client", &f)) qa_range_error("Out of memory");
    state.seats[i].client = qa_save_reader_integer_min(&f, 0);
    qa_save_reader_destroy(&f);
    if (!qa_save_reader_field(&row, "baseline", &f)) qa_range_error("Out of memory");
    state.seats[i].baseline = xdup(qa_save_reader_string(&f));
    qa_save_reader_destroy(&f);
    if (!qa_save_reader_field(&row, "effective", &f)) qa_range_error("Out of memory");
    state.seats[i].effective = xdup(qa_save_reader_string(&f));
    qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&row);
  }
  qa_save_reader_destroy(&seats_reader);
  qa_save_reader_destroy(&reader);
  require_seats(&state, seats, count);
  return state;
}
static const qa_app_override_registry *find_seat(const qa_app_override_registry *seats, size_t count, double seat, double client) {
  for (size_t i = 0; i < count; i++) if (seats[i].seat == seat && seats[i].client == client) return &seats[i];
  return NULL;
}
qa_save_image qa_app_save_team_arena_overrides(qa_save_image image, const qa_app_team_arena_overrides *state,
                                               const qa_app_override_registry *seats, size_t count) {
  if (state == NULL) return image;
  require_seats_reg(state, seats, count);
  qa_save_value *saved = qa_save_value_object();
  if (!saved) qa_range_error("Out of memory");
  qa_save_value *phase_value = qa_save_value_string(state->phase);
  qa_save_value *rows = qa_save_value_array();
  if (!phase_value || !rows) qa_range_error("Out of memory");
  if (!qa_save_value_set(saved, "phase", phase_value)) qa_range_error("Out of memory");
  for (size_t i = 0; i < state->seat_count; i++) {
    const qa_app_override_registry *seat = find_seat(seats, count, state->seats[i].seat, state->seats[i].client);
    if (seat == NULL) qa_range_error("Missing saved override seat");
    char *effective = qa_cvar_registry_variable_string(seat->cvars, "cg_drawTimer");
    if (!effective) qa_range_error("Out of memory");
    qa_save_value *row = qa_save_value_object();
    if (!row) qa_range_error("Out of memory");
    if (!qa_save_value_set(row, "seat", qa_save_value_number(state->seats[i].seat))
        || !qa_save_value_set(row, "client", qa_save_value_number(state->seats[i].client))
        || !qa_save_value_set(row, "baseline", qa_save_value_string(state->seats[i].baseline))
        || !qa_save_value_set(row, "effective", qa_save_value_string(effective)))
      qa_range_error("Out of memory");
    free(effective);
    if (!qa_save_value_push(rows, row)) qa_range_error("Out of memory");
  }
  if (!qa_save_value_set(saved, "seats", rows)) qa_range_error("Out of memory");
  size_t byte_length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(saved, &byte_length);
  qa_save_value_free(saved);
  if (bytes == NULL) qa_range_error("Out of memory");
  qa_provider_checkpoint *providers = calloc(image.provider_count + 1, sizeof *providers);
  if (!providers) qa_range_error("Out of memory");
  memcpy(providers, image.providers, image.provider_count * sizeof *providers);
  providers[image.provider_count].provider = image.recipe.map.entities.provider;
  providers[image.provider_count].schema = SCHEMA;
  providers[image.provider_count].version = 1;
  providers[image.provider_count].bytes.bytes = bytes;
  providers[image.provider_count].bytes.length = byte_length;
  image.providers = providers;
  image.provider_count++;
  return image;
}
void qa_app_apply_team_arena_overrides(const qa_app_team_arena_overrides *state, const qa_app_override_registry *seats, size_t count) {
  if (state == NULL) return;
  require_seats_reg(state, seats, count);
  if (strcmp(state->phase, "released") == 0) return;
  for (size_t i = 0; i < state->seat_count; i++) {
    const qa_app_override_registry *seat = find_seat(seats, count, state->seats[i].seat, state->seats[i].client);
    if (seat == NULL) qa_range_error("Missing override seat");
    cvar_set_(seat->cvars, "ui_drawTimer", state->seats[i].baseline, true);
    cvar_set_(seat->cvars, "cg_drawTimer", state->seats[i].effective, true);
  }
}
qa_app_team_arena_overrides qa_app_release_team_arena_overrides(const qa_app_team_arena_overrides *state,
                                                                const qa_app_override_registry *seats, size_t count, bool *out_is_new) {
  *out_is_new = false;
  if (state == NULL) { qa_app_team_arena_overrides empty; memset(&empty, 0, sizeof empty); return empty; }
  if (strcmp(state->phase, "released") == 0) return *state;
  require_seats_reg(state, seats, count);
  for (size_t i = 0; i < state->seat_count; i++) {
    const qa_app_override_registry *seat = find_seat(seats, count, state->seats[i].seat, state->seats[i].client);
    if (seat == NULL) qa_range_error("Missing override seat");
    cvar_set_(seat->cvars, "cg_drawTimer", state->seats[i].baseline, true);
  }
  qa_app_team_arena_overrides released; memset(&released, 0, sizeof released);
  released.phase = "released";
  released.seats = calloc(state->seat_count ? state->seat_count : 1, sizeof *released.seats);
  if (!released.seats) qa_range_error("Out of memory");
  released.seat_count = state->seat_count;
  for (size_t i = 0; i < state->seat_count; i++) {
    released.seats[i] = state->seats[i];
    released.seats[i].baseline = xdup(state->seats[i].baseline);
    released.seats[i].effective = xdup(state->seats[i].effective);
  }
  *out_is_new = true;
  return released;
}
void qa_app_cvar_archive_entries_free(qa_cvar_archive_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) { free(entries[i].name); free(entries[i].value); }
  free(entries);
}
qa_cvar_archive_entry *qa_app_team_arena_archive_entries(qa_cvar_registry *registry, const qa_app_team_arena_overrides *state, bool has_seat, double seat, size_t *out_count) {
  size_t count = 0;
  qa_cvar_archive_entry *entries = qa_cvar_registry_archive_entries(registry, NULL, NULL, &count);
  if (state == NULL || strcmp(state->phase, "active") != 0) { *out_count = count; return entries; }
  for (size_t i = 0; i < count; i++) {
    if (has_seat) {
      const qa_app_team_arena_override_seat_row *saved = NULL;
      for (size_t j = 0; j < state->seat_count; j++) if (state->seats[j].seat == seat) saved = &state->seats[j];
      if (ci_eq(entries[i].name, "cg_drawtimer") && saved != NULL) {
        free(entries[i].value);
        entries[i].value = xdup(saved->baseline);
      }
    } else {
      const char *saved_name = NULL;
      for (size_t j = 0; j < 6; j++)
        if (ci_eq(qa_app_team_arena_server_overrides[j].name, entries[i].name)) saved_name = qa_app_team_arena_server_overrides[j].saved;
      if (saved_name != NULL) {
        qa_cvar_read saved; memset(&saved, 0, sizeof saved);
        if (!qa_cvar_registry_find(registry, saved_name, &saved))
          qa_range_error("Team Arena source archive baseline is missing");
        free(entries[i].value);
        entries[i].value = xdup(saved.value);
        qa_cvar_read_free(&saved);
      }
    }
  }
  *out_count = count;
  return entries;
}
