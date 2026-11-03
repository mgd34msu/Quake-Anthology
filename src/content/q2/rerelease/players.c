/* Port of src/content/q2/rerelease/players.ts. GPL-2.0-or-later.
 * The overrides below are installed in the base Q2Players vtable; TS
 * super.<method>() is qa_q2_players_super_<method>(p->base, ...) and TS
 * this.<method>() is the dispatching qa_q2_players_<method>(p->base, ...). */
#include "qa/content/q2/rerelease/players.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/base/player/landmarks.h"
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
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

struct qa_q2rr_release_record {
  qa_q2_rerelease_players *players;
  qa_session_actor_registry *registry;
  qa_actor_id actor;
  int64_t subscription;
};

static void set_buf(char *dst, size_t cap, const char *src, size_t max) {
  if (!src) src = "";
  size_t n = strlen(src); if (n > max) n = max; if (n >= cap) n = cap - 1;
  memcpy(dst, src, n); dst[n] = '\0';
}
static qa_inventory_entry *copy_entries(const qa_inventory_entry *entries, size_t n) {
  if (n == 0) return NULL;
  qa_inventory_entry *copy = malloc(n * sizeof *copy);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, entries, n * sizeof *copy);
  return copy;
}
static qa_armor_state no_armor(void) {
  qa_armor_state none; memset(&none, 0, sizeof none);
  none.regular.kind = QA_REGULAR_ARMOR_NONE; none.powered.kind = "none";
  return none;
}
static qa_q2_base_player_state *state_of(qa_q2_rerelease_players *p, qa_actor_id a) {
  return qa_q2_players_get_state(p->base, a);
}
static qa_q2_player_hooks *hooks_of(qa_q2_rerelease_players *p) { return qa_q2_players_hooks(p->base); }
static bool playing(qa_q2_rerelease_players *p) { return strcmp(qa_q2_players_intermission(p->base).kind, "playing") == 0; }

/* ---- rereleaseStates / squadSpawns / selectedSpawns maps ---- */
qa_q2rr_player_extra_state *qa_q2rr_players_get_extra_or_null(qa_q2_rerelease_players *p, qa_actor_id a) {
  for (size_t i = 0; i < p->extra_count; i++) if (qa_same_actor(p->rerelease_states[i].actor, a)) return p->rerelease_states[i].state;
  return NULL;
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

qa_q2rr_player_extra_state *qa_q2_rerelease_players_get_extra(qa_q2_rerelease_players *p, qa_actor_id a) {
  qa_q2rr_player_extra_state *v = qa_q2rr_players_get_extra_or_null(p, a);
  if (!v) qa_q2rr_error("Q2 rerelease player is not admitted");
  return v;
}

/* q2Userinfo (base/player/index.ts): first occurrence wins. */
static const char *userinfo_get(const char *src, const char *key, char *buf, size_t cap) {
  return qa_q2_userinfo_get(src, key, buf, cap);
}
static bool userinfo_has(const char *src, const char *key) {
  char scratch[2];
  return qa_q2_userinfo_get(src, key, scratch, sizeof scratch) != NULL;
}
/* Number.parseInt(value ?? "0", 10) || 0 */
static double info_int(const char *v) {
  double value = qa_js_parse_int(v ? v : "0", 10);
  return isnan(value) || value == 0 ? 0 : value;
}

/* ---- overrides ---- */
static bool rr_spawn(void *self, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)self;
  qa_q2_spawn_module m = qa_q2_get_rerelease_spawns();
  return m.spawn(m.ctx, e, g);
}
/* TS { ...super.callbacks, ...q2RereleaseSpawns.callbacks }: each kind the
 * spawns table defines replaces the base kind. */
static qa_q2_callback_definitions rr_callbacks(void *self) {
  qa_q2_rerelease_players *p = self;
  qa_q2_callback_definitions out = qa_q2_players_super_callbacks(p->base), sp = qa_q2_rerelease_spawns_callbacks();
  if (sp.trajectory) { out.trajectory = sp.trajectory; out.trajectory_count = sp.trajectory_count; }
  if (sp.think) { out.think = sp.think; out.think_count = sp.think_count; }
  if (sp.use) { out.use = sp.use; out.use_count = sp.use_count; }
  if (sp.touch) { out.touch = sp.touch; out.touch_count = sp.touch_count; }
  if (sp.pain) { out.pain = sp.pain; out.pain_count = sp.pain_count; }
  if (sp.die) { out.die = sp.die; out.die_count = sp.die_count; }
  if (sp.blocked) { out.blocked = sp.blocked; out.blocked_count = sp.blocked_count; }
  p->merged_callbacks = out;
  return out;
}

static qa_q2_connection_result rr_connect(void *self, qa_q2_game_services *g, const char *userinfo, bool is_bot) {
  qa_q2_rerelease_players *p = self;
  qa_q2_base_player_rules *rules = qa_q2_players_rules(p->base);
  char b1[2048], b2[2048];
  const char *spec = userinfo_get(userinfo, "spectator", b1, sizeof b1);
  bool spectator = g->options.mode == QA_Q2_MODE_DEATHMATCH && spec && spec[0] && strcmp(spec, "0") != 0;
  const char *password = spectator ? rules->spectator_password : rules->password;
  const char *reason = "";
  const char *given = userinfo_get(userinfo, spectator ? "spectator" : "password", b2, sizeof b2);
  if ((spectator || !is_bot) && password[0] && strcmp(password, "none") != 0 && (!given || strcmp(password, given) != 0))
    reason = spectator ? "Spectator password required or incorrect." : "Password required or incorrect.";
  else if (spectator) {
    size_t cnt = 0, n = qa_q2_players_state_count(p->base);
    for (size_t i = 0; i < n; i++) {
      qa_actor_id actor; qa_q2_base_player_state *st;
      if (qa_q2_players_state_at(p->base, i, &actor, &st) && st->connected && st->requested_spectator) cnt++;
    }
    if ((double)cnt >= rules->max_spectators) reason = "Server spectator limit is full.";
  }
  qa_q2_connection_result r; memset(&r, 0, sizeof r);
  if (!reason[0]) { r.allowed = true; set_buf(r.userinfo, sizeof r.userinfo, userinfo, sizeof r.userinfo - 1); return r; }
  /* [...info].filter(key !== "rejmsg") then rejmsg; info keeps the first
   * occurrence of each key. */
  r.allowed = false; set_buf(r.reason, sizeof r.reason, reason, sizeof r.reason - 1);
  size_t w = 0;
  const char *s = userinfo; if (s[0] == '\\') s++;
  bool tok_key = true; const char *kstart = NULL; size_t klen = 0;
  while (*s) {
    const char *end = strchr(s, '\\'); size_t len = end ? (size_t)(end - s) : strlen(s);
    if (tok_key) { kstart = s; klen = len; tok_key = false; }
    else {
      bool seen = false;
      for (const char *q = userinfo[0] == '\\' ? userinfo + 1 : userinfo; q < kstart;) {
        const char *qe = strchr(q, '\\'); size_t ql = qe ? (size_t)(qe - q) : strlen(q);
        if (ql == klen && strncmp(q, kstart, klen) == 0) { seen = true; break; }
        if (!qe) break;
        const char *ve = strchr(qe + 1, '\\');
        if (!ve) break;
        q = ve + 1;
      }
      if (!seen && !(klen == 6 && strncmp(kstart, "rejmsg", 6) == 0) && w + klen + len + 2 < sizeof r.userinfo) {
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
qa_q2_connection_result qa_q2_rerelease_players_connect(qa_q2_rerelease_players *p, qa_q2_game_services *g,
                                                        const char *userinfo, bool is_bot) {
  return rr_connect(p, g, userinfo, is_bot);
}

static void on_release_players(void *c, qa_owned_actor actor) {
  struct qa_q2rr_release_record *r = c;
  if (!qa_same_actor(actor.id, r->actor)) return;
  qa_q2_rerelease_players *p = r->players;
  qa_q2rr_players_delete_extra(p, actor.id);
  placement_del(p->squad_spawns, &p->squad_count, actor.id);
  qa_session_actor_registry_off_release(r->registry, r->subscription);
  for (size_t i = 0; i < p->release_count; i++)
    if (p->releases[i] == r) { memmove(&p->releases[i], &p->releases[i + 1], (p->release_count - i - 1) * sizeof *p->releases); p->release_count--; break; }
  free(r);
}

static qa_q2_base_player_carry rr_save_carry(void *self, qa_q2_entity *e, qa_q2_game_services *g);

static qa_q2_base_player_state *rr_attach(void *self, qa_q2_entity *e, qa_q2_game_services *g, qa_q2_player_admission a) {
  qa_q2_rerelease_players *p = self;
  qa_q2rr_player_identity id = p->rerelease_hooks.player_identity(p->rerelease_hooks.ctx, e->actor.id);
  qa_q2rr_player_extra_state *extra = calloc(1, sizeof *extra);
  if (!extra) qa_range_error("Out of memory");
  qa_q2rr_player_extra_state_init(extra, id.seat, id.social_id);
  extra->lives = (g->options.mode == QA_Q2_MODE_COOP && p->rerelease_options.coop_lives) ? p->rerelease_options.coop_num_lives + 1 : 0;
  qa_q2rr_player_extra_state *previous = qa_q2rr_players_get_extra_or_null(p, e->actor.id);
  if (previous) { qa_q2rr_player_extra_state_destroy(previous); free(previous); }
  if (!qa_q2rr_players_set_extra(p, e->actor.id, extra)) qa_range_error("Out of memory");
  qa_q2_base_player_state *state = qa_q2_players_super_attach(p->base, e, g, a);
  if (extra->auto_shield >= 0) e->flags = rr_bor(e->flags, 0x40000000);
  struct qa_q2rr_release_record *record = malloc(sizeof *record);
  if (!record) qa_range_error("Out of memory");
  *record = (struct qa_q2rr_release_record){p, g->host->actors, e->actor.id, 0};
  if (p->release_count == p->release_cap) {
    size_t nc = p->release_cap ? p->release_cap * 2 : 8;
    void *q = realloc(p->releases, nc * sizeof *p->releases); if (!q) qa_range_error("Out of memory");
    p->releases = q; p->release_cap = nc;
  }
  p->releases[p->release_count++] = record;
  record->subscription = qa_session_actor_registry_on_release(g->host->actors, on_release_players, record);
  if (p->has_extension && p->extension.admitted) p->extension.admitted(p->extension.ctx, e, g);
  size_t n = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, e->actor.id, &n);
  qa_inventory_entry *spawn = copy_entries(entries, n);
  free(state->spawn_inventory); state->spawn_inventory = spawn; state->spawn_inventory_count = n;
  qa_q2_base_player_carry carry = qa_q2_players_save_carry(p->base, e, g);
  qa_q2_base_player_carry_set(&state->coop_respawn, carry);
  qa_q2_base_player_carry_clear(&carry);
  state->has_coop_respawn = true;
  return state;
}

static void rr_userinfo_changed(void *self, qa_q2_entity *e, qa_q2_game_services *g, const char *source) {
  qa_q2_rerelease_players *p = self;
  qa_q2_base_player_state *st = state_of(p, e->actor.id);
  if (!st) qa_q2rr_error("Q2 rerelease userinfo requires an admitted player");
  char truncated[2048]; set_buf(truncated, sizeof truncated, source, 2047);
  char b[2048];
  const char *nm = userinfo_get(truncated, "name", b, sizeof b);
  char name[32]; set_buf(name, sizeof name, nm ? nm : "badinfo", 31);
  set_buf(st->userinfo, sizeof st->userinfo, truncated, 2047);
  char b2[2048]; const char *sk = userinfo_get(truncated, "skin", b2, sizeof b2);
  set_buf(st->skin, sizeof st->skin, sk ? sk : "male/grunt", sizeof st->skin - 1);
  char b3[2048]; const char *sp = userinfo_get(truncated, "spectator", b3, sizeof b3);
  st->requested_spectator = g->options.mode == QA_Q2_MODE_DEATHMATCH && sp && sp[0] && strcmp(sp, "0") != 0;
  char b4[2048]; double fov = info_int(userinfo_get(truncated, "fov", b4, sizeof b4));
  st->fov = qa_js_max(1, qa_js_min(160, fov));
  char b5[2048]; double hand = qa_js_max(0, qa_js_min(2, info_int(userinfo_get(truncated, "hand", b5, sizeof b5))));
  st->hand = hand == 1 ? "left" : hand == 2 ? "center" : "right";
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(p, e->actor.id);
  char b6[2048]; double asw = qa_js_max(0, qa_js_min(3, info_int(userinfo_get(truncated, "autoswitch", b6, sizeof b6))));
  char b7[2048]; const char *bob = userinfo_get(truncated, "bobskip", b7, sizeof b7);
  extra->bob_skip = bob ? bob[0] == '1' : false;
  extra->auto_switch = asw == 1 ? 1 : asw == 2 ? 2 : asw == 3 ? 3 : 0;
  char b8[2048]; const char *ash = userinfo_get(truncated, "autoshield", b8, sizeof b8);
  extra->auto_shield = userinfo_has(truncated, "autoshield") ? info_int(ash) : -1;
  char b9[2048]; const char *dt = userinfo_get(truncated, "dogtag", b9, sizeof b9);
  set_buf(extra->dogtag, sizeof extra->dogtag, dt ? dt : "", sizeof extra->dogtag - 1);
  qa_q2_player_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "userinfo"; ev.actor = e->actor.id; ev.slot = st->slot; ev.name = name; ev.skin = st->skin;
  hooks_of(p)->emit(hooks_of(p)->ctx, ev);
  qa_q2_rerelease_event rev; memset(&rev, 0, sizeof rev);
  rev.kind = "player-dogtag"; rev.has_actor = true; rev.actor = e->actor.id; rev.value = extra->dogtag;
  rr_emit_rr(&p->rerelease_hooks, rev);
  if (rr_band(e->server_flags, 16) != 0) set_buf(st->name, sizeof st->name, name, sizeof st->name - 1);
  else snprintf(st->name, sizeof st->name, "##P%s", QA_JS_NUMBER(st->slot));
}

void qa_q2_rerelease_players_weapon_picked(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_item_id item, bool first) {
  qa_q2w_weapons *weapons = qa_q2_players_weapons(p->base);
  size_t n = 0;
  const qa_q2w_definition *const *defs = weapons ? qa_q2w_weapons_registered_definitions(weapons, &n) : NULL;
  const qa_q2w_definition *weapon = NULL;
  for (size_t i = 0; i < n; i++) if (defs[i]->item && item && strcmp(defs[i]->item, item) == 0) { weapon = defs[i]; break; }
  qa_q2w_state *st = qa_q2_players_weapon_state(p->base, e->actor.id);
  if (!weapon || !st) return;
  if ((st->weapon && strcmp(st->weapon, weapon->name) == 0) || (st->pending && strcmp(st->pending, weapon->name) == 0)) return;
  qa_q2_inventory_item inv; bool has_inv = qa_q2_item_module_lookup(qa_q2_players_items(p->base), item, &inv);
  bool ammo_weapon = has_inv && inv.kind && strcmp(inv.kind, "ammo") == 0;
  double required = ammo_weapon ? 1 : weapon->quantity;
  if (weapon->ammo && qa_shared_inventory_table_count(g->host->inventory, e->actor.id, weapon->ammo) < required) return;
  double mode = qa_q2_rerelease_players_get_extra(p, e->actor.id)->auto_switch;
  if (mode == 3 || (mode == 2 && ammo_weapon) ||
      (mode == 0 && !(st->weapon && strcmp(st->weapon, "blaster") == 0) && (g->options.mode == QA_Q2_MODE_DEATHMATCH || !first))) return;
  st->pending = weapon->name;
}

typedef struct { qa_q2_rerelease_players *p; qa_q2_entity *e; qa_q2_game_services *g; qa_death_reaction reaction; } obit_ctx;
static void obit_score(void *c, qa_q2_base_player_state *r, double change) {
  obit_ctx *k = c;
  qa_q2_players_apply_score(k->p->base, k->e, k->g, k->reaction, r, change);
}
static void rr_obituary(void *self, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r) {
  qa_q2_rerelease_players *p = self;
  qa_q2_player_context c = qa_q2_players_context(p->base, e, g);
  double means = 0; bool no_loss = false;
  if (e->last_attack && e->last_attack->cause.kind == QA_ATTACK_CAUSE_Q2) {
    means = e->last_attack->cause.means_of_death;
    no_loss = e->last_attack->cause.has_native && e->last_attack->cause.native.edition &&
              strcmp(e->last_attack->cause.native.edition, "rerelease") == 0 && e->last_attack->cause.native.no_point_loss;
  }
  qa_q2_base_player_state *attacker = r.pain.attacker ? state_of(p, *r.pain.attacker) : NULL;
  obit_ctx oc = {p, e, g, r};
  qa_q2rr_obituary ob = qa_q2_rerelease_obituary(c.state, attacker, means, g->options.mode, no_loss, &oc, obit_score);
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "localized-print"; ev.level = "medium"; ev.text = ob.text; ev.args = ob.args; ev.arg_count = ob.arg_count;
  rr_emit_rr(&p->rerelease_hooks, ev);
}
static void rr_clear_death_inventory(void *self, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_players *p = self;
  if (g->options.mode == QA_Q2_MODE_COOP && !qa_q2_uses_instanced_items(p->rerelease_options))
    qa_q2_players_super_clear_death_inventory(p->base, e, g);
}
static bool rr_can_drop_coop_stay_items(void *self, qa_q2_game_services *g) {
  qa_q2_rerelease_players *p = self;
  return g->options.mode == QA_Q2_MODE_COOP && qa_q2_uses_instanced_items(p->rerelease_options);
}
static void rr_death(void *self, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r) {
  qa_q2_rerelease_players *p = self;
  double means = 0;
  if (e->last_attack && e->last_attack->cause.kind == QA_ATTACK_CAUSE_Q2)
    means = (double)(qa_js_to_int32(e->last_attack->cause.means_of_death) & ~(int32_t)0x8000000);
  e->model2 = ""; e->model3 = ""; e->sound = "";
  qa_q2_players_context(p->base, e, g).state->loop_sound = "";
  if (means == 51) {
    qa_gameplay_authority_set_health(g->host->combat, e->actor, -100);
    qa_death_reaction r2 = r; r2.pain.damage = 400;
    qa_q2_players_super_death(p->base, e, g, r2); return;
  }
  if (means == 47 && rr_health(g, e->actor.id) < -80) e->flags = rr_bor(e->flags, 0x10000);
  qa_q2_players_super_death(p->base, e, g, r);
}
static void rr_record_damage(void *self, qa_q2_entity *e, qa_q2_game_services *g, qa_damage_decision d) {
  qa_q2_rerelease_players *p = self;
  qa_q2_players_super_record_damage(p->base, e, g, d);
  if (d.has_feedback && d.feedback_kind && strcmp(d.feedback_kind, "q2") == 0 && !(d.reaction && strcmp(d.reaction, "death") == 0)) {
    double until = rr_now(g) + 2;
    qa_q2_rerelease_players_get_extra(p, e->actor.id)->last_damage_until = until;
  }
}
static qa_q2_base_player_carry rr_save_carry(void *self, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_players *p = self;
  qa_q2_base_player_carry c = qa_q2_players_super_save_carry(p->base, e, g);
  c.flags = rr_band(e->flags, 16 | 32 | 4096 | 0x400000 | 0x40000000);
  return c;
}
static void rr_restore_carry(void *self, qa_q2_entity *e, qa_q2_game_services *g, qa_q2_base_player_carry carry) {
  qa_q2_rerelease_players *p = self;
  qa_q2_players_super_restore_carry(p->base, e, g, carry);
  if (carry.health <= 0) {
    qa_q2_base_player_state *st = qa_q2_players_context(p->base, e, g).state;
    size_t n = 0;
    const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, e->actor.id, &n);
    /* copy first: configure mutates the table */
    qa_inventory_entry *copy = copy_entries(entries, n);
    for (size_t i = 0; i < n; i++) { qa_inventory_entry en = copy[i]; en.count = 0; qa_shared_inventory_table_configure(g->host->inventory, e->actor, en, NULL, NULL); }
    free(copy);
    if (st->use_q2_inventory) qa_q2_item_module_configure_player(qa_q2_players_items(p->base), e->actor, g, true);
    else for (size_t i = 0; i < st->spawn_inventory_count; i++) qa_shared_inventory_table_configure(g->host->inventory, e->actor, st->spawn_inventory[i], NULL, NULL);
    qa_gameplay_authority_set_health(g->host->combat, e->actor, 100);
    qa_gameplay_authority_set_armor(g->host->combat, e->actor, no_armor());
    e->max_health = 100;
    e->flags = rr_bnot_and(e->flags, 16 | 32 | 4096 | 0x400000 | 0x40000000);
    e->power_cubes = 0;
    st->god = false; st->notarget = false;
    st->selected_item = st->use_q2_inventory ? (g->options.instagib ? "q2:weapon_railgun" : "q2:weapon_blaster") : NULL;
    qa_combat_trait_changes tc; memset(&tc, 0, sizeof tc); tc.has_invulnerable = true; tc.invulnerable = false;
    qa_gameplay_authority_set_traits(g->host->combat, e->actor, tc);
    qa_q2w_state *w = qa_q2_players_weapon_state(p->base, e->actor.id);
    if (w) { w->weapon = "blaster"; w->pending = NULL; }
    qa_q2_player_hooks *hooks = hooks_of(p);
    if (st->use_q2_inventory && hooks->persistent_inventory_initialized) hooks->persistent_inventory_initialized(hooks->ctx, e, g);
    if (p->has_extension && p->extension.spawned) p->extension.spawned(p->extension.ctx, e, g);
    qa_q2_base_player_carry nc = qa_q2_players_save_carry(p->base, e, g);
    qa_q2_base_player_carry_set(&st->coop_respawn, nc);
    qa_q2_base_player_carry_clear(&nc);
    st->has_coop_respawn = true;
  }
  qa_q2_rerelease_players_get_extra(p, e->actor.id)->flashlight = rr_band(carry.flags, 0x400000) != 0;
}
static bool rr_record_death(void *self, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r) {
  qa_q2_rerelease_players *p = self;
  qa_q2_base_player_carry carry = qa_q2_players_save_carry(p->base, e, g);
  qa_q2_base_player_state *st = qa_q2_players_context(p->base, e, g).state;
  bool first = qa_q2_players_super_record_death(p->base, e, g, r);
  qa_q2_rerelease_players_get_extra(p, e->actor.id)->invisibility_until = 0;
  if (!first) { qa_q2_base_player_carry_clear(&carry); return false; }
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(p, e->actor.id);
  extra->animation_time = 0;
  qa_q2_rerelease_options *o = &p->rerelease_options;
  if (g->options.mode == QA_Q2_MODE_DEATHMATCH && o->deathmatch_force_respawn_time != 0)
    st->respawn_time = rr_now(g) + o->deathmatch_force_respawn_time;
  if (g->options.mode == QA_Q2_MODE_COOP && qa_q2_uses_instanced_items(*o)) {
    carry.health = e->max_health; carry.maximum_health = e->max_health;
    qa_q2_base_player_carry_set(&st->coop_respawn, carry);
    st->has_coop_respawn = true;
  }
  qa_q2_base_player_carry_clear(&carry);
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
static void rr_after_client_think(void *self, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_players *p = self;
  qa_q2_intermission intermission = qa_q2_players_intermission(p->base);
  if (strcmp(intermission.kind, "intermission") == 0 && ((intermission.map && intermission.map[0] == '\0') ||
      (qa_q2_is_n64(&g->options) && g->options.mode != QA_Q2_MODE_DEATHMATCH && !p->intermission_camera_set))) {
    qa_q2_base_player_state *st = qa_q2_players_context(p->base, e, g).state;
    st->buttons = hooks_of(p)->movement(hooks_of(p)->ctx, e->actor.id).buttons;
    return;
  }
  qa_q2_players_super_after_client_think(p->base, e, g);
  if (!playing(p)) return;
  qa_q2_player_context context = qa_q2_players_context(p->base, e, g);
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(p, e->actor.id);
  qa_q2_player_character_adapter adapter;
  qa_q2_character_context c = qa_q2_player_context_as_character(&context, &adapter);
  qa_q2_rerelease_falling_damage(&c, extra, p->rerelease_options, rr_frame(g));
}
void qa_q2_rerelease_players_record_weapon_fire(qa_q2_rerelease_players *p, qa_actor_id a, double now) {
  qa_q2_rerelease_players_get_extra(p, a)->last_firing_until = now + 2.5;
}
bool qa_q2_rerelease_players_cloak_alpha(qa_q2_rerelease_players *p, qa_actor_id a, double now, double *out) {
  qa_q2rr_player_extra_state *extra = qa_q2rr_players_get_extra_or_null(p, a);
  if (!extra || extra->invisibility_until <= now) return false;
  *out = extra->invisibility_fade_until <= now ? 0 : qa_js_max(0.1, qa_js_min(1, (extra->invisibility_fade_until - now) / 2));
  return true;
}
void qa_q2_rerelease_players_reveal_invisibility(qa_q2_rerelease_players *p, qa_actor_id a, double until) {
  qa_q2_rerelease_players_get_extra(p, a)->invisibility_fade_until = until;
}
void qa_q2_rerelease_players_movement_impact(qa_q2_rerelease_players *p, qa_actor_id a, double d, bool on_ladder) {
  qa_q2rr_player_extra_state *x = qa_q2_rerelease_players_get_extra(p, a);
  x->impact_delta = d; x->on_ladder = on_ladder;
}
static void rr_begin_frame(void *self, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_players *p = self;
  if (!playing(p)) return;
  double now = rr_now(g);
  if (qa_q2_rerelease_players_get_extra(p, e->actor.id)->awaiting_respawn) {
    if (fmod(qa_js_round(now * 1000), 500) == 0) qa_q2_players_put_in_server(p->base, e, g, true, NULL);
    return;
  }
  qa_q2_players_super_begin_frame(p->base, e, g);
}
static void coop_respawn(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g);
static void rr_dead_frame(void *self, qa_q2_player_context *c) {
  qa_q2_rerelease_players *p = self;
  qa_q2_entity *e = c->entity; qa_q2_game_services *g = c->game; qa_q2_base_player_state *st = c->state;
  qa_q2_rerelease_options *o = &p->rerelease_options;
  if (rr_now(g) <= st->respawn_time || p->coop_restart_time != 0) return;
  if (g->options.mode == QA_Q2_MODE_COOP && (o->coop_squad_respawn || o->coop_lives)) { coop_respawn(p, e, g); return; }
  double mask = g->options.mode == QA_Q2_MODE_DEATHMATCH ? 1 : -1;
  if (rr_band(st->latched_buttons, mask) != 0 || (g->options.mode == QA_Q2_MODE_DEATHMATCH && o->deathmatch_force_respawn)) {
    qa_q2_players_respawn(p->base, e, g); st->latched_buttons = 0;
  }
}
static void rr_begin_intermission(void *self, qa_q2_game_services *g, const char *map, const qa_q2_landmark_carry *lm) {
  qa_q2_rerelease_players_begin_rerelease_intermission(self, g, map, lm, 0);
}
static void set_intermission_exit(qa_q2_rerelease_players *p) {
  qa_q2_intermission next = qa_q2_players_intermission(p->base);
  next.exit = true;
  qa_q2_players_set_intermission(p->base, next);
}
void qa_q2_rerelease_players_begin_rerelease_intermission(qa_q2_rerelease_players *p, qa_q2_game_services *g, const char *map, const qa_q2_landmark_carry *lm, double flags) {
  if (!playing(p)) return;
  p->intermission_flags = flags; p->has_intermission_fade_until = false;
  qa_q2_intermission next = {.kind = "intermission", .map = map, .started = rr_now(g), .exit = false, .has_landmark = lm != NULL,
                             .landmark_first = true};
  if (lm) next.landmark = *lm;
  qa_q2_players_set_intermission(p->base, next);
  for (size_t i = 0; i < qa_q2_players_state_count(p->base); i++) {
    qa_actor_id actor; qa_q2_base_player_state *st;
    if (!qa_q2_players_state_at(p->base, i, &actor, &st)) break;
    qa_q2_entity *ent = rr_ent(g, &actor);
    if (!ent || !st->connected || rr_health(g, actor) > 0) continue;
    if (qa_q2_uses_instanced_items(p->rerelease_options) && st->has_coop_respawn) {
      st->coop_respawn.health = ent->max_health; st->coop_respawn.maximum_health = ent->max_health;
    }
    qa_q2_players_respawn(p->base, ent, g);
  }
  if (p->has_extension && p->extension.before_level_change) p->extension.before_level_change(p->extension.ctx, g);
  bool end_unit = strchr(map, '*') != NULL;
  if (end_unit) {
    if (g->options.mode == QA_Q2_MODE_COOP) {
      size_t ln = 0; const qa_q2_inventory_item *list = qa_q2_item_module_list(qa_q2_players_items(p->base), &ln);
      for (size_t i = 0; i < qa_q2_players_state_count(p->base); i++) {
        qa_actor_id actor; qa_q2_base_player_state *st;
        if (!qa_q2_players_state_at(p->base, i, &actor, &st)) break;
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
    else if (rr_band(flags, 64) != 0 && g->options.mode != QA_Q2_MODE_DEATHMATCH) { set_intermission_exit(p); return; }
  } else if (g->options.mode != QA_Q2_MODE_DEATHMATCH) { set_intermission_exit(p); return; }
  if (!p->intermission_camera_set || !p->has_intermission_camera) {
    size_t an = qa_q2rr_entities_named(g, "info_player_intermission", NULL, 0);
    qa_q2_entity **authored = an ? malloc(an * sizeof *authored) : NULL;
    if (an && !authored) qa_range_error("Out of memory");
    if (an) (void)qa_q2rr_entities_named(g, "info_player_intermission", authored, an);
    qa_q2_entity *spot = NULL;
    if (an == 0) {
      qa_q2_entity *s1[1]; size_t n1 = qa_q2rr_entities_named(g, "info_player_start", s1, 1);
      if (n1) spot = s1[0];
      else { qa_q2_entity *s2[1]; size_t n2 = qa_q2rr_entities_named(g, "info_player_deathmatch", s2, 1); if (n2) spot = s2[0]; }
    } else {
      /* TS rereleaseRandom?.integer(4) ?? Math.floor(random() * 4). */
      double sel = g->host->rerelease_random ? (double)qa_q2_rerelease_random_integer_max(g->host->rerelease_random, 4)
                                             : floor(g->host->random(g->host->ctx) * 4);
      spot = authored[(size_t)((int64_t)sel % (int64_t)an)];
    }
    free(authored);
    if (!spot) qa_q2rr_error("Q2 rerelease intermission has no camera or player spawn");
    qa_body_state b = rr_body_of(g, spot);
    p->has_intermission_camera = true; p->intermission_camera_origin = b.origin; p->intermission_camera_angles = b.angles;
  }
  qa_q2_rerelease_players_move_to_camera(p, g, p->intermission_camera_origin, p->intermission_camera_angles, true);
}
static void rr_check_rules(void *self, qa_q2_game_services *g) {
  qa_q2_rerelease_players *p = self;
  qa_q2_intermission intermission = qa_q2_players_intermission(p->base);
  bool in_intermission = strcmp(intermission.kind, "intermission") == 0;
  if (in_intermission && intermission.map && intermission.map[0] == '\0') return;
  if (in_intermission && intermission.exit) {
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
  qa_q2_players_super_check_rules(p->base, g);
}
static void rr_before_exit_level(void *self, qa_q2_game_services *g, const char *map) {
  qa_q2_rerelease_players *p = self;
  if (rr_band(p->intermission_flags, 8) != 0) {
    p->intermission_flags = rr_bnot_and(p->intermission_flags, 8);
    for (size_t i = 0; i < qa_q2_players_state_count(p->base); i++) {
      qa_actor_id actor; qa_q2_base_player_state *st;
      if (!qa_q2_players_state_at(p->base, i, &actor, &st)) break;
      qa_q2_entity *ent = rr_ent(g, &actor); if (!ent) continue;
      size_t n = 0; const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, actor, &n);
      qa_inventory_entry *copy = copy_entries(entries, n);
      for (size_t j = 0; j < n; j++) { qa_inventory_entry ne = copy[j]; ne.count = 0; qa_shared_inventory_table_configure(g->host->inventory, ent->actor, ne, NULL, NULL); }
      free(copy);
      qa_gameplay_authority_set_health(g->host->combat, ent->actor, 0);
      qa_gameplay_authority_set_armor(g->host->combat, ent->actor, no_armor());
      qa_q2_item_module_clear_powerups(qa_q2_players_items(p->base), actor);
      if (p->rerelease_hooks.clear_expansion_powerups) p->rerelease_hooks.clear_expansion_powerups(p->rerelease_hooks.ctx, actor);
      ent->flags = rr_bnot_and(ent->flags, 16 | 32 | 4096 | 0x400000 | 0x40000000);
      ent->power_cubes = 0;
      st->god = false; st->notarget = false;
      qa_q2_base_player_carry_clear(&st->coop_respawn); st->has_coop_respawn = false;
      st->selected_item = NULL;
    }
  }
  if (strchr(map, '*') && p->has_extension && p->extension.leave_unit) p->extension.leave_unit(p->extension.ctx);
}
void qa_q2_rerelease_players_fade_frame(qa_q2_rerelease_players *p, qa_q2_game_services *g) {
  if (!p->has_intermission_fade_until) return;
  double until = p->intermission_fade_until, now = rr_now(g);
  if (now >= until) { qa_q2_players_check_rules(p->base, g); return; }
  double alpha = qa_js_max(0, qa_js_min(1, 1 - (until - now - 0.3)));
  for (size_t i = 0; i < qa_q2_players_state_count(p->base); i++) {
    qa_actor_id actor; qa_q2_base_player_state *st;
    if (!qa_q2_players_state_at(p->base, i, &actor, &st)) break;
    if (!st->connected) continue;
    qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "screen-blend"; ev.has_actor = true; ev.actor = actor; ev.blend.w = alpha;
    rr_emit_rr(&p->rerelease_hooks, ev);
  }
}
void qa_q2_rerelease_players_move_to_camera(qa_q2_rerelease_players *p, qa_q2_game_services *g, qa_vec3 origin, qa_vec3 angles, bool entering) {
  p->has_intermission_camera = true; p->intermission_camera_origin = origin; p->intermission_camera_angles = angles;
  for (size_t i = 0; i < qa_q2_players_state_count(p->base); i++) {
    qa_actor_id actor; qa_q2_base_player_state *st;
    if (!qa_q2_players_state_at(p->base, i, &actor, &st)) break;
    qa_q2_entity *ent = rr_ent(g, &actor); if (!ent) continue;
    if (entering && rr_health(g, actor) <= 0) qa_q2_players_respawn(p->base, ent, g);
    if (entering) {
      qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "entity-event"; ev.has_actor = true; ev.actor = actor; ev.event = 7; rr_emit_host(g, ev);
    }
    st->show_help = false; st->show_scores = g->options.mode == QA_Q2_MODE_DEATHMATCH;
    st->damage_alpha = 0; st->bonus_alpha = 0; st->loop_sound = "";
    qa_q2_item_module_clear_powerups(qa_q2_players_items(p->base), actor);
    if (p->rerelease_hooks.clear_expansion_powerups) p->rerelease_hooks.clear_expansion_powerups(p->rerelease_hooks.ctx, actor);
    qa_q2_rerelease_players_get_extra(p, actor)->invisibility_until = 0;
    qa_combat_trait_changes tc; memset(&tc, 0, sizeof tc); tc.has_invulnerable = true; tc.invulnerable = st->god;
    qa_gameplay_authority_set_traits(g->host->combat, ent->actor, tc);
    qa_q2w_state *w = qa_q2_players_weapon_state(p->base, actor);
    if (w) { w->grenade_blew_up = false; w->grenade_time = 0; w->view_model = NULL; }
    ent->view_height = 0; ent->model = ""; ent->model2 = ""; ent->model3 = ""; ent->effects = 0; ent->sound = ""; ent->visible = false;
    g->solid(g->ctx, ent, "none"); g->motion(g->ctx, ent, "stationary");
    qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_origin = true; ch.origin = origin;
    g->move(g->ctx, ent, ch, true); g->show(g->ctx, ent);
    qa_q2_player_movement_change mc; memset(&mc, 0, sizeof mc); mc.kind = "freeze"; mc.origin = origin; mc.angles = angles;
    hooks_of(p)->set_movement(hooks_of(p)->ctx, actor, mc);
    if (st->show_scores) qa_q2_players_scoreboard(p->base, ent, g, true);
  }
}
void qa_q2_rerelease_players_finish_camera(qa_q2_rerelease_players *p, qa_q2_game_services *g) {
  qa_q2_intermission current = qa_q2_players_intermission(p->base);
  if (strcmp(current.kind, "playing") == 0) {
    qa_q2_intermission next = {.kind = "intermission", .map = "", .started = rr_now(g), .exit = false, .has_landmark = false};
    qa_q2_players_set_intermission(p->base, next);
  } else {
    current.started = rr_now(g);
    current.exit = current.map && current.map[0] && !strchr(current.map, '*');
    qa_q2_players_set_intermission(p->base, current);
  }
}
void qa_q2_rerelease_players_emit_flashlight(qa_q2_rerelease_players *p, qa_actor_id a, qa_q2_game_services *g) {
  qa_q2_base_player_state *st = state_of(p, a);
  if (!st) qa_q2rr_error("Flashlight player is not admitted");
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "flashlight"; ev.has_actor = true; ev.actor = a; ev.hand = st->hand;
  ev.enabled = qa_q2_rerelease_players_get_extra(p, a)->flashlight && playing(p) && rr_health(g, a) > 0;
  rr_emit_rr(&p->rerelease_hooks, ev);
}
static void rr_end_frame(void *self, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_players *p = self;
  if (p->has_extension && p->extension.begin_player_frame) p->extension.begin_player_frame(p->extension.ctx, e, g);
  qa_q2_players_super_end_frame(p->base, e, g);
  if (p->has_extension && p->extension.end_player_frame) p->extension.end_player_frame(p->extension.ctx, e, g);
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(p, e->actor.id);
  double now = rr_now(g);
  qa_q2w_state *weapon = qa_q2_players_weapon_state(p->base, e->actor.id);
  if ((weapon ? weapon->last_firing_time : 0) > now + 2.4) extra->invisibility_fade_until = now + 2;
  double alpha = 1;
  if (playing(p) && rr_health(g, e->actor.id) > 0 && extra->invisibility_until > now)
    alpha = qa_js_max(0.1, qa_js_min(1, (extra->invisibility_fade_until - now) / 2));
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "alpha"; ev.has_actor = true; ev.actor = e->actor.id; ev.alpha = alpha;
  rr_emit_rr(&p->rerelease_hooks, ev);
  qa_q2_rerelease_players_emit_flashlight(p, e->actor.id, g);
  if (playing(p) && g->options.mode == QA_Q2_MODE_COOP && p->rerelease_options.coop_player_collision &&
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
static bool rr_client_command(void *self, qa_q2_entity *e, qa_q2_game_services *g, const char *cmd, const char *const *args, size_t n) {
  qa_q2_rerelease_players *p = self;
  if (qa_q2rr_ieq(cmd, "help") && g->options.mode != QA_Q2_MODE_DEATHMATCH && p->has_extension) {
    p->extension.help(p->extension.ctx, e, g); return true;
  }
  return qa_q2_players_super_client_command(p->base, e, g, cmd, args, n);
}
static void post_respawn(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  if (rr_band(e->server_flags, 1) != 0) return;
  qa_q2_base_player_state *st = qa_q2_players_context(p->base, e, g).state;
  qa_body_state b = rr_body_of(g, e);
  qa_q2_player_hooks *hooks = hooks_of(p);
  qa_q2_player_movement mv = hooks->movement(hooks->ctx, e->actor.id);
  qa_q2_player_movement_change mc; memset(&mc, 0, sizeof mc);
  mc.kind = "spawn"; mc.origin = b.origin; mc.velocity = b.velocity; mc.angles = mv.view_angles;
  mc.command_angles = mv.command_angles; mc.hold_milliseconds = 112; mc.spectator = st->spectator;
  hooks->set_movement(hooks->ctx, e->actor.id, mc);
  st->event = "q2:player-teleport";
  st->respawn_time = rr_now(g);
}
static void rr_put_in_server(void *self, qa_q2_entity *e, qa_q2_game_services *g, bool restore_loadout, const qa_q2_landmark_carry *lm) {
  qa_q2_rerelease_players *p = self;
  qa_q2_player_context c = qa_q2_players_context(p->base, e, g);
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(p, e->actor.id);
  qa_q2_player_hooks *hooks = hooks_of(p);
  if (hooks->select_spawn) {
    qa_q2rr_placement v; memset(&v, 0, sizeof v); v.actor = e->actor.id;
    if (hooks->select_spawn(hooks->ctx, e, g, &v.origin, &v.angles))
      if (!placement_set(&p->squad_spawns, &p->squad_count, &p->squad_cap, e->actor.id, v)) qa_range_error("Out of memory");
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
    qa_q2_entity *spot = qa_q2_select_rerelease_spawn(g, e, c.movement.standing_bounds, p->rerelease_options,
                                                      qa_q2_players_rules(p->base)->spawn_point, force);
    if (!spot && g->options.mode != QA_Q2_MODE_SINGLEPLAYER) {
      if (!extra->awaiting_respawn) extra->respawn_timeout = rr_now(g) + 3;
      extra->awaiting_respawn = true; extra->spawned = false;
      size_t pn = qa_q2rr_entities_named(g, "info_player_intermission", NULL, 0);
      qa_q2_entity *camera = NULL;
      if (pn == 0) {
        qa_q2_entity *s1[1]; size_t n1 = qa_q2rr_entities_named(g, "info_player_start", s1, 1);
        if (n1) camera = s1[0];
        else { qa_q2_entity *s2[1]; size_t n2 = qa_q2rr_entities_named(g, "info_player_deathmatch", s2, 1); if (n2) camera = s2[0]; }
      } else {
        qa_q2_entity **pts = malloc(pn * sizeof *pts);
        if (!pts) qa_range_error("Out of memory");
        (void)qa_q2rr_entities_named(g, "info_player_intermission", pts, pn);
        camera = pts[(size_t)((int64_t)floor(g->host->random(g->host->ctx) * 4) % (int64_t)pn)];
        free(pts);
      }
      qa_body_state b; memset(&b, 0, sizeof b);
      if (camera) b = rr_body_of(g, camera);
      c.state->dead = false; c.state->noclip = true; e->visible = false; e->server_flags = rr_bor(e->server_flags, 1);
      qa_q2_body_changes ch; memset(&ch, 0, sizeof ch); ch.has_origin = true; ch.origin = b.origin; ch.has_velocity = true; ch.velocity = QA_Q2_ZERO;
      g->move(g->ctx, e, ch, true); g->solid(g->ctx, e, "none");
      qa_q2_player_movement_change mc; memset(&mc, 0, sizeof mc); mc.kind = "freeze"; mc.origin = b.origin; mc.angles = b.angles;
      hooks->set_movement(hooks->ctx, e->actor.id, mc);
      qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "visibility"; ev.has_actor = true; ev.actor = e->actor.id; ev.visible = false; rr_emit_host(g, ev);
      g->link(g->ctx, e); return;
    }
    qa_q2_landmark_placement placement;
    bool placed = spot && carry_p && qa_q2_place_landmark(e, g, *carry_p, spot, c.movement.standing_bounds, &placement);
    qa_vec3 origin = placed ? placement.origin : (spot ? rr_body_of(g, spot).origin : QA_Q2_ZERO);
    qa_vec3 angles = placed ? placement.angles : (spot ? rr_body_of(g, spot).angles : QA_Q2_ZERO);
    qa_q2rr_placement v; memset(&v, 0, sizeof v); v.actor = e->actor.id;
    v.origin = qa_q2_add(origin, rr_v3(0, 0, g->options.mode == QA_Q2_MODE_DEATHMATCH ? 10 : 1));
    v.angles = angles; v.angles.x = angles.x / 3;
    v.velocity = placed ? placement.velocity : QA_Q2_ZERO; v.from_landmark = placed;
    if (!placement_set(&p->selected_spawns, &p->selected_count, &p->selected_cap, e->actor.id, v)) qa_range_error("Out of memory");
  }
  bool was_waiting = extra->awaiting_respawn;
  extra->awaiting_respawn = false; extra->respawn_timeout = 0; extra->pending_landmark.has = false;
  e->clip_mask = (g->options.mode == QA_Q2_MODE_COOP && !p->rerelease_options.coop_player_collision) ? 0x2010003 : 0x42010003;
  qa_q2_players_super_put_in_server(p->base, e, g, restore_loadout, carry_p);
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
static void rr_respawn(void *self, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_rerelease_players *p = self;
  if (g->options.mode == QA_Q2_MODE_SINGLEPLAYER) {
    qa_q2_player_event ev; memset(&ev, 0, sizeof ev);
    ev.kind = "load-menu"; ev.actor = e->actor.id;
    hooks_of(p)->emit(hooks_of(p)->ctx, ev); return;
  }
  if (!qa_q2_players_context(p->base, e, g).state->spectator) qa_q2_players_copy_to_body_queue(p->base, e, g);
  e->server_flags = rr_bnot_and(e->server_flags, 1);
  qa_q2_players_put_in_server(p->base, e, g, true, NULL);
  post_respawn(p, e, g);
}
static bool rr_kill_box(void *self, qa_q2_entity *e, qa_q2_game_services *g) {
  return qa_q2_kill_rerelease_box(e, g, self, true, true);
}
static bool squad_target(qa_q2_rerelease_players *p, qa_q2_game_services *g, qa_vec3 *origin, qa_vec3 *angles) {
  bool searching = p->rerelease_hooks.monsters_searching(p->rerelease_hooks.ctx, NULL);
  double now = rr_now(g);
  size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
  for (size_t i = 0; i < pn; i++) {
    qa_q2_entity *ent = rr_ent(g, &pls[i]);
    qa_q2_base_player_state *st = state_of(p, pls[i]);
    qa_q2rr_player_extra_state *x = qa_q2rr_players_get_extra_or_null(p, pls[i]);
    if (!ent || !st || !x || st->dead) continue;
    qa_q2w_state *w = qa_q2_players_weapon_state(p->base, pls[i]);
    double firing_until = w ? w->last_firing_time : x->last_firing_until;
    if (x->last_damage_until >= now || p->rerelease_hooks.monsters_searching(p->rerelease_hooks.ctx, &pls[i]) ||
        (searching && firing_until >= now)) { x->coop_respawn_state = QA_Q2_COOP_IN_COMBAT; continue; }
    if (!p->rerelease_hooks.grounded_on_world(p->rerelease_hooks.ctx, pls[i]) ||
        hooks_of(p)->movement(hooks_of(p)->ctx, pls[i]).water_level >= 3) { x->coop_respawn_state = QA_Q2_COOP_BAD_AREA; continue; }
    qa_vec3 spot;
    if (!qa_q2_rerelease_players_find_respawn_spot(p, ent, g, &spot)) { x->coop_respawn_state = QA_Q2_COOP_BLOCKED; continue; }
    *origin = spot; *angles = rr_body_of(g, ent).angles; angles->z = 0;
    return true;
  }
  return false;
}
static void coop_respawn(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g) {
  qa_q2_base_player_state *st = qa_q2_players_context(p->base, e, g).state;
  qa_q2rr_player_extra_state *extra = qa_q2_rerelease_players_get_extra(p, e->actor.id);
  qa_q2_rerelease_options *o = &p->rerelease_options;
  bool allowed = true;
  if (o->coop_lives && extra->lives == 0) { extra->coop_respawn_state = QA_Q2_COOP_NO_LIVES; allowed = false; }
  else if (o->coop_squad_respawn) {
    size_t pn = 0; const qa_actor_id *pls = g->host->players(g->host->ctx, &pn);
    bool any_alive = false;
    for (size_t i = 0; i < pn; i++) if (rr_health(g, pls[i]) > 0) any_alive = true;
    if (any_alive) {
      qa_q2rr_placement v; memset(&v, 0, sizeof v); v.actor = e->actor.id;
      if (!squad_target(p, g, &v.origin, &v.angles)) allowed = false;
      else if (!placement_set(&p->squad_spawns, &p->squad_count, &p->squad_cap, e->actor.id, v)) qa_range_error("Out of memory");
    }
  }
  if (allowed) {
    extra->coop_respawn_state = QA_Q2_COOP_NONE; st->requested_spectator = false;
    qa_q2_players_respawn(p->base, e, g); st->latched_buttons = 0;
  } else {
    if (extra->coop_respawn_state == QA_Q2_COOP_NONE) extra->coop_respawn_state = QA_Q2_COOP_WAITING;
    if (!st->spectator) {
      qa_q2_players_copy_to_body_queue(p->base, e, g);
      st->spectator = true; st->noclip = true;
      g->solid(g->ctx, e, "none");
      qa_combat_trait_changes tc; memset(&tc, 0, sizeof tc); tc.has_can_take_damage = true; tc.can_take_damage = false;
      qa_gameplay_authority_set_traits(g->host->combat, e->actor, tc);
      e->visible = false;
      qa_q2_presentation_event ev; memset(&ev, 0, sizeof ev);
      ev.kind = "visibility"; ev.has_actor = true; ev.actor = e->actor.id; ev.visible = false; rr_emit_host(g, ev);
      st->damage_alpha = 0; st->bonus_alpha = 0;
      qa_q2_player_movement_change mc; memset(&mc, 0, sizeof mc); mc.kind = "noclip"; mc.enabled = true;
      hooks_of(p)->set_movement(hooks_of(p)->ctx, e->actor.id, mc);
      g->link(g->ctx, e);
      qa_q2_players_chase(p->base, e, g, 1, false);
    }
  }
  qa_q2_rerelease_event ev; memset(&ev, 0, sizeof ev);
  ev.kind = "coop-respawn"; ev.has_actor = true; ev.actor = e->actor.id;
  ev.coop_state = extra->coop_respawn_state; ev.lives = extra->lives;
  rr_emit_rr(&p->rerelease_hooks, ev);
}
bool qa_q2_rerelease_players_find_respawn_spot(qa_q2_rerelease_players *p, qa_q2_entity *player, qa_q2_game_services *g, qa_vec3 *out) {
  qa_body_state b = rr_body_of(g, player);
  qa_bounds bounds = hooks_of(p)->movement(hooks_of(p)->ctx, player->actor.id).standing_bounds;
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
static qa_q2_spawn_placement rr_spawn_placement(void *self, qa_q2_entity *e, qa_q2_game_services *g, const qa_q2_landmark_carry *lm) {
  qa_q2_rerelease_players *p = self;
  qa_q2rr_placement *sq = placement_get(p->squad_spawns, p->squad_count, e->actor.id);
  if (!sq) {
    qa_q2rr_placement *sel = placement_get(p->selected_spawns, p->selected_count, e->actor.id);
    if (sel) {
      qa_q2_spawn_placement out = {sel->origin, sel->angles, sel->velocity, sel->from_landmark};
      placement_del(p->selected_spawns, &p->selected_count, e->actor.id); return out;
    }
    return qa_q2_players_super_spawn_placement(p->base, e, g, lm);
  }
  qa_q2_spawn_placement out = {sq->origin, sq->angles, QA_Q2_ZERO, false};
  placement_del(p->squad_spawns, &p->squad_count, e->actor.id);
  return out;
}
static void rr_world_effects(void *self, qa_q2_player_context *c) {
  qa_q2_player_character_adapter adapter;
  qa_q2_character_context cc = qa_q2_player_context_as_character(c, &adapter);
  qa_q2_rerelease_world_effects(&cc, qa_q2_rerelease_players_get_extra(self, c->entity->actor.id));
}
static void rr_falling_damage(void *self, qa_q2_player_context *c) { (void)self; (void)c; }
static qa_q2_damage_feedback_result rr_damage_feedback(void *self, qa_q2_player_context *c, double pain_index) {
  return qa_q2_rerelease_damage_feedback(c, qa_q2_rerelease_players_get_extra(self, c->entity->actor.id), pain_index);
}
static qa_q2_base_player_view rr_build_view(void *self, qa_q2_player_context *c, double flashes, bool intermission) {
  qa_q2_rerelease_players *p = self;
  qa_q2_base_player_view v = qa_q2_rerelease_build_view(c, qa_q2_rerelease_players_get_extra(p, c->entity->actor.id), flashes, intermission);
  if (p->has_intermission_fade_until)
    v.blend = (qa_vec4){0, 0, 0, qa_js_max(0, qa_js_min(1, 1 - (p->intermission_fade_until - rr_now(c->game) - 0.3)))};
  return v;
}
static void rr_update_bob(void *self, qa_q2_player_context *c) {
  (void)self;
  qa_body_state b = rr_body_of(c->game, c->entity);
  double speed = hypot(b.velocity.x, b.velocity.y);
  qa_q2_base_player_state *st = c->state;
  if (speed < 5) { st->bob_move = 0; st->bob_time = 0; }
  else if (c->movement.grounded) st->bob_move = rr_frame(c->game) / (speed > 210 ? 0.4 : speed > 100 ? 0.8 : 1.6);
  st->bob_time += st->bob_move;
}
static void rr_client_animation(void *self, qa_q2_player_context *c) {
  qa_q2_rerelease_client_animation(c, qa_q2_rerelease_players_get_extra(self, c->entity->actor.id));
}

static const qa_q2_players_vtable RERELEASE_PLAYERS_VTABLE = {
  .spawn = rr_spawn, .callbacks = rr_callbacks, .connect = rr_connect, .attach = rr_attach,
  .userinfo_changed = rr_userinfo_changed, .save_carry = rr_save_carry, .restore_carry = rr_restore_carry,
  .spawn_placement = rr_spawn_placement, .kill_box = rr_kill_box, .put_in_server = rr_put_in_server,
  .respawn = rr_respawn, .death = rr_death, .obituary = rr_obituary, .clear_death_inventory = rr_clear_death_inventory,
  .can_drop_coop_stay_items = rr_can_drop_coop_stay_items, .record_death = rr_record_death,
  .after_client_think = rr_after_client_think, .begin_frame = rr_begin_frame, .dead_frame = rr_dead_frame,
  .world_effects = rr_world_effects, .falling_damage = rr_falling_damage, .damage_feedback = rr_damage_feedback,
  .build_view = rr_build_view, .update_bob = rr_update_bob, .client_animation = rr_client_animation,
  .end_frame = rr_end_frame, .record_damage = rr_record_damage, .begin_intermission = rr_begin_intermission,
  .check_rules = rr_check_rules, .before_exit_level = rr_before_exit_level, .client_command = rr_client_command,
};

bool qa_q2_rerelease_players_init(qa_q2_rerelease_players *p, qa_q2_item_module *items, qa_q2w_weapons *weapons,
                                  qa_q2_player_hooks hooks, qa_q2_rerelease_hooks rr_hooks,
                                  const qa_q2_rerelease_options_changes *options,
                                  const qa_q2_base_player_rules *rules, uint32_t rules_mask) {
  memset(p, 0, sizeof *p);
  p->rerelease_hooks = rr_hooks;
  p->base = qa_q2_players_create_derived(items, weapons, hooks, rules, rules_mask, &RERELEASE_PLAYERS_VTABLE, p);
  if (!p->base) return false;
  p->rerelease_options = qa_q2_create_rerelease_options(options);
  return true;
}
void qa_q2_rerelease_players_destroy(qa_q2_rerelease_players *p) {
  for (size_t i = 0; i < p->release_count; i++) {
    qa_session_actor_registry_off_release(p->releases[i]->registry, p->releases[i]->subscription);
    free(p->releases[i]);
  }
  for (size_t i = 0; i < p->extra_count; i++) { qa_q2rr_player_extra_state_destroy(p->rerelease_states[i].state); free(p->rerelease_states[i].state); }
  free(p->releases); free(p->rerelease_states); free(p->squad_spawns); free(p->selected_spawns);
  qa_q2_players_destroy(p->base);
  memset(p, 0, sizeof *p);
}

/* TS captureRerelease: actor ids are saved as their raw slot/generation. */
qa_q2_rerelease_players_checkpoint qa_q2_rerelease_players_capture(const qa_q2_rerelease_players *p) {
  qa_q2_rerelease_players_checkpoint cp;
  memset(&cp, 0, sizeof cp);
  cp.version = 1;
  cp.options = p->rerelease_options;
  cp.coop_restart_time = p->coop_restart_time;
  cp.deadly_kill_box = p->deadly_kill_box;
  cp.intermission_flags = p->intermission_flags;
  cp.has_intermission_fade_until = p->has_intermission_fade_until;
  cp.intermission_fade_until = p->intermission_fade_until;
  cp.has_intermission_camera = p->has_intermission_camera;
  cp.camera_origin = p->intermission_camera_origin;
  cp.camera_angles = p->intermission_camera_angles;
  cp.intermission_camera_set = p->intermission_camera_set;
  qa_q2rr_saved_player *players = calloc(p->extra_count ? p->extra_count : 1, sizeof *players);
  qa_q2rr_saved_placement *squads = calloc(p->squad_count ? p->squad_count : 1, sizeof *squads);
  if (!players || !squads) qa_range_error("Out of memory");
  for (size_t i = 0; i < p->extra_count; i++) {
    qa_actor_id actor = p->rerelease_states[i].actor;
    players[i].actor = (qa_saved_actor_id){(double)actor.slot, (double)actor.generation};
    if (!qa_q2rr_player_extra_state_clone(&players[i].state, p->rerelease_states[i].state)) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < p->squad_count; i++) {
    qa_actor_id actor = p->squad_spawns[i].actor;
    squads[i].actor = (qa_saved_actor_id){(double)actor.slot, (double)actor.generation};
    squads[i].origin = p->squad_spawns[i].origin;
    squads[i].angles = p->squad_spawns[i].angles;
  }
  cp.players = players; cp.player_count = p->extra_count;
  cp.squad_spawns = squads; cp.squad_count = p->squad_count;
  return cp;
}
void qa_q2_rerelease_players_checkpoint_clear(qa_q2_rerelease_players_checkpoint *cp) {
  qa_q2rr_saved_player *players = (qa_q2rr_saved_player *)cp->players;
  for (size_t i = 0; i < cp->player_count; i++) qa_q2rr_player_extra_state_destroy(&players[i].state);
  free(players);
  free((qa_q2rr_saved_placement *)cp->squad_spawns);
  cp->players = NULL; cp->player_count = 0;
  cp->squad_spawns = NULL; cp->squad_count = 0;
}
/* TS restoreRerelease: Object.assign of a structured clone per player. */
void qa_q2_rerelease_players_restore(qa_q2_rerelease_players *p, qa_q2_game_services *g,
                                     const qa_q2_rerelease_players_checkpoint *cp) {
  while (p->extra_count > 0) qa_q2rr_players_delete_extra(p, p->rerelease_states[0].actor);
  p->squad_count = 0;
  p->rerelease_options = cp->options;
  p->coop_restart_time = cp->coop_restart_time;
  p->deadly_kill_box = cp->deadly_kill_box;
  p->intermission_flags = cp->intermission_flags;
  p->has_intermission_fade_until = cp->has_intermission_fade_until;
  p->intermission_fade_until = cp->intermission_fade_until;
  p->has_intermission_camera = cp->has_intermission_camera;
  p->intermission_camera_origin = cp->camera_origin;
  p->intermission_camera_angles = cp->camera_angles;
  p->intermission_camera_set = cp->intermission_camera_set;
  for (size_t i = 0; i < cp->player_count; i++) {
    qa_actor_id actor = qa_q2_restore_actor(g, cp->players[i].actor).id;
    qa_q2rr_player_extra_state *state = malloc(sizeof *state);
    if (!state || !qa_q2rr_player_extra_state_clone(state, &cp->players[i].state)) qa_range_error("Out of memory");
    /* The clone owns its arrays and buffers; keep the borrowed landmark name. */
    state->pending_landmark.name = qa_q2_checkpoint_keep_string(state->pending_landmark.name);
    qa_q2rr_player_extra_state *previous = qa_q2rr_players_get_extra_or_null(p, actor);
    if (previous) { qa_q2rr_player_extra_state_destroy(previous); free(previous); } /* Map.set keeps position */
    if (!qa_q2rr_players_set_extra(p, actor, state)) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < cp->squad_count; i++) {
    qa_q2rr_placement placement;
    memset(&placement, 0, sizeof placement);
    placement.actor = qa_q2_restore_actor(g, cp->squad_spawns[i].actor).id;
    placement.origin = cp->squad_spawns[i].origin;
    placement.angles = cp->squad_spawns[i].angles;
    if (!placement_set(&p->squad_spawns, &p->squad_count, &p->squad_cap, placement.actor, placement))
      qa_range_error("Out of memory");
  }
}
