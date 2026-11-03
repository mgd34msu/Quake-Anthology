/* Port of src/app/bootstrap/network/unified-native-components.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_native_components.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include "qa/foundation/support.h"
#include "qa/persistence/mods.h"
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static qa_save_reader field_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("out of memory");
  return out;
}
static double bounded_int(const qa_save_reader *reader, double minimum, double maximum) {
  double value = qa_save_reader_integer_min(reader, minimum);
  if (value != trunc(value) || fabs(value) > 9007199254740991.0 || value > maximum)
    qa_save_reader_fail(reader, "native component integer exceeds its range");
  return value;
}
static const char *bounded_text(const qa_save_reader *reader, size_t maximum) {
  const char *value = qa_save_reader_string(reader);
  size_t length = 0;
  while (value[length]) length++;
  if (length > maximum) qa_save_reader_fail(reader, "invalid native component string");
  return value;
}
static bool same_numbers(const double *a, size_t an, const double *b, size_t bn) {
  if (an != bn) return false;
  for (size_t i = 0; i < an; i++) if (a[i] != b[i]) return false;
  return true;
}
static bool actor_equal(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token
      && a.session.token == b.session.token;
}
qa_app_unified_native_projection qa_app_unified_project_native_components(
    const qa_app_unified_native_publication *previous, size_t previous_count,
    const qa_app_unified_native_publication *sources, size_t source_count) {
  qa_app_unified_native_projection out = {0};
  out.changed = source_count != previous_count;
  out.states = xmalloc(source_count * sizeof *out.states);
  out.state_count = source_count;
  out.frames = xmalloc(source_count * sizeof *out.frames);
  out.frame_count = source_count;
  for (size_t i = 0; i < source_count; i++) {
    const qa_app_unified_native_publication *source = &sources[i];
    const qa_app_unified_native_publication *old = NULL;
    for (size_t j = 0; j < previous_count; j++)
      if (strcmp(previous[j].identity.owner.provider, source->identity.owner.provider) == 0)
        old = &previous[j];
    bool same = old && qa_same_presentation_owner(&source->identity.owner, old->identity.owner)
        && source->identity.generation == old->identity.generation;
    if (same && (!qa_same_mod_identity(&source->identity.identity, &old->identity.identity)
            || !actor_equal(source->viewer, old->viewer)))
      qa_raise("Error", 0, 0, "Native component activation changed identity or recipient");
    bool has_hud = source->frame.has_native_hud;
    bool old_hud = same && old->frame.has_native_hud;
    const qa_native_q2_hud_frame *hud = &source->frame.native_hud_frame;
    const qa_native_q2_hud_frame *old_frame = old ? &old->frame.native_hud_frame : NULL;
    bool same_config = has_hud && old_hud && hud->configstrings == old_frame->configstrings;
    bool same_hud = !has_hud ? !old_hud
        : old_hud && same_config
            && strcmp(source->frame.native_hud_mode, old->frame.native_hud_mode) == 0
            && strcmp(hud->protocol.kind ? hud->protocol.kind : "",
                      old_frame->protocol.kind ? old_frame->protocol.kind : "") == 0
            && strcmp(hud->layout, old_frame->layout) == 0
            && hud->player_number == old_frame->player_number
            && same_numbers(hud->inventory, hud->inventory_count,
                            old_frame->inventory, old_frame->inventory_count);
    const qa_presentation_owner *previous_owner = i < previous_count
        ? &previous[i].identity.owner : NULL;
    bool owner_same = previous_owner
        && qa_same_presentation_owner(previous_owner, source->identity.owner);
    out.changed = out.changed || !same || !same_hud || !owner_same;
    qa_app_unified_native_state *state = &out.states[i];
    memset(state, 0, sizeof *state);
    state->identity = source->identity;
    state->has_hud = has_hud;
    if (has_hud) {
      state->hud.mode = source->frame.native_hud_mode;
      state->hud.protocol = hud->protocol;
      /* Configstring entries are borrowed from the caller's map
       * representation when it changed (see header note). */
      state->hud.has_configstrings = !same_config;
      state->hud.configstrings = (qa_native_q2_config_string *)hud->configstrings;
      state->hud.layout = hud->layout;
      state->hud.inventory = (double *)hud->inventory;
      state->hud.inventory_count = hud->inventory_count;
      state->hud.player_number = hud->player_number;
    }
    qa_app_unified_native_frame *frame = &out.frames[i];
    memset(frame, 0, sizeof *frame);
    frame->owner = source->identity.owner;
    frame->generation = source->identity.generation;
    frame->viewer = source->viewer;
    frame->has_view = source->frame.has_native_view;
    frame->view = source->frame.native_view;
    frame->has_hud = has_hud;
    if (has_hud) {
      frame->hud.stats = (double *)hud->stats;
      frame->hud.stat_count = hud->stat_count;
      frame->hud.server_frame = hud->server_frame;
      frame->hud.time_milliseconds = hud->time_milliseconds;
      frame->hud.has_frame_time = hud->has_frame_time_milliseconds;
      frame->hud.frame_time_milliseconds = hud->frame_time_milliseconds;
    }
  }
  return out;
}
void qa_app_unified_native_projection_free(qa_app_unified_native_projection *projection) {
  free(projection->states);
  free(projection->frames);
  memset(projection, 0, sizeof *projection);
}
static qa_save_value *object(void) {
  qa_save_value *value = qa_save_value_object();
  if (!value) qa_range_error("out of memory");
  return value;
}
static void set(qa_save_value *target, const char *key, qa_save_value *child) {
  if (!child || !qa_save_value_set(target, key, child)) qa_range_error("out of memory");
}
static void push(qa_save_value *target, qa_save_value *child) {
  if (!child || !qa_save_value_push(target, child)) qa_range_error("out of memory");
}
static qa_save_value *clone_value(const qa_save_value *value) {
  if (!value) return qa_save_value_undefined();
  qa_save_value *out = NULL;
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: out = qa_save_value_undefined(); break;
    case QA_SAVE_NULL: out = qa_save_value_null(); break;
    case QA_SAVE_BOOLEAN: out = qa_save_value_boolean(value->boolean); break;
    case QA_SAVE_NUMBER: out = qa_save_value_number(value->number); break;
    case QA_SAVE_BIGINT: out = qa_save_value_bigint_text(value->text); break;
    case QA_SAVE_STRING: out = qa_save_value_string(value->text); break;
    case QA_SAVE_BYTES: out = qa_save_value_bytes(value->bytes, value->byte_length); break;
    case QA_SAVE_ARRAY:
      out = qa_save_value_array();
      if (out) for (size_t i = 0; i < value->item_count; i++) push(out, clone_value(value->items[i]));
      break;
    case QA_SAVE_OBJECT:
      out = qa_save_value_object();
      if (out) for (size_t i = 0; i < value->entry_count; i++)
        set(out, value->entries[i].key, clone_value(value->entries[i].value));
      break;
  }
  if (!out) qa_range_error("out of memory");
  return out;
}
static qa_save_value *write_owner(qa_presentation_owner owner) {
  qa_save_value *out = object();
  set(out, "provider", qa_save_value_string(owner.provider));
  set(out, "generation", qa_save_value_number(owner.generation));
  return out;
}
static qa_save_value *write_vector(qa_vec3 v) {
  qa_save_value *out = object();
  set(out, "x", qa_save_value_number(v.x));
  set(out, "y", qa_save_value_number(v.y));
  set(out, "z", qa_save_value_number(v.z));
  return out;
}
static qa_save_value *write_color(qa_vec4 v) {
  qa_save_value *out = write_vector((qa_vec3){v.x, v.y, v.z});
  set(out, "w", qa_save_value_number(v.w));
  return out;
}
static qa_save_value *write_player_view(const qa_session_player_view *view) {
  qa_save_value *out = object();
  set(out, "origin", write_vector(view->origin));
  set(out, "angles", write_vector(view->angles));
  set(out, "viewHeight", qa_save_value_number(view->view_height));
  if (view->has_blend) set(out, "blend", write_color(view->blend));
  if (view->has_damage_blend) set(out, "damageBlend", write_color(view->damage_blend));
  if (view->has_kick_angles) set(out, "kickAngles", write_vector(view->kick_angles));
  if (view->has_field_of_view) set(out, "fieldOfView", qa_save_value_number(view->field_of_view));
  if (view->has_client_view_offset_delta)
    set(out, "clientViewOffsetDelta", write_vector(view->client_view_offset_delta));
  if (view->foreign_character_death)
    set(out, "foreignCharacterDeath", qa_save_value_boolean(true));
  if (view->has_pitch_drift) {
    qa_save_value *drift = object();
    set(drift, "grounded", qa_save_value_boolean(view->pitch_drift_grounded));
    set(drift, "idealPitch", qa_save_value_number(view->pitch_drift_ideal_pitch));
    set(drift, "disabled", qa_save_value_boolean(view->pitch_drift_disabled));
    set(out, "pitchDrift", drift);
  }
  return out;
}
static qa_save_value *write_camera_view(const qa_native_mod_camera_view *view) {
  qa_save_value *out = write_player_view(&view->view);
  qa_save_value *native = object();
  set(native, "edition", qa_save_value_string(view->edition));
  set(native, "movementOrigin", write_vector(view->movement_origin));
  set(native, "renderFlags", qa_save_value_number(view->render_flags));
  set(native, "positionPrediction", qa_save_value_boolean(view->position_prediction));
  set(native, "angularPrediction", qa_save_value_boolean(view->angular_prediction));
  set(native, "weaponVisible", qa_save_value_boolean(view->weapon_visible));
  set(out, "native", native);
  return out;
}
qa_save_value *qa_app_unified_write_native_states(
    const qa_app_unified_native_state *sources, size_t count) {
  qa_save_value *list = qa_save_value_array();
  if (!list) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    const qa_app_unified_native_state *source = &sources[i];
    qa_save_value *entry = object();
    set(entry, "owner", write_owner(source->identity.owner));
    set(entry, "identity", clone_value(source->identity.identity_wire));
    set(entry, "generation", qa_save_value_number(source->identity.generation));
    if (!source->has_hud) {
      set(entry, "hud", qa_save_value_null());
    } else {
      qa_save_value *hud = object();
      set(hud, "mode", qa_save_value_string(source->hud.mode));
      qa_save_value *frame = object();
      qa_save_value *protocol = object();
      set(protocol, "kind", qa_save_value_string(source->hud.protocol.kind));
      set(protocol, "version", qa_save_value_number((double)source->hud.protocol.version));
      set(frame, "protocol", protocol);
      if (!source->hud.has_configstrings) {
        set(frame, "configstrings", qa_save_value_null());
      } else {
        qa_save_value *configs = qa_save_value_array();
        if (!configs) qa_range_error("out of memory");
        for (size_t j = 0; j < source->hud.configstring_count; j++) {
          qa_save_value *row = object();
          set(row, "index", qa_save_value_number(source->hud.configstrings[j].index));
          set(row, "value", qa_save_value_string(source->hud.configstrings[j].value));
          push(configs, row);
        }
        set(frame, "configstrings", configs);
      }
      set(frame, "layout", qa_save_value_string(source->hud.layout));
      qa_save_value *inventory = qa_save_value_array();
      if (!inventory) qa_range_error("out of memory");
      for (size_t j = 0; j < source->hud.inventory_count; j++)
        push(inventory, qa_save_value_number(source->hud.inventory[j]));
      set(frame, "inventory", inventory);
      set(frame, "playerNumber", qa_save_value_number(source->hud.player_number));
      set(hud, "frame", frame);
      set(entry, "hud", hud);
    }
    push(list, entry);
  }
  return list;
}
static qa_q2_protocol_identity read_protocol(const qa_save_reader *reader) {
  static const char *kinds[] = {"q2-classic", "q2-rerelease"};
  qa_save_reader kind_reader = field_of(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 2);
  qa_save_reader_destroy(&kind_reader);
  qa_save_reader version = field_of(reader, "version");
  qa_q2_protocol_identity out = {kind, 0, 0, 0};
  if (strcmp(kind, "q2-classic") == 0) {
    qa_save_reader_literal_number(&version, 34);
    out.version = 34;
  } else {
    qa_save_reader_literal_number(&version, 1038);
    out.version = 1038;
  }
  qa_save_reader_destroy(&version);
  return out;
}
typedef struct states_scope { qa_app_unified_native_state *states; size_t count; } states_scope;
static void states_scope_free(void *arg) {
  states_scope *scope = arg;
  qa_app_unified_native_states_free(scope->states, scope->count);
}
size_t qa_app_unified_read_native_states(const qa_save_reader *reader,
    qa_app_unified_native_state **out) {
  *out = NULL;
  if (qa_save_reader_is_undefined(reader)) return 0;
  size_t count = qa_save_reader_list_count(reader);
  states_scope scope = {calloc(count ? count : 1, sizeof *scope.states), 0};
  if (!scope.states) qa_range_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, states_scope_free, &scope);
  qa_app_unified_native_state *states = scope.states;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader source;
    if (!qa_save_reader_item(reader, i, &source)) qa_range_error("out of memory");
    qa_app_unified_native_state *state = &states[i];
    scope.count = i + 1;
    qa_save_reader owner = field_of(&source, "owner");
    state->identity.owner = qa_app_unified_read_component_owner(&owner);
    qa_save_reader_destroy(&owner);
    qa_save_reader identity = field_of(&source, "identity");
    state->identity.identity_wire = clone_value(identity.value);
    state->identity.identity = qa_read_mod_identity(&identity);
    qa_save_reader_destroy(&identity);
    qa_save_reader generation = field_of(&source, "generation");
    state->identity.generation = bounded_int(&generation, 0, 9007199254740991.0);
    qa_save_reader_destroy(&generation);
    qa_save_reader hud = field_of(&source, "hud");
    if (!qa_save_reader_is_null(&hud)) {
      state->has_hud = true;
      qa_save_reader frame = field_of(&hud, "frame");
      qa_save_reader protocol_reader = field_of(&frame, "protocol");
      qa_q2_protocol_identity protocol = read_protocol(&protocol_reader);
      qa_save_reader_destroy(&protocol_reader);
      state->hud.protocol = protocol;
      qa_app_q2_layout layout = qa_app_q2_application_layout(protocol);
      qa_save_reader configs = field_of(&frame, "configstrings");
      if (!qa_save_reader_is_null(&configs)) {
        state->hud.has_configstrings = true;
        size_t config_count = qa_save_reader_list_count(&configs);
        qa_native_q2_config_string *entries = calloc(config_count ? config_count : 1, sizeof *entries);
        if (!entries) qa_range_error("out of memory");
        state->hud.configstrings = entries;
        state->hud.configstring_count = config_count;
        for (size_t j = 0; j < config_count; j++) {
          qa_save_reader row;
          if (!qa_save_reader_item(&configs, j, &row)) qa_range_error("out of memory");
          qa_save_reader index_reader = field_of(&row, "index");
          entries[j].index = bounded_int(&index_reader, 0, layout.max_config_strings - 1);
          qa_save_reader_destroy(&index_reader);
          qa_save_reader value_reader = field_of(&row, "value");
          entries[j].value = bounded_text(&value_reader, 65535);
          qa_save_reader_destroy(&value_reader);
          qa_save_reader_destroy(&row);
        }
        /* TS builds the Map after reading every entry; a bitmap over the index range stands in
         * for it (indices are already bounded by the layout). */
        size_t range = (size_t)layout.max_config_strings;
        uint8_t *seen = calloc(range ? range : 1, 1);
        if (!seen) qa_range_error("out of memory");
        bool duplicate = false;
        for (size_t j = 0; j < config_count && !duplicate; j++) {
          size_t index = (size_t)entries[j].index;
          if (seen[index]) duplicate = true;
          seen[index] = 1;
        }
        free(seen);
        if (duplicate) qa_save_reader_fail(&configs, "duplicate native configstrings");
      }
      qa_save_reader_destroy(&configs);
      qa_save_reader inventory_reader = field_of(&frame, "inventory");
      size_t inventory_count = qa_save_reader_list_count(&inventory_reader);
      double *inventory = calloc(inventory_count ? inventory_count : 1, sizeof *inventory);
      if (!inventory) qa_range_error("out of memory");
      state->hud.inventory = inventory;
      state->hud.inventory_count = inventory_count;
      for (size_t j = 0; j < inventory_count; j++) {
        qa_save_reader item;
        if (!qa_save_reader_item(&inventory_reader, j, &item)) qa_range_error("out of memory");
        inventory[j] = bounded_int(&item, -32768, 32767);
        qa_save_reader_destroy(&item);
      }
      if (inventory_count > 256) qa_save_reader_fail(&frame, "native inventory exceeds source slots");
      state->hud.inventory = inventory;
      state->hud.inventory_count = inventory_count;
      qa_save_reader_destroy(&inventory_reader);
      static const char *modes[] = {"layout-overlay", "replace-status"};
      qa_save_reader mode_reader = field_of(&hud, "mode");
      state->hud.mode = qa_save_reader_choice_string(&mode_reader, modes, 2);
      qa_save_reader_destroy(&mode_reader);
      qa_save_reader layout_reader = field_of(&frame, "layout");
      state->hud.layout = bounded_text(&layout_reader, 65535);
      qa_save_reader_destroy(&layout_reader);
      qa_save_reader player_reader = field_of(&frame, "playerNumber");
      state->hud.player_number = bounded_int(&player_reader, 0, 255);
      qa_save_reader_destroy(&player_reader);
      qa_save_reader_destroy(&frame);
    }
    qa_save_reader_destroy(&hud);
    qa_save_reader_destroy(&source);
  }
  qa_cleanup_pop(&guard);
  *out = states;
  return count;
}
void qa_app_unified_native_states_free(qa_app_unified_native_state *states, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free((void *)states[i].identity.identity.modules);
    free((void *)states[i].identity.identity.providers);
    qa_save_value_free((qa_save_value *)states[i].identity.identity_wire);
    free(states[i].hud.configstrings);
    free(states[i].hud.inventory);
  }
  free(states);
}
qa_save_value *qa_app_unified_write_native_frames(
    const qa_app_unified_native_frame *sources, size_t count) {
  qa_save_value *list = qa_save_value_array();
  if (!list) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) {
    const qa_app_unified_native_frame *source = &sources[i];
    qa_save_value *entry = object();
    set(entry, "owner", write_owner(source->owner));
    set(entry, "generation", qa_save_value_number(source->generation));
    set(entry, "viewer", qa_app_unified_wire_actor(source->viewer));
    if (!source->has_hud) {
      set(entry, "hud", qa_save_value_null());
    } else {
      qa_save_value *hud = object();
      qa_save_value *stats = qa_save_value_array();
      if (!stats) qa_range_error("out of memory");
      for (size_t j = 0; j < source->hud.stat_count; j++)
        push(stats, qa_save_value_number(source->hud.stats[j]));
      set(hud, "stats", stats);
      set(hud, "serverFrame", qa_save_value_number(source->hud.server_frame));
      set(hud, "timeMilliseconds", qa_save_value_number(source->hud.time_milliseconds));
      if (source->hud.has_frame_time)
        set(hud, "frameTimeMilliseconds", qa_save_value_number(source->hud.frame_time_milliseconds));
      set(entry, "hud", hud);
    }
    if (source->has_view) set(entry, "view", write_camera_view(&source->view));
    else set(entry, "view", qa_save_value_null());
    push(list, entry);
  }
  return list;
}
typedef struct frames_scope { qa_app_unified_native_frame *frames; size_t count; } frames_scope;
static void frames_scope_free(void *arg) {
  frames_scope *scope = arg;
  qa_app_unified_native_frames_free(scope->frames, scope->count);
}
size_t qa_app_unified_read_native_frames(const qa_save_reader *reader,
    const qa_app_unified_identity_decoder *identity, qa_app_unified_native_frame **out) {
  *out = NULL;
  if (qa_save_reader_is_undefined(reader)) return 0;
  size_t count = qa_save_reader_list_count(reader);
  frames_scope scope = {calloc(count ? count : 1, sizeof *scope.frames), 0};
  if (!scope.frames) qa_range_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, frames_scope_free, &scope);
  qa_app_unified_native_frame *frames = scope.frames;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader source;
    if (!qa_save_reader_item(reader, i, &source)) qa_range_error("out of memory");
    qa_app_unified_native_frame *frame = &frames[i];
    scope.count = i + 1;
    qa_save_reader owner = field_of(&source, "owner");
    frame->owner = qa_app_unified_read_component_owner(&owner);
    qa_save_reader_destroy(&owner);
    qa_save_reader generation = field_of(&source, "generation");
    frame->generation = bounded_int(&generation, 0, 9007199254740991.0);
    qa_save_reader_destroy(&generation);
    qa_save_reader viewer = field_of(&source, "viewer");
    frame->viewer = qa_app_unified_read_actor(&viewer, identity);
    qa_save_reader_destroy(&viewer);
    qa_save_reader view = field_of(&source, "view");
    if (!qa_save_reader_is_null(&view)) {
      frame->has_view = true;
      frame->view = qa_app_unified_read_native_camera_view(&view);
    }
    qa_save_reader_destroy(&view);
    qa_save_reader hud = field_of(&source, "hud");
    if (!qa_save_reader_is_null(&hud)) {
      frame->has_hud = true;
      qa_save_reader stats_reader = field_of(&hud, "stats");
      size_t stat_count = qa_save_reader_list_count(&stats_reader);
      double *stats = calloc(stat_count ? stat_count : 1, sizeof *stats);
      if (!stats) qa_range_error("out of memory");
      frame->hud.stats = stats;
      frame->hud.stat_count = stat_count;
      for (size_t j = 0; j < stat_count; j++) {
        qa_save_reader item;
        if (!qa_save_reader_item(&stats_reader, j, &item)) qa_range_error("out of memory");
        stats[j] = bounded_int(&item, -32768, 32767);
        qa_save_reader_destroy(&item);
      }
      if (stat_count != 32 && stat_count != 64)
        qa_save_reader_fail(&hud, "invalid source native stat count");
      frame->hud.stats = stats;
      frame->hud.stat_count = stat_count;
      qa_save_reader_destroy(&stats_reader);
      qa_save_reader frame_time = field_of(&hud, "frameTimeMilliseconds");
      if (!qa_save_reader_is_undefined(&frame_time)) {
        frame->hud.has_frame_time = true;
        frame->hud.frame_time_milliseconds = qa_save_reader_finite(&frame_time);
        if (frame->hud.frame_time_milliseconds <= 0)
          qa_save_reader_fail(&hud, "native source frame interval must be positive");
      }
      qa_save_reader_destroy(&frame_time);
      qa_save_reader server_frame = field_of(&hud, "serverFrame");
      frame->hud.server_frame = bounded_int(&server_frame, 0, 2147483647);
      qa_save_reader_destroy(&server_frame);
      qa_save_reader time_reader = field_of(&hud, "timeMilliseconds");
      frame->hud.time_milliseconds = qa_save_reader_finite(&time_reader);
      qa_save_reader_destroy(&time_reader);
    }
    qa_save_reader_destroy(&hud);
    qa_save_reader_destroy(&source);
  }
  qa_cleanup_pop(&guard);
  *out = frames;
  return count;
}
void qa_app_unified_native_frames_free(qa_app_unified_native_frame *frames, size_t count) {
  for (size_t i = 0; i < count; i++) free(frames[i].hud.stats);
  free(frames);
}
