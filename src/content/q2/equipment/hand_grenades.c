/* Port of src/content/q2/equipment/hand-grenades.ts. GPL-2.0-or-later.
 * The action runs through the foundation stepHandAction port
 * (qa_q2w_step_hand_action); damage through q2WeaponDamageMultiplier. */
#include "qa/content/q2/equipment/hand_grenades.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/weapons/damage.h"
#include "qa/foundation/support.h"

const qa_item_id QA_HAND_GRENADE_AMMO = "q2:ammo_grenades";

/* TS Map<ActorId, HandGrenadeEquipmentState>. TS compares state objects by
 * identity; every set stores a fresh serial so value-equal replacements
 * (a nested configure or restore) still read as a different object. */
typedef struct state_entry { qa_actor_id id; uint64_t serial; qa_hand_grenade_equipment_state v; } state_entry;
struct qa_q2_hand_grenade_equipment {
  qa_q2_game_services *game;
  qa_q2w_ballistics *ballistics;
  state_entry *states; size_t count, cap;
  uint64_t serial;
  int active_steps;
  int64_t release_subscription;
  qa_hand_grenade_checkpoint_entry *cp; size_t cp_count;
};

static bool reserved(qa_q2w_hand_action a) {
  return a.kind == QA_Q2W_HA_PREPARING || a.kind == QA_Q2W_HA_COOKING || a.kind == QA_Q2W_HA_RELEASING;
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
/* Returns the serial of the stored object. */
static uint64_t set_entry(qa_q2_hand_grenade_equipment *e, qa_actor_id a, qa_hand_grenade_equipment_state v) {
  uint64_t serial = ++e->serial;
  state_entry *found = find_entry(e, a);
  if (found) { found->v = v; found->serial = serial; return serial; }
  if (e->count == e->cap) {
    size_t cap = e->cap ? e->cap * 2 : 8;
    state_entry *grown = realloc(e->states, cap * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    e->states = grown; e->cap = cap;
  }
  e->states[e->count++] = (state_entry){a, serial, v};
  return serial;
}
static bool is_current(qa_q2_hand_grenade_equipment *e, qa_actor_id a, uint64_t serial) {
  state_entry *entry = find_entry(e, a);
  return entry != NULL && entry->serial == serial;
}

static void on_release(void *c, qa_owned_actor actor) { delete_entry(c, actor.id); }

qa_q2_hand_grenade_equipment *qa_q2_hand_grenade_equipment_create(qa_q2_game_services *g, qa_q2w_ballistics *ballistics) {
  qa_q2_hand_grenade_equipment *e = calloc(1, sizeof *e);
  if (!e) return NULL;
  e->game = g;
  e->ballistics = ballistics;
  qa_q2_source_callbacks_register(g->source_callbacks, qa_q2w_ballistics_callbacks(ballistics));
  e->release_subscription = qa_session_actor_registry_on_release(g->host->actors, on_release, e);
  return e;
}
qa_q2_game_services *qa_q2_hand_grenade_equipment_game(const qa_q2_hand_grenade_equipment *e) { return e->game; }
void qa_q2_hand_grenade_equipment_destroy(qa_q2_hand_grenade_equipment *e) {
  if (!e) return;
  qa_session_actor_registry_off_release(e->game->host->actors, e->release_subscription);
  free(e->states); free(e->cp);
  free(e);
}

static bool entry_of(qa_q2_game_services *g, qa_actor_id actor, qa_item_id it, qa_inventory_entry *copy) {
  size_t n = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, actor, &n);
  for (size_t i = 0; i < n; i++) if (strcmp(entries[i].item, it) == 0) { *copy = entries[i]; return true; }
  return false;
}

void qa_q2_hand_grenade_equipment_configure(qa_q2_hand_grenade_equipment *e, qa_actor_id actor, qa_hand_grenade_loadout cfg) {
  check_loadout(cfg);
  qa_q2_game_services *g = e->game;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(g->host->actors, actor, &owner) ||
      !qa_shared_body_table_read(g->host->bodies, actor).has_value ||
      !qa_shared_inventory_table_has(g->host->inventory, actor))
    qa_raise("Error", 0, 0, "Hand grenade equipment requires an existing shared actor, body and inventory");
  state_entry *previous = find_entry(e, owner.id);
  if (previous && reserved(previous->v.action) && previous->v.config.infinite_ammo != cfg.infinite_ammo)
    qa_raise("Error", 0, 0, "Cannot change hand grenade resource policy while a grenade is reserved");
  qa_inventory_entry ammo;
  if (!entry_of(g, actor, QA_HAND_GRENADE_AMMO, &ammo))
    qa_shared_inventory_table_configure(g->host->inventory, owner,
        (qa_inventory_entry){.item = QA_HAND_GRENADE_AMMO, .count = cfg.initial_ammo, .capacity = cfg.capacity}, NULL, NULL);
  /* configure() may release actors through inventory callbacks; re-read. */
  previous = find_entry(e, owner.id);
  qa_hand_grenade_equipment_state next = {.config = cfg};
  if (previous) next.action = previous->v.action;
  else next.action.kind = QA_Q2W_HA_IDLE;
  (void)set_entry(e, owner.id, next);
}

bool qa_q2_hand_grenade_equipment_state_of(qa_q2_hand_grenade_equipment *e, qa_actor_id a, qa_hand_grenade_equipment_state *out) {
  state_entry *found = find_entry(e, a);
  if (!found) return false;
  *out = found->v;
  return true;
}

/* ---- equipment step: TS HandEffect queue and HandActionHost ---- */
typedef enum effect_kind { EFFECT_SOUND, EFFECT_EMIT, EFFECT_AMMO } effect_kind;
typedef struct effect { effect_kind kind; const char *event; qa_q2w_hand_projectile_spec spec; } effect;
typedef struct step_host {
  qa_q2_hand_grenade_equipment *e;
  qa_actor_id actor;
  qa_owned_actor owner;
  qa_hand_grenade_loadout config;
  effect *effects; size_t count, cap;
} step_host;
static void push_effect(step_host *h, effect v) {
  if (h->count == h->cap) {
    size_t cap = h->cap ? h->cap * 2 : 8;
    effect *grown = realloc(h->effects, cap * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    h->effects = grown; h->cap = cap;
  }
  h->effects[h->count++] = v;
}
static double sh_firing_interval(void *c, double seconds) {
  step_host *h = c;
  const qa_q2w_hooks *hooks = &h->e->ballistics->hooks;
  return hooks->firing_interval ? hooks->firing_interval(hooks->ctx, h->actor, seconds) : seconds;
}
static bool sh_reserve(void *c) {
  step_host *h = c;
  if (h->config.infinite_ammo) return true;
  if (!qa_shared_inventory_table_consume(h->e->game->host->inventory, h->owner, QA_HAND_GRENADE_AMMO, 1)) return false;
  push_effect(h, (effect){.kind = EFFECT_AMMO});
  return true;
}
static void sh_consume(void *c) { (void)c; }
static void sh_refund(void *c) {
  step_host *h = c;
  qa_q2_game_services *g = h->e->game;
  if (h->config.infinite_ammo || !qa_session_actor_registry_is_live(g->host->actors, h->actor)) return;
  qa_inventory_entry entry;
  if (!entry_of(g, h->actor, QA_HAND_GRENADE_AMMO, &entry)) qa_raise("Error", 0, 0, "Reserved hand grenade lost its canonical inventory entry");
  entry.count += 1;
  qa_shared_inventory_table_configure(g->host->inventory, h->owner, entry, NULL, NULL);
  push_effect(h, (effect){.kind = EFFECT_AMMO});
}
static void sh_emit(void *c, qa_q2w_hand_projectile_spec spec) {
  push_effect(c, (effect){.kind = EFFECT_EMIT, .spec = spec});
}
static void sh_sound(void *c, const char *event) {
  push_effect(c, (effect){.kind = EFFECT_SOUND, .event = event});
}

/* TS finally { this.activeSteps-- }, plus the effect queue a GC would drop. */
static void step_finally(void *arg) {
  step_host *h = arg;
  free(h->effects);
  h->effects = NULL;
  h->e->active_steps--;
}

void qa_q2_hand_grenade_equipment_step(qa_q2_hand_grenade_equipment *e, qa_actor_id actor, qa_hand_grenade_equipment_input in) {
  state_entry *current = find_entry(e, actor);
  if (!current) return;
  qa_q2_game_services *g = e->game;
  qa_owned_actor owner;
  bool has_owner = qa_session_actor_registry_resolve_owned(g->host->actors, actor, &owner);
  if (!has_owner || in.lifecycle == QA_Q2W_REMOVED) { delete_entry(e, actor); return; }
  uint64_t current_serial = current->serial;
  qa_hand_grenade_equipment_state snapshot = current->v;
  double now = g->host->now(g->host->ctx);
  qa_hand_grenade_loadout config = snapshot.config;
  qa_q2w_lifecycle lifecycle = in.lifecycle;
  if (lifecycle == QA_Q2W_ALIVE) {
    qa_combat_state_result cs = qa_gameplay_authority_read(g->host->combat, actor);
    if ((cs.has_value ? cs.value.health : 0) <= 0) lifecycle = QA_Q2W_DEAD;
  }
  step_host sh = {.e = e, .actor = actor, .owner = owner, .config = config};
  qa_q2w_hand_action_host host = {&sh, sh_firing_interval, sh_reserve, sh_consume, sh_refund, sh_emit, sh_sound};
  qa_q2w_hand_action_input hin = {.edition = g->options.edition, .haste = in.haste, .quad_fire = in.quad_fire_until > now,
    .now = now, .pressed = in.pressed, .held = in.held, .released = in.released, .lifecycle = lifecycle,
    .enabled = config.enabled, .throw_angles = in.angles, .throw_damage_multiplier = 1, .throw_gravity = in.gravity,
    .throw_project = in.project};
  e->active_steps++;
  qa_cleanup finally;
  qa_cleanup_push(&finally, step_finally, &sh);
  qa_q2w_hand_action action = qa_q2w_step_hand_action(snapshot.action, hin, &host);
  if (is_current(e, actor, current_serial) && qa_session_actor_registry_is_live(g->host->actors, actor)) {
    qa_hand_grenade_equipment_state next = {.config = config, .action = action};
    uint64_t next_serial = set_entry(e, actor, next);
    /* Immediate explosions can synchronously kill or remove their owner. Publish the
     * consumed action first, and never overwrite a nested lifecycle update afterward. */
    for (size_t i = 0; i < sh.count; i++) {
      if (!is_current(e, actor, next_serial) || !qa_session_actor_registry_is_live(g->host->actors, actor)) break;
      effect ef = sh.effects[i];
      if (ef.kind == EFFECT_AMMO) {
        e->ballistics->hooks.ammo_changed(e->ballistics->hooks.ctx, actor, QA_HAND_GRENADE_AMMO);
      } else if (ef.kind == EFFECT_EMIT) {
        qa_q2w_damage_input damage_input = {in.quad_until, in.double_until, in.no_stack_double};
        double damage = ef.spec.damage * qa_q2w_weapon_damage_multiplier(actor, damage_input, now, &e->ballistics->hooks);
        if (!is_current(e, actor, next_serial) || !qa_session_actor_registry_is_live(g->host->actors, actor)) break;
        qa_q2w_hand_grenade_launch launch = {.start = ef.spec.start, .direction = ef.spec.direction, .damage = damage,
          .speed = ef.spec.speed, .timer = ef.spec.fuse, .radius = ef.spec.radius, .held = ef.spec.held,
          .gravity = ef.spec.gravity, .players_collide = in.players_collide};
        (void)qa_q2w_ballistics_fire_hand_grenade(e->ballistics, actor, g, launch);
      } else {
        qa_body_state_result body = qa_shared_body_table_read(g->host->bodies, actor);
        if (!body.has_value) qa_raise("Error", 0, 0, "Hand grenade owner lost its shared body before removal");
        qa_q2_presentation_event ev = {0};
        ev.kind = "sound"; ev.has_actor = true; ev.actor = actor; ev.origin = body.value.origin;
        ev.path = strcmp(ef.event, "cock") == 0 ? "weapons/hgrena1b.wav" : "weapons/hgrenc1b.wav";
        ev.channel = 1; ev.volume = 1; ev.attenuation = 1; ev.reliable = false;
        ev.loop = strcmp(ef.event, "cock") == 0 ? "once" : strcmp(ef.event, "cook-start") == 0 ? "start" : "stop";
        g->host->emit(g->host->ctx, ev);
      }
    }
  }
  qa_cleanup_pop(&finally);
  step_finally(&sh);
}

qa_hand_grenades_checkpoint qa_q2_hand_grenade_equipment_capture(qa_q2_hand_grenade_equipment *e) {
  if (e->active_steps != 0) qa_raise("Error", 0, 0, "Hand grenade saves require a completed action boundary");
  qa_hand_grenade_checkpoint_entry *cp = malloc((e->count ? e->count : 1) * sizeof *cp);
  if (!cp) qa_range_error("out of memory");
  for (size_t i = 0; i < e->count; i++)
    cp[i] = (qa_hand_grenade_checkpoint_entry){
      {(double)e->states[i].id.slot, (double)e->states[i].id.generation}, e->states[i].v};
  free(e->cp);
  e->cp = cp;
  e->cp_count = e->count;
  return (qa_hand_grenades_checkpoint){1, e->game->options.edition, e->cp, e->cp_count};
}

typedef struct restore_scratch { qa_actor_id *ids; } restore_scratch;
static void restore_free(void *arg) { free(((restore_scratch *)arg)->ids); }

void qa_q2_hand_grenade_equipment_restore(qa_q2_hand_grenade_equipment *e, qa_hand_grenades_checkpoint cp) {
  if (e->active_steps != 0) qa_raise("Error", 0, 0, "Hand grenade restore requires a completed action boundary");
  if (cp.edition != e->game->options.edition) qa_raise("Error", 0, 0, "Hand grenade checkpoint source edition differs");
  qa_q2_game_services *g = e->game;
  restore_scratch scratch = {malloc((cp.actor_count ? cp.actor_count : 1) * sizeof *scratch.ids)};
  if (!scratch.ids) qa_range_error("out of memory");
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, restore_free, &scratch);
  for (size_t i = 0; i < cp.actor_count; i++) {
    check_loadout(cp.actors[i].state.config);
    qa_owned_actor owner;
    if (!qa_session_actor_registry_resolve_saved(g->host->actors, cp.actors[i].actor, &owner) ||
        !qa_shared_body_table_read(g->host->bodies, owner.id).has_value ||
        !qa_shared_inventory_table_has(g->host->inventory, owner.id))
      qa_raise("Error", 0, 0, "Hand grenade checkpoint owner has no restored shared actor, body or inventory");
    for (size_t j = 0; j < i; j++)
      if (qa_same_actor(scratch.ids[j], owner.id)) qa_raise("Error", 0, 0, "Duplicate hand grenade checkpoint owner");
    qa_inventory_entry entry;
    if (!entry_of(g, owner.id, QA_HAND_GRENADE_AMMO, &entry)) qa_raise("Error", 0, 0, "Hand grenade checkpoint has no canonical ammo entry");
    scratch.ids[i] = owner.id;
  }
  e->count = 0;
  for (size_t i = 0; i < cp.actor_count; i++) (void)set_entry(e, scratch.ids[i], cp.actors[i].state);
  qa_cleanup_pop(&cleanup);
  restore_free(&scratch);
}
