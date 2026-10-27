/* Port of src/content/q2/multiplayer/lmctf/runes.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/lmctf/runes.h"
#include "../multiplayer_internal.h"

static const qa_bounds RUNE_BOUNDS = {{-15, -15, -15}, {15, 15, 15}};

struct qa_lmctf_runes {
  qa_lmctf_context *context;
  qa_q2_entity *(*red_flag)(void *ctx, qa_q2_game_services *game);
  void *red_flag_ctx;
  bool forward;
  qa_q2_named_think thinks[2];
  qa_q2_named_touch touches[2];
  qa_q2_callback_definitions callbacks;
};

static const qa_lmctf_rune_definition *definition_of(const char *classname) {
  for (int i = 0; i < QA_LMCTF_RUNE_COUNT; i++)
    if (strcmp(QA_LMCTF_RUNES[i].classname, classname) == 0) return &QA_LMCTF_RUNES[i];
  return NULL;
}
static const qa_lmctf_rune_definition *definition_or_fail(qa_q2_entity *entity) {
  const qa_lmctf_rune_definition *def = definition_of(entity->classname);
  if (def == NULL) qa_q2f_error("Unknown LMCTF rune %s", entity->classname);
  return def;
}

static bool pickup_impl(qa_lmctf_runes *r, qa_q2_entity *entity, qa_q2_game_services *g, qa_actor_id actor);
static void toss_rune(qa_lmctf_runes *r, qa_q2_entity *entity, qa_q2_game_services *g);
static void think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g);
static void touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_touch_contact contact) {
  pickup_impl(ctx, entity, g, contact.other);
}
static void drop_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_touch_contact contact) {
  if (entity->has_owner && qa_same_actor(entity->owner, contact.other)) return;
  pickup_impl(ctx, entity, g, contact.other);
}
static void dropped_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_lmctf_runes *r = ctx;
  entity->has_owner = false;
  entity->touch = (qa_q2_touch){r, touch_fn};
  qa_q2_think think = {r, think_fn};
  g->schedule(g->ctx, entity, 0.1, think);
}

static bool item_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_owned_actor player) {
  pickup_impl(ctx, entity, g, player.id);
  return false;
}
static bool item_use(void *ctx, qa_owned_actor player, qa_q2_game_services *g) {
  return qa_lmctf_runes_drop(ctx, player.id, g);
}

qa_lmctf_runes *qa_lmctf_runes_create(qa_lmctf_context *context,
    qa_q2_entity *(*red_flag)(void *ctx, qa_q2_game_services *game), void *red_flag_ctx) {
  qa_lmctf_runes *r = calloc(1, sizeof *r);
  if (r == NULL) return NULL;
  r->context = context;
  r->red_flag = red_flag;
  r->red_flag_ctx = red_flag_ctx;
  r->forward = true;
  r->thinks[0] = (qa_q2_named_think){"lmctf:Rune_Think", {r, think_fn}};
  r->thinks[1] = (qa_q2_named_think){"lmctf:Drop_Rune_Think", {r, dropped_think_fn}};
  r->touches[0] = (qa_q2_named_touch){"lmctf:Rune_Touch", {r, touch_fn}};
  r->touches[1] = (qa_q2_named_touch){"lmctf:Rune_DropTouch", {r, drop_touch_fn}};
  r->callbacks.think = r->thinks; r->callbacks.think_count = 2;
  r->callbacks.touch = r->touches; r->callbacks.touch_count = 2;
  for (int i = 0; i < QA_LMCTF_RUNE_COUNT; i++) {
    const qa_lmctf_rune_definition *rune = &QA_LMCTF_RUNES[i];
    qa_q2_item_definition def = {0};
    def.kind = "custom";
    def.classname = rune->classname;
    def.model = rune->model;
    def.icon = rune->icon;
    def.name = rune->name;
    def.sound = "items/pkup.wav";
    def.rotate = false;
    def.respawn = 0;
    def.capacity = 1;
    def.quantity = 1;
    def.has_coop_stay = true;
    def.coop_stay = false;
    def.droppable = false;
    def.custom_ctx = r;
    def.pickup = item_pickup;
    def.has_use = true;
    def.use = item_use;
    qa_q2_item_module_register(context->hooks.items, def);
  }
  return r;
}
void qa_lmctf_runes_destroy(qa_lmctf_runes *r) { free(r); }
qa_q2_callback_definitions qa_lmctf_runes_callbacks(qa_lmctf_runes *r) { return r->callbacks; }
bool qa_lmctf_runes_capture(qa_lmctf_runes *r) { return r->forward; }
void qa_lmctf_runes_restore(qa_lmctf_runes *r, bool forward) { r->forward = forward; }

const char *qa_lmctf_runes_held(qa_lmctf_runes *r, qa_actor_id actor, qa_q2_game_services *g) {
  qa_lmctf_player_state *state = qa_lmctf_context_get(r->context, actor);
  if (state == NULL || !state->has_rune) return NULL;
  qa_q2_entity *entity = qa_q2_game_entity_of(g, &state->rune);
  if (entity == NULL) return NULL;
  const qa_lmctf_rune_definition *def = definition_of(entity->classname);
  return def == NULL ? NULL : def->kind;
}

static qa_q2_entity *random_spot(qa_lmctf_runes *r, qa_q2_game_services *g) {
  (void)r;
  static const char *const names[3] = {"item_health_small", "item_health_large", "item_health"};
  size_t n = 0;
  qa_q2_entity *const *all = g->entities(g->ctx, &n);
  for (int c = 0; c < 3; c++) {
    size_t count = 0;
    for (size_t i = 0; i < n; i++)
      if (strcmp(all[i]->classname, names[c]) == 0) count++;
    if (count == 0) continue;
    double pick = trunc(g->host->random(g->host->ctx) * (double)count);
    if (pick > 20) pick = 20;
    long index = (long)pick - 1;
    if (index < 0) index = 0; /* TS Math.max(0, ...) */
    size_t at = 0;
    for (size_t i = 0; i < n; i++)
      if (strcmp(all[i]->classname, names[c]) == 0) {
        if ((long)at == index) return all[i];
        at++;
      }
    return NULL;
  }
  return NULL;
}

static qa_q2_entity *farthest_spot(qa_lmctf_runes *r, qa_q2_game_services *g) {
  size_t n = 0;
  qa_q2_entity *const *all = g->entities(g->ctx, &n);
  qa_q2_entity *best = NULL;
  double distance = 0;
  for (size_t i = 0; i < n; i++) {
    if (strcmp(all[i]->classname, "item_health") != 0) continue;
    double nearest = 9999999;
    for (int d = 0; d < QA_LMCTF_RUNE_COUNT; d++) {
      for (size_t j = i + 1; j < n; j++)
        if (strcmp(all[j]->classname, QA_LMCTF_RUNES[d].classname) == 0) {
          double current = qa_q2_length(qa_q2_subtract(qa_q2_game_body(g, all[i]).origin, qa_q2_game_body(g, all[j]).origin));
          if (current < nearest) nearest = current;
          break;
        }
    }
    if (nearest > distance) {
      best = all[i];
      distance = nearest;
    }
  }
  return best != NULL ? best : random_spot(r, g);
}

static void visible(qa_lmctf_runes *r, qa_q2_entity *entity, qa_q2_game_services *g) {
  (void)r;
  entity->visible = true;
  entity->server_flags = (double)((int64_t)entity->server_flags & ~1);
  entity->spawnflags = 0x10000;
  entity->model = definition_or_fail(entity)->model;
  g->motion(g->ctx, entity, "toss");
  g->solid(g->ctx, entity, "trigger");
  g->show(g->ctx, entity);
}

static void toss_rune(qa_lmctf_runes *r, qa_q2_entity *entity, qa_q2_game_services *g) {
  visible(r, entity, g);
  entity->touch = (qa_q2_touch){r, touch_fn};
  qa_vec3 origin = qa_q2_game_body(g, entity).origin;
  qa_vec3 destination = qa_q2_add(origin, (qa_vec3){0, 0, 48});
  bool has_owner = entity->has_owner;
  qa_actor_id owner = entity->owner;
  entity->has_owner = false;
  qa_vec3 velocity = {-2000 + g->host->random(g->host->ctx) * 4000,
                      -2000 + g->host->random(g->host->ctx) * 4000,
                      800 + g->host->random(g->host->ctx) * 200};
  qa_bounds bounds = RUNE_BOUNDS;
  qa_trace_result trace = qa_q2mp_trace(g, origin, destination, &bounds, has_owner ? &owner : NULL, 3);
  qa_q2_body_changes move = {0};
  move.has_bounds = true; move.bounds = bounds;
  move.has_origin = true;
  move.origin = trace.fraction < 1 || trace.all_solid ? origin : destination;
  move.has_velocity = true; move.velocity = velocity;
  move.has_ground = true; move.ground_null = true;
  qa_q2_game_move(g, entity, move, true);
  entity->timestamp = qa_q2mp_now(g);
  qa_q2_think think = {r, think_fn};
  g->schedule(g->ctx, entity, 0.1, think);
}

bool qa_lmctf_runes_spawn(qa_lmctf_runes *r, qa_q2_entity *entity, qa_q2_game_services *g) {
  const qa_lmctf_rune_definition *def = definition_of(entity->classname);
  if (def == NULL) return false;
  entity->model = def->model;
  entity->count = def->bit;
  if (strcmp(def->kind, "vampire") == 0) {
    entity->effects = (double)((int64_t)entity->effects | 0x400);
    entity->render_flags = (double)((int64_t)entity->render_flags | 1024);
  }
  qa_q2_source_callbacks_register(g->source_callbacks, r->callbacks);
  toss_rune(r, entity, g);
  return true;
}

void qa_lmctf_runes_post_spawn(qa_lmctf_runes *r, qa_q2_game_services *g) {
  for (int i = 0; i < QA_LMCTF_RUNE_COUNT; i++) {
    const qa_lmctf_rune_definition *def = &QA_LMCTF_RUNES[i];
    if (((int64_t)r->context->rules->runes & (int64_t)def->bit) == 0) continue;
    qa_q2_entity *spot = farthest_spot(r, g);
    if (spot == NULL) spot = r->red_flag(r->red_flag_ctx, g);
    if (spot == NULL) continue;
    qa_q2_entity *rune = g->create(g->ctx, def->classname, NULL, 0);
    qa_q2_body_changes move = {0};
    move.has_origin = true;
    move.origin = qa_q2_game_body(g, spot).origin;
    qa_q2_game_move(g, rune, move, true);
    qa_lmctf_runes_spawn(r, rune, g);
  }
}

static void think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_lmctf_runes *r = ctx;
  if (strcmp(entity->solid, "none") != 0) {
    const char *kind = definition_or_fail(entity)->kind;
    if (strcmp(kind, "damage") == 0) {
      entity->frame += r->forward ? 1 : -1;
      if (entity->frame >= 5) r->forward = false;
      else if (entity->frame <= 0) r->forward = true;
    } else if (strcmp(kind, "haste") == 0) {
      entity->frame = entity->frame >= 1 && entity->frame <= 15 ? entity->frame + 1 : 5;
    } else if (strcmp(kind, "regen") == 0) {
      entity->frame = fmod(entity->frame + 1, 14);
    } else {
      entity->frame = fmod(entity->frame + 1, 15);
    }
  }
  g->show(g->ctx, entity);
  qa_q2_think self = {r, think_fn};
  g->schedule(g->ctx, entity, 0.1, self);
  if (entity->timestamp + 30 < qa_q2mp_now(g)) {
    qa_q2_entity *spot = random_spot(r, g);
    if (spot == NULL) spot = r->red_flag(r->red_flag_ctx, g);
    if (spot != NULL) {
      qa_q2_body_changes move = {0};
      move.has_origin = true;
      move.origin = qa_q2_game_body(g, spot).origin;
      qa_q2_game_move(g, entity, move, true);
      toss_rune(r, entity, g);
    }
    entity->timestamp = qa_q2mp_now(g);
  }
}

static bool pickup_impl(qa_lmctf_runes *r, qa_q2_entity *entity, qa_q2_game_services *g, qa_actor_id actor) {
  qa_lmctf_player_state *state = qa_lmctf_context_get(r->context, actor);
  qa_q2_entity *player = qa_q2_game_entity_of(g, &actor);
  qa_q2mp_player_state *common = r->context->hooks.player(r->context->hooks.ctx, actor);
  if (state == NULL || player == NULL || common == NULL || common->spectator ||
      qa_q2mp_health_or(g, actor, 0) <= 0 || strcmp(entity->solid, "trigger") != 0) return false;
  if (state->has_rune) {
    entity->touch = (qa_q2_touch){r, touch_fn};
    qa_q2_think think = {r, think_fn};
    g->schedule(g->ctx, entity, 0.1, think);
    return false;
  }
  const qa_lmctf_rune_definition *def = definition_or_fail(entity);
  size_t en = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(g->host->inventory, actor, &en);
  bool has_entry = false;
  for (size_t i = 0; i < en; i++)
    if (entries[i].item != NULL && strcmp(entries[i].item, def->item) == 0) has_entry = true;
  if (!has_entry) {
    qa_inventory_entry entry = {0};
    entry.item = def->item; entry.count = 0; entry.capacity = 1;
    qa_shared_inventory_table_configure(g->host->inventory, player->actor, entry, NULL, NULL);
  }
  qa_shared_inventory_table_give(g->host->inventory, player->actor, def->item, 1);
  state->has_rune = true;
  state->rune = entity->actor.id;
  entity->has_owner = true;
  entity->owner = actor;
  entity->visible = false;
  entity->server_flags = (double)((int64_t)entity->server_flags | 1);
  g->cancel(g->ctx, entity);
  g->solid(g->ctx, entity, "none");
  g->motion(g->ctx, entity, "stationary");
  qa_q2_body_changes move = {0};
  move.has_velocity = true;
  move.velocity = QA_Q2_ZERO;
  qa_q2_game_move(g, entity, move, true);
  g->show(g->ctx, entity);
  g->sound(g->ctx, entity->actor, "misc/power1.wav", 3, 1, 1);
  qa_q2_presentation_event event = {0};
  event.kind = "pickup";
  event.player = actor;
  event.item = def->item;
  event.name = def->name;
  event.icon = def->icon;
  g->host->emit(g->host->ctx, event);
  return true;
}

bool qa_lmctf_runes_pickup(qa_lmctf_runes *r, qa_q2_entity *entity, qa_q2_game_services *g, qa_actor_id actor) {
  return pickup_impl(r, entity, g, actor);
}

bool qa_lmctf_runes_drop(qa_lmctf_runes *r, qa_actor_id actor, qa_q2_game_services *g) {
  qa_lmctf_player_state *state = qa_lmctf_context_get(r->context, actor);
  qa_q2_entity *player = qa_q2_game_entity_of(g, &actor);
  qa_q2_entity *rune = state != NULL && state->has_rune ? qa_q2_game_entity_of(g, &state->rune) : NULL;
  if (state == NULL || player == NULL || rune == NULL) return false;
  qa_shared_inventory_table_consume(g->host->inventory, player->actor, definition_or_fail(rune)->item, 1);
  state->has_rune = false;
  visible(r, rune, g);
  rune->has_owner = true;
  rune->owner = actor;
  rune->touch = (qa_q2_touch){r, drop_touch_fn};
  g->host->random(g->host->ctx);
  g->host->random(g->host->ctx);
  g->host->random(g->host->ctx);
  qa_q2_player_view_result view = g->host->player_view_state(g->host->ctx, actor);
  qa_vec3 forward = qa_math_angle_vectors_rule(view.has_value ? view.value.view_angles : qa_q2_game_body(g, player).angles, QA_ANGLE_RULE_NATIVE).forward;
  qa_lmctf_toss(rune, player, g, forward);
  rune->timestamp = qa_q2mp_now(g);
  qa_q2_think think = {r, dropped_think_fn};
  g->schedule(g->ctx, rune, 1, think);
  g->sound(g->ctx, player->actor, "misc/power2.wav", 3, 1, 1);
  return true;
}

double qa_lmctf_runes_damage(qa_lmctf_runes *r, const qa_actor_id *actor, double amount, qa_q2_game_services *g) {
  if (actor == NULL) return amount;
  const char *held = qa_lmctf_runes_held(r, *actor, g);
  return held != NULL && strcmp(held, "damage") == 0 ? trunc((double)(float)(amount * 1.75)) : amount;
}

double qa_lmctf_runes_after_power_armor(qa_lmctf_runes *r, qa_actor_id actor, double take, qa_q2_game_services *g) {
  const char *held = qa_lmctf_runes_held(r, actor, g);
  if (held == NULL || strcmp(held, "resist") != 0) return take;
  qa_q2_entity *entity = qa_q2_game_entity_of(g, &actor);
  if (entity != NULL) g->sound(g->ctx, entity->actor, "ctf/resist.wav", 3, 1, 1);
  return trunc((double)(float)(take / 1.75));
}

void qa_lmctf_runes_after_health(qa_lmctf_runes *r, qa_actor_id target, const qa_actor_id *attacker, double take, qa_q2_game_services *g) {
  if (attacker == NULL || qa_same_actor(*attacker, target) || take == 0) return;
  const char *held = qa_lmctf_runes_held(r, *attacker, g);
  if (held == NULL || strcmp(held, "vampire") != 0) return;
  qa_q2_entity *victim = qa_q2_game_entity_of(g, &target);
  qa_q2_entity *source = qa_q2_game_entity_of(g, attacker);
  qa_combat_state_result combat = qa_q2mp_combat_read(g, *attacker);
  if (source == NULL || !combat.has_value) return;
  int shift = g->host->is_player(g->host->ctx, target) ? 1
              : victim != NULL && strcmp(victim->classname, "bodyque") == 0 ? 2 : 0;
  if (shift == 0) return;
  double health = combat.value.health + (double)(((int32_t)take) >> shift);
  qa_gameplay_authority_set_health(g->host->combat, source->actor, health > 250 ? 250 : health);
  g->sound(g->ctx, source->actor, "brain/brnatck3.wav", 3, 1, 1);
}

void qa_lmctf_runes_player_frame(qa_lmctf_runes *r, qa_q2_entity *entity, qa_q2_game_services *g) {
  const char *held = qa_lmctf_runes_held(r, entity->actor.id, g);
  if (held == NULL || strcmp(held, "regen") != 0) return;
  qa_combat_state_result combat = qa_q2mp_combat_read(g, entity->actor.id);
  if (!combat.has_value) return;
  qa_lmctf_player_state *state = qa_lmctf_player(r->context, entity->actor.id);
  double heart_rate = trunc(combat.value.health / 5);
  if (heart_rate < 5) heart_rate = 5;
  if (heart_rate > 25) heart_rate = 25;
  double frame = round(qa_q2mp_now(g) * 10);
  if (frame < state->regen_frame + heart_rate) return;
  state->regen_frame = frame;
  bool sound = false;
  if (combat.value.health < entity->max_health + 25) {
    double health = trunc(combat.value.health + (double)(float)(heart_rate / 3));
    double cap = entity->max_health + 25;
    qa_gameplay_authority_set_health(g->host->combat, entity->actor, health < cap ? health : cap);
    sound = true;
  }
  qa_regular_armor_state armor = combat.value.armor.regular;
  if (armor.kind == QA_REGULAR_ARMOR_NONE || armor.points == 0) {
    qa_regular_armor_state regular = {0};
    regular.kind = QA_REGULAR_ARMOR_Q2;
    regular.points = trunc(heart_rate / 4);
    regular.normal_protection = 0.3;
    regular.energy_protection = 0;
    regular.item = "q2:item_armor_jacket";
    qa_gameplay_authority_set_regular_armor(g->host->combat, entity->actor, regular);
    sound = true;
  } else if (armor.points < 200) {
    double points = trunc(armor.points + (double)(float)(heart_rate / 3));
    qa_gameplay_authority_set_regular_points(g->host->combat, entity->actor, points < 200 ? points : 200, NULL);
    sound = true;
  }
  if (sound) g->sound(g->ctx, entity->actor, "ctf/regen.wav", 3, 1, 1);
}

void qa_lmctf_runes_weapon_frame(qa_lmctf_runes *r, qa_owned_actor entity, qa_q2_game_services *g, bool firing,
                                 void (*repeat)(void *ctx), void *repeat_ctx) {
  const char *rune = qa_lmctf_runes_held(r, entity.id, g);
  if (rune != NULL && strcmp(rune, "haste") == 0) {
    if (firing) g->sound(g->ctx, entity, "player/lava1.wav", 3, 1, 1);
    qa_q2mp_weapon_state *weapon = r->context->hooks.weapons->get_state(r->context->hooks.weapons->ctx, entity.id);
    double frame = weapon == NULL ? 0 : weapon->animation.frame;
    if (frame != 0) repeat(repeat_ctx);
  } else if (rune != NULL && strcmp(rune, "damage") == 0 && firing) {
    g->sound(g->ctx, entity, "ctf/strength.wav", 3, 1, 1);
  }
}
