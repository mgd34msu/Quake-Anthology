/* Port of src/persistence/q2-rerelease-state.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_rerelease_state.h"

#include <stdio.h>
#include <string.h>

#include "q2_codec_internal.h"

static void copy_text(char *dst, size_t cap, const char *src) { (void)snprintf(dst, cap, "%s", src != NULL ? src : ""); }
static qa_save_value *number_array_value(const double *values, size_t count) {
  qa_save_value *array = qa_save_value_array();
  for (size_t i = 0; array != NULL && i < count; i++)
    if (!q2c_push(array, qa_save_value_number(values[i]))) { qa_save_value_free(array); return NULL; }
  return array;
}

/* ---- Q2FogState ---- */
qa_q2_fog_state qa_read_q2_fog_state(const qa_save_reader *reader) {
  qa_q2_fog_state out;
  memset(&out, 0, sizeof out);
  qa_save_reader fog = q2c_field(reader, "fog"), height = q2c_field(reader, "heightFog");
  out.fog.density = q2c_number(&fog, "density");
  out.fog.color = q2c_vector(&fog, "color");
  out.fog.sky_factor = q2c_number(&fog, "skyFactor");
  out.height_fog.start_color = q2c_vector(&height, "startColor");
  out.height_fog.start_distance = q2c_number(&height, "startDistance");
  out.height_fog.end_color = q2c_vector(&height, "endColor");
  out.height_fog.end_distance = q2c_number(&height, "endDistance");
  out.height_fog.falloff = q2c_number(&height, "falloff");
  out.height_fog.density = q2c_number(&height, "density");
  out.height_fog.density_first = false; /* the reader builds { ..., falloff, density } */
  qa_save_reader_destroy(&fog);
  qa_save_reader_destroy(&height);
  return out;
}
static qa_save_value *fog_value(const qa_q2_fog_state *in) {
  q2c_object fog = q2c_object_new();
  q2c_set(&fog, "density", qa_save_value_number(in->fog.density));
  q2c_set(&fog, "color", q2c_vector_value(in->fog.color));
  q2c_set(&fog, "skyFactor", qa_save_value_number(in->fog.sky_factor));
  q2c_object height = q2c_object_new();
  q2c_set(&height, "startColor", q2c_vector_value(in->height_fog.start_color));
  q2c_set(&height, "startDistance", qa_save_value_number(in->height_fog.start_distance));
  q2c_set(&height, "endColor", q2c_vector_value(in->height_fog.end_color));
  q2c_set(&height, "endDistance", qa_save_value_number(in->height_fog.end_distance));
  if (in->height_fog.density_first) {
    q2c_set(&height, "density", qa_save_value_number(in->height_fog.density));
    q2c_set(&height, "falloff", qa_save_value_number(in->height_fog.falloff));
  } else {
    q2c_set(&height, "falloff", qa_save_value_number(in->height_fog.falloff));
    q2c_set(&height, "density", qa_save_value_number(in->height_fog.density));
  }
  q2c_object o = q2c_object_new();
  q2c_set(&o, "fog", q2c_done(&fog));
  q2c_set(&o, "heightFog", q2c_done(&height));
  return q2c_done(&o);
}

/* ---- Q2RereleasePlayerState (class field order) ---- */
static const char *const COOP_STATES[] = {"none", "in-combat", "bad-area", "blocked", "waiting", "no-lives"};
static qa_q2rr_player_extra_state read_player(const qa_save_reader *reader, qa_persist_decoded *owner) {
  static const char *const auto_switch[] = {"0", "1", "2", "3"};
  (void)auto_switch;
  qa_q2rr_player_extra_state out;
  memset(&out, 0, sizeof out);
  out.spawned = q2c_boolean(reader, "spawned");
  out.game_help1_changed = q2c_number(reader, "gameHelp1Changed");
  out.game_help2_changed = q2c_number(reader, "gameHelp2Changed");
  out.help_changed = q2c_number(reader, "helpChanged");
  out.help_time = q2c_number(reader, "helpTime");
  out.seat = q2c_integer(reader, "seat", 0);
  copy_text(out.social_id, sizeof out.social_id, q2c_string(reader, "socialId"));
  /* field("autoSwitch").choice(0, 1, 2, 3) */
  qa_save_reader sw = q2c_field(reader, "autoSwitch");
  double value = qa_save_reader_number(&sw);
  if (value != 0 && value != 1 && value != 2 && value != 3) qa_save_reader_fail(&sw, "expected 0 or 1 or 2 or 3");
  qa_save_reader_destroy(&sw);
  out.auto_switch = value;
  out.auto_shield = q2c_number(reader, "autoShield");
  copy_text(out.dogtag, sizeof out.dogtag, q2c_string(reader, "dogtag"));
  out.slime_debounce = q2c_number(reader, "slimeDebounce");
  out.wind_sound_time = q2c_number(reader, "windSoundTime");
  out.animation_time = q2c_number(reader, "animationTime");
  out.flash_time = q2c_number(reader, "flashTime");
  out.flashes = q2c_number(reader, "flashes");
  out.last_damage_until = q2c_number(reader, "lastDamageUntil");
  out.last_firing_until = q2c_number(reader, "lastFiringUntil");
  out.lives = q2c_number(reader, "lives");
  qa_save_reader invisible = q2c_field(reader, "invisibilityUntil");
  out.invisibility_until = qa_save_reader_finite(&invisible);
  qa_save_reader_destroy(&invisible);
  qa_save_reader fade = q2c_field(reader, "invisibilityFadeUntil");
  out.invisibility_fade_until = qa_save_reader_finite(&fade);
  qa_save_reader_destroy(&fade);
  const char *coop = q2c_choice(reader, "coopRespawnState", COOP_STATES, 6);
  for (int i = 0; i < 6; i++) if (coop == COOP_STATES[i]) out.coop_respawn_state = (qa_q2_coop_respawn_state)i;
  out.flashlight = q2c_boolean(reader, "flashlight");
  qa_save_reader fog = q2c_field(reader, "fog");
  out.fog = qa_read_q2_fog_state(&fog);
  qa_save_reader_destroy(&fog);
  qa_save_reader wanted = q2c_field(reader, "wantedFog");
  out.wanted_fog = qa_read_q2_fog_state(&wanted);
  qa_save_reader_destroy(&wanted);
  out.fog_transition = q2c_number(reader, "fogTransition");
  out.bob_skip = q2c_boolean(reader, "bobSkip");
  out.impact_delta = q2c_number(reader, "impactDelta");
  out.on_ladder = q2c_boolean(reader, "onLadder");
  out.grapple_released_until = q2c_number(reader, "grappleReleasedUntil");
  out.grapple_attached = q2c_boolean(reader, "grappleAttached");
  out.awaiting_respawn = q2c_boolean(reader, "awaitingRespawn");
  out.respawn_timeout = q2c_number(reader, "respawnTimeout");
  qa_save_reader landmark = q2c_field(reader, "pendingLandmark");
  if (!qa_save_reader_is_null(&landmark)) {
    out.pending_landmark.has = true;
    out.pending_landmark.name = q2c_string(&landmark, "name");
    out.pending_landmark.relative_origin = q2c_vector(&landmark, "relativeOrigin");
    out.pending_landmark.relative_velocity = q2c_vector(&landmark, "relativeVelocity");
    out.pending_landmark.relative_view_angles = q2c_vector(&landmark, "relativeViewAngles");
  }
  qa_save_reader_destroy(&landmark);
  out.slow_view_angles = q2c_vector(reader, "slowViewAngles");
  out.quake_time = q2c_number(reader, "quakeTime");
  out.help_location = q2c_vector(reader, "helpLocation");
  copy_text(out.help_image, sizeof out.help_image, q2c_string(reader, "helpImage"));
  qa_save_reader points = q2c_field(reader, "helpPoints");
  out.help_point_count = qa_save_reader_list_count(&points);
  out.help_points = qa_persist_decoded_array(owner, out.help_point_count, sizeof *out.help_points);
  out.help_point_cap = out.help_point_count;
  for (size_t i = 0; i < out.help_point_count; i++) {
    qa_save_reader it = q2c_item(&points, i);
    out.help_points[i] = qa_read_vector(&it);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&points);
  out.help_index = q2c_number(reader, "helpIndex");
  out.help_draw_time = q2c_number(reader, "helpDrawTime");
  if (q2c_present(reader, "helpMarkerUntil")) {
    qa_save_reader marker = q2c_field(reader, "helpMarkerUntil");
    out.help_marker_until = qa_save_reader_finite(&marker);
    qa_save_reader_destroy(&marker);
  }
  return out;
}
static qa_save_value *player_value(const qa_q2rr_player_extra_state *in) {
  q2c_object o = q2c_object_new();
  q2c_set(&o, "seat", qa_save_value_number(in->seat));
  q2c_set(&o, "socialId", q2c_string_value(in->social_id));
  q2c_set(&o, "spawned", qa_save_value_boolean(in->spawned));
  q2c_set(&o, "gameHelp1Changed", qa_save_value_number(in->game_help1_changed));
  q2c_set(&o, "gameHelp2Changed", qa_save_value_number(in->game_help2_changed));
  q2c_set(&o, "helpChanged", qa_save_value_number(in->help_changed));
  q2c_set(&o, "helpTime", qa_save_value_number(in->help_time));
  q2c_set(&o, "invisibilityUntil", qa_save_value_number(in->invisibility_until));
  q2c_set(&o, "invisibilityFadeUntil", qa_save_value_number(in->invisibility_fade_until));
  q2c_set(&o, "slimeDebounce", qa_save_value_number(in->slime_debounce));
  q2c_set(&o, "animationTime", qa_save_value_number(in->animation_time));
  q2c_set(&o, "flashTime", qa_save_value_number(in->flash_time));
  q2c_set(&o, "flashes", qa_save_value_number(in->flashes));
  q2c_set(&o, "lastDamageUntil", qa_save_value_number(in->last_damage_until));
  q2c_set(&o, "lastFiringUntil", qa_save_value_number(in->last_firing_until));
  q2c_set(&o, "lives", qa_save_value_number(in->lives));
  q2c_set(&o, "coopRespawnState", qa_save_value_string(qa_q2_coop_respawn_state_name(in->coop_respawn_state)));
  q2c_set(&o, "flashlight", qa_save_value_boolean(in->flashlight));
  q2c_set(&o, "fog", fog_value(&in->fog));
  q2c_set(&o, "wantedFog", fog_value(&in->wanted_fog));
  q2c_set(&o, "fogTransition", qa_save_value_number(in->fog_transition));
  q2c_set(&o, "bobSkip", qa_save_value_boolean(in->bob_skip));
  q2c_set(&o, "autoSwitch", qa_save_value_number(in->auto_switch));
  q2c_set(&o, "autoShield", qa_save_value_number(in->auto_shield));
  q2c_set(&o, "dogtag", q2c_string_value(in->dogtag));
  q2c_set(&o, "impactDelta", qa_save_value_number(in->impact_delta));
  q2c_set(&o, "onLadder", qa_save_value_boolean(in->on_ladder));
  q2c_set(&o, "grappleReleasedUntil", qa_save_value_number(in->grapple_released_until));
  q2c_set(&o, "grappleAttached", qa_save_value_boolean(in->grapple_attached));
  q2c_set(&o, "slowViewAngles", q2c_vector_value(in->slow_view_angles));
  q2c_set(&o, "quakeTime", qa_save_value_number(in->quake_time));
  q2c_set(&o, "windSoundTime", qa_save_value_number(in->wind_sound_time));
  q2c_set(&o, "awaitingRespawn", qa_save_value_boolean(in->awaiting_respawn));
  q2c_set(&o, "respawnTimeout", qa_save_value_number(in->respawn_timeout));
  qa_save_value *landmark = qa_save_value_null();
  if (in->pending_landmark.has) {
    q2c_object l = q2c_object_new();
    q2c_set(&l, "name", q2c_string_value(in->pending_landmark.name));
    q2c_set(&l, "relativeOrigin", q2c_vector_value(in->pending_landmark.relative_origin));
    q2c_set(&l, "relativeVelocity", q2c_vector_value(in->pending_landmark.relative_velocity));
    q2c_set(&l, "relativeViewAngles", q2c_vector_value(in->pending_landmark.relative_view_angles));
    qa_save_value_free(landmark);
    landmark = q2c_done(&l);
  }
  q2c_set(&o, "pendingLandmark", landmark);
  q2c_set(&o, "helpLocation", q2c_vector_value(in->help_location));
  q2c_set(&o, "helpImage", q2c_string_value(in->help_image));
  qa_save_value *points = qa_save_value_array();
  for (size_t i = 0; points != NULL && i < in->help_point_count; i++)
    if (!q2c_push(points, q2c_vector_value(in->help_points[i]))) { qa_save_value_free(points); points = NULL; }
  q2c_set(&o, "helpPoints", points);
  q2c_set(&o, "helpIndex", qa_save_value_number(in->help_index));
  q2c_set(&o, "helpDrawTime", qa_save_value_number(in->help_draw_time));
  q2c_set(&o, "helpMarkerUntil", qa_save_value_number(in->help_marker_until));
  return q2c_done(&o);
}

/* ---- Q2RereleasePlayersCheckpoint (captureRerelease literal order) ---- */
qa_q2_rerelease_players_checkpoint qa_read_q2_rerelease_players_checkpoint(const qa_save_reader *reader,
                                                                          qa_persist_decoded *owner) {
  qa_q2_rerelease_players_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader version = q2c_field(reader, "version");
  out.version = qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader options = q2c_field(reader, "options");
  out.options.coop_squad_respawn = q2c_boolean(&options, "coopSquadRespawn");
  out.options.coop_instanced_items = q2c_boolean(&options, "coopInstancedItems");
  out.options.coop_lives = q2c_boolean(&options, "coopLives");
  out.options.coop_num_lives = q2c_number(&options, "coopNumLives");
  out.options.deathmatch_force_respawn = q2c_boolean(&options, "deathmatchForceRespawn");
  out.options.deathmatch_no_fall_damage = q2c_boolean(&options, "deathmatchNoFallDamage");
  out.options.deathmatch_allow_exit = q2c_boolean(&options, "deathmatchAllowExit");
  out.options.deathmatch_spawn_farthest = q2c_boolean(&options, "deathmatchSpawnFarthest");
  out.options.deathmatch_force_respawn_time = q2c_number(&options, "deathmatchForceRespawnTime");
  out.options.coop_player_collision = q2c_boolean(&options, "coopPlayerCollision");
  out.options.auto_save_minimum_time = q2c_number(&options, "autoSaveMinimumTime");
  qa_save_reader_destroy(&options);
  out.coop_restart_time = q2c_number(reader, "coopRestartTime");
  out.intermission_flags = q2c_number(reader, "intermissionFlags");
  qa_save_reader fade = q2c_field(reader, "intermissionFadeUntil");
  if (!qa_save_reader_is_null(&fade)) { out.has_intermission_fade_until = true; out.intermission_fade_until = qa_save_reader_number(&fade); }
  qa_save_reader_destroy(&fade);
  qa_save_reader camera = q2c_field(reader, "intermissionCamera");
  if (!qa_save_reader_is_null(&camera)) {
    out.has_intermission_camera = true;
    out.camera_origin = q2c_vector(&camera, "origin");
    out.camera_angles = q2c_vector(&camera, "angles");
  }
  qa_save_reader_destroy(&camera);
  out.intermission_camera_set = q2c_boolean(reader, "intermissionCameraSet");
  out.deadly_kill_box = q2c_boolean(reader, "deadlyKillBox");
  qa_save_reader players = q2c_field(reader, "players");
  out.player_count = qa_save_reader_list_count(&players);
  qa_q2rr_saved_player *saved = qa_persist_decoded_array(owner, out.player_count, sizeof *saved);
  for (size_t i = 0; i < out.player_count; i++) {
    qa_save_reader it = q2c_item(&players, i);
    saved[i].actor = q2c_actor(&it, "actor");
    qa_save_reader state = q2c_field(&it, "state");
    saved[i].state = read_player(&state, owner);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&players);
  out.players = saved;
  qa_save_reader squads = q2c_field(reader, "squadSpawns");
  out.squad_count = qa_save_reader_list_count(&squads);
  qa_q2rr_saved_placement *placements = qa_persist_decoded_array(owner, out.squad_count, sizeof *placements);
  for (size_t i = 0; i < out.squad_count; i++) {
    qa_save_reader it = q2c_item(&squads, i);
    placements[i].actor = q2c_actor(&it, "actor");
    placements[i].origin = q2c_vector(&it, "origin");
    placements[i].angles = q2c_vector(&it, "angles");
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&squads);
  out.squad_spawns = placements;
  return out;
}
static qa_save_value *rerelease_players_value(const qa_q2_rerelease_players_checkpoint *in) {
  const qa_q2_rerelease_options *options = &in->options;
  q2c_object opts = q2c_object_new();
  q2c_set(&opts, "coopSquadRespawn", qa_save_value_boolean(options->coop_squad_respawn));
  q2c_set(&opts, "coopInstancedItems", qa_save_value_boolean(options->coop_instanced_items));
  q2c_set(&opts, "coopLives", qa_save_value_boolean(options->coop_lives));
  q2c_set(&opts, "coopNumLives", qa_save_value_number(options->coop_num_lives));
  q2c_set(&opts, "deathmatchForceRespawn", qa_save_value_boolean(options->deathmatch_force_respawn));
  q2c_set(&opts, "deathmatchNoFallDamage", qa_save_value_boolean(options->deathmatch_no_fall_damage));
  q2c_set(&opts, "deathmatchSpawnFarthest", qa_save_value_boolean(options->deathmatch_spawn_farthest));
  q2c_set(&opts, "deathmatchForceRespawnTime", qa_save_value_number(options->deathmatch_force_respawn_time));
  q2c_set(&opts, "deathmatchAllowExit", qa_save_value_boolean(options->deathmatch_allow_exit));
  q2c_set(&opts, "coopPlayerCollision", qa_save_value_boolean(options->coop_player_collision));
  q2c_set(&opts, "autoSaveMinimumTime", qa_save_value_number(options->auto_save_minimum_time));
  q2c_object o = q2c_object_new();
  q2c_set(&o, "version", qa_save_value_number(in->version));
  q2c_set(&o, "options", q2c_done(&opts));
  q2c_set(&o, "coopRestartTime", qa_save_value_number(in->coop_restart_time));
  q2c_set(&o, "deadlyKillBox", qa_save_value_boolean(in->deadly_kill_box));
  q2c_set(&o, "intermissionFlags", qa_save_value_number(in->intermission_flags));
  q2c_set(&o, "intermissionFadeUntil", in->has_intermission_fade_until ? qa_save_value_number(in->intermission_fade_until) : qa_save_value_null());
  qa_save_value *camera = qa_save_value_null();
  if (in->has_intermission_camera) {
    q2c_object c = q2c_object_new();
    q2c_set(&c, "origin", q2c_vector_value(in->camera_origin));
    q2c_set(&c, "angles", q2c_vector_value(in->camera_angles));
    qa_save_value_free(camera);
    camera = q2c_done(&c);
  }
  q2c_set(&o, "intermissionCamera", camera);
  q2c_set(&o, "intermissionCameraSet", qa_save_value_boolean(in->intermission_camera_set));
  qa_save_value *players = qa_save_value_array();
  for (size_t i = 0; players != NULL && i < in->player_count; i++) {
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(in->players[i].actor));
    q2c_set(&entry, "state", player_value(&in->players[i].state));
    if (!q2c_push(players, q2c_done(&entry))) { qa_save_value_free(players); players = NULL; }
  }
  q2c_set(&o, "players", players);
  qa_save_value *squads = qa_save_value_array();
  for (size_t i = 0; squads != NULL && i < in->squad_count; i++) {
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(in->squad_spawns[i].actor));
    q2c_set(&entry, "origin", q2c_vector_value(in->squad_spawns[i].origin));
    q2c_set(&entry, "angles", q2c_vector_value(in->squad_spawns[i].angles));
    if (!q2c_push(squads, q2c_done(&entry))) { qa_save_value_free(squads); squads = NULL; }
  }
  q2c_set(&o, "squadSpawns", squads);
  return q2c_done(&o);
}
uint8_t *qa_encode_q2_rerelease_players_checkpoint(const qa_q2_rerelease_players_checkpoint *checkpoint, size_t *out_length) {
  return q2c_encode(rerelease_players_value(checkpoint), out_length);
}
bool qa_decode_q2_rerelease_players_checkpoint(const uint8_t *bytes, size_t length, qa_q2_rerelease_players_checkpoint *out,
                                               qa_persist_decoded *owner) {
  qa_save_reader reader;
  if (!q2c_open(bytes, length, "q2-rerelease-players", owner, &reader)) return false;
  *out = qa_read_q2_rerelease_players_checkpoint(&reader, owner);
  qa_save_reader_destroy(&reader);
  return true;
}

/* ---- Q2RereleaseQ64Checkpoint ---- */
qa_q2_rerelease_q64_checkpoint qa_read_q2_rerelease_q64_checkpoint(const qa_save_reader *reader, qa_persist_decoded *owner) {
  qa_q2_rerelease_q64_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader eyes = q2c_field(reader, "eyes");
  out.eye_count = qa_save_reader_list_count(&eyes);
  qa_q2rr_eye_entry *eye = qa_persist_decoded_array(owner, out.eye_count, sizeof *eye);
  for (size_t i = 0; i < out.eye_count; i++) {
    qa_save_reader it = q2c_item(&eyes, i), state = q2c_field(&it, "state");
    eye[i].actor = q2c_actor(&it, "actor");
    eye[i].state.neutral_angles = q2c_vector(&state, "neutralAngles");
    eye[i].state.eye_position = q2c_vector(&state, "eyePosition");
    eye[i].state.vision_cone = q2c_number(&state, "visionCone");
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&eyes);
  out.eyes = eye;
  qa_save_reader cameras = q2c_field(reader, "cameras");
  out.camera_count = qa_save_reader_list_count(&cameras);
  qa_q2rr_camera_entry *camera = qa_persist_decoded_array(owner, out.camera_count, sizeof *camera);
  for (size_t i = 0; i < out.camera_count; i++) {
    qa_save_reader it = q2c_item(&cameras, i), state = q2c_field(&it, "state");
    camera[i].actor = q2c_actor(&it, "actor");
    camera[i].state.remaining = q2c_number(&state, "remaining");
    camera[i].state.distance = q2c_number(&state, "distance");
    camera[i].state.speed = q2c_number(&state, "speed");
    camera[i].state.angles = q2c_vector(&state, "angles");
    camera[i].state.reader_order = true;
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&cameras);
  out.cameras = camera;
  qa_save_reader dummies = q2c_field(reader, "dummies");
  out.dummy_count = qa_save_reader_list_count(&dummies);
  qa_q2rr_dummy_entry *dummy = qa_persist_decoded_array(owner, out.dummy_count, sizeof *dummy);
  for (size_t i = 0; i < out.dummy_count; i++) {
    qa_save_reader it = q2c_item(&dummies, i), state = q2c_field(&it, "state");
    dummy[i].actor = q2c_actor(&it, "actor");
    dummy[i].state.fade_remaining = q2c_number(&state, "fadeRemaining");
    dummy[i].state.fade_duration = q2c_number(&state, "fadeDuration");
    dummy[i].state.fading = q2c_boolean(&state, "fading");
    dummy[i].state.reader_order = true;
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&dummies);
  out.dummies = dummy;
  return out;
}
static qa_save_value *q64_value(const qa_q2_rerelease_q64_checkpoint *in) {
  qa_save_value *eyes = qa_save_value_array();
  for (size_t i = 0; eyes != NULL && i < in->eye_count; i++) {
    q2c_object state = q2c_object_new();
    q2c_set(&state, "neutralAngles", q2c_vector_value(in->eyes[i].state.neutral_angles));
    q2c_set(&state, "eyePosition", q2c_vector_value(in->eyes[i].state.eye_position));
    q2c_set(&state, "visionCone", qa_save_value_number(in->eyes[i].state.vision_cone));
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(in->eyes[i].actor));
    q2c_set(&entry, "state", q2c_done(&state));
    if (!q2c_push(eyes, q2c_done(&entry))) { qa_save_value_free(eyes); eyes = NULL; }
  }
  qa_save_value *cameras = qa_save_value_array();
  for (size_t i = 0; cameras != NULL && i < in->camera_count; i++) {
    const qa_q2rr_camera_state *s = &in->cameras[i].state;
    q2c_object state = q2c_object_new();
    if (s->reader_order) {
      q2c_set(&state, "remaining", qa_save_value_number(s->remaining));
      q2c_set(&state, "distance", qa_save_value_number(s->distance));
      q2c_set(&state, "speed", qa_save_value_number(s->speed));
    } else {
      q2c_set(&state, "speed", qa_save_value_number(s->speed));
      q2c_set(&state, "distance", qa_save_value_number(s->distance));
      q2c_set(&state, "remaining", qa_save_value_number(s->remaining));
    }
    q2c_set(&state, "angles", q2c_vector_value(s->angles));
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(in->cameras[i].actor));
    q2c_set(&entry, "state", q2c_done(&state));
    if (!q2c_push(cameras, q2c_done(&entry))) { qa_save_value_free(cameras); cameras = NULL; }
  }
  qa_save_value *dummies = qa_save_value_array();
  for (size_t i = 0; dummies != NULL && i < in->dummy_count; i++) {
    const qa_q2rr_dummy_state *s = &in->dummies[i].state;
    q2c_object state = q2c_object_new();
    if (s->reader_order) {
      q2c_set(&state, "fadeRemaining", qa_save_value_number(s->fade_remaining));
      q2c_set(&state, "fadeDuration", qa_save_value_number(s->fade_duration));
      q2c_set(&state, "fading", qa_save_value_boolean(s->fading));
    } else {
      q2c_set(&state, "fading", qa_save_value_boolean(s->fading));
      q2c_set(&state, "fadeDuration", qa_save_value_number(s->fade_duration));
      q2c_set(&state, "fadeRemaining", qa_save_value_number(s->fade_remaining));
    }
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "actor", q2c_actor_value(in->dummies[i].actor));
    q2c_set(&entry, "state", q2c_done(&state));
    if (!q2c_push(dummies, q2c_done(&entry))) { qa_save_value_free(dummies); dummies = NULL; }
  }
  q2c_object o = q2c_object_new();
  q2c_set(&o, "eyes", eyes);
  q2c_set(&o, "cameras", cameras);
  q2c_set(&o, "dummies", dummies);
  return q2c_done(&o);
}

/* ---- Q2RereleaseModuleCheckpoint (Q2RereleaseModule.capture literal order) ---- */
qa_q2_rerelease_module_checkpoint qa_read_q2_rerelease_module_checkpoint(const qa_save_reader *reader,
                                                                        qa_persist_decoded *owner) {
  qa_q2_rerelease_module_checkpoint out;
  memset(&out, 0, sizeof out);
  qa_save_reader version = q2c_field(reader, "version");
  out.version = qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader fog = q2c_field(reader, "worldFog");
  out.world_fog = qa_read_q2_fog_state(&fog);
  qa_save_reader_destroy(&fog);
  out.story = q2c_string(reader, "story");
  qa_save_reader sky = q2c_field(reader, "sky");
  out.sky.name = q2c_string(&sky, "name");
  out.sky.rotation = q2c_number(&sky, "rotation");
  out.sky.auto_rotate = q2c_boolean(&sky, "autoRotate");
  out.sky.axis = q2c_vector(&sky, "axis");
  qa_save_reader_destroy(&sky);
  qa_save_reader poi = q2c_field(reader, "poi");
  if (!qa_save_reader_is_null(&poi)) {
    out.has_poi = true;
    out.poi.actor = q2c_actor(&poi, "actor");
    out.poi.origin = q2c_vector(&poi, "origin");
    out.poi.image = q2c_string(&poi, "image");
    out.poi.has_dynamic = q2c_nullable_actor(&poi, "dynamic", &out.poi.dynamic);
  }
  qa_save_reader_destroy(&poi);
  out.poi_stage = q2c_number(reader, "poiStage");
  out.last_auto_save = q2c_number(reader, "lastAutoSave");
  qa_save_reader goals = q2c_field(reader, "goals");
  out.goals.goals = q2c_is_null(&goals, "goals") ? NULL : q2c_string(&goals, "goals");
  out.goals.goal_number = q2c_number(&goals, "goalNumber");
  qa_save_reader_destroy(&goals);
  qa_save_reader campaign = q2c_field(reader, "campaign"), mission = q2c_field(&campaign, "mission");
  out.campaign.mission.primary = (char *)q2c_string(&mission, "primary");
  out.campaign.mission.secondary = (char *)q2c_string(&mission, "secondary");
  out.campaign.mission.primary_changes = q2c_number(&mission, "primaryChanges");
  out.campaign.mission.secondary_changes = q2c_number(&mission, "secondaryChanges");
  qa_save_reader_destroy(&mission);
  out.campaign.cross_unit_flags = q2c_number(&campaign, "crossUnitFlags");
  qa_save_reader visited = q2c_field(&campaign, "visitedMaps");
  out.campaign.visited_count = qa_save_reader_list_count(&visited);
  const char **maps = qa_persist_decoded_array(owner, out.campaign.visited_count, sizeof *maps);
  for (size_t i = 0; i < out.campaign.visited_count; i++) {
    qa_save_reader it = q2c_item(&visited, i);
    maps[i] = qa_save_reader_string(&it);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&visited);
  out.campaign.visited_maps = maps;
  qa_save_reader levels = q2c_field(&campaign, "levels");
  out.campaign.level_count = qa_save_reader_list_count(&levels);
  qa_q2_rerelease_level_entry *level = qa_persist_decoded_array(owner, out.campaign.level_count, sizeof *level);
  for (size_t i = 0; i < out.campaign.level_count; i++) {
    qa_save_reader it = q2c_item(&levels, i);
    level[i].map = (char *)q2c_string(&it, "map");
    level[i].name = (char *)q2c_string(&it, "name");
    level[i].visit_order = q2c_number(&it, "visitOrder");
    level[i].total_secrets = q2c_number(&it, "totalSecrets");
    level[i].found_secrets = q2c_number(&it, "foundSecrets");
    level[i].total_monsters = q2c_number(&it, "totalMonsters");
    level[i].killed_monsters = q2c_number(&it, "killedMonsters");
    level[i].time = q2c_number(&it, "time");
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&levels);
  out.campaign.levels = level;
  qa_save_reader_destroy(&campaign);
  qa_save_reader q64 = q2c_field(reader, "q64");
  out.q64 = qa_read_q2_rerelease_q64_checkpoint(&q64, owner);
  qa_save_reader_destroy(&q64);
  qa_save_reader lights = q2c_field(reader, "lights");
  out.light_count = qa_save_reader_list_count(&lights);
  qa_q2rr_saved_light *light = qa_persist_decoded_array(owner, out.light_count, sizeof *light);
  for (size_t i = 0; i < out.light_count; i++) {
    qa_save_reader it = q2c_item(&lights, i);
    light[i].actor = q2c_actor(&it, "actor");
    light[i].active = q2c_boolean(&it, "active");
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&lights);
  out.lights = light;
  qa_save_reader bars = q2c_field(reader, "healthBars");
  out.health_bar_count = qa_save_reader_list_count(&bars);
  qa_q2rr_saved_healthbar *bar = qa_persist_decoded_array(owner, out.health_bar_count, sizeof *bar);
  for (size_t i = 0; i < out.health_bar_count; i++) {
    qa_save_reader it = q2c_item(&bars, i);
    if (!qa_save_reader_is_null(&it)) {
      bar[i].has = true;
      bar[i].controller = q2c_actor(&it, "controller");
      bar[i].target = q2c_actor(&it, "target");
      qa_save_reader dead = q2c_field(&it, "deadUntil");
      if (!qa_save_reader_is_null(&dead)) { bar[i].has_dead_until = true; bar[i].dead_until = qa_save_reader_number(&dead); }
      qa_save_reader_destroy(&dead);
    }
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&bars);
  out.health_bars = bar;
  qa_save_reader targets = q2c_field(reader, "healthTargets");
  out.health_target_count = qa_save_reader_list_count(&targets);
  qa_q2rr_saved_pair *pair = qa_persist_decoded_array(owner, out.health_target_count, sizeof *pair);
  for (size_t i = 0; i < out.health_target_count; i++) {
    qa_save_reader it = q2c_item(&targets, i);
    pair[i].controller = q2c_actor(&it, "controller");
    pair[i].target = q2c_actor(&it, "target");
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&targets);
  out.health_targets = pair;
  qa_save_reader picked = q2c_field(reader, "pickedUpBy");
  out.picked_count = qa_save_reader_list_count(&picked);
  qa_q2rr_saved_slots *slots = qa_persist_decoded_array(owner, out.picked_count, sizeof *slots);
  for (size_t i = 0; i < out.picked_count; i++) {
    qa_save_reader it = q2c_item(&picked, i), list = q2c_field(&it, "slots");
    slots[i].actor = q2c_actor(&it, "actor");
    slots[i].slot_count = qa_save_reader_list_count(&list);
    double *values = qa_persist_decoded_array(owner, slots[i].slot_count, sizeof *values);
    for (size_t j = 0; j < slots[i].slot_count; j++) {
      qa_save_reader slot = q2c_item(&list, j);
      values[j] = qa_save_reader_integer_min(&slot, 0);
      qa_save_reader_destroy(&slot);
    }
    slots[i].slots = values;
    qa_save_reader_destroy(&list);
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&picked);
  out.picked_up_by = slots;
  qa_save_reader sounds = q2c_field(reader, "triggerSoundTimes");
  out.trigger_sound_count = qa_save_reader_list_count(&sounds);
  qa_q2rr_saved_time *time = qa_persist_decoded_array(owner, out.trigger_sound_count, sizeof *time);
  for (size_t i = 0; i < out.trigger_sound_count; i++) {
    qa_save_reader it = q2c_item(&sounds, i);
    time[i].actor = q2c_actor(&it, "actor");
    time[i].time = q2c_number(&it, "time");
    qa_save_reader_destroy(&it);
  }
  qa_save_reader_destroy(&sounds);
  out.trigger_sound_times = time;
  return out;
}
static qa_save_value *module_value(const qa_q2_rerelease_module_checkpoint *in) {
  q2c_object o = q2c_object_new();
  q2c_set(&o, "version", qa_save_value_number(in->version));
  q2c_set(&o, "worldFog", fog_value(&in->world_fog));
  q2c_set(&o, "story", q2c_string_value(in->story));
  q2c_object sky = q2c_object_new();
  q2c_set(&sky, "name", q2c_string_value(in->sky.name));
  q2c_set(&sky, "rotation", qa_save_value_number(in->sky.rotation));
  q2c_set(&sky, "autoRotate", qa_save_value_boolean(in->sky.auto_rotate));
  q2c_set(&sky, "axis", q2c_vector_value(in->sky.axis));
  q2c_set(&o, "sky", q2c_done(&sky));
  qa_save_value *poi = qa_save_value_null();
  if (in->has_poi) {
    q2c_object p = q2c_object_new();
    q2c_set(&p, "actor", q2c_actor_value(in->poi.actor));
    q2c_set(&p, "origin", q2c_vector_value(in->poi.origin));
    q2c_set(&p, "image", q2c_string_value(in->poi.image));
    q2c_set(&p, "dynamic", q2c_nullable_actor_value(in->poi.has_dynamic, in->poi.dynamic));
    qa_save_value_free(poi);
    poi = q2c_done(&p);
  }
  q2c_set(&o, "poi", poi);
  q2c_set(&o, "poiStage", qa_save_value_number(in->poi_stage));
  q2c_set(&o, "lastAutoSave", qa_save_value_number(in->last_auto_save));
  q2c_object campaign = q2c_object_new();
  q2c_set(&campaign, "crossUnitFlags", qa_save_value_number(in->campaign.cross_unit_flags));
  qa_save_value *visited = qa_save_value_array();
  for (size_t i = 0; visited != NULL && i < in->campaign.visited_count; i++)
    if (!q2c_push(visited, q2c_string_value(in->campaign.visited_maps[i]))) { qa_save_value_free(visited); visited = NULL; }
  q2c_set(&campaign, "visitedMaps", visited);
  qa_save_value *levels = qa_save_value_array();
  for (size_t i = 0; levels != NULL && i < in->campaign.level_count; i++) {
    const qa_q2_rerelease_level_entry *level = &in->campaign.levels[i];
    q2c_object entry = q2c_object_new();
    q2c_set(&entry, "map", q2c_string_value(level->map));
    q2c_set(&entry, "name", q2c_string_value(level->name));
    q2c_set(&entry, "visitOrder", qa_save_value_number(level->visit_order));
    q2c_set(&entry, "totalSecrets", qa_save_value_number(level->total_secrets));
    q2c_set(&entry, "foundSecrets", qa_save_value_number(level->found_secrets));
    q2c_set(&entry, "totalMonsters", qa_save_value_number(level->total_monsters));
    q2c_set(&entry, "killedMonsters", qa_save_value_number(level->killed_monsters));
    q2c_set(&entry, "time", qa_save_value_number(level->time));
    if (!q2c_push(levels, q2c_done(&entry))) { qa_save_value_free(levels); levels = NULL; }
  }
  q2c_set(&campaign, "levels", levels);
  q2c_object mission = q2c_object_new();
  q2c_set(&mission, "primary", q2c_string_value(in->campaign.mission.primary));
  q2c_set(&mission, "secondary", q2c_string_value(in->campaign.mission.secondary));
  q2c_set(&mission, "primaryChanges", qa_save_value_number(in->campaign.mission.primary_changes));
  q2c_set(&mission, "secondaryChanges", qa_save_value_number(in->campaign.mission.secondary_changes));
  q2c_set(&campaign, "mission", q2c_done(&mission));
  q2c_set(&o, "campaign", q2c_done(&campaign));
  qa_save_value *bars = qa_save_value_array();
  for (size_t i = 0; bars != NULL && i < in->health_bar_count; i++) {
    const qa_q2rr_saved_healthbar *bar = &in->health_bars[i];
    qa_save_value *value = qa_save_value_null();
    if (bar->has) {
      q2c_object b = q2c_object_new();
      q2c_set(&b, "controller", q2c_actor_value(bar->controller));
      q2c_set(&b, "target", q2c_actor_value(bar->target));
      q2c_set(&b, "deadUntil", bar->has_dead_until ? qa_save_value_number(bar->dead_until) : qa_save_value_null());
      qa_save_value_free(value);
      value = q2c_done(&b);
    }
    if (!q2c_push(bars, value)) { qa_save_value_free(bars); bars = NULL; }
  }
  q2c_set(&o, "healthBars", bars);
  qa_save_value *targets = qa_save_value_array();
  for (size_t i = 0; targets != NULL && i < in->health_target_count; i++) {
    q2c_object t = q2c_object_new();
    q2c_set(&t, "controller", q2c_actor_value(in->health_targets[i].controller));
    q2c_set(&t, "target", q2c_actor_value(in->health_targets[i].target));
    if (!q2c_push(targets, q2c_done(&t))) { qa_save_value_free(targets); targets = NULL; }
  }
  q2c_set(&o, "healthTargets", targets);
  qa_save_value *picked = qa_save_value_array();
  for (size_t i = 0; picked != NULL && i < in->picked_count; i++) {
    q2c_object p = q2c_object_new();
    q2c_set(&p, "actor", q2c_actor_value(in->picked_up_by[i].actor));
    q2c_set(&p, "slots", number_array_value(in->picked_up_by[i].slots, in->picked_up_by[i].slot_count));
    if (!q2c_push(picked, q2c_done(&p))) { qa_save_value_free(picked); picked = NULL; }
  }
  q2c_set(&o, "pickedUpBy", picked);
  qa_save_value *sounds = qa_save_value_array();
  for (size_t i = 0; sounds != NULL && i < in->trigger_sound_count; i++) {
    q2c_object t = q2c_object_new();
    q2c_set(&t, "actor", q2c_actor_value(in->trigger_sound_times[i].actor));
    q2c_set(&t, "time", qa_save_value_number(in->trigger_sound_times[i].time));
    if (!q2c_push(sounds, q2c_done(&t))) { qa_save_value_free(sounds); sounds = NULL; }
  }
  q2c_set(&o, "triggerSoundTimes", sounds);
  q2c_set(&o, "q64", q64_value(&in->q64));
  qa_save_value *lights = qa_save_value_array();
  for (size_t i = 0; lights != NULL && i < in->light_count; i++) {
    q2c_object l = q2c_object_new();
    q2c_set(&l, "actor", q2c_actor_value(in->lights[i].actor));
    q2c_set(&l, "active", qa_save_value_boolean(in->lights[i].active));
    if (!q2c_push(lights, q2c_done(&l))) { qa_save_value_free(lights); lights = NULL; }
  }
  q2c_set(&o, "lights", lights);
  q2c_object goals = q2c_object_new();
  q2c_set(&goals, "goals", in->goals.goals != NULL ? qa_save_value_string(in->goals.goals) : qa_save_value_null());
  q2c_set(&goals, "goalNumber", qa_save_value_number(in->goals.goal_number));
  q2c_set(&o, "goals", q2c_done(&goals));
  return q2c_done(&o);
}
uint8_t *qa_encode_q2_rerelease_module_checkpoint(const qa_q2_rerelease_module_checkpoint *checkpoint, size_t *out_length) {
  return q2c_encode(module_value(checkpoint), out_length);
}
bool qa_decode_q2_rerelease_module_checkpoint(const uint8_t *bytes, size_t length, qa_q2_rerelease_module_checkpoint *out,
                                              qa_persist_decoded *owner) {
  qa_save_reader reader;
  if (!q2c_open(bytes, length, "q2-rerelease-module", owner, &reader)) return false;
  *out = qa_read_q2_rerelease_module_checkpoint(&reader, owner);
  qa_save_reader_destroy(&reader);
  return true;
}
