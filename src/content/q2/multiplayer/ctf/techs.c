/* Port of src/content/q2/multiplayer/ctf/techs.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/ctf/techs.h"
#include "../multiplayer_internal.h"

struct tech_row { const char *classname, *model, *icon, *name; };
static const struct tech_row TECHS[4] = {
    {"item_tech1", "resistance", "tech1", "Disruptor Shield"},
    {"item_tech2", "strength", "tech2", "Power Amplifier"},
    {"item_tech3", "haste", "tech3", "Time Accel"},
    {"item_tech4", "regeneration", "tech4", "AutoDoc"},
};
#define TECH_TIMEOUT 60

struct qa_q2_ctf_techs {
  qa_q2_ctf_context *context;
  qa_q2_named_think thinks[2];
  qa_q2_callback_definitions callbacks;
  char models[4][64];
};

static bool tech_has(qa_q2_ctf_techs *t, const qa_actor_id *actor, qa_q2_game_services *g, int ordinal) {
  if (actor == NULL || qa_q2_ctf_context_get(t->context, *actor) == NULL) return false;
  char item[32];
  snprintf(item, sizeof item, "q2:item_tech%d", ordinal);
  return qa_q2mp_inv_count(g, *actor, item) > 0;
}

static bool pickup(qa_q2_ctf_techs *t, qa_q2_entity *entity, qa_q2_game_services *g, qa_owned_actor player);

qa_q2_ctf_techs *qa_q2_ctf_techs_create(qa_q2_ctf_context *context) {
  qa_q2_ctf_techs *t = calloc(1, sizeof *t);
  if (t == NULL) return NULL;
  t->context = context;
  for (int i = 0; i < 4; i++) snprintf(t->models[i], sizeof t->models[i], "models/ctf/%s/tris.md2", TECHS[i].model);
  extern void qa_q2_ctf_techs_think_impl(void *, qa_q2_entity *, qa_q2_game_services *);
  extern void qa_q2_ctf_techs_spawn_impl(void *, qa_q2_entity *, qa_q2_game_services *);
  t->thinks[0] = (qa_q2_named_think){"TechThink", {t, qa_q2_ctf_techs_think_impl}};
  t->thinks[1] = (qa_q2_named_think){"SpawnTechs", {t, qa_q2_ctf_techs_spawn_impl}};
  t->callbacks.think = t->thinks;
  t->callbacks.think_count = 2;
  return t;
}
void qa_q2_ctf_techs_destroy(qa_q2_ctf_techs *t) { free(t); }
qa_q2_callback_definitions qa_q2_ctf_techs_callbacks(qa_q2_ctf_techs *t) { return t->callbacks; }

static bool item_pickup(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g, qa_owned_actor player) {
  return pickup(ctx, entity, g, player);
}

void qa_q2_ctf_techs_register(qa_q2_ctf_techs *t) {
  for (int i = 0; i < 4; i++) {
    qa_q2_item_definition def = {0};
    def.kind = "custom";
    def.console_give = "individual-only";
    def.classname = TECHS[i].classname;
    def.model = t->models[i];
    def.icon = TECHS[i].icon;
    def.name = TECHS[i].name;
    def.sound = "items/pkup.wav";
    def.rotate = true;
    def.respawn = 0;
    def.capacity = 1;
    def.quantity = 0;
    def.has_coop_stay = true;
    def.coop_stay = false;
    def.droppable = true;
    def.has_use = false;
    def.custom_ctx = t;
    def.pickup = item_pickup;
    qa_q2_item_module_register(t->context->hooks.items, def);
  }
}

bool qa_q2_ctf_techs_spawn(qa_q2_ctf_techs *t, qa_q2_entity *entity, qa_q2_game_services *g) {
  for (int i = 0; i < 4; i++)
    if (strcmp(TECHS[i].classname, entity->classname) == 0)
      return qa_q2_item_module_spawn(t->context->hooks.items, entity, g);
  return false;
}

static bool pickup(qa_q2_ctf_techs *t, qa_q2_entity *entity, qa_q2_game_services *g, qa_owned_actor player) {
  if (strcmp(t->context->match.phase, "setup") == 0 || strcmp(t->context->match.phase, "pregame") == 0) return false;
  qa_q2_ctf_player_state *state = qa_ctf_player(t->context, player.id);
  double now = qa_q2mp_now(g);
  for (int i = 0; i < 4; i++) {
    char item[32];
    snprintf(item, sizeof item, "q2:%s", TECHS[i].classname);
    if (qa_q2mp_inv_count(g, player.id, item) > 0) {
      if (now - state->last_tech_message > 2) {
        state->last_tech_message = now;
        qa_q2_presentation_event event = {0};
        event.kind = "centerprint";
        event.has_actor = true;
        event.actor = player.id;
        event.text = "You already have a TECH powerup.";
        g->host->emit(g->host->ctx, event);
      }
      return false;
    }
  }
  char item[32];
  snprintf(item, sizeof item, "q2:%s", entity->classname);
  /* item string must outlive the give call only. */
  char *item_copy = qa_q2mp_dup(item);
  qa_shared_inventory_table_give(g->host->inventory, player, item_copy, 1);
  free(item_copy);
  state->regen_time = now;
  return true;
}

static qa_q2_entity *find_spawn(qa_q2_ctf_techs *t, qa_q2_game_services *g) {
  (void)t;
  size_t n = 0;
  qa_q2_entity **spots = qa_q2mp_entities_named(g, "info_player_deathmatch", &n);
  double random_pick = floor(g->host->random(g->host->ctx) * 16);
  double remaining = random_pick < 15 ? random_pick : 15;
  long index = -1;
  while (remaining-- > 0) index = index + 1 == (long)n ? -1 : index + 1;
  qa_q2_entity *out = n == 0 ? NULL : spots[index < 0 ? 0 : (size_t)index];
  free(spots);
  return out;
}

void qa_q2_ctf_techs_think_impl(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g);
static qa_q2_entity *create_tech(qa_q2_ctf_techs *t, const char *classname, qa_q2_entity *spot, qa_q2_game_services *g) {
  qa_q2_entity *entity = g->create(g->ctx, classname, NULL, 0);
  qa_q2_item_module *items = t->context->hooks.items;
  if (!qa_q2_item_module_spawn(items, entity, g)) qa_q2f_error("Unregistered CTF tech %s", classname);
  qa_q2_callback_definitions callbacks = qa_q2_item_module_callbacks(items);
  qa_q2_touch touch = {0};
  bool found = false;
  for (size_t i = 0; i < callbacks.touch_count; i++)
    if (strcmp(callbacks.touch[i].name, "Touch_Item") == 0) { touch = callbacks.touch[i].cb; found = true; }
  if (!found) qa_q2f_error("CTF techs require the shared Touch_Item callback");
  entity->spawnflags = 0x10000;
  entity->effects = 1;
  entity->render_flags = 512;
  entity->has_owner = true;
  entity->owner = entity->actor.id;
  entity->touch = touch;
  double yaw = floor(g->host->random(g->host->ctx) * 360);
  if (yaw > 359) yaw = 359;
  qa_vec3 forward = qa_q2_movedir((qa_vec3){0, yaw, 0});
  qa_vec3 velocity = qa_q2_scale(forward, 100);
  velocity.z = 300;
  qa_q2_body_changes move = {0};
  move.has_origin = true;
  move.origin = qa_q2_add(qa_q2_game_body(g, spot).origin, (qa_vec3){0, 0, 16});
  move.has_velocity = true; move.velocity = velocity;
  move.has_bounds = true;
  move.bounds = (qa_bounds){{-15, -15, -15}, {15, 15, 15}};
  qa_q2_game_move(g, entity, move, false);
  g->solid(g->ctx, entity, "trigger");
  g->motion(g->ctx, entity, "toss");
  qa_q2_think think = {t, qa_q2_ctf_techs_think_impl};
  g->schedule(g->ctx, entity, TECH_TIMEOUT, think);
  g->show(g->ctx, entity);
  return entity;
}

void qa_q2_ctf_techs_think_impl(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_ctf_techs *t = ctx;
  qa_q2_entity *spot = find_spawn(t, g);
  if (spot == NULL) {
    qa_q2_think self = {t, qa_q2_ctf_techs_think_impl};
    g->schedule(g->ctx, entity, TECH_TIMEOUT, self);
    return;
  }
  create_tech(t, entity->classname, spot, g);
  g->remove(g->ctx, entity);
}

static void spawn_all(qa_q2_ctf_techs *t, qa_q2_game_services *g) {
  for (int i = 0; i < 4; i++) {
    qa_q2_entity *spot = find_spawn(t, g);
    if (spot != NULL) create_tech(t, TECHS[i].classname, spot, g);
  }
}

void qa_q2_ctf_techs_spawn_impl(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  spawn_all(ctx, g);
  g->remove(g->ctx, entity);
}

void qa_q2_ctf_techs_setup(qa_q2_ctf_techs *t, qa_q2_game_services *g) {
  if (((int64_t)g->options.deathmatch_flags & 524288) != 0) return;
  qa_q2_entity *spawner = g->create(g->ctx, "ctf_tech_spawn", NULL, 0);
  qa_q2_think think = {t, qa_q2_ctf_techs_spawn_impl};
  g->schedule(g->ctx, spawner, 2, think);
}

void qa_q2_ctf_techs_reset(qa_q2_ctf_techs *t, qa_q2_game_services *g) {
  size_t n = 0;
  qa_q2_entity *const *all = g->entities(g->ctx, &n);
  qa_q2_entity **copy = qa_q2mp_xalloc(malloc((n == 0 ? 1 : n) * sizeof *copy));
  for (size_t i = 0; i < n; i++) copy[i] = all[i];
  for (size_t i = 0; i < n; i++) {
    const qa_q2_item_definition *def = qa_q2_item_module_item_definition(t->context->hooks.items, copy[i]);
    if (def == NULL) continue;
    for (int j = 0; j < 4; j++)
      if (strcmp(TECHS[j].classname, def->classname) == 0) g->remove(g->ctx, copy[i]);
  }
  free(copy);
  spawn_all(t, g);
}

void qa_q2_ctf_techs_respawn(qa_q2_ctf_techs *t, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_entity *spot = find_spawn(t, g);
  if (spot != NULL) create_tech(t, entity->classname, spot, g);
  g->remove(g->ctx, entity);
}

void qa_q2_ctf_techs_drop(qa_q2_ctf_techs *t, qa_q2_entity *entity, qa_q2_game_services *g, bool dead) {
  for (int i = 0; i < 4; i++) {
    char item[32];
    snprintf(item, sizeof item, "q2:%s", TECHS[i].classname);
    double count = qa_q2mp_inv_count(g, entity->actor.id, item);
    if (count == 0) continue;
    char *item_copy = qa_q2mp_dup(item);
    qa_q2_drop_options options = {0};
    options.player_death = false;
    qa_q2_entity *dropped = qa_q2_item_module_drop(t->context->hooks.items, entity, g, item_copy, options);
    if (dropped == NULL) { free(item_copy); continue; }
    if (dead) {
      dropped->has_owner = false;
      double rx = floor(g->host->random(g->host->ctx) * 600);
      if (rx > 599) rx = 599;
      double ry = floor(g->host->random(g->host->ctx) * 600);
      if (ry > 599) ry = 599;
      qa_q2_body_changes move = {0};
      move.has_velocity = true;
      move.velocity = (qa_vec3){rx - 300, ry - 300, qa_q2_game_body(g, dropped).velocity.z};
      qa_q2_game_move(g, dropped, move, true);
    }
    qa_q2_think think = {t, qa_q2_ctf_techs_think_impl};
    g->schedule(g->ctx, dropped, TECH_TIMEOUT, think);
    qa_shared_inventory_table_consume(g->host->inventory, entity->actor, item_copy, count);
    free(item_copy);
  }
}

static void tech_sound(qa_q2_ctf_techs *t, qa_owned_actor entity, qa_q2_game_services *g, const char *name) {
  double volume = t->context->hooks.weapons->silencer_shots(t->context->hooks.weapons->ctx, entity.id) > 0 ? 0.2 : 1;
  char path[64];
  snprintf(path, sizeof path, "ctf/%s.wav", name);
  qa_q2_presentation_event event = {0};
  event.kind = "sound";
  event.has_actor = true;
  event.actor = entity.id;
  qa_q2_entity *e = qa_q2_game_entity_of(g, &entity.id);
  event.origin = e == NULL ? QA_Q2_ZERO : qa_q2_game_body(g, e).origin;
  event.path = path;
  event.channel = 2;
  event.volume = volume;
  event.attenuation = 1;
  event.reliable = false;
  event.loop = "once";
  g->host->emit(g->host->ctx, event);
}

double qa_q2_ctf_techs_strength(qa_q2_ctf_techs *t, const qa_actor_id *actor, qa_q2_game_services *g, double damage) {
  return damage != 0 && tech_has(t, actor, g, 2) ? damage * 2 : damage;
}

double qa_q2_ctf_techs_resistance(qa_q2_ctf_techs *t, qa_actor_id actor, qa_q2_game_services *g, double take) {
  if (take == 0 || !tech_has(t, &actor, g, 1)) return take;
  qa_q2_entity *entity = qa_q2_game_entity_of(g, &actor);
  if (entity != NULL) tech_sound(t, entity->actor, g, "tech1");
  return trunc(take / 2);
}

bool qa_q2_ctf_techs_haste(qa_q2_ctf_techs *t, qa_actor_id actor, qa_q2_game_services *g) {
  return tech_has(t, &actor, g, 3);
}

bool qa_q2_ctf_techs_strength_sound(qa_q2_ctf_techs *t, qa_owned_actor entity, qa_q2_game_services *g) {
  if (!tech_has(t, &entity.id, g, 2)) return false;
  qa_q2_ctf_player_state *state = qa_ctf_player(t->context, entity.id);
  if (state->tech_sound_time < qa_q2mp_now(g)) {
    state->tech_sound_time = qa_q2mp_now(g) + 1;
    tech_sound(t, entity, g, qa_q2_item_module_player_powerups(t->context->hooks.items, entity.id).quad_until > qa_q2mp_now(g) ? "tech2x" : "tech2");
  }
  return true;
}

void qa_q2_ctf_techs_haste_sound(qa_q2_ctf_techs *t, qa_owned_actor entity, qa_q2_game_services *g) {
  if (!qa_q2_ctf_techs_haste(t, entity.id, g)) return;
  qa_q2_ctf_player_state *state = qa_ctf_player(t->context, entity.id);
  if (state->tech_sound_time < qa_q2mp_now(g)) {
    state->tech_sound_time = qa_q2mp_now(g) + 1;
    tech_sound(t, entity, g, "tech3");
  }
}

bool qa_q2_ctf_techs_has_regeneration(qa_q2_ctf_techs *t, qa_actor_id actor, qa_q2_game_services *g) {
  return tech_has(t, &actor, g, 4);
}

void qa_q2_ctf_techs_regenerate(qa_q2_ctf_techs *t, qa_q2_entity *entity, qa_q2_game_services *g) {
  if (!qa_q2_ctf_techs_has_regeneration(t, entity->actor.id, g)) return;
  qa_q2_ctf_player_state *state = qa_ctf_player(t->context, entity->actor.id);
  double now = qa_q2mp_now(g);
  qa_combat_state_result current = qa_q2mp_combat_read(g, entity->actor.id);
  if (!current.has_value || state->regen_time >= now) return;
  state->regen_time = now;
  bool noise = false;
  if (current.value.health < 150) {
    double health = current.value.health + 5;
    qa_gameplay_authority_set_health(g->host->combat, entity->actor, health > 150 ? 150 : health);
    state->regen_time += 0.5;
    noise = true;
  }
  qa_regular_armor_state armor = current.value.armor.regular;
  if (armor.kind != QA_REGULAR_ARMOR_NONE && armor.points > 0 && armor.points < 150) {
    double points = armor.points + 5;
    qa_gameplay_authority_set_regular_points(g->host->combat, entity->actor, points > 150 ? 150 : points, NULL);
    state->regen_time += 0.5;
    noise = true;
  }
  if (noise && state->tech_sound_time < now) {
    state->tech_sound_time = now + 1;
    tech_sound(t, entity->actor, g, "tech4");
  }
}
