/* Port of src/content/q2/multiplayer/lmctf/weapons.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/lmctf/weapons.h"
#include "../multiplayer_internal.h"
#include "qa/content/q2/foundation/weapons/presentation.h"
#include "qa/foundation/js.h"

static const double PLASMA_PAUSES[2] = {16, 46};
static const double PLASMA_FIRES[2] = {4, 5};
const qa_q2w_definition QA_LMCTF_PLASMA = {
    .name = "lmctf:plasma", .item = "q2:weapon_plasma", .classname = "weapon_plasma", .ammo = "q2:ammo_cells",
    .quantity = 10, .warning = 10, .view_model = "models/weapons/v_plasma/tris.md2",
    .world_model = "models/weapons/g_plasma/tris.md2", .player_model = 12, .activate_last = 3, .fire_last = 11,
    .idle_last = 46, .deactivate_last = 51, .pauses = PLASMA_PAUSES, .pause_count = 2, .fires = PLASMA_FIRES,
    .fire_count = 2, .repeating = false};

struct qa_lmctf_weapons {
  qa_lmctf_context *context;
  qa_q2_named_think thinks[1];
  qa_q2_named_touch touches[2];
  qa_q2_callback_definitions callbacks;
};

static void mode_print(qa_lmctf_weapons *w, qa_owned_actor self, qa_q2_game_services *g) {
  qa_lmctf_print(g, &self.id, "%s", qa_lmctf_player(w->context, self.id)->plasma_mode ? "bounce plasma\n" : "spread plasma\n");
}

static void weapon_think(void *ctx, qa_q2w_context *current, qa_q2w_weapons *weapons) {
  qa_lmctf_weapons *w = ctx;
  w->context->plasma_quad = current->input.quad_until > current->now;
  qa_q2w_state *state = current->state;
  bool activating = state->phase == QA_Q2W_ACTIVATING && state->frame == 3;
  if (state->phase == QA_Q2W_READY && state->frame == 35 && !current->input.attack &&
      !state->latched_attack && state->pending == NULL)
    current->game->sound(current->game->ctx, current->self.actor, "weapons/plasma/vent.wav", 1, 1, 1);
  qa_q2w_weapons_generic_classic(weapons, current);
  if (activating && state->phase == QA_Q2W_READY) mode_print(w, current->self.actor, current->game);
}

static void weapon_fire(void *ctx, qa_q2w_context *current, qa_q2w_weapons *weapons) {
  qa_lmctf_weapons *w = ctx;
  qa_owned_actor self = current->self.actor;
  qa_q2_game_services *g = current->game;
  qa_q2w_state *state = current->state;
  double now = current->now;
  if (qa_q2w_weapons_ammo(weapons, current) < 1) {
    state->frame++;
    if (now >= state->empty_sound_time) {
      g->sound(g->ctx, self, "weapons/plasma/empty.wav", 2, 1, 1);
      state->empty_sound_time = now + 1;
    }
    qa_q2w_weapons_no_ammo(weapons, current, false);
    return;
  }
  if (state->frame == 4) {
    qa_q2w_projection projection = qa_q2w_weapons_project_default(weapons, current, (qa_vec3){8, 8, -8});
    qa_vec3 start = projection.start, direction = projection.direction;
    bool reflect = qa_lmctf_player(w->context, self.id)->plasma_mode;
    qa_vec3 kick_origin = qa_q2_scale(qa_math_angle_vectors_rule(current->input.angles, QA_ANGLE_RULE_NATIVE).forward, -2);
    qa_vec3 recoil = qa_q2w_weapon_recoil(state, g->options.edition, now).kick_angles;
    qa_q2w_weapons_kick_default(weapons, current, kick_origin, recoil);
    g->sound(g->ctx, self, reflect ? "weapons/plasma/fire1.wav" : "weapons/plasma/fire2.wav", 1, 1, 1);
    qa_lmctf_weapons_launch(w, self, g, start, direction, reflect);
    size_t en = 0;
    const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, self.id, &en);
    const qa_inventory_entry *cells = NULL;
    for (size_t i = 0; i < en; i++)
      if (entries[i].item != NULL && strcmp(entries[i].item, "q2:ammo_cells") == 0) cells = &entries[i];
    if (cells == NULL) qa_raise("Error", 0, 0, "LMCTF plasma fired without its admitted cell counter");
    qa_inventory_entry entry = *cells;
    entry.has_count_policy = true;
    entry.count_policy.kind = "source-counter";
    entry.count_policy.arithmetic = "int32";
    qa_shared_inventory_table_configure(g->host->inventory, self, entry, NULL, NULL);
    qa_shared_inventory_table_adjust_source_counter(g->host->inventory, self, "q2:ammo_cells", -9);
    if ((qa_js_to_int32(qa_q2_options_deathmatch_flags(&g->options)) & 8192) == 0)
      qa_shared_inventory_table_adjust_source_counter(g->host->inventory, self, "q2:ammo_cells", -1);
    weapons->base.hooks.ammo_changed(weapons->base.hooks.ctx, self.id, "q2:ammo_cells");
    qa_q2_base_player_state *player = w->context->hooks.player(w->context->hooks.ctx, self.id);
    if (player != NULL) {
      player->damage_pitch = -2;
      player->damage_roll = (g->host->random(g->host->ctx) * 2 - 1) * 2;
      player->damage_time = now + 0.5;
    }
    qa_q2w_ballistics_player_noise(&weapons->base, self, g, start, "weapon");
  }
  state->frame++;
}

static bool selection_choose(void *ctx, qa_q2w_owner self, qa_q2_game_services *g, qa_q2w_state *state) {
  qa_lmctf_weapons *w = ctx;
  if (state->weapon != NULL && strcmp(state->weapon, "lmctf:plasma") == 0) {
    qa_lmctf_player_state *player = qa_lmctf_player(w->context, self.actor.id);
    player->plasma_mode = !player->plasma_mode;
    mode_print(w, self.actor, g);
  }
  return true;
}

static void free_fn(void *ctx, qa_q2_entity *self, qa_q2_game_services *g) {
  (void)ctx;
  g->remove(g->ctx, self);
}

static void impact(qa_lmctf_weapons *w, qa_q2_entity *self, qa_q2_game_services *g, qa_touch_contact contact, bool reflect) {
  if (contact.surface != NULL && (qa_js_to_int32(contact.surface->native_flags) & 4) != 0) {
    g->remove(g->ctx, self);
    return;
  }
  if (!reflect) {
    qa_q2_entity *other = qa_q2_game_entity_of(g, &contact.other);
    if (other != NULL && strcmp(other->classname, "goop") == 0) return;
  }
  qa_body_state body = qa_q2_game_body(g, self);
  qa_q2_entity *owner = self->has_owner ? qa_q2_game_entity_of(g, &self->owner) : NULL;
  qa_vec3 normal = contact.plane != NULL ? contact.plane->normal : QA_Q2_ZERO;
  double damage = (reflect ? 39 : 28) * (w->context->plasma_quad ? 4 : 1);
  qa_combat_state_result target = qa_q2mp_combat_read(g, contact.other);
  bool hurt = target.has_value && target.value.can_take_damage;
  if (owner != NULL && g->host->is_player(g->host->ctx, owner->actor.id))
    qa_q2w_ballistics_player_noise(&w->context->hooks.weapons->base, owner->actor, g, body.origin, "impact");
  if (hurt) {
    qa_actor_id owner_id = self->has_owner ? self->owner : self->actor.id;
    g->damage(g->ctx, contact.other, self, self->actor.id, self->has_owner ? &owner_id : NULL, damage, 1,
              body.velocity, body.origin, normal, 34, 4, "q2:weapon_plasma");
  } else {
    qa_q2_presentation_event event = {0};
    event.kind = "effect";
    event.effect = "q2:laser_sparks";
    event.origin = body.origin;
    event.direction = normal;
    event.count = 32;
    event.color = 176;
    g->host->emit(g->host->ctx, event);
    qa_actor_id owner_id = self->has_owner ? self->owner : self->actor.id;
    g->radius_damage(g->ctx, self, self->has_owner ? &owner_id : NULL, damage, NULL, damage + 70, 34, 0, "q2:weapon_plasma");
  }
  if (reflect && !hurt) {
    g->sound(g->ctx, self->actor, "weapons/plasma/bounce.wav", 4, 1, 3);
    return;
  }
  g->sound(g->ctx, self->actor, "weapons/plasma/hit.wav", 4, 1, 2);
  g->solid(g->ctx, self, "none");
  self->touch = (qa_q2_touch){0};
  self->model = "sprites/s_plasma2.sp2";
  self->frame = 0;
  self->sound = "";
  qa_q2_body_changes move = {0};
  move.has_origin = true;
  move.origin = qa_q2_add(body.origin, qa_q2_scale(body.velocity, -0.1));
  move.has_velocity = true;
  move.velocity = QA_Q2_ZERO;
  qa_q2_game_move(g, self, move, true);
  g->show(g->ctx, self);
  qa_q2_think free_think = {w, free_fn};
  g->schedule(g->ctx, self, 0.1, free_think);
}

static void reflect_fn(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_touch_contact contact) {
  impact(ctx, self, g, contact, true);
}
static void spread_fn(void *ctx, qa_q2_entity *self, qa_q2_game_services *g, qa_touch_contact contact) {
  impact(ctx, self, g, contact, false);
}

qa_lmctf_weapons *qa_lmctf_weapons_create(qa_lmctf_context *context) {
  qa_lmctf_weapons *w = calloc(1, sizeof *w);
  if (w == NULL) return NULL;
  w->context = context;
  w->thinks[0] = (qa_q2_named_think){"lmctf:plasma_free", {w, free_fn}};
  w->touches[0] = (qa_q2_named_touch){"lmctf:plasma_reflect_touch", {w, reflect_fn}};
  w->touches[1] = (qa_q2_named_touch){"lmctf:plasma_spread_touch", {w, spread_fn}};
  w->callbacks.think = w->thinks; w->callbacks.think_count = 1;
  w->callbacks.touch = w->touches; w->callbacks.touch_count = 2;
  qa_q2_item_definition def = {0};
  def.kind = "weapon";
  def.classname = QA_LMCTF_PLASMA.classname;
  def.ammo = QA_LMCTF_PLASMA.ammo;
  def.model = QA_LMCTF_PLASMA.world_model;
  def.icon = "w_plasma";
  def.name = "Plasma Rifle";
  def.sound = "misc/w_pkup.wav";
  def.rotate = true;
  def.respawn = 30;
  def.has_coop_stay = true;
  def.coop_stay = true;
  qa_q2_item_module_register(context->hooks.items, def);
  qa_q2w_extension extension = {0};
  extension.definition = &QA_LMCTF_PLASMA;
  extension.ctx = w;
  extension.fire = weapon_fire;
  extension.think = weapon_think;
  extension.selection_requested = QA_LMCTF_PLASMA.name;
  extension.selection_choose = selection_choose;
  qa_q2w_weapons_register(context->hooks.weapons, extension);
  return w;
}
void qa_lmctf_weapons_destroy(qa_lmctf_weapons *w) { free(w); }
qa_q2_callback_definitions qa_lmctf_weapons_callbacks(qa_lmctf_weapons *w) { return w->callbacks; }

void qa_lmctf_weapons_launch(qa_lmctf_weapons *w, qa_owned_actor owner, qa_q2_game_services *g, qa_vec3 start, qa_vec3 direction, bool reflect) {
  qa_vec3 angles = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE);
  static const double spread_yaws[3] = {0, 10, -10};
  const double *yaws = reflect ? spread_yaws : spread_yaws;
  size_t count = reflect ? 1 : 3;
  for (size_t i = 0; i < count; i++) {
    double yaw = yaws[i];
    qa_q2_entity *goop = g->create(g->ctx, "goop", NULL, 0);
    qa_vec3 dir = yaw == 0 ? direction
                           : qa_math_angle_vectors_rule((qa_vec3){angles.x, angles.y + yaw, angles.z}, QA_ANGLE_RULE_NATIVE).forward;
    qa_vec3 velocity = qa_q2_scale(dir, 1200);
    goop->has_owner = true;
    goop->owner = owner.id;
    goop->clip_mask = 0x6000003;
    goop->server_flags = 2;
    goop->damage = reflect ? 39 : 1;
    goop->effects = 0x100000 | 0x2000;
    goop->render_flags = 32;
    goop->model = "sprites/s_plasma1.sp2";
    goop->sound = "weapons/plasma/flyby.wav";
    goop->touch = reflect ? (qa_q2_touch){w, reflect_fn} : (qa_q2_touch){w, spread_fn};
    qa_q2_body_changes move = {0};
    move.has_origin = true; move.origin = start;
    move.has_velocity = true; move.velocity = velocity;
    move.has_angles = true; move.angles = reflect ? velocity : QA_Q2_ZERO;
    move.has_bounds = true;
    move.bounds = reflect ? (qa_bounds){{-12, -12, -12}, {12, 12, 12}} : (qa_bounds){{0, 0, 0}, {0, 0, 0}};
    qa_q2_game_move(g, goop, move, true);
    g->solid(g->ctx, goop, "box");
    g->motion(g->ctx, goop, reflect ? "wall-bounce" : "fly-missile");
    g->show(g->ctx, goop);
    qa_q2_think free_think = {w, free_fn};
    g->schedule(g->ctx, goop, reflect ? 1.5 : 3, free_think);
  }
}
