/* Port of src/content/q2/equipment/hand-grenades.ts. GPL-2.0-or-later.
 * Mirrored hand action logic (TS owners: weapons/hand-action.ts,
 * weapons/hand-grenade.ts, weapons/damage.ts). */
#include "qa/content/q2/equipment/hand_grenades.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

const qa_item_id QA_HAND_GRENADE_AMMO = "q2:ammo_grenades";

typedef struct state_entry { qa_actor_id id; qa_hand_grenade_equipment_state v; } state_entry;
struct qa_q2_hand_grenade_equipment {
  qa_q2_game_services *game;
  qa_q2f_ballistics ballistics;
  state_entry *states; size_t count, cap;
  int active_steps;
  qa_hand_grenade_checkpoint_entry *cp; size_t cp_count;
};

static bool reserved(qa_q2f_hand_action a) {
  return strcmp(a.kind, "preparing") == 0 || strcmp(a.kind, "cooking") == 0 || strcmp(a.kind, "releasing") == 0;
}
static bool is_safe_integer(double v) {
  return isfinite(v) && trunc(v) == v && fabs(v) <= 9007199254740991.0;
}
static void check_loadout(qa_hand_grenade_loadout cfg) {
  if (!is_safe_integer(cfg.capacity) || cfg.capacity < 0 || !is_safe_integer(cfg.initial_ammo) ||
      cfg.initial_ammo < 0 || cfg.initial_ammo > cfg.capacity)
    qa_range_error("Hand grenade allowance must be whole ammunition within capacity");
}

static state_entry *find_entry(qa_q2_hand_grenade_equipment *e, qa_actor_id a) {
  for (size_t i = 0; i < e->count; i++) if (qa_same_actor(e->states[i].id, a)) return &e->states[i];
  return NULL;
}
static void delete_entry(qa_q2_hand_grenade_equipment *e, qa_actor_id a) {
  for (size_t i = 0; i < e->count; i++)
    if (qa_same_actor(e->states[i].id, a)) { memmove(&e->states[i], &e->states[i + 1], (e->count - i - 1) * sizeof *e->states); e->count--; return; }
}
static void set_entry(qa_q2_hand_grenade_equipment *e, qa_actor_id a, qa_hand_grenade_equipment_state v) {
  state_entry *found = find_entry(e, a);
  if (found) { found->v = v; return; }
  if (e->count == e->cap) { e->cap = e->cap ? e->cap * 2 : 8; e->states = realloc(e->states, e->cap * sizeof *e->states); if (!e->states) qa_range_error("out of memory"); }
  e->states[e->count++] = (state_entry){a, v};
}

typedef struct release_ctx { qa_q2_hand_grenade_equipment *e; } release_ctx;
static release_ctx *release_ctxs;
static size_t release_ctx_count, release_ctx_cap;
static void on_release(void *c, qa_owned_actor actor) {
  delete_entry(((release_ctx *)c)->e, actor.id);
}

qa_q2_hand_grenade_equipment *qa_q2_hand_grenade_equipment_create(qa_q2_game_services *g, qa_q2f_ballistics ballistics) {
  qa_q2_hand_grenade_equipment *e = calloc(1, sizeof *e);
  if (!e) return NULL;
  e->game = g;
  e->ballistics = ballistics;
  qa_q2_source_callbacks_register(g->source_callbacks, ballistics.callbacks);
  if (release_ctx_count == release_ctx_cap) { release_ctx_cap = release_ctx_cap ? release_ctx_cap * 2 : 8; release_ctxs = realloc(release_ctxs, release_ctx_cap * sizeof *release_ctxs); if (!release_ctxs) qa_range_error("out of memory"); }
  release_ctxs[release_ctx_count] = (release_ctx){e};
  qa_session_actor_registry_on_release(g->host->actors, on_release, &release_ctxs[release_ctx_count++]);
  return e;
}
void qa_q2_hand_grenade_equipment_destroy(qa_q2_hand_grenade_equipment *e) {
  if (!e) return;
  free(e->states); free(e->cp);
  free(e);
}

static const qa_inventory_entry *entry_of(qa_q2_game_services *g, qa_actor_id actor, qa_item_id it, qa_inventory_entry *copy) {
  size_t n = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, actor, &n);
  for (size_t i = 0; i < n; i++) if (strcmp(entries[i].item, it) == 0) { *copy = entries[i]; return copy; }
  return NULL;
}

void qa_q2_hand_grenade_equipment_configure(qa_q2_hand_grenade_equipment *e, qa_actor_id actor, qa_hand_grenade_loadout cfg) {
  check_loadout(cfg);
  qa_q2_game_services *g = e->game;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(g->host->actors, actor, &owner) ||
      !qa_shared_body_table_read(g->host->bodies, actor).has_value ||
      !qa_shared_inventory_table_has(g->host->inventory, actor))
    qa_range_error("Hand grenade equipment requires an existing shared actor, body and inventory");
  state_entry *previous = find_entry(e, owner.id);
  if (previous && reserved(previous->v.action) && previous->v.config.infinite_ammo != cfg.infinite_ammo)
    qa_range_error("Cannot change hand grenade resource policy while a grenade is reserved");
  qa_inventory_entry ammo;
  if (!entry_of(g, actor, QA_HAND_GRENADE_AMMO, &ammo))
    qa_shared_inventory_table_configure(g->host->inventory, owner,
        (qa_inventory_entry){.item = QA_HAND_GRENADE_AMMO, .count = cfg.initial_ammo, .capacity = cfg.capacity}, NULL, NULL);
  qa_hand_grenade_equipment_state next = {.config = cfg, .action = previous ? previous->v.action : (qa_q2f_hand_action){.kind = "idle"}};
  set_entry(e, owner.id, next);
}

bool qa_q2_hand_grenade_equipment_state_of(qa_q2_hand_grenade_equipment *e, qa_actor_id a, qa_hand_grenade_equipment_state *out) {
  state_entry *found = find_entry(e, a);
  if (!found) return false;
  *out = found->v;
  return true;
}

/* ---- mirrored hand-action ---- */
static double hand_recovery(qa_q2_edition ed, bool haste, bool quad_fire) {
  return ed == QA_Q2_EDITION_CLASSIC ? 1 : (haste ? 0.5 : 1) * (quad_fire ? 0.5 : 1);
}
static double hand_frame(qa_q2_edition ed, bool haste, bool quad_fire) {
  return trunc(100 * hand_recovery(ed, haste, quad_fire)) / 1000;
}
static double hand_deadline(double now, double seconds, qa_q2_edition ed) {
  return ed == QA_Q2_EDITION_CLASSIC ? now + seconds : (round(now * 1000) + round(seconds * 1000)) / 1000;
}

typedef struct hand_input {
  double now;
  qa_q2_edition edition;
  bool pressed, held, released, haste, quad_fire;
  const char *lifecycle;
  bool enabled;
  qa_vec3 angles;
  double gravity;
  void *project_ctx;
  qa_q2f_projection (*project)(void *, qa_vec3, qa_vec3);
} hand_input;

typedef struct hand_host {
  void *ctx;
  double (*firing_interval)(void *, double);
  bool (*reserve)(void *);
  void (*consume)(void *);
  void (*refund)(void *);
  void (*emit)(void *, qa_q2f_hand_projectile_spec);
  void (*sound)(void *, const char *);
} hand_host;

static double firing_interval(hand_host *h, double seconds) {
  double result = h->firing_interval ? h->firing_interval(h->ctx, seconds) : seconds;
  if (!isfinite(result) || result < 0) qa_range_error("Original hand grenade cadence must produce a finite nonnegative interval");
  return result;
}
static double frame_deadline_from(hand_input *in, hand_host *h, double from) {
  return (round(from * 1000) + round(firing_interval(h, hand_frame(in->edition, in->haste, in->quad_fire)) * 1000)) / 1000;
}

static qa_q2f_hand_projectile_spec calculate_throw(hand_input *in, double fuse_deadline, bool held, bool alive) {
  bool rerelease = in->edition == QA_Q2_EDITION_RERELEASE;
  qa_vec3 angles = in->angles;
  if (rerelease) angles.x = fmax(-62.5, angles.x);
  qa_q2f_projection proj = in->project(in->project_ctx, angles, rerelease ? (qa_vec3){2, 0, -14} : (qa_vec3){8, 8, -8});
  double fuse = fuse_deadline - in->now;
  double charged = 400 + (3 - fuse) * (400.0 / 3);
  double speed = trunc(rerelease ? (alive ? fmin(800, charged) : 400) : charged);
  return (qa_q2f_hand_projectile_spec){.start = proj.start, .direction = proj.direction, .damage = 125,
      .speed = speed, .fuse = fuse, .radius = 165, .gravity = in->gravity, .held = held};
}

static qa_q2f_hand_action emit_action(double expires_at, hand_input *in, hand_host *h, bool held) {
  h->sound(h->ctx, "cook-stop");
  h->consume(h->ctx);
  bool dead_classic = strcmp(in->lifecycle, "dead") == 0 && in->edition == QA_Q2_EDITION_CLASSIC;
  double fuse_deadline = dead_classic ? in->now : expires_at;
  bool alive = strcmp(in->lifecycle, "alive") == 0 || strcmp(in->lifecycle, "removing") == 0;
  h->emit(h->ctx, calculate_throw(in, fuse_deadline, held, alive));
  return (qa_q2f_hand_action){.kind = "recovering",
    .ready_at = hand_deadline(in->now, firing_interval(h, hand_recovery(in->edition, in->haste, in->quad_fire)), in->edition),
    .require_release = held && in->held};
}
static qa_q2f_hand_action release_action(double expires_at, hand_input *in, hand_host *h, double released_at) {
  if (in->edition == QA_Q2_EDITION_RERELEASE) return emit_action(expires_at, in, h, false);
  double throw_at = frame_deadline_from(in, h, released_at);
  return in->now < throw_at ? (qa_q2f_hand_action){.kind = "releasing", .expires_at = expires_at, .throw_at = throw_at}
                             : emit_action(expires_at, in, h, false);
}
static qa_q2f_hand_action cook_action(qa_q2f_hand_action st, hand_input *in, hand_host *h) {
  if (in->now >= st.expires_at) return emit_action(st.expires_at, in, h, true);
  return (!in->released && in->held) ? st : release_action(st.expires_at, in, h, in->now);
}

static qa_q2f_hand_action step_hand_action(qa_q2f_hand_action st, hand_input *in, hand_host *h) {
  if (strcmp(in->lifecycle, "removed") == 0) {
    if (reserved(st)) h->consume(h->ctx);
    return (qa_q2f_hand_action){.kind = "disarmed"};
  }
  if (strcmp(in->lifecycle, "dead") == 0 || strcmp(in->lifecycle, "removing") == 0 || !in->enabled) {
    if (strcmp(st.kind, "preparing") == 0) h->refund(h->ctx);
    if (strcmp(st.kind, "cooking") == 0 || strcmp(st.kind, "releasing") == 0)
      emit_action(st.expires_at, in, h, strcmp(in->lifecycle, "dead") == 0 && in->edition == QA_Q2_EDITION_RERELEASE);
    return (qa_q2f_hand_action){.kind = "disarmed"};
  }
  if (strcmp(st.kind, "disarmed") == 0) return in->held ? st : (qa_q2f_hand_action){.kind = "idle"};
  if (strcmp(st.kind, "idle") == 0) {
    if (!in->pressed || !h->reserve(h->ctx)) return st;
    return (qa_q2f_hand_action){.kind = "preparing", .frame = in->edition == QA_Q2_EDITION_CLASSIC ? 1 : 2,
      .next_at = frame_deadline_from(in, h, in->now), .release_queued = in->released || !in->held};
  }
  if (strcmp(st.kind, "preparing") == 0) {
    double frame = st.frame, next_at = st.next_at;
    while (in->now >= next_at && frame < 11) {
      if (frame == 5) h->sound(h->ctx, "cock");
      frame++;
      next_at = frame_deadline_from(in, h, next_at);
    }
    if (in->now < next_at)
      return (qa_q2f_hand_action){.kind = "preparing", .frame = frame, .next_at = next_at,
        .release_queued = st.release_queued || in->released || !in->held};
    h->sound(h->ctx, "cook-start");
    qa_q2f_hand_action cooking = {.kind = "cooking", .expires_at = hand_deadline(next_at, 3.2, in->edition)};
    return st.release_queued ? release_action(cooking.expires_at, in, h, next_at) : cook_action(cooking, in, h);
  }
  if (strcmp(st.kind, "cooking") == 0) return cook_action(st, in, h);
  if (strcmp(st.kind, "releasing") == 0) return in->now < st.throw_at ? st : emit_action(st.expires_at, in, h, false);
  /* recovering */
  bool require_release = st.require_release && in->held && !in->released;
  if (in->now < st.ready_at) {
    qa_q2f_hand_action out = st;
    out.require_release = require_release;
    return out;
  }
  return require_release ? (qa_q2f_hand_action){.kind = "disarmed"} : (qa_q2f_hand_action){.kind = "idle"};
}

/* ---- equipment step ---- */
typedef struct effect { const char *kind; const char *event; qa_q2f_hand_projectile_spec spec; } effect;
typedef struct step_host {
  qa_q2_hand_grenade_equipment *e;
  qa_actor_id actor;
  qa_owned_actor owner;
  qa_hand_grenade_loadout config;
  effect *effects; size_t count, cap;
} step_host;
static void push_effect(step_host *h, effect v) {
  if (h->count == h->cap) { h->cap = h->cap ? h->cap * 2 : 8; h->effects = realloc(h->effects, h->cap * sizeof *h->effects); if (!h->effects) qa_range_error("out of memory"); }
  h->effects[h->count++] = v;
}
static double sh_firing_interval(void *c, double seconds) {
  step_host *h = c;
  return h->e->ballistics.has_firing_interval ? h->e->ballistics.firing_interval(h->e->ballistics.ctx, h->actor, seconds) : seconds;
}
static bool sh_reserve(void *c) {
  step_host *h = c;
  if (h->config.infinite_ammo) return true;
  if (!qa_shared_inventory_table_consume(h->e->game->host->inventory, h->owner, QA_HAND_GRENADE_AMMO, 1)) return false;
  push_effect(h, (effect){.kind = "ammo"});
  return true;
}
static void sh_consume(void *c) { (void)c; }
static void sh_refund(void *c) {
  step_host *h = c;
  qa_q2_game_services *g = h->e->game;
  if (h->config.infinite_ammo || !qa_session_actor_registry_is_live(g->host->actors, h->actor)) return;
  qa_inventory_entry entry;
  if (!entry_of(g, h->actor, QA_HAND_GRENADE_AMMO, &entry)) qa_range_error("Reserved hand grenade lost its canonical inventory entry");
  entry.count += 1;
  qa_shared_inventory_table_configure(g->host->inventory, h->owner, entry, NULL, NULL);
  push_effect(h, (effect){.kind = "ammo"});
}
static void sh_emit(void *c, qa_q2f_hand_projectile_spec spec) {
  push_effect(c, (effect){.kind = "emit", .spec = spec});
}
static void sh_sound(void *c, const char *event) {
  push_effect(c, (effect){.kind = "sound", .event = event});
}

static double damage_multiplier(qa_q2_hand_grenade_equipment *e, qa_actor_id actor, qa_hand_grenade_equipment_input in, double now) {
  bool quad = in.quad_until > now;
  double q = quad ? (e->ballistics.has_quad_multiplier ? e->ballistics.quad_multiplier(e->ballistics.ctx, actor) : 4) : 1;
  double d = (in.double_until > now && !(quad && in.no_stack_double)) ? 2 : 1;
  double s = e->ballistics.has_source_damage_multiplier ? e->ballistics.source_damage_multiplier(e->ballistics.ctx, actor) : 1;
  return q * d * s;
}

void qa_q2_hand_grenade_equipment_step(qa_q2_hand_grenade_equipment *e, qa_actor_id actor, qa_hand_grenade_equipment_input in) {
  state_entry *current = find_entry(e, actor);
  if (!current) return;
  qa_q2_game_services *g = e->game;
  qa_owned_actor owner;
  bool has_owner = qa_session_actor_registry_resolve_owned(g->host->actors, actor, &owner);
  if (!has_owner || strcmp(in.lifecycle, "removed") == 0) { delete_entry(e, actor); return; }
  qa_hand_grenade_equipment_state snapshot = current->v;
  double now = g->host->now(g->host->ctx);
  const char *lifecycle = in.lifecycle;
  if (strcmp(lifecycle, "alive") == 0) {
    qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, actor);
    if ((cs.has_value ? cs.value.health : 0) <= 0) lifecycle = "dead";
  }
  step_host sh = {.e = e, .actor = actor, .owner = owner, .config = snapshot.config};
  hand_host host = {&sh, sh_firing_interval, sh_reserve, sh_consume, sh_refund, sh_emit, sh_sound};
  hand_input hin = {.now = now, .edition = g->options.edition, .pressed = in.pressed, .held = in.held,
    .released = in.released, .haste = in.haste, .quad_fire = in.quad_fire_until > now,
    .lifecycle = lifecycle, .enabled = snapshot.config.enabled, .angles = in.angles,
    .gravity = in.gravity, .project_ctx = in.project_ctx, .project = in.project};
  e->active_steps++;
  qa_q2f_hand_action action = step_hand_action(snapshot.action, &hin, &host);
  state_entry *after = find_entry(e, actor);
  bool same = after && memcmp(&after->v, &snapshot, sizeof snapshot) == 0;
  if (!same || !qa_session_actor_registry_is_live(g->host->actors, actor)) {
    free(sh.effects);
    e->active_steps--;
    return;
  }
  qa_hand_grenade_equipment_state next = {.config = snapshot.config, .action = action};
  set_entry(e, actor, next);
  for (size_t i = 0; i < sh.count; i++) {
    state_entry *cur = find_entry(e, actor);
    if (!cur || memcmp(&cur->v, &next, sizeof next) != 0 || !qa_session_actor_registry_is_live(g->host->actors, actor)) break;
    effect *ef = &sh.effects[i];
    if (strcmp(ef->kind, "ammo") == 0) {
      if (e->ballistics.ammo_changed) e->ballistics.ammo_changed(e->ballistics.ctx, actor, QA_HAND_GRENADE_AMMO);
    } else if (strcmp(ef->kind, "emit") == 0) {
      double damage = ef->spec.damage * damage_multiplier(e, actor, in, now);
      state_entry *cur2 = find_entry(e, actor);
      if (!cur2 || memcmp(&cur2->v, &next, sizeof next) != 0 || !qa_session_actor_registry_is_live(g->host->actors, actor)) break;
      qa_q2f_hand_grenade_launch launch = {.spec = ef->spec, .timer = ef->spec.fuse, .players_collide = in.players_collide};
      launch.spec.damage = damage;
      e->ballistics.fire_hand_grenade(e->ballistics.ctx, actor, g, launch);
    } else {
      qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, actor);
      if (!body.has_value) { free(sh.effects); e->active_steps--; qa_range_error("Hand grenade owner lost its shared body before removal"); }
      qa_q2_presentation_event ev = {0};
      ev.kind = "sound"; ev.has_actor = true; ev.actor = actor; ev.origin = body.value.origin;
      ev.path = strcmp(ef->event, "cock") == 0 ? "weapons/hgrena1b.wav" : "weapons/hgrenc1b.wav";
      ev.channel = 1; ev.volume = 1; ev.attenuation = 1;
      ev.loop = strcmp(ef->event, "cock") == 0 ? "once" : strcmp(ef->event, "cook-start") == 0 ? "start" : "stop";
      g->host->emit(g->host->ctx, ev);
    }
  }
  free(sh.effects);
  e->active_steps--;
}

qa_hand_grenades_checkpoint qa_q2_hand_grenade_equipment_capture(qa_q2_hand_grenade_equipment *e) {
  if (e->active_steps != 0) qa_range_error("Hand grenade saves require a completed action boundary");
  free(e->cp);
  e->cp = malloc((e->count ? e->count : 1) * sizeof *e->cp);
  if (!e->cp) qa_range_error("out of memory");
  for (size_t i = 0; i < e->count; i++)
    e->cp[i] = (qa_hand_grenade_checkpoint_entry){
      {(double)e->states[i].id.slot, (double)e->states[i].id.generation}, e->states[i].v};
  e->cp_count = e->count;
  return (qa_hand_grenades_checkpoint){1, e->game->options.edition, e->cp, e->cp_count};
}

void qa_q2_hand_grenade_equipment_restore(qa_q2_hand_grenade_equipment *e, qa_hand_grenades_checkpoint cp) {
  if (e->active_steps != 0) qa_range_error("Hand grenade restore requires a completed action boundary");
  if (cp.edition != e->game->options.edition) qa_range_error("Hand grenade checkpoint source edition differs");
  qa_q2_game_services *g = e->game;
  qa_actor_id *ids = NULL;
  if (cp.actor_count) { ids = malloc(cp.actor_count * sizeof *ids); if (!ids) qa_range_error("out of memory"); }
  for (size_t i = 0; i < cp.actor_count; i++) {
    check_loadout(cp.actors[i].state.config);
    qa_owned_actor owner;
    if (!qa_session_actor_registry_resolve_saved(g->host->actors, cp.actors[i].actor, &owner) ||
        !qa_shared_body_table_read(g->host->bodies, owner.id).has_value ||
        !qa_shared_inventory_table_has(g->host->inventory, owner.id)) {
      free(ids);
      qa_range_error("Hand grenade checkpoint owner has no restored shared actor, body or inventory");
    }
    for (size_t j = 0; j < i; j++) if (qa_same_actor(ids[j], owner.id)) { free(ids); qa_range_error("Duplicate hand grenade checkpoint owner"); }
    qa_inventory_entry entry;
    if (!entry_of(g, owner.id, QA_HAND_GRENADE_AMMO, &entry)) { free(ids); qa_range_error("Hand grenade checkpoint has no canonical ammo entry"); }
    ids[i] = owner.id;
  }
  e->count = 0;
  for (size_t i = 0; i < cp.actor_count; i++) set_entry(e, ids[i], cp.actors[i].state);
  free(ids);
}
