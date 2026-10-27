/* Port of src/content/q2/foundation/weapons/player.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/player.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/weapons/damage.h"
#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/content/q2/foundation/weapons/generic_frame.h"
#include "qa/content/q2/foundation/weapons/hand_grenade.h"
#include "qa/content/q2/foundation/weapons/presentation.h"
#include "qa/core/math.h"

static qa_vec3 v3(double x, double y, double z) { qa_vec3 v = {x, y, z}; return v; }
static qa_vec3 zv(void) { return v3(0, 0, 0); }
static bool grow(void **arr, size_t *cap, size_t count, size_t size) {
  if (count < *cap) return true;
  size_t next = *cap ? *cap * 2 : 8;
  void *p = realloc(*arr, next * size);
  if (!p) return false;
  *arr = p; *cap = next; return true;
}
static double now_of(qa_q2f_game *g) { return g->host->now(g->host->ctx); }
static double rnd_of(qa_q2f_game *g) { return g->host->random(g->host->ctx); }
static double health_of(qa_q2f_game *g, qa_actor_id a) {
  qa_combat_state_result r = g->host->combat.read(g->host->combat.ctx, a);
  return r.has_value ? r.value.health : 0;
}
static double inv_count(qa_q2f_game *g, qa_actor_id a, const char *item) {
  return g->host->inventory.count(g->host->inventory.ctx, a, item);
}
static void game_sound(qa_q2f_game *g, qa_owned_actor a, const char *path, bool has_ch, double ch, bool has_vol, double vol, bool has_att, double att) {
  g->sound(g->ctx, a, path, has_ch, ch, has_vol, vol, has_att, att);
}
static bool name_is(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

const char *qa_q2w_selection_name(qa_q2w_selection s) {
  switch (s) {
    case QA_Q2W_SEL_SELECTED: return "selected";
    case QA_Q2W_SEL_CURRENT: return "current";
    case QA_Q2W_SEL_NOT_OWNED: return "not-owned";
    case QA_Q2W_SEL_NO_AMMO: return "no-ammo";
    case QA_Q2W_SEL_NOT_ENOUGH_AMMO: return "not-enough-ammo";
  }
  return "selected";
}

bool qa_q2w_weapons_init(qa_q2w_weapons *w, qa_q2w_hooks hooks) {
  memset(w, 0, sizeof *w);
  if (!qa_q2w_ballistics_init(&w->base, hooks)) return false;
  for (size_t i = 0; i < qa_q2w_base_weapon_count; i++) {
    if (!grow((void **)&w->definitions, &w->definition_cap, w->definition_count, sizeof *w->definitions)) return false;
    w->definitions[w->definition_count++] = &qa_q2w_base_weapons[i];
  }
  return true;
}
void qa_q2w_weapons_destroy(qa_q2w_weapons *w) {
  free(w->definitions); free(w->extensions); free(w->fallback_order);
  qa_q2w_ballistics_destroy(&w->base);
  memset(w, 0, sizeof *w);
}
void qa_q2w_weapons_set_source_rules(qa_q2w_weapons *w, qa_q2w_source_rules rules) {
  if (w->has_source_rules) qa_q2f_error("Q2 source weapon rules already selected: %s", w->source_rules.kind);
  w->source_rules = rules; w->has_source_rules = true;
}
const qa_q2w_definition *qa_q2w_weapons_definition(qa_q2w_weapons *w, qa_q2w_name name) {
  for (size_t i = 0; i < w->definition_count; i++)
    if (strcmp(w->definitions[i]->name, name) == 0) return w->definitions[i];
  qa_q2f_error("Q2 weapon is not registered: %s", name);
}
const qa_q2w_definition *qa_q2w_weapons_definition_from_classname(qa_q2w_weapons *w, const char *classname) {
  for (size_t i = 0; i < w->definition_count; i++)
    if (strcmp(w->definitions[i]->classname, classname) == 0) return w->definitions[i];
  return NULL;
}
const qa_q2w_definition *const *qa_q2w_weapons_registered_definitions(qa_q2w_weapons *w, size_t *out_count) {
  *out_count = w->definition_count;
  return w->definitions;
}
void qa_q2w_weapons_register(qa_q2w_weapons *w, qa_q2w_extension extension) {
  for (size_t i = 0; i < w->definition_count; i++)
    if (strcmp(w->definitions[i]->name, extension.definition->name) == 0)
      qa_q2f_error("Q2 weapon already registered: %s", extension.definition->name);
  if (!grow((void **)&w->definitions, &w->definition_cap, w->definition_count, sizeof *w->definitions)) return;
  w->definitions[w->definition_count++] = extension.definition;
  if (!grow((void **)&w->extensions, &w->extension_cap, w->extension_count, sizeof *w->extensions)) return;
  w->extensions[w->extension_count++] = extension;
}
static qa_q2w_extension *extension_of(qa_q2w_weapons *w, const char *name) {
  for (size_t i = 0; i < w->extension_count; i++)
    if (strcmp(w->extensions[i].definition->name, name) == 0) return &w->extensions[i];
  return NULL;
}
void qa_q2w_weapons_set_fallback_order(qa_q2w_weapons *w, const qa_q2w_name *order, size_t count) {
  for (size_t i = 0; i < count; i++) (void)qa_q2w_weapons_definition(w, order[i]);
  free(w->fallback_order);
  w->fallback_order = NULL; w->fallback_count = 0; w->has_fallback = true;
  if (count) {
    w->fallback_order = malloc(count * sizeof *w->fallback_order);
    if (!w->fallback_order) return;
    memcpy(w->fallback_order, order, count * sizeof *order);
    w->fallback_count = count;
  }
}
static qa_q2w_state *require_state(qa_q2w_weapons *w, qa_q2w_owner self) {
  qa_q2w_state *s = qa_q2w_ballistics_state(&w->base, self.actor.id);
  if (!s) qa_q2f_error("Q2 player weapon state is not bound");
  return s;
}
qa_q2w_state *qa_q2w_weapons_bind(qa_q2w_weapons *w, qa_owned_actor self, qa_q2f_game *game, qa_q2w_state *state) {
  game->host->actors.assert_owned(game->host->actors.ctx, self);
  if (qa_q2w_ballistics_state(&w->base, self.id)) qa_q2f_error("Q2 weapon state already bound to actor");
  qa_q2w_ballistics_reset_silencer(&w->base, self.id);
  qa_q2w_ballistics_track_actors(&w->base, game);
  if (!state) {
    state = malloc(sizeof *state);
    if (!state) return NULL;
    qa_q2w_state_init_default(state);
  }
  if (!qa_q2w_ballistics_set_state(&w->base, self.id, state)) return NULL;
  return state;
}
qa_q2w_selection qa_q2w_weapons_request_weapon(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2f_game *game, qa_q2w_name name, bool allow_empty) {
  qa_q2w_state *state = require_state(w, self);
  for (size_t i = 0; i < w->extension_count; i++) {
    qa_q2w_extension *e = &w->extensions[i];
    if (e->selection_requested && strcmp(e->selection_requested, name) == 0
        && e->selection_choose && e->selection_choose(e->ctx, self, game, state)) {
      name = e->definition->name;
      break;
    }
  }
  const qa_q2w_definition *definition = qa_q2w_weapons_definition(w, name);
  if (name_is(state->weapon, name)) return QA_Q2W_SEL_CURRENT;
  if (inv_count(game, self.actor.id, definition->item) < 1) return QA_Q2W_SEL_NOT_OWNED;
  if (!allow_empty && definition->ammo && !name_is(definition->ammo, definition->item)) {
    double ammo = inv_count(game, self.actor.id, definition->ammo);
    if (ammo == 0) return QA_Q2W_SEL_NO_AMMO;
    if (ammo < definition->quantity) return QA_Q2W_SEL_NOT_ENOUGH_AMMO;
  }
  state->pending = name;
  return QA_Q2W_SEL_SELECTED;
}
void qa_q2w_weapons_request_holster(qa_q2w_weapons *w, qa_q2w_owner self) {
  qa_q2w_state *state = require_state(w, self);
  if (state->primary_handoff != QA_Q2W_HANDOFF_ACTIVE) return;
  state->primary_handoff = state->weapon == NULL ? QA_Q2W_HANDOFF_HOLSTERED : QA_Q2W_HANDOFF_HOLSTERING;
  state->pending = NULL;
}
bool qa_q2w_weapons_is_holstered(qa_q2w_weapons *w, qa_q2w_owner self) {
  return require_state(w, self)->primary_handoff == QA_Q2W_HANDOFF_HOLSTERED;
}
bool qa_q2w_weapons_continues_attack(qa_q2w_weapons *w, const qa_q2w_context *context) {
  (void)w;
  return context->state->primary_handoff == QA_Q2W_HANDOFF_ACTIVE && context->input.attack;
}

static bool make_context(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2f_game *game, qa_q2w_state *state,
                         qa_q2w_input input, bool silenced, qa_q2w_context *out) {
  if (state->weapon == NULL) return false;
  out->self = self; out->game = game; out->state = state; out->input = input;
  out->definition = qa_q2w_weapons_definition(w, state->weapon);
  out->now = now_of(game);
  out->rerelease = game->options.edition == QA_Q2F_RERELEASE;
  out->silenced = silenced;
  return true;
}
static bool context_default(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2f_game *game, qa_q2w_state *state,
                            qa_q2w_input input, qa_q2w_context *out) {
  return make_context(w, self, game, state, input, qa_q2w_ballistics_silencer_shots(&w->base, self.actor.id) > 0, out);
}
double qa_q2w_weapons_ammo(qa_q2w_weapons *w, qa_q2w_context *c) {
  (void)w;
  return c->definition->ammo == NULL ? INFINITY : inv_count(c->game, c->self.actor.id, c->definition->ammo);
}
void qa_q2w_weapons_consume(qa_q2w_weapons *w, qa_q2w_context *c, double count, bool infinite) {
  const qa_q2w_definition *d = c->definition;
  if (d->ammo == NULL || (infinite && (c->rerelease ? c->input.infinite_ammo : ((uint32_t)c->game->options.deathmatch_flags & 8192u) != 0))) return;
  double before = qa_q2w_weapons_ammo(w, c);
  if (!c->game->host->inventory.consume(c->game->host->inventory.ctx, c->self.actor, d->ammo, count))
    qa_q2f_error("Q2 weapon fired without its admitted ammunition");
  if (c->rerelease && before > d->warning && qa_q2w_weapons_ammo(w, c) <= d->warning)
    game_sound(c->game, c->self.actor, "weapons/lowammo.wav", true, 0, false, 0, false, 0);
  w->base.hooks.ammo_changed(w->base.hooks.ctx, c->self.actor.id, d->ammo);
}
void qa_q2w_weapons_consume_default(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_weapons_consume(w, c, c->definition->quantity, true);
}
static bool reserve_hand_grenade(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_state *state = c->state;
  if (state->hand_reservation != QA_Q2W_RES_NONE) return false;
  if (c->rerelease ? c->input.infinite_ammo : ((uint32_t)c->game->options.deathmatch_flags & 8192u) != 0) {
    state->hand_reservation = QA_Q2W_RES_INFINITE; return true;
  }
  double before = qa_q2w_weapons_ammo(w, c);
  if (!c->game->host->inventory.consume(c->game->host->inventory.ctx, c->self.actor, "q2:ammo_grenades", 1)) return false;
  state->hand_reservation = QA_Q2W_RES_FINITE;
  if (c->rerelease && before > c->definition->warning && qa_q2w_weapons_ammo(w, c) <= c->definition->warning)
    game_sound(c->game, c->self.actor, "weapons/lowammo.wav", true, 0, false, 0, false, 0);
  w->base.hooks.ammo_changed(w->base.hooks.ctx, c->self.actor.id, "q2:ammo_grenades");
  return true;
}
static void cancel_hand_preparation(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2f_game *game, qa_q2w_state *state) {
  qa_q2w_hand_reservation reservation = state->hand_reservation;
  state->hand_reservation = QA_Q2W_RES_NONE;
  if (reservation == QA_Q2W_RES_FINITE && state->grenade_time == 0 && game->host->actors.is_live(game->host->actors.ctx, self.actor.id)) {
    size_t count = 0;
    const qa_inventory_entry *entries = game->host->inventory.entries(game->host->inventory.ctx, self.actor.id, &count);
    const qa_inventory_entry *entry = NULL;
    for (size_t i = 0; i < count; i++) if (strcmp(entries[i].item, "q2:ammo_grenades") == 0) { entry = &entries[i]; break; }
    if (!entry) qa_q2f_error("Reserved hand grenade lost its canonical inventory entry");
    qa_inventory_entry next = *entry;
    next.count = entry->count + 1;
    game->host->inventory.configure(game->host->inventory.ctx, self.actor, next);
    w->base.hooks.ammo_changed(w->base.hooks.ctx, self.actor.id, "q2:ammo_grenades");
  }
}
void qa_q2w_weapons_no_ammo(qa_q2w_weapons *w, qa_q2w_context *c, bool sound) {
  qa_q2w_state *state = c->state;
  if (sound && c->now >= state->empty_sound_time) {
    game_sound(c->game, c->self.actor, "weapons/noammo.wav", true, c->rerelease ? 1 : 2, false, 0, false, 0);
    state->empty_sound_time = c->now + 1;
  }
  static const char *const order_rr[] = {"railgun", "hyperblaster", "chaingun", "machinegun", "supershotgun", "shotgun", "rocketlauncher", "grenadelauncher", "blaster"};
  static const char *const order_cl[] = {"railgun", "hyperblaster", "chaingun", "machinegun", "supershotgun", "shotgun", "blaster"};
  const qa_q2w_name *order = w->has_fallback ? w->fallback_order : (c->rerelease ? order_rr : order_cl);
  size_t count = w->has_fallback ? w->fallback_count : (c->rerelease ? 9 : 7);
  for (size_t i = 0; i < count; i++) {
    const qa_q2w_definition *d = qa_q2w_weapons_definition(w, order[i]);
    if (!name_is(order[i], "blaster") && inv_count(c->game, c->self.actor.id, d->item) == 0) continue;
    if (d->ammo && inv_count(c->game, c->self.actor.id, d->ammo) < d->quantity) continue;
    state->pending = order[i];
    break;
  }
}
void qa_q2w_weapons_animation(qa_q2w_weapons *w, qa_q2w_context *c, const char *priority, double first, double last) {
  if (!c->input.animate_player) return;
  qa_q2w_event e; memset(&e, 0, sizeof e);
  e.kind = QA_Q2W_EVENT_PLAYER_ANIMATION; e.actor = &c->self.actor.id; e.priority = priority;
  e.first = first; e.last = last; e.reset_time = c->rerelease;
  w->base.hooks.emit(w->base.hooks.ctx, e);
}
void qa_q2w_weapons_attack_animation(qa_q2w_weapons *w, qa_q2w_context *c, double offset) {
  qa_q2w_frame_range f = qa_q2w_attack_frames(c->input.ducked, offset);
  qa_q2w_weapons_animation(w, c, "attack", f.first, f.last);
}
static void reverse_animation(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_frame_range f = qa_q2w_reverse_frames(c->input.ducked);
  qa_q2w_weapons_animation(w, c, "reverse", f.first, f.last);
}
double qa_q2w_weapons_firing_interval(qa_q2w_weapons *w, qa_actor_id actor, double seconds) {
  return w->base.hooks.firing_interval ? w->base.hooks.firing_interval(w->base.hooks.ctx, actor, seconds) : seconds;
}
double qa_q2w_weapons_animation_time(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_animation_rate_input in;
  in.quick_switch = c->input.quick_switch;
  in.frame_seconds = c->game->host->frame_seconds(c->game->host->ctx);
  in.phase = c->state->phase; in.frame = c->state->frame;
  in.quad_fire_until = c->input.quad_fire_until; in.haste = c->input.haste; in.now = c->now;
  double rate = qa_q2w_animation_rate(in);
  double native = trunc(1000 / rate) / 1000;
  double interval = c->state->phase == QA_Q2W_FIRING ? qa_q2w_weapons_firing_interval(w, c->self.actor.id, native) : native;
  c->state->gun_rate = interval == native ? rate : 1 / interval;
  return interval;
}
double qa_q2w_weapons_multiplier(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_damage_input in = {c->input.quad_until, c->input.double_until, c->input.no_stack_double};
  return qa_q2w_weapon_damage_multiplier(c->self.actor.id, in, c->now, &w->base.hooks);
}
void qa_q2w_weapons_powerup_sound(qa_q2w_weapons *w, qa_q2w_context *c) {
  bool ctf = w->has_source_rules && name_is(w->source_rules.kind, "ctf");
  if (!(ctf && w->source_rules.strength_sound && w->source_rules.strength_sound(w->source_rules.ctx, c))) {
    qa_q2w_powerup_sound_input in = {c->input.quad_until, c->input.double_until, c->rerelease, c->now};
    const char *sound = qa_q2w_powerup_sound(in);
    if (sound) game_sound(c->game, c->self.actor, sound, true, 3, false, 0, false, 0);
  }
  if (ctf && w->source_rules.haste_sound) w->source_rules.haste_sound(w->source_rules.ctx, c);
}
qa_q2w_projection qa_q2w_weapons_project_source(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2f_game *game, qa_q2w_input input, qa_vec3 angles, qa_vec3 offset) {
  const qa_q2w_input *stored = qa_q2w_ballistics_input(&w->base, self.actor.id);
  qa_q2w_actor_view view = {input.hand, self.view_height, stored == NULL || stored->players_collide};
  return qa_q2w_project_actor(self.actor.id, game, view, angles, offset);
}
qa_q2w_projection qa_q2w_weapons_project(qa_q2w_weapons *w, qa_q2w_context *c, qa_vec3 offset, qa_vec3 angles) {
  return qa_q2w_weapons_project_source(w, c->self, c->game, c->input, angles, offset);
}
qa_q2w_projection qa_q2w_weapons_project_default(qa_q2w_weapons *w, qa_q2w_context *c, qa_vec3 offset) {
  return qa_q2w_weapons_project(w, c, offset, c->input.angles);
}
void qa_q2w_weapons_kick(qa_q2w_weapons *w, qa_q2w_context *c, qa_vec3 origin, qa_vec3 angles, double duration) {
  (void)w;
  qa_q2w_set_recoil(c->state, c->game->options.edition, c->now, origin, angles, duration);
}
void qa_q2w_weapons_kick_default(qa_q2w_weapons *w, qa_q2w_context *c, qa_vec3 origin, qa_vec3 angles) {
  qa_q2w_weapons_kick(w, c, origin, angles, c->rerelease ? 0.2 : 0);
}
void qa_q2w_weapons_flash(qa_q2w_weapons *w, qa_q2w_context *c, double flash) {
  qa_q2w_event e; memset(&e, 0, sizeof e);
  e.kind = QA_Q2W_EVENT_MUZZLEFLASH; e.actor = &c->self.actor.id; e.flash = flash; e.silenced = c->silenced;
  w->base.hooks.emit(w->base.hooks.ctx, e);
}
void qa_q2w_weapons_set_loop(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2f_game *game, qa_q2w_state *state, const char *path) {
  (void)w;
  if (strcmp(state->loop_sound, path) == 0) return;
  qa_vec3 origin = game->body(game->ctx, self.actor).origin;
  qa_q2f_presentation_event e;
  if (strcmp(state->loop_sound, "") != 0) {
    memset(&e, 0, sizeof e);
    e.kind = QA_Q2F_EVENT_SOUND; e.actor = &self.actor.id; e.origin = origin; e.path = state->loop_sound;
    e.channel = 1; e.volume = 1; e.attenuation = 1; e.loop = "stop";
    game->host->emit(game->host->ctx, e);
  }
  state->loop_sound = path;
  if (strcmp(path, "") != 0) {
    memset(&e, 0, sizeof e);
    e.kind = QA_Q2F_EVENT_SOUND; e.actor = &self.actor.id; e.origin = origin; e.path = path;
    e.channel = 1; e.volume = 1; e.attenuation = 1; e.loop = "start";
    game->host->emit(game->host->ctx, e);
  }
}
static void present(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2f_game *game, qa_q2w_state *state) {
  const qa_q2w_definition *d = state->weapon == NULL ? NULL : qa_q2w_weapons_definition(w, state->weapon);
  qa_q2w_recoil_result recoil = qa_q2w_weapon_recoil(state, game->options.edition, now_of(game));
  qa_q2w_event e; memset(&e, 0, sizeof e);
  e.kind = QA_Q2W_EVENT_VIEW_WEAPON; e.actor = &self.actor.id; e.weapon = state->weapon;
  e.model = state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERED ? "" : (state->view_model ? state->view_model : (d ? d->view_model : ""));
  e.player_model = d ? d->player_model : 0;
  e.frame = state->frame; e.skin = state->view_skin; e.rate = state->gun_rate;
  e.kick_origin = recoil.kick_origin; e.kick_angles = recoil.kick_angles;
  w->base.hooks.emit(w->base.hooks.ctx, e);
}

static void throw_grenade(qa_q2w_weapons *w, qa_q2w_context *c, bool held);
static void fire_weapon(qa_q2w_weapons *w, qa_q2w_context *c);
static void fire_held(qa_q2w_weapons *w, qa_q2w_context *c, bool held) {
  qa_q2w_extension *e = extension_of(w, c->definition->name);
  if (!e || !e->held) throw_grenade(w, c, held);
  else e->held(e->ctx, c, w, held);
}

typedef struct hook_env { qa_q2w_weapons *w; qa_q2w_context *c; } hook_env;
static double hk_random(void *ctx) { return rnd_of(((hook_env *)ctx)->c->game); }
static double hk_ammo(void *ctx) { hook_env *e = ctx; return qa_q2w_weapons_ammo(e->w, e->c); }
static void hk_no_ammo(void *ctx) { hook_env *e = ctx; qa_q2w_weapons_no_ammo(e->w, e->c, true); }
static void hk_fire(void *ctx, bool buffered) {
  hook_env *e = ctx;
  if (!buffered) { fire_weapon(e->w, e->c); return; }
  qa_q2w_context copy = *e->c;
  copy.input.attack = true;
  fire_weapon(e->w, &copy);
}
static void hk_change(void *ctx) { hook_env *e = ctx; qa_q2w_weapons_change_weapon(e->w, e->c->self, e->c->game, e->c->state, e->c->input); }
static void hk_reverse(void *ctx) { hook_env *e = ctx; reverse_animation(e->w, e->c); }
static void hk_attack_anim(void *ctx) { hook_env *e = ctx; qa_q2w_weapons_attack_animation(e->w, e->c, 1); }
static void hk_powerup(void *ctx) { hook_env *e = ctx; qa_q2w_weapons_powerup_sound(e->w, e->c); }
static double hk_anim_time(void *ctx) { hook_env *e = ctx; return qa_q2w_weapons_animation_time(e->w, e->c); }
static void hk_prepare_drop(void *ctx) {
  hook_env *e = ctx;
  if (e->c->state->primary_handoff == QA_Q2W_HANDOFF_ACTIVE && e->c->state->pending == NULL)
    e->c->state->pending = e->c->state->weapon;
}
static qa_q2w_frame_hooks generic_hooks(hook_env *env) {
  qa_q2w_frame_hooks h;
  memset(&h, 0, sizeof h);
  h.ctx = env; h.random = hk_random; h.ammo = hk_ammo; h.no_ammo = hk_no_ammo; h.fire = hk_fire;
  h.change_weapon = hk_change; h.reverse_animation = hk_reverse; h.attack_animation = hk_attack_anim;
  h.powerup_sound = hk_powerup; h.animation_time = hk_anim_time; h.prepare_drop = hk_prepare_drop;
  return h;
}
static void generic_classic_frame(qa_q2w_weapons *w, qa_q2w_context *c) {
  if (c->state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERED) return;
  if (w->has_source_rules && name_is(w->source_rules.kind, "lmctf")) c->state->source_firing = false;
  hook_env env = {w, c};
  qa_q2w_frame_hooks h = generic_hooks(&env);
  qa_q2w_classic_frame_input in = {c->input.attack, c->state->pending != NULL || c->state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERING};
  qa_q2w_step_classic_frame(c->state, c->definition, in, &h);
}
void qa_q2w_weapons_generic_classic(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_phase phase = c->state->phase;
  generic_classic_frame(w, c);
  if (!(w->has_source_rules && name_is(w->source_rules.kind, "ctf"))) return;
  bool grapple = name_is(c->state->weapon, "grapple");
  if (grapple && c->state->phase == QA_Q2W_FIRING) return;
  if (((w->source_rules.haste && w->source_rules.haste(w->source_rules.ctx, c)) || grapple) && phase == c->state->phase)
    generic_classic_frame(w, c);
}
void qa_q2w_weapons_generic_rerelease(qa_q2w_weapons *w, qa_q2w_context *c) {
  if (c->state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERED) return;
  hook_env env = {w, c};
  qa_q2w_frame_hooks h = generic_hooks(&env);
  qa_q2w_rerelease_frame_input in;
  in.attack = c->input.attack;
  in.change_requested = c->state->pending != NULL || c->state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERING;
  in.now = c->now;
  in.frame_seconds = c->game->host->frame_seconds(c->game->host->ctx);
  in.instant_switch = c->input.instant_switch; in.holster = c->input.holster; in.weapon_thunk = c->input.weapon_thunk;
  qa_q2w_step_rerelease_frame(c->state, c->definition, in, &h);
}

void qa_q2w_weapons_change_weapon(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2f_game *game, qa_q2w_state *state, qa_q2w_input input) {
  if (state->primary_handoff == QA_Q2W_HANDOFF_ACTIVE && game->options.edition == QA_Q2F_RERELEASE
      && health_of(game, self.actor.id) > 0 && !input.instant_switch && input.holster) return;
  qa_q2w_extension *held_ext = state->weapon ? extension_of(w, state->weapon) : NULL;
  if (state->grenade_time != 0 && (name_is(state->weapon, "grenades")
        ? state->hand_reservation != QA_Q2W_RES_NONE
        : game->options.edition == QA_Q2F_RERELEASE
          || (state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERING && held_ext && held_ext->held))) {
    qa_q2w_context old;
    if (context_default(w, self, game, state, input, &old)) {
      if (!old.rerelease) state->grenade_time = now_of(game);
      fire_held(w, &old, false);
    }
  }
  cancel_hand_preparation(w, self, game, state);
  state->grenade_time = 0;
  if (state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERING && health_of(game, self.actor.id) > 0) {
    state->primary_handoff = QA_Q2W_HANDOFF_HOLSTERED; state->pending = NULL;
    state->latched_attack = false; state->fire_buffered = false;
    qa_q2w_weapons_set_loop(w, self, game, state, "");
    return;
  }
  state->primary_handoff = QA_Q2W_HANDOFF_ACTIVE;
  if (state->weapon && state->pending && !name_is(state->pending, state->weapon) && game->options.edition == QA_Q2F_RERELEASE)
    game_sound(game, self.actor, "weapons/change.wav", true, 1, false, 0, false, 0);
  state->last_weapon = state->weapon; state->weapon = state->pending; state->pending = NULL;
  state->machinegun_shots = 0; state->view_model = NULL; state->view_skin = 0;
  qa_q2w_weapons_set_loop(w, self, game, state, "");
  if (state->weapon == NULL) return;
  state->phase = QA_Q2W_ACTIVATING; state->frame = 0;
  qa_q2w_context c;
  bool has = context_default(w, self, game, state, input, &c);
  if (has) qa_q2w_weapons_animation(w, &c, "pain", input.ducked ? 169 : 62, input.ducked ? 172 : 65);
  if (game->options.edition == QA_Q2F_RERELEASE && input.instant_switch && has) {
    qa_q2w_extension *e = extension_of(w, c.definition->name);
    if (e && e->think) e->think(e->ctx, &c, w);
    else if (name_is(state->weapon, "grenades")) qa_q2w_weapons_throw_rerelease(w, &c, NULL);
    else qa_q2w_weapons_generic_rerelease(w, &c);
  }
}

void qa_q2w_weapons_resume_primary(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2f_game *game, qa_q2w_input input, qa_q2w_name name) {
  qa_q2w_state *state = require_state(w, self);
  if (state->primary_handoff == QA_Q2W_HANDOFF_ACTIVE) return;
  if (state->primary_handoff != QA_Q2W_HANDOFF_HOLSTERED) qa_q2f_error("Q2 primary must finish holstering before it resumes");
  const char *requested = name ? name : state->weapon;
  const qa_q2w_definition *definition = NULL;
  if (requested) for (size_t i = 0; i < w->definition_count; i++)
    if (strcmp(w->definitions[i]->name, requested) == 0) { definition = w->definitions[i]; break; }
  bool alive = health_of(game, self.actor.id) > 0;
  state->pending = NULL;
  if (alive) {
    if (definition && (name_is(definition->name, "blaster") || inv_count(game, self.actor.id, definition->item) > 0)
        && (definition->ammo == NULL || inv_count(game, self.actor.id, definition->ammo) >= definition->quantity))
      state->pending = definition->name;
    else {
      qa_q2w_context c;
      if (context_default(w, self, game, state, input, &c)) qa_q2w_weapons_no_ammo(w, &c, false);
      else state->pending = "blaster";
    }
  }
  qa_q2w_weapons_change_weapon(w, self, game, state, input);
}
bool qa_q2w_weapons_can_drop(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2f_game *game, qa_q2w_name name) {
  if (((uint32_t)game->options.deathmatch_flags & 4u) != 0) return false;
  qa_q2w_state *state = require_state(w, self);
  double count = inv_count(game, self.actor.id, qa_q2w_weapons_definition(w, name)->item);
  return count > 0 && !((name_is(state->weapon, name) || name_is(state->pending, name)) && count == 1);
}

static void blaster_fire(qa_q2w_weapons *w, qa_q2w_context *c, qa_vec3 offset, double damage, bool hyper, double fx) {
  qa_q2w_projection p = qa_q2w_weapons_project_default(w, c, qa_add3_double(v3(24, 8, -8), offset));
  qa_vec3 kick_angles;
  if (hyper && c->rerelease) {
    double kx = (rnd_of(c->game) * 2 - 1) * 0.7, ky = (rnd_of(c->game) * 2 - 1) * 0.7, kz = (rnd_of(c->game) * 2 - 1) * 0.7;
    kick_angles = v3(kx, ky, kz);
  } else kick_angles = v3(-1, 0, 0);
  qa_q2w_weapons_kick_default(w, c, qa_scale3_double(qa_math_angle_vectors_rule(c->input.angles, QA_ANGLE_RULE_NATIVE).forward, -2), kick_angles);
  (void)qa_q2w_ballistics_fire_blaster_default(&w->base, c->self.actor, c->game, p.start, p.direction,
                                               damage * qa_q2w_weapons_multiplier(w, c), c->rerelease && !hyper ? 1500 : 1000, fx, hyper);
  qa_q2w_weapons_flash(w, c, hyper ? 14 : 0);
  qa_q2w_ballistics_player_noise(&w->base, c->self.actor, c->game, p.start, "weapon");
}
static void hyperblaster_fire(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_state *state = c->state;
  if (c->rerelease) {
    state->frame = state->frame > 20 ? 6 : state->frame + 1;
    if (state->frame == 12) {
      if (qa_q2w_weapons_ammo(w, c) > 0 && qa_q2w_weapons_continues_attack(w, c)) state->frame = 6;
      else game_sound(c->game, c->self.actor, "weapons/hyprbd1a.wav", true, 0, false, 0, false, 0);
    }
    qa_q2w_weapons_set_loop(w, c->self, c->game, state, state->frame >= 6 && state->frame <= 11 ? "weapons/hyprbl1a.wav" : "");
    if (qa_q2w_weapons_continues_attack(w, c) && state->frame >= 6 && state->frame <= 11) {
      if (qa_q2w_weapons_ammo(w, c) < 1) { qa_q2w_weapons_no_ammo(w, c, true); return; }
      double rotation = (state->frame - 5) * 2 * 3.141592653589793 / 6;
      blaster_fire(w, c, v3(-4 * sin(rotation), 4 * cos(rotation), 0),
                   strcmp(c->game->options.mode, "deathmatch") == 0 ? 15 : 20, true, fmod(state->frame, 4) == 0 ? 64 : 0);
      qa_q2w_weapons_powerup_sound(w, c);
      qa_q2w_weapons_consume_default(w, c);
      qa_q2w_weapons_attack_animation(w, c, trunc(rnd_of(c->game) + 0.25));
    }
  } else {
    qa_q2w_weapons_set_loop(w, c->self, c->game, state, "weapons/hyprbl1a.wav");
    if (!qa_q2w_weapons_continues_attack(w, c)) state->frame++;
    else {
      if (qa_q2w_weapons_ammo(w, c) < 1) qa_q2w_weapons_no_ammo(w, c, true);
      else {
        double rotation = (state->frame - 5) * 2 * 3.141592653589793 / 6;
        blaster_fire(w, c, v3(-4 * sin(rotation), 0, 4 * cos(rotation)),
                     strcmp(c->game->options.mode, "deathmatch") == 0 ? 15 : 20, true, state->frame == 6 || state->frame == 9 ? 64 : 0);
        qa_q2w_weapons_consume_default(w, c);
        qa_q2w_weapons_attack_animation(w, c, 1);
      }
      state->frame++;
      if (state->frame == 12 && qa_q2w_weapons_ammo(w, c) > 0) state->frame = 6;
    }
    if (state->frame == 12) {
      game_sound(c->game, c->self.actor, "weapons/hyprbd1a.wav", true, 0, false, 0, false, 0);
      qa_q2w_weapons_set_loop(w, c->self, c->game, state, "");
    }
  }
}
static void *lag_begin_if(qa_q2w_weapons *w, qa_q2w_context *c, qa_q2w_projection p, bool *active) {
  *active = c->rerelease && w->base.hooks.lag_is_history;
  return *active ? w->base.hooks.lag_begin(w->base.hooks.ctx, c->self.actor.id, p.start, p.direction) : NULL;
}
static void lag_end_if(qa_q2w_weapons *w, bool active, void *token) {
  if (active && w->base.hooks.lag_end) w->base.hooks.lag_end(w->base.hooks.ctx, token);
}
static void machinegun_fire(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_state *state = c->state;
  if (!qa_q2w_weapons_continues_attack(w, c)) {
    state->machinegun_shots = 0;
    state->frame = c->rerelease ? 6 : state->frame + 1;
    return;
  }
  state->frame = state->frame == 4 ? 5 : 4;
  if (qa_q2w_weapons_ammo(w, c) < 1) { state->frame = 6; qa_q2w_weapons_no_ammo(w, c, true); return; }
  qa_q2f_game *g = c->game;
  qa_vec3 origin, angles;
  if (c->rerelease) {
    double o1 = (rnd_of(g) * 2 - 1) * 0.35, o2 = (rnd_of(g) * 2 - 1) * 0.35, o3 = (rnd_of(g) * 2 - 1) * 0.35;
    double a1 = (rnd_of(g) * 2 - 1) * 0.7, a2 = (rnd_of(g) * 2 - 1) * 0.7, a3 = (rnd_of(g) * 2 - 1) * 0.7;
    origin = v3(o1, o2, o3); angles = v3(a1, a2, a3);
  } else {
    double oy = (rnd_of(g) * 2 - 1) * 0.35, ay = (rnd_of(g) * 2 - 1) * 0.7;
    double oz = (rnd_of(g) * 2 - 1) * 0.35, az = (rnd_of(g) * 2 - 1) * 0.7;
    origin = v3((rnd_of(g) * 2 - 1) * 0.35, oy, oz);
    angles = v3(state->machinegun_shots * -1.5, ay, az);
    if (strcmp(g->options.mode, "deathmatch") != 0) state->machinegun_shots = fmin(9, state->machinegun_shots + 1);
  }
  qa_q2w_weapons_kick_default(w, c, origin, angles);
  qa_q2w_projection p = qa_q2w_weapons_project(w, c, v3(0, c->rerelease ? 0 : 8, -8),
                                                c->rerelease ? c->input.angles : qa_add3_double(c->input.angles, angles));
  bool active; void *token = lag_begin_if(w, c, p, &active);
  qa_q2w_ballistics_fire_bullet(&w->base, c->self.actor, g, p.start, p.direction,
                                8 * qa_q2w_weapons_multiplier(w, c), 2 * qa_q2w_weapons_multiplier(w, c), 300, 500, QA_Q2W_MOD_MACHINEGUN);
  lag_end_if(w, active, token);
  if (c->rerelease) qa_q2w_weapons_powerup_sound(w, c);
  qa_q2w_weapons_flash(w, c, 1);
  qa_q2w_ballistics_player_noise(&w->base, c->self.actor, g, p.start, "weapon");
  qa_q2w_weapons_consume_default(w, c);
  qa_q2w_weapons_attack_animation(w, c, trunc(rnd_of(g) + 0.25));
}
static void chaingun_fire(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_state *state = c->state;
  qa_q2f_game *g = c->game;
  if (c->rerelease && state->frame > 31) {
    state->frame = 5;
    game_sound(g, c->self.actor, "weapons/chngnu1a.wav", true, 0, true, 1, true, 2);
  } else {
    if (!c->rerelease && state->frame == 5) game_sound(g, c->self.actor, "weapons/chngnu1a.wav", true, 0, true, 1, true, 2);
    if (state->frame == 14 && !qa_q2w_weapons_continues_attack(w, c)) {
      state->frame = 32; qa_q2w_weapons_set_loop(w, c->self, g, state, ""); return;
    }
    if (state->frame == 21 && qa_q2w_weapons_continues_attack(w, c) && qa_q2w_weapons_ammo(w, c) > 0) state->frame = 15;
    else state->frame++;
  }
  if (state->frame == 22) {
    qa_q2w_weapons_set_loop(w, c->self, g, state, "");
    game_sound(g, c->self.actor, "weapons/chngnd1a.wav", true, 0, true, 1, true, 2);
  } else if (!c->rerelease) qa_q2w_weapons_set_loop(w, c->self, g, state, "weapons/chngnl1a.wav");
  if (c->rerelease && (state->frame < 5 || state->frame > 21)) return;
  if (c->rerelease) qa_q2w_weapons_set_loop(w, c->self, g, state, "weapons/chngnl1a.wav");
  qa_q2w_weapons_attack_animation(w, c, (double)((int32_t)state->frame & 1));
  double shots = fmin(qa_q2w_weapons_ammo(w, c), state->frame <= 9 ? 1 : state->frame <= 14 ? (qa_q2w_weapons_continues_attack(w, c) ? 2 : 1) : 3);
  if (shots == 0) { qa_q2w_weapons_no_ammo(w, c, true); return; }
  qa_vec3 origin, angles;
  if (c->rerelease) {
    double o1 = (rnd_of(g) * 2 - 1) * 0.35, o2 = (rnd_of(g) * 2 - 1) * 0.35, o3 = (rnd_of(g) * 2 - 1) * 0.35;
    origin = v3(o1, o2, o3);
    double factor = 0.5 + shots * 0.15;
    double a1 = (rnd_of(g) * 2 - 1) * factor, a2 = (rnd_of(g) * 2 - 1) * factor, a3 = (rnd_of(g) * 2 - 1) * factor;
    angles = v3(a1, a2, a3);
  } else {
    double ox = (rnd_of(g) * 2 - 1) * 0.35, ax = (rnd_of(g) * 2 - 1) * 0.7;
    double oy = (rnd_of(g) * 2 - 1) * 0.35, ay = (rnd_of(g) * 2 - 1) * 0.7;
    double oz = (rnd_of(g) * 2 - 1) * 0.35, az = (rnd_of(g) * 2 - 1) * 0.7;
    origin = v3(ox, oy, oz); angles = v3(ax, ay, az);
  }
  qa_q2w_weapons_kick_default(w, c, origin, angles);
  qa_q2w_projection p = qa_q2w_weapons_project_default(w, c, v3(0, 0, -8));
  bool active; void *token = lag_begin_if(w, c, p, &active);
  for (double shot = 0; shot < shots; shot++) {
    double py = (c->rerelease ? 0 : 7) + (rnd_of(g) * 2 - 1) * 4, pz = (rnd_of(g) * 2 - 1) * 4 - 8;
    p = qa_q2w_weapons_project_default(w, c, v3(0, py, pz));
    qa_q2w_ballistics_fire_bullet(&w->base, c->self.actor, g, p.start, p.direction,
                                  (strcmp(g->options.mode, "deathmatch") == 0 ? 6 : 8) * qa_q2w_weapons_multiplier(w, c),
                                  2 * qa_q2w_weapons_multiplier(w, c), 300, 500, QA_Q2W_MOD_CHAINGUN);
  }
  lag_end_if(w, active, token);
  if (c->rerelease) qa_q2w_weapons_powerup_sound(w, c);
  qa_q2w_weapons_flash(w, c, 3 + shots - 1);
  qa_q2w_ballistics_player_noise(&w->base, c->self.actor, g, p.start, "weapon");
  qa_q2w_weapons_consume(w, c, shots, true);
}
static void shotgun_fire(qa_q2w_weapons *w, qa_q2w_context *c, bool super) {
  qa_q2w_state *state = c->state;
  if (!super && state->frame == 9) { if (!c->rerelease) state->frame++; return; }
  qa_q2w_projection start = qa_q2w_weapons_project_default(w, c, v3(0, c->rerelease ? 0 : 8, -8));
  qa_q2w_weapons_kick_default(w, c, qa_scale3_double(qa_math_angle_vectors_rule(c->input.angles, QA_ANGLE_RULE_NATIVE).forward, -2), v3(-2, 0, 0));
  bool active; void *token = lag_begin_if(w, c, start, &active);
  qa_vec3 noise_origin = start.start;
  if (super) {
    const double yaws[2] = {-5, 5};
    for (int i = 0; i < 2; i++) {
      qa_vec3 angles = c->input.angles;
      angles.y += yaws[i];
      qa_q2w_projection p;
      if (c->rerelease) p = qa_q2w_weapons_project(w, c, v3(0, 0, -8), angles);
      else { p.start = start.start; p.direction = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE).forward; }
      noise_origin = p.start;
      qa_q2w_ballistics_fire_shotgun(&w->base, c->self.actor, c->game, p.start, p.direction,
                                     6 * qa_q2w_weapons_multiplier(w, c), 12 * qa_q2w_weapons_multiplier(w, c), 1000, 500, 10, QA_Q2W_MOD_SUPERSHOTGUN);
    }
  } else {
    qa_q2w_ballistics_fire_shotgun(&w->base, c->self.actor, c->game, start.start, start.direction,
                                   4 * qa_q2w_weapons_multiplier(w, c), 8 * qa_q2w_weapons_multiplier(w, c), 500, 500, 12, QA_Q2W_MOD_SHOTGUN);
  }
  lag_end_if(w, active, token);
  qa_q2w_weapons_flash(w, c, super ? 13 : 2);
  if (!c->rerelease) state->frame++;
  qa_q2w_ballistics_player_noise(&w->base, c->self.actor, c->game, noise_origin, "weapon");
  qa_q2w_weapons_consume_default(w, c);
}
static void grenade_launcher_fire(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_vec3 angles = c->input.angles;
  if (c->rerelease && angles.x < -62.5) angles.x = -62.5;
  qa_q2w_projection p = qa_q2w_weapons_project(w, c, v3(8, c->rerelease ? 0 : 8, -8), angles);
  qa_q2w_weapons_kick_default(w, c, qa_scale3_double(qa_math_angle_vectors_rule(c->input.angles, QA_ANGLE_RULE_NATIVE).forward, -2), v3(-1, 0, 0));
  if (c->rerelease) {
    double right = (-0.9999999403953552 + rnd_of(c->game) * 1.9999999403953552) * 10;
    double up = 200 + (-0.9999999403953552 + rnd_of(c->game) * 1.9999999403953552) * 10;
    qa_q2w_grenade_adjustment adj = {right, up, c->input.gravity};
    (void)qa_q2w_ballistics_fire_grenade(&w->base, c->self.actor, c->game, p.start, p.direction,
                                         120 * qa_q2w_weapons_multiplier(w, c), 600, 2.5, 160, false, false, false, &adj);
  } else {
    (void)qa_q2w_ballistics_fire_grenade(&w->base, c->self.actor, c->game, p.start, p.direction,
                                         120 * qa_q2w_weapons_multiplier(w, c), 600, 2.5, 160, false, false, false, NULL);
  }
  qa_q2w_weapons_flash(w, c, 8);
  if (!c->rerelease) c->state->frame++;
  qa_q2w_ballistics_player_noise(&w->base, c->self.actor, c->game, p.start, "weapon");
  qa_q2w_weapons_consume_default(w, c);
}
static void rocket_launcher_fire(qa_q2w_weapons *w, qa_q2w_context *c) {
  double damage = 100 + floor(rnd_of(c->game) * 20);
  qa_q2w_projection p = qa_q2w_weapons_project_default(w, c, v3(8, 8, -8));
  qa_q2w_weapons_kick_default(w, c, qa_scale3_double(qa_math_angle_vectors_rule(c->input.angles, QA_ANGLE_RULE_NATIVE).forward, -2), v3(-1, 0, 0));
  (void)qa_q2w_ballistics_fire_rocket(&w->base, c->self.actor, c->game, p.start, p.direction,
                                      damage * qa_q2w_weapons_multiplier(w, c), 650, 120, 120 * qa_q2w_weapons_multiplier(w, c));
  qa_q2w_weapons_flash(w, c, 7);
  if (!c->rerelease) c->state->frame++;
  qa_q2w_ballistics_player_noise(&w->base, c->self.actor, c->game, p.start, "weapon");
  qa_q2w_weapons_consume_default(w, c);
}
static void railgun_fire(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_projection p = qa_q2w_weapons_project_default(w, c, v3(0, 7, -8));
  bool dm = strcmp(c->game->options.mode, "deathmatch") == 0;
  qa_q2w_weapons_kick_default(w, c, qa_scale3_double(qa_math_angle_vectors_rule(c->input.angles, QA_ANGLE_RULE_NATIVE).forward, -3), v3(-3, 0, 0));
  bool active; void *token = lag_begin_if(w, c, p, &active);
  qa_q2w_ballistics_fire_rail(&w->base, c->self.actor, c->game, p.start, p.direction,
                              (dm ? 100 : c->rerelease ? 125 : 150) * qa_q2w_weapons_multiplier(w, c),
                              (dm ? 200 : c->rerelease ? 225 : 250) * qa_q2w_weapons_multiplier(w, c));
  lag_end_if(w, active, token);
  qa_q2w_weapons_flash(w, c, 6);
  if (!c->rerelease) c->state->frame++;
  qa_q2w_ballistics_player_noise(&w->base, c->self.actor, c->game, p.start, "weapon");
  qa_q2w_weapons_consume_default(w, c);
}
static void bfg_fire(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_state *state = c->state;
  if (state->frame == 9) {
    qa_q2w_weapons_flash(w, c, 12);
    if (!c->rerelease) state->frame++;
    qa_q2w_ballistics_player_noise(&w->base, c->self.actor, c->game,
                                   c->rerelease ? c->game->body(c->game->ctx, c->self.actor).origin : zv(), "weapon");
    return;
  }
  if (qa_q2w_weapons_ammo(w, c) < 50) { if (!c->rerelease) state->frame++; return; }
  qa_q2w_projection p = qa_q2w_weapons_project_default(w, c, v3(8, 8, -8));
  (void)qa_q2w_ballistics_fire_bfg(&w->base, c->self.actor, c->game, p.start, p.direction,
                                   (strcmp(c->game->options.mode, "deathmatch") == 0 ? 200 : 500) * qa_q2w_weapons_multiplier(w, c), 400, 1000);
  qa_q2w_weapons_kick(w, c, qa_scale3_double(qa_math_angle_vectors_rule(c->input.angles, QA_ANGLE_RULE_NATIVE).forward, -2),
                      v3(c->rerelease ? -20 : -40, 0, (rnd_of(c->game) * 2 - 1) * 8),
                      c->rerelease ? 0.6 - c->game->host->frame_seconds(c->game->host->ctx) : 0.5);
  if (c->rerelease) qa_q2w_weapons_flash(w, c, 19); else state->frame++;
  qa_q2w_ballistics_player_noise(&w->base, c->self.actor, c->game, p.start, "weapon");
  qa_q2w_weapons_consume_default(w, c);
}
typedef struct proj_env { qa_q2w_weapons *w; qa_q2w_context *c; } proj_env;
static qa_q2w_projection proj_throw(void *ctx, qa_vec3 angles, qa_vec3 offset) {
  proj_env *e = ctx;
  return qa_q2w_weapons_project(e->w, e->c, offset, angles);
}
static void throw_grenade(qa_q2w_weapons *w, qa_q2w_context *c, bool held) {
  qa_q2w_state *state = c->state;
  if (name_is(c->definition->name, "grenades") && state->hand_reservation == QA_Q2W_RES_NONE) return;
  proj_env env = {w, c};
  qa_q2w_hand_throw_input in;
  in.edition = c->game->options.edition; in.angles = c->input.angles;
  in.alive = health_of(c->game, c->self.actor.id) > 0;
  in.now = c->now; in.fuse_deadline = state->grenade_time;
  in.damage_multiplier = qa_q2w_weapons_multiplier(w, c); in.gravity = c->input.gravity; in.held = held;
  in.project = (qa_q2w_project_fn){&env, proj_throw};
  qa_q2w_hand_projectile_spec spec = qa_q2w_calculate_hand_throw(in);
  if (name_is(c->definition->name, "grenades")) state->hand_reservation = QA_Q2W_RES_NONE;
  state->grenade_time = c->rerelease ? 0 : c->now + qa_q2w_weapons_firing_interval(w, c->self.actor.id, 1);
  (void)qa_q2w_ballistics_fire_grenade(&w->base, c->self.actor, c->game, spec.start, spec.direction,
                                       spec.damage, spec.speed, spec.fuse, spec.radius, true, spec.held, false, NULL);
  if (!name_is(c->definition->name, "grenades")) qa_q2w_weapons_consume(w, c, 1, true);
  if (!c->rerelease && health_of(c->game, c->self.actor.id) > 0)
    qa_q2w_weapons_animation(w, c, c->input.ducked ? "attack" : "reverse", c->input.ducked ? 159 : 119, c->input.ducked ? 162 : 112);
}
static void fire_weapon(qa_q2w_weapons *w, qa_q2w_context *c) {
  qa_q2w_extension *e = extension_of(w, c->definition->name);
  if (e) { e->fire(e->ctx, c, w); return; }
  const char *name = c->definition->name;
  if (name_is(name, "blaster")) {
    blaster_fire(w, c, zv(), c->rerelease || strcmp(c->game->options.mode, "deathmatch") == 0 ? 15 : 10, false, 8);
    if (!c->rerelease) c->state->frame++;
  } else if (name_is(name, "hyperblaster")) hyperblaster_fire(w, c);
  else if (name_is(name, "machinegun")) machinegun_fire(w, c);
  else if (name_is(name, "chaingun")) chaingun_fire(w, c);
  else if (name_is(name, "shotgun")) shotgun_fire(w, c, false);
  else if (name_is(name, "supershotgun")) shotgun_fire(w, c, true);
  else if (name_is(name, "grenadelauncher")) grenade_launcher_fire(w, c);
  else if (name_is(name, "rocketlauncher")) rocket_launcher_fire(w, c);
  else if (name_is(name, "railgun")) railgun_fire(w, c);
  else if (name_is(name, "bfg")) bfg_fire(w, c);
  else if (name_is(name, "grenades")) throw_grenade(w, c, false);
  else qa_q2f_error("Q2 weapon has no source fire callback: %s", name);
}

static bool paused_includes(const qa_q2w_definition *d, double frame) {
  for (size_t i = 0; i < d->pause_count; i++) if (d->pauses[i] == frame) return true;
  return false;
}
void qa_q2w_weapons_throw_classic(qa_q2w_weapons *w, qa_q2w_context *c, const qa_q2w_throw_definition *t) {
  qa_q2w_state *state = c->state;
  const qa_q2w_definition *d = c->definition;
  double idle_first = d->fire_last + 1;
  if (state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERING) { qa_q2w_weapons_change_weapon(w, c->self, c->game, state, c->input); return; }
  if (state->pending != NULL && state->phase == QA_Q2W_READY) { qa_q2w_weapons_change_weapon(w, c->self, c->game, state, c->input); return; }
  if (state->phase == QA_Q2W_ACTIVATING) { state->phase = QA_Q2W_READY; state->frame = idle_first; return; }
  if (state->phase == QA_Q2W_READY) {
    if (state->latched_attack || c->input.attack) {
      state->latched_attack = false;
      if (!t ? reserve_hand_grenade(w, c) : qa_q2w_weapons_ammo(w, c) > 0) {
        state->frame = 1; state->phase = QA_Q2W_FIRING; state->grenade_time = 0;
      } else qa_q2w_weapons_no_ammo(w, c, true);
      return;
    }
    if (t && t->wrap_before_pause && state->frame == d->idle_last) { state->frame = idle_first; return; }
    if (paused_includes(d, state->frame) && floor(rnd_of(c->game) * 16) != 0) return;
    if (++state->frame > d->idle_last) state->frame = idle_first;
    return;
  }
  if (state->phase != QA_Q2W_FIRING) return;
  if (state->frame == (t ? t->sound_frame : 5))
    game_sound(c->game, c->self.actor, t ? t->cock_sound : "weapons/hgrena1b.wav", true, 1, false, 0, false, 0);
  if (state->frame == (t ? t->hold_frame : 11)) {
    if (state->grenade_time == 0) {
      state->grenade_time = qa_q2w_hand_fuse_deadline(c->now, QA_Q2F_CLASSIC);
      qa_q2w_weapons_set_loop(w, c->self, c->game, state, t ? t->hold_sound : "weapons/hgrenc1b.wav");
    }
    if ((t ? t->explode : true) && !state->grenade_blew_up && c->now >= state->grenade_time) {
      qa_q2w_weapons_set_loop(w, c->self, c->game, state, "");
      if (!t) throw_grenade(w, c, true); else t->fire(t->ctx, c, true);
      if (!t && (!name_is(state->weapon, d->name) || !c->game->host->actors.is_live(c->game->host->actors.ctx, c->self.actor.id))) return;
      state->grenade_blew_up = true;
    }
    if (c->input.attack) return;
    if (state->grenade_blew_up) {
      if (c->now >= state->grenade_time) { state->frame = d->fire_last; state->grenade_blew_up = false; }
      else return;
    }
  }
  if (state->frame == (t ? t->fire_frame : 12)) {
    qa_q2w_weapons_set_loop(w, c->self, c->game, state, "");
    if (!t) throw_grenade(w, c, false); else t->fire(t->ctx, c, t->release_held);
    if (!t && (!name_is(state->weapon, d->name) || !c->game->host->actors.is_live(c->game->host->actors.ctx, c->self.actor.id))) return;
  }
  if (state->frame == d->fire_last && c->now < state->grenade_time) return;
  state->frame++;
  if (state->frame == idle_first) { state->grenade_time = 0; state->phase = QA_Q2W_READY; }
}
void qa_q2w_weapons_throw_rerelease(qa_q2w_weapons *w, qa_q2w_context *c, const qa_q2w_throw_definition *t) {
  qa_q2w_state *state = c->state;
  const qa_q2w_definition *d = c->definition;
  double fire_last = d->fire_last, idle_first = fire_last + 1, idle_last = d->idle_last;
  double idle_ready = !t ? idle_last + 1 : idle_first;
  double sound_frame = t ? t->sound_frame : 5, hold_frame = t ? t->hold_frame : 11;
  const char *cock_sound = t ? t->cock_sound : "weapons/hgrena1b.wav";
  const char *hold_sound = t ? t->hold_sound : "weapons/hgrenc1b.wav";
  bool explodes = t ? t->explode : true;
  if (state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERING) {
    if (state->think_time <= c->now || c->input.instant_switch) qa_q2w_weapons_change_weapon(w, c->self, c->game, state, c->input);
    return;
  }
  if (state->pending != NULL && state->phase == QA_Q2W_READY) {
    if (state->think_time <= c->now) {
      qa_q2w_weapons_change_weapon(w, c->self, c->game, state, c->input);
      state->think_time = qa_q2w_millisecond_sum(c->now, qa_q2w_weapons_animation_time(w, c));
    }
    return;
  }
  if (state->phase == QA_Q2W_ACTIVATING) {
    if (state->think_time <= c->now) {
      state->phase = QA_Q2W_READY; state->frame = idle_ready;
      state->think_time = qa_q2w_millisecond_sum(c->now, qa_q2w_weapons_animation_time(w, c));
      state->fire_finished = qa_q2w_millisecond_sum(c->now, qa_q2w_weapons_animation_time(w, c));
    }
    return;
  }
  if (state->phase == QA_Q2W_READY) {
    if ((state->fire_buffered || state->latched_attack || c->input.attack) && state->fire_finished <= c->now) {
      state->latched_attack = false;
      if (!t ? reserve_hand_grenade(w, c) : qa_q2w_weapons_ammo(w, c) > 0) {
        state->frame = !t ? 2 : 1; state->phase = QA_Q2W_FIRING; state->grenade_time = 0;
        state->think_time = qa_q2w_millisecond_sum(c->now, qa_q2w_weapons_animation_time(w, c));
      } else qa_q2w_weapons_no_ammo(w, c, true);
    } else if (state->think_time <= c->now) {
      state->think_time = qa_q2w_millisecond_sum(c->now, qa_q2w_weapons_animation_time(w, c));
      if (state->frame >= idle_last) state->frame = idle_first;
      else if (!paused_includes(d, state->frame) || floor(rnd_of(c->game) * 16) == 0) state->frame++;
    }
    return;
  }
  if (state->phase != QA_Q2W_FIRING) return;
  state->last_firing_time = qa_q2w_millisecond_sum(c->now, 2.5);
  if (state->think_time > c->now) return;
  if (state->frame == sound_frame && strcmp(cock_sound, "") != 0)
    game_sound(c->game, c->self.actor, cock_sound, true, 1, false, 0, false, 0);
  qa_q2w_hand_tempo tempo = {QA_Q2F_RERELEASE, c->input.haste, c->input.quad_fire_until > c->now};
  double wait = qa_q2w_weapons_firing_interval(w, c->self.actor.id, qa_q2w_hand_recovery_seconds(tempo));
  if (state->frame == hold_frame) {
    if (state->grenade_time == 0 && state->grenade_finished == 0) state->grenade_time = qa_q2w_hand_fuse_deadline(c->now, QA_Q2F_RERELEASE);
    if (!state->grenade_blew_up && strcmp(hold_sound, "") != 0) qa_q2w_weapons_set_loop(w, c->self, c->game, state, hold_sound);
    if (explodes && !state->grenade_blew_up && c->now >= state->grenade_time) {
      qa_q2w_weapons_powerup_sound(w, c);
      qa_q2w_weapons_set_loop(w, c->self, c->game, state, "");
      if (!t) throw_grenade(w, c, true); else t->fire(t->ctx, c, true);
      if (!t && (!name_is(state->weapon, d->name) || !c->game->host->actors.is_live(c->game->host->actors.ctx, c->self.actor.id))) return;
      state->grenade_blew_up = true; state->grenade_finished = qa_q2w_millisecond_sum(c->now, wait);
    }
    if (c->input.attack) { state->think_time = qa_q2w_millisecond_sum(c->now, 0.001); return; }
    if (state->grenade_blew_up) {
      if (c->now >= state->grenade_finished) {
        state->frame = fire_last; state->grenade_blew_up = false;
        state->think_time = qa_q2w_millisecond_sum(c->now, qa_q2w_weapons_animation_time(w, c));
      } else return;
    } else {
      state->frame++;
      qa_q2w_weapons_powerup_sound(w, c);
      qa_q2w_weapons_set_loop(w, c->self, c->game, state, "");
      if (!t) throw_grenade(w, c, false); else t->fire(t->ctx, c, false);
      if (!t && (!name_is(state->weapon, d->name) || !c->game->host->actors.is_live(c->game->host->actors.ctx, c->self.actor.id))) return;
      state->grenade_finished = qa_q2w_millisecond_sum(c->now, wait);
      qa_q2w_weapons_animation(w, c, c->input.ducked ? "attack" : "reverse", c->input.ducked ? 159 : 119, c->input.ducked ? 162 : 112);
    }
  }
  state->think_time = qa_q2w_millisecond_sum(c->now, qa_q2w_weapons_animation_time(w, c));
  if (state->frame == fire_last && c->now < state->grenade_finished) return;
  state->frame++;
  if (state->frame == idle_first) {
    state->grenade_finished = 0; state->phase = QA_Q2W_READY; state->fire_buffered = false;
    state->fire_finished = qa_q2w_millisecond_sum(c->now, qa_q2w_weapons_animation_time(w, c));
    state->frame = idle_ready;
    if (qa_q2w_weapons_ammo(w, c) == 0) {
      qa_q2w_weapons_no_ammo(w, c, false);
      qa_q2w_weapons_change_weapon(w, c->self, c->game, state, c->input);
    }
  }
}

typedef struct run_env { qa_q2w_weapons *w; qa_q2w_owner self; qa_q2f_game *game; qa_q2w_state *state; qa_q2w_input input; bool classic_silenced; } run_env;
static void run_once(void *ctx) {
  run_env *e = ctx;
  qa_q2w_context c;
  bool silenced = e->game->options.edition == QA_Q2F_CLASSIC ? e->classic_silenced
                                                                : qa_q2w_ballistics_silencer_shots(&e->w->base, e->self.actor.id) > 0;
  if (!make_context(e->w, e->self, e->game, e->state, e->input, silenced, &c)) return;
  if (e->state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERED) return;
  qa_q2w_extension *ext = extension_of(e->w, c.definition->name);
  if (ext && ext->think) { ext->think(ext->ctx, &c, e->w); return; }
  if (name_is(e->state->weapon, "grenades")) {
    if (c.rerelease) qa_q2w_weapons_throw_rerelease(e->w, &c, NULL);
    else qa_q2w_weapons_throw_classic(e->w, &c, NULL);
    return;
  }
  if (c.rerelease) qa_q2w_weapons_generic_rerelease(e->w, &c);
  else qa_q2w_weapons_generic_classic(e->w, &c);
}
void qa_q2w_weapons_tick(qa_q2w_weapons *w, qa_q2w_owner self, qa_q2f_game *game, qa_q2w_input input) {
  qa_q2w_state *state = require_state(w, self);
  double now = now_of(game);
  (void)qa_q2w_ballistics_set_input(&w->base, self.actor.id, input);
  state->latched_attack = state->latched_attack || input.latched_attack;
  if (input.spectator) return;
  if (health_of(game, self.actor.id) < 1) {
    if (state->grenade_time != 0 && (name_is(state->weapon, "grenades")
          ? state->hand_reservation != QA_Q2W_RES_NONE : game->options.edition == QA_Q2F_RERELEASE)) {
      qa_q2w_context c;
      if (context_default(w, self, game, state, input, &c)) {
        if (!c.rerelease) state->grenade_time = now;
        fire_held(w, &c, c.rerelease);
      }
    }
    state->pending = NULL;
    qa_q2w_weapons_change_weapon(w, self, game, state, input);
    present(w, self, game, state);
    return;
  }
  if (state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERED) { present(w, self, game, state); return; }
  if (state->weapon == NULL) {
    if (state->pending != NULL) qa_q2w_weapons_change_weapon(w, self, game, state, input);
    present(w, self, game, state);
    return;
  }
  bool classic_silenced = qa_q2w_ballistics_silencer_shots(&w->base, self.actor.id) > 0;
  run_env env = {w, self, game, state, input, classic_silenced};
  run_once(&env);
  if (w->has_source_rules && name_is(w->source_rules.kind, "lmctf")) {
    qa_q2w_context c;
    if (make_context(w, self, game, state, input, classic_silenced, &c) && w->source_rules.post_native_think)
      w->source_rules.post_native_think(w->source_rules.ctx, &c, &env, run_once);
  }
  if (game->options.edition == QA_Q2F_RERELEASE && game->host->frame_seconds(game->host->ctx) > 0.033) {
    qa_q2w_context c;
    if (context_default(w, self, game, state, input, &c)) {
      double interval = qa_q2w_weapons_animation_time(w, &c);
      if (interval < game->host->frame_seconds(game->host->ctx)) {
        double remaining = round(qa_q2w_millisecond_sum(now, game->host->frame_seconds(game->host->ctx)) * 1000) - round(state->think_time * 1000);
        while (remaining > 0) {
          state->think_time = qa_q2w_millisecond_sum(state->think_time, -interval);
          state->fire_finished = qa_q2w_millisecond_sum(state->fire_finished, -interval);
          run_once(&env);
          remaining -= round(interval * 1000);
        }
      }
    }
  } else if (game->options.edition == QA_Q2F_CLASSIC && input.quad_fire_until > now) run_once(&env);
  present(w, self, game, state);
}

static bool live(qa_q2f_game *g, qa_actor_id a) { return g->host->actors.is_live(g->host->actors.ctx, a); }
static qa_q2w_noise_checkpoint_result save_noise(bool has, const qa_q2w_noise_record *r) {
  qa_q2w_noise_checkpoint_result out;
  memset(&out, 0, sizeof out);
  out.has_value = has;
  if (has) {
    (void)qa_q2f_save_actor(&r->actor, &out.value.actor);
    out.value.origin = r->origin; out.value.time = r->time; out.value.secondary = r->secondary;
  }
  return out;
}
bool qa_q2w_weapons_capture(qa_q2w_weapons *w, qa_q2f_game *game, qa_q2w_weapons_checkpoint *out) {
  qa_q2w_ballistics *b = &w->base;
  memset(out, 0, sizeof *out);
  out->format_version = 2;
  out->source_rules = w->has_source_rules ? w->source_rules.kind : "base";
  size_t n = 0;
  for (size_t i = 0; i < b->silencer_count; i++) if (live(game, b->silencer_charges[i].actor)) n++;
  qa_q2w_charge_entry *charges = n ? malloc(n * sizeof *charges) : NULL;
  if (n && !charges) return false;
  n = 0;
  for (size_t i = 0; i < b->silencer_count; i++)
    if (live(game, b->silencer_charges[i].actor)) {
      (void)qa_q2f_save_actor(&b->silencer_charges[i].actor, &charges[n].actor);
      charges[n].charges = b->silencer_charges[i].value; n++;
    }
  out->silencer_charges = charges; out->silencer_charge_count = n;
  const char **registered = malloc((w->definition_count ? w->definition_count : 1) * sizeof *registered);
  if (!registered) return false;
  for (size_t i = 0; i < w->definition_count; i++) registered[i] = w->definitions[i]->name;
  out->registered = registered; out->registered_count = w->definition_count;
  out->has_fallback_order = w->has_fallback;
  if (w->has_fallback && w->fallback_count) {
    const char **order = malloc(w->fallback_count * sizeof *order);
    if (!order) return false;
    memcpy(order, w->fallback_order, w->fallback_count * sizeof *order);
    out->fallback_order = order; out->fallback_order_count = w->fallback_count;
  }
  n = 0;
  for (size_t i = 0; i < b->state_count; i++) if (live(game, b->states[i].actor)) n++;
  qa_q2w_state_entry *states = n ? malloc(n * sizeof *states) : NULL;
  if (n && !states) return false;
  n = 0;
  for (size_t i = 0; i < b->state_count; i++)
    if (live(game, b->states[i].actor)) {
      (void)qa_q2f_save_actor(&b->states[i].actor, &states[n].actor);
      states[n].state = *b->states[i].state; n++;
    }
  out->states = states; out->state_count = n;
  n = 0;
  for (size_t i = 0; i < b->input_count; i++) if (live(game, b->inputs[i].actor)) n++;
  qa_q2w_input_entry *inputs = n ? malloc(n * sizeof *inputs) : NULL;
  if (n && !inputs) return false;
  n = 0;
  for (size_t i = 0; i < b->input_count; i++)
    if (live(game, b->inputs[i].actor)) {
      (void)qa_q2f_save_actor(&b->inputs[i].actor, &inputs[n].actor);
      inputs[n].input = b->inputs[i].input; n++;
    }
  out->inputs = inputs; out->input_count = n;
  n = 0;
  for (size_t i = 0; i < b->noise_count; i++) if (live(game, b->noises[i].actor)) n++;
  qa_q2w_noises_entry *noises = n ? malloc(n * sizeof *noises) : NULL;
  if (n && !noises) return false;
  n = 0;
  for (size_t i = 0; i < b->noise_count; i++)
    if (live(game, b->noises[i].actor)) {
      (void)qa_q2f_save_actor(&b->noises[i].actor, &noises[n].actor);
      noises[n].primary = save_noise(b->noises[i].has_primary, &b->noises[i].primary);
      noises[n].secondary = save_noise(b->noises[i].has_secondary, &b->noises[i].secondary);
      n++;
    }
  out->noises = noises; out->noise_count = n;
  out->sound_entity = save_noise(b->has_sound_entity, &b->sound_entity);
  out->sound2_entity = save_noise(b->has_sound2_entity, &b->sound2_entity);
  n = 0;
  for (size_t i = 0; i < b->blaster_cause_count; i++) if (live(game, b->blaster_causes[i].actor)) n++;
  qa_q2w_cause_entry *causes = n ? malloc(n * sizeof *causes) : NULL;
  if (n && !causes) return false;
  n = 0;
  for (size_t i = 0; i < b->blaster_cause_count; i++)
    if (live(game, b->blaster_causes[i].actor)) {
      (void)qa_q2f_save_actor(&b->blaster_causes[i].actor, &causes[n].actor);
      causes[n].means_of_death = b->blaster_causes[i].value; n++;
    }
  out->blaster_causes = causes; out->blaster_cause_count = n;
  return true;
}
void qa_q2w_weapons_checkpoint_free(qa_q2w_weapons_checkpoint *cp) {
  free((void *)cp->silencer_charges); free((void *)cp->registered); free((void *)cp->fallback_order);
  free((void *)cp->states); free((void *)cp->inputs); free((void *)cp->noises); free((void *)cp->blaster_causes);
  memset(cp, 0, sizeof *cp);
}
static bool restore_noise(qa_q2f_game *game, qa_q2w_noise_checkpoint_result in, qa_q2w_noise_record *out) {
  if (!in.has_value) return false;
  qa_q2f_owned_result resolved = game->host->actors.resolve_saved(game->host->actors.ctx, in.value.actor);
  out->actor = resolved.has_value ? resolved.value.id
                                  : game->host->actors.reference_saved(game->host->actors.ctx, in.value.actor);
  out->origin = in.value.origin; out->time = in.value.time; out->secondary = in.value.secondary;
  return true;
}
void qa_q2w_weapons_restore(qa_q2w_weapons *w, qa_q2f_game *game, const qa_q2w_weapons_checkpoint *cp) {
  qa_q2w_ballistics *b = &w->base;
  const char *kind = w->has_source_rules ? w->source_rules.kind : "base";
  if (strcmp(cp->source_rules, kind) != 0)
    qa_q2f_error("Q2 weapon checkpoint source rules differ from the selected game module");
  bool differs = cp->registered_count != w->definition_count;
  if (!differs) for (size_t i = 0; i < cp->registered_count; i++) {
    bool found = false;
    for (size_t j = 0; j < w->definition_count; j++) if (strcmp(w->definitions[j]->name, cp->registered[i]) == 0) found = true;
    if (!found) differs = true;
  }
  if (differs) qa_q2f_error("Q2 weapon checkpoint arsenal differs from the selected source modules");
  for (size_t i = 0; i < b->state_count; i++) free(b->states[i].state);
  b->state_count = 0; b->input_count = 0; b->noise_count = 0; b->blaster_cause_count = 0; b->silencer_count = 0;
  qa_q2w_ballistics_track_actors(b, game);
  free(w->fallback_order); w->fallback_order = NULL; w->fallback_count = 0; w->has_fallback = cp->has_fallback_order;
  if (cp->has_fallback_order && cp->fallback_order_count) {
    w->fallback_order = malloc(cp->fallback_order_count * sizeof *w->fallback_order);
    if (w->fallback_order) {
      memcpy(w->fallback_order, cp->fallback_order, cp->fallback_order_count * sizeof *w->fallback_order);
      w->fallback_count = cp->fallback_order_count;
    }
  }
  for (size_t i = 0; i < w->fallback_count; i++) (void)qa_q2w_weapons_definition(w, w->fallback_order[i]);
  for (size_t i = 0; i < cp->state_count; i++) {
    qa_owned_actor actor = qa_q2f_restore_actor(game, cp->states[i].actor);
    const qa_q2w_state *saved = &cp->states[i].state;
    if (saved->weapon) (void)qa_q2w_weapons_definition(w, saved->weapon);
    if (saved->pending) (void)qa_q2w_weapons_definition(w, saved->pending);
    if (saved->last_weapon) (void)qa_q2w_weapons_definition(w, saved->last_weapon);
    qa_q2w_state *state = malloc(sizeof *state);
    if (!state) return;
    qa_q2w_state_init(state, saved->weapon);
    *state = *saved;
    qa_q2w_owner self = {actor, 0};
    (void)qa_q2w_weapons_bind(w, self.actor, game, state);
  }
  for (size_t i = 0; i < cp->silencer_charge_count; i++)
    qa_q2w_ballistics_grant_silencer(b, qa_q2f_restore_actor(game, cp->silencer_charges[i].actor).id, game, cp->silencer_charges[i].charges);
  for (size_t i = 0; i < cp->input_count; i++)
    (void)qa_q2w_ballistics_set_input(b, qa_q2f_restore_actor(game, cp->inputs[i].actor).id, cp->inputs[i].input);
  for (size_t i = 0; i < cp->noise_count; i++) {
    qa_actor_id actor = qa_q2f_restore_actor(game, cp->noises[i].actor).id;
    qa_q2w_noise_record primary, secondary;
    bool has_p = restore_noise(game, cp->noises[i].primary, &primary);
    bool has_s = restore_noise(game, cp->noises[i].secondary, &secondary);
    /* Append directly to the public map fields. */
    if (b->noise_count >= b->noise_cap) {
      size_t next = b->noise_cap ? b->noise_cap * 2 : 8;
      qa_q2w_actor_noises *grown = realloc(b->noises, next * sizeof *grown);
      if (!grown) return;
      b->noises = grown; b->noise_cap = next;
    }
    qa_q2w_actor_noises *entry = &b->noises[b->noise_count++];
    memset(entry, 0, sizeof *entry);
    entry->actor = actor; entry->has_primary = has_p; entry->primary = primary;
    entry->has_secondary = has_s; entry->secondary = secondary;
  }
  b->has_sound_entity = restore_noise(game, cp->sound_entity, &b->sound_entity);
  b->has_sound2_entity = restore_noise(game, cp->sound2_entity, &b->sound2_entity);
  for (size_t i = 0; i < cp->blaster_cause_count; i++) {
    qa_actor_id actor = qa_q2f_restore_actor(game, cp->blaster_causes[i].actor).id;
    if (b->blaster_cause_count >= b->blaster_cause_cap) {
      size_t next = b->blaster_cause_cap ? b->blaster_cause_cap * 2 : 8;
      qa_q2w_actor_double *grown = realloc(b->blaster_causes, next * sizeof *grown);
      if (!grown) return;
      b->blaster_causes = grown; b->blaster_cause_cap = next;
    }
    b->blaster_causes[b->blaster_cause_count++] = (qa_q2w_actor_double){actor, cp->blaster_causes[i].means_of_death};
  }
  game->source_callbacks.register_defs(game->source_callbacks.ctx, qa_q2w_ballistics_callbacks(b));
}




