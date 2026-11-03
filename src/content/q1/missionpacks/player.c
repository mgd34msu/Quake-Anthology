/* Port of src/content/q1/missionpacks/player.ts. The registered extension,
 * damage source effects and the sphere callbacks receive the players
 * struct as ctx. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/player.h"

#include "missionpacks_internal.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/missionpacks/messages.h"

static const struct { const char *base; const char *powered; const char *ammo; const char *message; } COMBOS[] = {
  {"nailgun", "rogue:lava-nailgun", "rogue:ammo/lava-nails", "$qc_lava_enabled"},
  {"supernailgun", "rogue:lava-supernailgun", "rogue:ammo/lava-nails", "$qc_super_lava_enabled"},
  {"grenadelauncher", "rogue:multi-grenade", "rogue:ammo/multi-rockets", "$qc_multi_gl_enabled"},
  {"rocketlauncher", "rogue:multi-rocket", "rogue:ammo/multi-rockets", "$qc_multi_rl_enabled"},
  {"lightning", "rogue:plasma", "rogue:ammo/plasma", "$qc_plasma_enabled"},
};
static const struct { const char *id; const char *warn; const char *lost; const char *sound; } TIMERS[] = {
  {"hipnotic:wetsuit", "$qc_wetsuit_fade", "", "items/suit2.wav"},
  {"hipnotic:empathy", "$qc_empathy_fade", "", "items/suit2.wav"},
  {"rogue:shield", "$qc_shield_failing", "$qc_shield_lost", "shield/fadeout.wav"},
  {"rogue:antigrav", "$qc_antigrav_failing", "$qc_antigrav_lost", "belt/fadeout.wav"},
};

static qa_q1mp_player_state_entry *find_state(qa_q1mp_mission_pack_players *players, qa_actor_id actor) {
  for (size_t i = 0; i < players->state_count; i++)
    if (qa_same_actor(players->states[i].actor, actor)) return &players->states[i];
  return NULL;
}
static void delete_state(qa_q1mp_mission_pack_players *players, qa_actor_id actor) {
  for (size_t i = 0; i < players->state_count; i++) {
    if (!qa_same_actor(players->states[i].actor, actor)) continue;
    memmove(&players->states[i], &players->states[i + 1], (players->state_count - i - 1) * sizeof *players->states);
    players->state_count--;
    return;
  }
}
static void set_state(qa_q1mp_mission_pack_players *players, qa_actor_id actor, qa_q1_actor *state) {
  qa_q1mp_player_state_entry *entry = find_state(players, actor);
  if (entry != NULL) { entry->state = state; return; }
  if (players->state_count == players->state_capacity) {
    const size_t capacity = players->state_capacity != 0 ? players->state_capacity * 2 : 8;
    qa_q1mp_player_state_entry *grown = realloc(players->states, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    players->states = grown; players->state_capacity = capacity;
  }
  players->states[players->state_count++] = (qa_q1mp_player_state_entry){actor, state};
}
static qa_q1_actor *player_state(qa_q1mp_mission_pack_players *players, qa_q1_player_state *player) {
  qa_q1_services *game = players->game;
  const qa_q1mp_player_state_entry *cached = find_state(players, player->actor.id);
  if (cached != NULL && qa_q1_live(game, cached->state)) return cached->state;
  /* A restored world rebuilds its entities, so a cache miss still looks for a saved state entity once. */
  qa_q1_actor *existing = NULL;
  for (size_t i = 0; i < game->entity_count && existing == NULL; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (qa_q1mp_streq(entity->classname, "missionpack_player_state") && entity->has_owner &&
        qa_same_actor(entity->owner, player->actor.id)) existing = entity;
  }
  qa_q1_actor *entity = existing != NULL ? existing : qa_q1_create(game, "missionpack_player_state", NULL, NULL);
  if (existing == NULL) { entity->has_owner = true; entity->owner = player->actor.id; }
  set_state(players, player->actor.id, entity);
  return entity;
}
static void configure(qa_q1_services *game, qa_owned_actor actor, qa_item_id item, double count, double capacity) {
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = item; entry.count = count; entry.capacity = capacity;
  qa_shared_inventory_table_configure(game->host.inventory, actor, entry, NULL, NULL);
}
void qa_q1mp_players_attach(qa_q1mp_mission_pack_players *players, qa_q1_player_state *player) {
  qa_q1_services *game = players->game;
  qa_shared_inventory_table *inventory = game->host.inventory;
  char prefix[32];
  (void)snprintf(prefix, sizeof prefix, "%s:", players->pack);
  for (size_t i = 0; i < QA_Q1MP_MISSION_WEAPONS_COUNT; i++) {
    if (!qa_q1mp_starts(qa_q1mp_mission_weapons[i].id, prefix)) continue;
    const qa_item_id item = qa_q1_weapon_item(qa_q1mp_mission_weapons[i].id);
    configure(game, player->actor, item, qa_shared_inventory_table_count(inventory, player->actor.id, item), 1);
  }
  if (qa_q1mp_streq(players->pack, "rogue")) {
    static const struct { const char *item; double capacity; } ENTRIES[] = {
      {"rogue:ammo/lava-nails", 200}, {"rogue:ammo/multi-rockets", 100}, {"rogue:ammo/plasma", 100}, {"rogue:artifact/vengeance", 1}};
    for (size_t i = 0; i < sizeof ENTRIES / sizeof *ENTRIES; i++)
      configure(game, player->actor, ENTRIES[i].item, qa_shared_inventory_table_count(inventory, player->actor.id, ENTRIES[i].item),
                ENTRIES[i].capacity);
  }
  (void)player_state(players, player);
}
void qa_q1mp_players_enable_combos(qa_q1mp_mission_pack_players *players, qa_q1_player_state *player) {
  if (!qa_q1mp_streq(players->pack, "rogue")) return;
  qa_q1_services *game = players->game;
  qa_shared_inventory_table *inventory = game->host.inventory;
  for (size_t i = 0; i < sizeof COMBOS / sizeof *COMBOS; i++) {
    if (qa_shared_inventory_table_count(inventory, player->actor.id, qa_q1_weapon_item(COMBOS[i].powered)) == 0 &&
        qa_shared_inventory_table_count(inventory, player->actor.id, qa_q1_weapon_item(COMBOS[i].base)) > 0 &&
        qa_shared_inventory_table_count(inventory, player->actor.id, COMBOS[i].ammo) > 0) {
      (void)qa_shared_inventory_table_give(inventory, player->actor, qa_q1_weapon_item(COMBOS[i].powered), 1);
      qa_q1mp_mission_message(game, &player->actor.id, COMBOS[i].message);
    }
  }
}
void qa_q1mp_players_powerup(qa_q1mp_mission_pack_players *players, qa_q1_player_state *player, qa_q1_powerup powerup, double seconds) {
  qa_q1_actor *state = player_state(players, player);
  char key[128];
  (void)snprintf(key, sizeof key, "%s:warned", powerup); (void)qa_q1_actor_field_delete(state, key);
  (void)snprintf(key, sizeof key, "%s:lost", powerup); (void)qa_q1_actor_field_delete(state, key);
  if (qa_q1mp_streq(powerup, "rogue:antigrav")) qa_q1_set_gravity(players->game, player->actor.id, 0.25);
  qa_q1_give_powerup(players->game, player, powerup, seconds);
}
double qa_q1mp_players_gravity_scale(qa_q1mp_mission_pack_players *players, qa_actor_id actor) {
  const qa_q1_player_state *player = qa_q1_player(players->game, actor);
  return (player != NULL ? qa_q1_player_powerup_get(player, "rogue:antigrav") : 0) > players->game->time ? 0.25 : 1;
}
static void frame(qa_q1mp_mission_pack_players *players, qa_q1_player_state *player, double seconds) {
  qa_q1_services *game = players->game;
  qa_q1_actor *state = player_state(players, player);
  qa_q1mp_players_enable_combos(players, player);
  for (size_t i = 0; i < sizeof TIMERS / sizeof *TIMERS; i++) {
    double expires;
    if (!qa_q1_player_powerup_has(player, TIMERS[i].id, &expires)) continue;
    char key[128];
    (void)snprintf(key, sizeof key, "%s:warned", TIMERS[i].id);
    if (expires < seconds + 3 && qa_q1_actor_number(state, key) == 0) {
      qa_q1mp_mission_message(game, &player->actor.id, TIMERS[i].warn);
      qa_q1_sound(game, player->actor.id, TIMERS[i].sound, "auto", 1, 1);
      qa_q1mp_set_mission_number(state, key, 1);
    }
    (void)snprintf(key, sizeof key, "%s:flash", TIMERS[i].id);
    if (expires < seconds + 3 && qa_q1_actor_number(state, key) < seconds) {
      const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
      if (body.has_value) qa_q1_effect(game, "pickup", body.value.origin, &player->actor.id, 1);
      qa_q1mp_set_mission_number(state, key, seconds + 1);
    }
    (void)snprintf(key, sizeof key, "%s:lost", TIMERS[i].id);
    if (expires <= seconds && qa_q1_actor_number(state, key) == 0) {
      if (TIMERS[i].lost[0] != '\0') qa_q1mp_mission_message(game, &player->actor.id, TIMERS[i].lost);
      qa_q1mp_set_mission_number(state, key, 1);
      if (qa_q1mp_streq(TIMERS[i].id, "rogue:antigrav")) qa_q1_set_gravity(game, player->actor.id, 1);
    }
  }
  if (qa_q1_player_powerup_get(player, "rogue:antigrav") > seconds && qa_q1_actor_number(state, "rogue:belt-sound") < seconds) {
    qa_q1_sound(game, player->actor.id, "belt/use.wav", "auto", 1, 1);
    qa_q1mp_set_mission_number(state, "rogue:belt-sound", seconds + 3);
  }
  if (qa_q1_player_powerup_get(player, "hipnotic:wetsuit") > seconds) {
    player->air_finished = seconds + 12;
    if (player->water_level >= 2) {
      const double cycle = qa_q1_actor_number(state, "hipnotic:scuba") - seconds;
      if (qa_q1_actor_number(state, "hipnotic:scuba") < seconds) {
        qa_q1_sound(game, player->actor.id, "misc/wetsuit.wav", "body", 1, 1);
        qa_q1mp_set_mission_number(state, "hipnotic:scuba", seconds + 7);
      } else if (fabs(cycle - 6) < 0.04 || fabs(cycle - 5.5) < 0.04 || fabs(cycle - 5) < 0.04) {
        qa_q1_actor *timer = qa_q1_create(game, "death_bubbles", NULL, NULL);
        timer->has_owner = true; timer->owner = player->actor.id; timer->count = 1;
        qa_q1_schedule(game, timer, 0.1, qa_q1_callback_registry_action(game->named, timer, "base:death_bubbles"));
      }
      const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
      char text[40];
      qa_net_js_number(seconds, text, sizeof text);
      if (body.has_value && !qa_q1mp_streq(qa_q1_actor_text(state, "hipnotic:scaled-time"), text)) {
        qa_body_state next = body.value;
        next.velocity = qa_q1_vscale(body.value.velocity, player->water_level == 2 ? 1.25 : 1.5);
        qa_shared_body_table_write(game->host.bodies, player->actor, next);
        qa_q1mp_field_text(state, "hipnotic:scaled-time", text);
        qa_q1mp_set_mission_number(state, "hipnotic:scaled-level", player->water_level);
      }
    }
  }
  qa_q1_actor *entity = qa_q1_get_entity(game, &player->actor.id);
  if (entity != NULL)
    entity->effects = qa_q1_player_powerup_get(player, "hipnotic:empathy") > seconds ? qa_js_bit_or(entity->effects, 8)
                                                                                     : qa_js_bit_and(entity->effects, ~8);
}
static void after_physics(qa_q1mp_mission_pack_players *players, qa_q1_player_state *player, double seconds) {
  qa_q1_actor *state = player_state(players, player);
  char text[40];
  qa_net_js_number(seconds, text, sizeof text);
  if (qa_q1mp_streq(qa_q1_actor_text(state, "hipnotic:scaled-time"), text)) {
    const qa_body_state_result body = qa_shared_body_table_read(players->game->host.bodies, player->actor.id);
    if (body.has_value) {
      qa_body_state next = body.value;
      next.velocity = qa_q1_vscale(body.value.velocity, qa_q1_actor_number(state, "hipnotic:scaled-level") == 2 ? 0.8 : 0.66);
      qa_shared_body_table_write(players->game->host.bodies, player->actor, next);
    }
    (void)qa_q1_actor_field_delete(state, "hipnotic:scaled-time");
  }
}
static void shield_hit(qa_q1mp_mission_pack_players *players, qa_q1_player_state *player) {
  qa_q1_services *game = players->game;
  qa_q1_actor *state = player_state(players, player);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (!body.has_value) return;
  if (qa_q1_actor_number(state, "rogue:shield-death") <= game->time) {
    qa_q1_actor *shield = qa_q1_create(game, "power_shield", NULL, NULL);
    shield->has_owner = true; shield->owner = player->actor.id;
    qa_q1mp_set_string(&shield->model, "progs/p_shield.mdl");
    shield->delay = qa_q1mp_fround(game->time + 0.3);
    qa_q1_set_bounds(game, shield, QA_Q1_POINT);
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_origin = true; patch.origin = body.value.origin; patch.has_angles = true; patch.angles = body.value.angles;
    qa_q1_set_body(game, shield, &patch);
    qa_q1_link(game, shield);
    qa_q1mp_set_mission_number(state, "rogue:shield-death", shield->delay);
    qa_q1_schedule(game, shield, 0.1, qa_q1_callback_registry_action(game->named, shield, "rogue:shield-think"));
  }
  if (qa_q1_actor_number(state, "rogue:shield-sound") < game->time) {
    qa_q1_sound(game, player->actor.id, "shield/hit.wav", "item", 1, 1);
    qa_q1mp_set_mission_number(state, "rogue:shield-sound", game->time + 0.5);
  }
}
static bool before_quad(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                        const qa_combat_state *attacker, qa_damage_preparation *out) {
  (void)target; (void)attacker; /* TS beforeQuad(request, damage) */
  qa_q1mp_mission_pack_players *players = ctx;
  qa_q1_services *game = players->game;
  qa_q1_player_state *player = qa_q1_player(game, request.target);
  const qa_attack_cause cause = request.attack.cause;
  out->is_continue = true; out->amount = damage;
  if (player == NULL) return true;
  const bool hipnotic = qa_q1mp_streq(players->pack, "hipnotic");
  const bool wetsuit = qa_q1_player_powerup_get(player, "hipnotic:wetsuit") != 0;
  if (hipnotic && cause.kind == QA_ATTACK_CAUSE_Q1 && qa_q1mp_streq(cause.death_type, "discharge") && wetsuit) {
    out->is_continue = false; return true;
  }
  /* weapons.qc LightningDamage refuses a wetsuit wearer for the player, Shambler and gremlin beams alike. */
  if (hipnotic && cause.kind == QA_ATTACK_CAUSE_Q1 && qa_q1mp_streq(request.attack.weapon, qa_q1_services_weapon_item(game, "lightning")) &&
      !qa_q1mp_streq(request.delivery, "radius") && wetsuit) {
    out->is_continue = false; return true;
  }
  if (hipnotic && qa_q1_player_powerup_get(player, "hipnotic:empathy") != 0) {
    qa_q1_actor *state = player_state(players, player);
    if (qa_q1_actor_number(state, "hipnotic:empathy-sound") < game->time) {
      qa_q1_sound(game, player->actor.id, "hipitems/empathy2.wav", "item", 1, 1);
      qa_q1mp_set_mission_number(state, "hipnotic:empathy-sound", game->time + 0.5);
    }
  }
  if (qa_q1mp_streq(players->pack, "rogue") && qa_q1_player_powerup_get(player, "rogue:shield") != 0 && request.attack.inflictor != NULL) {
    const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, request.target);
    const qa_body_state_result incoming = qa_shared_body_table_read(game->host.bodies, *request.attack.inflictor);
    if (body.has_value && incoming.has_value) {
      const double incoming_yaw = qa_q1_yaw_for(qa_q1_vsub(incoming.value.origin, body.value.origin));
      const double hit_angle = incoming_yaw - qa_q1_yaw_for(qa_q1_make_vectors(game, body.value.angles).forward);
      if (!((hit_angle > 90 && hit_angle < 270) || (hit_angle < -90 && hit_angle > -270))) {
        shield_hit(players, player);
        out->amount = qa_q1mp_fround(damage * (qa_q1mp_streq(game->host.classname(game->host.ctx, *request.attack.inflictor), "lava_spike") ? 0.7 : 0.3));
        return true;
      }
    }
  }
  return true;
}
static bool after_quad(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                       const qa_combat_state *attacker_state, qa_damage_preparation *out) {
  (void)target; (void)attacker_state; /* TS afterQuad(request, damage) */
  qa_q1mp_mission_pack_players *players = ctx;
  qa_q1_services *game = players->game;
  qa_q1_player_state *player = qa_q1_player(game, request.target);
  const qa_actor_id *attacker = request.attack.attacker;
  out->is_continue = true; out->amount = damage;
  if (player == NULL) return true;
  if (attacker != NULL) qa_q1mp_set_mission_reference(player_state(players, player), "rogue:killer", attacker);
  const qa_actor_id *inflictor = request.attack.inflictor;
  if (qa_q1mp_streq(players->pack, "hipnotic") && attacker != NULL && !qa_same_actor(*attacker, request.target) &&
      qa_q1_player_powerup_get(player, "hipnotic:empathy") != 0) {
    const qa_q1_player_state *source = inflictor == NULL ? NULL : qa_q1_player(game, *inflictor);
    if (inflictor == NULL || (source != NULL ? qa_q1_player_powerup_get(source, "hipnotic:empathy") : 0) == 0) {
      const double reflected = qa_q1mp_fround(damage / 2);
      const qa_actor_id victim = request.target, reflected_at = *attacker;
      (void)qa_q1_damage(game, reflected_at, victim, &victim, reflected, NULL, "direct", "hipnotic:empathy", NULL);
      out->amount = reflected;
      return true;
    }
  }
  return true;
}
/* sphere_remove: a leaving owner's Vengeance Sphere is removed with a broadcast. */
static void deny_vengeance(qa_q1mp_mission_pack_players *players, qa_actor_id owner) {
  qa_q1_services *game = players->game;
  const qa_q1_actor *owner_entity = qa_q1_get_entity(game, &owner);
  const char *name = owner_entity != NULL ? qa_q1_actor_text(owner_entity, "netname") : "";
  size_t count = 0;
  qa_q1_actor **spheres = qa_q1mp_entities(game, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *sphere = spheres[i];
    if (!qa_q1mp_streq(sphere->classname, "Vengeance") || !sphere->has_owner || !qa_same_actor(sphere->owner, owner) ||
        !qa_q1_live(game, sphere)) continue;
    size_t recipient_count = 0;
    const qa_actor_id *recipients = game->host.players(game->host.ctx, &recipient_count);
    const qa_q1_msg_arg args[1] = {{false, 0, name}};
    for (size_t r = 0; r < recipient_count; r++)
      if (!qa_same_actor(recipients[r], owner)) qa_q1_message(game, &recipients[r], "$qc_death_denied_vengance", false, args, 1);
    qa_q1_remove(game, sphere);
  }
  free(spheres);
}
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q1mp_mission_pack_players *players = ctx;
  qa_q1_services *game = players->game;
  const qa_q1mp_player_state_entry *entry = find_state(players, actor.id);
  qa_q1_actor *state = entry != NULL ? entry->state : NULL;
  delete_state(players, actor.id);
  if (state != NULL && qa_q1mp_streq(players->pack, "rogue") && qa_q1mp_streq(game->options.edition, "rerelease"))
    deny_vengeance(players, actor.id);
  if (state != NULL && qa_q1_live(game, state)) qa_q1_remove(game, state);
}
bool qa_q1mp_players_sphere(qa_q1mp_mission_pack_players *players, qa_q1_actor *item, qa_q1_player_state *player) {
  qa_q1_services *game = players->game;
  if (qa_shared_inventory_table_give(game->host.inventory, player->actor, "rogue:artifact/vengeance", 1) == 0) return false;
  qa_q1_actor *sphere = qa_q1_create(game, "Vengeance", NULL, NULL);
  sphere->has_owner = true; sphere->owner = player->actor.id;
  qa_q1mp_set_string(&sphere->model, "progs/sphere.mdl");
  sphere->movement = "flymissile"; sphere->solid = "none";
  sphere->angular_velocity = QA_Q1MP_V3(40, 40, 40);
  sphere->delay = qa_q1mp_fround(game->time + 30);
  qa_q1_set_origin(game, sphere, qa_q1_get_body(game, item).origin);
  qa_q1_schedule(game, sphere, 0.1, qa_q1_callback_registry_action(game->named, sphere, "rogue:sphere-think"));
  return true;
}
static void sphere_attack(qa_q1mp_mission_pack_players *players, qa_q1_services *game, qa_q1_actor *sphere);
static void sphere_think(qa_q1mp_mission_pack_players *players, qa_q1_services *game, qa_q1_actor *sphere) {
  qa_q1_player_state *player = sphere->has_owner ? qa_q1_player(game, sphere->owner) : NULL;
  const qa_body_state_result body = sphere->has_owner ? qa_shared_body_table_read(game->host.bodies, sphere->owner)
                                                      : (qa_body_state_result){0};
  if (player == NULL || !body.has_value) { qa_q1_remove(game, sphere); return; }
  if (sphere->attack_finished < game->time) {
    qa_q1_sound(game, sphere->actor.id, "sphere/sphere.wav", "voice", 1, 1);
    sphere->attack_finished = qa_q1mp_fround(game->time + 4);
  }
  if (game->time > sphere->delay || qa_q1_health(game, player->actor.id) < 1) {
    (void)qa_shared_inventory_table_consume(game->host.inventory, player->actor, "rogue:artifact/vengeance", 1);
    if (game->time > sphere->delay) {
      qa_q1mp_mission_message(game, &player->actor.id, "$qc_vengeance_lost");
      qa_q1_remove(game, sphere);
      return;
    }
    qa_actor_id killer;
    bool has_killer = qa_q1mp_mission_reference(game, player_state(players, player), "rogue:killer", &killer);
    if (has_killer && !qa_q1_is_player(game, &killer)) {
      const qa_q1_actor *entity = qa_q1_get_entity(game, &killer);
      has_killer = entity != NULL && entity->has_owner;
      if (has_killer) killer = entity->owner;
    }
    if (!has_killer || !qa_q1_is_player(game, &killer)) { qa_q1_remove(game, sphere); return; }
    qa_q1mp_set_mission_reference(sphere, "rogue:enemy", &killer);
    sphere_attack(players, game, sphere);
    return;
  }
  const qa_vec3 center = qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, 48)), source = qa_q1_get_body(game, sphere).origin;
  if (sphere->count < 0 || sphere->count > 3) sphere->count = 0;
  const qa_q1_trace trace = qa_q1mp_trace(game, source, center, NULL, false);
  if (trace.fraction < 1) { qa_q1_set_origin(game, sphere, center); sphere->count++; }
  else {
    const qa_vec3 offset = sphere->count == 0 ? QA_Q1MP_V3(16, 0, 0) : sphere->count == 1 ? QA_Q1MP_V3(0, 16, 0) :
                           sphere->count == 2 ? QA_Q1MP_V3(-16, 0, 0) : QA_Q1MP_V3(0, -16, 0);
    const qa_vec3 direction = qa_q1_vsub(qa_q1_vadd(center, offset), source);
    const double distance = qa_q1_length(direction);
    if (distance < 8) sphere->count++;
    else qa_q1mp_move_missile(game, sphere, qa_q1_vscale(qa_q1_normalize(direction), distance < 50 ? 150 : 500));
  }
  qa_q1_schedule(game, sphere, 0.1, qa_q1_callback_registry_action(game->named, sphere, "rogue:sphere-think"));
}
static void sphere_attack(qa_q1mp_mission_pack_players *players, qa_q1_services *game, qa_q1_actor *sphere) {
  (void)players; /* TS sphereAttack is a method but reads only its game argument */
  sphere->solid = "trigger";
  sphere->touch = qa_q1_callback_registry_touch(game->named, sphere, "rogue:sphere-impact");
  qa_q1_link(game, sphere);
  qa_actor_id target;
  const bool has_target = qa_q1mp_mission_reference(game, sphere, "rogue:enemy", &target);
  const qa_body_state_result body = has_target ? qa_shared_body_table_read(game->host.bodies, target) : (qa_body_state_result){0};
  if (!has_target || !body.has_value || qa_q1_health(game, target) < 1) {
    qa_q1mp_mission_message(game, sphere->has_owner ? &sphere->owner : NULL, "$qc_you_are_denied_vengeance");
    qa_q1_remove(game, sphere);
    return;
  }
  qa_q1mp_move_missile(game, sphere,
                       qa_q1_vscale(qa_q1_normalize(qa_q1_vsub(qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, 22)),
                                                               qa_q1_get_body(game, sphere).origin)), 650));
  qa_q1_schedule(game, sphere, 0.1, qa_q1_callback_registry_action(game->named, sphere, "rogue:sphere-attack"));
}
static void shield_think(void *ctx, qa_q1_services *runtime, qa_q1_actor *shield) {
  (void)ctx;
  const qa_body_state_result body = shield->has_owner ? qa_shared_body_table_read(runtime->host.bodies, shield->owner)
                                                      : (qa_body_state_result){0};
  if (!body.has_value || shield->delay < runtime->time) { qa_q1_remove(runtime, shield); return; }
  if (shield->delay - 0.25 <= runtime->time) qa_q1mp_set_string(&shield->model, "");
  else {
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_origin = true; patch.origin = body.value.origin; patch.has_angles = true; patch.angles = body.value.angles;
    qa_q1_set_body(runtime, shield, &patch);
  }
  qa_q1_schedule(runtime, shield, 0.05, qa_q1_callback_registry_action(runtime->named, shield, "rogue:shield-think"));
}
static void sphere_think_action(void *ctx, qa_q1_services *runtime, qa_q1_actor *sphere) { sphere_think(ctx, runtime, sphere); }
static void sphere_attack_action(void *ctx, qa_q1_services *runtime, qa_q1_actor *sphere) { sphere_attack(ctx, runtime, sphere); }
static void sphere_impact(void *ctx, qa_q1_services *runtime, qa_q1_actor *sphere, qa_actor_id target,
                          const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS (runtime, sphere, target) */
  const qa_actor_id self = sphere->actor.id;
  if (qa_q1_health(runtime, target) != 0) (void)qa_q1_damage(runtime, target, self, &self, 1000, NULL, "direct", "rogue:vengeance", NULL);
  qa_q1_radius_damage(runtime, self, &self, 300, &target, NULL, "rogue:vengeance");
  const qa_vec3 origin = qa_q1_get_body(runtime, sphere).origin, velocity = qa_q1_get_body(runtime, sphere).velocity;
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true; patch.origin = qa_q1_vsub(origin, qa_q1_vscale(qa_q1_normalize(velocity), 8));
  qa_q1_set_body(runtime, sphere, &patch);
  qa_q1_sprite_explosion(runtime, sphere);
}
static void extension_attach(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player) {
  (void)runtime; qa_q1mp_players_attach(ctx, player);
}
static void extension_frame(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player, double seconds) {
  (void)runtime; frame(ctx, player, seconds);
}
static void extension_after_physics(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player, double seconds) {
  (void)runtime; after_physics(ctx, player, seconds);
}
void qa_q1mp_mission_pack_players_init(qa_q1mp_mission_pack_players *players, qa_q1_services *game, qa_q1mp_pack pack) {
  memset(players, 0, sizeof *players);
  players->game = game; players->pack = pack;
  const bool hipnotic = qa_q1mp_streq(pack, "hipnotic");
  players->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, players);
  players->source_effects.ctx = players;
  players->source_effects.before_quad = before_quad;
  players->source_effects.after_quad = after_quad;
  qa_q1_register_damage_source_effects(game, hipnotic ? "q1:hipnotic:effects" : "q1:rogue:effects", players->source_effects);
  players->extension.ctx = players;
  players->extension.id = hipnotic ? "q1:hipnotic:players" : "q1:rogue:players";
  players->extension.attach = extension_attach;
  players->extension.frame = extension_frame;
  players->extension.after_physics = extension_after_physics;
  qa_q1_register_player_extension(game, &players->extension);
  qa_q1_callback_registry_register(game->named, "rogue:shield-think", (qa_q1_callback_handlers){.action = shield_think});
  qa_q1_callback_registry_register(game->named, "rogue:sphere-think", (qa_q1_callback_handlers){.ctx = players, .action = sphere_think_action});
  qa_q1_callback_registry_register(game->named, "rogue:sphere-attack", (qa_q1_callback_handlers){.ctx = players, .action = sphere_attack_action});
  qa_q1_callback_registry_register(game->named, "rogue:sphere-impact", (qa_q1_callback_handlers){.touch = sphere_impact});
}
void qa_q1mp_mission_pack_players_destroy(qa_q1mp_mission_pack_players *players) {
  if (players->game != NULL) qa_session_actor_registry_off_release(players->game->host.actors, players->release_subscription);
  free(players->states);
  players->states = NULL; players->state_count = players->state_capacity = 0;
}
