/* Port of src/content/q2/multiplayer/ctf/grapple.ts. GPL-2.0-or-later.
 * Note: TS equipment.states clear/delete have no landed equipment API;
 * only the inactive map is cleared/deleted (equipment states are
 * overwritten in place on restore). */
#include "qa/content/q2/multiplayer/ctf/grapple.h"
#include "../multiplayer_internal.h"
#include "qa/content/q2/equipment/grapple_weapon.h"
#include "qa/content/q2/foundation/weapons/presentation.h"
#include "qa/content/q2/multiplayer/ctf/native_grapple_hooks.h"

struct inactive_entry { qa_actor_id actor; qa_ctf_grapple_state state; };
struct qa_q2_ctf_grapple {
  qa_q2_ctf_context *context;
  qa_shared_grapple_control *shared;
  qa_q2_ctf_grapple_equipment *equipment;
  qa_q2_ctf_native_grapple_ctx native_ctx;
  struct inactive_entry *inactive;
  size_t inactive_count, inactive_cap;
};

static void emit_cable(void *ctx, qa_grapple_cable_event cable) {
  qa_q2_ctf_grapple *g = ctx;
  qa_q2_ctf_event event = {0};
  event.kind = "grapple-cable";
  event.actor = cable.actor;
  event.start = cable.start;
  event.end = cable.end;
  event.offset = cable.offset;
  g->context->hooks.emit(g->context->hooks.ctx, event);
}

static bool can_damage(void *ctx, qa_actor_id owner, qa_actor_id target) {
  qa_q2_ctf_grapple *g = ctx;
  qa_q2_ctf_player_state *owner_state = qa_q2_ctf_context_get(g->context, owner);
  qa_q2_ctf_player_state *target_state = qa_q2_ctf_context_get(g->context, target);
  return qa_same_actor(owner, target) || owner_state == NULL || owner_state->team == 0 ||
         target_state == NULL || target_state->team != owner_state->team;
}

static qa_ctf_grapple_settings settings_fn(void *ctx, qa_actor_id actor, qa_q2_game_services *game) {
  (void)game;
  qa_q2_ctf_grapple *g = ctx;
  qa_q2mp_weapon_input input;
  bool has = g->context->hooks.weapons->get_input(g->context->hooks.weapons->ctx, actor, &input);
  qa_ctf_grapple_settings settings = {650, 650, 10, !(has && input.has_players_collide && !input.players_collide)};
  return settings;
}

static bool shared_native(qa_shared_grapple_control *shared) {
  return shared != NULL && shared->selection.is_enabled && shared->selection.binding != NULL &&
         strcmp(shared->selection.binding, "slot") == 0 && shared->selection.mechanic != NULL &&
         strcmp(shared->selection.mechanic, "q2-ctf") == 0 && shared->native_slot(shared->ctx, "q2-ctf");
}

qa_q2_ctf_grapple *qa_q2_ctf_grapple_module_create(qa_q2_ctf_context *context, qa_shared_grapple_control *shared) {
  qa_q2_ctf_grapple *g = calloc(1, sizeof *g);
  if (g == NULL) return NULL;
  g->context = context;
  g->shared = shared;
  if (!(shared != NULL && !shared_native(shared))) {
    g->native_ctx.hooks = &context->hooks;
    g->native_ctx.emit_cable = emit_cable;
    g->native_ctx.emit_ctx = g;
    g->native_ctx.weapon_aim = true;
    g->equipment = qa_q2_ctf_grapple_create(qa_native_grapple_hooks(&g->native_ctx), can_damage, g, settings_fn, g);
    if (g->equipment == NULL) {
      free(g);
      return NULL;
    }
  }
  return g;
}

void qa_q2_ctf_grapple_module_destroy(qa_q2_ctf_grapple *g) {
  if (g == NULL) return;
  if (g->equipment != NULL) qa_q2_ctf_grapple_destroy(g->equipment);
  free(g->inactive);
  free(g);
}

qa_q2_ctf_grapple_equipment *qa_q2_ctf_grapple_module_equipment(qa_q2_ctf_grapple *g) { return g->equipment; }
bool qa_q2_ctf_grapple_module_native_enabled(qa_q2_ctf_grapple *g) { return g->equipment != NULL; }
qa_shared_grapple_control *qa_q2_ctf_grapple_module_shared(qa_q2_ctf_grapple *g) { return g->shared; }

static qa_ctf_grapple_state *inactive_ref(qa_q2_ctf_grapple *g, qa_actor_id actor, bool create) {
  for (size_t i = 0; i < g->inactive_count; i++)
    if (qa_same_actor(g->inactive[i].actor, actor)) return &g->inactive[i].state;
  if (!create) return NULL;
  if (g->inactive_count == g->inactive_cap) {
    g->inactive_cap = g->inactive_cap == 0 ? 8 : g->inactive_cap * 2;
    g->inactive = qa_q2mp_xalloc(realloc(g->inactive, g->inactive_cap * sizeof *g->inactive));
  }
  g->inactive[g->inactive_count].actor = actor;
  qa_ctf_grapple_state_init(&g->inactive[g->inactive_count].state);
  return &g->inactive[g->inactive_count++].state;
}

qa_ctf_grapple_state qa_q2_ctf_grapple_module_state(qa_q2_ctf_grapple *g, qa_actor_id actor) {
  if (g->equipment != NULL) return *qa_q2_ctf_grapple_state(g->equipment, actor);
  qa_ctf_grapple_state fresh;
  qa_ctf_grapple_state_init(&fresh);
  return fresh;
}

qa_ctf_grapple_state *qa_q2_ctf_grapple_module_state_ref(qa_q2_ctf_grapple *g, qa_actor_id actor) {
  if (g->equipment != NULL) return qa_q2_ctf_grapple_state(g->equipment, actor);
  return inactive_ref(g, actor, true);
}

bool qa_q2_ctf_grapple_module_state_has(qa_q2_ctf_grapple *g, qa_actor_id actor) {
  if (g->equipment != NULL) return qa_q2_ctf_grapple_state(g->equipment, actor) != NULL;
  return inactive_ref(g, actor, false) != NULL;
}

void qa_q2_ctf_grapple_module_state_set(qa_q2_ctf_grapple *g, qa_actor_id actor, qa_ctf_grapple_state state) {
  *qa_q2_ctf_grapple_module_state_ref(g, actor) = state;
}

void qa_q2_ctf_grapple_module_state_delete(qa_q2_ctf_grapple *g, qa_actor_id actor) {
  for (size_t i = 0; i < g->inactive_count; i++)
    if (qa_same_actor(g->inactive[i].actor, actor)) {
      memmove(&g->inactive[i], &g->inactive[i + 1], (g->inactive_count - i - 1) * sizeof *g->inactive);
      g->inactive_count--;
      return;
    }
  /* Equipment map delete is not exposed by the landed equipment. */
}

void qa_q2_ctf_grapple_module_states_clear(qa_q2_ctf_grapple *g) { g->inactive_count = 0; }

qa_q2_callback_definitions qa_q2_ctf_grapple_module_callbacks(qa_q2_ctf_grapple *g) {
  if (g->equipment != NULL) return qa_q2_ctf_grapple_callbacks(g->equipment);
  qa_q2_callback_definitions empty = {0};
  return empty;
}

void qa_q2_ctf_grapple_module_reset(qa_q2_ctf_grapple *g, qa_owned_actor player, qa_q2_game_services *game) {
  if (g->equipment != NULL) qa_q2_ctf_grapple_reset(g->equipment, player.id, game);
  else if (g->shared != NULL) g->shared->release(g->shared->ctx, player.id);
}

void qa_q2_ctf_grapple_module_command(qa_q2_ctf_grapple *g, qa_q2_entity *player, bool pressed) {
  if (g->shared != NULL && g->shared->selection.is_enabled && g->shared->selection.binding != NULL &&
      strcmp(g->shared->selection.binding, "offhand") == 0)
    g->shared->input(g->shared->ctx, player->actor.id, pressed);
}

void qa_q2_ctf_grapple_module_offhand(qa_q2_ctf_grapple *g, qa_q2_entity *player, qa_q2_game_services *game, bool pressed) {
  if (g->equipment != NULL) qa_q2_ctf_grapple_offhand(g->equipment, player->actor.id, game, pressed);
  else qa_q2_ctf_grapple_module_command(g, player, pressed);
}

void qa_q2_ctf_grapple_module_touch(qa_q2_ctf_grapple *g, qa_q2_entity *hook, qa_q2_game_services *game, qa_touch_contact contact) {
  if (g->equipment != NULL) qa_q2_ctf_grapple_touch(g->equipment, hook, game, contact);
}

bool qa_q2_ctf_grapple_module_fire_grapple(qa_q2_ctf_grapple *g, qa_q2_entity *player, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction, double damage, double speed, double effects) {
  if (g->equipment == NULL) return false;
  return qa_q2_ctf_grapple_fire(g->equipment, player->actor.id, game, start, direction, damage, speed, effects);
}

bool qa_q2_ctf_grapple_module_fire_grapple_default(qa_q2_ctf_grapple *g, qa_q2_entity *player, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction) {
  return qa_q2_ctf_grapple_module_fire_grapple(g, player, game, start, direction, 10, 650, 0);
}

void qa_q2_ctf_grapple_module_player_frame(qa_q2_ctf_grapple *g, qa_q2_entity *player, qa_q2_game_services *game, const char *binding) {
  qa_q2mp_weapons_port *weapons = g->context->hooks.weapons;
  qa_q2mp_weapon_state *weapon = weapons->get_state(weapons->ctx, player->actor.id);
  if (weapon != NULL) {
    qa_q2mp_weapon_input input;
    bool has_input = weapons->get_input(weapons->ctx, player->actor.id, &input);
    bool holster = game->options.edition == QA_Q2_EDITION_RERELEASE && has_input && input.holster;
    bool change = weapon->pending != NULL || (weapon->primary_handoff != NULL && strcmp(weapon->primary_handoff, "holstering") == 0) || holster;
    bool selected = strcmp(binding, "weapon-slot") == 0 && weapon->weapon != NULL && strcmp(weapon->weapon, "grapple") == 0;
    if (qa_ctf_grapple_weapon_should_reset(&weapon->animation, selected, change)) {
      if (game->options.edition == QA_Q2_EDITION_RERELEASE && weapon->primary_handoff != NULL &&
          strcmp(weapon->primary_handoff, "active") == 0)
        weapon->pending = weapon->weapon;
      qa_q2_ctf_grapple_module_reset(g, player->actor, game);
      return;
    }
  }
  if (g->equipment != NULL) qa_q2_ctf_grapple_player_frame(g->equipment, player->actor.id, game, false);
}

static bool grapple_use(void *ctx, qa_owned_actor actor, qa_q2_game_services *game) {
  qa_q2_ctf_grapple *g = ctx;
  qa_q2_entity *entity = qa_q2_game_entity_of(game, &actor.id);
  if (!qa_q2_ctf_grapple_module_native_enabled(g) || entity == NULL) return false;
  const char *result = g->context->hooks.weapons->request_weapon(g->context->hooks.weapons->ctx, entity, game, "grapple");
  return result != NULL && strcmp(result, "selected") == 0;
}
static bool grapple_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_owned_actor player) {
  (void)ctx; (void)entity; (void)game; (void)player;
  return false;
}

static void extension_fire(void *ctx, qa_q2mp_weapon_context *context, qa_q2mp_weapons_port *weapons) {
  (void)weapons;
  qa_q2_ctf_grapple_module_fire(ctx, context);
}

struct frame_ctx {
  qa_q2_ctf_grapple *g;
  qa_q2mp_weapon_context *context;
  qa_q2mp_weapons_port *weapons;
};
static void step_reset(void *ctx) {
  struct frame_ctx *f = ctx;
  qa_q2_ctf_grapple_module_reset(f->g, f->context->actor, f->context->game);
}
static void step_prepare_drop(void *ctx) {
  struct frame_ctx *f = ctx;
  qa_q2mp_weapon_state *state = f->context->state;
  if (f->context->game->options.edition == QA_Q2_EDITION_RERELEASE && state->primary_handoff != NULL &&
      strcmp(state->primary_handoff, "active") == 0 && state->pending == NULL)
    state->pending = state->weapon;
}
static void step_generic(void *ctx) {
  struct frame_ctx *f = ctx;
  if (f->context->game->options.edition == QA_Q2_EDITION_RERELEASE)
    f->weapons->generic_rerelease(f->weapons->ctx, f->context);
  else
    f->weapons->generic_classic(f->weapons->ctx, f->context);
}

static void extension_think(void *ctx, qa_q2mp_weapon_context *original, qa_q2mp_weapons_port *weapons) {
  qa_q2_ctf_grapple *g = ctx;
  if (g->equipment == NULL) return;
  qa_q2mp_weapon_context context = *original;
  qa_q2_edition edition = original->game->options.edition;
  context.rerelease = edition == QA_Q2_EDITION_RERELEASE;
  context.definition = edition == QA_Q2_EDITION_RERELEASE ? &QA_Q2_RERELEASE_CTF_GRAPPLE : &QA_Q2_CTF_GRAPPLE;
  qa_q2mp_weapon_state *state = context.state;
  qa_grapple_weapon_input input = {0};
  input.attack = context.input.attack;
  input.change_requested = state->pending != NULL || (state->primary_handoff != NULL && strcmp(state->primary_handoff, "holstering") == 0);
  input.holster = context.input.holster;
  input.latched_holster = false;
  struct frame_ctx frame = {g, &context, weapons};
  qa_grapple_step_hooks hooks = {0};
  hooks.ctx = &frame;
  hooks.reset = step_reset;
  hooks.prepare_drop = step_prepare_drop;
  hooks.generic = step_generic;
  qa_step_ctf_grapple_weapon(&state->animation, qa_q2_ctf_grapple_state(g->equipment, context.actor.id), input, edition == QA_Q2_EDITION_RERELEASE ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC, hooks);
}

void qa_q2_ctf_grapple_module_register(qa_q2_ctf_grapple *g) {
  qa_q2mp_weapons_port *weapons = g->context->hooks.weapons;
  qa_q2_item_module *items = g->context->hooks.items;
  if (qa_q2_ctf_grapple_module_native_enabled(g)) {
    const qa_q2f_weapon_definition *definition =
        g->shared != NULL && g->shared->selection.is_enabled && g->shared->selection.edition != NULL &&
                strcmp(g->shared->selection.edition, "rerelease") == 0
            ? &QA_Q2_RERELEASE_CTF_GRAPPLE
            : &QA_Q2_CTF_GRAPPLE;
    qa_q2mp_weapon_extension extension = {0};
    extension.definition = definition;
    extension.ctx = g;
    extension.fire = extension_fire;
    extension.think = extension_think;
    weapons->register_weapon(weapons->ctx, extension);
  }
  qa_q2_item_definition def = {0};
  def.kind = "custom";
  def.classname = "weapon_grapple";
  def.model = "";
  def.icon = "w_grapple";
  def.name = "Grapple";
  def.sound = "misc/w_pkup.wav";
  def.rotate = false;
  def.respawn = 0;
  def.capacity = 1;
  def.quantity = 0;
  def.has_coop_stay = true;
  def.coop_stay = true;
  def.droppable = false;
  def.custom_ctx = g;
  def.pickup = grapple_pickup;
  def.has_use = true;
  def.use = grapple_use;
  qa_q2_item_module_register(items, def);
}

struct kick_ctx {
  qa_q2_ctf_grapple *g;
  qa_q2mp_weapon_context *context;
};
static void kick_fn(void *ctx, qa_vec3 origin, double pitch) {
  struct kick_ctx *k = ctx;
  /* TS q2WeaponRecoil operates on Q2WeaponState; the landed mirror
   * takes qa_q2w_state, so project the recoil fields across. */
  qa_q2w_state projected = {0};
  qa_q2mp_weapon_state *state = k->context->state;
  projected.kick_origin = state->kick_origin;
  projected.kick_angles = state->kick_angles;
  projected.kick_time = state->kick_time;
  projected.kick_until = state->kick_until;
  projected.kick_duration = state->kick_duration;
  qa_vec3 angles = qa_q2w_weapon_recoil(&projected, k->context->game->options.edition == QA_Q2_EDITION_RERELEASE ? QA_Q2F_RERELEASE : QA_Q2F_CLASSIC, k->context->now).kick_angles;
  angles.x = pitch;
  k->g->context->hooks.weapons->kick(k->g->context->hooks.weapons->ctx, k->context, origin, angles);
}

void qa_q2_ctf_grapple_module_fire(qa_q2_ctf_grapple *g, qa_q2mp_weapon_context *context) {
  if (g->equipment == NULL) return;
  struct kick_ctx kick = {g, context};
  qa_grapple_weapon_presentation presentation = {0};
  presentation.ctx = &kick;
  presentation.kick = kick_fn;
  qa_fire_ctf_grapple_weapon(context->actor.id, context->game, g->equipment, &context->state->animation,
                             context->game->options.edition, presentation);
}
