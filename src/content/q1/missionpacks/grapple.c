/* Port of src/content/q1/missionpacks/grapple.ts. The registered weapon,
 * player extension and callbacks receive the grapple struct as ctx.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/grapple.h"

#include "missionpacks_internal.h"

/* TS hooks.set(`${owner.slot}:${owner.generation}`, hook). */
static void set_hook(qa_q1mp_rogue_grapple *grapple, qa_actor_id owner, qa_q1_actor *hook) {
  for (size_t i = 0; i < grapple->hook_count; i++)
    if (grapple->hooks[i].slot == owner.slot && grapple->hooks[i].generation == owner.generation) {
      grapple->hooks[i].hook = hook;
      return;
    }
  if (grapple->hook_count == grapple->hook_capacity) {
    const size_t capacity = grapple->hook_capacity != 0 ? grapple->hook_capacity * 2 : 8;
    qa_q1mp_grapple_hook *grown = realloc(grapple->hooks, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    grapple->hooks = grown;
    grapple->hook_capacity = capacity;
  }
  grapple->hooks[grapple->hook_count++] = (qa_q1mp_grapple_hook){owner.slot, owner.generation, hook};
}
/* Live hooks by owner. A checkpoint restores into a fresh provider, so the first non-empty entity table is scanned once. */
static qa_q1_actor *hook_of(qa_q1mp_rogue_grapple *grapple, qa_actor_id owner) {
  qa_q1_services *game = grapple->game;
  if (!grapple->indexed) {
    if (game->entity_count == 0) return NULL;
    for (size_t i = 0; i < game->entity_count; i++) {
      qa_q1_actor *entity = game->entities[i];
      if (qa_q1mp_streq(entity->classname, "hook") && entity->has_owner) set_hook(grapple, entity->owner, entity);
    }
    grapple->indexed = true;
  }
  for (size_t i = 0; i < grapple->hook_count; i++) {
    if (grapple->hooks[i].slot != owner.slot || grapple->hooks[i].generation != owner.generation) continue;
    qa_q1_actor *hook = grapple->hooks[i].hook;
    if (qa_q1_live(game, hook)) return hook;
    memmove(&grapple->hooks[i], &grapple->hooks[i + 1], (grapple->hook_count - i - 1) * sizeof *grapple->hooks);
    grapple->hook_count--;
    return NULL;
  }
  return NULL;
}
static void presentation(qa_q1mp_rogue_grapple *grapple, qa_q1_player_state *player, double punch) {
  qa_q1_weapon_punch(grapple->game, player, punch);
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "weapon"; event.has_player = true; event.player = player->actor.id; event.weapon = "rogue:grapple";
  event.view_model = "progs/v_grpple.mdl"; event.frame = player->weapon_frame; event.punch = punch;
  qa_q1_emit(grapple->game, event);
}
static bool fire(qa_q1mp_rogue_grapple *grapple, qa_q1_player_state *player) {
  qa_q1_services *game = grapple->game;
  player->attack_finished = qa_q1mp_fround(game->time + 0.1);
  if (hook_of(grapple, player->actor.id) != NULL) { player->weapon_frame = 2; presentation(grapple, player, 0); return true; }
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (!body.has_value) return false;
  const qa_vec3 forward = qa_q1_make_vectors(game, player->view_angles).forward;
  qa_q1_actor *hook = qa_q1_create(game, "hook", NULL, NULL);
  hook->has_owner = true; hook->owner = player->actor.id; hook->movement = "flymissile"; hook->solid = "bbox";
  qa_q1mp_set_string(&hook->model, "progs/hook.mdl"); hook->frame = 1; hook->projectile_weapon = "rogue:grapple";
  hook->touch = qa_q1_callback_registry_touch(game->named, hook, "rogue:grapple-anchor");
  set_hook(grapple, player->actor.id, hook);
  qa_q1_set_bounds(game, hook, QA_Q1_POINT);
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true; patch.origin = qa_q1_vadd(qa_q1_vadd(body.value.origin, qa_q1_vscale(forward, 16)), QA_Q1MP_V3(0, 0, 16));
  patch.has_velocity = true; patch.velocity = qa_q1_vscale(forward, 800);
  patch.has_angles = true; patch.angles = qa_q1mp_velocity_angles(forward);
  qa_q1_set_body(game, hook, &patch);
  qa_q1_link(game, hook);
  qa_q1_schedule(game, hook, 2, qa_q1_callback_registry_action(game->named, hook, "rogue:grapple-reset"));
  qa_q1_sound(game, player->actor.id, "weapons/chain1.wav", "weapon", 1, 1);
  player->weapon_frame = 1; player->weapon_animation_at = game->time; player->continuous_firing = false;
  presentation(grapple, player, -2);
  return true;
}
static void reset(qa_q1mp_rogue_grapple *grapple, qa_q1_actor *hook) {
  qa_q1_services *game = grapple->game;
  qa_q1_player_state *player = hook->has_owner ? qa_q1_player(game, hook->owner) : NULL;
  if (player != NULL) {
    player->weapon_frame = 0; player->attack_finished = qa_q1mp_fround(game->time + 0.25); player->weapon_animation_at = -1;
    if (qa_q1mp_streq(player->weapon, "rogue:grapple")) presentation(grapple, player, 0);
  }
  qa_q1_remove(game, hook);
}
static void anchor(qa_q1mp_rogue_grapple *grapple, qa_q1_actor *hook, qa_actor_id other) {
  qa_q1_services *game = grapple->game;
  qa_q1_player_state *player = hook->has_owner ? qa_q1_player(game, hook->owner) : NULL;
  if (player == NULL) { reset(grapple, hook); return; }
  if (qa_same_actor(other, player->actor.id)) return;
  if (qa_q1mp_streq(game->host.contents(game->host.ctx, qa_q1_get_body(game, hook).origin), "sky")) { reset(grapple, hook); return; }
  const qa_actor_id owner = player->actor.id;
  if (qa_q1_is_player(game, &other)) {
    const qa_combat_state_result target_state = qa_gameplay_authority_read(game->host.combat, other);
    const qa_combat_state_result owner_state = qa_gameplay_authority_read(game->host.combat, owner);
    const char *target_team = target_state.has_value ? target_state.value.team : NULL;
    const char *owner_team = owner_state.has_value ? owner_state.value.team : NULL;
    /* TS `targetTeam === ownerTeam` over string | null | undefined. */
    const bool same_team = target_state.has_value == owner_state.has_value &&
                           (target_team == NULL ? owner_team == NULL : qa_q1mp_streq(target_team, owner_team));
    if (same_team) { reset(grapple, hook); return; }
    qa_q1_sound(game, hook->actor.id, "player/axhit1.wav", "weapon", 1, 1);
    (void)qa_q1_damage(game, other, hook->actor.id, &owner, 10, "rogue:grapple", NULL, NULL, NULL);
  } else {
    qa_q1_sound(game, hook->actor.id, "player/axhit2.wav", "weapon", 1, 1);
    const qa_combat_state_result state = qa_gameplay_authority_read(game->host.combat, other);
    if (state.has_value && state.value.can_take_damage) (void)qa_q1_damage(game, other, hook->actor.id, &owner, 1, "rogue:grapple", NULL, NULL, NULL);
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_velocity = true; patch.velocity = QA_Q1_ZERO;
    qa_q1_set_body(game, hook, &patch);
    hook->angular_velocity = QA_Q1_ZERO;
  }
  hook->frame = 2;
  qa_q1_sound(game, owner, "weapons/tink1.wav", "weapon", 1, 1);
  if (!player->attack_held) { reset(grapple, hook); return; }
  hook->count = 1; hook->solid = "none"; hook->touch = (qa_q1_bound_touch){0};
  qa_q1mp_set_mission_reference(hook, "rogue:target", &other);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, owner);
  if (body.has_value) {
    qa_body_state next = body.value;
    next.ground = NULL;
    qa_shared_body_table_write(game->host.bodies, player->actor, next);
  }
  qa_q1_link(game, hook);
  qa_q1_schedule(game, hook, 0, qa_q1_callback_registry_action(game->named, hook, "rogue:grapple-track"));
}
static void track(qa_q1mp_rogue_grapple *grapple, qa_q1_actor *hook) {
  qa_q1_services *game = grapple->game;
  qa_q1_player_state *player = hook->has_owner ? qa_q1_player(game, hook->owner) : NULL;
  qa_actor_id target;
  const bool has_target = qa_q1mp_mission_reference(game, hook, "rogue:target", &target);
  if (player == NULL || qa_q1_health(game, player->actor.id) <= 0 || !has_target ||
      (qa_q1_is_player(game, &target) && qa_q1_health(game, target) <= 0)) { reset(grapple, hook); return; }
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, target);
  if (!body.has_value) { reset(grapple, hook); return; }
  const qa_actor_id owner = player->actor.id;
  if (qa_q1_is_player(game, &target)) {
    const qa_q1_player_state *held = qa_q1_player(game, target);
    if ((held != NULL ? held->teleport_until : 0) > game->time) { reset(grapple, hook); return; }
    qa_q1_set_origin(game, hook, body.value.origin);
    qa_q1_sound(game, hook->actor.id, "pendulum/hit.wav", "weapon", 1, 1);
    (void)qa_q1_damage(game, target, hook->actor.id, &owner, 1, "rogue:grapple", NULL, NULL, NULL);
    (void)game->host.random(game->host.ctx); (void)game->host.random(game->host.ctx); (void)game->host.random(game->host.ctx);
    qa_q1_effect(game, "blood", body.value.origin, &target, 20);
  }
  const qa_q1_actor *entity = qa_q1_get_entity(game, &target);
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  if ((entity != NULL && qa_q1mp_streq(entity->solid, "slidebox")) || qa_q1_is_player(game, &target)) {
    patch.has_origin = true;
    patch.origin = qa_q1_vadd(body.value.origin, qa_q1_vscale(qa_q1_vadd(body.value.bounds.min, body.value.bounds.max), 0.5));
    patch.has_velocity = true; patch.velocity = QA_Q1_ZERO;
  } else { patch.has_velocity = true; patch.velocity = body.value.velocity; }
  qa_q1_set_body(game, hook, &patch);
  qa_q1_schedule(game, hook, 0.1, qa_q1_callback_registry_action(game->named, hook, "rogue:grapple-track"));
}
static void service(qa_q1mp_rogue_grapple *grapple, qa_q1_player_state *player) {
  qa_q1_services *game = grapple->game;
  qa_q1_actor *hook = hook_of(grapple, player->actor.id);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (hook == NULL || !body.has_value) return;
  const qa_vec3 origin = qa_q1_get_body(game, hook).origin;
  const double distance = qa_q1_length(qa_q1_vsub(origin, body.value.origin));
  if (hook->count == 1) {
    if ((!player->attack_held && qa_q1mp_streq(player->weapon, "rogue:grapple")) || player->teleport_until > game->time) {
      reset(grapple, hook);
      return;
    }
    const qa_q1_basis basis = qa_q1_make_vectors(game, body.value.angles);
    const qa_vec3 direction = qa_q1_vsub(origin, qa_q1_vadd(qa_q1_vadd(body.value.origin, qa_q1_vscale(basis.up, player->jump_held ? 0 : 16)),
                                                            qa_q1_vscale(basis.forward, 16)));
    const double speed = qa_q1_length(direction);
    qa_body_state next = body.value;
    next.velocity = qa_q1_vscale(qa_q1_normalize(direction), speed <= 100 ? speed * 10 : 1000);
    next.ground = NULL;
    qa_shared_body_table_write(game->host.bodies, player->actor, next);
  }
  if (distance > 50) {
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "beam"; event.beam_style = "grapple"; event.has_actor = true; event.actor = hook->actor.id;
    event.start = origin; event.end = qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, 16));
    qa_q1_emit(game, event);
  }
}

static bool weapon_fire(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player) { (void)runtime; return fire(ctx, player); }
static void weapon_animate(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player, double seconds) {
  (void)runtime; (void)seconds; /* TS animate: (_runtime, player) */
  qa_q1mp_rogue_grapple *grapple = ctx;
  if (hook_of(grapple, player->actor.id) == NULL || player->weapon_frame != 1 ||
      grapple->game->time < player->weapon_animation_at + 0.1) return;
  player->weapon_frame = 2;
  presentation(grapple, player, 0);
}
static void extension_attach(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player) {
  (void)ctx;
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = qa_q1_weapon_item("rogue:grapple");
  entry.count = runtime->options.deathmatch != 0 && qa_q1mp_teamplay(runtime) >= 4 ? 1 : 0;
  entry.capacity = 1;
  qa_shared_inventory_table_configure(runtime->host.inventory, player->actor, entry, NULL, NULL);
}
static void extension_frame(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player, double seconds) {
  (void)runtime; (void)seconds; /* TS frame: (_runtime, player) */
  service(ctx, player);
}
static void reset_action(void *ctx, qa_q1_services *runtime, qa_q1_actor *hook) { (void)runtime; reset(ctx, hook); }
static void anchor_touch(void *ctx, qa_q1_services *runtime, qa_q1_actor *hook, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)runtime; (void)normal; (void)surface; /* TS (_runtime, hook, other) */
  anchor(ctx, hook, other);
}
static void track_action(void *ctx, qa_q1_services *runtime, qa_q1_actor *hook) { (void)runtime; track(ctx, hook); }

void qa_q1mp_rogue_grapple_init(qa_q1mp_rogue_grapple *grapple, qa_q1_services *game) {
  memset(grapple, 0, sizeof *grapple);
  grapple->game = game;
  grapple->weapon.ctx = grapple;
  grapple->weapon.id = "rogue:grapple";
  grapple->weapon.model = "progs/v_grpple.mdl";
  grapple->weapon.ammo = NULL;
  grapple->weapon.rank = 12;
  grapple->weapon.fire = weapon_fire;
  grapple->weapon.animate = weapon_animate;
  qa_q1_register_weapon(game, &grapple->weapon);
  grapple->extension.ctx = grapple;
  grapple->extension.id = "rogue:grapple";
  grapple->extension.attach = extension_attach;
  grapple->extension.frame = extension_frame;
  qa_q1_register_player_extension(game, &grapple->extension);
  qa_q1_callback_registry_register(game->named, "rogue:grapple-reset", (qa_q1_callback_handlers){.ctx = grapple, .action = reset_action});
  qa_q1_callback_registry_register(game->named, "rogue:grapple-anchor", (qa_q1_callback_handlers){.ctx = grapple, .touch = anchor_touch});
  qa_q1_callback_registry_register(game->named, "rogue:grapple-track", (qa_q1_callback_handlers){.ctx = grapple, .action = track_action});
}
void qa_q1mp_rogue_grapple_destroy(qa_q1mp_rogue_grapple *grapple) {
  free(grapple->hooks);
  grapple->hooks = NULL;
  grapple->hook_count = grapple->hook_capacity = 0;
}
