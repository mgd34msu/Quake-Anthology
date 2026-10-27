/* Port of src/content/q2/rerelease/players.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/players.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/rerelease/environment.h"
#include "qa/content/q2/rerelease/killbox.h"
#include "qa/content/q2/rerelease/obituary.h"
#include "qa/content/q2/rerelease/spawns.h"
#include "qa/content/q2/rerelease/view.h"
#include "qa/foundation/support.h"
#include "rr_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void qa_q2rr_create_player_rules(qa_q2rr_player_rules *o) {
  memset(o, 0, sizeof *o);
  o->max_spectators = 4; o->flood_messages = 4; o->flood_seconds = 4; o->flood_wait_seconds = 10;
  o->roll_speed = 200; o->roll_angle = 2; o->run_pitch = 0.002; o->run_roll = 0.005;
  o->bob_up = 0.005; o->bob_pitch = 0.002; o->bob_roll = 0.002;
}
void qa_q2rr_carry_free(qa_q2rr_player_carry *c) { free(c->inventory); c->inventory = NULL; c->inventory_count = 0; }
bool qa_q2rr_carry_clone(qa_q2rr_player_carry *d, const qa_q2rr_player_carry *s) {
  *d = *s; d->inventory = NULL;
  if (s->inventory_count) {
    d->inventory = malloc(s->inventory_count * sizeof *d->inventory);
    if (!d->inventory) return false;
    memcpy(d->inventory, s->inventory, s->inventory_count * sizeof *d->inventory);
  }
  return true;
}
static void set_buf(char *dst, size_t cap, const char *src, size_t max) {
  if (!src) src = "";
  size_t n = strlen(src); if (n > max) n = max; if (n >= cap) n = cap - 1;
  memcpy(dst, src, n); dst[n] = '\0';
}
void qa_q2rr_player_state_init(qa_q2rr_player_state *s, double slot, double entered_at) {
  memset(s, 0, sizeof *s);
  s->slot = slot; s->entered_at = entered_at;
  s->use_q2_weapons = true; s->use_q2_inventory = true;
  set_buf(s->skin, sizeof s->skin, "male/grunt", 2047);
  s->gender = "male"; s->fov = 90; s->connected = true; s->drown_damage = 2;
  s->animation_end = 39; s->selected_item = "q2:weapon_blaster";
}
void qa_q2rr_player_state_destroy(qa_q2rr_player_state *s) {
  free(s->spawn_inventory); free(s->flood_times);
  if (s->has_coop_respawn) qa_q2rr_carry_free(&s->coop_respawn);
  s->spawn_inventory = NULL; s->flood_times = NULL;
}
bool qa_q2rr_player_state_clone(qa_q2rr_player_state *d, const qa_q2rr_player_state *s) {
  *d = *s; d->spawn_inventory = NULL; d->flood_times = NULL; d->coop_respawn.inventory = NULL;
  if (s->spawn_inventory_count) {
    d->spawn_inventory = malloc(s->spawn_inventory_count * sizeof *d->spawn_inventory);
    if (!d->spawn_inventory) return false;
    memcpy(d->spawn_inventory, s->spawn_inventory, s->spawn_inventory_count * sizeof *d->spawn_inventory);
  }
  if (s->flood_time_count) {
    d->flood_times = malloc(s->flood_time_count * sizeof *d->flood_times);
    if (!d->flood_times) return false;
    memcpy(d->flood_times, s->flood_times, s->flood_time_count * sizeof *d->flood_times);
    d->flood_time_cap = s->flood_time_count;
  }
  if (s->has_coop_respawn && !qa_q2rr_carry_clone(&d->coop_respawn, &s->coop_respawn)) return false;
  return true;
}

/* ---- maps ---- */
qa_q2rr_player_state *qa_q2rr_players_get_state(qa_q2_rerelease_players *p, qa_actor_id a) {
  for (size_t i = 0; i < p->state_count; i++) if (qa_same_actor(p->states[i].actor, a)) return p->states[i].state;
  return NULL;
}
qa_q2rr_player_extra_state *qa_q2rr_players_get_extra_or_null(qa_q2_rerelease_players *p, qa_actor_id a) {
  for (size_t i = 0; i < p->extra_count; i++) if (qa_same_actor(p->rerelease_states[i].actor, a)) return p->rerelease_states[i].state;
  return NULL;
}
bool qa_q2rr_players_set_state(qa_q2_rerelease_players *p, qa_actor_id a, qa_q2rr_player_state *s) {
  for (size_t i = 0; i < p->state_count; i++) if (qa_same_actor(p->states[i].actor, a)) { p->states[i].state = s; return true; }
  if (p->state_count == p->state_cap) {
    size_t nc = p->state_cap ? p->state_cap * 2 : 8;
    void *q = realloc(p->states, nc * sizeof *p->states); if (!q) return false;
    p->states = q; p->state_cap = nc;
  }
  p->states[p->state_count].actor = a; p->states[p->state_count].state = s; p->state_count++;
  return true;
}
bool qa_q2rr_players_set_extra(qa_q2_rerelease_players *p, qa_actor_id a, qa_q2rr_player_extra_state *s) {
  for (size_t i = 0; i < p->extra_count; i++) if (qa_same_actor(p->rerelease_states[i].actor, a)) { p->rerelease_states[i].state = s; return true; }
  if (p->extra_count == p->extra_cap) {
    size_t nc = p->extra_cap ? p->extra_cap * 2 : 8;
    void *q = realloc(p->rerelease_states, nc * sizeof *p->rerelease_states); if (!q) return false;
    p->rerelease_states = q; p->extra_cap = nc;
  }
  p->rerelease_states[p->extra_count].actor = a; p->rerelease_states[p->extra_count].state = s; p->extra_count++;
  return true;
}
void qa_q2rr_players_delete_state(qa_q2_rerelease_players *p, qa_actor_id a) {
  for (size_t i = 0; i < p->state_count; i++) if (qa_same_actor(p->states[i].actor, a)) {
    qa_q2rr_player_state_destroy(p->states[i].state); free(p->states[i].state);
    memmove(&p->states[i], &p->states[i + 1], (p->state_count - i - 1) * sizeof *p->states); p->state_count--; return;
  }
}
void qa_q2rr_players_delete_extra(qa_q2_rerelease_players *p, qa_actor_id a) {
  for (size_t i = 0; i < p->extra_count; i++) if (qa_same_actor(p->rerelease_states[i].actor, a)) {
    qa_q2rr_player_extra_state_destroy(p->rerelease_states[i].state); free(p->rerelease_states[i].state);
    memmove(&p->rerelease_states[i], &p->rerelease_states[i + 1], (p->extra_count - i - 1) * sizeof *p->rerelease_states); p->extra_count--; return;
  }
}
static bool placement_set(qa_q2rr_placement **arr, size_t *n, size_t *cap, qa_actor_id a, qa_q2rr_placement v) {
  for (size_t i = 0; i < *n; i++) if (qa_same_actor((*arr)[i].actor, a)) { (*arr)[i] = v; return true; }
  if (*n == *cap) { size_t nc = *cap ? *cap * 2 : 8; void *q = realloc(*arr, nc * sizeof **arr); if (!q) return false; *arr = q; *cap = nc; }
  (*arr)[*n] = v; (*n)++; return true;
}
static qa_q2rr_placement *placement_get(qa_q2rr_placement *arr, size_t n, qa_actor_id a) {
  for (size_t i = 0; i < n; i++) if (qa_same_actor(arr[i].actor, a)) return &arr[i];
  return NULL;
}
static void placement_del(qa_q2rr_placement *arr, size_t *n, qa_actor_id a) {
  for (size_t i = 0; i < *n; i++) if (qa_same_actor(arr[i].actor, a)) { memmove(&arr[i], &arr[i + 1], (*n - i - 1) * sizeof *arr); (*n)--; return; }
}

bool qa_q2_rerelease_players_init(qa_q2_rerelease_players *p, qa_q2_item_module *items,
                                  qa_q2rr_weapons *weapons, qa_q2rr_player_hooks hooks,
                                  qa_q2_rerelease_hooks rr_hooks,
                                  const qa_q2_rerelease_options_changes *options,
                                  const qa_q2rr_player_rules *rules) {
  memset(p, 0, sizeof *p);
  p->items = items; p->weapons = weapons; p->hooks = hooks; p->rerelease_hooks = rr_hooks;
  if (rules) p->rules = *rules; else qa_q2rr_create_player_rules(&p->rules);
  p->rerelease_options = qa_q2_create_rerelease_options(options);
  p->intermission.playing = true;
  return true;
}
void qa_q2_rerelease_players_destroy(qa_q2_rerelease_players *p) {
  for (size_t i = 0; i < p->state_count; i++) { qa_q2rr_player_state_destroy(p->states[i].state); free(p->states[i].state); }
  for (size_t i = 0; i < p->extra_count; i++) { qa_q2rr_player_extra_state_destroy(p->rerelease_states[i].state); free(p->rerelease_states[i].state); }
  free(p->states); free(p->rerelease_states); free(p->squad_spawns); free(p->selected_spawns);
  memset(p, 0, sizeof *p);
}

#define SUPER(name) (p->base.name ? p->base.name : (qa_q2rr_error("Q2Players base op missing: " #name), p->base.name))

bool qa_q2_rerelease_players_spawn(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)p;
  qa_q2_spawn_module m = qa_q2_get_rerelease_spawns();
  return m.spawn(m.ctx, e, g);
}
static qa_q2_named_think rr_spawn_thinks[1];
qa_q2_callback_definitions qa_q2_rerelease_players_callbacks(qa_q2_rerelease_players *p) {
  qa_q2_callback_definitions base = {0}, sp = qa_q2_rerelease_spawns_callbacks();
  if (p->base.callbacks) base = p->base.callbacks(p->base.ctx);
  /* TS {...super.callbacks, ...spawns.callbacks}: spawns think wins/extends.
   * C merge keeps base pain/die etc. and the spawns think table. */
  qa_q2_callback_definitions out = base;
  if (sp.think_count) { rr_spawn_thinks[0] = sp.think[0]; out.think = rr_spawn_thinks; out.think_count = sp.think_count; }
  return out;
}
qa_q2rr_player_extra_state *qa_q2_rerelease_players_get_extra(qa_q2_rerelease_players *p, qa_actor_id a) {
  qa_q2rr_player_extra_state *v = qa_q2rr_players_get_extra_or_null(p, a);
  if (!v) qa_q2rr_error("Q2 rerelease player is not admitted");
  return v;
}

/* context closure adapters */
typedef struct { qa_q2_rerelease_players *p; qa_q2_entity *e; qa_q2_game_services *g; } ctx_call;
static ctx_call call_scratch[16]; static size_t call_next;
static qa_q2_player_powerups call_powerups(void *c) { ctx_call *k = c; return qa_q2_item_module_player_powerups(k->p->items, k->e->actor.id); }
static qa_q2rr_weapon_result call_weapon(void *c) {
  ctx_call *k = c; qa_q2rr_weapon_result r = {0};
  if (k->p->hooks.weapon_state) return k->p->hooks.weapon_state(k->p->hooks.ctx, k->e->actor.id);
  if (k->p->weapons && k->p->weapons->get_state) {
    qa_q2rr_weapon_state_entry *w = k->p->weapons->get_state(k->p->weapons->ctx, k->e->actor.id);
    if (w) { r.has = true; r.v.q2_name = w->weapon; r.v.loop_sound = w->loop_sound; }
  }
  return r;
}
static void call_env_damage(void *c, double amount, double means, double flags) {
  ctx_call *k = c; qa_q2_game_services *g = k->g;
  qa_actor_id world = g->host->world_actor(g->host->ctx);
  qa_vec3 dir = means == 22 ? rr_v3(0, 0, 1) : QA_Q2_ZERO;
  g->damage(g->ctx, k->e->actor.id, k->e, world, &world, amount, 0, dir, rr_body_of(g, k->e).origin, QA_Q2_ZERO, means, flags, NULL);
}
static void call_sound(void *c, qa_q2_entity *e, const char *path, double ch, double vol, double att) {
  ctx_call *k = c; rr_sound_ent(k->g, e, path, ch, vol ? vol : 1, att ? att : 1);
}
qa_q2rr_player_context qa_q2_rerelease_players_context(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2rr_player_state *st = qa_q2rr_players_get_state(p, e->actor.id);
  if (!st) qa_q2rr_error("Q2 player has not been admitted");
  ctx_call *k = &call_scratch[call_next++ % 16];
  k->p = p; k->e = e; k->g = g;
  qa_q2rr_player_context c; memset(&c, 0, sizeof c);
  c.entity = e; c.game = g; c.state = st; c.rules = &p->rules; c.items = p->items; c.weapons = p->weapons; c.hooks = &p->hooks;
  c.movement = p->hooks.movement(p->hooks.ctx, e->actor.id);
  c.call_ctx = k; c.powerups = call_powerups; c.weapon_state = call_weapon;
  c.environment_damage = call_env_damage; c.sound = call_sound;
  return c;
}

/* q2Userinfo mirror (base/player/index.ts). */
static const char *userinfo_get(const char *src, const char *key, char *buf, size_t cap) {
  /* tokens split on '\\', first occurrence wins */
  const char *s = src;
  if (s[0] == '\\') s++;
  bool first_token = true;
  const char *cur_key = NULL; size_t cur_klen = 0;
  while (*s || first_token) {
    const char *end = strchr(s, '\\');
    size_t len = end ? (size_t)(end - s) : strlen(s);
    if (cur_key == NULL) { cur_key = s; cur_klen = len; }
    else {
      if (strlen(key) == cur_klen && strncmp(cur_key, key, cur_klen) == 0) {
        size_t n = len < cap - 1 ? len : cap - 1; memcpy(buf, s, n); buf[n] = '\0'; return buf;
      }
      cur_key = NULL;
    }
    first_token = false;
    if (!end) break;
    s = end + 1;
    if (!*s) break;
  }
  return NULL;
}
static double info_int(const char *v) { return v ? (double)strtol(v, NULL, 10) : 0; }

qa_q2rr_connection_result qa_q2_rerelease_players_connect(qa_q2_rerelease_players *p, qa_q2_game_services *g, const char *userinfo, bool is_bot) {
  char b1[512], b2[512];
  const char *spec = userinfo_get(userinfo, "spectator", b1, sizeof b1);
  bool spectator = g->options.mode == QA_Q2_MODE_DEATHMATCH && spec && spec[0] && strcmp(spec, "0") != 0;
  const char *password = spectator ? p->rules.spectator_password : p->rules.password;
  const char *reason = "";
  const char *given = userinfo_get(userinfo, spectator ? "spectator" : "password", b2, sizeof b2);
  if ((spectator || !is_bot) && password[0] && strcmp(password, "none") != 0 && (!given || strcmp(password, given) != 0))
    reason = spectator ? "Spectator password required or incorrect." : "Password required or incorrect.";
  else if (spectator) {
    size_t cnt = 0;
    for (size_t i = 0; i < p->state_count; i++) if (p->states[i].state->connected && p->states[i].state->requested_spectator) cnt++;
    if ((double)cnt >= p->rules.max_spectators) reason = "Server spectator limit is full.";
  }
  qa_q2rr_connection_result r; memset(&r, 0, sizeof r);
  if (!reason[0]) { r.allowed = true; set_buf(r.userinfo, sizeof r.userinfo, userinfo, sizeof r.userinfo - 1); return r; }
  /* rebuild without rejmsg, append rejmsg */
  r.allowed = false; set_buf(r.reason, sizeof r.reason, reason, sizeof r.reason - 1);
  size_t w = 0;
  const char *s = userinfo; if (s[0] == '\\') s++;
  bool tok_key = true; const char *kstart = NULL; size_t klen = 0;
  while (*s) {
    const char *end = strchr(s, '\\'); size_t len = end ? (size_t)(end - s) : strlen(s);
    if (tok_key) { kstart = s; klen = len; tok_key = false; }
    else {
      if (!(klen == 6 && strncmp(kstart, "rejmsg", 6) == 0) && w + klen + len + 2 < sizeof r.userinfo) {
        r.userinfo[w++] = '\\'; memcpy(r.userinfo + w, kstart, klen); w += klen;
        r.userinfo[w++] = '\\'; memcpy(r.userinfo + w, s, len); w += len;
      }
      tok_key = true;
    }
    if (!end) break;
    s = end + 1;
  }
  snprintf(r.userinfo + w, sizeof r.userinfo - w, "\\rejmsg\\%s", reason);
  return r;
}

static void on_release_players(void *c, qa_owned_actor actor);
typedef struct { qa_q2_rerelease_players *p; qa_actor_id id; int64_t sub; qa_q2_game_services *g; } rel_ctx;
static rel_ctx rel_scratch[32]; static size_t rel_next;
static void on_release_players(void *c, qa_owned_actor actor) {
  rel_ctx *r = c;
  if (qa_same_actor(actor.id, r->id)) {
    /* rerelease cleanup only; base attach owns its own release */
    qa_q2rr_players_delete_extra(r->p, actor.id);
    placement_del(r->p->squad_spawns, &r->p->squad_count, actor.id);
    qa_session_actor_registry_off_release(r->g->host->actors, r->sub);
  }
}

qa_q2rr_player_state *qa_q2_rerelease_players_attach(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_q2rr_player_admission a) {
  qa_q2rr_player_identity id = p->rerelease_hooks.player_identity(p->rerelease_hooks.ctx, e->actor.id);
  qa_q2rr_player_extra_state *extra = calloc(1, sizeof *extra);
  qa_q2rr_player_extra_state_init(extra, id.seat, id.social_id);
  extra->lives = (g->options.mode == QA_Q2_MODE_COOP && p->rerelease_options.coop_lives) ? p->rerelease_options.coop_num_lives + 1 : 0;
  qa_q2rr_players_set_extra(p, e->actor.id, extra);
  qa_q2rr_player_state *state = SUPER(attach)(p->base.ctx, e, g, a);
  if (extra->auto_shield >= 0) e->flags = rr_bor(e->flags, 0x40000000);
  rel_ctx *rc = &rel_scratch[rel_next++ % 32];
  rc->p = p; rc->id = e->actor.id; rc->g = g; rc->sub = 0;
  rc->sub = qa_session_actor_registry_on_release(g->host->actors, on_release_players, rc);
  if (p->has_extension && p->extension.admitted) p->extension.admitted(p->extension.ctx, e, g);
  size_t n = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, e->actor.id, &n);
  free(state->spawn_inventory); state->spawn_inventory = NULL; state->spawn_inventory_count = 0;
  if (n) { state->spawn_inventory = malloc(n * sizeof *entries); memcpy(state->spawn_inventory, entries, n * sizeof *entries); state->spawn_inventory_count = n; }
  /* state.coopRespawn = saveCarry via rerelease override below (needs carry) */
  qa_q2rr_player_carry carry = qa_q2_rerelease_players_save_carry(p, e, g);
  if (state->has_coop_respawn) qa_q2rr_carry_free(&state->coop_respawn);
  state->coop_respawn = carry; state->has_coop_respawn = true;
  return state;
}

void qa_q2_rerelease_players_userinfo_changed(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, const char *source) {
  qa_q2rr_player_state *st = qa_q2rr_players_get_state(p, e->actor.id);
  if (!st) qa_q2rr_error("Q2 rerelease userinfo requires an admitted player");
  char truncated[2048]; set_buf(truncated, sizeof truncated, source, 2047);
  char b[512];
  const char *nm = userinfo_get(truncated, "name", b, sizeof b);
  char name[32]; set_buf(name, sizeof name, nm ? nm : "badinfo", 31);
  set_buf(st->userinfo, sizeof st->userinfo, truncated, 2047);
  char b2[512]; const char *sk = userinfo_get(truncated, "skin", b2, sizeof b2);
  set_buf(st->skin, sizeof st->skin, sk ? sk : "male/grunt", sizeof st->skin - 1);
  char b3[512]; const char *sp = userinfo_get(truncated, "spectator", b3, sizeof b3);
  st->requested_spectator = g->options.mode == QA_Q2_MODE_DEATHMATCH && sp && sp[0] && strcmp(sp, "0") != 0;
  char b4[64]; double fov = info_int(userinfo_get(truncated, "fov", b4, sizeof b4));
  st->fov = fov < 1 ? 1 : fov > 160 ? 160 : fov;
  char b5[64]; double hand = info_int(userinfo_get(truncated, "hand", b5, sizeof b5));
  hand = hand < 0 ? 0 : hand > 2 ? 2 : hand;
  st->hand = hand == 1 ? QA_Q2RR_HAND_LEFT : hand == 2 ? QA_Q2RR_HAND_CENTER : QA_Q2RR_HAND_RIGHT;
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(p, e->actor.id);
  char b6[64]; double asw = info_int(userinfo_get(truncated, "autoswitch", b6, sizeof b6));
  asw = asw < 0 ? 0 : asw > 3 ? 3 : asw;
  char b7[64]; const char *bob = userinfo_get(truncated, "bobskip", b7, sizeof b7);
  extra->bob_skip = bob ? bob[0] == '1' : false;
  extra->auto_switch = asw;
  char b8[64]; const char *ash = userinfo_get(truncated, "autoshield", b8, sizeof b8);
  extra->auto_shield = ash ? info_int(ash) : -1;
  char b9[512]; const char *dt = userinfo_get(truncated, "dogtag", b9, sizeof b9);
  set_buf(extra->dogtag, sizeof extra->dogtag, dt ? dt : "", sizeof extra->dogtag - 1);
  qa_q2rr_player_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "userinfo"; ev.has_actor = true; ev.actor = e->actor.id; ev.slot = st->slot; ev.name = name; ev.skin = st->skin;
  p->hooks.emit(p->hooks.ctx, ev);
  qa_q2_rerelease_event rev; memset(&rev, 0, sizeof rev);
  rev.kind = "player-dogtag"; rev.has_actor = true; rev.actor = e->actor.id; rev.value = extra->dogtag;
  rr_emit_rr(&p->rerelease_hooks, rev);
  if (rr_band(e->server_flags, 16) != 0) set_buf(st->name, sizeof st->name, name, sizeof st->name - 1);
  else snprintf(st->name, sizeof st->name, "##P%lld", (long long)(int64_t)st->slot);
}

void qa_q2_rerelease_players_weapon_picked(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_item_id item, bool first) {
  if (!p->weapons || !p->weapons->registered_definitions || !p->weapons->get_state) return;
  size_t n = 0;
  const qa_q2rr_weapon_definition *defs = p->weapons->registered_definitions(p->weapons->ctx, &n);
  const qa_q2rr_weapon_definition *weapon = NULL;
  for (size_t i = 0; i < n; i++) if (defs[i].item && item && strcmp(defs[i].item, item) == 0) { weapon = &defs[i]; break; }
  qa_q2rr_weapon_state_entry *st = p->weapons->get_state(p->weapons->ctx, e->actor.id);
  if (!weapon || !st) return;
  if ((st->weapon && weapon->name && strcmp(st->weapon, weapon->name) == 0) ||
      (st->pending && weapon->name && strcmp(st->pending, weapon->name) == 0)) return;
  qa_q2_inventory_item inv; bool has_inv = qa_q2_item_module_lookup(p->items, item, &inv);
  bool ammo_weapon = has_inv && inv.kind && strcmp(inv.kind, "ammo") == 0;
  double required = ammo_weapon ? 1 : weapon->quantity;
  if (weapon->ammo && qa_shared_inventory_table_count(g->host->inventory, e->actor.id, weapon->ammo) < required) return;
  double mode = qa_q2_rerelease_players_get_extra(p, e->actor.id)->auto_switch;
  if (mode == 3 || (mode == 2 && ammo_weapon) ||
      (mode == 0 && !(st->weapon && strcmp(st->weapon, "blaster") == 0) && (g->options.mode == QA_Q2_MODE_DEATHMATCH || !first))) return;
  st->pending = weapon->name;
}

static void obit_score(void *c, qa_q2rr_player_state *r, double change);
typedef struct { qa_q2_rerelease_players *p; qa_q2_entity *e; qa_q2_game_services *g; qa_death_reaction reaction; } obit_ctx;
static void obit_score(void *c, qa_q2rr_player_state *r, double change) {
  obit_ctx *k = c;
  if (k->p->base.apply_score) k->p->base.apply_score(k->p->base.ctx, k->e, k->g, k->reaction, r, change);
  else r->score += change;
}
void qa_q2_rerelease_players_obituary(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r) {
  qa_q2rr_player_context c = qa_q2_rerelease_players_context(p, e, g);
  double means = 0; bool no_loss = false;
  if (e->last_attack && e->last_attack->cause.kind == QA_ATTACK_CAUSE_Q2) {
    means = e->last_attack->cause.means_of_death;
    no_loss = e->last_attack->cause.has_native && e->last_attack->cause.native.edition &&
              strcmp(e->last_attack->cause.native.edition, "rerelease") == 0 && e->last_attack->cause.native.no_point_loss;
  }
  qa_q2rr_player_state *attacker = r.pain.attacker ? qa_q2rr_players_get_state(p, *r.pain.attacker) : NULL;
  obit_ctx oc = {p, e, g, r};
  qa_q2rr_obituary ob = qa_q2_rerelease_obituary(c.state, attacker, means, g->options.mode, no_loss, &oc, obit_score);
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "localized-print"; ev.level = "medium"; ev.text = ob.text; ev.args = ob.args; ev.arg_count = ob.arg_count;
  rr_emit_rr(&p->rerelease_hooks, ev);
}
void qa_q2_rerelease_players_clear_death_inventory(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  if (g->options.mode == QA_Q2_MODE_COOP && !qa_q2_uses_instanced_items(p->rerelease_options))
    SUPER(clear_death_inventory)(p->base.ctx, e, g);
}
bool qa_q2_rerelease_players_can_drop_coop_stay_items(qa_q2_rerelease_players *p, qa_q2_game_services *g) {
  return g->options.mode == QA_Q2_MODE_COOP && qa_q2_uses_instanced_items(p->rerelease_options);
}
void qa_q2_rerelease_players_death(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r) {
  double means = 0;
  if (e->last_attack && e->last_attack->cause.kind == QA_ATTACK_CAUSE_Q2)
    means = (double)((int32_t)e->last_attack->cause.means_of_death & ~(int32_t)0x8000000);
  e->model2 = ""; e->model3 = ""; e->sound = "";
  qa_q2_rerelease_players_context(p, e, g).state->loop_sound[0] = '\0';
  if (means == 51) {
    qa_gameplay_authority_set_health(g->host->combat, e->actor, -100);
    qa_death_reaction r2 = r; r2.pain.damage = 400;
    SUPER(death)(p->base.ctx, e, g, r2); return;
  }
  if (means == 47 && rr_health(g, e->actor.id) < -80) e->flags = rr_bor(e->flags, 0x10000);
  SUPER(death)(p->base.ctx, e, g, r);
}
void qa_q2_rerelease_players_record_damage(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_damage_decision d) {
  SUPER(record_damage)(p->base.ctx, e, g, d);
  if (d.has_feedback && d.feedback_kind && strcmp(d.feedback_kind, "q2") == 0 && d.reaction && strcmp(d.reaction, "death") != 0)
    qa_q2_rerelease_players_get_extra(p, e->actor.id)->last_damage_until = rr_now(g) + 2;
}
qa_q2rr_player_carry qa_q2_rerelease_players_save_carry(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2rr_player_carry c = SUPER(save_carry)(p->base.ctx, e, g);
  c.flags = rr_band(e->flags, 16 | 32 | 4096 | 0x400000 | 0x40000000);
  return c;
}
void qa_q2_rerelease_players_restore_carry(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, const qa_q2rr_player_carry *carry) {
  SUPER(restore_carry)(p->base.ctx, e, g, carry);
  if (carry->health <= 0) {
    qa_q2rr_player_state *st = qa_q2_rerelease_players_context(p, e, g).state;
    size_t n = 0;
    const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, e->actor.id, &n);
    /* copy first: configure mutates the table */
    qa_inventory_entry *copy = NULL;
    if (n) { copy = malloc(n * sizeof *copy); memcpy(copy, entries, n * sizeof *copy); }
    for (size_t i = 0; i < n; i++) { qa_inventory_entry en = copy[i]; en.count = 0; qa_shared_inventory_table_configure(g->host->inventory, e->actor, en, NULL, NULL); }
    free(copy);
    if (st->use_q2_inventory) qa_q2_item_module_configure_player(p->items, e->actor, g, true);
    else for (size_t i = 0; i < st->spawn_inventory_count; i++) qa_shared_inventory_table_configure(g->host->inventory, e->actor, st->spawn_inventory[i], NULL, NULL);
    qa_gameplay_authority_set_health(g->host->combat, e->actor, 100);
    qa_armor_state none; memset(&none, 0, sizeof none);
    none.regular.kind = QA_REGULAR_ARMOR_NONE; none.powered.kind = "none";
    qa_gameplay_authority_set_armor(g->host->combat, e->actor, none);
    e->max_health = 100;
    e->flags = rr_bnot_and(e->flags, 16 | 32 | 4096 | 0x400000 | 0x40000000);
    e->power_cubes = 0;
    st->god = false; st->notarget = false;
    st->selected_item = st->use_q2_inventory ? "q2:weapon_blaster" : NULL;
    qa_combat_trait_changes tc; memset(&tc, 0, sizeof tc); tc.has_invulnerable = true; tc.invulnerable = false;
    qa_gameplay_authority_set_traits(g->host->combat, e->actor, tc);
    if (p->weapons && p->weapons->get_state) {
      qa_q2rr_weapon_state_entry *w = p->weapons->get_state(p->weapons->ctx, e->actor.id);
      if (w) { w->weapon = "blaster"; w->pending = NULL; }
    }
    if (st->use_q2_inventory && p->hooks.persistent_inventory_initialized)
      p->hooks.persistent_inventory_initialized(p->hooks.ctx, e, g);
    if (p->has_extension && p->extension.spawned) p->extension.spawned(p->extension.ctx, e, g);
    qa_q2rr_player_carry nc = qa_q2_rerelease_players_save_carry(p, e, g);
    if (st->has_coop_respawn) qa_q2rr_carry_free(&st->coop_respawn);
    st->coop_respawn = nc; st->has_coop_respawn = true;
  }
  qa_q2_rerelease_players_get_extra(p, e->actor.id)->flashlight = rr_band(carry->flags, 0x400000) != 0;
}
bool qa_q2_rerelease_players_record_death(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r) {
  qa_q2rr_player_carry carry = qa_q2_rerelease_players_save_carry(p, e, g);
  qa_q2rr_player_state *st = qa_q2_rerelease_players_context(p, e, g).state;
  bool first = SUPER(record_death)(p->base.ctx, e, g, r);
  qa_q2_rerelease_players_get_extra(p, e->actor.id)->invisibility_until = 0;
  if (!first) { qa_q2rr_carry_free(&carry); return false; }
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(p, e->actor.id);
  extra->animation_time = 0;
  qa_q2_rerelease_options *o = &p->rerelease_options;
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH && o->deathmatch_force_respawn_time != 0)
    st->respawn_time = rr_now(g) + o->deathmatch_force_respawn_time;
  if (g->options.mode == QA_Q2_MODE_COOP && qa_q2_uses_instanced_items(*o)) {
    carry.health = e->max_health; carry.maximum_health = e->max_health;
    if (st->has_coop_respawn) qa_q2rr_carry_free(&st->coop_respawn);
    st->coop_respawn = carry; st->has_coop_respawn = true;
  } else qa_q2rr_carry_free(&carry);
  if (g->options.mode == QA_Q2_MODE_COOP && (o->coop_squad_respawn || o->coop_lives)) {
    if (o->coop_lives && extra->lives != 0) extra->lives--;
    size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
    bool all_dead = true;
    for (size_t i = 0; i < pn; i++) {
      qa_q2rr_player_extra_state *x = qa_q2rr_players_get_extra_or_null(p, pls[i]);
      bool dead = rr_health(g, pls[i]) <= 0 && (p->deadly_kill_box || !o->coop_lives || (x ? x->lives : 0) <= 0);
      if (!dead) { all_dead = false; break; }
    }
    if (all_dead) {
      p->coop_restart_time = rr_now(g) + 5;
      for (size_t i = 0; i < pn; i++) {
        qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
        ev.kind = "centerprint"; ev.has_actor = true; ev.actor = pls[i]; ev.text = "$g_coop_lose";
        rr_emit_host(g, ev);
      }
    } else st->respawn_time = rr_now(g) + 3;
  }
  return true;
}
void qa_q2_rerelease_players_after_client_think(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  if (!p->intermission.playing && (p->intermission.map[0] == '\0' ||
      (qa_q2_is_n64(&g->options) && g->options.mode != QA_Q2_MODE_DEATHMATCH && !p->intermission_camera_set))) {
    qa_q2rr_player_state *st = qa_q2_rerelease_players_context(p, e, g).state;
    st->buttons = p->hooks.movement(p->hooks.ctx, e->actor.id).buttons;
    return;
  }
  SUPER(after_client_think)(p->base.ctx, e, g);
  if (!p->intermission.playing) return;
  qa_q2rr_player_context c = qa_q2_rerelease_players_context(p, e, g);
  qa_q2_rerelease_falling_damage(&c, qa_q2_rerelease_players_get_extra(p, e->actor.id), p->rerelease_options, rr_frame(g));
}
void qa_q2_rerelease_players_record_weapon_fire(qa_q2_rerelease_players *p, qa_actor_id a, double now) {
  qa_q2_rerelease_players_get_extra(p, a)->last_firing_until = now + 2.5;
}
void qa_q2_rerelease_players_reveal_invisibility(qa_q2_rerelease_players *p, qa_actor_id a, double until) {
  qa_q2_rerelease_players_get_extra(p, a)->invisibility_fade_until = until;
}
void qa_q2_rerelease_players_movement_impact(qa_q2_rerelease_players *p, qa_actor_id a, double d, bool on_ladder) {
  qa_q2rr_player_extra_state *x = qa_q2_rerelease_players_get_extra(p, a);
  x->impact_delta = d; x->on_ladder = on_ladder;
}
void qa_q2_rerelease_players_begin_frame(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  if (!p->intermission.playing) return;
  double now = rr_now(g);
  if (qa_q2_rerelease_players_get_extra(p, e->actor.id)->awaiting_respawn) {
    if ((int64_t)llround(now * 1000) % 500 == 0) qa_q2_rerelease_players_put_in_server(p, e, g, true, NULL);
    return;
  }
  SUPER(begin_frame)(p->base.ctx, e, g);
}
static void coop_respawn(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_rerelease_players_dead_frame(qa_q2_rerelease_players *p, qa_q2rr_player_context *c) {
  qa_q2_entity *e = c->entity; qa_q2_game_services *g = c->game; qa_q2rr_player_state *st = c->state;
  qa_q2_rerelease_options *o = &p->rerelease_options;
  if (rr_now(g) <= st->respawn_time || p->coop_restart_time != 0) return;
  if (g->options.mode == QA_Q2_MODE_COOP && (o->coop_squad_respawn || o->coop_lives)) { coop_respawn(p, e, g); return; }
  double mask = g->options.mode == QA_Q2_MODE_DEATHMATCH ? 1 : -1;
  if (rr_band(st->latched_buttons, mask) != 0 || (g->options.mode == QA_Q2_MODE_DEATHMATCH && o->deathmatch_force_respawn)) {
    qa_q2_rerelease_players_respawn(p, e, g); st->latched_buttons = 0;
  }
}
void qa_q2_rerelease_players_begin_intermission(qa_q2_rerelease_players *p, qa_q2_game_services *g, const char *map, const qa_q2_landmark_carry *lm) {
  qa_q2_rerelease_players_begin_rerelease_intermission(p, g, map, lm, 0);
}
void qa_q2_rerelease_players_begin_rerelease_intermission(qa_q2_rerelease_players *p, qa_q2_game_services *g, const char *map, const qa_q2_landmark_carry *lm, double flags) {
  if (!p->intermission.playing) return;
  p->intermission_flags = flags; p->has_intermission_fade_until = false;
  p->intermission.playing = false;
  set_buf(p->intermission.map, sizeof p->intermission.map, map, sizeof p->intermission.map - 1);
  p->intermission.started = rr_now(g); p->intermission.exit = false;
  p->intermission.has_landmark = lm != NULL;
  if (lm) p->intermission.landmark = *lm;
  for (size_t i = 0; i < p->state_count; i++) {
    qa_actor_id actor = p->states[i].actor; qa_q2rr_player_state *st = p->states[i].state;
    qa_q2_entity *ent = rr_ent(g, &actor);
    if (!ent || !st->connected || rr_health(g, actor) > 0) continue;
    if (qa_q2_uses_instanced_items(p->rerelease_options) && st->has_coop_respawn) {
      st->coop_respawn.health = ent->max_health; st->coop_respawn.maximum_health = ent->max_health;
    }
    qa_q2_rerelease_players_respawn(p, ent, g);
  }
  if (p->has_extension && p->extension.before_level_change) p->extension.before_level_change(p->extension.ctx, g);
  bool end_unit = strchr(map, '*') != NULL;
  if (end_unit) {
    if (g->options.mode == QA_Q2_MODE_COOP) {
      size_t ln = 0; const qa_q2_inventory_item *list = qa_q2_item_module_list(p->items, &ln);
      for (size_t i = 0; i < p->state_count; i++) {
        qa_actor_id actor = p->states[i].actor; qa_q2rr_player_state *st = p->states[i].state;
        qa_q2_entity *ent = rr_ent(g, &actor);
        if (!ent || !st->connected) continue;
        for (size_t j = 0; j < ln; j++) if (list[j].kind && strcmp(list[j].kind, "key") == 0) {
          size_t en = 0; const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, actor, &en);
          for (size_t k = 0; k < en; k++) if (entries[k].item && list[j].id && strcmp(entries[k].item, list[j].id) == 0) {
            qa_inventory_entry ne = entries[k]; ne.count = 0;
            qa_shared_inventory_table_configure(g->host->inventory, ent->actor, ne, NULL, NULL); break;
          }
        }
      }
    }
    qa_q2_entity *worlds[1]; size_t wn = qa_q2rr_entities_named(g, "worldspawn", worlds, 1);
    const char *ach = wn ? qa_q2_spawn_get(&worlds[0]->spawn, "achievement") : NULL;
    if (ach && ach[0]) {
      qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev); ev.kind = "achievement"; ev.id = ach;
      rr_emit_rr(&p->rerelease_hooks, ev);
    }
    if (rr_band(flags, 16) == 0) { if (p->has_extension && p->extension.end_of_unit) p->extension.end_of_unit(p->extension.ctx, g); }
    else if (rr_band(flags, 64) != 0 && g->options.mode != QA_Q2_MODE_DEATHMATCH) { p->intermission.exit = true; return; }
  } else if (g->options.mode != QA_Q2_MODE_DEATHMATCH) { p->intermission.exit = true; return; }
  if (!p->intermission_camera_set || !p->has_intermission_camera) {
    qa_q2_entity *authored[64]; size_t an = qa_q2rr_entities_named(g, "info_player_intermission", authored, 64);
    qa_q2_entity *spot = NULL;
    if (an == 0) {
      qa_q2_entity *s1[1]; size_t n1 = qa_q2rr_entities_named(g, "info_player_start", s1, 1);
      if (n1) spot = s1[0];
      else { qa_q2_entity *s2[1]; size_t n2 = qa_q2rr_entities_named(g, "info_player_deathmatch", s2, 1); if (n2) spot = s2[0]; }
    } else {
      double sel = 0;
      if (g->host->rerelease_random) sel = (double)0; /* landed random integer via host random fallback below */
      sel = floor(g->host->random(g->host->ctx) * 4);
      spot = authored[(size_t)((int64_t)sel % (int64_t)an)];
    }
    if (!spot) qa_q2rr_error("Q2 rerelease intermission has no camera or player spawn");
    qa_body_state b = rr_body_of(g, spot);
    p->has_intermission_camera = true; p->intermission_camera_origin = b.origin; p->intermission_camera_angles = b.angles;
  }
  qa_q2_rerelease_players_move_to_camera(p, g, p->intermission_camera_origin, p->intermission_camera_angles, true);
}
void qa_q2_rerelease_players_check_rules(qa_q2_rerelease_players *p, qa_q2_game_services *g) {
  if (!p->intermission.playing && p->intermission.map[0] == '\0') return;
  if (!p->intermission.playing && p->intermission.exit) {
    if (rr_band(p->intermission_flags, 32) != 0) {
      if (!p->has_intermission_fade_until) { p->has_intermission_fade_until = true; p->intermission_fade_until = rr_now(g) + 1.3; }
      if (rr_now(g) < p->intermission_fade_until) return;
      p->intermission_flags = rr_bnot_and(p->intermission_flags, 32); p->has_intermission_fade_until = false;
    }
  }
  if (p->coop_restart_time != 0 && rr_now(g) >= p->coop_restart_time) {
    p->coop_restart_time = 0;
    qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev); ev.kind = "restart-level"; ev.map = g->options.map_name;
    rr_emit_rr(&p->rerelease_hooks, ev); return;
  }
  SUPER(check_rules)(p->base.ctx, g);
}
void qa_q2_rerelease_players_before_exit_level(qa_q2_rerelease_players *p, qa_q2_game_services *g, const char *map) {
  if (rr_band(p->intermission_flags, 8) != 0) {
    p->intermission_flags = rr_bnot_and(p->intermission_flags, 8);
    for (size_t i = 0; i < p->state_count; i++) {
      qa_actor_id actor = p->states[i].actor; qa_q2rr_player_state *st = p->states[i].state;
      qa_q2_entity *ent = rr_ent(g, &actor); if (!ent) continue;
      size_t n = 0; const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, actor, &n);
      qa_inventory_entry *copy = NULL;
      if (n) { copy = malloc(n * sizeof *copy); memcpy(copy, entries, n * sizeof *copy); }
      for (size_t j = 0; j < n; j++) { qa_inventory_entry ne = copy[j]; ne.count = 0; qa_shared_inventory_table_configure(g->host->inventory, ent->actor, ne, NULL, NULL); }
      free(copy);
      qa_gameplay_authority_set_health(g->host->combat, ent->actor, 0);
      qa_armor_state none; memset(&none, 0, sizeof none); none.regular.kind = QA_REGULAR_ARMOR_NONE; none.powered.kind = "none";
      qa_gameplay_authority_set_armor(g->host->combat, ent->actor, none);
      qa_q2_item_module_clear_powerups(p->items, actor);
      if (p->rerelease_hooks.clear_expansion_powerups) p->rerelease_hooks.clear_expansion_powerups(p->rerelease_hooks.ctx, actor);
      ent->flags = rr_bnot_and(ent->flags, 16 | 32 | 4096 | 0x400000 | 0x40000000);
      ent->power_cubes = 0;
      st->god = false; st->notarget = false;
      if (st->has_coop_respawn) { qa_q2rr_carry_free(&st->coop_respawn); st->has_coop_respawn = false; }
      st->selected_item = NULL;
    }
  }
  if (strchr(map, '*') && p->has_extension && p->extension.leave_unit) p->extension.leave_unit(p->extension.ctx);
}
void qa_q2_rerelease_players_fade_frame(qa_q2_rerelease_players *p, qa_q2_game_services *g) {
  if (!p->has_intermission_fade_until) return;
  double until = p->intermission_fade_until, now = rr_now(g);
  if (now >= until) { qa_q2_rerelease_players_check_rules(p, g); return; }
  double alpha = 1 - (until - now - 0.3); alpha = alpha < 0 ? 0 : alpha > 1 ? 1 : alpha;
  for (size_t i = 0; i < p->state_count; i++) if (p->states[i].state->connected) {
    qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "screen-blend"; ev.has_actor = true; ev.actor = p->states[i].actor; ev.blend.w = alpha;
    rr_emit_rr(&p->rerelease_hooks, ev);
  }
}
void qa_q2_rerelease_players_move_to_camera(qa_q2_rerelease_players *p, qa_q2_game_services *g, qa_vec3 origin, qa_vec3 angles, bool entering) {
  p->has_intermission_camera = true; p->intermission_camera_origin = origin; p->intermission_camera_angles = angles;
  for (size_t i = 0; i < p->state_count; i++) {
    qa_actor_id actor = p->states[i].actor; qa_q2rr_player_state *st = p->states[i].state;
    qa_q2_entity *ent = rr_ent(g, &actor); if (!ent) continue;
    if (entering && rr_health(g, actor) <= 0) qa_q2_rerelease_players_respawn(p, ent, g);
    if (entering) {
      qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "entity-event"; ev.has_actor = true; ev.actor = actor; ev.event = 7; rr_emit_host(g, ev);
    }
    st->show_help = false; st->show_scores = g->options.mode == QA_Q2_MODE_DEATHMATCH;
    st->damage_alpha = 0; st->bonus_alpha = 0; st->loop_sound[0] = '\0';
    qa_q2_item_module_clear_powerups(p->items, actor);
    if (p->rerelease_hooks.clear_expansion_powerups) p->rerelease_hooks.clear_expansion_powerups(p->rerelease_hooks.ctx, actor);
    qa_q2_rerelease_players_get_extra(p, actor)->invisibility_until = 0;
    qa_combat_trait_changes tc; memset(&tc, 0, sizeof tc); tc.has_invulnerable = true; tc.invulnerable = st->god;
    qa_gameplay_authority_set_traits(g->host->combat, ent->actor, tc);
    if (p->weapons && p->weapons->get_state) {
      qa_q2rr_weapon_state_entry *w = p->weapons->get_state(p->weapons->ctx, actor);
      if (w) { w->grenade_blew_up = false; w->grenade_time = 0; w->view_model = NULL; }
    }
    ent->view_height = 0; ent->model = ""; ent->model2 = ""; ent->model3 = ""; ent->effects = 0; ent->sound = ""; ent->visible = false;
    g->solid(g->ctx, ent, "none"); g->motion(g->ctx, ent, "stationary");
    qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_origin = true; ch.origin = origin;
    g->move(g->ctx, ent, ch, true); g->show(g->ctx, ent);
    qa_q2rr_movement_change mc; memset(&mc, 0, sizeof mc); mc.kind = "freeze"; mc.origin = origin; mc.angles = angles;
    p->hooks.set_movement(p->hooks.ctx, actor, mc);
    if (st->show_scores && p->base.scoreboard) p->base.scoreboard(p->base.ctx, ent, g);
  }
}
void qa_q2_rerelease_players_finish_camera(qa_q2_rerelease_players *p, qa_q2_game_services *g) {
  if (p->intermission.playing) {
    p->intermission.playing = false; p->intermission.map[0] = '\0';
    p->intermission.started = rr_now(g); p->intermission.exit = false; p->intermission.has_landmark = false;
  } else {
    p->intermission.started = rr_now(g);
    p->intermission.exit = p->intermission.map[0] && !strchr(p->intermission.map, '*');
  }
}
void qa_q2_rerelease_players_emit_flashlight(qa_q2_rerelease_players *p, qa_actor_id a, qa_q2_game_services *g) {
  qa_q2rr_player_state *st = qa_q2rr_players_get_state(p, a);
  if (!st) qa_q2rr_error("Flashlight player is not admitted");
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "flashlight"; ev.has_actor = true; ev.actor = a; ev.hand = st->hand;
  ev.enabled = qa_q2_rerelease_players_get_extra(p, a)->flashlight && p->intermission.playing && rr_health(g, a) > 0;
  rr_emit_rr(&p->rerelease_hooks, ev);
}
void qa_q2_rerelease_players_end_frame(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  if (p->has_extension && p->extension.begin_player_frame) p->extension.begin_player_frame(p->extension.ctx, e, g);
  SUPER(end_frame)(p->base.ctx, e, g);
  if (p->has_extension && p->extension.end_player_frame) p->extension.end_player_frame(p->extension.ctx, e, g);
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(p, e->actor.id);
  double now = rr_now(g);
  double alpha = 1;
  if (p->intermission.playing && rr_health(g, e->actor.id) > 0 && extra->invisibility_until > now) {
    alpha = (extra->invisibility_fade_until - now) / 2; alpha = alpha < 0.1 ? 0.1 : alpha > 1 ? 1 : alpha;
  }
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "alpha"; ev.has_actor = true; ev.actor = e->actor.id; ev.alpha = alpha;
  rr_emit_rr(&p->rerelease_hooks, ev);
  qa_q2_rerelease_players_emit_flashlight(p, e->actor.id, g);
  if (p->intermission.playing && g->options.mode == QA_Q2_MODE_COOP && p->rerelease_options.coop_player_collision &&
      rr_band(e->clip_mask, 0x40000000) == 0 && rr_can_take_damage(g, e->actor.id)) {
    qa_body_state b = rr_body_of(g, e);
    qa_q2_trace_request req; memset(&req, 0, sizeof req);
    req.start = b.origin; req.end = b.origin; req.bounds = &b.bounds; req.ignore = &e->actor.id; req.mask = 0x40000000;
    qa_trace_result tr = g->host->trace(g->host->ctx, req);
    if (!tr.start_solid && !tr.all_solid) {
      e->clip_mask = rr_bor(e->clip_mask, 0x40000000);
      if (p->rerelease_hooks.player_collision) p->rerelease_hooks.player_collision(p->rerelease_hooks.ctx, e->actor.id, true);
    }
  }
}
bool qa_q2_rerelease_players_client_command(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, const char *cmd, const char *const *args, size_t n) {
  if (qa_q2rr_ieq(cmd, "help") && g->options.mode != QA_Q2_MODE_DEATHMATCH && p->has_extension) {
    p->extension.help(p->extension.ctx, e, g); return true;
  }
  return SUPER(client_command)(p->base.ctx, e, g, cmd, args, n);
}
static void post_respawn(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  if (rr_band(e->server_flags, 1) != 0) return;
  qa_q2rr_player_state *st = qa_q2_rerelease_players_context(p, e, g).state;
  qa_body_state b = rr_body_of(g, e);
  qa_q2rr_player_movement mv = p->hooks.movement(p->hooks.ctx, e->actor.id);
  qa_q2rr_movement_change mc; memset(&mc, 0, sizeof mc);
  mc.kind = "spawn"; mc.origin = b.origin; mc.velocity = b.velocity; mc.angles = mv.view_angles;
  mc.command_angles = mv.command_angles; mc.hold_milliseconds = 112; mc.spectator = st->spectator;
  p->hooks.set_movement(p->hooks.ctx, e->actor.id, mc);
  set_buf(st->event, sizeof st->event, "q2:player-teleport", sizeof st->event - 1);
  st->respawn_time = rr_now(g);
}
void qa_q2_rerelease_players_put_in_server(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, bool restore_loadout, const qa_q2_landmark_carry *lm) {
  qa_q2rr_player_context c = qa_q2_rerelease_players_context(p, e, g);
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(p, e->actor.id);
  if (p->hooks.select_spawn) {
    qa_q2rr_spawn_choice ms = p->hooks.select_spawn(p->hooks.ctx, e, g);
    if (ms.has) {
      qa_q2rr_placement v; memset(&v, 0, sizeof v); v.actor = e->actor.id; v.origin = ms.origin; v.angles = ms.angles;
      placement_set(&p->squad_spawns, &p->squad_count, &p->squad_cap, e->actor.id, v);
    }
  }
  if (lm) {
    extra->pending_landmark.has = true; extra->pending_landmark.name = lm->name;
    extra->pending_landmark.relative_origin = lm->relative_origin;
    extra->pending_landmark.relative_velocity = lm->relative_velocity;
    extra->pending_landmark.relative_view_angles = lm->relative_view_angles;
  }
  qa_q2_landmark_carry carry; const qa_q2_landmark_carry *carry_p = NULL;
  if (extra->pending_landmark.has) {
    carry.player = e->actor.id; carry.name = extra->pending_landmark.name;
    carry.relative_origin = extra->pending_landmark.relative_origin;
    carry.relative_velocity = extra->pending_landmark.relative_velocity;
    carry.relative_view_angles = extra->pending_landmark.relative_view_angles;
    carry_p = &carry;
  }
  if (!placement_get(p->squad_spawns, p->squad_count, e->actor.id)) {
    bool force = extra->awaiting_respawn && rr_now(g) > extra->respawn_timeout;
    qa_q2_entity *spot = qa_q2_select_rerelease_spawn(g, e, c.movement.standing_bounds, p->rerelease_options, p->rules.spawn_point, force);
    if (!spot && g->options.mode != QA_Q2_MODE_SINGLEPLAYER) {
      if (!extra->awaiting_respawn) extra->respawn_timeout = rr_now(g) + 3;
      extra->awaiting_respawn = true; extra->spawned = false;
      qa_q2_entity *pts[64]; size_t pn = qa_q2rr_entities_named(g, "info_player_intermission", pts, 64);
      qa_q2_entity *camera = NULL;
      if (pn == 0) {
        qa_q2_entity *s1[1]; size_t n1 = qa_q2rr_entities_named(g, "info_player_start", s1, 1);
        if (n1) camera = s1[0];
        else { qa_q2_entity *s2[1]; size_t n2 = qa_q2rr_entities_named(g, "info_player_deathmatch", s2, 1); if (n2) camera = s2[0]; }
      } else camera = pts[(size_t)((int64_t)floor(g->host->random(g->host->ctx) * 4) % (int64_t)pn)];
      qa_body_state b; memset(&b, 0, sizeof b);
      if (camera) b = rr_body_of(g, camera);
      c.state->dead = false; c.state->noclip = true; e->visible = false; e->server_flags = rr_bor(e->server_flags, 1);
      qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_origin = true; ch.origin = b.origin; ch.has_velocity = true; ch.velocity = QA_Q2_ZERO;
      g->move(g->ctx, e, ch, true); g->solid(g->ctx, e, "none");
      qa_q2rr_movement_change mc; memset(&mc, 0, sizeof mc); mc.kind = "freeze"; mc.origin = b.origin; mc.angles = b.angles;
      p->hooks.set_movement(p->hooks.ctx, e->actor.id, mc);
      qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "visibility"; ev.has_actor = true; ev.actor = e->actor.id; ev.visible = false; rr_emit_host(g, ev);
      g->link(g->ctx, e); return;
    }
    qa_vec3 porigin = QA_Q2_ZERO, pangles = QA_Q2_ZERO, pvel = QA_Q2_ZERO; bool placed = false;
    if (spot && carry_p && qa_q2rr_spawns.place_landmark)
      placed = qa_q2rr_spawns.place_landmark(qa_q2rr_spawns.ctx, e, g, carry_p, spot, c.movement.standing_bounds, &porigin, &pangles, &pvel);
    qa_vec3 origin = placed ? porigin : (spot ? rr_body_of(g, spot).origin : QA_Q2_ZERO);
    qa_vec3 angles = placed ? pangles : (spot ? rr_body_of(g, spot).angles : QA_Q2_ZERO);
    qa_q2rr_placement v; memset(&v, 0, sizeof v); v.actor = e->actor.id;
    v.origin = qa_q2_add(origin, rr_v3(0, 0, g->options.mode == QA_Q2_MODE_DEATHMATCH ? 10 : 1));
    v.angles = angles; v.angles.x = angles.x / 3;
    v.velocity = placed ? pvel : QA_Q2_ZERO; v.from_landmark = placed;
    placement_set(&p->selected_spawns, &p->selected_count, &p->selected_cap, e->actor.id, v);
  }
  bool was_waiting = extra->awaiting_respawn;
  extra->awaiting_respawn = false; extra->respawn_timeout = 0; extra->pending_landmark.has = false;
  e->clip_mask = (g->options.mode == QA_Q2_MODE_COOP && !p->rerelease_options.coop_player_collision) ? 0x2010003 : 0x42010003;
  SUPER(put_in_server)(p->base.ctx, e, g, restore_loadout, carry_p);
  if (p->rerelease_hooks.player_collision)
    p->rerelease_hooks.player_collision(p->rerelease_hooks.ctx, e->actor.id, rr_band(e->clip_mask, 0x40000000) != 0);
  extra->slime_debounce = 0; extra->animation_time = 0; extra->slow_view_angles = QA_Q2_ZERO;
  extra->coop_respawn_state = QA_Q2_COOP_NONE; extra->invisibility_until = 0; extra->invisibility_fade_until = 0;
  if (p->has_extension && p->extension.spawned) p->extension.spawned(p->extension.ctx, e, g);
  if (qa_q2rr_ieq(g->options.map_name, "rboss") && g->options.mode != QA_Q2_MODE_DEATHMATCH && c.state->use_q2_inventory) {
    qa_inventory_entry en; memset(&en, 0, sizeof en); en.item = "q2:key_nuke"; en.count = 1; en.capacity = 1;
    qa_shared_inventory_table_configure(g->host->inventory, e->actor, en, NULL, NULL);
  }
  if (was_waiting) post_respawn(p, e, g);
}
void qa_q2_rerelease_players_respawn(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  if (g->options.mode == QA_Q2_MODE_SINGLEPLAYER) {
    qa_q2rr_player_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "load-menu"; ev.has_actor = true; ev.actor = e->actor.id;
    p->hooks.emit(p->hooks.ctx, ev); return;
  }
  if (!qa_q2_rerelease_players_context(p, e, g).state->spectator && p->base.copy_to_body_queue)
    p->base.copy_to_body_queue(p->base.ctx, e, g);
  e->server_flags = rr_bnot_and(e->server_flags, 1);
  qa_q2_rerelease_players_put_in_server(p, e, g, true, NULL);
  post_respawn(p, e, g);
}
bool qa_q2_rerelease_players_kill_box(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  return qa_q2_kill_rerelease_box(e, g, p, true, true);
}
static bool squad_target(qa_q2_rerelease_players *p, qa_q2_game_services *g, qa_vec3 *origin, qa_vec3 *angles) {
  bool searching = p->rerelease_hooks.monsters_searching(p->rerelease_hooks.ctx, NULL);
  double now = rr_now(g);
  size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
  for (size_t i = 0; i < pn; i++) {
    qa_q2_entity *ent = rr_ent(g, &pls[i]);
    qa_q2rr_player_state *st = qa_q2rr_players_get_state(p, pls[i]);
    qa_q2rr_player_extra_state *x = qa_q2rr_players_get_extra_or_null(p, pls[i]);
    if (!ent || !st || !x || st->dead) continue;
    double firing_until = x->last_firing_until;
    if (p->weapons && p->weapons->get_state) {
      qa_q2rr_weapon_state_entry *w = p->weapons->get_state(p->weapons->ctx, pls[i]);
      if (w && w->has_last_firing_time) firing_until = w->last_firing_time;
    }
    if (x->last_damage_until >= now || p->rerelease_hooks.monsters_searching(p->rerelease_hooks.ctx, &pls[i]) ||
        (searching && firing_until >= now)) { x->coop_respawn_state = QA_Q2_COOP_IN_COMBAT; continue; }
    if (!p->rerelease_hooks.grounded_on_world(p->rerelease_hooks.ctx, pls[i]) ||
        p->hooks.movement(p->hooks.ctx, pls[i]).water_level >= 3) { x->coop_respawn_state = QA_Q2_COOP_BAD_AREA; continue; }
    qa_vec3 spot;
    if (!qa_q2_rerelease_players_find_respawn_spot(p, ent, g, &spot)) { x->coop_respawn_state = QA_Q2_COOP_BLOCKED; continue; }
    *origin = spot; *angles = rr_body_of(g, ent).angles; angles->z = 0;
    return true;
  }
  return false;
}
static void coop_respawn(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2rr_player_state *st = qa_q2_rerelease_players_context(p, e, g).state;
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(p, e->actor.id);
  qa_q2_rerelease_options *o = &p->rerelease_options;
  bool allowed = true;
  if (o->coop_lives && extra->lives == 0) { extra->coop_respawn_state = QA_Q2_COOP_NO_LIVES; allowed = false; }
  else if (o->coop_squad_respawn) {
    size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
    bool any_alive = false;
    for (size_t i = 0; i < pn; i++) if (rr_health(g, pls[i]) > 0) any_alive = true;
    if (any_alive) {
      qa_vec3 origin, angles;
      if (!squad_target(p, g, &origin, &angles)) allowed = false;
      else {
        qa_q2rr_placement v; memset(&v, 0, sizeof v); v.actor = e->actor.id; v.origin = origin; v.angles = angles;
        placement_set(&p->squad_spawns, &p->squad_count, &p->squad_cap, e->actor.id, v);
      }
    }
  }
  if (allowed) {
    extra->coop_respawn_state = QA_Q2_COOP_NONE; st->spectator = false; st->requested_spectator = false;
    qa_q2_rerelease_players_respawn(p, e, g); st->latched_buttons = 0;
  } else {
    if (extra->coop_respawn_state == QA_Q2_COOP_NONE) extra->coop_respawn_state = QA_Q2_COOP_WAITING;
    if (!st->spectator) {
      if (p->base.copy_to_body_queue) p->base.copy_to_body_queue(p->base.ctx, e, g);
      st->spectator = true; st->noclip = true;
      g->solid(g->ctx, e, "none");
      qa_combat_trait_changes tc; memset(&tc, 0, sizeof tc); tc.has_can_take_damage = true; tc.can_take_damage = false;
      qa_gameplay_authority_set_traits(g->host->combat, e->actor, tc);
      e->visible = false;
      qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "visibility"; ev.has_actor = true; ev.actor = e->actor.id; ev.visible = false; rr_emit_host(g, ev);
      st->damage_alpha = 0; st->bonus_alpha = 0;
      qa_q2rr_movement_change mc; memset(&mc, 0, sizeof mc); mc.kind = "noclip"; mc.enabled = true;
      p->hooks.set_movement(p->hooks.ctx, e->actor.id, mc);
      g->link(g->ctx, e);
      if (p->base.chase) p->base.chase(p->base.ctx, e, g, 1);
    }
  }
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "coop-respawn"; ev.has_actor = true; ev.actor = e->actor.id;
  ev.coop_state = extra->coop_respawn_state; ev.lives = extra->lives;
  rr_emit_rr(&p->rerelease_hooks, ev);
}
bool qa_q2_rerelease_players_find_respawn_spot(qa_q2_rerelease_players *p, qa_q2_entity *player, qa_q2_game_services *g, qa_vec3 *out) {
  qa_body_state b = rr_body_of(g, player);
  qa_bounds bounds = p->hooks.movement(p->hooks.ctx, player->actor.id).standing_bounds;
  const double mask = 0x201001b;
  qa_q2_trace_request req; memset(&req, 0, sizeof req);
  req.start = b.origin; req.end = b.origin; req.bounds = &bounds; req.ignore = &player->actor.id; req.mask = mask;
  qa_trace_result first = g->host->trace(g->host->ctx, req);
  if (first.start_solid || first.all_solid) return false;
  static const double yaws[] = {0, 90, 45, -45, -90};
  for (size_t yi = 0; yi < 5; yi++) {
    qa_q2_trace_request r2 = req; r2.end = qa_q2_add(b.origin, rr_v3(0, 0, 128));
    qa_trace_result up = g->host->trace(g->host->ctx, r2);
    if (up.start_solid || up.all_solid || rr_band(g->host->point_contents(g->host->ctx, up.end), 24) != 0) continue;
    qa_vec3 back_dir = qa_q2_movedir(rr_v3(0, b.angles.y + 180 + yaws[yi], 0));
    qa_q2_trace_request r3 = req; r3.start = up.end; r3.end = qa_q2_add(up.end, qa_q2_scale(back_dir, 128));
    qa_trace_result back = g->host->trace(g->host->ctx, r3);
    if (back.start_solid || back.all_solid || rr_band(g->host->point_contents(g->host->ctx, back.end), 24) != 0) continue;
    qa_q2_trace_request r4 = req; r4.start = back.end; r4.end = qa_q2_add(back.end, rr_v3(0, 0, -512));
    qa_trace_result fl = g->host->trace(g->host->ctx, r4);
    if (fl.start_solid || fl.all_solid || fl.fraction == 1) continue;
    if (fl.hit.kind != QA_TRACE_HIT_WORLD) continue;
    if (rr_band(g->host->point_contents(g->host->ctx, fl.end), 24) != 0) continue;
    if (rr_band(g->host->point_contents(g->host->ctx, qa_q2_add(fl.end, rr_v3(0, 0, 22))), 56) != 0) continue;
    if (!fl.contact.has_plane) continue;
    if (fl.contact.plane.normal.z < 0.7) continue;
    double height = fabs(b.origin.z - fl.end.z);
    if (height > 72) continue;
    if (height > 18) {
      qa_q2_trace_request rp = req; rp.bounds = NULL; rp.start = b.origin; rp.end = fl.end;
      if (g->host->trace(g->host->ctx, rp).fraction != 1) continue;
      rp.start = qa_q2_add(b.origin, rr_v3(0, 0, 22)); rp.end = qa_q2_add(fl.end, rr_v3(0, 0, 22));
      if (g->host->trace(g->host->ctx, rp).fraction != 1) continue;
    }
    *out = fl.end; return true;
  }
  return false;
}
void qa_q2_rerelease_players_spawn_placement(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g,
                                             const qa_q2_landmark_carry *lm, qa_vec3 *origin, qa_vec3 *angles,
                                             qa_vec3 *velocity, bool *from_landmark) {
  qa_q2rr_placement *sq = placement_get(p->squad_spawns, p->squad_count, e->actor.id);
  if (!sq) {
    qa_q2rr_placement *sel = placement_get(p->selected_spawns, p->selected_count, e->actor.id);
    if (sel) {
      *origin = sel->origin; *angles = sel->angles; *velocity = sel->velocity; *from_landmark = sel->from_landmark;
      placement_del(p->selected_spawns, &p->selected_count, e->actor.id); return;
    }
    SUPER(spawn_placement)(p->base.ctx, e, g, lm, origin, angles, velocity, from_landmark); return;
  }
  *origin = sq->origin; *angles = sq->angles; *velocity = QA_Q2_ZERO; *from_landmark = false;
  placement_del(p->squad_spawns, &p->squad_count, e->actor.id);
}
void qa_q2_rerelease_players_world_effects(qa_q2_rerelease_players *p, qa_q2rr_player_context *c) {
  qa_q2_rerelease_world_effects(c, qa_q2_rerelease_players_get_extra(p, c->entity->actor.id));
}
void qa_q2_rerelease_players_falling_damage_noop(qa_q2rr_player_context *c) { (void)c; }
void qa_q2_rerelease_players_damage_feedback(qa_q2_rerelease_players *p, qa_q2rr_player_context *c, double pain_index, double *flashes, double *out_pain) {
  qa_q2rr_feedback_result r = qa_q2_rerelease_damage_feedback(c, qa_q2_rerelease_players_get_extra(p, c->entity->actor.id), pain_index);
  *flashes = r.flashes; *out_pain = r.pain_index;
}
qa_q2rr_player_view qa_q2_rerelease_players_build_view(qa_q2_rerelease_players *p, qa_q2rr_player_context *c, double flashes, bool intermission) {
  qa_q2rr_player_view v = qa_q2_rerelease_build_view(c, qa_q2_rerelease_players_get_extra(p, c->entity->actor.id), flashes, intermission);
  if (p->has_intermission_fade_until) {
    double a = 1 - (p->intermission_fade_until - rr_now(c->game) - 0.3);
    a = a < 0 ? 0 : a > 1 ? 1 : a;
    v.blend = (qa_vec4){0, 0, 0, a};
  }
  return v;
}
void qa_q2_rerelease_players_update_bob(qa_q2_rerelease_players *p, qa_q2rr_player_context *c) {
  (void)p;
  qa_body_state b = rr_body_of(c->game, c->entity);
  double speed = hypot(b.velocity.x, b.velocity.y);
  qa_q2rr_player_state *st = c->state;
  if (speed < 5) { st->bob_move = 0; st->bob_time = 0; }
  else if (c->movement.grounded) st->bob_move = rr_frame(c->game) / (speed > 210 ? 0.4 : speed > 100 ? 0.8 : 1.6);
  st->bob_time += st->bob_move;
}
void qa_q2_rerelease_players_client_animation(qa_q2_rerelease_players *p, qa_q2rr_player_context *c) {
  qa_q2_rerelease_client_animation(c, qa_q2_rerelease_players_get_extra(p, c->entity->actor.id));
}
#undef SUPER
