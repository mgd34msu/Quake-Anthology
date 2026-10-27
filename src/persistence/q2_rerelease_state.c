/* Port of src/persistence/q2-rerelease-state.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_rerelease_state.h"

#include <stdlib.h>
#include <string.h>

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
/* TS readSavedActor (save-image.ts, not yet ported as a module):
 * {slot: integer(0), generation: integer(0)}. Local copy only. */
static qa_saved_actor_id read_actor(const qa_save_reader *reader) {
  qa_saved_actor_id actor;
  qa_save_reader slot = field(reader, "slot");
  actor.slot = qa_save_reader_integer_min(&slot, 0);
  qa_save_reader_destroy(&slot);
  qa_save_reader generation = field(reader, "generation");
  actor.generation = qa_save_reader_integer_min(&generation, 0);
  qa_save_reader_destroy(&generation);
  return actor;
}
static bool set_own(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
static bool push_own(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) { qa_save_value_free(child); return false; }
  return true;
}
__attribute__((unused)) static qa_save_value *str_value(const char *text) {
  return qa_save_value_string(text != NULL ? text : "");
}
__attribute__((unused)) static qa_save_value *vec_value(const qa_vec3 *vector) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "x", qa_save_value_number(vector->x)) ||
      !set_own(obj, "y", qa_save_value_number(vector->y)) ||
      !set_own(obj, "z", qa_save_value_number(vector->z))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
static qa_save_value *actor_value(const qa_saved_actor_id *actor) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "slot", qa_save_value_number(actor->slot)) ||
      !set_own(obj, "generation", qa_save_value_number(actor->generation))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
__attribute__((unused)) static qa_save_value *time_value(const qa_source_time *time) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "kind", qa_save_value_string(
          time->kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds")) ||
      !set_own(obj, "value", qa_save_value_number(time->value))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}
/* Moves every entry of `child` into `object` (TS object spread). */
__attribute__((unused)) static bool merge_into(qa_save_value *object, qa_save_value *child) {
  for (size_t i = 0; i < child->entry_count; i++) {
    qa_save_value *value = child->entries[i].value;
    child->entries[i].value = NULL;
    if (!qa_save_value_set(object, child->entries[i].key, value)) {
      child->entries[i].value = value;
      return false;
    }
  }
  return true;
}

static void read_q2_fog(const qa_save_reader *reader, qa_q2_fog_settings *out);
static qa_save_value *build_q2_fog(const qa_q2_fog_settings *in);
static void read_q2_height_fog(const qa_save_reader *reader, qa_q2_height_fog_settings *out);
static qa_save_value *build_q2_height_fog(const qa_q2_height_fog_settings *in);
static void read_q2_fog_state(const qa_save_reader *reader, qa_q2_fog_state *out);
static qa_save_value *build_q2_fog_state(const qa_q2_fog_state *in);
static void read_q2_pending_landmark(const qa_save_reader *reader, qa_q2_pending_landmark *out);
static qa_save_value *build_q2_pending_landmark(const qa_q2_pending_landmark *in);
static void read_q2_rerelease_player_state(const qa_save_reader *reader, qa_q2_rerelease_saved_player *out);
static qa_save_value *build_q2_rerelease_player_state(const qa_q2_rerelease_saved_player *in);
static void read_q2_rerelease_options(const qa_save_reader *reader, qa_q2_rerelease_options *out);
static qa_save_value *build_q2_rerelease_options(const qa_q2_rerelease_options *in);
static void read_q2_intermission_camera(const qa_save_reader *reader, qa_q2_intermission_camera *out);
static qa_save_value *build_q2_intermission_camera(const qa_q2_intermission_camera *in);
static void read_q2_rerelease_player_entry(const qa_save_reader *reader, qa_q2_rerelease_player_entry *out);
static qa_save_value *build_q2_rerelease_player_entry(const qa_q2_rerelease_player_entry *in);
static void read_q2_squad_spawn(const qa_save_reader *reader, qa_q2_squad_spawn *out);
static qa_save_value *build_q2_squad_spawn(const qa_q2_squad_spawn *in);
static void read_q2_rerelease_players_checkpoint(const qa_save_reader *reader, qa_q2_rerelease_players_checkpoint *out);
static qa_save_value *build_q2_rerelease_players_checkpoint(const qa_q2_rerelease_players_checkpoint *in);
static void read_q2_rerelease_sky(const qa_save_reader *reader, qa_q2_rerelease_sky *out);
static qa_save_value *build_q2_rerelease_sky(const qa_q2_rerelease_sky *in);
static void read_q2_rerelease_poi(const qa_save_reader *reader, qa_q2_rerelease_poi *out);
static qa_save_value *build_q2_rerelease_poi(const qa_q2_rerelease_poi *in);
static void read_q2_rerelease_goals(const qa_save_reader *reader, qa_q2_rerelease_goals *out);
static qa_save_value *build_q2_rerelease_goals(const qa_q2_rerelease_goals *in);
static void read_q2_campaign_mission(const qa_save_reader *reader, qa_q2_campaign_mission *out);
static qa_save_value *build_q2_campaign_mission(const qa_q2_campaign_mission *in);
static void read_q2_campaign_level(const qa_save_reader *reader, qa_q2_campaign_level *out);
static qa_save_value *build_q2_campaign_level(const qa_q2_campaign_level *in);
static void read_q2_campaign(const qa_save_reader *reader, qa_q2_campaign *out);
static qa_save_value *build_q2_campaign(const qa_q2_campaign *in);
static void read_q2_q64_eye_state(const qa_save_reader *reader, qa_q2_q64_eye_state *out);
static qa_save_value *build_q2_q64_eye_state(const qa_q2_q64_eye_state *in);
static void read_q2_q64_eye_entry(const qa_save_reader *reader, qa_q2_q64_eye_entry *out);
static qa_save_value *build_q2_q64_eye_entry(const qa_q2_q64_eye_entry *in);
static void read_q2_q64_camera_state(const qa_save_reader *reader, qa_q2_q64_camera_state *out);
static qa_save_value *build_q2_q64_camera_state(const qa_q2_q64_camera_state *in);
static void read_q2_q64_camera_entry(const qa_save_reader *reader, qa_q2_q64_camera_entry *out);
static qa_save_value *build_q2_q64_camera_entry(const qa_q2_q64_camera_entry *in);
static void read_q2_q64_dummy_state(const qa_save_reader *reader, qa_q2_q64_dummy_state *out);
static qa_save_value *build_q2_q64_dummy_state(const qa_q2_q64_dummy_state *in);
static void read_q2_q64_dummy_entry(const qa_save_reader *reader, qa_q2_q64_dummy_entry *out);
static qa_save_value *build_q2_q64_dummy_entry(const qa_q2_q64_dummy_entry *in);
static void read_q2_rerelease_q64_checkpoint(const qa_save_reader *reader, qa_q2_rerelease_q64_checkpoint *out);
static qa_save_value *build_q2_rerelease_q64_checkpoint(const qa_q2_rerelease_q64_checkpoint *in);
static void read_q2_light_entry(const qa_save_reader *reader, qa_q2_light_entry *out);
static qa_save_value *build_q2_light_entry(const qa_q2_light_entry *in);
static void read_q2_health_bar(const qa_save_reader *reader, qa_q2_health_bar *out);
static qa_save_value *build_q2_health_bar(const qa_q2_health_bar *in);
static void read_q2_health_target_entry(const qa_save_reader *reader, qa_q2_health_target_entry *out);
static qa_save_value *build_q2_health_target_entry(const qa_q2_health_target_entry *in);
static void read_q2_picked_up_by_entry(const qa_save_reader *reader, qa_q2_picked_up_by_entry *out);
static qa_save_value *build_q2_picked_up_by_entry(const qa_q2_picked_up_by_entry *in);
static void read_q2_trigger_sound_time(const qa_save_reader *reader, qa_q2_trigger_sound_time *out);
static qa_save_value *build_q2_trigger_sound_time(const qa_q2_trigger_sound_time *in);
static void read_q2_rerelease_module_checkpoint(const qa_save_reader *reader, qa_q2_rerelease_module_checkpoint *out);
static qa_save_value *build_q2_rerelease_module_checkpoint(const qa_q2_rerelease_module_checkpoint *in);

static void read_q2_fog(const qa_save_reader *reader, qa_q2_fog_settings *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "density");
    (*out).density = qa_save_reader_number(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "color");
    (*out).color = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "skyFactor");
    (*out).sky_factor = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_fog(const qa_q2_fog_settings *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "density", qa_save_value_number((*in).density))) goto fail;
  if (!set_own(obj, "color", vec_value(&(*in).color))) goto fail;
  if (!set_own(obj, "skyFactor", qa_save_value_number((*in).sky_factor))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_height_fog(const qa_save_reader *reader, qa_q2_height_fog_settings *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "startColor");
    (*out).start_color = qa_read_vector(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "startDistance");
    (*out).start_distance = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "endColor");
    (*out).end_color = qa_read_vector(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "endDistance");
    (*out).end_distance = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "falloff");
    (*out).falloff = qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "density");
    (*out).density = qa_save_reader_number(&f5);
    qa_save_reader_destroy(&f5); }
}
static qa_save_value *build_q2_height_fog(const qa_q2_height_fog_settings *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "startColor", vec_value(&(*in).start_color))) goto fail;
  if (!set_own(obj, "startDistance", qa_save_value_number((*in).start_distance))) goto fail;
  if (!set_own(obj, "endColor", vec_value(&(*in).end_color))) goto fail;
  if (!set_own(obj, "endDistance", qa_save_value_number((*in).end_distance))) goto fail;
  if (!set_own(obj, "falloff", qa_save_value_number((*in).falloff))) goto fail;
  if (!set_own(obj, "density", qa_save_value_number((*in).density))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_fog_state(const qa_save_reader *reader, qa_q2_fog_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "fog");
    read_q2_fog(&f0, &(*out).fog);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "heightFog");
    read_q2_height_fog(&f1, &(*out).height_fog);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_fog_state(const qa_q2_fog_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "fog", build_q2_fog(&(*in).fog))) goto fail;
  if (!set_own(obj, "heightFog", build_q2_height_fog(&(*in).height_fog))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_pending_landmark(const qa_save_reader *reader, qa_q2_pending_landmark *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "name");
    (*out).name = qa_save_reader_string(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "relativeOrigin");
    (*out).relative_origin = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "relativeVelocity");
    (*out).relative_velocity = qa_read_vector(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "relativeViewAngles");
    (*out).relative_view_angles = qa_read_vector(&f3);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_pending_landmark(const qa_q2_pending_landmark *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "name", str_value((*in).name))) goto fail;
  if (!set_own(obj, "relativeOrigin", vec_value(&(*in).relative_origin))) goto fail;
  if (!set_own(obj, "relativeVelocity", vec_value(&(*in).relative_velocity))) goto fail;
  if (!set_own(obj, "relativeViewAngles", vec_value(&(*in).relative_view_angles))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rerelease_player_state(const qa_save_reader *reader, qa_q2_rerelease_saved_player *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "spawned");
    (*out).spawned = qa_save_reader_boolean(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "gameHelp1Changed");
    (*out).game_help1_changed = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "gameHelp2Changed");
    (*out).game_help2_changed = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "helpChanged");
    (*out).help_changed = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "helpTime");
    (*out).help_time = qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "seat");
    (*out).seat = qa_save_reader_integer_min(&f5, 0);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "socialId");
    (*out).social_id = qa_save_reader_string(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "autoSwitch");
    static const double ch1[] = {0, 1, 2, 3};
    (*out).auto_switch = qa_save_reader_choice_number(&f7, ch1, 4);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "autoShield");
    (*out).auto_shield = qa_save_reader_number(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "dogtag");
    (*out).dogtag = qa_save_reader_string(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "slimeDebounce");
    (*out).slime_debounce = qa_save_reader_number(&f10);
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "windSoundTime");
    (*out).wind_sound_time = qa_save_reader_number(&f11);
    qa_save_reader_destroy(&f11); }
  { qa_save_reader f12 = field(reader, "animationTime");
    (*out).animation_time = qa_save_reader_number(&f12);
    qa_save_reader_destroy(&f12); }
  { qa_save_reader f13 = field(reader, "flashTime");
    (*out).flash_time = qa_save_reader_number(&f13);
    qa_save_reader_destroy(&f13); }
  { qa_save_reader f14 = field(reader, "flashes");
    (*out).flashes = qa_save_reader_number(&f14);
    qa_save_reader_destroy(&f14); }
  { qa_save_reader f15 = field(reader, "lastDamageUntil");
    (*out).last_damage_until = qa_save_reader_number(&f15);
    qa_save_reader_destroy(&f15); }
  { qa_save_reader f16 = field(reader, "lastFiringUntil");
    (*out).last_firing_until = qa_save_reader_number(&f16);
    qa_save_reader_destroy(&f16); }
  { qa_save_reader f17 = field(reader, "lives");
    (*out).lives = qa_save_reader_number(&f17);
    qa_save_reader_destroy(&f17); }
  { qa_save_reader f18 = field(reader, "invisibilityUntil");
    (*out).invisibility_until = qa_save_reader_finite(&f18);
    qa_save_reader_destroy(&f18); }
  { qa_save_reader f19 = field(reader, "invisibilityFadeUntil");
    (*out).invisibility_fade_until = qa_save_reader_finite(&f19);
    qa_save_reader_destroy(&f19); }
  { qa_save_reader f20 = field(reader, "coopRespawnState");
    static const char *const ch2[] = {"none", "in-combat", "bad-area", "blocked", "waiting", "no-lives"};
    (*out).coop_respawn_state = qa_save_reader_choice_string(&f20, ch2, 6);
    qa_save_reader_destroy(&f20); }
  { qa_save_reader f21 = field(reader, "flashlight");
    (*out).flashlight = qa_save_reader_boolean(&f21);
    qa_save_reader_destroy(&f21); }
  { qa_save_reader f22 = field(reader, "fog");
    read_q2_fog_state(&f22, &(*out).fog);
    qa_save_reader_destroy(&f22); }
  { qa_save_reader f23 = field(reader, "wantedFog");
    read_q2_fog_state(&f23, &(*out).wanted_fog);
    qa_save_reader_destroy(&f23); }
  { qa_save_reader f24 = field(reader, "fogTransition");
    (*out).fog_transition = qa_save_reader_number(&f24);
    qa_save_reader_destroy(&f24); }
  { qa_save_reader f25 = field(reader, "bobSkip");
    (*out).bob_skip = qa_save_reader_boolean(&f25);
    qa_save_reader_destroy(&f25); }
  { qa_save_reader f26 = field(reader, "impactDelta");
    (*out).impact_delta = qa_save_reader_number(&f26);
    qa_save_reader_destroy(&f26); }
  { qa_save_reader f27 = field(reader, "onLadder");
    (*out).on_ladder = qa_save_reader_boolean(&f27);
    qa_save_reader_destroy(&f27); }
  { qa_save_reader f28 = field(reader, "grappleReleasedUntil");
    (*out).grapple_released_until = qa_save_reader_number(&f28);
    qa_save_reader_destroy(&f28); }
  { qa_save_reader f29 = field(reader, "grappleAttached");
    (*out).grapple_attached = qa_save_reader_boolean(&f29);
    qa_save_reader_destroy(&f29); }
  { qa_save_reader f30 = field(reader, "awaitingRespawn");
    (*out).awaiting_respawn = qa_save_reader_boolean(&f30);
    qa_save_reader_destroy(&f30); }
  { qa_save_reader f31 = field(reader, "respawnTimeout");
    (*out).respawn_timeout = qa_save_reader_number(&f31);
    qa_save_reader_destroy(&f31); }
  { qa_save_reader f32 = field(reader, "pendingLandmark");
    if (!qa_save_reader_is_null(&f32)) { (*out).has_pending_landmark = true;
      read_q2_pending_landmark(&f32, &(*out).pending_landmark);
    }
    qa_save_reader_destroy(&f32); }
  { qa_save_reader f33 = field(reader, "slowViewAngles");
    (*out).slow_view_angles = qa_read_vector(&f33);
    qa_save_reader_destroy(&f33); }
  { qa_save_reader f34 = field(reader, "quakeTime");
    (*out).quake_time = qa_save_reader_number(&f34);
    qa_save_reader_destroy(&f34); }
  { qa_save_reader f35 = field(reader, "helpLocation");
    (*out).help_location = qa_read_vector(&f35);
    qa_save_reader_destroy(&f35); }
  { qa_save_reader f36 = field(reader, "helpImage");
    (*out).help_image = qa_save_reader_string(&f36);
    qa_save_reader_destroy(&f36); }
  { qa_save_reader f37 = field(reader, "helpPoints");
    (*out).help_points_count = qa_save_reader_list_count(&f37);
    (*out).help_points = (qa_vec3 *)calloc((*out).help_points_count != 0 ? (*out).help_points_count : 1, sizeof(qa_vec3));
    if ((*out).help_points == NULL) qa_save_reader_fail(&f37, "out of memory");
    for (size_t i3 = 0; i3 < (*out).help_points_count; i3++) {
      qa_save_reader it4 = item(&f37, i3);
      (*out).help_points[i3] = qa_read_vector(&it4);
      qa_save_reader_destroy(&it4);
    }
    qa_save_reader_destroy(&f37); }
  { qa_save_reader f38 = field(reader, "helpIndex");
    (*out).help_index = qa_save_reader_number(&f38);
    qa_save_reader_destroy(&f38); }
  { qa_save_reader f39 = field(reader, "helpDrawTime");
    (*out).help_draw_time = qa_save_reader_number(&f39);
    qa_save_reader_destroy(&f39); }
  { qa_save_reader f40 = field(reader, "helpMarkerUntil");
    (*out).help_marker_until = qa_save_reader_is_undefined(&f40) ? 0 : qa_save_reader_finite(&f40);
    qa_save_reader_destroy(&f40); }
}
static qa_save_value *build_q2_rerelease_player_state(const qa_q2_rerelease_saved_player *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "spawned", qa_save_value_boolean((*in).spawned))) goto fail;
  if (!set_own(obj, "gameHelp1Changed", qa_save_value_number((*in).game_help1_changed))) goto fail;
  if (!set_own(obj, "gameHelp2Changed", qa_save_value_number((*in).game_help2_changed))) goto fail;
  if (!set_own(obj, "helpChanged", qa_save_value_number((*in).help_changed))) goto fail;
  if (!set_own(obj, "helpTime", qa_save_value_number((*in).help_time))) goto fail;
  if (!set_own(obj, "seat", qa_save_value_number((*in).seat))) goto fail;
  if (!set_own(obj, "socialId", str_value((*in).social_id))) goto fail;
  if (!set_own(obj, "autoSwitch", qa_save_value_number((*in).auto_switch))) goto fail;
  if (!set_own(obj, "autoShield", qa_save_value_number((*in).auto_shield))) goto fail;
  if (!set_own(obj, "dogtag", str_value((*in).dogtag))) goto fail;
  if (!set_own(obj, "slimeDebounce", qa_save_value_number((*in).slime_debounce))) goto fail;
  if (!set_own(obj, "windSoundTime", qa_save_value_number((*in).wind_sound_time))) goto fail;
  if (!set_own(obj, "animationTime", qa_save_value_number((*in).animation_time))) goto fail;
  if (!set_own(obj, "flashTime", qa_save_value_number((*in).flash_time))) goto fail;
  if (!set_own(obj, "flashes", qa_save_value_number((*in).flashes))) goto fail;
  if (!set_own(obj, "lastDamageUntil", qa_save_value_number((*in).last_damage_until))) goto fail;
  if (!set_own(obj, "lastFiringUntil", qa_save_value_number((*in).last_firing_until))) goto fail;
  if (!set_own(obj, "lives", qa_save_value_number((*in).lives))) goto fail;
  if (!set_own(obj, "invisibilityUntil", qa_save_value_number((*in).invisibility_until))) goto fail;
  if (!set_own(obj, "invisibilityFadeUntil", qa_save_value_number((*in).invisibility_fade_until))) goto fail;
  if (!set_own(obj, "coopRespawnState", str_value((*in).coop_respawn_state))) goto fail;
  if (!set_own(obj, "flashlight", qa_save_value_boolean((*in).flashlight))) goto fail;
  if (!set_own(obj, "fog", build_q2_fog_state(&(*in).fog))) goto fail;
  if (!set_own(obj, "wantedFog", build_q2_fog_state(&(*in).wanted_fog))) goto fail;
  if (!set_own(obj, "fogTransition", qa_save_value_number((*in).fog_transition))) goto fail;
  if (!set_own(obj, "bobSkip", qa_save_value_boolean((*in).bob_skip))) goto fail;
  if (!set_own(obj, "impactDelta", qa_save_value_number((*in).impact_delta))) goto fail;
  if (!set_own(obj, "onLadder", qa_save_value_boolean((*in).on_ladder))) goto fail;
  if (!set_own(obj, "grappleReleasedUntil", qa_save_value_number((*in).grapple_released_until))) goto fail;
  if (!set_own(obj, "grappleAttached", qa_save_value_boolean((*in).grapple_attached))) goto fail;
  if (!set_own(obj, "awaitingRespawn", qa_save_value_boolean((*in).awaiting_respawn))) goto fail;
  if (!set_own(obj, "respawnTimeout", qa_save_value_number((*in).respawn_timeout))) goto fail;
  if (!set_own(obj, "pendingLandmark", (*in).has_pending_landmark ? build_q2_pending_landmark(&(*in).pending_landmark) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "slowViewAngles", vec_value(&(*in).slow_view_angles))) goto fail;
  if (!set_own(obj, "quakeTime", qa_save_value_number((*in).quake_time))) goto fail;
  if (!set_own(obj, "helpLocation", vec_value(&(*in).help_location))) goto fail;
  if (!set_own(obj, "helpImage", str_value((*in).help_image))) goto fail;
  { qa_save_value *arr5 = qa_save_value_array();
    if (arr5 == NULL) goto fail;
    for (size_t i6 = 0; i6 < (*in).help_points_count; i6++) {
      if (!push_own(arr5, vec_value(&(*in).help_points[i6]))) { qa_save_value_free(arr5); goto fail; }
    }
    if (!set_own(obj, "helpPoints", arr5)) goto fail; }
  if (!set_own(obj, "helpIndex", qa_save_value_number((*in).help_index))) goto fail;
  if (!set_own(obj, "helpDrawTime", qa_save_value_number((*in).help_draw_time))) goto fail;
  if (!set_own(obj, "helpMarkerUntil", qa_save_value_number((*in).help_marker_until))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rerelease_options(const qa_save_reader *reader, qa_q2_rerelease_options *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "coopSquadRespawn");
    (*out).coop_squad_respawn = qa_save_reader_boolean(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "coopInstancedItems");
    (*out).coop_instanced_items = qa_save_reader_boolean(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "coopLives");
    (*out).coop_lives = qa_save_reader_boolean(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "coopNumLives");
    (*out).coop_num_lives = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "deathmatchForceRespawn");
    (*out).deathmatch_force_respawn = qa_save_reader_boolean(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "deathmatchNoFallDamage");
    (*out).deathmatch_no_fall_damage = qa_save_reader_boolean(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "deathmatchAllowExit");
    (*out).deathmatch_allow_exit = qa_save_reader_boolean(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "deathmatchSpawnFarthest");
    (*out).deathmatch_spawn_farthest = qa_save_reader_boolean(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "deathmatchForceRespawnTime");
    (*out).deathmatch_force_respawn_time = qa_save_reader_number(&f8);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "coopPlayerCollision");
    (*out).coop_player_collision = qa_save_reader_boolean(&f9);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "autoSaveMinimumTime");
    (*out).auto_save_minimum_time = qa_save_reader_number(&f10);
    qa_save_reader_destroy(&f10); }
}
static qa_save_value *build_q2_rerelease_options(const qa_q2_rerelease_options *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "coopSquadRespawn", qa_save_value_boolean((*in).coop_squad_respawn))) goto fail;
  if (!set_own(obj, "coopInstancedItems", qa_save_value_boolean((*in).coop_instanced_items))) goto fail;
  if (!set_own(obj, "coopLives", qa_save_value_boolean((*in).coop_lives))) goto fail;
  if (!set_own(obj, "coopNumLives", qa_save_value_number((*in).coop_num_lives))) goto fail;
  if (!set_own(obj, "deathmatchForceRespawn", qa_save_value_boolean((*in).deathmatch_force_respawn))) goto fail;
  if (!set_own(obj, "deathmatchNoFallDamage", qa_save_value_boolean((*in).deathmatch_no_fall_damage))) goto fail;
  if (!set_own(obj, "deathmatchAllowExit", qa_save_value_boolean((*in).deathmatch_allow_exit))) goto fail;
  if (!set_own(obj, "deathmatchSpawnFarthest", qa_save_value_boolean((*in).deathmatch_spawn_farthest))) goto fail;
  if (!set_own(obj, "deathmatchForceRespawnTime", qa_save_value_number((*in).deathmatch_force_respawn_time))) goto fail;
  if (!set_own(obj, "coopPlayerCollision", qa_save_value_boolean((*in).coop_player_collision))) goto fail;
  if (!set_own(obj, "autoSaveMinimumTime", qa_save_value_number((*in).auto_save_minimum_time))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_intermission_camera(const qa_save_reader *reader, qa_q2_intermission_camera *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "origin");
    (*out).origin = qa_read_vector(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "angles");
    (*out).angles = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_intermission_camera(const qa_q2_intermission_camera *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "origin", vec_value(&(*in).origin))) goto fail;
  if (!set_own(obj, "angles", vec_value(&(*in).angles))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rerelease_player_entry(const qa_save_reader *reader, qa_q2_rerelease_player_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_rerelease_player_state(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_rerelease_player_entry(const qa_q2_rerelease_player_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_rerelease_player_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_squad_spawn(const qa_save_reader *reader, qa_q2_squad_spawn *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "origin");
    (*out).origin = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "angles");
    (*out).angles = qa_read_vector(&f2);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_squad_spawn(const qa_q2_squad_spawn *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "origin", vec_value(&(*in).origin))) goto fail;
  if (!set_own(obj, "angles", vec_value(&(*in).angles))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rerelease_players_checkpoint(const qa_save_reader *reader, qa_q2_rerelease_players_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "version");
    (*out).version = qa_save_reader_literal_number(&f0, 1);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "options");
    read_q2_rerelease_options(&f1, &(*out).options);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "coopRestartTime");
    (*out).coop_restart_time = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "intermissionFlags");
    (*out).intermission_flags = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "intermissionFadeUntil");
    if (!qa_save_reader_is_null(&f4)) { (*out).has_intermission_fade_until = true;
      (*out).intermission_fade_until = qa_save_reader_number(&f4);
    }
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "intermissionCamera");
    if (!qa_save_reader_is_null(&f5)) { (*out).has_intermission_camera = true;
      read_q2_intermission_camera(&f5, &(*out).intermission_camera);
    }
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "intermissionCameraSet");
    (*out).intermission_camera_set = qa_save_reader_boolean(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "deadlyKillBox");
    (*out).deadly_kill_box = qa_save_reader_boolean(&f7);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "players");
    (*out).players_count = qa_save_reader_list_count(&f8);
    (*out).players = (qa_q2_rerelease_player_entry *)calloc((*out).players_count != 0 ? (*out).players_count : 1, sizeof(qa_q2_rerelease_player_entry));
    if ((*out).players == NULL) qa_save_reader_fail(&f8, "out of memory");
    for (size_t i7 = 0; i7 < (*out).players_count; i7++) {
      qa_save_reader it8 = item(&f8, i7);
      read_q2_rerelease_player_entry(&it8, &(*out).players[i7]);
      qa_save_reader_destroy(&it8);
    }
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "squadSpawns");
    (*out).squad_spawns_count = qa_save_reader_list_count(&f9);
    (*out).squad_spawns = (qa_q2_squad_spawn *)calloc((*out).squad_spawns_count != 0 ? (*out).squad_spawns_count : 1, sizeof(qa_q2_squad_spawn));
    if ((*out).squad_spawns == NULL) qa_save_reader_fail(&f9, "out of memory");
    for (size_t i9 = 0; i9 < (*out).squad_spawns_count; i9++) {
      qa_save_reader it10 = item(&f9, i9);
      read_q2_squad_spawn(&it10, &(*out).squad_spawns[i9]);
      qa_save_reader_destroy(&it10);
    }
    qa_save_reader_destroy(&f9); }
}
static qa_save_value *build_q2_rerelease_players_checkpoint(const qa_q2_rerelease_players_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number((*in).version))) goto fail;
  if (!set_own(obj, "options", build_q2_rerelease_options(&(*in).options))) goto fail;
  if (!set_own(obj, "coopRestartTime", qa_save_value_number((*in).coop_restart_time))) goto fail;
  if (!set_own(obj, "intermissionFlags", qa_save_value_number((*in).intermission_flags))) goto fail;
  if (!set_own(obj, "intermissionFadeUntil", (*in).has_intermission_fade_until ? qa_save_value_number((*in).intermission_fade_until) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "intermissionCamera", (*in).has_intermission_camera ? build_q2_intermission_camera(&(*in).intermission_camera) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "intermissionCameraSet", qa_save_value_boolean((*in).intermission_camera_set))) goto fail;
  if (!set_own(obj, "deadlyKillBox", qa_save_value_boolean((*in).deadly_kill_box))) goto fail;
  { qa_save_value *arr11 = qa_save_value_array();
    if (arr11 == NULL) goto fail;
    for (size_t i12 = 0; i12 < (*in).players_count; i12++) {
      if (!push_own(arr11, build_q2_rerelease_player_entry(&(*in).players[i12]))) { qa_save_value_free(arr11); goto fail; }
    }
    if (!set_own(obj, "players", arr11)) goto fail; }
  { qa_save_value *arr13 = qa_save_value_array();
    if (arr13 == NULL) goto fail;
    for (size_t i14 = 0; i14 < (*in).squad_spawns_count; i14++) {
      if (!push_own(arr13, build_q2_squad_spawn(&(*in).squad_spawns[i14]))) { qa_save_value_free(arr13); goto fail; }
    }
    if (!set_own(obj, "squadSpawns", arr13)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rerelease_sky(const qa_save_reader *reader, qa_q2_rerelease_sky *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "name");
    (*out).name = qa_save_reader_string(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "rotation");
    (*out).rotation = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "autoRotate");
    (*out).auto_rotate = qa_save_reader_boolean(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "axis");
    (*out).axis = qa_read_vector(&f3);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_rerelease_sky(const qa_q2_rerelease_sky *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "name", str_value((*in).name))) goto fail;
  if (!set_own(obj, "rotation", qa_save_value_number((*in).rotation))) goto fail;
  if (!set_own(obj, "autoRotate", qa_save_value_boolean((*in).auto_rotate))) goto fail;
  if (!set_own(obj, "axis", vec_value(&(*in).axis))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rerelease_poi(const qa_save_reader *reader, qa_q2_rerelease_poi *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "origin");
    (*out).origin = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "image");
    (*out).image = qa_save_reader_string(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "dynamic");
    if (!qa_save_reader_is_null(&f3)) { (*out).has_dynamic = true;
      (*out).dynamic = read_actor(&f3);
    }
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_rerelease_poi(const qa_q2_rerelease_poi *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "origin", vec_value(&(*in).origin))) goto fail;
  if (!set_own(obj, "image", str_value((*in).image))) goto fail;
  if (!set_own(obj, "dynamic", (*in).has_dynamic ? actor_value(&(*in).dynamic) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rerelease_goals(const qa_save_reader *reader, qa_q2_rerelease_goals *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "goals");
    if (qa_save_reader_is_null(&f0)) (*out).goals = NULL; else {
      (*out).goals = qa_save_reader_string(&f0);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "goalNumber");
    (*out).goal_number = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_rerelease_goals(const qa_q2_rerelease_goals *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "goals", (*in).goals != NULL ? str_value((*in).goals) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "goalNumber", qa_save_value_number((*in).goal_number))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_campaign_mission(const qa_save_reader *reader, qa_q2_campaign_mission *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "primary");
    (*out).primary = qa_save_reader_string(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "secondary");
    (*out).secondary = qa_save_reader_string(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "primaryChanges");
    (*out).primary_changes = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "secondaryChanges");
    (*out).secondary_changes = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_campaign_mission(const qa_q2_campaign_mission *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "primary", str_value((*in).primary))) goto fail;
  if (!set_own(obj, "secondary", str_value((*in).secondary))) goto fail;
  if (!set_own(obj, "primaryChanges", qa_save_value_number((*in).primary_changes))) goto fail;
  if (!set_own(obj, "secondaryChanges", qa_save_value_number((*in).secondary_changes))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_campaign_level(const qa_save_reader *reader, qa_q2_campaign_level *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "map");
    (*out).map = qa_save_reader_string(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "name");
    (*out).name = qa_save_reader_string(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "visitOrder");
    (*out).visit_order = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "totalSecrets");
    (*out).total_secrets = qa_save_reader_number(&f3);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "foundSecrets");
    (*out).found_secrets = qa_save_reader_number(&f4);
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "totalMonsters");
    (*out).total_monsters = qa_save_reader_number(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "killedMonsters");
    (*out).killed_monsters = qa_save_reader_number(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "time");
    (*out).time = qa_save_reader_number(&f7);
    qa_save_reader_destroy(&f7); }
}
static qa_save_value *build_q2_campaign_level(const qa_q2_campaign_level *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "map", str_value((*in).map))) goto fail;
  if (!set_own(obj, "name", str_value((*in).name))) goto fail;
  if (!set_own(obj, "visitOrder", qa_save_value_number((*in).visit_order))) goto fail;
  if (!set_own(obj, "totalSecrets", qa_save_value_number((*in).total_secrets))) goto fail;
  if (!set_own(obj, "foundSecrets", qa_save_value_number((*in).found_secrets))) goto fail;
  if (!set_own(obj, "totalMonsters", qa_save_value_number((*in).total_monsters))) goto fail;
  if (!set_own(obj, "killedMonsters", qa_save_value_number((*in).killed_monsters))) goto fail;
  if (!set_own(obj, "time", qa_save_value_number((*in).time))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_campaign(const qa_save_reader *reader, qa_q2_campaign *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "mission");
    read_q2_campaign_mission(&f0, &(*out).mission);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "crossUnitFlags");
    (*out).cross_unit_flags = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "visitedMaps");
    (*out).visited_maps_count = qa_save_reader_list_count(&f2);
    (*out).visited_maps = (const char * *)calloc((*out).visited_maps_count != 0 ? (*out).visited_maps_count : 1, sizeof(const char *));
    if ((*out).visited_maps == NULL) qa_save_reader_fail(&f2, "out of memory");
    for (size_t i16 = 0; i16 < (*out).visited_maps_count; i16++) {
      qa_save_reader it17 = item(&f2, i16);
      (*out).visited_maps[i16] = qa_save_reader_string(&it17);
      qa_save_reader_destroy(&it17);
    }
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "levels");
    (*out).levels_count = qa_save_reader_list_count(&f3);
    (*out).levels = (qa_q2_campaign_level *)calloc((*out).levels_count != 0 ? (*out).levels_count : 1, sizeof(qa_q2_campaign_level));
    if ((*out).levels == NULL) qa_save_reader_fail(&f3, "out of memory");
    for (size_t i18 = 0; i18 < (*out).levels_count; i18++) {
      qa_save_reader it19 = item(&f3, i18);
      read_q2_campaign_level(&it19, &(*out).levels[i18]);
      qa_save_reader_destroy(&it19);
    }
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_campaign(const qa_q2_campaign *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "mission", build_q2_campaign_mission(&(*in).mission))) goto fail;
  if (!set_own(obj, "crossUnitFlags", qa_save_value_number((*in).cross_unit_flags))) goto fail;
  { qa_save_value *arr20 = qa_save_value_array();
    if (arr20 == NULL) goto fail;
    for (size_t i21 = 0; i21 < (*in).visited_maps_count; i21++) {
      if (!push_own(arr20, str_value((*in).visited_maps[i21]))) { qa_save_value_free(arr20); goto fail; }
    }
    if (!set_own(obj, "visitedMaps", arr20)) goto fail; }
  { qa_save_value *arr22 = qa_save_value_array();
    if (arr22 == NULL) goto fail;
    for (size_t i23 = 0; i23 < (*in).levels_count; i23++) {
      if (!push_own(arr22, build_q2_campaign_level(&(*in).levels[i23]))) { qa_save_value_free(arr22); goto fail; }
    }
    if (!set_own(obj, "levels", arr22)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_q64_eye_state(const qa_save_reader *reader, qa_q2_q64_eye_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "neutralAngles");
    (*out).neutral_angles = qa_read_vector(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "eyePosition");
    (*out).eye_position = qa_read_vector(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "visionCone");
    (*out).vision_cone = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_q64_eye_state(const qa_q2_q64_eye_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "neutralAngles", vec_value(&(*in).neutral_angles))) goto fail;
  if (!set_own(obj, "eyePosition", vec_value(&(*in).eye_position))) goto fail;
  if (!set_own(obj, "visionCone", qa_save_value_number((*in).vision_cone))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_q64_eye_entry(const qa_save_reader *reader, qa_q2_q64_eye_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_q64_eye_state(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_q64_eye_entry(const qa_q2_q64_eye_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_q64_eye_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_q64_camera_state(const qa_save_reader *reader, qa_q2_q64_camera_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "remaining");
    (*out).remaining = qa_save_reader_number(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "distance");
    (*out).distance = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "speed");
    (*out).speed = qa_save_reader_number(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "angles");
    (*out).angles = qa_read_vector(&f3);
    qa_save_reader_destroy(&f3); }
}
static qa_save_value *build_q2_q64_camera_state(const qa_q2_q64_camera_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "remaining", qa_save_value_number((*in).remaining))) goto fail;
  if (!set_own(obj, "distance", qa_save_value_number((*in).distance))) goto fail;
  if (!set_own(obj, "speed", qa_save_value_number((*in).speed))) goto fail;
  if (!set_own(obj, "angles", vec_value(&(*in).angles))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_q64_camera_entry(const qa_save_reader *reader, qa_q2_q64_camera_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_q64_camera_state(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_q64_camera_entry(const qa_q2_q64_camera_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_q64_camera_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_q64_dummy_state(const qa_save_reader *reader, qa_q2_q64_dummy_state *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "fadeRemaining");
    (*out).fade_remaining = qa_save_reader_number(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "fadeDuration");
    (*out).fade_duration = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "fading");
    (*out).fading = qa_save_reader_boolean(&f2);
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_q64_dummy_state(const qa_q2_q64_dummy_state *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "fadeRemaining", qa_save_value_number((*in).fade_remaining))) goto fail;
  if (!set_own(obj, "fadeDuration", qa_save_value_number((*in).fade_duration))) goto fail;
  if (!set_own(obj, "fading", qa_save_value_boolean((*in).fading))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_q64_dummy_entry(const qa_save_reader *reader, qa_q2_q64_dummy_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "state");
    read_q2_q64_dummy_state(&f1, &(*out).state);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_q64_dummy_entry(const qa_q2_q64_dummy_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "state", build_q2_q64_dummy_state(&(*in).state))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rerelease_q64_checkpoint(const qa_save_reader *reader, qa_q2_rerelease_q64_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "eyes");
    (*out).eyes_count = qa_save_reader_list_count(&f0);
    (*out).eyes = (qa_q2_q64_eye_entry *)calloc((*out).eyes_count != 0 ? (*out).eyes_count : 1, sizeof(qa_q2_q64_eye_entry));
    if ((*out).eyes == NULL) qa_save_reader_fail(&f0, "out of memory");
    for (size_t i24 = 0; i24 < (*out).eyes_count; i24++) {
      qa_save_reader it25 = item(&f0, i24);
      read_q2_q64_eye_entry(&it25, &(*out).eyes[i24]);
      qa_save_reader_destroy(&it25);
    }
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "cameras");
    (*out).cameras_count = qa_save_reader_list_count(&f1);
    (*out).cameras = (qa_q2_q64_camera_entry *)calloc((*out).cameras_count != 0 ? (*out).cameras_count : 1, sizeof(qa_q2_q64_camera_entry));
    if ((*out).cameras == NULL) qa_save_reader_fail(&f1, "out of memory");
    for (size_t i26 = 0; i26 < (*out).cameras_count; i26++) {
      qa_save_reader it27 = item(&f1, i26);
      read_q2_q64_camera_entry(&it27, &(*out).cameras[i26]);
      qa_save_reader_destroy(&it27);
    }
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "dummies");
    (*out).dummies_count = qa_save_reader_list_count(&f2);
    (*out).dummies = (qa_q2_q64_dummy_entry *)calloc((*out).dummies_count != 0 ? (*out).dummies_count : 1, sizeof(qa_q2_q64_dummy_entry));
    if ((*out).dummies == NULL) qa_save_reader_fail(&f2, "out of memory");
    for (size_t i28 = 0; i28 < (*out).dummies_count; i28++) {
      qa_save_reader it29 = item(&f2, i28);
      read_q2_q64_dummy_entry(&it29, &(*out).dummies[i28]);
      qa_save_reader_destroy(&it29);
    }
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_rerelease_q64_checkpoint(const qa_q2_rerelease_q64_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  { qa_save_value *arr30 = qa_save_value_array();
    if (arr30 == NULL) goto fail;
    for (size_t i31 = 0; i31 < (*in).eyes_count; i31++) {
      if (!push_own(arr30, build_q2_q64_eye_entry(&(*in).eyes[i31]))) { qa_save_value_free(arr30); goto fail; }
    }
    if (!set_own(obj, "eyes", arr30)) goto fail; }
  { qa_save_value *arr32 = qa_save_value_array();
    if (arr32 == NULL) goto fail;
    for (size_t i33 = 0; i33 < (*in).cameras_count; i33++) {
      if (!push_own(arr32, build_q2_q64_camera_entry(&(*in).cameras[i33]))) { qa_save_value_free(arr32); goto fail; }
    }
    if (!set_own(obj, "cameras", arr32)) goto fail; }
  { qa_save_value *arr34 = qa_save_value_array();
    if (arr34 == NULL) goto fail;
    for (size_t i35 = 0; i35 < (*in).dummies_count; i35++) {
      if (!push_own(arr34, build_q2_q64_dummy_entry(&(*in).dummies[i35]))) { qa_save_value_free(arr34); goto fail; }
    }
    if (!set_own(obj, "dummies", arr34)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_light_entry(const qa_save_reader *reader, qa_q2_light_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "active");
    (*out).active = qa_save_reader_boolean(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_light_entry(const qa_q2_light_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "active", qa_save_value_boolean((*in).active))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_health_bar(const qa_save_reader *reader, qa_q2_health_bar *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "controller");
    (*out).controller = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "target");
    (*out).target = read_actor(&f1);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "deadUntil");
    if (!qa_save_reader_is_null(&f2)) { (*out).has_dead_until = true;
      (*out).dead_until = qa_save_reader_number(&f2);
    }
    qa_save_reader_destroy(&f2); }
}
static qa_save_value *build_q2_health_bar(const qa_q2_health_bar *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "controller", actor_value(&(*in).controller))) goto fail;
  if (!set_own(obj, "target", actor_value(&(*in).target))) goto fail;
  if (!set_own(obj, "deadUntil", (*in).has_dead_until ? qa_save_value_number((*in).dead_until) : qa_save_value_null())) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_health_target_entry(const qa_save_reader *reader, qa_q2_health_target_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "controller");
    (*out).controller = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "target");
    (*out).target = read_actor(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_health_target_entry(const qa_q2_health_target_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "controller", actor_value(&(*in).controller))) goto fail;
  if (!set_own(obj, "target", actor_value(&(*in).target))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_picked_up_by_entry(const qa_save_reader *reader, qa_q2_picked_up_by_entry *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "slots");
    (*out).slots_count = qa_save_reader_list_count(&f1);
    (*out).slots = (double *)calloc((*out).slots_count != 0 ? (*out).slots_count : 1, sizeof(double));
    if ((*out).slots == NULL) qa_save_reader_fail(&f1, "out of memory");
    for (size_t i36 = 0; i36 < (*out).slots_count; i36++) {
      qa_save_reader it37 = item(&f1, i36);
      (*out).slots[i36] = qa_save_reader_integer_min(&it37, 0);
      qa_save_reader_destroy(&it37);
    }
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_picked_up_by_entry(const qa_q2_picked_up_by_entry *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  { qa_save_value *arr38 = qa_save_value_array();
    if (arr38 == NULL) goto fail;
    for (size_t i39 = 0; i39 < (*in).slots_count; i39++) {
      if (!push_own(arr38, qa_save_value_number((*in).slots[i39]))) { qa_save_value_free(arr38); goto fail; }
    }
    if (!set_own(obj, "slots", arr38)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_trigger_sound_time(const qa_save_reader *reader, qa_q2_trigger_sound_time *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "actor");
    (*out).actor = read_actor(&f0);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "time");
    (*out).time = qa_save_reader_number(&f1);
    qa_save_reader_destroy(&f1); }
}
static qa_save_value *build_q2_trigger_sound_time(const qa_q2_trigger_sound_time *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "actor", actor_value(&(*in).actor))) goto fail;
  if (!set_own(obj, "time", qa_save_value_number((*in).time))) goto fail;
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}

static void read_q2_rerelease_module_checkpoint(const qa_save_reader *reader, qa_q2_rerelease_module_checkpoint *out) {
  memset(out, 0, sizeof *out);
  { qa_save_reader f0 = field(reader, "version");
    (*out).version = qa_save_reader_literal_number(&f0, 1);
    qa_save_reader_destroy(&f0); }
  { qa_save_reader f1 = field(reader, "worldFog");
    read_q2_fog_state(&f1, &(*out).world_fog);
    qa_save_reader_destroy(&f1); }
  { qa_save_reader f2 = field(reader, "story");
    (*out).story = qa_save_reader_string(&f2);
    qa_save_reader_destroy(&f2); }
  { qa_save_reader f3 = field(reader, "sky");
    read_q2_rerelease_sky(&f3, &(*out).sky);
    qa_save_reader_destroy(&f3); }
  { qa_save_reader f4 = field(reader, "poi");
    if (!qa_save_reader_is_null(&f4)) { (*out).has_poi = true;
      read_q2_rerelease_poi(&f4, &(*out).poi);
    }
    qa_save_reader_destroy(&f4); }
  { qa_save_reader f5 = field(reader, "poiStage");
    (*out).poi_stage = qa_save_reader_number(&f5);
    qa_save_reader_destroy(&f5); }
  { qa_save_reader f6 = field(reader, "lastAutoSave");
    (*out).last_auto_save = qa_save_reader_number(&f6);
    qa_save_reader_destroy(&f6); }
  { qa_save_reader f7 = field(reader, "goals");
    read_q2_rerelease_goals(&f7, &(*out).goals);
    qa_save_reader_destroy(&f7); }
  { qa_save_reader f8 = field(reader, "campaign");
    read_q2_campaign(&f8, &(*out).campaign);
    qa_save_reader_destroy(&f8); }
  { qa_save_reader f9 = field(reader, "q64");
    read_q2_rerelease_q64_checkpoint(&f9, &(*out).q64);
    qa_save_reader_destroy(&f9); }
  { qa_save_reader f10 = field(reader, "lights");
    (*out).lights_count = qa_save_reader_list_count(&f10);
    (*out).lights = (qa_q2_light_entry *)calloc((*out).lights_count != 0 ? (*out).lights_count : 1, sizeof(qa_q2_light_entry));
    if ((*out).lights == NULL) qa_save_reader_fail(&f10, "out of memory");
    for (size_t i40 = 0; i40 < (*out).lights_count; i40++) {
      qa_save_reader it41 = item(&f10, i40);
      read_q2_light_entry(&it41, &(*out).lights[i40]);
      qa_save_reader_destroy(&it41);
    }
    qa_save_reader_destroy(&f10); }
  { qa_save_reader f11 = field(reader, "healthBars");
    (*out).health_bars_count = qa_save_reader_list_count(&f11);
    (*out).health_bars = (qa_q2_health_bar_opt *)calloc((*out).health_bars_count != 0 ? (*out).health_bars_count : 1, sizeof(qa_q2_health_bar_opt));
    if ((*out).health_bars == NULL) qa_save_reader_fail(&f11, "out of memory");
    for (size_t i42 = 0; i42 < (*out).health_bars_count; i42++) {
      qa_save_reader it43 = item(&f11, i42);
      if (!qa_save_reader_is_null(&it43)) { (*out).health_bars[i42].has = true;
        read_q2_health_bar(&it43, &(*out).health_bars[i42].value);
      }
      qa_save_reader_destroy(&it43);
    }
    qa_save_reader_destroy(&f11); }
  { qa_save_reader f12 = field(reader, "healthTargets");
    (*out).health_targets_count = qa_save_reader_list_count(&f12);
    (*out).health_targets = (qa_q2_health_target_entry *)calloc((*out).health_targets_count != 0 ? (*out).health_targets_count : 1, sizeof(qa_q2_health_target_entry));
    if ((*out).health_targets == NULL) qa_save_reader_fail(&f12, "out of memory");
    for (size_t i44 = 0; i44 < (*out).health_targets_count; i44++) {
      qa_save_reader it45 = item(&f12, i44);
      read_q2_health_target_entry(&it45, &(*out).health_targets[i44]);
      qa_save_reader_destroy(&it45);
    }
    qa_save_reader_destroy(&f12); }
  { qa_save_reader f13 = field(reader, "pickedUpBy");
    (*out).picked_up_by_count = qa_save_reader_list_count(&f13);
    (*out).picked_up_by = (qa_q2_picked_up_by_entry *)calloc((*out).picked_up_by_count != 0 ? (*out).picked_up_by_count : 1, sizeof(qa_q2_picked_up_by_entry));
    if ((*out).picked_up_by == NULL) qa_save_reader_fail(&f13, "out of memory");
    for (size_t i46 = 0; i46 < (*out).picked_up_by_count; i46++) {
      qa_save_reader it47 = item(&f13, i46);
      read_q2_picked_up_by_entry(&it47, &(*out).picked_up_by[i46]);
      qa_save_reader_destroy(&it47);
    }
    qa_save_reader_destroy(&f13); }
  { qa_save_reader f14 = field(reader, "triggerSoundTimes");
    (*out).trigger_sound_times_count = qa_save_reader_list_count(&f14);
    (*out).trigger_sound_times = (qa_q2_trigger_sound_time *)calloc((*out).trigger_sound_times_count != 0 ? (*out).trigger_sound_times_count : 1, sizeof(qa_q2_trigger_sound_time));
    if ((*out).trigger_sound_times == NULL) qa_save_reader_fail(&f14, "out of memory");
    for (size_t i48 = 0; i48 < (*out).trigger_sound_times_count; i48++) {
      qa_save_reader it49 = item(&f14, i48);
      read_q2_trigger_sound_time(&it49, &(*out).trigger_sound_times[i48]);
      qa_save_reader_destroy(&it49);
    }
    qa_save_reader_destroy(&f14); }
}
static qa_save_value *build_q2_rerelease_module_checkpoint(const qa_q2_rerelease_module_checkpoint *in) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "version", qa_save_value_number((*in).version))) goto fail;
  if (!set_own(obj, "worldFog", build_q2_fog_state(&(*in).world_fog))) goto fail;
  if (!set_own(obj, "story", str_value((*in).story))) goto fail;
  if (!set_own(obj, "sky", build_q2_rerelease_sky(&(*in).sky))) goto fail;
  if (!set_own(obj, "poi", (*in).has_poi ? build_q2_rerelease_poi(&(*in).poi) : qa_save_value_null())) goto fail;
  if (!set_own(obj, "poiStage", qa_save_value_number((*in).poi_stage))) goto fail;
  if (!set_own(obj, "lastAutoSave", qa_save_value_number((*in).last_auto_save))) goto fail;
  if (!set_own(obj, "goals", build_q2_rerelease_goals(&(*in).goals))) goto fail;
  if (!set_own(obj, "campaign", build_q2_campaign(&(*in).campaign))) goto fail;
  if (!set_own(obj, "q64", build_q2_rerelease_q64_checkpoint(&(*in).q64))) goto fail;
  { qa_save_value *arr50 = qa_save_value_array();
    if (arr50 == NULL) goto fail;
    for (size_t i51 = 0; i51 < (*in).lights_count; i51++) {
      if (!push_own(arr50, build_q2_light_entry(&(*in).lights[i51]))) { qa_save_value_free(arr50); goto fail; }
    }
    if (!set_own(obj, "lights", arr50)) goto fail; }
  { qa_save_value *arr52 = qa_save_value_array();
    if (arr52 == NULL) goto fail;
    for (size_t i53 = 0; i53 < (*in).health_bars_count; i53++) {
      if (!push_own(arr52, (*in).health_bars[i53].has ? build_q2_health_bar(&(*in).health_bars[i53].value) : qa_save_value_null())) { qa_save_value_free(arr52); goto fail; }
    }
    if (!set_own(obj, "healthBars", arr52)) goto fail; }
  { qa_save_value *arr54 = qa_save_value_array();
    if (arr54 == NULL) goto fail;
    for (size_t i55 = 0; i55 < (*in).health_targets_count; i55++) {
      if (!push_own(arr54, build_q2_health_target_entry(&(*in).health_targets[i55]))) { qa_save_value_free(arr54); goto fail; }
    }
    if (!set_own(obj, "healthTargets", arr54)) goto fail; }
  { qa_save_value *arr56 = qa_save_value_array();
    if (arr56 == NULL) goto fail;
    for (size_t i57 = 0; i57 < (*in).picked_up_by_count; i57++) {
      if (!push_own(arr56, build_q2_picked_up_by_entry(&(*in).picked_up_by[i57]))) { qa_save_value_free(arr56); goto fail; }
    }
    if (!set_own(obj, "pickedUpBy", arr56)) goto fail; }
  { qa_save_value *arr58 = qa_save_value_array();
    if (arr58 == NULL) goto fail;
    for (size_t i59 = 0; i59 < (*in).trigger_sound_times_count; i59++) {
      if (!push_own(arr58, build_q2_trigger_sound_time(&(*in).trigger_sound_times[i59]))) { qa_save_value_free(arr58); goto fail; }
    }
    if (!set_own(obj, "triggerSoundTimes", arr58)) goto fail; }
  return obj;
fail:
  qa_save_value_free(obj);
  return NULL;
}



qa_q2_fog_state qa_read_q2_fog_state(const qa_save_reader *reader) {
  qa_q2_fog_state out;
  read_q2_fog_state(reader, &out);
  return out;
}







qa_q2_rerelease_players_checkpoint qa_read_q2_rerelease_players_checkpoint(const qa_save_reader *reader) {
  qa_q2_rerelease_players_checkpoint out;
  read_q2_rerelease_players_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_rerelease_players_checkpoint(const qa_q2_rerelease_players_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_rerelease_players_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_rerelease_players_checkpoint(const uint8_t *bytes, size_t length, qa_q2_rerelease_players_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-rerelease-players")) { qa_save_value_free(root); return false; }
  read_q2_rerelease_players_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}













qa_q2_rerelease_q64_checkpoint qa_read_q2_rerelease_q64_checkpoint(const qa_save_reader *reader) {
  qa_q2_rerelease_q64_checkpoint out;
  read_q2_rerelease_q64_checkpoint(reader, &out);
  return out;
}






qa_q2_rerelease_module_checkpoint qa_read_q2_rerelease_module_checkpoint(const qa_save_reader *reader) {
  qa_q2_rerelease_module_checkpoint out;
  read_q2_rerelease_module_checkpoint(reader, &out);
  return out;
}
uint8_t *qa_encode_q2_rerelease_module_checkpoint(const qa_q2_rerelease_module_checkpoint *checkpoint, size_t *out_length) {
  qa_save_value *value = build_q2_rerelease_module_checkpoint(checkpoint);
  if (value == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(value, out_length);
  qa_save_value_free(value);
  return bytes;
}
bool qa_decode_q2_rerelease_module_checkpoint(const uint8_t *bytes, size_t length, qa_q2_rerelease_module_checkpoint *out) {
  qa_save_value *root = qa_decode_checkpoint_value(bytes, length);
  if (root == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "q2-rerelease-module")) { qa_save_value_free(root); return false; }
  read_q2_rerelease_module_checkpoint(&reader, out);
  qa_save_reader_destroy(&reader);
  out->root = root;
  return true;
}

