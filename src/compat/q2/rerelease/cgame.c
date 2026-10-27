/* Port of src/compat/q2/rerelease/cgame.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/cgame.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/compat/q2/rerelease/player_state.h"

struct qa_compat_q2_rerelease_cgame { qa_compat_q2_rerelease_cgame_options options; };
qa_compat_q2_rerelease_cgame *qa_compat_q2_rerelease_cgame_create(qa_compat_q2_rerelease_cgame_options options) {
  qa_compat_q2_rerelease_cgame *g = calloc(1, sizeof *g);
  if (!g) return NULL;
  g->options = options;
  return g;
}
void qa_compat_q2_rerelease_cgame_destroy(qa_compat_q2_rerelease_cgame *g) { free(g); }
const qa_compat_q2_rerelease_cgame_options *qa_compat_q2_rerelease_cgame_get_options(const qa_compat_q2_rerelease_cgame *g) { return &g->options; }
qa_q2_cgame_api_identity qa_compat_q2_rerelease_cgame_api(const qa_compat_q2_rerelease_cgame *g) { (void)g; return (qa_q2_cgame_api_identity){2022}; }
static qa_compat_q2_rerelease_guest_module *mod(qa_compat_q2_rerelease_cgame *g) { return g->options.module; }
static qa_compat_q2_memory *mem(qa_compat_q2_rerelease_cgame *g) { return qa_compat_q2_rerelease_guest_module_get_memory(mod(g)); }
typedef void (*temp_fn)(void *ctx, qa_guest_address address);
static void temporary(qa_compat_q2_rerelease_cgame *g, double size, void *ctx, temp_fn fn) {
  qa_compat_q2_memory *memory = mem(g);
  qa_guest_address address = memory->allocate(memory->ctx, size, true, 8, "cgame call data");
  fn(ctx, address);
  memory->unmap(memory->ctx, address, size);
}
typedef struct { qa_compat_q2_rerelease_cgame *g; const qa_q2_rerelease_player_state *state; void *ctx; temp_fn fn; } player_ctx_t;
static void player_thunk(void *v, qa_guest_address address) {
  player_ctx_t *p = v;
  qa_compat_q2_write_rerelease_player_state(mem(p->g), address, p->state);
  p->fn(p->ctx, address);
}
static void with_player(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *state, void *ctx, temp_fn fn) {
  qa_compat_q2_rerelease_layouts_ensure();
  player_ctx_t p = {g, state, ctx, fn};
  temporary(g, qa_compat_q2_rerelease_player_state_layout.byte_length, &p, player_thunk);
}
typedef struct { qa_compat_q2_rerelease_cgame *g; const char *text; void *ctx; temp_fn fn; } text_ctx_t;
static void text_thunk(void *v, qa_guest_address address) {
  text_ctx_t *t = v;
  mem(t->g)->write(mem(t->g)->ctx, address, (const uint8_t *)t->text, strlen(t->text));
  t->fn(t->ctx, address);
}
static void with_text(qa_compat_q2_rerelease_cgame *g, const char *text, void *ctx, temp_fn fn) {
  text_ctx_t t = {g, text, ctx, fn};
  temporary(g, (double)strlen(text) + 1, &t, text_thunk);
}
static double split_index(qa_compat_q2_rerelease_cgame *g, qa_seat_id seat) {
  double index = g->options.split_index(g->options.ctx, seat);
  if (!(index == trunc(index)) || !isfinite(index) || index < -9007199254740991.0 || index > 9007199254740991.0 || index < 0 || index >= 8)
    qa_range_error("Cgame split index exceeds MAX_SPLIT_PLAYERS");
  return index;
}
static double integer_result(qa_guest_call_result value) {
  if (value.kind != QA_GUEST_VALUE_INT32 && value.kind != QA_GUEST_VALUE_UINT32)
    qa_range_error("Cgame source integer return required");
  return value.number_value;
}
void qa_compat_q2_rerelease_cgame_init(qa_compat_q2_rerelease_cgame *g) {
  g->options.refresh_cvars(g->options.ctx);
  (void)qa_compat_q2_rerelease_guest_module_call_cgame(mod(g), "Init", NULL, 0);
}
void qa_compat_q2_rerelease_cgame_shutdown(qa_compat_q2_rerelease_cgame *g) { (void)qa_compat_q2_rerelease_guest_module_call_cgame(mod(g), "Shutdown", NULL, 0); }
void qa_compat_q2_rerelease_cgame_touch_pictures(qa_compat_q2_rerelease_cgame *g) { (void)qa_compat_q2_rerelease_guest_module_call_cgame(mod(g), "TouchPics", NULL, 0); }
typedef struct { qa_compat_q2_rerelease_cgame *g; qa_q2_hud_draw frame; qa_guest_address player; } hud_ctx_t;
static void hud_data(void *v, qa_guest_address data) {
  hud_ctx_t *h = v;
  qa_q2_hud_draw *frame = &h->frame;
  size_t layout_len = strlen(frame->server_data.layout);
  if (layout_len >= 1024 || frame->server_data.inventory.count > 256) qa_range_error("Cgame server data exceeds source arrays");
  qa_compat_q2_memory *memory = mem(h->g);
  memory->write(memory->ctx, data, (const uint8_t *)frame->server_data.layout, layout_len);
  for (int i = 0; i < 256; i++) {
    double value = (size_t)i < frame->server_data.inventory.count ? frame->server_data.inventory.values[i] : 0;
    qa_compat_q2_mem_write_i16(memory, qa_compat_q2_mem_offset(memory, data, 1024 + i * 2), (int32_t)value);
  }
  double viewport[4] = {frame->viewport.x, frame->viewport.y, frame->viewport.width, frame->viewport.height};
  double safe[4] = {frame->safe_area.x, frame->safe_area.y, frame->safe_area.width, frame->safe_area.height};
  qa_compat_q2_rerelease_guest_module_draw_hud(mod(h->g), split_index(h->g, frame->seat), data, viewport, safe, frame->scale, frame->player_number, h->player);
}
static void hud_player(void *v, qa_guest_address player) {
  hud_ctx_t *h = v;
  h->player = player;
  qa_compat_q2_rerelease_layouts_ensure();
  temporary(h->g, qa_compat_q2_rerelease_cgame_server_data_layout.byte_length, h, hud_data);
}
static void hud_seat(void *v) { hud_ctx_t *h = v; with_player(h->g, &h->frame.player, h, hud_player); }
void qa_compat_q2_rerelease_cgame_draw_hud(qa_compat_q2_rerelease_cgame *g, qa_q2_hud_draw frame) {
  g->options.refresh_cvars(g->options.ctx);
  hud_ctx_t h = {g, frame, {0}};
  g->options.with_seat(g->options.ctx, frame.seat, &h, hud_seat);
}
typedef struct { qa_compat_q2_rerelease_cgame *g; const char *name; bool has_index; double index; double result; } stat_ctx_t;
static void stat_call(void *v, qa_guest_address address) {
  stat_ctx_t *s = v;
  qa_guest_call_value args[2];
  args[0] = qa_compat_q2_rerelease_guest_pointer(true, address);
  size_t n = 1;
  if (s->has_index) { args[1] = qa_compat_q2_rerelease_guest_int(s->index); n = 2; }
  s->result = integer_result(qa_compat_q2_rerelease_guest_module_call_cgame(mod(s->g), s->name, args, n));
}
static double stat(qa_compat_q2_rerelease_cgame *g, const char *name, const qa_q2_rerelease_player_state *player, bool has_index, double index) {
  stat_ctx_t s = {g, name, has_index, index, 0};
  with_player(g, player, &s, stat_call);
  return s.result;
}
double qa_compat_q2_rerelease_cgame_layout_flags(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *p) { return stat(g, "LayoutFlags", p, false, 0); }
double qa_compat_q2_rerelease_cgame_active_weapon_wheel_weapon(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *p) { return stat(g, "GetActiveWeaponWheelWeapon", p, false, 0); }
double qa_compat_q2_rerelease_cgame_owned_weapon_wheel_weapons(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *p) { return stat(g, "GetOwnedWeaponWheelWeapons", p, false, 0); }
double qa_compat_q2_rerelease_cgame_weapon_wheel_ammo_count(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *p, double id) { return stat(g, "GetWeaponWheelAmmoCount", p, true, id); }
double qa_compat_q2_rerelease_cgame_powerup_wheel_count(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *p, double id) { return stat(g, "GetPowerupWheelCount", p, true, id); }
double qa_compat_q2_rerelease_cgame_hit_marker_damage(qa_compat_q2_rerelease_cgame *g, const qa_q2_rerelease_player_state *p) { return stat(g, "GetHitMarkerDamage", p, false, 0); }
void qa_compat_q2_rerelease_cgame_pmove_raw(qa_compat_q2_rerelease_cgame *g, qa_guest_address address) {
  qa_guest_call_value arg = qa_compat_q2_rerelease_guest_pointer(true, address);
  (void)qa_compat_q2_rerelease_guest_module_call_cgame(mod(g), "Pmove", &arg, 1);
}
typedef struct { qa_compat_q2_rerelease_cgame *g; double index; qa_seat_id seat; bool flag; } call_ctx_t;
static void config_string_call(void *v, qa_guest_address address) {
  call_ctx_t *c = v;
  qa_guest_call_value args[2] = {qa_compat_q2_rerelease_guest_int(c->index), qa_compat_q2_rerelease_guest_pointer(true, address)};
  (void)qa_compat_q2_rerelease_guest_module_call_cgame(mod(c->g), "ParseConfigString", args, 2);
}
void qa_compat_q2_rerelease_cgame_parse_config_string(qa_compat_q2_rerelease_cgame *g, double index, const char *value) {
  call_ctx_t c; memset(&c, 0, sizeof c); c.g = g; c.index = index;
  with_text(g, value, &c, config_string_call);
}
static void center_print_call(void *v, qa_guest_address address) {
  call_ctx_t *c = v;
  qa_guest_call_value args[3] = {qa_compat_q2_rerelease_guest_pointer(true, address), qa_compat_q2_rerelease_guest_int(split_index(c->g, c->seat)), qa_compat_q2_rerelease_guest_bool(c->flag)};
  (void)qa_compat_q2_rerelease_guest_module_call_cgame(mod(c->g), "ParseCenterPrint", args, 3);
}
typedef struct { qa_compat_q2_rerelease_cgame *g; const char *text; call_ctx_t call; } seat_text_t;
static void center_print_seat(void *v) { seat_text_t *s = v; with_text(s->g, s->text, &s->call, center_print_call); }
void qa_compat_q2_rerelease_cgame_parse_center_print(qa_compat_q2_rerelease_cgame *g, qa_seat_id seat, const char *text, bool instant) {
  seat_text_t s = {g, text, {g, 0, seat, instant}};
  g->options.with_seat(g->options.ctx, seat, &s, center_print_seat);
}
void qa_compat_q2_rerelease_cgame_clear_notify(qa_compat_q2_rerelease_cgame *g, qa_seat_id seat) {
  qa_guest_call_value arg = qa_compat_q2_rerelease_guest_int(split_index(g, seat));
  (void)qa_compat_q2_rerelease_guest_module_call_cgame(mod(g), "ClearNotify", &arg, 1);
}
void qa_compat_q2_rerelease_cgame_clear_center_print(qa_compat_q2_rerelease_cgame *g, qa_seat_id seat) {
  qa_guest_call_value arg = qa_compat_q2_rerelease_guest_int(split_index(g, seat));
  (void)qa_compat_q2_rerelease_guest_module_call_cgame(mod(g), "ClearCenterprint", &arg, 1);
}
static void notify_call(void *v, qa_guest_address address) {
  call_ctx_t *c = v;
  qa_guest_call_value args[3] = {qa_compat_q2_rerelease_guest_int(split_index(c->g, c->seat)), qa_compat_q2_rerelease_guest_pointer(true, address), qa_compat_q2_rerelease_guest_bool(c->flag)};
  (void)qa_compat_q2_rerelease_guest_module_call_cgame(mod(c->g), "NotifyMessage", args, 3);
}
static void notify_seat(void *v) { seat_text_t *s = v; with_text(s->g, s->text, &s->call, notify_call); }
void qa_compat_q2_rerelease_cgame_notify_message(qa_compat_q2_rerelease_cgame *g, qa_seat_id seat, const char *text, bool chat) {
  seat_text_t s = {g, text, {g, 0, seat, chat}};
  g->options.with_seat(g->options.ctx, seat, &s, notify_seat);
}
typedef struct { qa_compat_q2_rerelease_cgame *g; double flash_id; qa_vec3 result; } flash_ctx_t;
static void flash_call(void *v, qa_guest_address address) {
  flash_ctx_t *f = v;
  qa_guest_call_value args[2] = {qa_compat_q2_rerelease_guest_int(f->flash_id), qa_compat_q2_rerelease_guest_pointer(true, address)};
  (void)qa_compat_q2_rerelease_guest_module_call_cgame(mod(f->g), "GetMonsterFlashOffset", args, 2);
  qa_compat_q2_memory *memory = mem(f->g);
  f->result = (qa_vec3){qa_compat_q2_mem_read_f32(memory, address), qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, address, 4)),
                        qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, address, 8))};
}
qa_vec3 qa_compat_q2_rerelease_cgame_monster_flash_offset(qa_compat_q2_rerelease_cgame *g, double flash_id) {
  flash_ctx_t f = {g, flash_id, {0, 0, 0}};
  temporary(g, 12, &f, flash_call);
  return f.result;
}
