/* Port of src/content/q2/multiplayer/lmctf/grapple.ts. GPL-2.0-or-later.
 * Note: TS equipment.states clear/delete have no landed equipment API;
 * only the inactive map is cleared/deleted. */
#include "qa/content/q2/multiplayer/lmctf/grapple.h"
#include "../multiplayer_internal.h"
#include "qa/content/q2/equipment/grapple_weapon.h"
#include "qa/content/q2/foundation/weapons/presentation.h"
#include "qa/content/q2/multiplayer/ctf/native_grapple_hooks.h"

struct inactive_entry { qa_actor_id actor; qa_lmctf_grapple_state state; };
struct qa_lmctf_grapple {
  qa_lmctf_context *context;
  qa_shared_grapple_control *shared;
  qa_lmctf_grapple_equipment *equipment;
  qa_q2_ctf_hooks hooks_adapter;
  qa_q2_ctf_native_grapple_ctx native_ctx;
  struct inactive_entry *inactive;
  size_t inactive_count, inactive_cap;
};

static void emit_cable(void *ctx, qa_grapple_cable_event cable) {
  qa_lmctf_grapple *g = ctx;
  qa_lmctf_event event = {0};
  event.kind = "grapple-cable";
  event.actor = cable.actor;
  event.start = cable.start;
  event.end = cable.end;
  event.offset = cable.offset;
  g->context->hooks.emit(g->context->hooks.ctx, event);
}

static bool can_attach(void *ctx, qa_actor_id owner, qa_actor_id target, qa_q2_game_services *game) {
  qa_lmctf_grapple *g = ctx;
  qa_q2_entity *entity = qa_q2_game_entity_of(game, &target);
  const char *classname = entity == NULL ? "" : entity->classname;
  bool world = qa_same_actor(target, game->host->world_actor(game->host->ctx));
  if (!game->host->is_player(game->host->ctx, target) && strcmp(classname, "bodyque") != 0 &&
      strcmp(classname, "worldspawn") != 0 && strncmp(classname, "func", 4) != 0 &&
      strncmp(classname, "info_flag", 9) != 0 && !world)
    return false;
  qa_lmctf_player_state *owner_state = qa_lmctf_context_get(g->context, owner);
  qa_lmctf_player_state *target_state = qa_lmctf_context_get(g->context, target);
  return owner_state == NULL || owner_state->team == 0 || target_state == NULL || target_state->team != owner_state->team;
}
static bool can_damage(void *ctx, qa_actor_id target) {
  qa_lmctf_grapple *g = ctx;
  return ((int64_t)g->context->rules->ctf_flags & 64) == 0 || g->context->hooks.player(g->context->hooks.ctx, target) == NULL;
}
static bool player_hit(void *ctx, qa_actor_id target, qa_q2_game_services *game) {
  qa_lmctf_grapple *g = ctx;
  return qa_lmctf_active(g->context, game, target);
}
static void released(void *ctx, qa_actor_id actor) {
  qa_lmctf_grapple *g = ctx;
  qa_q2mp_weapon_state *weapon = g->context->hooks.weapons->get_state(g->context->hooks.weapons->ctx, actor);
  if (weapon != NULL && weapon->weapon != NULL && strcmp(weapon->weapon, "lmctf:hook") == 0)
    qa_release_lmctf_grapple_weapon(&weapon->animation);
}

static bool shared_native(qa_shared_grapple_control *shared) {
  return shared != NULL && shared->selection.is_enabled && shared->selection.binding != NULL &&
         strcmp(shared->selection.binding, "slot") == 0 && shared->selection.mechanic != NULL &&
         strcmp(shared->selection.mechanic, "q2-lmctf") == 0 && shared->native_slot(shared->ctx, "q2-lmctf");
}

static void extension_fire(void *ctx, qa_q2mp_weapon_context *context, qa_q2mp_weapons *weapons);
static void extension_think(void *ctx, qa_q2mp_weapon_context *context, qa_q2mp_weapons *weapons);

qa_lmctf_grapple *qa_lmctf_grapple_module_create(qa_lmctf_context *context, qa_shared_grapple_control *shared) {
  qa_lmctf_grapple *g = calloc(1, sizeof *g);
  if (g == NULL) return NULL;
  g->context = context;
  g->shared = shared;
  if (!(shared != NULL && !shared_native(shared))) {
    /* Adapt LmctfHooks to the Q2CtfHooks subset native hooks read. */
    g->hooks_adapter.ctx = context->hooks.ctx;
    g->hooks_adapter.items = context->hooks.items;
    g->hooks_adapter.weapons = context->hooks.weapons;
    g->hooks_adapter.player = context->hooks.player;
    g->hooks_adapter.set_skin = context->hooks.set_skin;
    g->hooks_adapter.spawn_player = context->hooks.spawn_player;
    g->hooks_adapter.observer = context->hooks.observer;
    g->hooks_adapter.teleport = context->hooks.teleport;
    g->hooks_adapter.chase = context->hooks.chase;
    g->hooks_adapter.set_grapple_prediction = context->hooks.set_grapple_prediction;
    g->hooks_adapter.gravity = context->hooks.gravity;
    g->hooks_adapter.end_level = context->hooks.end_level;
    g->hooks_adapter.kick = context->hooks.kick;
    g->hooks_adapter.set_deathmatch_flags = context->hooks.set_deathmatch_flags;
    g->hooks_adapter.chat_allowed = context->hooks.chat_allowed;
    g->native_ctx.hooks = &g->hooks_adapter;
    g->native_ctx.emit_cable = emit_cable;
    g->native_ctx.emit_ctx = g;
    g->native_ctx.weapon_aim = false;
    qa_lmctf_grapple_policy policy = {0};
    policy.ctx = g;
    policy.can_attach = can_attach;
    policy.can_damage = can_damage;
    policy.player_hit = player_hit;
    g->equipment = qa_lmctf_grapple_create(qa_native_grapple_hooks(&g->native_ctx), policy, released, g);
    if (g->equipment == NULL) {
      free(g);
      return NULL;
    }
  }
  if (g->equipment == NULL) {
    qa_q2_item_definition def = {0};
    def.kind = "custom";
    def.classname = "weapon_hook";
    def.model = "";
    def.icon = "w_blaster";
    def.name = "Grappling Hook";
    def.sound = "misc/w_pkup.wav";
    def.rotate = false;
    def.respawn = 0;
    def.capacity = 1;
    def.quantity = 0;
    def.has_coop_stay = true;
    def.coop_stay = true;
    def.droppable = false;
    def.custom_ctx = NULL;
    def.pickup = NULL;
    def.has_use = true;
    def.use = NULL;
    /* TS pickup/use are () => false; landed NULL pickup/use also fail. */
    qa_q2_item_module_register(context->hooks.items, def);
    return g;
  }
  qa_q2_item_definition def = {0};
  def.kind = "weapon";
  def.classname = "weapon_hook";
  def.ammo = NULL;
  def.model = QA_LMCTF_GRAPPLE.world_model;
  def.icon = "w_blaster";
  def.name = "Grappling Hook";
  def.sound = "misc/w_pkup.wav";
  def.rotate = false;
  def.respawn = 0;
  def.has_coop_stay = true;
  def.coop_stay = true;
  qa_q2_item_module_register(context->hooks.items, def);
  qa_q2mp_weapon_extension extension = {0};
  extension.definition = &QA_LMCTF_GRAPPLE;
  extension.ctx = g;
  extension.fire = extension_fire;
  extension.think = extension_think;
  context->hooks.weapons->register_weapon(context->hooks.weapons->ctx, extension);
  return g;
}

static void extension_fire(void *ctx, qa_q2mp_weapon_context *context, qa_q2mp_weapons *weapons) {
  (void)weapons;
  qa_lmctf_grapple *g = ctx;
  qa_lmctf_grapple_module_fire(g, context->actor, context->game);
}

struct step_ctx { qa_lmctf_grapple *g; qa_q2mp_weapon_context *context; qa_q2mp_weapons *weapons; };
static void step_abort(void *ctx) {
  struct step_ctx *s = ctx;
  qa_lmctf_grapple_module_abort(s->g, s->context->actor, s->context->game);
}
static void step_generic(void *ctx) {
  struct step_ctx *s = ctx;
  s->weapons->generic_classic(s->weapons->ctx, s->context);
}
static void extension_think(void *ctx, qa_q2mp_weapon_context *context, qa_q2mp_weapons *weapons) {
  qa_lmctf_grapple *g = ctx;
  qa_q2mp_weapon_state *state = context->state;
  qa_grapple_weapon_input input = {0};
  input.attack = context->input.attack;
  input.change_requested = state->pending != NULL || (state->primary_handoff != NULL && strcmp(state->primary_handoff, "holstering") == 0);
  input.holster = context->input.holster;
  input.latched_holster = false;
  struct step_ctx step = {g, context, weapons};
  qa_grapple_step_hooks hooks = {0};
  hooks.ctx = &step;
  hooks.abort = step_abort;
  hooks.generic = step_generic;
  qa_step_lmctf_grapple_weapon(&state->animation, qa_lmctf_grapple_module_state_ref(g, context->actor.id), input, hooks);
}

void qa_lmctf_grapple_module_destroy(qa_lmctf_grapple *g) {
  if (g == NULL) return;
  if (g->equipment != NULL) qa_lmctf_grapple_destroy(g->equipment);
  free(g->inactive);
  free(g);
}
qa_lmctf_grapple_equipment *qa_lmctf_grapple_module_equipment(qa_lmctf_grapple *g) { return g->equipment; }
bool qa_lmctf_grapple_module_native_enabled(qa_lmctf_grapple *g) { return g->equipment != NULL; }
qa_shared_grapple_control *qa_lmctf_grapple_module_shared(qa_lmctf_grapple *g) { return g->shared; }

static qa_lmctf_grapple_state *inactive_ref(qa_lmctf_grapple *g, qa_actor_id actor, bool create) {
  for (size_t i = 0; i < g->inactive_count; i++)
    if (qa_same_actor(g->inactive[i].actor, actor)) return &g->inactive[i].state;
  if (!create) return NULL;
  if (g->inactive_count == g->inactive_cap) {
    g->inactive_cap = g->inactive_cap == 0 ? 8 : g->inactive_cap * 2;
    g->inactive = qa_q2mp_xalloc(realloc(g->inactive, g->inactive_cap * sizeof *g->inactive));
  }
  g->inactive[g->inactive_count].actor = actor;
  qa_lmctf_grapple_state_init(&g->inactive[g->inactive_count].state);
  return &g->inactive[g->inactive_count++].state;
}

qa_lmctf_grapple_state qa_lmctf_grapple_module_state(qa_lmctf_grapple *g, qa_actor_id actor) {
  if (g->equipment != NULL) return *qa_lmctf_grapple_state_of(g->equipment, actor);
  qa_lmctf_grapple_state fresh;
  qa_lmctf_grapple_state_init(&fresh);
  return fresh;
}
qa_lmctf_grapple_state *qa_lmctf_grapple_module_state_ref(qa_lmctf_grapple *g, qa_actor_id actor) {
  if (g->equipment != NULL) return qa_lmctf_grapple_state_of(g->equipment, actor);
  return inactive_ref(g, actor, true);
}
bool qa_lmctf_grapple_module_state_has(qa_lmctf_grapple *g, qa_actor_id actor) {
  if (g->equipment != NULL) return qa_lmctf_grapple_state_of(g->equipment, actor) != NULL;
  return inactive_ref(g, actor, false) != NULL;
}
void qa_lmctf_grapple_module_state_set(qa_lmctf_grapple *g, qa_actor_id actor, qa_lmctf_grapple_state state) {
  *qa_lmctf_grapple_module_state_ref(g, actor) = state;
}
void qa_lmctf_grapple_module_state_delete(qa_lmctf_grapple *g, qa_actor_id actor) {
  for (size_t i = 0; i < g->inactive_count; i++)
    if (qa_same_actor(g->inactive[i].actor, actor)) {
      memmove(&g->inactive[i], &g->inactive[i + 1], (g->inactive_count - i - 1) * sizeof *g->inactive);
      g->inactive_count--;
      return;
    }
}
void qa_lmctf_grapple_module_states_clear(qa_lmctf_grapple *g) { g->inactive_count = 0; }
qa_q2_callback_definitions qa_lmctf_grapple_module_callbacks(qa_lmctf_grapple *g) {
  if (g->equipment != NULL) return qa_lmctf_grapple_callbacks(g->equipment);
  qa_q2_callback_definitions empty = {0};
  return empty;
}
double qa_lmctf_grapple_module_gravity_scale(qa_lmctf_grapple *g, qa_actor_id actor) {
  return g->equipment != NULL ? qa_lmctf_grapple_gravity_scale(g->equipment, actor) : 1;
}
void qa_lmctf_grapple_module_abort(qa_lmctf_grapple *g, qa_owned_actor player, qa_q2_game_services *game) {
  if (g->equipment != NULL) qa_lmctf_grapple_abort(g->equipment, player.id, game);
  else if (g->shared != NULL) g->shared->release(g->shared->ctx, player.id);
}

struct kick_ctx { qa_lmctf_grapple *g; qa_q2mp_weapon_state *weapon; qa_q2_game_services *game; };
static void kick_fn(void *ctx, qa_vec3 origin, double pitch) {
  struct kick_ctx *k = ctx;
  qa_q2w_state projected = {0};
  projected.kick_origin = k->weapon->kick_origin;
  projected.kick_angles = k->weapon->kick_angles;
  projected.kick_time = k->weapon->kick_time;
  projected.kick_until = k->weapon->kick_until;
  projected.kick_duration = k->weapon->kick_duration;
  qa_q2f_edition edition = k->game->options.edition == QA_Q2_EDITION_RERELEASE ? QA_Q2F_RERELEASE : QA_Q2F_CLASSIC;
  double now = qa_q2mp_now(k->game);
  qa_vec3 angles = qa_q2w_weapon_recoil(&projected, edition, now).kick_angles;
  angles.x = pitch;
  qa_q2w_set_recoil_default(&projected, edition, now, origin, angles);
  k->weapon->kick_origin = projected.kick_origin;
  k->weapon->kick_angles = projected.kick_angles;
  k->weapon->kick_time = projected.kick_time;
  k->weapon->kick_until = projected.kick_until;
  k->weapon->kick_duration = projected.kick_duration;
}

void qa_lmctf_grapple_module_fire(qa_lmctf_grapple *g, qa_owned_actor player, qa_q2_game_services *game) {
  if (g->equipment == NULL) return;
  qa_q2mp_weapon_state *weapon = g->context->hooks.weapons->get_state(g->context->hooks.weapons->ctx, player.id);
  if (weapon == NULL) {
    qa_lmctf_grapple_fire(g->equipment, player.id, game);
    return;
  }
  struct kick_ctx kick = {g, weapon, game};
  qa_grapple_weapon_presentation presentation = {0};
  presentation.ctx = &kick;
  presentation.kick = kick_fn;
  qa_fire_lmctf_grapple_weapon(player.id, game, g->equipment, &weapon->animation, presentation);
}

void qa_lmctf_grapple_module_command(qa_lmctf_grapple *g, qa_q2_entity *player, qa_q2_game_services *game, bool pressed) {
  if (g->equipment == NULL) {
    if (g->shared != NULL && g->shared->selection.is_enabled && g->shared->selection.binding != NULL &&
        strcmp(g->shared->selection.binding, "offhand") == 0)
      g->shared->input(g->shared->ctx, player->actor.id, pressed);
    return;
  }
  qa_lmctf_grapple_state *state = qa_lmctf_grapple_module_state_ref(g, player->actor.id);
  qa_q2mp_player_state *common = g->context->hooks.player(g->context->hooks.ctx, player->actor.id);
  qa_q2mp_weapon_state *weapon = g->context->hooks.weapons->get_state(g->context->hooks.weapons->ctx, player->actor.id);
  if (common == NULL || common->noclip || common->spectator) return;
  if (((int64_t)g->context->rules->ctf_flags & 16) != 0) {
    if (weapon != NULL && weapon->weapon != NULL && strcmp(weapon->weapon, "lmctf:hook") == 0) {
      state->hook_held = pressed;
      if (pressed) weapon->animation.latched_attack = true;
      else qa_lmctf_grapple_module_abort(g, player->actor, game);
      return;
    }
    if (!pressed) {
      qa_lmctf_grapple_module_abort(g, player->actor, game);
      return;
    }
    if (state->has_hook) return;
    if (qa_q2mp_inv_count(game, player->actor.id, "q2:weapon_hook") == 0) {
      qa_lmctf_print(game, &player->actor.id, "You have no hook.\n");
      return;
    }
    qa_q2mp_weapon_input input;
    bool has_input = g->context->hooks.weapons->get_input(g->context->hooks.weapons->ctx, player->actor.id, &input);
    if ((has_input ? input.quad_until : 0) > qa_q2mp_now(game)) game->sound(game->ctx, player->actor, "items/damage3.wav", 3, 1, 1);
    qa_lmctf_grapple_module_fire(g, player->actor, game);
    return;
  }
  if (pressed) g->context->hooks.weapons->request_weapon(g->context->hooks.weapons->ctx, player, game, "lmctf:hook");
}
