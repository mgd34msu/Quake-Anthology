/*
 * Depth checks for src/content/q3/presentation client.ts, hud.ts,
 * mission-hud.ts, ref-entity.ts and state.ts. The TS suite exercises state and
 * ref-entity directly (tests/app/cgame-status, q3-model-registration,
 * gameplay/q3/presentation/presentation); client, hud and mission-hud have no
 * direct TS test, so their cases assert the TS source branch by branch through
 * the real composition. All leaves receive sentinel contexts and verify them.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/audio.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/client.h"
#include "qa/content/q3/presentation/hud.h"
#include "qa/content/q3/presentation/mission_hud.h"
#include "qa/input/key_codes.h"
#include "qa/movement/q3/constants.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)
#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    fflush(NULL);                                                       \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0);                                  \
      if (!freopen("/dev/null", "w", stderr)) _exit(3);                 \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

/* ---- fixture ---- */
enum { MAX_DRAWS = 8192, MAX_SHADERS = 2048, MAX_LINES = 256 };
typedef struct draw_record {
  qa_rect rect;
  const char *picture;
  qa_vec4 color;
  bool has_color;
} draw_record;
typedef struct fixture {
  uint32_t session_sentinel, common_sentinel, sound_sentinel, resources_sentinel, sink_sentinel, assets_sentinel;
  uint32_t collision_sentinel, movement_sentinel, commands_sentinel, snapshots_sentinel, audio_sentinel, cinematics_sentinel;
  qa_cvar_registry *cvars;
  qa_command_buffer *buffer;
  const char *strings[1024];
  char server_info[256];
  qa_q3_pres_scene_shader shaders[MAX_SHADERS];
  char *shader_names[MAX_SHADERS];
  size_t shader_count;
  draw_record draws[MAX_DRAWS];
  size_t draw_count;
  qa_vec4 color;
  bool has_color;
  char lines[MAX_LINES][256];
  size_t line_count;
  char reliable[32][128];
  size_t reliable_count;
  char console[32][128];
  size_t console_count;
  char registered_commands[128][64];
  size_t registered_command_count;
  size_t local_sound_count;
  qa_pcm_sound *last_local_sound;
  double last_local_channel;
  size_t key_catcher_calls;
  double key_catcher;
  size_t loading_updates;
  double message_sequence;
  bool assert_stale;
  size_t assert_calls;
  qa_q3_pres_presentation_sound_bank *bank;
  qa_sound_asset sound_assets[256];
  size_t sound_asset_count;
  char sound_names[256][128];
  const char *hud_text, *score_text;
  qa_q3_pres_scene_recorder *recorder;
  qa_q3_registered_font font;
  qa_q3_ui_asset_registry fonts;
  qa_q3_pres_client_presentation *presentation;
  bool weapon_hud_on;
  size_t scene_center_prints;
  double scene_center_time, scene_center_duration;
  size_t publish_count, render_count, model_calls;
  bool presentation_mission;
  bool wire_enabled;
  double wire_team_index, wire_team, wire_pm_flags, wire_weapons;
  qa_q3_registered_font fonts_by_name[4];
  qa_picture_asset font_pictures[4];
  char font_names[4][48];
  size_t font_count;
} fixture;
#define SENTINEL 0x51A7C0DEu
#define EXPECT_CTX(field) do { if (((fixture *)((char *)ctx - offsetof(fixture, field)))->field != SENTINEL) abort(); } while (0)
static fixture *fx_session(void *ctx) { EXPECT_CTX(session_sentinel); return (fixture *)((char *)ctx - offsetof(fixture, session_sentinel)); }
static fixture *fx_common(void *ctx) { EXPECT_CTX(common_sentinel); return (fixture *)((char *)ctx - offsetof(fixture, common_sentinel)); }
static fixture *fx_sound(void *ctx) { EXPECT_CTX(sound_sentinel); return (fixture *)((char *)ctx - offsetof(fixture, sound_sentinel)); }
static fixture *fx_res(void *ctx) { EXPECT_CTX(resources_sentinel); return (fixture *)((char *)ctx - offsetof(fixture, resources_sentinel)); }
static fixture *fx_sink(void *ctx) { EXPECT_CTX(sink_sentinel); return (fixture *)((char *)ctx - offsetof(fixture, sink_sentinel)); }
static fixture *fx_assets(void *ctx) { EXPECT_CTX(assets_sentinel); return (fixture *)((char *)ctx - offsetof(fixture, assets_sentinel)); }
/* session */
static double s_sequence(void *ctx) { return fx_session(ctx)->message_sequence; }
static const char *const *s_game_state(void *ctx, size_t *count) { *count = 1024; return fx_session(ctx)->strings; }
static bool s_server_command(void *ctx, double sequence, const char *const **out, size_t *count) {
  (void)fx_session(ctx); (void)sequence; (void)out; (void)count;
  return false;
}
static bool s_ping(void *ctx, double number, double *out) { (void)fx_session(ctx); *out = number; return true; }
static void s_reliable(void *ctx, const char *text) {
  fixture *f = fx_session(ctx);
  if (f->reliable_count < 32) snprintf(f->reliable[f->reliable_count++], 128, "%s", text);
}
static void s_console(void *ctx, const char *text) {
  fixture *f = fx_session(ctx);
  if (f->console_count < 32) snprintf(f->console[f->console_count++], 128, "%s", text);
}
static void s_register_command(void *ctx, const char *name) {
  fixture *f = fx_session(ctx);
  if (f->registered_command_count < 128) snprintf(f->registered_commands[f->registered_command_count++], 64, "%s", name);
}
static void s_user_command(void *ctx, double w, double z) { (void)fx_session(ctx); (void)w; (void)z; }
static void s_assert_current(void *ctx) {
  fixture *f = fx_session(ctx);
  f->assert_calls++;
  if (f->assert_stale) abort();
}
static void s_print(void *ctx, const char *text) {
  fixture *f = fx_session(ctx);
  if (f->line_count < MAX_LINES) snprintf(f->lines[f->line_count++], 256, "%s", text);
}
static bool has_line(fixture *f, const char *needle) {
  for (size_t i = 0; i < f->line_count; i++) if (strstr(f->lines[i], needle)) return true;
  return false;
}
static double cmd_number(void *ctx) { if (*(uint32_t *)ctx != SENTINEL) abort(); return 0; }
static bool cmd_read(void *ctx, double n, qa_q3_base_user_command *out) {
  (void)n;
  if (*(uint32_t *)ctx != SENTINEL) abort();
  memset(out, 0, sizeof *out);
  return true;
}
static fixture *fx_snap(void *ctx) { EXPECT_CTX(snapshots_sentinel); return (fixture *)((char *)ctx - offsetof(fixture, snapshots_sentinel)); }
static qa_q3_pres_snapshot_current snap_current(void *ctx) {
  fixture *f = fx_snap(ctx);
  qa_q3_pres_snapshot_current current = {f->wire_enabled ? 1 : 0, f->wire_enabled ? 1000 : 0};
  return current;
}
static bool snap_read(void *ctx, double n, qa_q3_wire_snapshot *out) {
  fixture *f = fx_snap(ctx);
  if (!f->wire_enabled || n != 1) return false;
  memset(out, 0, sizeof *out);
  out->message_number = 1;
  out->server_time = 1000;
  out->server_command_number = 5;
  out->area_mask_len = 32;
  out->player_state.product = f->presentation_mission ? QA_Q3_PRODUCT_MISSIONPACK : QA_Q3_PRODUCT_BASEQ3;
  out->player_state.client_num = 2;
  out->player_state.pm_flags = (int64_t)f->wire_pm_flags;
  out->player_state.persistant[(size_t)f->wire_team_index] = (int64_t)f->wire_team;
  const qa_q3_base_stat_schema *stats = qa_q3_base_stat_schema_for(f->presentation_mission ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3);
  out->player_state.stats[(size_t)stats->health] = 100;
  out->player_state.stats[(size_t)stats->weapons] = (int64_t)f->wire_weapons;
  return true;
}
/* common */
static double c_ms(void *ctx) { (void)fx_common(ctx); return 1000; }
static double c_frame(void *ctx) { (void)fx_common(ctx); return 1; }
static qa_q3_pres_lighting_sample c_light(void *ctx, qa_vec3 point) {
  (void)fx_common(ctx); (void)point;
  qa_q3_pres_lighting_sample sample;
  memset(&sample, 0, sizeof sample);
  return sample;
}
static double c_memory(void *ctx) { (void)fx_common(ctx); return 16 * 1024 * 1024; }
static void c_update_loading(void *ctx, qa_q3_pres_client_source_fn draw, void *draw_ctx) {
  fixture *f = fx_common(ctx);
  f->loading_updates++;
  draw(draw_ctx);
}
static void c_character(void *ctx, qa_q3_pres_client_entity *e, qa_q3_pres_client_source_fn src, void *sctx) { (void)fx_common(ctx); (void)e; src(sctx); }
static void c_event(void *ctx, qa_q3_pres_client_entity *e, qa_vec3 pos, qa_q3_pres_client_source_fn src, void *sctx) { (void)fx_common(ctx); (void)e; (void)pos; src(sctx); }
static void c_predict(void *ctx, qa_q3_pres_client_entity *e, qa_q3_pres_client_source_fn src, void *sctx) { (void)fx_common(ctx); (void)e; src(sctx); }
static void c_view_weapon(void *ctx, qa_q3_base_source_player_state *s, qa_q3_pres_client_source_fn src, void *sctx) { (void)fx_common(ctx); (void)s; src(sctx); }
static void c_player_weapon(void *ctx, const qa_q3_pres_ref_model_entity *pe, qa_q3_base_source_player_state *s, qa_q3_pres_client_entity *e, qa_q3_base_team t, qa_q3_pres_client_source_fn src, void *sctx) {
  (void)fx_common(ctx); (void)pe; (void)s; (void)e; (void)t; src(sctx);
}
static void c_key_catcher(void *ctx, double mask) { fixture *f = fx_common(ctx); f->key_catcher_calls++; f->key_catcher = mask; }
static qa_q3_pres_weapon_hud_reading c_weapon_hud(void *ctx) {
  (void)fx_common(ctx);
  qa_q3_pres_weapon_hud_reading reading;
  memset(&reading, 0, sizeof reading);
  reading.has_status = true;
  reading.status.ammo_is_finite = true;
  reading.status.ammo_count = 17;
  return reading;
}
static void c_scene_center(void *ctx, const char *text, double t, double d) {
  fixture *f = fx_common(ctx);
  (void)text;
  f->scene_center_prints++;
  f->scene_center_time = t;
  f->scene_center_duration = d;
}
/* sound */
static void snd_start(void *ctx, const qa_vec3 *o, double e, double c, qa_pcm_sound *p) { (void)fx_sound(ctx); (void)o; (void)e; (void)c; (void)p; }
static void snd_source(void *ctx, qa_pcm_sound *p, qa_start_sound_options o) { (void)fx_sound(ctx); (void)p; (void)o; }
static void snd_local(void *ctx, qa_pcm_sound *p, double c) { fixture *f = fx_sound(ctx); f->local_sound_count++; f->last_local_sound = p; f->last_local_channel = c; }
static void snd_loop(void *ctx, double e, qa_vec3 o, qa_vec3 v, qa_pcm_sound *p, bool r) { (void)fx_sound(ctx); (void)e; (void)o; (void)v; (void)p; (void)r; }
static void snd_update(void *ctx, double e, qa_vec3 p) { (void)fx_sound(ctx); (void)e; (void)p; }
static void snd_stop(void *ctx, double e) { (void)fx_sound(ctx); (void)e; }
static void snd_clear(void *ctx, bool k) { (void)fx_sound(ctx); (void)k; }
static void snd_listener(void *ctx, double c, qa_vec3 o, qa_axis a) { (void)fx_sound(ctx); (void)c; (void)o; (void)a; }
static bool snd_track(void *ctx, const char *i, const char *l) { (void)fx_sound(ctx); (void)i; (void)l; return true; }
static const qa_sound_asset *bank_load(void *ctx, const char *path, bool compressed) {
  fixture *f = fx_sound(ctx);
  (void)compressed;
  if (f->sound_asset_count >= 256) abort();
  size_t i = f->sound_asset_count++;
  snprintf(f->sound_names[i], 128, "%s", path);
  memset(&f->sound_assets[i], 0, sizeof f->sound_assets[i]);
  f->sound_assets[i].resource = f->sound_names[i];
  f->sound_assets[i].name = f->sound_names[i];
  return &f->sound_assets[i];
}
/* resources */
static const qa_q3_pres_scene_shader *intern_shader(fixture *f, const char *path) {
  for (size_t i = 0; i < f->shader_count; i++) if (strcmp(f->shader_names[i], path) == 0) return &f->shaders[i];
  if (f->shader_count >= MAX_SHADERS) abort();
  f->shader_names[f->shader_count] = strdup(path);
  f->shaders[f->shader_count].name = f->shader_names[f->shader_count];
  return &f->shaders[f->shader_count++];
}
static bool r_model(void *ctx, const char *path, qa_q3_pres_scene_model *out) { fx_res(ctx)->model_calls++; (void)path; *out = qa_q3_pres_default_model; return true; }
static bool r_skin(void *ctx, const char *path, const qa_q3_pres_scene_skin **out) { (void)fx_res(ctx); (void)path; *out = NULL; return true; }
static bool r_shader(void *ctx, const char *path, const qa_q3_pres_scene_shader **out) {
  fixture *f = fx_res(ctx);
  *out = path && path[0] ? intern_shader(f, path) : NULL;
  return true;
}
static qa_material_picture r_picture(void *ctx, const qa_q3_pres_scene_shader *shader) {
  fixture *f = fx_res(ctx);
  qa_material_picture picture;
  memset(&picture, 0, sizeof picture);
  picture.name = shader ? shader->name : "<null>";
  picture.order = shader ? (double)(shader - f->shaders) + 1 : 0;
  return picture;
}
static double r_model_handle(void *ctx, const qa_q3_pres_scene_model *m) { (void)fx_res(ctx); (void)m; return 7; }
static qa_q3_pres_scene_model r_model_for(void *ctx, double h) { (void)fx_res(ctx); (void)h; return qa_q3_pres_default_model; }
static const qa_q3_pres_scene_shader *r_shader_for(void *ctx, double h) {
  fixture *f = fx_res(ctx);
  return h >= 1 && h <= (double)f->shader_count ? &f->shaders[(size_t)h - 1] : NULL;
}
static void r_clear(void *ctx) { (void)fx_res(ctx); }
static void r_add_ref(void *ctx, const qa_q3_pres_admitted_ref_entity *e) { (void)fx_res(ctx); (void)e; }
static void r_add_poly(void *ctx, const qa_q3_pres_ref_poly *p) { (void)fx_res(ctx); (void)p; }
static void r_add_light(void *ctx, qa_q3_pres_dynamic_light l) { (void)fx_res(ctx); (void)l; }
static void r_remap(void *ctx, const char *a, const char *b, const char *c) { (void)fx_res(ctx); (void)a; (void)b; (void)c; }
static void r_render(void *ctx, const qa_q3_pres_refdef *r) { fx_res(ctx)->render_count++; (void)r; }
static bool r_world(void *ctx, const char *p, qa_q3_pres_world_scene *out) { (void)fx_res(ctx); (void)p; memset(out, 0, sizeof *out); return true; }
/* sink */
static void sink_color(void *ctx, const qa_vec4 *color) {
  fixture *f = fx_sink(ctx);
  f->has_color = color != NULL;
  if (color) f->color = *color;
}
static void sink_stretch(void *ctx, qa_rect rect, qa_text_texture_rect uv, const qa_picture_asset *picture) {
  fixture *f = fx_sink(ctx);
  (void)uv;
  if (f->draw_count >= MAX_DRAWS) return;
  draw_record *record = &f->draws[f->draw_count++];
  record->rect = rect;
  record->picture = picture->kind == QA_PICTURE_MATERIAL ? picture->material.name : "<image>";
  record->color = f->color;
  record->has_color = f->has_color;
}
/* assets */
static const char *asset_text(fixture *f, const char *path) {
  if (f->hud_text && strcmp(path, "ui/hud.txt") == 0) return f->hud_text;
  if (f->score_text && strcmp(path, "ui/score.menu") == 0) return f->score_text;
  return NULL;
}
static bool a_read(void *ctx, const char *path, uint8_t **out, size_t *length) {
  fixture *f = fx_assets(ctx);
  const char *text = asset_text(f, path);
  if (!text) abort();
  *length = strlen(text);
  *out = malloc(*length + 1);
  memcpy(*out, text, *length + 1);
  return true;
}
static bool a_has(void *ctx, const char *path) { return asset_text(fx_assets(ctx), path) != NULL; }
static bool a_list(void *ctx, const char *prefix, char ***out, size_t *count) { (void)fx_assets(ctx); (void)prefix; *out = NULL; *count = 0; return true; }
static double a_length(void *ctx, const char *path) {
  const char *text = asset_text(fx_assets(ctx), path);
  return text ? (double)strlen(text) : -1;
}
/* collision */
static qa_q3_trace_result col_trace(void *ctx, qa_q3_trace_query query) {
  if (*(uint32_t *)ctx != SENTINEL) abort();
  qa_q3_trace_result result;
  memset(&result, 0, sizeof result);
  result.fraction = 1;
  result.end = query.end;
  result.solidity = "clear";
  return result;
}
static double col_contents(void *ctx, qa_vec3 p, double m) { (void)p; (void)m; if (*(uint32_t *)ctx != SENTINEL) abort(); return 0; }
static qa_q3_trace_result col_ttrace(void *ctx, qa_q3_trace_query q, qa_vec3 o, qa_vec3 a) { (void)o; (void)a; return col_trace(ctx, q); }
static double col_tcontents(void *ctx, qa_vec3 p, double m, qa_vec3 o, qa_vec3 a) { (void)o; (void)a; return col_contents(ctx, p, m); }
/* movement */
static qa_bounds mv_move(void *ctx, qa_q3_pres_source_player_state *s, qa_q3_pres_user_command c, qa_q3_pres_movement_options o) {
  (void)s; (void)c; (void)o;
  if (*(uint32_t *)ctx != SENTINEL) abort();
  qa_bounds bounds;
  memset(&bounds, 0, sizeof bounds);
  return bounds;
}
static void mv_angles(void *ctx, qa_q3_pres_source_player_state *s, qa_q3_pres_user_command c) { (void)s; (void)c; if (*(uint32_t *)ctx != SENTINEL) abort(); }
/* fonts */
static bool font_picture(void *ctx, const char *path, const char *mode, qa_picture_asset *out) {
  (void)ctx; (void)path; (void)mode;
  memset(out, 0, sizeof *out);
  return true;
}
static const void *font_register(void *ctx, const char *path, double size, qa_font_print_fn print, void *print_ctx) {
  fixture *f = ctx;
  (void)print; (void)print_ctx;
  if (!path) return NULL;
  size_t index = 0;
  while (index < f->font_count && strcmp(f->font_names[index], path) != 0) index++;
  if (index == f->font_count) {
    if (f->font_count >= 4) abort();
    f->font_count++;
    snprintf(f->font_names[index], 48, "%s", path);
    qa_picture_asset *picture = &f->font_pictures[index];
    memset(picture, 0, sizeof *picture);
    picture->kind = QA_PICTURE_MATERIAL;
    picture->material.name = f->font_names[index];
    qa_q3_registered_font *font = &f->fonts_by_name[index];
    snprintf(font->name, sizeof font->name, "%.40s", path);
    font->glyph_scale = 48.0 / size;
    font->glyph_count = QA_Q3_FONT_GLYPHS;
    for (size_t i = 0; i < QA_Q3_FONT_GLYPHS; i++) {
      font->glyphs[i].metrics.height = 16;
      font->glyphs[i].metrics.x_skip = 8;
      font->glyphs[i].metrics.image_width = 8;
      font->glyphs[i].metrics.image_height = 16;
      font->glyphs[i].picture = picture;
    }
  }
  return &f->fonts_by_name[index];
}
/* ui audio / cinematics */
static void ui_play(void *ctx, qa_ui_runtime_sound_arg s) { (void)s; if (*(uint32_t *)ctx != SENTINEL) abort(); }
static void ui_bg(void *ctx, const char *p) { (void)p; if (*(uint32_t *)ctx != SENTINEL) abort(); }
static void ui_stop_bg(void *ctx) { if (*(uint32_t *)ctx != SENTINEL) abort(); }
static bool cin_play(void *ctx, qa_ui_cinematic_asset a, qa_rect r, qa_ui_cinematic_instance *out) { (void)a; (void)r; (void)out; if (*(uint32_t *)ctx != SENTINEL) abort(); return false; }
static void cin_run(void *ctx, double h, double t) { (void)h; (void)t; if (*(uint32_t *)ctx != SENTINEL) abort(); }
static void cin_draw(void *ctx, double h, qa_rect r, const qa_draw2d *d) { (void)h; (void)r; (void)d; if (*(uint32_t *)ctx != SENTINEL) abort(); }
static void cin_stop(void *ctx, double h) { (void)h; if (*(uint32_t *)ctx != SENTINEL) abort(); }
static bool cin_prepare(void *ctx, const char *p, const qa_ui_cinematic_asset **out) { (void)p; (void)out; if (*(uint32_t *)ctx != SENTINEL) abort(); return false; }
static void cin_stop_slot(void *ctx, double i) { (void)i; if (*(uint32_t *)ctx != SENTINEL) abort(); }
/* scene target */
static char mount_root[64];
static void mount_cleanup(void) {
  char command[128];
  snprintf(command, sizeof command, "rm -rf '%s'", mount_root);
  if (getpid() && mount_root[0] && system(command) != 0) abort();
}
static const qa_q3_pres_fog_selection *scene_fogs(void *ctx, size_t *count) { (void)ctx; *count = 0; return NULL; }
static void scene_print(void *ctx, const char *t) { (void)ctx; (void)t; }
static const qa_actor_id *scene_actor(void *ctx, const qa_q3_pres_ref_model_entity *e) { (void)ctx; (void)e; return NULL; }
static void scene_publish(void *ctx, qa_q3_pres_presented_scene *s) { ((fixture *)ctx)->publish_count++; qa_q3_pres_presented_scene_destroy(s); }

static void write_wav(const char *root, const char *relative) {
  static const uint8_t wav[52] = {'R','I','F','F',44,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
    0x11,0x2B,0,0,0x22,0x56,0,0,2,0,16,0,'d','a','t','a',8,0,0,0,100,0,200,0,0x2C,1,0x90,1};
  char path[512];
  snprintf(path, sizeof path, "%s/%s", root, relative);
  char command[600];
  snprintf(command, sizeof command, "mkdir -p \"$(dirname '%s')\"", path);
  if (system(command) != 0) abort();
  FILE *file = fopen(path, "wb");
  if (!file || fwrite(wav, 1, sizeof wav, file) != sizeof wav) abort();
  fclose(file);
}
static qa_sound_bank *mounted_bank(void) {
  static char root[] = "/tmp/qa-q3-pres-XXXXXX";
  static qa_sound_bank *bank;
  if (bank) return bank;
  if (!mkdtemp(root)) abort();
  write_wav(root, "sound/player/talk.wav");
  write_wav(root, "sound/feedback/one.wav");
  write_wav(root, "sound/feedback/two.wav");
  write_wav(root, "sound/feedback/three.wav");
  qa_content_identity identity = {QA_GAME_FAMILY_Q3, "test", "q3-pres", "1"};
  char *content_id = qa_create_content_id(identity);
  char *mount_id = qa_create_mount_id("q3-pres", "main");
  char *plan_id = qa_create_mount_plan_id("q3-pres", "r1");
  static qa_content_mount mounts[1];
  mounts[0].kind = QA_CONTENT_MOUNT_LOOSE;
  mounts[0].as.loose.identity = qa_create_mount_identity(mount_id, content_id, 0);
  mounts[0].as.loose.root_path = root;
  static qa_mount_id order[1];
  order[0] = mount_id;
  qa_resolved_mount_plan plan = {0};
  plan.id = plan_id;
  plan.mounts = mounts;
  plan.mount_count = 1;
  plan.default_order = order;
  plan.default_order_count = 1;
  qa_open_mount_options options = {0};
  qa_content_mounted *mounted = qa_open_mount_plan(&plan, &options);
  if (!mounted) abort();
  bank = qa_sound_bank_create(mounted);
  if (!bank) abort();
  atexit(mount_cleanup);
  snprintf(mount_root, sizeof mount_root, "%s", root);
  return bank;
}
static fixture *fixture_create(const char *game_type) {
  fixture *f = calloc(1, sizeof *f);
  f->session_sentinel = f->common_sentinel = f->sound_sentinel = f->resources_sentinel = f->sink_sentinel = f->assets_sentinel = SENTINEL;
  f->collision_sentinel = f->movement_sentinel = f->commands_sentinel = f->snapshots_sentinel = f->audio_sentinel = f->cinematics_sentinel = SENTINEL;
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  f->cvars = qa_cvar_registry_create(&cvar_options);
  qa_command_buffer_options buffer_options;
  memset(&buffer_options, 0, sizeof buffer_options);
  buffer_options.dialect = QA_COMMAND_DIALECT_Q3;
  buffer_options.cvars = f->cvars;
  f->buffer = qa_command_buffer_create(&buffer_options);
  for (size_t i = 0; i < 1024; i++) f->strings[i] = "";
  snprintf(f->server_info, sizeof f->server_info, "\\g_gametype\\%s\\mapname\\q3dm1\\sv_maxclients\\8\\timelimit\\0\\fraglimit\\20\\capturelimit\\8", game_type);
  f->strings[0] = f->server_info;
  f->strings[20] = "baseq3-1";
  f->strings[21] = "1234";
  f->bank = qa_q3_pres_presentation_sound_bank_create(mounted_bank(), NULL, bank_load, &f->sound_sentinel);
  qa_font_asset_services services = {f, font_picture, font_register};
  qa_q3_ui_asset_registry_init(&f->fonts, services, NULL, NULL);
  qa_q3_pres_scene_target target;
  memset(&target, 0, sizeof target);
  target.far_clip = 1000;
  target.near_clip = 1;
  target.ctx = f;
  target.fog_selections = scene_fogs;
  target.print = scene_print;
  target.actor = scene_actor;
  target.publish = scene_publish;
  f->recorder = qa_q3_pres_scene_recorder_create(target);
  return f;
}
static qa_q3_pres_presentation_common make_common(fixture *f, qa_q3_base_product product) {
  qa_q3_pres_presentation_common c;
  memset(&c, 0, sizeof c);
  qa_q3_pres_presentation_session *s = &c.session;
  s->product = product;
  s->client_number = 2;
  s->last_executed_server_command = 5;
  s->mode = QA_Q3_PRES_SESSION_LIVE;
  s->commands.ctx = &f->commands_sentinel;
  s->commands.current_number = cmd_number;
  s->commands.read = cmd_read;
  s->snapshots.ctx = &f->snapshots_sentinel;
  s->snapshots.current = snap_current;
  s->snapshots.read = snap_read;
  s->cvars = f->cvars;
  s->ctx = &f->session_sentinel;
  s->server_message_sequence = s_sequence;
  s->get_game_state = s_game_state;
  s->get_server_command = s_server_command;
  s->snapshot_ping = s_ping;
  s->add_reliable_command = s_reliable;
  s->append_console_command = s_console;
  s->register_cgame_command = s_register_command;
  s->set_user_command_value = s_user_command;
  s->assert_current = s_assert_current;
  s->print = s_print;
  c.assets.base.ctx = &f->assets_sentinel;
  c.assets.base.read = a_read;
  c.assets.base.has = a_has;
  c.assets.base.list = a_list;
  c.assets.read_file_length = a_length;
  c.assets.read_sync = a_read;
  qa_q3_pres_renderer_resources *r = &c.resources;
  r->ctx = &f->resources_sentinel;
  r->register_model = r_model;
  r->register_skin = r_skin;
  r->register_shader = r_shader;
  r->register_shader_no_mip = r_shader;
  r->picture = r_picture;
  r->model_handle = r_model_handle;
  r->model_for_handle = r_model_for;
  r->shader_for_handle = r_shader_for;
  r->clear_scene = r_clear;
  r->add_ref_entity = r_add_ref;
  r->add_poly = r_add_poly;
  r->add_light = r_add_light;
  r->remap_shader = r_remap;
  r->render_scene = r_render;
  r->load_world = r_world;
  c.scene = f->recorder;
  c.collision.ctx = &f->collision_sentinel;
  c.collision.trace = col_trace;
  c.collision.point_contents = col_contents;
  c.collision.transformed_trace = col_ttrace;
  c.collision.transformed_point_contents = col_tcontents;
  qa_q3_pres_client_sound *sound = &c.sound;
  sound->bank = f->bank;
  sound->ctx = &f->sound_sentinel;
  sound->start_sound = snd_start;
  sound->start_source_sound = snd_source;
  sound->start_local_sound = snd_local;
  sound->add_loop_sound = snd_loop;
  sound->update_sound_position = snd_update;
  sound->stop_looping_sound = snd_stop;
  sound->clear_looping_sounds = snd_clear;
  sound->set_listener = snd_listener;
  sound->start_background_track = snd_track;
  qa_text_draw_sink sink;
  memset(&sink, 0, sizeof sink);
  sink.ctx = &f->sink_sentinel;
  sink.target.width = 640;
  sink.target.height = 480;
  sink.set_color = sink_color;
  sink.stretch_pixels = sink_stretch;
  c.draw.commands = sink;
  c.draw.space = QA_COORD_STRETCH_640;
  c.font_registry = &f->fonts;
  c.target_width = 640;
  c.target_height = 480;
  c.ctx = &f->common_sentinel;
  c.milliseconds = c_ms;
  c.frame_number = c_frame;
  c.light_for_point = c_light;
  c.memory_remaining = c_memory;
  return c;
}
static qa_q3_pres_client_presentation_options make_primary(fixture *f, qa_q3_base_product product) {
  qa_q3_pres_client_presentation_options o;
  memset(&o, 0, sizeof o);
  o.common = make_common(f, product);
  o.movement.command_timing = "q3";
  o.movement.ctx = &f->movement_sentinel;
  o.movement.move_player = mv_move;
  o.movement.update_view_angles = mv_angles;
  o.update_loading_screen = c_update_loading;
  o.character = c_character;
  o.event = c_event;
  o.predict_item = c_predict;
  o.view_weapon = c_view_weapon;
  o.player_weapon = c_player_weapon;
  if (product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    o.menus_missionpack = true;
    o.menu_commands = f->buffer;
    o.menu_audio.ctx = &f->audio_sentinel;
    o.menu_audio.play_local = ui_play;
    o.menu_audio.start_background = ui_bg;
    o.menu_audio.stop_background = ui_stop_bg;
    o.menu_cinematics.base.ctx = &f->cinematics_sentinel;
    o.menu_cinematics.base.play = cin_play;
    o.menu_cinematics.base.run = cin_run;
    o.menu_cinematics.base.draw = cin_draw;
    o.menu_cinematics.base.stop = cin_stop;
    o.menu_cinematics.owner_ctx = &f->cinematics_sentinel;
    o.menu_cinematics.prepare = cin_prepare;
    o.menu_cinematics.stop_slot = cin_stop_slot;
    o.set_key_catcher = c_key_catcher;
  }
  return o;
}
static qa_q3_pres_retail_snapshot g_snapshot;
/* A prepared snapshot records team/health/etc. through the live record. */
static void attach_snapshot(qa_q3_pres_client_presentation *p) {
  qa_q3_pres_client_game_state *state = qa_q3_pres_client_presentation_state(p);
  memset(&g_snapshot, 0, sizeof g_snapshot);
  CHECK(qa_q3_base_player_state_record_init(&g_snapshot.player_state, state->product, 0, 0, 0, NULL));
  g_snapshot.player_state.client_num = 2;
  state->snap = &g_snapshot;
}
static void detach_snapshot(qa_q3_pres_client_presentation *p) {
  qa_q3_base_player_state_record_destroy(&g_snapshot.player_state);
  qa_q3_pres_client_presentation_state(p)->snap = NULL;
}
static void set_stat(qa_q3_pres_client_game_state *state, double health, double armor, double team) {
  const qa_q3_base_stat_schema *stats = qa_q3_base_stat_schema_for(state->product);
  qa_q3_base_player_state_slots_set(&g_snapshot.player_state.stats, stats->health, health);
  qa_q3_base_player_state_slots_set(&g_snapshot.player_state.stats, stats->armor, armor);
  qa_q3_base_player_state_slots_set(&g_snapshot.player_state.persistant, QA_Q3_BASE_PERS_TEAM, team);
}
static void fixture_clear_draws(fixture *f) { f->draw_count = 0; f->local_sound_count = 0; f->console_count = 0; }
static size_t count_picture(fixture *f, const char *name) {
  size_t n = 0;
  for (size_t i = 0; i < f->draw_count; i++) if (strcmp(f->draws[i].picture, name) == 0) n++;
  return n;
}
static const char *cvar_text(fixture *f, const char *name, char *out, size_t size) {
  qa_cvar_snapshot value;
  out[0] = '\0';
  if (qa_cvar_registry_get(f->cvars, name, &value)) {
    snprintf(out, size, "%s", value.value);
    qa_cvar_read_free(&value);
  }
  return out;
}
static bool near(double a, double b) { return fabs(a - b) < 1e-4; }

/* ---- state.ts / ref-entity.ts (tests/app, gameplay presentation) ---- */
static void test_state_and_ref_entity(void) {
  qa_q3_pres_client_game_state state;
  CHECK(qa_q3_pres_client_game_state_init(&state, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 0));
  CHECK(state.client_num == 0 && state.processed_snapshot_num == 0 && state.snap == NULL);
  CHECK(state.num_scores == 0 && !state.show_scores && state.center_print == NULL);
  CHECK(state.test_model_entity.model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT);
  CHECK(strcmp(state.test_model_entity.model.path, "*default") == 0);
  CHECK(qa_q3_pres_client_game_state_entity_at(&state, 0) == &state.entities[0]);
  CHECK(qa_q3_pres_client_game_state_entity_at(&state, 1023) == &state.entities[1023]);
  CHECK_ABORTS((void)qa_q3_pres_client_game_state_entity_at(&state, 1024));
  CHECK_ABORTS((void)qa_q3_pres_client_game_state_entity_at(&state, -1));
  CHECK_ABORTS((void)qa_q3_pres_client_game_state_entity_at(&state, 0.5));
  qa_q3_pres_client_game_state_destroy(&state);
  qa_q3_pres_client_game_state bad;
  CHECK_ABORTS((void)qa_q3_pres_client_game_state_init(&bad, QA_Q3_BASE_PRODUCT_BASEQ3, 64, 0));
  CHECK_ABORTS((void)qa_q3_pres_client_game_state_init(&bad, QA_Q3_BASE_PRODUCT_BASEQ3, 1.5, 0));
  CHECK_ABORTS((void)qa_q3_pres_client_game_state_init(&bad, QA_Q3_BASE_PRODUCT_BASEQ3, 0, 2147483648.0));
  CHECK(qa_q3_pres_client_game_state_init(&bad, QA_Q3_BASE_PRODUCT_MISSIONPACK, 63, -2147483648.0));
  qa_q3_pres_client_game_state_destroy(&bad);
  qa_q3_pres_client_game_static_state *cgs = calloc(1, sizeof *cgs);
  qa_q3_pres_client_game_static_state_init(cgs, QA_Q3_BASE_PRODUCT_MISSIONPACK);
  CHECK(cgs->game_type == QA_Q3_BASE_GT_FFA && cgs->product == QA_Q3_BASE_PRODUCT_MISSIONPACK);
  CHECK(cgs->game_models[255].kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT && cgs->game_sounds[255] == NULL);
  CHECK(cgs->team_vote_time[1] == 0 && !cgs->team_vote_modified[1]);
  qa_q3_pres_client_game_static_state_destroy(cgs);
  free(cgs);
  /* DEFAULT_MODEL identity and factories. */
  CHECK(qa_q3_pres_default_model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT);
  CHECK(strcmp(qa_q3_pres_default_model.path, "*default") == 0);
  qa_q3_pres_ref_model_entity model = qa_q3_pres_create_model_entity_default();
  CHECK(model.model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT && model.shading.render_flags == 0);
  CHECK(model.shading.custom_shader == NULL && model.custom_skin == NULL && !model.non_normalized_axes);
  CHECK(model.axis.vectors[0].x == 0 && model.shading.shader_rgba.w == 0);
  qa_q3_pres_ref_sprite_entity sprite = qa_q3_pres_create_sprite_entity();
  CHECK(sprite.radius == 0 && sprite.rotation == 0);
  qa_q3_pres_ref_beam_entity beam = qa_q3_pres_create_beam_entity();
  CHECK(beam.radius == 0 && beam.axis.vectors[2].z == 0);
  qa_q3_pres_ref_portal_entity portal = qa_q3_pres_create_portal_entity();
  CHECK(portal.render_flags == 0 && portal.frame == 0);
  CHECK(strcmp(qa_q3_pres_ref_entity_kind_name(qa_q3_pres_ref_entity_from_rail(QA_Q3_PRES_REF_RAIL_CORE, qa_q3_pres_create_rail_core_entity()).kind), "rail-core") == 0);
  CHECK(strcmp(qa_q3_pres_ref_entity_kind_name(qa_q3_pres_ref_entity_from_rail(QA_Q3_PRES_REF_LIGHTNING, qa_q3_pres_create_lightning_entity()).kind), "lightning") == 0);
  CHECK(strcmp(qa_q3_pres_ref_entity_kind_name(qa_q3_pres_ref_entity_from_portal(portal).kind), "portal-surface") == 0);
  CHECK(QA_Q3_PRES_RF_SHADOW_PLANE == 256 && QA_Q3_PRES_RF_LIGHTING_ORIGIN == 128);
  /* copyRefEntity owns a value copy while handle identity is retained. */
  qa_q3_pres_scene_shader shader = {"shader"};
  model.shading.custom_shader = &shader;
  model.origin.x = 3;
  qa_q3_pres_ref_entity tagged = qa_q3_pres_ref_entity_from_model(model);
  qa_q3_pres_ref_entity copy = qa_q3_pres_copy_ref_entity(&tagged);
  tagged.as.model.origin.x = 9;
  CHECK(copy.as.model.origin.x == 3 && copy.as.model.shading.custom_shader == &shader);
  qa_q3_pres_ref_poly_vertex vertices[2];
  memset(vertices, 0, sizeof vertices);
  vertices[1].position.y = 4;
  qa_q3_pres_ref_poly poly = {&shader, vertices, 2}, poly_copy;
  CHECK(qa_q3_pres_copy_ref_poly(&poly, &poly_copy));
  vertices[1].position.y = 8;
  CHECK(poly_copy.vertices != vertices && poly_copy.vertices[1].position.y == 4 && poly_copy.shader == &shader);
  qa_q3_pres_ref_poly_destroy(&poly_copy);
  qa_q3_pres_admitted_ref_entity admitted;
  memset(&admitted, 0, sizeof admitted);
  admitted.is_poly_record = true;
  admitted.record.rotation = 11;
  qa_q3_pres_admitted_ref_entity admitted_copy = qa_q3_pres_copy_admitted_ref_entity(&admitted);
  CHECK(admitted_copy.is_poly_record && admitted_copy.record.rotation == 11);
}

/* ---- client.ts composition ---- */
static void test_client_baseq3(void) {
  fixture *f = fixture_create("0");
  qa_q3_pres_client_presentation *p = qa_q3_pres_create_client_presentation(make_primary(f, QA_Q3_BASE_PRODUCT_BASEQ3));
  CHECK(p != NULL);
  qa_q3_pres_client_game_state *state = qa_q3_pres_client_presentation_state(p);
  qa_q3_pres_client_game_static_state *cgs = qa_q3_pres_client_presentation_static_state(p);
  CHECK(state->client_num == 2 && cgs->server_command_sequence == 5);
  CHECK(state->weapon_select == QA_Q3_WP_MACHINEGUN);
  CHECK(cgs->redflag == -1 && cgs->blueflag == -1 && cgs->flag_status == -1);
  CHECK(cgs->level_start_time == 1234);
  CHECK(strcmp(qa_q3_pres_client_presentation_config_string(p, 20), "baseq3-1") == 0);
  CHECK_ABORTS((void)qa_q3_pres_client_presentation_config_string(p, 1024));
  CHECK_ABORTS((void)qa_q3_pres_client_presentation_config_string(p, -1));
  CHECK_ABORTS((void)qa_q3_pres_client_presentation_config_string(p, 0.5));
  CHECK(f->registered_command_count > 0);
  bool saw_score = false;
  for (size_t i = 0; i < f->registered_command_count; i++) if (strcmp(f->registered_commands[i], "+scores") == 0) saw_score = true;
  CHECK(saw_score);
  CHECK(f->loading_updates > 0);
  CHECK(f->assert_calls > 0);
  CHECK(qa_q3_pres_client_presentation_hud(p) != NULL && qa_q3_pres_client_presentation_status(p) != NULL);
  CHECK(qa_q3_pres_client_presentation_menus(p) == NULL && qa_q3_pres_client_presentation_console(p) != NULL);
  /* TS: key/mouse/event helpers assert current; baseq3 has no menus. */
  size_t before = f->assert_calls;
  qa_q3_pres_client_presentation_key_event(p, 13, true);
  qa_q3_pres_client_presentation_mouse_event(p, 1, 2);
  qa_q3_pres_client_presentation_event_handling(p, 0);
  CHECK(f->assert_calls == before + 3);
  /* Mission console commands are not part of the base cgame's table. */
  const char *const load_hud[] = {"loadhud"};
  CHECK(!qa_q3_pres_client_console_runtime_handles(qa_q3_pres_client_presentation_console(p), load_hud[0]));
  CHECK(!qa_q3_pres_client_console_runtime_execute(qa_q3_pres_client_presentation_console(p), load_hud, 1));
  /* The drawing queue reaches the sentinel sink through composed tools. */
  fixture_clear_draws(f);
  qa_q3_pres_client_draw_tools *tools = qa_q3_pres_client_presentation_tools(p);
  qa_rect rect = {10, 20, 30, 40};
  qa_q3_pres_client_draw_tools_draw_pic(tools, rect, qa_q3_pres_client_presentation_media(p)->graphics.white_shader);
  CHECK(f->draw_count == 1 && strcmp(f->draws[0].picture, "white") == 0);
  /* TS cgame print/console closures. */
  qa_q3_pres_client_presentation_close(p);
  qa_q3_pres_client_presentation_close(p);
  qa_q3_pres_client_presentation_destroy(p);
  /* Version mismatch is a CommonError drop. */
  fixture *mismatch = fixture_create("0");
  mismatch->strings[20] = "other";
  CHECK_ABORTS((void)qa_q3_pres_create_client_presentation(make_primary(mismatch, QA_Q3_BASE_PRODUCT_BASEQ3)));
  /* An initialization that observes a later message sequence fails. */
  fixture *stale = fixture_create("0");
  stale->message_sequence = 0;
  stale->assert_stale = true;
  CHECK_ABORTS((void)qa_q3_pres_create_client_presentation(make_primary(stale, QA_Q3_BASE_PRODUCT_BASEQ3)));
}
static void test_client_scene(void) {
  fixture *f = fixture_create("0");
  qa_q3_pres_scene_presentation_options options;
  memset(&options, 0, sizeof options);
  options.common = make_common(f, QA_Q3_BASE_PRODUCT_BASEQ3);
  options.center_print = c_scene_center;
  qa_q3_pres_client_presentation *p = qa_q3_pres_create_scene_presentation(options);
  CHECK(p != NULL);
  CHECK(qa_q3_pres_client_presentation_hud(p) == NULL && qa_q3_pres_client_presentation_status(p) == NULL);
  CHECK(qa_q3_pres_client_presentation_console(p) == NULL && qa_q3_pres_client_presentation_prediction(p) == NULL);
  CHECK(qa_q3_pres_client_presentation_frames(p)->scene_scope);
  CHECK(f->registered_command_count == 0);
  CHECK(f->loading_updates == 0);
  qa_q3_pres_snapshot_runtime *snapshots = qa_q3_pres_client_presentation_snapshots(p);
  CHECK(snapshots->host.no_predict);
  /* TS centerPrint on a scene: forwards time and cg_centertime * 1000. */
  qa_q3_pres_client_presentation_state(p)->time = 777;
  qa_q3_pres_client_event_runtime *events = qa_q3_pres_client_presentation_events(p);
  events->host.center_print(events->host.ctx, "hello", 100, 8);
  CHECK(f->scene_center_prints == 1 && f->scene_center_time == 777 && near(f->scene_center_duration, 3000));
  /* Scene frame hosts carry no primary owners. */
  CHECK(qa_q3_pres_client_presentation_frames(p)->host.hud == NULL);
  CHECK(qa_q3_pres_client_presentation_frames(p)->host.set_user_command_value == NULL);
  qa_q3_pres_client_presentation_close(p);
  qa_q3_pres_client_presentation_destroy(p);
}

/* ---- hud.ts ---- */
static void test_hud_baseq3(void) {
  fixture *f = fixture_create("0");
  qa_q3_pres_client_presentation *p = qa_q3_pres_create_client_presentation(make_primary(f, QA_Q3_BASE_PRODUCT_BASEQ3));
  CHECK(p != NULL);
  qa_q3_pres_client_hud *hud = qa_q3_pres_client_presentation_hud(p);
  qa_q3_pres_client_game_state *state = qa_q3_pres_client_presentation_state(p);
  qa_q3_pres_client_game_static_state *cgs = qa_q3_pres_client_presentation_static_state(p);
  qa_q3_pres_client_media *media = qa_q3_pres_client_presentation_media(p);
  CHECK_ABORTS(qa_q3_pres_client_hud_draw_follow(hud)); /* no snapshot */
  attach_snapshot(p);
  CHECK(!qa_q3_pres_client_hud_draw_follow(hud));
  /* Follow text: drawBigString then forced-color 32x48 name. */
  CHECK(qa_q3_pres_set_string(&cgs->client_info[2].name, "abc"));
  g_snapshot.player_state.pm_flags = QA_Q3_FLAG_FOLLOW;
  fixture_clear_draws(f);
  CHECK(qa_q3_pres_client_hud_draw_follow(hud));
  CHECK(f->draw_count > 3);
  draw_record *last = &f->draws[f->draw_count - 1];
  CHECK(last->rect.width == 32 && last->rect.height == 48 && last->rect.y == 40);
  CHECK(last->rect.x == trunc(0.5 * (640 - 32 * 3)) + 64);
  g_snapshot.player_state.pm_flags = 0;
  /* Spectator: three text lines by game type. */
  fixture_clear_draws(f);
  cgs->game_type = QA_Q3_BASE_GT_FFA;
  qa_q3_pres_client_hud_draw_spectator(hud);
  size_t spectator_only = f->draw_count;
  CHECK(spectator_only > 0);
  fixture_clear_draws(f);
  cgs->game_type = QA_Q3_BASE_GT_TOURNAMENT;
  qa_q3_pres_client_hud_draw_spectator(hud);
  CHECK(f->draw_count > spectator_only);
  fixture_clear_draws(f);
  cgs->game_type = QA_Q3_BASE_GT_TEAM;
  qa_q3_pres_client_hud_draw_spectator(hud);
  CHECK(f->draw_count > spectator_only);
  cgs->game_type = QA_Q3_BASE_GT_FFA;
  /* Vote: zero time draws nothing; modified plays talk sound on channel 6. */
  fixture_clear_draws(f);
  qa_q3_pres_client_hud_draw_vote(hud);
  CHECK(f->draw_count == 0);
  cgs->vote_time = 500;
  cgs->vote_modified = true;
  state->time = 1500;
  CHECK(qa_q3_pres_set_string(&cgs->vote_string, "map q3dm1"));
  cgs->vote_yes = 2;
  cgs->vote_no = 1;
  qa_q3_pres_client_hud_draw_vote(hud);
  CHECK(!cgs->vote_modified && f->local_sound_count == 1 && f->last_local_channel == 6);
  CHECK(f->last_local_sound != NULL && f->last_local_sound == media->sounds.talk_sound);
  CHECK(f->draw_count == strlen("VOTE(29):map q3dm1 yes:2 no:1") - 3);
  CHECK(f->draws[0].rect.y == 58);
  /* Team vote reads client 0's team. */
  fixture_clear_draws(f);
  cgs->client_info[0].team = QA_Q3_BASE_TEAM_SPECTATOR;
  cgs->team_vote_time[0] = 1;
  qa_q3_pres_client_hud_draw_team_vote(hud);
  CHECK(f->draw_count == 0);
  cgs->client_info[0].team = QA_Q3_BASE_TEAM_RED;
  cgs->team_vote_modified[0] = true;
  qa_q3_pres_client_hud_draw_team_vote(hud);
  CHECK(f->draw_count > 0 && f->draws[0].rect.y == 90 && f->local_sound_count == 1 && !cgs->team_vote_modified[0]);
  /* Ammo warning text and centering. */
  fixture_clear_draws(f);
  state->low_ammo_warning = 0;
  qa_q3_pres_client_hud_draw_ammo_warning(hud);
  CHECK(f->draw_count == 0);
  state->low_ammo_warning = 2;
  qa_q3_pres_client_hud_draw_ammo_warning(hud);
  CHECK(f->draw_count == strlen("OUTOFAMMO") * 2);
  CHECK(f->draws[0].rect.y == 66 && f->draws[f->draw_count - 1].rect.y == 64);
  state->low_ammo_warning = 0;
  /* Rewards: stack shift, time reset, sound on channel 7, icon row. */
  fixture_clear_draws(f);
  qa_pcm_sound first, second;
  qa_q3_pres_scene_shader medal = {"medal"};
  state->rewards[0].count = 1;
  state->rewards[0].sound = &first;
  state->rewards[1].sound = &second;
  state->rewards[1].shader = &medal;
  state->rewards[1].count = 2;
  state->reward_stack = 1;
  state->reward_time = 0;
  state->time = 9000;
  qa_q3_pres_client_hud_draw_reward(hud);
  CHECK(state->reward_stack == 0 && state->reward_time == 9000);
  CHECK(f->last_local_sound == &second && f->last_local_channel == 7);
  CHECK(state->rewards[0].count == 2 && state->rewards[0].shader == &medal);
  CHECK(count_picture(f, "medal") == 2);
  CHECK(f->draws[0].rect.x == 320 - 2 * 24 && f->draws[1].rect.x == 320 - 2 * 24 + 48 && f->draws[0].rect.y == 56);
  /* count >= 10: icon plus centered 8x16 number. */
  fixture_clear_draws(f);
  state->rewards[0].count = 12;
  state->reward_time = 9000;
  qa_q3_pres_client_hud_draw_reward(hud);
  CHECK(count_picture(f, "medal") == 1 && f->draws[0].rect.x == 296);
  CHECK(f->draw_count >= 3 && f->draws[f->draw_count - 1].rect.width == 8 && f->draws[f->draw_count - 1].rect.height == 16);
  /* A full reward stack reads past the source array. */
  state->reward_time = 0;
  state->reward_stack = 10;
  CHECK_ABORTS(qa_q3_pres_client_hud_draw_reward(hud));
  state->reward_stack = 0;
  /* Crosshair: spectator or third person suppressed; index modulo 10, health tint. */
  fixture_clear_draws(f);
  set_stat(state, 100, 0, QA_Q3_BASE_TEAM_SPECTATOR);
  qa_q3_pres_client_hud_draw_crosshair(hud);
  CHECK(f->draw_count == 0);
  set_stat(state, 100, 0, QA_Q3_BASE_TEAM_FREE);
  state->rendering_third_person = true;
  qa_q3_pres_client_hud_draw_crosshair(hud);
  CHECK(f->draw_count == 0);
  state->rendering_third_person = false;
  state->refdef.x = 10;
  state->refdef.y = 20;
  state->refdef.width = 620;
  state->refdef.height = 400;
  qa_q3_pres_client_hud_draw_crosshair(hud);
  CHECK(f->draw_count == 1);
  /* cg_drawCrosshair default 4 selects crosshaird; cg_crosshairSize 24 at scale 1. */
  CHECK(strcmp(f->draws[0].picture, media->graphics.crosshair_shader[4]->name) == 0);
  CHECK(near(f->draws[0].rect.width, 24));
  CHECK(near(f->draws[0].rect.x, 10 + 0.5 * (620 - 24)));
  CHECK(near(f->draws[0].rect.y, 20 + 0.5 * (400 - 24)));
  /* Item pickup blend grows size inside 200 ms. */
  fixture_clear_draws(f);
  state->time = 9100;
  state->item_pickup_blend_time = 9000;
  qa_q3_pres_client_hud_draw_crosshair(hud);
  CHECK(near(f->draws[0].rect.width, 24 * 1.5));
  state->item_pickup_blend_time = 0;
  /* Crosshair scan: trace past 64 keeps state; a player hit records time. */
  state->crosshair_client_num = 0;
  state->crosshair_client_time = 0;
  qa_q3_pres_client_hud_scan_for_crosshair_entity(hud);
  /* col_trace world hit reports ENTITYNUM from prediction: no player selected. */
  CHECK(state->crosshair_client_time == 0);
  /* Warmup headings, countdown sounds and fixed sizes. */
  fixture_clear_draws(f);
  state->warmup = 0;
  qa_q3_pres_client_hud_draw_warmup(hud);
  CHECK(f->draw_count == 0);
  state->warmup = -1;
  state->warmup_count = 5;
  qa_q3_pres_client_hud_draw_warmup(hud);
  CHECK(state->warmup_count == 0 && f->draw_count > 0);
  fixture_clear_draws(f);
  cgs->game_type = QA_Q3_BASE_GT_FFA;
  state->time = 10000;
  state->warmup = 12500;
  state->warmup_count = 9;
  qa_q3_pres_client_hud_draw_warmup(hud);
  CHECK(state->warmup_count == 2);
  CHECK(f->last_local_sound != NULL && f->last_local_sound == media->sounds.count3_sound && f->last_local_channel == 7);
  /* Heading "Free For All" (32 x trunc(32*1.1)=35) then 20x30 countdown. */
  CHECK(f->draws[0].rect.width == 32 && f->draws[0].rect.height == 35 && f->draws[0].rect.y == 27);
  draw_record *count_glyph = &f->draws[f->draw_count - 1];
  CHECK(count_glyph->rect.width == 20 && count_glyph->rect.height == 30);
  fixture_clear_draws(f);
  state->warmup = 9000;
  state->warmup_count = 0;
  qa_q3_pres_client_hud_draw_warmup(hud);
  CHECK(state->warmup == 0);
  /* Tournament without two players omits its heading. */
  fixture_clear_draws(f);
  cgs->game_type = QA_Q3_BASE_GT_TOURNAMENT;
  cgs->maxclients = 2;
  cgs->client_info[0].info_valid = false;
  cgs->client_info[1].info_valid = false;
  state->warmup = 12500;
  state->warmup_count = 2;
  qa_q3_pres_client_hud_draw_warmup(hud);
  CHECK(f->draws[0].rect.width == 20);
  /* Two clients: heading is "a vs b" at y 20. */
  fixture_clear_draws(f);
  cgs->client_info[0].info_valid = true;
  cgs->client_info[0].team = QA_Q3_BASE_TEAM_FREE;
  CHECK(qa_q3_pres_set_string(&cgs->client_info[0].name, "a"));
  cgs->client_info[1].info_valid = true;
  cgs->client_info[1].team = QA_Q3_BASE_TEAM_FREE;
  CHECK(qa_q3_pres_set_string(&cgs->client_info[1].name, "b"));
  qa_q3_pres_client_hud_draw_warmup(hud);
  CHECK(f->draws[0].rect.width == 32 && f->draws[0].rect.height == 48);
  cgs->game_type = QA_Q3_BASE_GT_FFA;
  state->warmup = 0;
  /* Status bar head idle path. */
  state->damage_time = 0;
  state->head_end_time = 0;
  state->time = 2000;
  fixture_clear_draws(f);
  qa_q3_pres_client_hud_draw_status_bar_head(hud, 285);
  CHECK(state->head_start_time == 0 && state->head_end_time >= 2100 && state->head_end_time <= 4100);
  /* Damage path: expected values printed by bun from the TS statements and
   * GameRandom(0): {x:270,size:84,startYaw:191.25,endYaw:160,
   * endPitch:-3.8952205181121826,endTime:1161}. */
  qa_game_random_reset(hud->host.random, 0);
  state->time = 1000;
  state->damage_time = 900;
  state->damage_x = 0.25;
  state->head_start_time = 0;
  qa_q3_pres_client_hud_draw_status_bar_head(hud, 285);
  CHECK(state->head_start_yaw == 191.25 && state->head_end_yaw == 160);
  CHECK(state->head_end_pitch == -3.8952205181121826 && state->head_end_time == 1161 && state->head_start_time == 1000);
  state->damage_time = 0;
  state->damage_x = 0;
  state->time = 2000;
  /* Status bar honours cg_drawStatus and paints health then armor fields. */
  fixture_clear_draws(f);
  set_stat(state, 50, 20, QA_Q3_BASE_TEAM_FREE);
  state->predicted_player_state.weapon = 0;
  qa_q3_pres_client_hud_draw_status_bar(hud);
  size_t armored = f->draw_count;
  CHECK(armored > 0);
  fixture_clear_draws(f);
  set_stat(state, 50, 0, QA_Q3_BASE_TEAM_FREE);
  qa_q3_pres_client_hud_draw_status_bar(hud);
  CHECK(f->draw_count < armored);
  /* Intermission path: base single player only center-prints. */
  state->score_fade_time = 0;
  cgs->game_type = QA_Q3_BASE_GT_SINGLE_PLAYER;
  qa_q3_pres_client_hud_draw_intermission(hud);
  CHECK(state->score_fade_time == 0);
  cgs->game_type = QA_Q3_BASE_GT_FFA;
  qa_q3_pres_client_hud_draw_intermission(hud);
  CHECK(state->score_fade_time == state->time);
  /* draw2D: levelShot returns before reading the snapshot; order pending ignored in base. */
  state->level_shot = true;
  fixture_clear_draws(f);
  qa_q3_pres_client_hud_draw_2d(hud);
  CHECK(f->draw_count == 0);
  state->level_shot = false;
  /* draw2D: spectator branch draws SPECTATOR and no status bar. */
  set_stat(state, 100, 0, QA_Q3_BASE_TEAM_SPECTATOR);
  fixture_clear_draws(f);
  qa_q3_pres_client_hud_draw_2d(hud);
  CHECK(f->draw_count > 0);
  CHECK(!state->show_scores);
  /* Mission-only operations abort in baseq3. */
  CHECK_ABORTS(qa_q3_pres_client_hud_draw_prox_warning(hud));
  CHECK_ABORTS(qa_q3_pres_client_hud_draw_timed_menus(hud));
  /* Init rejects mixed owners. */
  qa_q3_pres_client_hud_host mixed = hud->host;
  qa_q3_pres_client_game_state *other = calloc(1, sizeof *other);
  qa_q3_pres_client_game_static_state *other_static = calloc(1, sizeof *other_static);
  other->product = QA_Q3_BASE_PRODUCT_BASEQ3;
  other_static->product = QA_Q3_BASE_PRODUCT_BASEQ3;
  qa_q3_pres_client_hud probe;
  CHECK_ABORTS(qa_q3_pres_client_hud_init(&probe, other, other_static, mixed, hud->variant));
  qa_q3_pres_client_hud_variant wrong = hud->variant;
  wrong.kind = QA_Q3_BASE_PRODUCT_MISSIONPACK;
  CHECK_ABORTS(qa_q3_pres_client_hud_init(&probe, state, cgs, mixed, wrong));
  free(other);
  free(other_static);
  detach_snapshot(p);
  qa_q3_pres_client_presentation_close(p);
  qa_q3_pres_client_presentation_destroy(p);
}

/* ---- mission-hud.ts ---- */
static const char HUD_SET[] = "{\n  loadMenu { \"ui/score.menu\" }\n}\n";
/* Repeats every string, including menu and item names, on one first load. */
static const char REPEATED_MENU[] =
    "  menuDef { name \"dup\" rect 0 0 10 10 itemDef { name \"dup\" group \"dup\" rect 0 0 1 1 } itemDef { name \"dup\" group \"dup\" rect 0 0 1 1 } }\n"
    "  menuDef { name \"dup\" rect 0 0 10 10 }\n";
static const char GRADIENT_MENU[] =
    "  assetGlobalDef { gradientBar \"ui/assets/gradientbar4.tga\" }\n"
    "  menuDef { name \"score_menu\" rect 0 0 640 480 itemDef { name \"bar\" rect 0 0 64 64 style 2 backcolor 1 1 1 1 visible 1 } }\n";
static const char LABEL_MENU[] =
    "  assetGlobalDef { smallFont \"fonts/small\" 12 font \"fonts/normal\" 16 bigFont \"fonts/big\" 24 }\n"
    "  menuDef { name \"score_menu\" rect 0 0 640 480 itemDef { name \"label\" text \"Hi\" type 0 rect 0 0 64 64 textscale 0.3 forecolor 1 1 1 1 visible 1 } }\n";
static const char SCORE_MENU[] =
    "  assetGlobalDef {\n"
    "    smallFont \"fonts/small\" 12\n"
    "    font \"fonts/normal\" 16\n"
    "    bigFont \"fonts/big\" 24\n"
    "    gradientBar \"ui/assets/gradientbar3.tga\"\n"
    "  }\n"
    "  menuDef {\n"
    "    name \"score_menu\"\n"
    "    rect 0 0 640 480\n"
    "    itemDef { name \"list\" type 6 feeder 11 rect 0 0 300 300 elementheight 16 visible 1 }\n"
    "  }\n"
    "  menuDef { name \"teamscore_menu\" rect 0 0 640 480 }\n"
    "  menuDef { name \"voiceMenu\" rect 0 0 100 100 }\n"
    "  menuDef { name \"teamMenu\" rect 0 0 100 100 }\n"
    "  menuDef { name \"getMenu\" rect 0 0 100 100 }\n";
static qa_q3_pres_client_presentation *mission_create(fixture *f) {
  f->hud_text = HUD_SET;
  f->score_text = SCORE_MENU;
  f->presentation_mission = true;
  qa_q3_pres_client_presentation_options options = make_primary(f, QA_Q3_BASE_PRODUCT_MISSIONPACK);
  options.weapon_hud = f->weapon_hud_on ? c_weapon_hud : NULL;
  return qa_q3_pres_create_client_presentation(options);
}
/* Mission first load that repeats one string many times. */
static void test_first_load_repeated_string(void) {
  fixture *f = fixture_create("0");
  f->hud_text = HUD_SET;
  f->score_text = REPEATED_MENU;
  f->presentation_mission = true;
  qa_q3_pres_client_presentation *p = qa_q3_pres_create_client_presentation(make_primary(f, QA_Q3_BASE_PRODUCT_MISSIONPACK));
  CHECK(p != NULL);
  qa_q3_pres_mission_hud *hud = qa_q3_pres_client_presentation_menus(p);
  qa_ui_runtime_snapshot snapshot;
  CHECK(qa_q3_pres_mission_hud_menu_state(hud, &snapshot));
  CHECK(snapshot.menu_count == 2);
  CHECK(snapshot.menus[0].name && strcmp(snapshot.menus[0].name, "dup") == 0);
  CHECK(snapshot.menus[1].name && strcmp(snapshot.menus[1].name, "dup") == 0);
  CHECK(snapshot.menus[0].item_count == 2 && strcmp(snapshot.menus[0].items[1].group, "dup") == 0);
  qa_ui_runtime_snapshot_destroy(&snapshot);
  qa_q3_pres_client_presentation_close(p);
  qa_q3_pres_client_presentation_destroy(p);
}
/* TS reads fonts and widget assets through getters, so changes reach the runtime. */
static void test_reload_live_options(void) {
  fixture *f = fixture_create("0");
  f->hud_text = HUD_SET;
  f->score_text = LABEL_MENU;
  f->presentation_mission = true;
  qa_q3_pres_client_presentation *p = qa_q3_pres_create_client_presentation(make_primary(f, QA_Q3_BASE_PRODUCT_MISSIONPACK));
  CHECK(p != NULL);
  qa_q3_pres_mission_hud *hud = qa_q3_pres_client_presentation_menus(p);
  qa_q3_pres_client_game_state *state = qa_q3_pres_client_presentation_state(p);
  attach_snapshot(p);
  state->show_scores = true;
  /* textscale 0.3 is between the thresholds: the normal font. */
  fixture_clear_draws(f);
  CHECK(qa_q3_pres_mission_hud_draw_scoreboard(hud));
  CHECK(count_picture(f, "fonts/normal") == 2 && count_picture(f, "fonts/big") == 0);
  /* A threshold cvar change after load selects the big font on the next paint. */
  qa_cvar_snapshot value;
  CHECK(qa_cvar_registry_set(f->cvars, "ui_bigFont", "0.2", true, &value));
  qa_cvar_read_free(&value);
  CHECK(qa_q3_pres_client_configuration_update_cvars(hud->host.configuration));
  fixture_clear_draws(f);
  CHECK(qa_q3_pres_mission_hud_draw_scoreboard(hud));
  CHECK(count_picture(f, "fonts/big") == 2 && count_picture(f, "fonts/normal") == 0);
  /* A reload that changes gradientBar paints the new picture. */
  f->score_text = GRADIENT_MENU;
  qa_ui_runtime *runtime = hud->runtime;
  CHECK(qa_q3_pres_mission_hud_load_hud_menu(hud));
  CHECK(hud->runtime == runtime);
  CHECK(strcmp(hud->widget_assets.gradient_bar.material.name, "ui/assets/gradientbar4.tga") == 0);
  qa_q3_pres_mission_hud_clear_scoreboard(hud);
  fixture_clear_draws(f);
  CHECK(qa_q3_pres_mission_hud_draw_scoreboard(hud));
  CHECK(count_picture(f, "ui/assets/gradientbar4.tga") == 1);
  CHECK(count_picture(f, "ui/assets/gradientbar2.tga") == 0);
  state->show_scores = false;
  detach_snapshot(p);
  qa_q3_pres_client_presentation_close(p);
  qa_q3_pres_client_presentation_destroy(p);
}
/* frame.ts line: PERS_TEAM spectator with MoveFlags.SCOREBOARD takes the tourney path. */
static size_t frame_publishes(qa_q3_base_product product, double team_index, double team, double flags, double weapons, size_t *models) {
  fixture *f = fixture_create("0");
  f->presentation_mission = product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  f->hud_text = HUD_SET;
  f->score_text = SCORE_MENU;
  f->wire_enabled = true;
  f->wire_team_index = team_index;
  f->wire_team = team;
  f->wire_pm_flags = flags;
  f->wire_weapons = weapons;
  qa_q3_pres_client_presentation *p = qa_q3_pres_create_client_presentation(make_primary(f, product));
  CHECK(p != NULL);
  size_t before = f->model_calls;
  qa_q3_pres_presentation_frame input = {1000, QA_Q3_PRES_STEREO_CENTER, false, 2};
  CHECK(qa_q3_pres_presentation_frame_runtime_draw_active_frame(qa_q3_pres_client_presentation_frames(p), input));
  if (models) *models = f->model_calls - before;
  size_t published = f->publish_count;
  qa_q3_pres_client_presentation_close(p);
  qa_q3_pres_client_presentation_destroy(p);
  return published;
}
static void test_frame_indices(void) {
  CHECK(QA_Q3_BASE_PERS_TEAM == 3 && QA_Q3_FLAG_SCOREBOARD == 8192);
  qa_q3_base_product base = QA_Q3_BASE_PRODUCT_BASEQ3, mission = QA_Q3_BASE_PRODUCT_MISSIONPACK;
  /* Tourney path returns before renderScene. */
  CHECK(frame_publishes(base, QA_Q3_BASE_PERS_TEAM, QA_Q3_BASE_TEAM_SPECTATOR, QA_Q3_FLAG_SCOREBOARD, 0, NULL) == 0);
  CHECK(frame_publishes(mission, QA_Q3_BASE_PERS_TEAM, QA_Q3_BASE_TEAM_SPECTATOR, QA_Q3_FLAG_SCOREBOARD, 0, NULL) == 0);
  /* Spectator without the flag, and the old flag value 32, render normally. */
  CHECK(frame_publishes(base, QA_Q3_BASE_PERS_TEAM, QA_Q3_BASE_TEAM_SPECTATOR, 0, 0, NULL) == 1);
  CHECK(frame_publishes(base, QA_Q3_BASE_PERS_TEAM, QA_Q3_BASE_TEAM_SPECTATOR, 32, 0, NULL) == 1);
  /* The old index 1 (PERS_HITS) no longer selects the path. */
  CHECK(frame_publishes(base, QA_Q3_BASE_PERS_HITS, QA_Q3_BASE_TEAM_SPECTATOR, QA_Q3_FLAG_SCOREBOARD, 0, NULL) == 1);
  /* statSchema(product).weapons bits register owned weapons in both products. */
  size_t none = 0, owned = 0;
  (void)frame_publishes(base, QA_Q3_BASE_PERS_TEAM, QA_Q3_BASE_TEAM_FREE, 0, 0, &none);
  (void)frame_publishes(base, QA_Q3_BASE_PERS_TEAM, QA_Q3_BASE_TEAM_FREE, 0, 1 << 3, &owned);
  CHECK(owned > none);
  (void)frame_publishes(mission, QA_Q3_BASE_PERS_TEAM, QA_Q3_BASE_TEAM_FREE, 0, 0, &none);
  (void)frame_publishes(mission, QA_Q3_BASE_PERS_TEAM, QA_Q3_BASE_TEAM_FREE, 0, 1 << 3, &owned);
  CHECK(owned > none);
}
/* mission-owner-draw.ts paint honors weaponHud; one visible/statusHandle. */
static qa_ui_owner_draw_paint_request owner_request(qa_q3_pres_client_presentation *p, double id, double flags) {
  qa_ui_owner_draw_paint_request request;
  memset(&request, 0, sizeof request);
  request.draw = &qa_q3_pres_client_presentation_tools(p)->draw;
  request.rect.x = 100;
  request.rect.y = 200;
  request.rect.width = 64;
  request.rect.height = 32;
  request.owner_draw = id;
  request.owner_draw_flags = flags;
  request.text_scale = 0.3;
  request.color.x = request.color.y = request.color.z = request.color.w = 1;
  return request;
}
static void owner_paint_case(bool weapon_hud) {
  fixture *f = fixture_create("0");
  f->weapon_hud_on = weapon_hud;
  qa_q3_pres_client_presentation *p = mission_create(f);
  CHECK(p != NULL);
  qa_q3_pres_mission_hud *hud = qa_q3_pres_client_presentation_menus(p);
  qa_q3_pres_mission_owner_draw *owner = &hud->owner_draw;
  qa_q3_pres_client_game_state *state = qa_q3_pres_client_presentation_state(p);
  attach_snapshot(p);
  static qa_q3_pres_scene_shader ammo = {"ammo-icon"};
  qa_q3_pres_client_weapon_info *info = qa_q3_pres_client_weapon_media_registry_weapon(hud->host.weapon_registry, 2);
  info->ammo_icon = &ammo;
  info->ammo_model.kind = QA_Q3_PRES_SCENE_MODEL_LOADED;
  info->ammo_model.path = "models/ammo";
  qa_q3_pres_client_game_state_entity_at(state, 2)->current_state.weapon = 2;
  state->predicted_player_state.weapon = 2;
  qa_q3_base_player_state_slots_set(&g_snapshot.player_state.ammo, 2, 7);
  /* ICON2D and the 2DONLY flag draw the ammo icon unless weaponHud owns it. */
  fixture_clear_draws(f);
  qa_ui_owner_draw_paint_request request = owner_request(p, QA_Q3_PRES_OD_PLAYER_AMMO_ICON2D, 0);
  qa_q3_pres_mission_owner_draw_paint(owner, &request);
  CHECK(count_picture(f, "ammo-icon") == (weapon_hud ? 0u : 1u));
  fixture_clear_draws(f);
  request = owner_request(p, QA_Q3_PRES_OD_PLAYER_AMMO_ICON, QA_Q3_PRES_OD_SHOW_2DONLY);
  qa_q3_pres_mission_owner_draw_paint(owner, &request);
  CHECK(count_picture(f, "ammo-icon") == (weapon_hud ? 0u : 1u));
  /* Without 2D the default cg_draw3dIcons renders the model. */
  fixture_clear_draws(f);
  size_t rendered = f->render_count;
  request = owner_request(p, QA_Q3_PRES_OD_PLAYER_AMMO_ICON, 0);
  qa_q3_pres_mission_owner_draw_paint(owner, &request);
  CHECK(f->render_count == rendered + (weapon_hud ? 0u : 1u) && f->draw_count == 0);
  /* Ammo value draws centered digits only without weaponHud. */
  fixture_clear_draws(f);
  request = owner_request(p, QA_Q3_PRES_OD_PLAYER_AMMO_VALUE, 0);
  qa_q3_pres_mission_owner_draw_paint(owner, &request);
  CHECK(f->draw_count == (weapon_hud ? 0u : 1u));
  /* The query reads the reader (17) or the ammo slot (7). */
  CHECK(qa_q3_pres_mission_owner_draw_value(owner, QA_Q3_PRES_OD_PLAYER_AMMO_VALUE) == (weapon_hud ? 17 : 7));
  /* A background picture replaces the digits; weapon 0 paints nothing. */
  fixture_clear_draws(f);
  request.background.kind = QA_UI_PICTURE_VALUE;
  request.background.picture.kind = QA_PICTURE_MATERIAL;
  request.background.picture.material.name = "number-bg";
  qa_q3_pres_mission_owner_draw_paint(owner, &request);
  CHECK(count_picture(f, "number-bg") == (weapon_hud ? 0u : 1u));
  qa_q3_pres_client_game_state_entity_at(state, 2)->current_state.weapon = 0;
  fixture_clear_draws(f);
  qa_q3_pres_mission_owner_draw_paint(owner, &request);
  CHECK(f->draw_count == 0);
  /* cg_drawStatus 0 suppresses paint; status_handle has one implementation. */
  CHECK(qa_q3_pres_mission_owner_draw_status_handle(owner, 7) == hud->media->graphics.camp_shader);
  CHECK(qa_q3_pres_mission_owner_draw_visible(owner, QA_Q3_PRES_OD_SHOW_HEALTHOK) ==
        (qa_q3_base_player_state_slots_get(&g_snapshot.player_state.stats, qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_MISSIONPACK)->health) >= 25));
  detach_snapshot(p);
  qa_q3_pres_client_presentation_close(p);
  qa_q3_pres_client_presentation_destroy(p);
}
static void test_mission_hud(void) {
  fixture *f = fixture_create("0");
  qa_q3_pres_client_presentation *p = mission_create(f);
  CHECK(p != NULL);
  qa_q3_pres_mission_hud *hud = qa_q3_pres_client_presentation_menus(p);
  CHECK(hud != NULL);
  qa_q3_pres_client_game_state *state = qa_q3_pres_client_presentation_state(p);
  qa_q3_pres_client_game_static_state *cgs = qa_q3_pres_client_presentation_static_state(p);
  qa_q3_pres_client_media *media = qa_q3_pres_client_presentation_media(p);
  char text[256];
  /* assetCache and loadHudMenu ran in composition order. */
  qa_q3_pres_mission_hud_cached_assets cached = qa_q3_pres_mission_hud_cached(hud);
  CHECK(cached.fx_base && strcmp(cached.fx_base->name, "menu/art/fx_base") == 0);
  CHECK(cached.fx_colors[6] && strcmp(cached.fx_colors[6]->name, "menu/art/fx_white") == 0);
  CHECK(strcmp(cached.widgets.scroll_bar.material.name, "ui/assets/scrollbar.tga") == 0);
  CHECK(strcmp(cached.widgets.slider_thumb.material.name, "ui/assets/sliderbutt_1.tga") == 0);
  CHECK(strcmp(cached.widgets.gradient_bar.material.name, "ui/assets/gradientbar3.tga") == 0);
  CHECK(has_line(f, "UI menu load time = 0 milli seconds"));
  /* Font sink stores the registered font behind the live set. */
  const qa_q3_font_set *fonts = qa_q3_pres_mission_hud_fonts(hud);
  CHECK(fonts->profile == QA_Q3_FONT_CGAME && fonts->big == &hud->big_font);
  CHECK(near(fonts->small_threshold, 0.25) && near(fonts->big_threshold, 0.4));
  CHECK(hud->big_font.glyph_scale == 48.0 / 24);
  CHECK(hud->small_font.glyph_scale == 48.0 / 12);
  /* menuState reports the parsed set. */
  qa_ui_runtime_snapshot snapshot;
  CHECK(qa_q3_pres_mission_hud_menu_state(hud, &snapshot));
  CHECK(snapshot.menu_count == 5);
  qa_ui_runtime_snapshot_destroy(&snapshot);
  /* getMenuBuffer diagnostics. */
  CHECK(qa_q3_pres_mission_hud_get_menu_buffer(hud, "ui/none.menu") == NULL);
  CHECK(has_line(f, "^1menu file not found: ui/none.menu, using default\n"));
  char *buffer = qa_q3_pres_mission_hud_get_menu_buffer(hud, "ui/hud.txt");
  /* TS 2026-11-01: trap_PC_LoadSource compresses the buffer; bun compressScriptText(HUD_SET). */
  CHECK(buffer && strcmp(buffer, "{\nloadMenu { \"ui/score.menu\" }\n}") == 0);
  free(buffer);
  CHECK_ABORTS((void)qa_q3_pres_mission_hud_load_menus(hud, "ui/absent.txt"));
  /* Chat rotation and source limits. */
  qa_q3_pres_mission_hud_set_print_string(hud, 0, "sys");
  qa_q3_pres_mission_hud_set_print_string(hud, 1, "one");
  qa_q3_pres_mission_hud_set_print_string(hud, 1, "two");
  qa_q3_pres_mission_hud_chat chat = qa_q3_pres_mission_hud_chat_text(hud);
  CHECK(strcmp(chat.system, "sys") == 0 && strcmp(chat.team1, "two") == 0 && strcmp(chat.team2, "one") == 0);
  char too_long[257];
  memset(too_long, 'x', 256);
  too_long[256] = '\0';
  CHECK_ABORTS(qa_q3_pres_mission_hud_set_print_string(hud, 0, too_long));
  qa_q3_pres_mission_hud_init_team_chat(hud);
  CHECK(qa_q3_pres_mission_hud_chat_text(hud).system[0] == '\0');
  attach_snapshot(p);
  /* Selection cycling writes VM integer and the name/number cvars. */
  state->num_sorted_team_players = 2;
  state->sorted_team_players[0] = 2;
  state->sorted_team_players[1] = 5;
  cgs->client_info[5].team_task = 3;
  CHECK(qa_q3_pres_set_string(&cgs->client_info[5].name, "Ranger"));
  CHECK(qa_q3_pres_mission_hud_get_selected_player(hud) == 0);
  qa_q3_pres_mission_hud_select_next_player(hud);
  CHECK(qa_q3_pres_mission_hud_get_selected_player(hud) == 1);
  CHECK(strcmp(cvar_text(f, "cg_selectedPlayerName", text, sizeof text), "Ranger") == 0);
  CHECK(strcmp(cvar_text(f, "cg_selectedPlayer", text, sizeof text), "5") == 0);
  CHECK(cgs->current_order == 3);
  qa_q3_pres_mission_hud_select_next_player(hud); /* index 2 == everyone slot */
  CHECK(strcmp(cvar_text(f, "cg_selectedPlayerName", text, sizeof text), "Everyone") == 0);
  qa_q3_pres_mission_hud_select_previous_player(hud); /* index 2: not in range -> count */
  CHECK(qa_q3_pres_client_configuration_set_vm_integer(hud->host.configuration, QA_Q3_PRES_VM_CG_CURRENT_SELECTED_PLAYER, 1));
  qa_q3_pres_mission_hud_select_previous_player(hud);
  CHECK(strcmp(cvar_text(f, "cg_selectedPlayer", text, sizeof text), "2") == 0);
  /* getSelectedPlayer repairs out-of-range. */
  CHECK(qa_q3_pres_client_configuration_set_vm_integer(hud->host.configuration, QA_Q3_PRES_VM_CG_CURRENT_SELECTED_PLAYER, 9));
  CHECK(qa_q3_pres_mission_hud_get_selected_player(hud) == 0);
  /* CheckOrderPending: game type gate, self order, other client, everyone. */
  cgs->game_type = QA_Q3_BASE_GT_TEAM;
  cgs->order_pending = true;
  cgs->current_order = 1;
  f->console_count = 0;
  qa_q3_pres_mission_hud_check_order_pending(hud);
  CHECK(cgs->order_pending && f->console_count == 0);
  cgs->game_type = QA_Q3_BASE_GT_CTF;
  qa_q3_pres_mission_hud_check_order_pending(hud);
  CHECK(!cgs->order_pending && f->console_count == 3);
  CHECK(strcmp(f->console[0], "teamtask 1\n") == 0);
  CHECK(strcmp(f->console[1], "cmd vsay_team onoffense\n") == 0);
  CHECK(strcmp(f->console[2], "+button7; wait; -button7") == 0);
  cgs->order_pending = true;
  cgs->current_order = 5;
  CHECK(qa_q3_pres_client_configuration_set_vm_integer(hud->host.configuration, QA_Q3_PRES_VM_CG_CURRENT_SELECTED_PLAYER, 1));
  f->console_count = 0;
  qa_q3_pres_mission_hud_check_order_pending(hud);
  CHECK(f->console_count == 1 && strcmp(f->console[0], "cmd vtell 5 returnflag\n") == 0);
  cgs->order_pending = true;
  cgs->current_order = 2;
  CHECK(qa_q3_pres_client_configuration_set_vm_integer(hud->host.configuration, QA_Q3_PRES_VM_CG_CURRENT_SELECTED_PLAYER, 2));
  f->console_count = 0;
  qa_q3_pres_mission_hud_check_order_pending(hud);
  CHECK(f->console_count == 2 && strcmp(f->console[0], "cmd vsay_team defend\n") == 0);
  CHECK(strcmp(f->console[1], "+button8; wait; -button8") == 0);
  cgs->order_pending = true;
  cgs->current_order = 99;
  CHECK_ABORTS(qa_q3_pres_mission_hud_check_order_pending(hud));
  cgs->order_pending = false;
  CHECK(qa_q3_pres_client_configuration_set_vm_integer(hud->host.configuration, QA_Q3_PRES_VM_CG_CURRENT_SELECTED_PLAYER, 0));
  /* Feeders. */
  state->num_scores = 3;
  state->scores[0].client = 2; state->scores[0].team = QA_Q3_BASE_TEAM_RED; state->scores[0].time = 7; state->scores[0].ping = 42;
  state->scores[1].client = 5; state->scores[1].team = QA_Q3_BASE_TEAM_BLUE; state->scores[1].time = 123; state->scores[1].ping = -1;
  state->scores[2].client = 3; state->scores[2].team = QA_Q3_BASE_TEAM_BLUE;
  CHECK(qa_q3_pres_mission_hud_feeder_count(hud, QA_Q3_PRES_FEEDER_SCOREBOARD) == 3);
  CHECK(qa_q3_pres_mission_hud_feeder_count(hud, QA_Q3_PRES_FEEDER_RED) == 1);
  CHECK(qa_q3_pres_mission_hud_feeder_count(hud, QA_Q3_PRES_FEEDER_BLUE) == 2);
  CHECK(qa_q3_pres_mission_hud_feeder_count(hud, 99) == 0);
  qa_ui_runtime_feeder_item item;
  cgs->client_info[2].info_valid = false;
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 3, &item);
  CHECK(item.text[0] == '\0' && !item.has_picture);
  cgs->client_info[2].info_valid = true;
  cgs->client_info[5].info_valid = true;
  cgs->client_info[2].score = 14;
  CHECK(qa_q3_pres_set_string(&cgs->client_info[2].name, "Sarge"));
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 3, &item);
  CHECK(strcmp(item.text, "Sarge") == 0);
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 4, &item);
  CHECK(strcmp(item.text, "14") == 0);
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 5, &item);
  CHECK(strcmp(item.text, "   7") == 0);
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 6, &item);
  CHECK(strcmp(item.text, "  42") == 0);
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_BLUE, 0, 6, &item);
  CHECK(strcmp(item.text, "connecting") == 0);
  /* Column 0: flag picture, bot skill, handicap, empty. */
  cgs->client_info[2].powerups = 1 << QA_Q3_PW_REDFLAG;
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 0, &item);
  CHECK(item.has_picture);
  cgs->client_info[2].powerups = 0;
  cgs->client_info[2].bot_skill = 2;
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 0, &item);
  CHECK(item.has_picture && strcmp(item.picture.material.name, media->graphics.bot_skill_shaders[1]->name) == 0);
  cgs->client_info[2].bot_skill = 0;
  cgs->client_info[2].handicap = 50;
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 0, &item);
  CHECK(!item.has_picture && strcmp(item.text, "50") == 0);
  cgs->client_info[2].handicap = 100;
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 0, &item);
  CHECK(!item.has_picture && item.text[0] == '\0');
  /* Column 1: status handle only on team feeders. */
  cgs->client_info[2].team_task = 3;
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 1, &item);
  CHECK(item.has_picture && strcmp(item.picture.material.name, media->graphics.patrol_shader->name) == 0);
  cgs->client_info[2].team_task = 0;
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 1, &item);
  CHECK(strcmp(item.picture.material.name, media->graphics.assault_shader->name) == 0);
  /* Column 2: ready, leader; non-team tournament record and spectator. */
  const qa_q3_base_stat_schema *stats = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_MISSIONPACK);
  qa_q3_base_player_state_slots_set(&g_snapshot.player_state.stats, stats->clients_ready, 1 << 2);
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 2, &item);
  CHECK(strcmp(item.text, "Ready") == 0);
  qa_q3_base_player_state_slots_set(&g_snapshot.player_state.stats, stats->clients_ready, 0);
  cgs->client_info[2].team_leader = true;
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_RED, 0, 2, &item);
  CHECK(strcmp(item.text, "Leader") == 0);
  cgs->game_type = QA_Q3_BASE_GT_TOURNAMENT;
  cgs->client_info[2].wins = 3;
  cgs->client_info[2].losses = 1;
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_SCOREBOARD, 0, 2, &item);
  CHECK(strcmp(item.text, "3/1") == 0);
  cgs->game_type = QA_Q3_BASE_GT_FFA;
  cgs->client_info[2].team = QA_Q3_BASE_TEAM_SPECTATOR;
  qa_q3_pres_mission_hud_feeder_item(hud, QA_Q3_PRES_FEEDER_SCOREBOARD, 0, 2, &item);
  CHECK(strcmp(item.text, "Spectator") == 0);
  /* Selection maps team-relative rows onto score indices. */
  cgs->game_type = QA_Q3_BASE_GT_FFA;
  qa_q3_pres_mission_hud_feeder_selection(hud, QA_Q3_PRES_FEEDER_RED, 2);
  CHECK(state->selected_score == 2);
  cgs->game_type = QA_Q3_BASE_GT_TEAM;
  qa_q3_pres_mission_hud_feeder_selection(hud, QA_Q3_PRES_FEEDER_BLUE, 1);
  CHECK(state->selected_score == 2);
  qa_q3_pres_mission_hud_feeder_selection(hud, QA_Q3_PRES_FEEDER_RED, 0);
  CHECK(state->selected_score == 0);
  /* setScoreSelection finds this client's row. */
  state->selected_score = 2;
  qa_q3_pres_mission_hud_set_score_selection(hud);
  CHECK(state->selected_score == 0);
  /* Scoreboard branches. */
  cgs->game_type = QA_Q3_BASE_GT_FFA;
  state->warmup = 5;
  state->show_scores = false;
  CHECK(!qa_q3_pres_mission_hud_draw_scoreboard(hud));
  state->warmup = 0;
  state->score_fade_time = 0;
  state->deferred_player_loading = 4;
  CHECK(qa_q3_pres_set_string(&state->killer_name, "killer"));
  state->predicted_player_state.pm_type = QA_Q3_PM_NORMAL;
  CHECK(!qa_q3_pres_mission_hud_draw_scoreboard(hud));
  CHECK(state->deferred_player_loading == 0 && (!state->killer_name || state->killer_name[0] == '\0'));
  state->show_scores = true;
  CHECK(qa_q3_pres_mission_hud_menu_scoreboard(hud) == NULL);
  CHECK(qa_q3_pres_mission_hud_draw_scoreboard(hud));
  CHECK(qa_q3_pres_mission_hud_menu_scoreboard(hud) != NULL);
  CHECK(state->deferred_player_loading == 1);
  CHECK(!hud->scoreboard_first_time);
  /* Deferred load threshold; paused returns false. */
  state->deferred_player_loading = 10;
  CHECK(qa_q3_pres_mission_hud_draw_scoreboard(hud));
  CHECK(state->deferred_player_loading == 11);
  qa_ui_captured_menu *board = qa_q3_pres_mission_hud_menu_scoreboard(hud);
  qa_q3_pres_mission_hud_scroll_feeder(hud, board, QA_Q3_PRES_FEEDER_SCOREBOARD, true);
  CHECK_ABORTS(qa_q3_pres_mission_hud_scroll_feeder(hud, (qa_ui_captured_menu *)(uintptr_t)8, QA_Q3_PRES_FEEDER_SCOREBOARD, true));
  qa_q3_pres_mission_hud_clear_scoreboard(hud);
  CHECK(qa_q3_pres_mission_hud_menu_scoreboard(hud) == NULL);
  state->show_scores = false;
  /* Response head and timed close. */
  state->time = 4000;
  qa_q3_pres_mission_hud_show_response_head(hud);
  CHECK(state->voice_time == 4000);
  CHECK(strcmp(cvar_text(f, "cl_conXOffset", text, sizeof text), "72") == 0);
  qa_q3_pres_mission_hud_draw_timed_menus(hud);
  CHECK(state->voice_time == 4000);
  state->time = 6501;
  qa_q3_pres_mission_hud_draw_timed_menus(hud);
  CHECK(state->voice_time == 0);
  CHECK(strcmp(cvar_text(f, "cl_conXOffset", text, sizeof text), "0") == 0);
  /* Names fold only ASCII case over valid clients. */
  cgs->maxclients = 8;
  CHECK(qa_q3_pres_mission_hud_client_num_from_name(hud, "RANGER") == 5);
  CHECK(qa_q3_pres_mission_hud_client_num_from_name(hud, "none") == -1);
  /* Mouse: PM_NORMAL without scores drops the catcher. */
  state->predicted_player_state.pm_type = QA_Q3_PM_NORMAL;
  f->key_catcher_calls = 0;
  qa_q3_pres_mission_hud_mouse_event(hud, 10, 10);
  CHECK(f->key_catcher_calls == 1 && f->key_catcher == 0 && cgs->cursor_x == 0);
  /* Dead player moves the clamped cursor and selects its pointer shader. */
  state->predicted_player_state.pm_type = QA_Q3_PM_DEAD;
  qa_q3_pres_mission_hud_mouse_event(hud, 700, -5);
  CHECK(cgs->cursor_x == 640 && cgs->cursor_y == 0);
  CHECK(cgs->active_cursor == media->graphics.select_cursor);
  /* Keys: release ignored; Mouse2 toggles capture. */
  qa_q3_pres_mission_hud_key_event(hud, QA_KEY_MOUSE2, false);
  CHECK(hud->captured_menu == NULL);
  cgs->cursor_x = 10;
  cgs->cursor_y = 10;
  qa_q3_pres_mission_hud_key_event(hud, QA_KEY_MOUSE2, true);
  CHECK(hud->captured_menu != NULL);
  qa_q3_pres_mission_hud_mouse_event(hud, 1, 1);
  qa_q3_pres_mission_hud_key_event(hud, QA_KEY_MOUSE2, true);
  CHECK(hud->captured_menu == NULL);
  /* Normal movement closes menus and catcher. */
  state->predicted_player_state.pm_type = QA_Q3_PM_NORMAL;
  f->key_catcher_calls = 0;
  cgs->event_handling = 1;
  qa_q3_pres_mission_hud_key_event(hud, 65, true);
  CHECK(f->key_catcher_calls == 1 && cgs->event_handling == 0);
  qa_q3_pres_client_presentation_event_handling(p, 2);
  CHECK(cgs->event_handling == 2);
  qa_q3_pres_mission_hud_show_team_menu(hud);
  qa_q3_pres_mission_hud_hide_team_menu(hud);
  /* Resets advance the lifecycle generation; the runtime stays. */
  qa_ui_runtime *runtime = hud->runtime;
  double generation = hud->generation;
  qa_q3_pres_mission_hud_reset_menus(hud);
  CHECK(hud->generation == generation + 1 && hud->runtime == runtime);
  /* Reload: resetStrings then a second load keeps the runtime. */
  qa_q3_pres_mission_hud_reset_strings(hud);
  CHECK(qa_q3_pres_mission_hud_load_hud_menu(hud));
  CHECK(hud->runtime == runtime);
  CHECK(qa_q3_pres_mission_hud_menu_state(hud, &snapshot));
  CHECK(snapshot.menu_count == 5);
  qa_ui_runtime_snapshot_destroy(&snapshot);
  /* A further load without resetting strings resolves through the bucket table. */
  CHECK(qa_q3_pres_mission_hud_load_hud_menu(hud));
  CHECK(hud->runtime == runtime && hud->definition_count == 3);
  /* The console loadhud command clears the scoreboard and reloads. */
  const char *const load_hud[] = {"loadhud"};
  state->show_scores = true;
  qa_q3_pres_mission_hud_clear_scoreboard(hud);
  CHECK(qa_q3_pres_mission_hud_draw_scoreboard(hud));
  CHECK(qa_q3_pres_mission_hud_menu_scoreboard(hud) != NULL);
  CHECK(qa_q3_pres_client_console_runtime_execute(qa_q3_pres_client_presentation_console(p), load_hud, 1));
  CHECK(qa_q3_pres_mission_hud_menu_scoreboard(hud) == NULL);
  CHECK(hud->definition_count == 4 && hud->runtime == runtime);
  state->show_scores = false;
  /* Mission HUD owner queries. */
  qa_q3_pres_mission_owner_draw *owner = &hud->owner_draw;
  CHECK(qa_q3_pres_mission_owner_draw_value(owner, QA_Q3_PRES_OD_RED_SCORE) == cgs->scores1);
  CHECK(qa_q3_pres_mission_owner_draw_value(owner, 9999) == -1);
  CHECK(qa_q3_pres_mission_owner_draw_visible(owner, QA_Q3_PRES_OD_SHOW_ANYNONTEAMGAME));
  CHECK(!qa_q3_pres_mission_owner_draw_visible(owner, QA_Q3_PRES_OD_SHOW_ANYTEAMGAME));
  CHECK(qa_q3_pres_mission_owner_draw_status_handle(owner, 2) == media->graphics.defend_shader);
  CHECK(qa_q3_pres_mission_owner_draw_width(owner, 9999, 1) == 0);
  /* Mission HUD draw2D paints menus, prox warning and owner-draw branches. */
  qa_q3_pres_client_hud *client_hud = qa_q3_pres_client_presentation_hud(p);
  set_stat(state, 100, 0, QA_Q3_BASE_TEAM_FREE);
  state->predicted_player_state.pm_type = QA_Q3_PM_NORMAL;
  fixture_clear_draws(f);
  g_snapshot.player_state.e_flags = 2;
  state->time = 100000;
  qa_q3_pres_client_hud_draw_prox_warning(client_hud);
  CHECK(client_hud->prox_counter == 5 && client_hud->prox_tick == 0 && f->draw_count > 0);
  CHECK(f->draws[f->draw_count - 1].color.x == 1 && f->draws[f->draw_count - 1].color.y == 0);
  state->time = 200000;
  qa_q3_pres_client_hud_draw_prox_warning(client_hud);
  CHECK(client_hud->prox_tick == 5 && client_hud->prox_counter == 4 && client_hud->prox_time == 201000);
  g_snapshot.player_state.e_flags = 0;
  qa_q3_pres_client_hud_draw_prox_warning(client_hud);
  CHECK(client_hud->prox_time == 0);
  /* Vote adds the mission hint line. */
  fixture_clear_draws(f);
  cgs->vote_time = state->time;
  qa_q3_pres_client_hud_draw_vote(client_hud);
  size_t with_hint = f->draw_count;
  CHECK(f->draws[with_hint - 1].rect.y == 76);
  cgs->vote_time = 0;
  /* Order pending past time fires from draw2D before cg_draw2D is read. */
  cgs->order_pending = true;
  cgs->game_type = QA_Q3_BASE_GT_CTF;
  cgs->current_order = 3;
  cgs->order_time = 0;
  f->console_count = 0;
  state->level_shot = true;
  qa_q3_pres_client_hud_draw_2d(client_hud);
  CHECK(!cgs->order_pending && f->console_count == 3);
  state->level_shot = false;
  cgs->game_type = QA_Q3_BASE_GT_FFA;
  /* draw2D full active path through the UI runtime frame. */
  fixture_clear_draws(f);
  qa_q3_pres_client_hud_draw_2d(client_hud);
  CHECK(!state->score_board_showing);
  /* Base-only drawing refused in missionpack. */
  CHECK_ABORTS(qa_q3_pres_client_hud_draw_status_bar(client_hud));
  CHECK_ABORTS(qa_q3_pres_client_hud_draw_status_bar_head(client_hud, 1));
  CHECK_ABORTS((void)qa_q3_pres_client_hud_draw_holdable_item(client_hud));
  /* Tourney scoreboard draws nothing and returns false in missionpack. */
  CHECK(!qa_q3_pres_client_hud_draw_tourney_scoreboard(client_hud));
  /* Console team commands forward to the HUD. */
  const char *const next[] = {"nextTeamMember"};
  cgs->order_pending = false;
  CHECK(qa_q3_pres_client_configuration_set_vm_integer(hud->host.configuration, QA_Q3_PRES_VM_CG_CURRENT_SELECTED_PLAYER, 0));
  CHECK(qa_q3_pres_client_console_runtime_handles(qa_q3_pres_client_presentation_console(p), next[0]));
  CHECK(qa_q3_pres_client_console_runtime_execute(qa_q3_pres_client_presentation_console(p), next, 1));
  CHECK(qa_q3_pres_mission_hud_get_selected_player(hud) == 1);
  CHECK(strcmp(cvar_text(f, "cg_selectedPlayerName", text, sizeof text), "Ranger") == 0);
  /* Disposed owner refuses. */
  detach_snapshot(p);
  qa_q3_pres_client_presentation_close(p);
  CHECK(hud->closed && hud->runtime == NULL);
  CHECK_ABORTS(qa_q3_pres_mission_hud_paint_all(hud));
  CHECK_ABORTS(qa_q3_pres_mission_hud_reset_menus(hud));
  CHECK_ABORTS((void)qa_q3_pres_mission_hud_asset_cache(hud));
  qa_q3_pres_client_presentation_destroy(p);
  /* Weapon HUD reader feeds the ammo value query. */
  fixture *g = fixture_create("0");
  g->weapon_hud_on = true;
  qa_q3_pres_client_presentation *q = mission_create(g);
  CHECK(q != NULL);
  attach_snapshot(q);
  CHECK(qa_q3_pres_mission_owner_draw_value(&qa_q3_pres_client_presentation_menus(q)->owner_draw, QA_Q3_PRES_OD_PLAYER_AMMO_VALUE) == 17);
  detach_snapshot(q);
  qa_q3_pres_client_presentation_close(q);
  qa_q3_pres_client_presentation_destroy(q);
}

int main(void) {
  test_state_and_ref_entity();
  test_client_baseq3();
  test_client_scene();
  test_hud_baseq3();
  test_mission_hud();
  test_first_load_repeated_string();
  test_reload_live_options();
  test_frame_indices();
  owner_paint_case(false);
  owner_paint_case(true);
  printf("q3 presentation depth: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
