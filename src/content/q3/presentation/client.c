/* Port of src/content/q3/presentation/client.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/client.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/presentation/draw_status.h"
#include "qa/content/q3/presentation/draw_tools.h"
#include "qa/content/q3/presentation/effects.h"
#include "qa/content/q3/presentation/entities.h"
#include "qa/content/q3/presentation/events.h"
#include "qa/content/q3/presentation/frame_audio.h"
#include "qa/content/q3/presentation/hud.h"
#include "qa/content/q3/presentation/hud_corners.h"
#include "qa/content/q3/presentation/local_entities.h"
#include "qa/content/q3/presentation/marks.h"
#include "qa/content/q3/presentation/players.h"
#include "qa/content/q3/presentation/prediction.h"
#include "qa/content/q3/presentation/server_commands.h"
#include "qa/content/q3/presentation/snapshots.h"
#include "qa/content/q3/presentation/ui_adapters.h"
#include "qa/content/q3/presentation/view.h"
#include "qa/content/q3/presentation/weapons.h"
#include "qa/core/common_error.h"
#include "qa/core/game_numeric.h"
#include "qa/core/info_string.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/particles/q3_system.h"
#define PRIMARY_ONLY_ERROR "Supplemental cgame scene cannot take over primary input, UI or movement"
enum { CONFIG_STRING_COUNT = 1024, WEAPON_SCRATCH = 16 };
typedef qa_q3_pres_client_presentation pres;
struct qa_q3_pres_client_presentation {
  qa_q3_pres_presentation_common common;
  qa_q3_pres_client_presentation_options primary;
  qa_q3_pres_scene_presentation_options scene_options;
  bool scene_only, closed;
  double initial_message_sequence;
  /* TS FrameContext */
  bool loading_context, demo_playback;
  double engine_frame_number;
  qa_game_random random;
  char *strings[CONFIG_STRING_COUNT];
  size_t string_count;
  qa_q3_pres_client_game_state state;
  qa_q3_pres_client_game_static_state static_state;
  bool state_ready, static_ready;
  qa_q3_pres_client_info_store *clients;
  qa_q3_pres_client_configuration *configuration;
  qa_q3_pres_client_weapon_media_registry *weapon_registry;
  qa_q3_pres_packet_weapon_info *weapon_scratch;
  qa_q3_pres_client_media media;
  bool media_ready;
  qa_q3_pres_client_draw_tools tools;
  qa_q3_pres_client_draw_icons icons;
  qa_q3_pres_client_loading_screen loading;
  qa_q3_pres_client_server_command_runtime *server_commands;
  qa_q3_pres_snapshot_runtime snapshots;
  bool snapshots_ready;
  qa_q3_pres_prediction_runtime prediction;
  qa_q3_base_predictable_event_debug event_debug;
  qa_q3_pres_view_runtime view;
  qa_q3_pres_engine_ui_model_painter model_painter;
  qa_q3_pres_mission_hud menus;
  bool has_menus;
  qa_q3_pres_client_draw_status status;
  bool has_status;
  qa_q3_pres_client_frame_audio frame_audio;
  qa_q3_pres_client_weapon_selection console_weapons;
  qa_q3_pres_client_console_runtime console;
  bool has_console;
  qa_q3_pres_registered_client_graphics registered;
  bool graphics_registered;
  qa_q3_pres_local_entity_pool *pool;
  qa_q3_pres_client_effects effects;
  qa_q3_pres_bsp_mark_projector projector;
  qa_q3_pres_impact_mark_system *marks;
  qa_particle_system particles;
  bool particles_ready;
  qa_q3_pres_mission_local_entity_media local_media;
  qa_q3_pres_local_entity_system local_entities;
  qa_q3_pres_client_weapon_runtime weapons;
  qa_q3_pres_mission_player_media mission_player_media;
  qa_q3_pres_player_presenter players;
  qa_q3_pres_packet_entity_media packet_media;
  qa_q3_pres_packet_entity_presenter packet;
  qa_q3_pres_client_event_runtime events;
  qa_q3_pres_player_state_runtime player_state;
  qa_q3_pres_client_hud_corners corners;
  qa_q3_pres_base_scoreboard scoreboard;
  qa_q3_pres_client_hud hud;
  bool has_hud;
  qa_q3_pres_presentation_frame_runtime frames;
  /* Owned text behind borrowed TS string results. */
  char *settings_model, *settings_head, *settings_red, *settings_blue, *player_name, *console_cvar;
  const qa_q3_pres_scene_shader *status_shader;
};
static void oom(void) { qa_range_error("Cgame presentation allocation failed"); }
static const char *owned_text(char **slot, const char *value) {
  char *copy = strdup(value);
  if (!copy) oom();
  free(*slot);
  *slot = copy;
  return copy;
}
/* ---- configuration readers ---- */
/* TS VM_SYMBOLS lookup: symbol names first (they differ from engine names, as
 * cg_buildScript over com_buildScript), then the configuration's cvar names. */
static qa_cvar_snapshot read_vm(pres *p, const char *name) {
  qa_cvar_snapshot snapshot;
  for (int symbol = 0; symbol < QA_Q3_PRES_VM_SYMBOL_COUNT; symbol++) {
    if (strcmp(qa_q3_pres_client_vm_cvar_symbol_name((qa_q3_pres_client_vm_cvar_symbol)symbol), name) != 0) continue;
    if (!qa_q3_pres_client_configuration_read_vm_symbol(p->configuration, (qa_q3_pres_client_vm_cvar_symbol)symbol, &snapshot)) oom();
    return snapshot;
  }
  if (!qa_q3_pres_client_configuration_read_vm_cvar(p->configuration, name, &snapshot)) oom();
  return snapshot;
}
static double integer(pres *p, const char *name) {
  qa_cvar_snapshot snapshot = read_vm(p, name);
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
static double numeric(pres *p, const char *name) {
  qa_cvar_snapshot snapshot = read_vm(p, name);
  double value = snapshot.numeric_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
static bool enabled(pres *p, const char *name) { return integer(p, name) != 0; }
static bool mission_enabled(pres *p, const char *name) {
  return p->common.session.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && enabled(p, name);
}
static const char *config_string(void *ctx, double index) {
  pres *p = ctx;
  if (index == trunc(index) && (index < 0 || index >= CONFIG_STRING_COUNT)) {
    char message[64];
    snprintf(message, sizeof message, "CG_ConfigString: bad index: %s", QA_JS_NUMBER(index));
    qa_common_error(QA_COMMON_ERROR_DROP, message);
  }
  if (index != trunc(index) || (size_t)index >= p->string_count) qa_range_error("CG_ConfigString: bad index %s", QA_JS_NUMBER(index));
  return p->strings[(size_t)index];
}
static void refresh_game_state(void *ctx) {
  pres *p = ctx;
  size_t count = 0;
  const char *const *strings = p->common.session.get_game_state(p->common.session.ctx, &count);
  if (count > CONFIG_STRING_COUNT) count = CONFIG_STRING_COUNT;
  for (size_t i = 0; i < count; i++) owned_text(&p->strings[i], strings[i] ? strings[i] : "");
  p->string_count = count;
}
static void print_text(void *ctx, const char *text) {
  pres *p = ctx;
  p->common.session.print(p->common.session.ctx, text);
}
static void set_cvar(void *ctx, const char *name, const char *value) {
  pres *p = ctx;
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(p->common.session.cvars, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
}
static qa_q3_pres_client_presentation_options *client_options(pres *p) {
  if (p->scene_only) qa_range_error(PRIMARY_ONLY_ERROR);
  return &p->primary;
}
static void send_client_command(void *ctx, const char *text) {
  pres *p = ctx;
  client_options(p)->common.session.add_reliable_command(p->common.session.ctx, text);
}
static void send_console_command(void *ctx, const char *text) {
  pres *p = ctx;
  client_options(p)->common.session.append_console_command(p->common.session.ctx, text);
}
static qa_q3_pres_client_draw_status *draw_status(pres *p) {
  if (!p->has_status) qa_range_error("Supplemental cgame cannot own primary status messages");
  return &p->status;
}
static void center_print(void *ctx, const char *text, double y, double width) {
  pres *p = ctx;
  if (p->scene_only) {
    p->scene_options.center_print(p->common.ctx, text, p->state.time, numeric(p, "cg_centertime") * 1000);
  } else if (!qa_q3_pres_client_draw_status_center_print(draw_status(p), text, y, width)) {
    oom();
  }
}
/* ---- sound ---- */
static void start_sound(void *ctx, const qa_vec3 *origin, double entity, double channel, qa_pcm_sound *pcm) {
  qa_q3_pres_client_sound *sound = &((pres *)ctx)->common.sound;
  sound->start_sound(sound->ctx, origin, entity, channel, pcm);
}
static void start_sound_flagged(void *ctx, bool has_origin, qa_vec3 origin, double entity, double channel, qa_pcm_sound *pcm) {
  start_sound(ctx, has_origin ? &origin : NULL, entity, channel, pcm);
}
static void start_sound_at(void *ctx, qa_vec3 origin, double entity, double channel, qa_pcm_sound *pcm) {
  start_sound(ctx, &origin, entity, channel, pcm);
}
static void start_local_sound(void *ctx, qa_pcm_sound *pcm, double channel) {
  qa_q3_pres_client_sound *sound = &((pres *)ctx)->common.sound;
  sound->start_local_sound(sound->ctx, pcm, channel);
}
static void start_source_sound(void *ctx, qa_pcm_sound *pcm, qa_start_sound_options options) {
  qa_q3_pres_client_sound *sound = &((pres *)ctx)->common.sound;
  sound->start_source_sound(sound->ctx, pcm, options);
}
static void add_loop_sound(void *ctx, double entity, qa_vec3 origin, qa_vec3 velocity, qa_pcm_sound *pcm, bool real_loop) {
  qa_q3_pres_client_sound *sound = &((pres *)ctx)->common.sound;
  sound->add_loop_sound(sound->ctx, entity, origin, velocity, pcm, real_loop);
}
static void add_unreal_loop_sound(void *ctx, double entity, qa_vec3 origin, qa_vec3 velocity, qa_pcm_sound *pcm) {
  add_loop_sound(ctx, entity, origin, velocity, pcm, false);
}
static void clear_looping_sounds(void *ctx, bool kill_all) {
  qa_q3_pres_client_sound *sound = &((pres *)ctx)->common.sound;
  sound->clear_looping_sounds(sound->ctx, kill_all);
}
static void update_sound_position(void *ctx, double entity, qa_vec3 position) {
  qa_q3_pres_client_sound *sound = &((pres *)ctx)->common.sound;
  sound->update_sound_position(sound->ctx, entity, position);
}
static void stop_looping_sound(void *ctx, double entity) {
  qa_q3_pres_client_sound *sound = &((pres *)ctx)->common.sound;
  sound->stop_looping_sound(sound->ctx, entity);
}
static bool bank_register_sound(void *ctx, const char *path, bool compressed, qa_pcm_sound **out) {
  return qa_q3_pres_presentation_sound_bank_register_sound(((pres *)ctx)->common.sound.bank, path, compressed, out);
}
static double bank_index_for_sound(void *ctx, qa_pcm_sound *sound) {
  return qa_q3_pres_presentation_sound_bank_index_for_sound(((pres *)ctx)->common.sound.bank, sound);
}
static qa_pcm_sound *bank_sound(void *ctx, const char *path, bool compressed) {
  qa_pcm_sound *sound = NULL;
  if (!qa_q3_pres_presentation_sound_bank_sound(((pres *)ctx)->common.sound.bank, path, compressed, &sound)) oom();
  return sound;
}
static qa_pcm_sound *bank_sound_plain(void *ctx, const char *path) { return bank_sound(ctx, path, false); }
/* ---- resources ---- */
static bool res_register_model(void *ctx, const char *path, qa_q3_pres_scene_model *out) {
  qa_q3_pres_renderer_resources *r = &((pres *)ctx)->common.resources;
  return r->register_model(r->ctx, path, out);
}
static bool res_register_skin(void *ctx, const char *path, const qa_q3_pres_scene_skin **out) {
  qa_q3_pres_renderer_resources *r = &((pres *)ctx)->common.resources;
  return r->register_skin(r->ctx, path, out);
}
static bool res_register_shader(void *ctx, const char *path, const qa_q3_pres_scene_shader **out) {
  qa_q3_pres_renderer_resources *r = &((pres *)ctx)->common.resources;
  return r->register_shader(r->ctx, path, out);
}
static bool res_register_shader_no_mip(void *ctx, const char *path, const qa_q3_pres_scene_shader **out) {
  qa_q3_pres_renderer_resources *r = &((pres *)ctx)->common.resources;
  return r->register_shader_no_mip(r->ctx, path, out);
}
static const qa_q3_pres_scene_shader *res_register_shader_value(void *ctx, const char *path) {
  const qa_q3_pres_scene_shader *shader = NULL;
  if (!res_register_shader(ctx, path, &shader)) oom();
  return shader;
}
static const qa_particle_shader *particle_register_shader(void *ctx, const char *name) {
  return (const qa_particle_shader *)res_register_shader_value(ctx, name);
}
static qa_material_picture tools_picture(void *ctx, const qa_q3_pres_scene_shader *shader) {
  qa_q3_pres_renderer_resources *r = &((pres *)ctx)->common.resources;
  return r->picture(r->ctx, shader);
}
static void add_ref_entity(void *ctx, const qa_q3_pres_ref_entity *entity) {
  qa_q3_pres_renderer_resources *r = &((pres *)ctx)->common.resources;
  qa_q3_pres_admitted_ref_entity admitted;
  memset(&admitted, 0, sizeof admitted);
  admitted.entity = *entity;
  r->add_ref_entity(r->ctx, &admitted);
}
static void add_light(void *ctx, qa_q3_pres_dynamic_light light) {
  qa_q3_pres_renderer_resources *r = &((pres *)ctx)->common.resources;
  r->add_light(r->ctx, light);
}
static void add_poly(void *ctx, const qa_q3_pres_ref_poly *poly) {
  qa_q3_pres_renderer_resources *r = &((pres *)ctx)->common.resources;
  r->add_poly(r->ctx, poly);
}
static void clear_scene(void *ctx) {
  qa_q3_pres_renderer_resources *r = &((pres *)ctx)->common.resources;
  r->clear_scene(r->ctx);
}
static qa_particle_resources particle_resources(pres *p) {
  qa_particle_resources resources = {p, particle_register_shader};
  return resources;
}
/* ---- client info store ---- */
static qa_q3_pres_client_info_settings client_info_settings(void *ctx) {
  pres *p = ctx;
  qa_q3_pres_client_info_settings settings;
  memset(&settings, 0, sizeof settings);
  settings.game_type = p->static_state.game_type;
  settings.max_clients = p->static_state.maxclients;
  settings.force_model = enabled(p, "cg_forceModel");
  qa_cvar_snapshot value;
  settings.model = "";
  if (qa_cvar_registry_get(p->common.session.cvars, "model", &value)) {
    settings.model = owned_text(&p->settings_model, value.value);
    qa_cvar_read_free(&value);
  }
  settings.head_model = "";
  if (qa_cvar_registry_get(p->common.session.cvars, "headmodel", &value)) {
    settings.head_model = owned_text(&p->settings_head, value.value);
    qa_cvar_read_free(&value);
  }
  settings.red_team_name = settings.blue_team_name = "";
  if (p->common.session.product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    value = read_vm(p, "cg_redTeamName");
    settings.red_team_name = owned_text(&p->settings_red, value.value);
    qa_cvar_read_free(&value);
    value = read_vm(p, "cg_blueTeamName");
    settings.blue_team_name = owned_text(&p->settings_blue, value.value);
    qa_cvar_read_free(&value);
  }
  settings.defer_players = enabled(p, "cg_deferPlayers");
  settings.build_script = enabled(p, "cg_buildScript");
  settings.loading = p->loading_context;
  return settings;
}
static double memory_remaining(void *ctx) {
  pres *p = ctx;
  return p->common.memory_remaining(p->common.ctx);
}
static qa_q3_pres_client_info *client_info_cb(void *ctx, double index) {
  return qa_q3_pres_client_info_store_client_info(((pres *)ctx)->clients, index);
}
static bool new_client_info_cb(void *ctx, double index, const char *config) {
  return qa_q3_pres_client_info_store_new_client_info(((pres *)ctx)->clients, index, config);
}
static bool load_deferred_players_cb(void *ctx, void (*reset)(void *reset_ctx, qa_q3_pres_client_entity *entity), void *reset_ctx) {
  return qa_q3_pres_client_info_store_load_deferred_players(((pres *)ctx)->clients, reset, reset_ctx);
}
static bool reset_clients_cb(void *ctx) { return qa_q3_pres_client_info_store_reset(((pres *)ctx)->clients); }
static void reset_player_entity(void *ctx, qa_q3_pres_client_entity *entity) {
  qa_q3_pres_player_presenter_reset_player_entity(&((pres *)ctx)->players, entity);
}
static qa_pcm_sound *custom_sound(void *ctx, double number, const char *name) {
  return qa_q3_pres_client_info_store_custom_sound(((pres *)ctx)->clients, number, name);
}
static bool status_visible(void *ctx) {
  pres *p = ctx;
  return p->common.session.status_visible(p->common.session.ctx);
}
/* ---- draw icon settings and loading screen ---- */
static qa_q3_pres_client_draw_icon_settings icon_settings(void *ctx) {
  bool draw_icons = enabled(ctx, "cg_drawIcons");
  bool draw_3d_icons = enabled(ctx, "cg_draw3dIcons");
  qa_q3_pres_client_draw_icon_settings settings = {draw_icons, draw_3d_icons};
  return settings;
}
static void draw_loading_information(void *ctx) {
  pres *p = ctx;
  if (!qa_q3_pres_client_loading_screen_draw_information(&p->loading, &p->common.draw)) oom();
}
static void update_screen(void *ctx) {
  pres *p = ctx;
  p->common.session.assert_current(p->common.session.ctx);
  if (p->common.session.server_message_sequence(p->common.session.ctx) != p->initial_message_sequence)
    qa_range_error("Snapshot parsing must serialize behind cgame initialization");
  if (!p->scene_only) p->primary.update_loading_screen(p->common.ctx, draw_loading_information, p);
}
static void loading_string(pres *p, const char *text) {
  if (!qa_q3_pres_client_loading_screen_loading_string(&p->loading, text)) oom();
}
static void media_loading_string(void *ctx, const char *text) { loading_string(ctx, text); }
static void media_loading_item(void *ctx, double index) {
  if (!qa_q3_pres_client_loading_screen_loading_item(&((pres *)ctx)->loading, index)) oom();
}
static void media_loading_client(void *ctx, double index) {
  if (!qa_q3_pres_client_loading_screen_loading_client(&((pres *)ctx)->loading, index)) oom();
}
static qa_q3_pres_client_media_settings media_settings(void *ctx) {
  qa_q3_pres_client_media_settings settings = {enabled(ctx, "cg_buildScript")};
  return settings;
}
static bool load_voice_chats_cb(void *ctx) {
  return qa_q3_pres_client_server_command_runtime_load_voice_chats(((pres *)ctx)->server_commands);
}
static bool build_spectator_cb(void *ctx) {
  return qa_q3_pres_client_server_command_runtime_build_spectator_string(((pres *)ctx)->server_commands);
}
/* ---- server commands ---- */
static bool get_server_command(void *ctx, double sequence, char ***out, size_t *out_count) {
  pres *p = ctx;
  const char *const *tokens = NULL;
  size_t count = 0;
  if (!p->common.session.get_server_command(p->common.session.ctx, sequence, &tokens, &count)) return false;
  char **copy = malloc((count ? count : 1) * sizeof *copy);
  if (!copy) oom();
  for (size_t i = 0; i < count; i++) {
    copy[i] = strdup(tokens[i]);
    if (!copy[i]) oom();
  }
  *out = copy;
  *out_count = count;
  return true;
}
static bool sc_read_vm_cvar(void *ctx, const char *name, qa_cvar_snapshot *out) {
  *out = read_vm(ctx, name);
  return true;
}
static qa_pcm_sound *sc_sound(void *ctx, qa_q3_pres_client_server_command_sound name) {
  const qa_q3_pres_client_media_sounds *s = &((pres *)ctx)->media.sounds;
  switch (name) {
    case QA_Q3_PRES_COUNT_PREPARE_SOUND: return s->count_prepare_sound;
    case QA_Q3_PRES_COUNT_PREPARE_TEAM_SOUND: return s->count_prepare_team_sound;
    case QA_Q3_PRES_COUNT_FIGHT_SOUND: return s->count_fight_sound;
    case QA_Q3_PRES_TALK_SOUND: return s->talk_sound;
    case QA_Q3_PRES_VOTE_NOW: return s->vote_now;
    case QA_Q3_PRES_VOTE_PASSED: return s->vote_passed;
    case QA_Q3_PRES_VOTE_FAILED: return s->vote_failed;
    case QA_Q3_PRES_CLIENT_SERVER_COMMAND_SOUND_COUNT: break;
  }
  return NULL;
}
static void start_background_track(void *ctx, const char *intro, const char *loop) {
  pres *p = ctx;
  client_options(p);
  if (!p->common.sound.start_background_track(p->common.sound.ctx, intro, loop)) oom();
}
static void remap_shader(void *ctx, const char *original, const char *replacement, const char *offset) {
  pres *p = ctx;
  client_options(p);
  p->common.resources.remap_shader(p->common.resources.ctx, original, replacement, offset);
}
static void clear_local_entities(void *ctx) { qa_q3_pres_local_entity_pool_initialize(((pres *)ctx)->pool); }
static void clear_marks(void *ctx) { qa_q3_pres_impact_mark_system_reset(((pres *)ctx)->marks); }
static void clear_particles(void *ctx) {
  pres *p = ctx;
  qa_particle_resources resources = particle_resources(p);
  qa_particle_system_clear(&p->particles, &resources);
}
static void sc_set_score_selection(void *ctx) {
  pres *p = ctx;
  if (!p->has_menus) qa_range_error("Mission score selection called in baseq3");
  qa_q3_pres_mission_hud_set_score_selection(&p->menus);
}
static void sc_show_response_head(void *ctx) {
  pres *p = ctx;
  if (!p->has_menus) qa_range_error("Mission response head called in baseq3");
  qa_q3_pres_mission_hud_show_response_head(&p->menus);
}
/* ---- snapshots ---- */
static void execute_server_commands(void *ctx, double sequence) {
  if (!qa_q3_pres_client_server_command_runtime_execute_new_server_commands(((pres *)ctx)->server_commands, sequence)) oom();
}
static void respawn_cb(void *ctx) { qa_q3_pres_player_state_runtime_respawn(&((pres *)ctx)->player_state); }
static void check_events_cb(void *ctx, qa_q3_pres_client_entity *entity) {
  qa_q3_pres_client_event_runtime_check_events(&((pres *)ctx)->events, entity);
}
static void transition_player_state(void *ctx, qa_q3_base_source_player_state *current, const qa_q3_base_source_player_state *previous) {
  qa_q3_pres_player_state_runtime_transition_player_state(&((pres *)ctx)->player_state, current, (qa_q3_base_source_player_state *)previous);
}
static void lagometer_snapshot(void *ctx, const qa_q3_wire_snapshot *snapshot) {
  pres *p = ctx;
  if (p->scene_only) return;
  if (!snapshot) {
    qa_q3_pres_client_draw_status_add_lagometer_snapshot_info(draw_status(p), NULL);
    return;
  }
  double ping = 0;
  if (!p->common.session.snapshot_ping(p->common.session.ctx, (double)snapshot->message_number, &ping))
    qa_range_error("Cgame snapshot has no engine-owned ping record");
  qa_q3_pres_lagometer_snapshot_sample sample = {ping, (double)snapshot->flags};
  qa_q3_pres_client_draw_status_add_lagometer_snapshot_info(draw_status(p), &sample);
}
/* ---- view ---- */
static qa_q3_pres_view_settings view_settings(void *ctx) {
  pres *p = ctx;
  qa_q3_pres_view_settings s;
  memset(&s, 0, sizeof s);
  s.video_width = p->common.target != NULL ? p->common.target->width : p->common.target_width;
  s.video_height = p->common.target != NULL ? p->common.target->height : p->common.target_height;
  s.view_size = integer(p, "cg_viewsize");
  s.third_person = enabled(p, "cg_thirdPerson");
  s.third_person_range = numeric(p, "cg_thirdPersonRange");
  s.third_person_angle = numeric(p, "cg_thirdPersonAngle");
  s.camera_mode = enabled(p, "cg_cameraMode");
  s.camera_orbit_integer = integer(p, "cg_cameraOrbit");
  s.camera_orbit_value = numeric(p, "cg_cameraOrbit");
  s.camera_orbit_delay = integer(p, "cg_cameraOrbitDelay");
  s.error_decay = numeric(p, "cg_errorDecay");
  s.run_pitch = numeric(p, "cg_runpitch");
  s.run_roll = numeric(p, "cg_runroll");
  s.bob_pitch = numeric(p, "cg_bobpitch");
  s.bob_roll = numeric(p, "cg_bobroll");
  s.bob_up = numeric(p, "cg_bobup");
  s.fov = numeric(p, "cg_fov");
  s.zoom_fov = numeric(p, "cg_zoomFov");
  s.dm_flags = p->static_state.dm_flags;
  s.gun_x = numeric(p, "cg_gun_x");
  s.gun_y = numeric(p, "cg_gun_y");
  s.gun_z = numeric(p, "cg_gun_z");
  return s;
}
static void set_view_size(void *ctx, double value) {
  char text[32];
  snprintf(text, sizeof text, "%s", QA_JS_NUMBER(value));
  set_cvar(ctx, "cg_viewsize", text);
}
static void set_third_person_angle(void *ctx, double value) {
  if (!qa_q3_pres_client_configuration_set_vm_numeric_value(((pres *)ctx)->configuration, QA_Q3_PRES_VM_CG_THIRD_PERSON_ANGLE, value)) oom();
}
/* ---- mission menu host ---- */
static qa_q3_pres_weapon_hud_reading weapon_hud_cb(void *ctx) {
  pres *p = ctx;
  return p->primary.weapon_hud(p->common.ctx);
}
static double milliseconds_cb(void *ctx) {
  pres *p = ctx;
  return p->common.milliseconds(p->common.ctx);
}
static void set_key_catcher(void *ctx, double mask) {
  pres *p = ctx;
  p->primary.set_key_catcher(p->common.ctx, mask);
}
/* ---- status ---- */
static qa_cvar_snapshot status_cvar(void *ctx, qa_q3_pres_draw_status_cvar name) {
  return read_vm(ctx, qa_q3_pres_draw_status_cvar_name(name));
}
/* ---- console ---- */
static bool console_cvar_get(void *ctx, const char *name, const char **out) {
  pres *p = ctx;
  qa_cvar_snapshot value;
  if (!qa_cvar_registry_get(p->common.session.cvars, name, &value)) return false;
  *out = owned_text(&p->console_cvar, value.value);
  qa_cvar_read_free(&value);
  return true;
}
static bool view_test_gun(void *ctx, const char *name, const double *lerp) { return qa_q3_pres_view_runtime_test_gun(&((pres *)ctx)->view, name, lerp); }
static bool view_test_model(void *ctx, const char *name, const double *lerp) { return qa_q3_pres_view_runtime_test_model(&((pres *)ctx)->view, name, lerp); }
static void view_next_frame(void *ctx) { qa_q3_pres_view_runtime_next_model_frame(&((pres *)ctx)->view); }
static void view_previous_frame(void *ctx) { qa_q3_pres_view_runtime_previous_model_frame(&((pres *)ctx)->view); }
static void view_next_skin(void *ctx) { qa_q3_pres_view_runtime_next_model_skin(&((pres *)ctx)->view); }
static void view_previous_skin(void *ctx) { qa_q3_pres_view_runtime_previous_model_skin(&((pres *)ctx)->view); }
static void view_zoom_down(void *ctx) { qa_q3_pres_view_runtime_zoom_down(&((pres *)ctx)->view); }
static void view_zoom_up(void *ctx) { qa_q3_pres_view_runtime_zoom_up(&((pres *)ctx)->view); }
static bool view_clear_model(void *ctx) { return qa_q3_pres_view_runtime_clear_test_model(&((pres *)ctx)->view); }
static void console_next_weapon(void *ctx) { qa_q3_pres_client_weapon_selection_next_weapon(&((pres *)ctx)->console_weapons); }
static void console_previous_weapon(void *ctx) { qa_q3_pres_client_weapon_selection_previous_weapon(&((pres *)ctx)->console_weapons); }
static void console_select_weapon(void *ctx, double weapon) { qa_q3_pres_client_weapon_selection_select_weapon(&((pres *)ctx)->console_weapons, weapon); }
static void hud_reset_strings(void *ctx) { qa_q3_pres_mission_hud_reset_strings(&((pres *)ctx)->menus); }
static void hud_reset_menus(void *ctx) { qa_q3_pres_mission_hud_reset_menus(&((pres *)ctx)->menus); }
static bool hud_load_menus(void *ctx, const char *path) { return qa_q3_pres_mission_hud_load_menus(&((pres *)ctx)->menus, path); }
static void hud_clear_scoreboard(void *ctx) { qa_q3_pres_mission_hud_clear_scoreboard(&((pres *)ctx)->menus); }
static bool hud_menu_scoreboard(void *ctx) { return qa_q3_pres_mission_hud_menu_scoreboard(&((pres *)ctx)->menus) != NULL; }
static bool hud_scroll_feeder(void *ctx, double feeder, bool down) {
  qa_q3_pres_mission_hud *menus = &((pres *)ctx)->menus;
  qa_q3_pres_mission_hud_scroll_feeder(menus, qa_q3_pres_mission_hud_menu_scoreboard(menus), feeder, down);
  return true;
}
static void orders_next(void *ctx) { qa_q3_pres_mission_hud_select_next_player(&((pres *)ctx)->menus); }
static void orders_previous(void *ctx) { qa_q3_pres_mission_hud_select_previous_player(&((pres *)ctx)->menus); }
static bool orders_other_flag(void *ctx) { return qa_q3_pres_mission_hud_other_team_has_flag(&((pres *)ctx)->menus); }
static bool orders_your_flag(void *ctx) { return qa_q3_pres_mission_hud_your_team_has_flag(&((pres *)ctx)->menus); }
static qa_cvar_snapshot console_cvar(void *ctx, qa_q3_pres_console_cvar name) { return read_vm(ctx, qa_q3_pres_console_cvar_name(name)); }
static void console_add_command(void *ctx, const char *name) {
  pres *p = ctx;
  client_options(p)->common.session.register_cgame_command(p->common.session.ctx, name);
}
static qa_pcm_sound *console_sound(void *ctx, qa_q3_pres_console_sound name) {
  const qa_q3_pres_client_media_sounds *s = &((pres *)ctx)->media.sounds;
  return name == QA_Q3_PRES_CONSOLE_SOUND_WINNER ? s->winner_sound : s->loser_sound;
}
static void add_buffered_sound(void *ctx, qa_pcm_sound *sound) {
  qa_q3_pres_client_frame_audio_add_buffered_sound(&((pres *)ctx)->frame_audio, sound);
}
/* ---- prediction ---- */
static qa_bounds move_player(void *ctx, qa_q3_pres_source_player_state *state, qa_q3_pres_user_command command, qa_q3_pres_movement_options options) {
  pres *p = ctx;
  qa_q3_pres_movement_host *movement = &client_options(p)->movement;
  return movement->move_player(movement->ctx, state, command, options);
}
static void update_view_angles(void *ctx, qa_q3_pres_source_player_state *state, qa_q3_pres_user_command command) {
  pres *p = ctx;
  qa_q3_pres_movement_host *movement = &client_options(p)->movement;
  movement->update_view_angles(movement->ctx, state, command);
}
static void predict_item(void *ctx, qa_q3_pres_client_entity *entity, qa_q3_pres_predict_item_source_fn source, void *source_ctx) {
  pres *p = ctx;
  client_options(p)->predict_item(p->common.ctx, entity, source, source_ctx);
}
static const char *debug_show_events(void *ctx) {
  pres *p = ctx;
  qa_cvar_snapshot value;
  if (!qa_cvar_registry_get(p->common.session.cvars, "showevents", &value)) return "";
  const char *text = owned_text(&p->console_cvar, value.value);
  qa_cvar_read_free(&value);
  return text;
}
static qa_q3_pres_prediction_settings prediction_settings(void *ctx) {
  pres *p = ctx;
  qa_q3_pres_prediction_settings s;
  memset(&s, 0, sizeof s);
  s.game_type = p->static_state.game_type;
  s.dm_flags = p->static_state.dm_flags;
  s.demo_playback = p->demo_playback;
  s.no_predict = enabled(p, "cg_nopredict");
  s.synchronous_clients = enabled(p, "cg_synchronousClients");
  s.predict_items = enabled(p, "cg_predictItems");
  s.pmove_fixed = enabled(p, "pmove_fixed");
  s.pmove_msec = integer(p, "pmove_msec");
  s.ladders_disabled = !enabled(p, "pmove_ladders");
  s.error_decay_integer = integer(p, "cg_errorDecay");
  s.error_decay_value = numeric(p, "cg_errorDecay");
  s.show_miss = integer(p, "cg_showmiss");
  return s;
}
static void set_pmove_msec(void *ctx, double value) {
  char text[32];
  snprintf(text, sizeof text, "%s", QA_JS_NUMBER(value));
  set_cvar(ctx, "pmove_msec", text);
}
static qa_q3_pres_movement_trace prediction_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds, double skip, double mask) {
  return qa_q3_pres_prediction_runtime_trace(&((pres *)ctx)->prediction, start, end, bounds, skip, mask);
}
static double prediction_point_contents(void *ctx, qa_vec3 point, double pass) {
  return qa_q3_pres_prediction_runtime_point_contents(&((pres *)ctx)->prediction, point, pass);
}
static qa_q3_trace_result prediction_trace_result(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds, double skip, double mask) {
  return prediction_trace(ctx, start, end, bounds, skip, mask).result;
}
static double collision_point_contents(void *ctx, qa_vec3 point) {
  qa_q3_pres_collision_world *collision = &((pres *)ctx)->prediction.collision;
  return collision->point_contents(collision->ctx, point, 0);
}
static qa_vec3 collision_trace_end(void *ctx, qa_vec3 start, qa_vec3 end, double mask) {
  qa_q3_pres_collision_world *collision = &((pres *)ctx)->prediction.collision;
  qa_q3_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start;
  query.end = end;
  query.shape.kind = "point";
  query.mask = mask;
  return collision->trace(collision->ctx, query).end;
}
static qa_particle_trace particle_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds, double pass, double contents) {
  qa_q3_pres_movement_trace trace = prediction_trace(ctx, start, end, bounds, pass, contents);
  qa_particle_trace result = {trace.result.end, trace.entity_num, trace.result.solidity, trace.result.fraction};
  return result;
}
/* ---- effects, marks, weapons ---- */
static double random_integer(void *ctx) { return qa_game_random_rand(&((pres *)ctx)->random); }
static double random_value(void *ctx) { return qa_game_random_random(&((pres *)ctx)->random); }
static double random_crandom(void *ctx) { return qa_game_random_crandom(&((pres *)ctx)->random); }
static double marks_clock(void *ctx) { return ((pres *)ctx)->state.time; }
static bool marks_enabled(void *ctx) { return enabled(ctx, "cg_addMarks"); }
static const qa_q3_pres_scene_shader *marks_energy_shader(void *ctx) { return ((pres *)ctx)->media.graphics.energy_mark_shader; }
static qa_q3_pres_local_entity *fx_smoke_puff(void *ctx, qa_q3_pres_smoke_puff_options options) {
  return qa_q3_pres_client_effects_smoke_puff(&((pres *)ctx)->effects, options);
}
static void fx_bubble_trail(void *ctx, qa_vec3 start, qa_vec3 end, double spacing) {
  qa_q3_pres_client_effects_bubble_trail(&((pres *)ctx)->effects, start, end, spacing);
}
static qa_q3_pres_local_entity *fx_make_explosion(void *ctx, qa_q3_pres_explosion_options options) {
  return qa_q3_pres_client_effects_make_explosion(&((pres *)ctx)->effects, options);
}
static void fx_bleed(void *ctx, qa_vec3 origin, double entity) { qa_q3_pres_client_effects_bleed(&((pres *)ctx)->effects, origin, entity); }
static bool marks_impact(void *ctx, qa_q3_pres_impact_mark_request request, qa_q3_pres_ref_poly **polys, size_t *count) {
  return qa_q3_pres_impact_mark_system_impact_mark(((pres *)ctx)->marks, request, polys, count);
}
static void particle_explosion(void *ctx, qa_vec3 origin, qa_vec3 velocity, double duration, double size_start, double size_end) {
  qa_particle_explosion_request request = {"explode1", origin, velocity, duration, size_start, size_end};
  qa_particle_system_explosion(&((pres *)ctx)->particles, request);
}
static bool wd_fade_color(void *ctx, double start, double duration, qa_vec4 *out) {
  return qa_q3_pres_fade_color(((pres *)ctx)->state.time, start, duration, out);
}
static void wd_set_color(void *ctx, const qa_vec4 *color) { qa_draw2d_set_color(&((pres *)ctx)->tools.draw, color); }
static void wd_draw_pic(void *ctx, double x, double y, double width, double height, const qa_q3_pres_scene_shader *shader) {
  qa_rect rect = {x, y, width, height};
  qa_q3_pres_client_draw_tools_draw_pic(&((pres *)ctx)->tools, rect, shader);
}
static double wd_string_length(void *ctx, const char *text) {
  (void)ctx;
  return qa_q3_pres_draw_strlen(text);
}
static void wd_big_string(void *ctx, double x, double y, const char *text, qa_vec4 color) {
  qa_q3_pres_client_draw_tools_draw_big_string_color(&((pres *)ctx)->tools, x, y, text, color);
}
static qa_q3_pres_weapon_presentation_settings weapon_settings(void *ctx) {
  pres *p = ctx;
  qa_q3_pres_weapon_presentation_settings s;
  memset(&s, 0, sizeof s);
  s.brass_time = integer(p, "cg_brassTime");
  s.rail_trail_time = numeric(p, "cg_railTrailTime");
  s.old_rail = enabled(p, "cg_oldRail");
  s.no_projectile_trail = enabled(p, "cg_noProjectileTrail");
  s.old_plasma = enabled(p, "cg_oldPlasma");
  s.old_rocket = enabled(p, "cg_oldRocket");
  s.true_lightning = numeric(p, "cg_trueLightning");
  s.draw_gun = enabled(p, "cg_drawGun");
  s.fov = integer(p, "cg_fov");
  s.gun_x = numeric(p, "cg_gun_x");
  s.gun_y = numeric(p, "cg_gun_y");
  s.gun_z = numeric(p, "cg_gun_z");
  s.gun_frame = 0;
  s.tracer_length = numeric(p, "cg_tracerLength");
  s.tracer_width = numeric(p, "cg_tracerWidth");
  s.tracer_chance = numeric(p, "cg_tracerChance");
  s.ragepro = p->common.ragepro;
  return s;
}
/* ---- players ---- */
static bool body_hidden(void *ctx, double entity) {
  pres *p = ctx;
  return p->common.body_hidden(p->common.ctx, entity);
}
static bool body_submission(void *ctx, double entity, qa_qvm_body_part part, const qa_q3_pres_ref_model_entity *source, bool base) {
  pres *p = ctx;
  return p->primary.body_submission(p->common.ctx, entity, part, source, base);
}
static qa_q3_pres_player_presentation_settings player_settings(void *ctx) {
  pres *p = ctx;
  qa_q3_pres_player_presentation_settings s;
  memset(&s, 0, sizeof s);
  s.game_type = p->static_state.game_type;
  s.camera_mode = enabled(p, "cg_cameraMode");
  s.no_player_animations = enabled(p, "cg_noPlayerAnims");
  s.animation_speed = numeric(p, "cg_animSpeed");
  s.swing_speed = numeric(p, "cg_swingSpeed");
  s.draw_friend = enabled(p, "cg_drawFriend");
  s.shadows = integer(p, "cg_shadows");
  s.enable_breath = mission_enabled(p, "cg_enableBreath");
  s.enable_dust = mission_enabled(p, "cg_enableDust");
  s.debug_position = enabled(p, "cg_debugPosition");
  s.debug_animation = enabled(p, "cg_debugAnim");
  return s;
}
static qa_q3_pres_lighting_sample light_for_point(void *ctx, qa_vec3 point) {
  pres *p = ctx;
  return p->common.light_for_point(p->common.ctx, point);
}
typedef struct player_weapon_source {
  pres *p;
  const qa_q3_pres_ref_model_entity *parent;
  qa_q3_base_source_player_state *state;
  qa_q3_pres_client_entity *entity;
  qa_q3_base_team team;
} player_weapon_source;
static void run_player_weapon(void *ctx) {
  player_weapon_source *source = ctx;
  qa_q3_pres_client_weapon_runtime_add_player_weapon(&source->p->weapons, source->parent, source->state, source->entity, source->team);
}
static void add_player_weapon(void *ctx, const qa_q3_pres_ref_model_entity *parent, qa_q3_base_source_player_state *state,
                              qa_q3_pres_client_entity *entity, qa_q3_base_team team) {
  pres *p = ctx;
  if (p->scene_only) return;
  player_weapon_source source = {p, parent, state, entity, team};
  p->primary.player_weapon(p->common.ctx, parent, state, entity, team, run_player_weapon, &source);
}
/* ---- packet entities ---- */
static void body_pose(void *ctx, qa_q3_pres_client_entity *entity) {
  pres *p = ctx;
  p->primary.body_pose(p->common.ctx, entity);
}
typedef struct entity_source { pres *p; qa_q3_pres_client_entity *entity; } entity_source;
static void run_player(void *ctx) {
  entity_source *source = ctx;
  qa_q3_pres_player_presenter_player(&source->p->players, source->entity);
}
static void packet_player(void *ctx, qa_q3_pres_client_entity *entity) {
  pres *p = ctx;
  entity_source source = {p, entity};
  if (p->scene_only) run_player(&source);
  else p->primary.character(p->common.ctx, entity, run_player, &source);
}
static void packet_missile_trail(void *ctx, qa_q3_pres_missile_trail kind, qa_q3_pres_client_entity *entity, const qa_q3_pres_packet_weapon_info *weapon) {
  qa_q3_pres_client_weapon_runtime_missile_trail(&((pres *)ctx)->weapons, kind, entity, weapon);
}
static void packet_grapple_trail(void *ctx, qa_q3_pres_client_entity *entity, const qa_q3_pres_packet_weapon_info *weapon) {
  qa_q3_pres_client_weapon_runtime_grapple_trail(&((pres *)ctx)->weapons, entity, weapon);
}
static void packet_powerups(void *ctx, const qa_q3_pres_ref_model_entity *entity, const qa_q3_base_entity_state *state, qa_q3_base_team team) {
  qa_q3_pres_ref_model_entity copy = *entity;
  qa_q3_pres_player_presenter_add_ref_entity_with_powerups(&((pres *)ctx)->players, &copy, state, team, (qa_qvm_body_part)0);
}
/* ---- events ---- */
static void present_event(void *ctx, qa_q3_pres_client_entity *entity, qa_vec3 position, qa_q3_pres_event_source_fn source, void *source_ctx) {
  pres *p = ctx;
  if (p->scene_only) source(source_ctx);
  else p->primary.event(p->common.ctx, entity, position, source, source_ctx);
}
static void sync_weapon_scratch(pres *p) {
  size_t count = 0;
  const qa_q3_pres_client_weapon_info *weapons = qa_q3_pres_client_weapon_media_registry_weapons(p->weapon_registry, &count);
  for (size_t i = 0; i < count && i < WEAPON_SCRATCH; i++) p->weapon_scratch[i] = weapons[i].base;
}
static void prepare_weapon(void *ctx, double weapon) {
  pres *p = ctx;
  if (!qa_q3_pres_client_weapon_media_registry_register_weapon(p->weapon_registry, weapon)) oom();
  sync_weapon_scratch(p);
}
static const char *event_player_name(void *ctx, double number) {
  pres *p = ctx;
  char *value = qa_info_value_for_key(config_string(p, 544 + number), "n");
  if (!value) oom();
  free(p->player_name);
  p->player_name = value;
  return value;
}
static const char *sound_config_string(void *ctx, double index) { return config_string(ctx, 288 + index); }
static void packet_sound_position(void *ctx, qa_q3_pres_client_entity *entity) {
  qa_q3_pres_packet_entity_presenter_set_entity_sound_position(&((pres *)ctx)->packet, entity);
}
static void packet_beam(void *ctx, qa_q3_pres_client_entity *entity) {
  qa_q3_pres_packet_entity_presenter_beam(&((pres *)ctx)->packet, entity);
}
static void voice_chat_local(void *ctx, double mode, bool voice_only, double client_num, double color, const char *command) {
  if (!qa_q3_pres_client_server_command_runtime_voice_chat_local(((pres *)ctx)->server_commands, mode, voice_only, client_num, color, command)) oom();
}
static void entity_event_cb(void *ctx, qa_q3_pres_client_entity *entity, qa_vec3 position) {
  qa_q3_pres_client_event_runtime_entity_event(&((pres *)ctx)->events, entity, position);
}
static void pain_event_cb(void *ctx, qa_q3_pres_client_entity *entity, double health) {
  qa_q3_pres_client_event_runtime_pain_event(&((pres *)ctx)->events, entity, health);
}
static qa_q3_pres_client_event_options event_options(pres *p) {
  qa_q3_pres_client_event_options o;
  memset(&o, 0, sizeof o);
  o.game_type = p->static_state.game_type;
  o.debug_events = enabled(p, "cg_debugEvents");
  o.footsteps = enabled(p, "cg_footsteps");
  o.autoswitch = enabled(p, "cg_autoswitch");
  o.demo_playback = p->demo_playback;
  o.no_predict = enabled(p, "cg_nopredict");
  o.synchronous_clients = enabled(p, "cg_synchronousClients");
  o.single_player_active = mission_enabled(p, "cg_singlePlayerActive");
  o.camera_orbit = enabled(p, "cg_cameraOrbit");
  return o;
}
static qa_q3_pres_effect_options effect_options(pres *p) {
  qa_q3_pres_effect_options o;
  memset(&o, 0, sizeof o);
  o.no_projectile_trail = enabled(p, "cg_noProjectileTrail");
  o.blood = enabled(p, "cg_blood");
  o.gibs = enabled(p, "cg_gibs");
  o.score_plum = enabled(p, "cg_scorePlum");
  o.hardware = p->common.ragepro ? QA_Q3_PRES_EFFECT_RAGEPRO : QA_Q3_PRES_EFFECT_GENERIC;
  return o;
}
/* Landed hosts store as plain values what TS reads through getters. VM cvars
 * and frame context only change at enter_frame and update_cvars, so these run
 * from enter_frame and from the clear_looping_sounds call that follows
 * update_cvars in both frame paths. */
static void refresh_live(pres *p) {
  p->snapshots.host.demo_playback = p->demo_playback;
  p->snapshots.host.no_predict = p->scene_only || enabled(p, "cg_nopredict");
  p->snapshots.host.synchronous_clients = enabled(p, "cg_synchronousClients");
  p->effects.options = effect_options(p);
  p->events.host.options = event_options(p);
  p->player_state.host.show_miss = enabled(p, "cg_showmiss");
}
/* ---- HUD ---- */
static qa_cvar_snapshot corner_cvar(void *ctx, qa_q3_pres_hud_corner_cvar name) { return read_vm(ctx, qa_q3_pres_hud_corner_cvar_name(name)); }
static const qa_q3_pres_scene_shader *corner_weapon_icon(void *ctx, double weapon) {
  return qa_q3_pres_client_weapon_media_registry_weapon_or_empty(((pres *)ctx)->weapon_registry, weapon)->weapon_icon;
}
static bool corner_pickup_visual(void *ctx, double value, const qa_q3_pres_scene_shader **out) {
  pres *p = ctx;
  if (!qa_q3_pres_client_weapon_media_registry_register_item_visuals(p->weapon_registry, value)) oom();
  size_t count = 0;
  const qa_q3_pres_packet_item_visual *items = qa_q3_pres_client_weapon_media_registry_items(p->weapon_registry, &count);
  if (value != trunc(value) || value < 0 || value >= (double)count) return false;
  *out = items[(size_t)value].icon;
  return true;
}
static qa_cvar_snapshot hud_cvar(void *ctx, qa_q3_pres_hud_cvar name) { return read_vm(ctx, qa_q3_pres_hud_cvar_name(name)); }
static qa_cvar_snapshot scoreboard_cvar(void *ctx, qa_q3_pres_scoreboard_cvar name) { return read_vm(ctx, qa_q3_pres_scoreboard_cvar_name(name)); }
/* ---- frame host ---- */
static qa_q3_pres_packet_entity_options packet_options(void *ctx) {
  pres *p = ctx;
  sync_weapon_scratch(p);
  p->particles.state.snap_origin = p->state.snap ? &p->state.snap->player_state.origin_value : NULL;
  qa_q3_pres_packet_entity_options options;
  memset(&options, 0, sizeof options);
  options.game_type = p->static_state.game_type;
  options.smooth_clients = enabled(p, "cg_smoothClients");
  options.simple_items = enabled(p, "cg_simpleItems");
  options.obelisk_respawn_delay = p->common.session.product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? integer(p, "cg_obeliskRespawnDelay") : 0;
  return options;
}
static void enter_frame(void *ctx, qa_q3_pres_presentation_frame frame) {
  pres *p = ctx;
  p->common.session.assert_current(p->common.session.ctx);
  p->demo_playback = frame.demo_playback;
  p->engine_frame_number = frame.engine_frame_number;
  refresh_live(p);
}
static void frame_clear_looping(void *ctx, bool kill_all) {
  refresh_live(ctx);
  clear_looping_sounds(ctx, kill_all);
}
static void present_view_weapon(void *ctx, qa_q3_base_source_player_state *state, qa_q3_pres_frame_source_fn source, void *source_ctx) {
  pres *p = ctx;
  client_options(p)->view_weapon(p->common.ctx, state, source, source_ctx);
}
static void set_user_command_value(void *ctx, double weapon, double sensitivity) {
  pres *p = ctx;
  client_options(p)->common.session.set_user_command_value(p->common.session.ctx, weapon, sensitivity);
}
static void set_listener(void *ctx, double client, qa_vec3 origin, qa_axis axis) {
  qa_q3_pres_client_sound *sound = &((pres *)ctx)->common.sound;
  sound->set_listener(sound->ctx, client, origin, axis);
}
static bool loading_frame(void *ctx) {
  pres *p = ctx;
  return qa_q3_pres_client_loading_screen_draw_information(&p->loading, &p->common.draw);
}
static void set_timescale(void *ctx, double value) {
  qa_q3_game_format_arg args[] = {qa_q3_game_format_number(value)};
  char *text = qa_q3_game_format("%f", args, 1, 32000);
  if (!text) oom();
  set_cvar(ctx, "timescale", text);
  free(text);
}
/* ---- composition ---- */
static bool create_early(pres *p) {
  qa_q3_pres_presentation_session *session = &p->common.session;
  session->assert_current(session->ctx);
  p->initial_message_sequence = session->server_message_sequence(session->ctx);
  if (!qa_q3_pres_client_game_state_init(&p->state, session->product, session->client_number, p->initial_message_sequence)) return false;
  p->state_ready = true;
  qa_q3_pres_client_game_static_state_init(&p->static_state, session->product);
  p->static_ready = true;
  p->static_state.server_command_sequence = session->last_executed_server_command;
  p->demo_playback = session->mode == QA_Q3_PRES_SESSION_DEMO;
  p->engine_frame_number = p->common.frame_number(p->common.ctx);
  qa_game_random_init(&p->random, 0);
  for (size_t i = 0; i < CONFIG_STRING_COUNT; i++) owned_text(&p->strings[i], "");
  p->string_count = CONFIG_STRING_COUNT;
  /* ClientInfoStore */
  qa_q3_pres_client_info_host info;
  memset(&info, 0, sizeof info);
  info.state = &p->state;
  info.assets = p->common.assets.base;
  info.read_file_length = p->common.assets.read_file_length; /* TS passes the SoundAssetReader, which has readFileLength */
  info.resources_ctx = p;
  info.register_model = res_register_model;
  info.register_skin = res_register_skin;
  info.ctx = p;
  info.settings = client_info_settings;
  info.memory_remaining = memory_remaining;
  info.register_shader_no_mip = res_register_shader_no_mip;
  info.register_sound = bank_register_sound;
  info.sound = bank_sound;
  info.print = print_text;
  qa_q3_pres_client_info *slots[QA_Q3_PRES_MAX_CLIENTS];
  for (size_t i = 0; i < QA_Q3_PRES_MAX_CLIENTS; i++) slots[i] = &p->static_state.client_info[i];
  if (!qa_q3_pres_client_info_store_init(&p->clients, info, slots, QA_Q3_PRES_MAX_CLIENTS)) return false;
  /* TS clients.entityReset = entity => players.resetPlayerEntity(entity) (set once the player presenter exists;
     nothing loads client info before then). */
  qa_q3_pres_client_info_store_set_entity_reset(p->clients, reset_player_entity, p);
  /* ClientConfiguration */
  qa_q3_pres_client_configuration_host config;
  memset(&config, 0, sizeof config);
  if (session->status_visible) {
    config.status_ctx = p;
    config.status_visible = status_visible;
  }
  config.cvars = session->cvars;
  config.state = &p->state;
  config.static_state = &p->static_state;
  config.clients_ctx = p;
  config.new_client_info = new_client_info_cb;
  config.config_ctx = p;
  config.config_string = config_string;
  if (!qa_q3_pres_client_configuration_init(&p->configuration, session->product, config)) return false;
  /* ClientMedia and its weapon registry */
  qa_q3_pres_weapon_registration_host registration;
  memset(&registration, 0, sizeof registration);
  registration.ctx = p;
  registration.register_model = res_register_model;
  registration.register_shader = res_register_shader;
  registration.register_sound = bank_register_sound;
  p->weapon_registry = qa_q3_pres_client_weapon_media_registry_create(session->product, registration);
  if (!p->weapon_registry) return false;
  qa_q3_pres_client_weapon_media_registry_view view;
  if (!qa_q3_pres_client_weapon_media_registry_make_view(p->weapon_registry, &p->weapon_scratch, &view)) return false;
  qa_q3_pres_client_sound_bank bank = {p, bank_register_sound, bank_index_for_sound};
  if (!qa_q3_pres_client_media_init(&p->media, session->product, &p->static_state, p->common.resources, bank, view)) return false;
  p->media_ready = true;
  /* Draw owners */
  qa_q3_pres_draw_tools_media tool_media;
  memset(&tool_media, 0, sizeof tool_media);
  tool_media.ctx = p;
  tool_media.picture = tools_picture;
  qa_q3_pres_client_draw_tools_init(&p->tools, p->common.draw, tool_media);
  qa_q3_pres_draw_icons_media icon_media;
  memset(&icon_media, 0, sizeof icon_media);
  icon_media.product = session->product;
  icon_media.static_state = &p->static_state;
  icon_media.resources = p->common.resources;
  qa_q3_pres_client_draw_icons_init(&p->icons, &p->state, &p->tools, icon_media, icon_settings, p, p->tools.draw.commands.ctx);
  qa_q3_pres_client_loading_imports imports = {p, config_string, update_screen};
  qa_q3_pres_client_loading_screen_init(&p->loading, &p->state, &p->media, session->cvars, imports);
  /* ClientServerCommandRuntime */
  qa_q3_pres_client_server_command_host commands;
  memset(&commands, 0, sizeof commands);
  commands.state = &p->state;
  commands.static_state = &p->static_state;
  commands.clients_ctx = p;
  commands.client_info = client_info_cb;
  commands.new_client_info = new_client_info_cb;
  commands.load_deferred_players = load_deferred_players_cb;
  commands.reset_clients = reset_clients_cb;
  commands.resources_ctx = p;
  commands.register_model = res_register_model;
  commands.assets = p->common.assets;
  commands.random = &p->random;
  commands.ctx = p;
  commands.reset_player_entity = reset_player_entity;
  commands.get_server_command = get_server_command;
  commands.refresh_game_state = refresh_game_state;
  commands.config_string = config_string;
  commands.read_vm_cvar = sc_read_vm_cvar;
  commands.set_cvar = set_cvar;
  commands.print = print_text;
  commands.center_print = center_print;
  commands.send_console_command = send_console_command;
  commands.sound = sc_sound;
  commands.register_sound = bank_register_sound;
  commands.start_local_sound = start_local_sound;
  commands.start_background_track = start_background_track;
  commands.remap_shader = remap_shader;
  commands.clear_local_entities = clear_local_entities;
  commands.clear_marks = clear_marks;
  commands.clear_particles = clear_particles;
  commands.clear_looping_sounds = clear_looping_sounds;
  commands.set_score_selection = sc_set_score_selection;
  commands.show_response_head = sc_show_response_head;
  commands.memory_remaining = memory_remaining;
  if (!qa_q3_pres_client_server_command_runtime_create(&p->server_commands, commands)) return false;
  /* SnapshotRuntime */
  qa_q3_pres_snapshot_host snapshots;
  memset(&snapshots, 0, sizeof snapshots);
  snapshots.source = session->snapshots;
  snapshots.demo_playback = p->demo_playback;
  snapshots.no_predict = p->scene_only;
  snapshots.ctx = p;
  snapshots.execute_server_commands = execute_server_commands;
  snapshots.respawn = respawn_cb;
  snapshots.reset_player_entity = reset_player_entity;
  snapshots.check_events = check_events_cb;
  snapshots.transition_player_state = transition_player_state;
  snapshots.lagometer_snapshot = lagometer_snapshot;
  snapshots.warn = print_text;
  qa_q3_pres_snapshot_runtime_init(&p->snapshots, &p->state, snapshots);
  p->snapshots_ready = true;
  /* PredictionRuntime is pure closures here and must precede the view owner. */
  qa_q3_pres_prediction_host prediction;
  memset(&prediction, 0, sizeof prediction);
  prediction.movement.command_timing = p->scene_only ? "q3" : p->primary.movement.command_timing;
  prediction.movement.ctx = p;
  prediction.movement.move_player = move_player;
  prediction.movement.update_view_angles = update_view_angles;
  prediction.ctx = p;
  prediction.predict_item = predict_item;
  if (!p->scene_only && p->primary.source_debug) {
    p->event_debug.ctx = p;
    p->event_debug.module_is_game = false;
    p->event_debug.show_events = debug_show_events;
    p->event_debug.print = print_text;
    prediction.event_debug = &p->event_debug;
  }
  prediction.commands = session->commands;
  prediction.settings = prediction_settings;
  prediction.set_pmove_msec = set_pmove_msec;
  prediction.transition_player_state = transition_player_state;
  prediction.warn = print_text;
  qa_q3_pres_prediction_runtime_init(&p->prediction, &p->state, p->common.collision, prediction);
  /* ViewRuntime */
  qa_q3_pres_view_host view_host = {p, view_settings, set_view_size, set_third_person_angle, res_register_model, print_text};
  qa_q3_pres_view_runtime_init(&p->view, &p->state, &p->prediction, view_host);
  /* MissionHud */
  if (!p->scene_only && p->primary.menus_missionpack) {
    qa_q3_pres_engine_ui_model_painter_init(&p->model_painter, p->common.resources, p->tools.draw.commands.ctx);
    qa_q3_pres_mission_hud_host menus;
    memset(&menus, 0, sizeof menus);
    if (p->primary.weapon_hud) {
      menus.weapon_hud_ctx = p;
      menus.weapon_hud = weapon_hud_cb;
    }
    menus.assets = p->common.assets;
    menus.font_registry = p->common.font_registry;
    menus.icons = &p->icons;
    menus.configuration = p->configuration;
    menus.cvars = session->cvars;
    menus.ui_commands = p->primary.menu_commands;
    menus.command_context = p->primary.command_context;
    menus.clients = p->clients;
    menus.random = &p->random;
    menus.cinematics = p->primary.menu_cinematics;
    menus.model_painter = &p->model_painter;
    menus.audio = p->primary.menu_audio;
    menus.weapon_registry = p->weapon_registry;
    menus.ctx = p;
    menus.append = send_console_command;
    menus.config_string = config_string;
    menus.reset_player_entity = reset_player_entity;
    menus.print = print_text;
    menus.milliseconds = milliseconds_cb;
    menus.set_key_catcher = set_key_catcher;
    if (!qa_q3_pres_mission_hud_init(&p->menus, &p->state, &p->static_state, &p->media, menus)) return false;
    p->has_menus = true;
  }
  /* ClientDrawStatus */
  if (!p->scene_only) {
    qa_q3_pres_draw_status_variant variant;
    memset(&variant, 0, sizeof variant);
    variant.kind = p->has_menus ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
    if (p->has_menus) variant.fonts = &p->menus.fonts;
    qa_q3_pres_draw_status_host status;
    memset(&status, 0, sizeof status);
    status.commands = session->commands;
    status.ctx = p;
    status.read_vm_cvar = status_cvar;
    status.register_shader = res_register_shader;
    qa_q3_pres_client_draw_status_init(&p->status, &p->state, &p->static_state, &p->tools, &p->static_state, variant, status);
    p->has_status = true;
  }
  /* ClientFrameAudio */
  qa_q3_pres_client_frame_audio_host audio = {p, start_local_sound, start_sound};
  qa_q3_pres_client_frame_audio_init(&p->frame_audio, &p->state, NULL, audio);
  /* ClientConsoleRuntime */
  qa_q3_pres_client_weapon_selection_init(&p->console_weapons, &p->state, p->scene_only ? NULL : p->primary.weapon_selection, p->common.ctx);
  qa_q3_pres_client_console_host console;
  memset(&console, 0, sizeof console);
  console.cvars_ctx = p;
  console.cvar_get = console_cvar_get;
  console.cvar_set = set_cvar;
  console.view_ctx = p;
  console.test_gun = view_test_gun;
  console.test_model = view_test_model;
  console.next_model_frame = view_next_frame;
  console.previous_model_frame = view_previous_frame;
  console.next_model_skin = view_next_skin;
  console.previous_model_skin = view_previous_skin;
  console.zoom_down = view_zoom_down;
  console.zoom_up = view_zoom_up;
  console.clear_test_model = view_clear_model;
  console.weapons_ctx = p;
  console.next_weapon = console_next_weapon;
  console.previous_weapon = console_previous_weapon;
  console.select_weapon = console_select_weapon;
  console.clients_ctx = p;
  console.load_deferred_players = load_deferred_players_cb;
  console.reset_clients = reset_clients_cb;
  console.commands_ctx = p;
  console.build_spectator_string = build_spectator_cb;
  console.hud_available = p->has_menus;
  console.hud_reason = "Base cgame has no mission menu console commands";
  console.orders_available = p->has_menus;
  console.orders_reason = "Base cgame has no mission team-order console commands";
  if (p->has_menus) {
    console.hud_ctx = p;
    console.hud_reset_strings = hud_reset_strings;
    console.hud_reset_menus = hud_reset_menus;
    console.hud_load_menus = hud_load_menus;
    console.hud_clear_scoreboard = hud_clear_scoreboard;
    console.hud_menu_scoreboard = hud_menu_scoreboard;
    console.hud_scroll_feeder = hud_scroll_feeder;
    console.orders_ctx = p;
    console.select_next_player = orders_next;
    console.select_previous_player = orders_previous;
    console.other_team_has_flag = orders_other_flag;
    console.your_team_has_flag = orders_your_flag;
  }
  console.ctx = p;
  console.read_vm_cvar = console_cvar;
  console.reset_player_entity = reset_player_entity;
  console.add_command = console_add_command;
  console.send_client_command = send_client_command;
  console.send_console_command = send_console_command;
  console.print = print_text;
  console.center_print = center_print;
  console.sound = console_sound;
  console.add_buffered_sound = add_buffered_sound;
  qa_q3_pres_client_console_runtime_init(&p->console, &p->state, &p->static_state, console, &p->state, &p->state);
  p->has_console = true;
  return true;
}
/* Value picks landed owners hold where TS reads media.graphics lazily. */
static void sync_media_picks(pres *p) {
  const qa_q3_pres_client_media_graphics *g = &p->media.graphics;
  p->tools.media.charset_shader = g->charset_shader;
  p->tools.media.white_shader = g->white_shader;
  p->tools.media.charset_prop = g->charset_prop;
  p->tools.media.charset_prop_glow = g->charset_prop_glow;
  p->tools.media.charset_prop_b = g->charset_prop_b;
  p->tools.media.back_tile_shader = g->back_tile_shader;
  p->icons.media.red_flag_model = g->red_flag_model;
  p->icons.media.blue_flag_model = g->blue_flag_model;
  p->icons.media.neutral_flag_model = g->neutral_flag_model;
  p->icons.media.defer_shader = g->defer_shader;
  p->icons.media.team_status_bar = g->team_status_bar;
  if (p->has_status) p->status.host.lagometer_shader = g->lagometer_shader;
  p->frame_audio.wear_off_sound = p->media.sounds.wear_off_sound;
}
static bool register_media(pres *p) {
  qa_q3_pres_presentation_session *session = &p->common.session;
  if (!qa_q3_pres_register_client_loading_graphics(&p->media)) return false;
  sync_media_picks(p);
  qa_q3_pres_client_configuration_register_cvars(p->configuration);
  if (!p->scene_only) {
    size_t count = 0;
    const char *const *names = qa_q3_pres_client_console_command_names(session->product, &count);
    for (size_t i = 0; i < count; i++) session->register_cgame_command(session->ctx, names[i]);
  }
  p->state.weapon_select = QA_Q3_WP_MACHINEGUN;
  p->static_state.redflag = -1;
  p->static_state.blueflag = -1;
  p->static_state.flag_status = -1;
  refresh_game_state(p);
  if (strcmp(config_string(p, 20), "baseq3-1") != 0) {
    char message[1200];
    snprintf(message, sizeof message, "Client/Server game mismatch: baseq3-1/%.1024s", config_string(p, 20));
    qa_common_error(QA_COMMON_ERROR_DROP, message);
  }
  p->static_state.level_start_time = qa_game_atoi(config_string(p, 21));
  if (!qa_q3_pres_client_server_command_runtime_parse_server_info(p->server_commands)) return false;
  loading_string(p, "collision map");
  p->loading_context = true;
  qa_q3_pres_client_media_host host;
  memset(&host, 0, sizeof host);
  host.state = &p->state;
  host.static_state = &p->static_state;
  host.clients_ctx = p;
  host.new_client_info = new_client_info_cb;
  host.client_info = client_info_cb;
  host.commands_ctx = p;
  host.load_voice_chats = load_voice_chats_cb;
  host.build_spectator_string = build_spectator_cb;
  host.ctx = p;
  host.config_string = config_string;
  host.settings = media_settings;
  host.loading_string = media_loading_string;
  host.loading_item = media_loading_item;
  host.loading_client = media_loading_client;
  host.clear_scene = clear_scene;
  loading_string(p, "sounds");
  if (!qa_q3_pres_register_client_sounds(&p->media, &host)) return false;
  loading_string(p, "graphics");
  if (!qa_q3_pres_register_client_graphics(&p->media, &host, &p->registered)) return false;
  p->graphics_registered = true;
  sync_media_picks(p);
  loading_string(p, "clients");
  return qa_q3_pres_register_clients(&p->media, &host);
}
static bool create_gameplay(pres *p) {
  qa_q3_base_product product = p->common.session.product;
  /* Registered scalar media projections are captured only after their source registration phase. */
  if (!qa_q3_pres_local_entity_pool_create(&p->pool, product)) return false;
  qa_q3_pres_effect_imports effect_imports = {p, random_integer, start_sound_at};
  qa_q3_pres_client_effects * effects2 = &p->effects;
  qa_q3_pres_client_game_state * state2 = &p->state;
  qa_q3_pres_local_entity_pool * pool2 = p->pool;
  qa_q3_base_product pool_product = product;
  qa_q3_pres_effect_media media2 = qa_q3_pres_client_media_effects(&p->media);
  qa_q3_pres_effect_options options2 = effect_options(p);
  qa_q3_pres_client_effects_init(effects2, state2, pool2, pool_product, media2,
                                 options2, effect_imports);
  qa_q3_pres_bsp_mark_projector_init(&p->projector, p->common.mark_geometry);
  qa_q3_pres_impact_mark_options mark_options = {p, marks_clock, marks_enabled, marks_energy_shader};
  p->marks = qa_q3_pres_impact_mark_system_create(&p->projector, mark_options);
  if (!p->marks) return false;
  qa_particle_host particles;
  memset(&particles, 0, sizeof particles);
  particles.animations = p->registered.particle_animations;
  particles.media = qa_q3_pres_client_media_particles(&p->media);
  particles.prediction.ctx = p;
  particles.prediction.trace = particle_trace;
  particles.random = &p->random;
  particles.hardware_type = p->common.ragepro ? "rage-pro" : "generic";
  particles.config_ctx = p;
  particles.config_string = config_string;
  particles.print_ctx = p;
  particles.print = print_text;
  qa_particle_client_state particle_state = {&p->state.time, &p->state.refdef.view_axis, NULL};
  qa_particle_system_init(&p->particles, particle_state, particles, NULL);
  p->particles_ready = true;
  /* LocalEntitySystem */
  memset(&p->local_media, 0, sizeof p->local_media);
  (void)qa_q3_pres_client_media_local_entities(&p->media, &p->local_media);
  qa_q3_pres_local_entity_host local;
  memset(&local, 0, sizeof local);
  local.product = product;
  local.media = &p->local_media.base;
  local.mission_media = product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? &p->local_media : NULL;
  local.prediction_ctx = p;
  local.prediction_trace = prediction_trace;
  local.collision = p->common.collision;
  local.audio_ctx = p;
  local.audio_start_sound = start_source_sound;
  local.client_num = p->state.client_num;
  local.random = &p->random;
  local.marks = qa_q3_pres_impact_mark_system_as_view(p->marks);
  qa_q3_pres_local_entity_system_init(&p->local_entities, qa_q3_pres_client_effects_as_view(&p->effects), local);
  /* ClientWeaponRuntime */
  qa_q3_pres_client_weapon_host weapons;
  memset(&weapons, 0, sizeof weapons);
  weapons.ctx = p;
  weapons.add_ref_entity = add_ref_entity;
  weapons.add_light = add_light;
  weapons.start_sound = start_sound;
  weapons.add_loop_sound = add_loop_sound;
  weapons.prediction_ctx = p;
  weapons.trace = prediction_trace;
  weapons.point_contents = prediction_point_contents;
  weapons.collision_point_contents = collision_point_contents;
  weapons.collision_trace_end = collision_trace_end;
  weapons.random_ctx = p;
  weapons.rand = random_integer;
  weapons.random = random_value;
  weapons.crandom = random_crandom;
  weapons.local_entities = p->pool;
  weapons.effects_ctx = p;
  weapons.smoke_puff = fx_smoke_puff;
  weapons.bubble_trail = fx_bubble_trail;
  weapons.make_explosion = fx_make_explosion;
  weapons.bleed = fx_bleed;
  weapons.marks_ctx = p;
  weapons.impact_mark = marks_impact;
  weapons.particles_ctx = p;
  weapons.explosion = particle_explosion;
  weapons.media = qa_q3_pres_client_media_weapons(&p->media);
  weapons.drawing.ctx = p;
  weapons.drawing.fade_color = wd_fade_color;
  weapons.drawing.set_color = wd_set_color;
  weapons.drawing.draw_pic = wd_draw_pic;
  weapons.drawing.draw_string_length = wd_string_length;
  weapons.drawing.draw_big_string_color = wd_big_string;
  weapons.settings = weapon_settings;
  weapons.client_info = client_info_cb;
  weapons.sound = bank_sound_plain;
  weapons.add_poly = add_poly;
  qa_q3_pres_client_weapon_runtime_init(&p->weapons, &p->state, p->weapon_registry, weapons);
  /* PlayerPresenter */
  qa_q3_pres_player_presentation_host players;
  memset(&players, 0, sizeof players);
  players.product = product;
  if (product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    p->mission_player_media = qa_q3_pres_client_media_mission_players(&p->media);
    players.mission_media = &p->mission_player_media;
  }
  players.body_ctx = p;
  if (p->common.body_hidden) players.body_hidden = body_hidden;
  if (!p->scene_only && p->primary.body_submission) players.body_submission = body_submission;
  players.state = &p->state;
  players.media = qa_q3_pres_client_media_players(&p->media);
  players.clients_ctx = p;
  players.client_info = client_info_cb;
  players.collision = p->common.collision;
  players.effects = qa_q3_pres_client_effects_as_smoke_view(&p->effects);
  players.random = &p->random;
  players.ctx = p;
  players.settings = player_settings;
  players.trace = prediction_trace_result;
  players.trace_ctx = p;
  players.add_entity = add_ref_entity;
  players.add_light = add_light;
  players.add_poly = add_poly;
  players.light_for_point = light_for_point;
  players.marks = qa_q3_pres_impact_mark_system_as_view(p->marks);
  players.add_looping_sound = add_unreal_loop_sound;
  players.add_player_weapon = add_player_weapon;
  players.print = print_text;
  qa_q3_pres_player_presenter_init(&p->players, players);
  /* PacketEntityPresenter */
  p->packet_media = qa_q3_pres_client_media_packet(&p->media);
  qa_q3_pres_packet_entity_imports packet;
  memset(&packet, 0, sizeof packet);
  packet.ctx = p;
  if (p->common.body_hidden) packet.body_hidden = body_hidden;
  if (!p->scene_only && p->primary.body_pose) packet.pose = body_pose;
  packet.add_ref_entity = add_ref_entity;
  packet.add_light = add_light;
  packet.update_sound_position = update_sound_position;
  packet.add_loop_sound = add_loop_sound;
  packet.start_sound = start_sound_flagged;
  packet.random_integer = random_integer;
  packet.player = packet_player;
  packet.missile_trail = packet_missile_trail;
  packet.grapple_trail = packet_grapple_trail;
  packet.add_entity_with_powerups = packet_powerups;
  qa_q3_pres_packet_entity_presenter_init(&p->packet, &p->state, &p->packet_media, packet);
  /* ClientEventRuntime */
  const qa_q3_pres_client_media_sounds *s = &p->media.sounds;
  qa_q3_pres_client_event_host events;
  memset(&events, 0, sizeof events);
  events.product = product;
  if (product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    events.mission_sounds[QA_Q3_PRES_USE_INVULNERABILITY_SOUND] = s->use_invulnerability_sound;
    events.mission_sounds[QA_Q3_PRES_SCOUT_SOUND] = s->scout_sound;
    events.mission_sounds[QA_Q3_PRES_GUARD_SOUND] = s->guard_sound;
    events.mission_sounds[QA_Q3_PRES_DOUBLER_SOUND] = s->doubler_sound;
    events.mission_sounds[QA_Q3_PRES_AMMOREGEN_SOUND] = s->ammoregen_sound;
    events.mission_sounds[QA_Q3_PRES_WSTBIMPL_SOUND] = s->wstbimpl_sound;
    events.mission_sounds[QA_Q3_PRES_WSTBIMPM_SOUND] = s->wstbimpm_sound;
    events.mission_sounds[QA_Q3_PRES_WSTBIMPD_SOUND] = s->wstbimpd_sound;
    events.mission_sounds[QA_Q3_PRES_WSTBACTV_SOUND] = s->wstbactv_sound;
    events.mission_sounds[QA_Q3_PRES_YOUR_TEAM_TOOK_THE_FLAG_SOUND] = s->your_team_took_the_flag_sound;
    events.mission_sounds[QA_Q3_PRES_ENEMY_TOOK_THE_FLAG_SOUND] = s->enemy_took_the_flag_sound;
    events.mission_sounds[QA_Q3_PRES_KAMIKAZE_FAR_SOUND] = s->kamikaze_far_sound;
    events.mission_effects = qa_q3_pres_client_effects_as_mission_view(&p->effects);
    events.mission_ctx = p;
    events.start_local_sound = start_local_sound;
    events.voice_chat_local = voice_chat_local;
  }
  events.media = qa_q3_pres_client_media_events(&p->media);
  events.options = event_options(p);
  events.random = &p->random;
  events.entities_ctx = p;
  events.set_entity_sound_position = packet_sound_position;
  events.beam = packet_beam;
  events.weapons = qa_q3_pres_client_weapon_runtime_as_view(&p->weapons);
  events.effects = qa_q3_pres_client_effects_as_event_view(&p->effects);
  events.ctx = p;
  events.present_event = present_event;
  events.prepare_weapon = prepare_weapon;
  events.client_info = client_info_cb;
  events.client_info_ctx = p;
  events.player_name = event_player_name;
  events.sound_config_string = sound_config_string;
  events.custom_sound = custom_sound;
  events.register_sound = bank_sound;
  events.start_sound = start_sound_flagged;
  events.stop_looping_sound = stop_looping_sound;
  events.add_buffered_sound = add_buffered_sound;
  events.print = print_text;
  events.center_print = center_print;
  qa_q3_pres_client_event_runtime_init(&p->events, &p->state, events);
  /* PlayerStateRuntime */
  qa_q3_pres_player_state_host transitions;
  memset(&transitions, 0, sizeof transitions);
  transitions.product = product;
  if (product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    transitions.mission_sounds[QA_Q3_PRES_HIT_SOUND_HIGH_ARMOR] = s->hit_sound_high_armor;
    transitions.mission_sounds[QA_Q3_PRES_HIT_SOUND_LOW_ARMOR] = s->hit_sound_low_armor;
    transitions.mission_sounds[QA_Q3_PRES_FIRST_IMPRESSIVE_SOUND] = s->first_impressive_sound;
    transitions.mission_sounds[QA_Q3_PRES_FIRST_EXCELLENT_SOUND] = s->first_excellent_sound;
    transitions.mission_sounds[QA_Q3_PRES_FIRST_HUMILIATION_SOUND] = s->first_humiliation_sound;
  }
  if (!p->scene_only && p->primary.weapon_hud) {
    transitions.weapon_hud_ctx = p;
    transitions.weapon_hud = weapon_hud_cb;
  }
  transitions.static_state = &p->static_state;
  transitions.events.ctx = p;
  transitions.events.entity_event = entity_event_cb;
  transitions.events.pain_event = pain_event_cb;
  transitions.sounds[QA_Q3_PRES_NO_AMMO_SOUND] = s->no_ammo_sound;
  transitions.sounds[QA_Q3_PRES_HIT_SOUND] = s->hit_sound;
  transitions.sounds[QA_Q3_PRES_HIT_TEAM_SOUND] = s->hit_team_sound;
  transitions.sounds[QA_Q3_PRES_CAPTURE_AWARD_SOUND] = s->capture_award_sound;
  transitions.sounds[QA_Q3_PRES_IMPRESSIVE_SOUND] = s->impressive_sound;
  transitions.sounds[QA_Q3_PRES_EXCELLENT_SOUND] = s->excellent_sound;
  transitions.sounds[QA_Q3_PRES_HUMILIATION_SOUND] = s->humiliation_sound;
  transitions.sounds[QA_Q3_PRES_DEFEND_SOUND] = s->defend_sound;
  transitions.sounds[QA_Q3_PRES_ASSIST_SOUND] = s->assist_sound;
  transitions.sounds[QA_Q3_PRES_DENIED_SOUND] = s->denied_sound;
  transitions.sounds[QA_Q3_PRES_HOLY_SHIT_SOUND] = s->holy_shit_sound;
  transitions.sounds[QA_Q3_PRES_YOU_HAVE_FLAG_SOUND] = s->you_have_flag_sound;
  transitions.sounds[QA_Q3_PRES_TAKEN_LEAD_SOUND] = s->taken_lead_sound;
  transitions.sounds[QA_Q3_PRES_TIED_LEAD_SOUND] = s->tied_lead_sound;
  transitions.sounds[QA_Q3_PRES_LOST_LEAD_SOUND] = s->lost_lead_sound;
  transitions.sounds[QA_Q3_PRES_SUDDEN_DEATH_SOUND] = s->sudden_death_sound;
  transitions.sounds[QA_Q3_PRES_ONE_MINUTE_SOUND] = s->one_minute_sound;
  transitions.sounds[QA_Q3_PRES_FIVE_MINUTE_SOUND] = s->five_minute_sound;
  transitions.sounds[QA_Q3_PRES_ONE_FRAG_SOUND] = s->one_frag_sound;
  transitions.sounds[QA_Q3_PRES_TWO_FRAG_SOUND] = s->two_frag_sound;
  transitions.sounds[QA_Q3_PRES_THREE_FRAG_SOUND] = s->three_frag_sound;
  const qa_q3_pres_client_media_graphics *g = &p->media.graphics;
  transitions.medals[QA_Q3_PRES_MEDAL_CAPTURE] = g->medal_capture;
  transitions.medals[QA_Q3_PRES_MEDAL_IMPRESSIVE] = g->medal_impressive;
  transitions.medals[QA_Q3_PRES_MEDAL_EXCELLENT] = g->medal_excellent;
  transitions.medals[QA_Q3_PRES_MEDAL_GAUNTLET] = g->medal_gauntlet;
  transitions.medals[QA_Q3_PRES_MEDAL_DEFEND] = g->medal_defend;
  transitions.medals[QA_Q3_PRES_MEDAL_ASSIST] = g->medal_assist;
  transitions.show_miss = enabled(p, "cg_showmiss");
  transitions.ctx = p;
  transitions.start_local_sound = start_local_sound;
  transitions.add_buffered_sound = add_buffered_sound;
  transitions.print = print_text;
  qa_q3_pres_player_state_runtime_init(&p->player_state, &p->state, transitions);
  return true;
}
static bool create_hud_and_finish(pres *p) {
  if (p->has_menus) {
    if (!qa_q3_pres_mission_hud_asset_cache(&p->menus)) return false;
    if (!qa_q3_pres_mission_hud_load_hud_menu(&p->menus)) return false;
  }
  qa_q3_pres_hud_corners_host corners;
  memset(&corners, 0, sizeof corners);
  corners.ctx = p;
  corners.read_vm_cvar = corner_cvar;
  corners.config_string = config_string;
  corners.milliseconds = milliseconds_cb;
  corners.graphics = &p->media.graphics;
  corners.weapon_icon = corner_weapon_icon;
  corners.register_shader = res_register_shader_value;
  corners.pickup_visual = corner_pickup_visual;
  qa_q3_pres_client_hud_corners_init(&p->corners, &p->state, &p->static_state, &p->icons, corners);
  if (p->has_status) {
    qa_q3_pres_client_hud_host host;
    memset(&host, 0, sizeof host);
    if (p->primary.weapon_hud) {
      host.weapon_hud_ctx = p;
      host.weapon_hud = weapon_hud_cb;
    }
    host.icons = &p->icons;
    host.status = &p->status;
    host.corners = &p->corners;
    host.prediction = &p->prediction;
    host.weapons = &p->weapons;
    host.media = &p->media;
    host.weapon_registry = p->weapon_registry;
    host.random = &p->random;
    host.ctx = p;
    host.read_vm_cvar = hud_cvar;
    host.start_local_sound = start_local_sound;
    qa_q3_pres_client_hud_variant variant;
    memset(&variant, 0, sizeof variant);
    if (p->has_menus) {
      variant.kind = QA_Q3_BASE_PRODUCT_MISSIONPACK;
      variant.fonts = &p->menus.fonts;
      variant.menus = &p->menus;
    } else {
      qa_q3_pres_base_scoreboard_host board;
      memset(&board, 0, sizeof board);
      board.graphics = &p->media.graphics;
      board.icons = &p->icons;
      board.clients = p->clients;
      board.players = &p->players;
      board.ctx = p;
      board.read_vm_cvar = scoreboard_cvar;
      board.config_string = config_string;
      board.send_client_command = send_client_command;
      board.print = print_text;
      qa_q3_pres_base_scoreboard_init(&p->scoreboard, &p->state, &p->static_state, board);
      variant.kind = QA_Q3_BASE_PRODUCT_BASEQ3;
      variant.scoreboard = &p->scoreboard;
    }
    qa_q3_pres_client_hud_init(&p->hud, &p->state, &p->static_state, host, variant);
    p->has_hud = true;
  }
  p->loading_context = false;
  qa_q3_pres_local_entity_pool_initialize(p->pool);
  qa_q3_pres_impact_mark_system_reset(p->marks);
  if (!qa_q3_pres_set_string(&p->state.info_screen_text, "")) return false;
  if (!qa_q3_pres_client_server_command_runtime_set_config_values(p->server_commands)) return false;
  if (!p->scene_only && !qa_q3_pres_client_server_command_runtime_start_music(p->server_commands)) return false;
  loading_string(p, "");
  if (p->has_menus) qa_q3_pres_mission_hud_init_team_chat(&p->menus);
  if (!p->scene_only && !qa_q3_pres_client_server_command_runtime_shader_state_changed(p->server_commands)) return false;
  clear_looping_sounds(p, true);
  sync_media_picks(p);
  qa_q3_pres_presentation_frame_host frame;
  memset(&frame, 0, sizeof frame);
  frame.configuration = p->configuration;
  frame.media = &p->media;
  frame.snapshots = &p->snapshots;
  frame.packet = &p->packet;
  frame.marks = p->marks;
  frame.particles = &p->particles;
  frame.local_entities = &p->local_entities;
  frame.frame_audio = &p->frame_audio;
  frame.server_commands = p->server_commands;
  frame.scene = p->common.scene;
  frame.weapon_registry = p->weapon_registry;
  frame.ctx = p;
  frame.packet_options = packet_options;
  frame.enter_frame = enter_frame;
  frame.clear_looping_sounds = frame_clear_looping;
  if (p->has_hud) {
    frame.prediction = &p->prediction;
    frame.view = &p->view;
    frame.weapons = &p->weapons;
    frame.hud = &p->hud;
    frame.status = &p->status;
    frame.tools = &p->tools;
    frame.ragepro = p->common.ragepro;
    frame.present_view_weapon = present_view_weapon;
    frame.set_user_command_value = set_user_command_value;
    frame.set_listener = set_listener;
    frame.loading_frame = loading_frame;
    frame.set_timescale = set_timescale;
    frame.print = print_text;
    qa_q3_pres_presentation_frame_runtime_init(&p->frames, &p->state, frame);
  } else {
    qa_q3_pres_presentation_scene_host scene = {frame};
    qa_q3_pres_presentation_frame_runtime_init_scene(&p->frames, &p->state, scene);
  }
  p->common.session.assert_current(p->common.session.ctx);
  return true;
}
static pres *create_presentation(pres *p) {
  if (!create_early(p) || !register_media(p) || !create_gameplay(p) || !create_hud_and_finish(p)) {
    qa_q3_pres_client_presentation_destroy(p);
    return NULL;
  }
  return p;
}
qa_q3_pres_client_presentation *qa_q3_pres_create_client_presentation(qa_q3_pres_client_presentation_options options) {
  pres *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->common = options.common;
  p->primary = options;
  return create_presentation(p);
}
qa_q3_pres_client_presentation *qa_q3_pres_create_scene_presentation(qa_q3_pres_scene_presentation_options options) {
  pres *p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->common = options.common;
  p->scene_options = options;
  p->scene_only = true;
  return create_presentation(p);
}
void qa_q3_pres_client_presentation_destroy(qa_q3_pres_client_presentation *p) {
  if (!p) return;
  if (p->has_menus) qa_q3_pres_mission_hud_destroy(&p->menus);
  if (p->particles_ready) qa_particle_system_destroy(&p->particles);
  if (p->graphics_registered) qa_particle_animations_destroy(&p->registered.particle_animations);
  if (p->marks) qa_q3_pres_impact_mark_system_destroy(p->marks);
  if (p->pool) qa_q3_pres_local_entity_pool_destroy(p->pool);
  if (p->snapshots_ready) qa_q3_pres_snapshot_runtime_destroy(&p->snapshots);
  if (p->server_commands) qa_q3_pres_client_server_command_runtime_destroy(p->server_commands);
  if (p->media_ready) qa_q3_pres_client_media_destroy(&p->media);
  free(p->weapon_scratch);
  if (p->weapon_registry) qa_q3_pres_client_weapon_media_registry_destroy(p->weapon_registry);
  if (p->configuration) qa_q3_pres_client_configuration_destroy(p->configuration);
  if (p->clients) qa_q3_pres_client_info_store_destroy(p->clients);
  for (size_t i = 0; i < CONFIG_STRING_COUNT; i++) free(p->strings[i]);
  free(p->settings_model);
  free(p->settings_head);
  free(p->settings_red);
  free(p->settings_blue);
  free(p->player_name);
  free(p->console_cvar);
  if (p->static_ready) qa_q3_pres_client_game_static_state_destroy(&p->static_state);
  if (p->state_ready) qa_q3_pres_client_game_state_destroy(&p->state);
  free(p);
}
#define ACCESSOR(type, name, expr) type qa_q3_pres_client_presentation_##name(qa_q3_pres_client_presentation *p) { return expr; }
ACCESSOR(qa_q3_pres_client_game_state *, state, &p->state)
ACCESSOR(qa_q3_pres_client_game_static_state *, static_state, &p->static_state)
ACCESSOR(qa_q3_pres_client_configuration *, configuration, p->configuration)
ACCESSOR(qa_q3_pres_client_media *, media, &p->media)
ACCESSOR(qa_q3_pres_client_info_store *, clients, p->clients)
ACCESSOR(qa_q3_pres_prediction_runtime *, prediction, p->scene_only ? NULL : &p->prediction)
ACCESSOR(qa_q3_pres_view_runtime *, view, p->scene_only ? NULL : &p->view)
ACCESSOR(qa_q3_pres_snapshot_runtime *, snapshots, &p->snapshots)
ACCESSOR(qa_q3_pres_packet_entity_presenter *, packet, &p->packet)
ACCESSOR(qa_q3_pres_client_event_runtime *, events, &p->events)
ACCESSOR(qa_q3_pres_client_effects *, effects, &p->effects)
ACCESSOR(qa_q3_pres_local_entity_system *, local_entities, &p->local_entities)
ACCESSOR(qa_particle_system *, particles, &p->particles)
ACCESSOR(qa_q3_pres_impact_mark_system *, marks, p->marks)
ACCESSOR(qa_q3_pres_client_weapon_runtime *, weapons, &p->weapons)
ACCESSOR(qa_q3_pres_client_server_command_runtime *, server_commands, p->scene_only ? NULL : p->server_commands)
ACCESSOR(qa_q3_pres_client_console_runtime *, console, p->scene_only ? NULL : &p->console)
ACCESSOR(qa_q3_pres_client_frame_audio *, frame_audio, &p->frame_audio)
ACCESSOR(qa_q3_pres_client_loading_screen *, loading, p->scene_only ? NULL : &p->loading)
ACCESSOR(qa_q3_pres_client_hud *, hud, p->has_hud ? &p->hud : NULL)
ACCESSOR(qa_q3_pres_client_draw_status *, status, p->has_status ? &p->status : NULL)
ACCESSOR(qa_q3_pres_client_draw_tools *, tools, p->scene_only ? NULL : &p->tools)
ACCESSOR(qa_q3_pres_mission_hud *, menus, p->has_menus ? &p->menus : NULL)
ACCESSOR(qa_q3_pres_presentation_frame_runtime *, frames, &p->frames)
const char *qa_q3_pres_client_presentation_config_string(qa_q3_pres_client_presentation *p, double index) { return config_string(p, index); }
static void close_frames(void *ctx) { pres *p = ctx; qa_q3_pres_presentation_frame_runtime_close(&p->frames); }
static void close_menus(void *ctx) { pres *p = ctx; if (p->has_menus) qa_q3_pres_mission_hud_dispose(&p->menus); }
static void close_console(void *ctx) { pres *p = ctx; qa_q3_pres_client_console_runtime_dispose(&p->console); }
static void close_server_commands(void *ctx) { pres *p = ctx; if (!qa_q3_pres_client_server_command_runtime_dispose(p->server_commands)) oom(); }
static void close_pool(void *ctx) { pres *p = ctx; qa_q3_pres_local_entity_pool_initialize(p->pool); }
static void close_marks(void *ctx) { pres *p = ctx; qa_q3_pres_impact_mark_system_reset(p->marks); }
static void close_particles(void *ctx) { pres *p = ctx; qa_particle_system_reset_round(&p->particles); }
static void close_looping(void *ctx) { clear_looping_sounds(ctx, true); }
static void close_scene(void *ctx) { pres *p = ctx; qa_q3_pres_scene_recorder_clear_scene(p->common.scene); }
void qa_q3_pres_client_presentation_close(qa_q3_pres_client_presentation *p) {
  if (p->closed) return;
  p->closed = true;
  /* TS runs every step, collecting failures: one rethrows, several become an AggregateError. */
  static void (*const steps[])(void *) = {close_frames, close_menus, close_console, close_server_commands,
      close_pool, close_marks, close_particles, close_looping, close_scene};
  qa_caught_error first = {0}; size_t errors = 0;
  for (size_t i = 0; i < sizeof steps / sizeof *steps; i++) {
    qa_caught_error caught;
    if (!qa_try(steps[i], p, &caught)) { if (errors++ == 0) first = caught; }
  }
  if (errors == 1) qa_rethrow(&first);
  if (errors > 1) qa_raise("AggregateError", 0, 0, "Cgame presentation close failed");
}
void qa_q3_pres_client_presentation_key_event(qa_q3_pres_client_presentation *p, double key, bool down) {
  p->common.session.assert_current(p->common.session.ctx);
  if (p->has_menus) qa_q3_pres_mission_hud_key_event(&p->menus, key, down);
}
void qa_q3_pres_client_presentation_mouse_event(qa_q3_pres_client_presentation *p, double x, double y) {
  p->common.session.assert_current(p->common.session.ctx);
  if (p->has_menus) qa_q3_pres_mission_hud_mouse_event(&p->menus, x, y);
}
void qa_q3_pres_client_presentation_event_handling(qa_q3_pres_client_presentation *p, double type) {
  p->common.session.assert_current(p->common.session.ctx);
  if (p->has_menus) qa_q3_pres_mission_hud_event_handling(&p->menus, type);
}
