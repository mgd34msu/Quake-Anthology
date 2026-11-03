/* Port of src/content/q1/missionpacks/items.ts. The registered item
 * touch receives the borrowed MissionItemServices as its ctx.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/items.h"

#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/messages.h"

double qa_q1mp_hipnotic_weapon_rank(qa_q1_weapon weapon) {
  if (qa_q1mp_streq(weapon, "lightning")) return 1;
  if (qa_q1mp_streq(weapon, "rocketlauncher")) return 2;
  if (qa_q1mp_streq(weapon, "hipnotic:laser")) return 3;
  if (qa_q1mp_streq(weapon, "supernailgun")) return 4;
  if (qa_q1mp_streq(weapon, "hipnotic:proximity")) return 5;
  if (qa_q1mp_streq(weapon, "grenadelauncher")) return 6;
  if (qa_q1mp_streq(weapon, "supershotgun")) return 7;
  if (qa_q1mp_streq(weapon, "nailgun")) return 8;
  if (qa_q1mp_streq(weapon, "hipnotic:mjolnir")) return 9;
  return 10;
}

/* TS MissionItem: ItemAppearance & the kind union. */
typedef enum item_kind { ITEM_WEAPON, ITEM_AMMO, ITEM_POWERUP, ITEM_HORN, ITEM_SPHERE } item_kind;
typedef struct mission_item {
  item_kind kind;
  const qa_q1mp_mission_weapon_definition *weapon;
  qa_item_id item; double amount;
  qa_q1_powerup powerup; double seconds;
  char model[64]; const char *sound; const char *name; qa_bounds bounds;
} mission_item;
static const qa_bounds ARTIFACT_BOUNDS = {{-16, -16, -24}, {16, 16, 32}};
static const qa_bounds FLOOR_BOUNDS = {{-16, -16, 0}, {16, 16, 32}};
static const qa_bounds WEAPON_BOUNDS = {{-16, -16, 0}, {16, 16, 56}};

static void random_type(qa_q1_services *game, qa_q1_actor *item) {
  const double value = game->host.random(game->host.ctx);
  qa_q1mp_field_text(item, "rogue:random-type",
                     value < 0.2 ? "item_powerup_shield" : value < 0.4 ? "item_powerup_belt" :
                     value < 0.6 ? "item_artifact_invulnerability" : value < 0.8 ? "item_artifact_invisibility" :
                     "item_artifact_super_damage");
}
static void appearance(mission_item *out, item_kind kind, const char *model, const char *sound, const char *name, qa_bounds bounds) {
  out->kind = kind; (void)snprintf(out->model, sizeof out->model, "%s", model);
  out->sound = sound; out->name = name; out->bounds = bounds;
}
static void powerup_item(mission_item *out, qa_q1_powerup powerup, double seconds, const char *model, const char *sound,
                         const char *name, qa_bounds bounds) {
  appearance(out, ITEM_POWERUP, model, sound, name, bounds);
  out->powerup = powerup; out->seconds = seconds;
}
/* False = TS null. */
static bool definition(qa_q1_actor *entity, mission_item *out) {
  memset(out, 0, sizeof *out);
  const char *name = qa_q1mp_streq(entity->classname, "item_random_powerup") ? qa_q1_actor_text(entity, "rogue:random-type")
                                                                             : entity->classname;
  for (size_t i = 0; i < QA_Q1MP_MISSION_WEAPONS_COUNT; i++) {
    const qa_q1mp_mission_weapon_definition *weapon = &qa_q1mp_mission_weapons[i];
    if (weapon->pickup == NULL || !qa_q1mp_streq(weapon->pickup, name)) continue;
    appearance(out, ITEM_WEAPON, weapon->world_model, "weapons/pkup.wav",
               qa_q1mp_streq(weapon->id, "hipnotic:laser") ? "$qc_laser_cannon" :
               qa_q1mp_streq(weapon->id, "hipnotic:mjolnir") ? "$qc_mjolnir" : "$qc_prox_gun", WEAPON_BOUNDS);
    out->weapon = weapon;
    return true;
  }
  if (qa_q1mp_streq(name, "item_artifact_wetsuit")) {
    powerup_item(out, "hipnotic:wetsuit", 30, "progs/wetsuit.mdl", "misc/weton.wav", "$qc_wetsuit", ARTIFACT_BOUNDS); return true;
  }
  if (qa_q1mp_streq(name, "item_artifact_empathy_shields")) {
    powerup_item(out, "hipnotic:empathy", 30, "progs/empathy.mdl", "hipitems/empathy.wav", "$qc_empathy_shields", FLOOR_BOUNDS); return true;
  }
  if (qa_q1mp_streq(name, "item_hornofconjuring")) {
    appearance(out, ITEM_HORN, "progs/horn.mdl", "hipitems/horn.wav", "$qc_horn_of_conjuring", FLOOR_BOUNDS); return true;
  }
  if (qa_q1mp_streq(name, "item_powerup_shield")) {
    powerup_item(out, "rogue:shield", 30, "progs/shield.mdl", "shield/pickup.wav", "$qc_power_shield", ARTIFACT_BOUNDS); return true;
  }
  if (qa_q1mp_streq(name, "item_powerup_belt")) {
    powerup_item(out, "rogue:antigrav", 45, "progs/beltup.mdl", "belt/pickup.wav", "$qc_anti_grav_belt", ARTIFACT_BOUNDS); return true;
  }
  if (qa_q1mp_streq(name, "item_sphere")) {
    appearance(out, ITEM_SPHERE, "progs/sphere.mdl", "sphere/sphere.wav", "$qc_vengeance_sphere", (qa_bounds){{-8, -8, -8}, {8, 8, 8}});
    return true;
  }
  const bool big = (qa_js_to_int32(entity->spawnflags) & 1) != 0;
  if (qa_q1mp_streq(name, "item_lava_spikes") || qa_q1mp_streq(name, "item_multi_rockets") || qa_q1mp_streq(name, "item_plasma")) {
    const bool lava = qa_q1mp_streq(name, "item_lava_spikes"), rockets = qa_q1mp_streq(name, "item_multi_rockets");
    char model[64];
    (void)snprintf(model, sizeof model, "maps/b_%s%d.bsp", lava ? "lnail" : rockets ? "mrock" : "plas", big ? 1 : 0);
    appearance(out, ITEM_AMMO, model, "weapons/lock4.wav", lava ? "$qc_lava_nails" : rockets ? "$qc_multi_rockets" : "plasma",
               (qa_bounds){{0, 0, 0}, {32, 32, 56}});
    out->item = lava ? "rogue:ammo/lava-nails" : rockets ? "rogue:ammo/multi-rockets" : "rogue:ammo/plasma";
    out->amount = (lava ? 25 : rockets ? 5 : 6) * (big ? 2 : 1);
    return true;
  }
  if (qa_q1mp_streq(entity->classname, "item_random_powerup")) {
    const qa_q1_powerup powerup = qa_q1mp_streq(name, "item_artifact_invulnerability") ? "invulnerability" :
                                  qa_q1mp_streq(name, "item_artifact_invisibility") ? "invisibility" : "quad";
    const bool invulnerability = qa_q1mp_streq(powerup, "invulnerability"), invisibility = qa_q1mp_streq(powerup, "invisibility");
    char model[64];
    (void)snprintf(model, sizeof model, "progs/%s.mdl", invulnerability ? "invulner" : invisibility ? "invisibl" : "quaddama");
    powerup_item(out, powerup, 30, model, invulnerability ? "items/protect.wav" : invisibility ? "items/inv1.wav" : "items/damage.wav",
                 qa_q1mp_streq(powerup, "quad") ? "$qc_quad_damage" : invisibility ? "$qc_ring_of_shadows" : "$qc_pentagram_of_protection",
                 ARTIFACT_BOUNDS);
    return true;
  }
  return false;
}

typedef enum take_result { TAKE_REFUSED, TAKE_LEAVE, TAKE_TAKEN } take_result;
static bool rules_auto_switch(qa_q1_services *game, qa_q1_player_state *player, bool owned) {
  const qa_q1_pickup_rules *rules = game->pickup_rules;
  return rules != NULL && rules->auto_switch != NULL ? rules->auto_switch(rules->ctx, game, player, owned) : true;
}
static take_result take_weapon(qa_q1_services *game, qa_q1_player_state *player, const qa_q1mp_mission_weapon_definition *weapon) {
  const qa_q1_pickup_rules *rules = game->pickup_rules;
  const bool leave = rules != NULL && rules->weapon_leave != NULL ? rules->weapon_leave(rules->ctx, game) : false;
  const qa_item_id item = qa_q1_weapon_item(weapon->id);
  const qa_pickup_admission *admission =
      game->pickup_admission != NULL && game->pickup_admission->maps(game->pickup_admission->ctx, "weapons", item)
          ? game->pickup_admission : NULL;
  const bool owned = admission == NULL ? qa_shared_inventory_table_count(game->host.inventory, player->actor.id, item) > 0
                                       : admission->owns(admission->ctx, player->actor.id, item);
  if (leave && owned) return TAKE_REFUSED;
  const qa_item_id ammo = weapon->ammo != NULL ? weapon->ammo : "q1:ammo/cells";
  if (admission != NULL) {
    const qa_pickup_selection selection = rules_auto_switch(game, player, owned)
                                              ? game->options.deathmatch == 0 ? QA_PICKUP_SELECTION_ALWAYS : QA_PICKUP_SELECTION_BETTER
                                              : QA_PICKUP_SELECTION_NEVER;
    const qa_pickup_ammo_grant grants[1] = {{ammo, weapon->pickup_ammo}};
    const qa_pickup_weapon_offer offer = {item, grants, 1};
    if (!admission->weapon(admission->ctx, player->actor, offer, selection)) return TAKE_REFUSED;
    return leave ? TAKE_LEAVE : TAKE_TAKEN;
  }
  (void)qa_shared_inventory_table_give(game->host.inventory, player->actor, item, 1);
  (void)qa_shared_inventory_table_give(game->host.inventory, player->actor, ammo, weapon->pickup_ammo);
  if (rules_auto_switch(game, player, owned)) {
    if (game->options.deathmatch == 0 || weapon->rank < qa_q1mp_hipnotic_weapon_rank(player->weapon))
      (void)qa_q1_select_weapon(game, player->actor, weapon->id);
  }
  return leave ? TAKE_LEAVE : TAKE_TAKEN;
}
static void pickup(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_q1mp_mission_item_services *services) {
  mission_item item;
  const bool known = definition(entity, &item);
  qa_q1_player_state *player = qa_q1_player(game, other);
  if (!known || player == NULL || !qa_q1mp_streq(entity->solid, "trigger") ||
      (item.kind != ITEM_HORN && qa_q1_health(game, other) <= 0)) return;
  take_result result = TAKE_TAKEN;
  switch (item.kind) {
    case ITEM_WEAPON: result = take_weapon(game, player, item.weapon); break;
    case ITEM_AMMO: {
      const qa_pickup_admission *admission = game->pickup_admission;
      if (admission != NULL && admission->maps(admission->ctx, "ammo", item.item)) {
        const bool auto_switch = qa_q1mp_streq(game->options.edition, "classic") || !qa_q1mp_streq(player->auto_switch, "never");
        const qa_pickup_ammo_grant grant = {item.item, item.amount};
        if (!admission->ammo(admission->ctx, player->actor, grant, true, auto_switch)) return;
        break;
      }
      const qa_q1_weapon best = qa_q1_choose_best(game, player->actor);
      if (qa_shared_inventory_table_give(game->host.inventory, player->actor, item.item, item.amount) == 0) return;
      services->enable_combos(services->ctx, player);
      if (qa_q1mp_streq(player->weapon, best) &&
          (qa_q1mp_streq(game->options.edition, "classic") || !qa_q1mp_streq(player->auto_switch, "never")))
        (void)qa_q1_select_weapon(game, player->actor, qa_q1_choose_best(game, player->actor));
      break;
    }
    case ITEM_POWERUP: services->powerup(services->ctx, player, item.powerup, item.seconds); break;
    case ITEM_SPHERE: if (!services->sphere(services->ctx, entity, player)) return; break;
    case ITEM_HORN: break;
  }
  if (result == TAKE_REFUSED) return;
  if (item.kind == ITEM_HORN) qa_q1mp_mission_message(game, &other, "$qc_got_horn");
  else qa_q1mp_mission_pickup_message(game, other, item.name);
  qa_q1_sound(game, player->actor.id, item.sound, item.kind == ITEM_WEAPON || item.kind == ITEM_AMMO ? "item" : "voice",
              item.kind == ITEM_HORN ? 0 : 1, 1);
  qa_q1_effect(game, "pickup", qa_q1_get_body(game, entity).origin, &other, 1);
  if (result == TAKE_LEAVE) return;
  entity->solid = "none"; qa_q1mp_set_string(&entity->model, ""); qa_q1_link(game, entity);
  const double deathmatch = game->options.deathmatch;
  const bool rerelease = qa_q1mp_streq(game->options.edition, "rerelease");
  const bool supply = item.kind == ITEM_WEAPON || item.kind == ITEM_AMMO;
  const double respawn =
      item.kind == ITEM_SPHERE ? 180 :
      item.kind == ITEM_AMMO && rerelease && (deathmatch == 3 || deathmatch == 5) ? 15 :
      supply || (qa_q1mp_streq(entity->classname, "item_random_powerup") && item.kind == ITEM_POWERUP &&
                 !qa_q1mp_streq(item.powerup, "rogue:shield") && !qa_q1mp_streq(item.powerup, "rogue:antigrav")) ? 30 : 60;
  const bool returns = supply ? (qa_q1mp_streq(game->options.edition, "classic") ? deathmatch == 1 : deathmatch != 0 && deathmatch != 2)
                              : deathmatch != 0;
  if (returns)
    qa_q1_schedule(game, entity, respawn,
                   qa_q1_callback_registry_action(game->named, entity,
                                                  qa_q1mp_streq(entity->classname, "item_random_powerup") ? "missionpack:random-regen" : "SUB_regen"));
  else qa_q1_cancel(game, entity);
  if (item.kind == ITEM_HORN) services->horn(services->ctx, entity, player);
  else qa_q1_use_targets(game, entity, &other);
}
static void item_touch(void *ctx, qa_q1_services *runtime, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS (runtime, entity, other) => pickup(...) */
  pickup(runtime, entity, other, ctx);
}
static void random_regen(void *ctx, qa_q1_services *runtime, qa_q1_actor *entity) {
  (void)ctx;
  random_type(runtime, entity);
  mission_item item;
  if (!definition(entity, &item)) qa_range_error("Missing Rogue random powerup definition");
  qa_q1mp_set_string(&entity->model, item.model); qa_q1mp_set_string(&entity->original_model, item.model);
  entity->solid = "trigger";
  qa_q1_sound_defaults(runtime, entity->actor.id, "items/itembk2.wav");
  qa_q1_link(runtime, entity);
}
static void item_spawn(void *ctx, qa_q1_services *runtime, qa_q1_actor *entity) {
  (void)ctx;
  const char *classname = entity->classname; /* the registered classname the TS closure captures */
  if (runtime->options.deathmatch == 0 && (qa_q1mp_streq(classname, "item_sphere") || qa_q1mp_streq(classname, "item_random_powerup"))) {
    qa_q1_remove(runtime, entity);
    return;
  }
  if (qa_q1mp_streq(classname, "item_random_powerup")) random_type(runtime, entity);
  mission_item item;
  if (!definition(entity, &item)) qa_range_error("Unknown mission-pack item %s", classname);
  qa_q1mp_set_string(&entity->model, item.model); qa_q1mp_set_string(&entity->original_model, item.model);
  entity->solid = "none"; entity->movement = "none";
  if (item.kind == ITEM_SPHERE) entity->angular_velocity = QA_Q1MP_V3(40, 40, 40);
  entity->touch = qa_q1_callback_registry_touch(runtime->named, entity, "missionpack:item-touch");
  qa_q1_set_bounds(runtime, entity, item.bounds);
  qa_q1_schedule(runtime, entity, 0.2, qa_q1_callback_registry_action(runtime->named, entity, "PlaceItem"));
}
void qa_q1mp_register_mission_pack_items(qa_q1_services *game, const qa_q1mp_mission_item_services *services) {
  qa_q1_callback_registry_register(game->named, "missionpack:item-touch",
                                   (qa_q1_callback_handlers){.ctx = (void *)services, .touch = item_touch});
  qa_q1_callback_registry_register(game->named, "missionpack:random-regen",
                                   (qa_q1_callback_handlers){.action = random_regen});
  static const char *const HIPNOTIC[] = {"weapon_laser_gun", "weapon_mjolnir", "weapon_proximity_gun", "item_artifact_wetsuit",
                                         "item_artifact_empathy_shields", "item_hornofconjuring"};
  static const char *const ROGUE[] = {"item_lava_spikes", "item_multi_rockets", "item_plasma", "item_powerup_shield",
                                      "item_powerup_belt", "item_sphere", "item_random_powerup"};
  const bool hipnotic = qa_q1mp_streq(services->pack, "hipnotic");
  const char *const *classnames = hipnotic ? HIPNOTIC : ROGUE;
  const size_t count = hipnotic ? sizeof HIPNOTIC / sizeof *HIPNOTIC : sizeof ROGUE / sizeof *ROGUE;
  for (size_t i = 0; i < count; i++) qa_q1_register_spawn(game, classnames[i], item_spawn, NULL);
}
