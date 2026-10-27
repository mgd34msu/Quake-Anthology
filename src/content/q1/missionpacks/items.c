/* Port of src/content/q1/missionpacks/items.ts. Pickup admission
 * (game.pickupAdmission, TS owner foundation/pickups via q1b
 * qa_pickup_admission) is used through the landed q1b pointer when
 * present. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/items.h"
#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/messages.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>

double qa_q1mp_hipnotic_weapon_rank(const char *weapon) {
  if (qa_q1mp_streq(weapon,"lightning")) return 1;
  if (qa_q1mp_streq(weapon,"rocketlauncher")) return 2;
  if (qa_q1mp_streq(weapon,"hipnotic:laser")) return 3;
  if (qa_q1mp_streq(weapon,"supernailgun")) return 4;
  if (qa_q1mp_streq(weapon,"hipnotic:proximity")) return 5;
  if (qa_q1mp_streq(weapon,"grenadelauncher")) return 6;
  if (qa_q1mp_streq(weapon,"supershotgun")) return 7;
  if (qa_q1mp_streq(weapon,"nailgun")) return 8;
  if (qa_q1mp_streq(weapon,"hipnotic:mjolnir")) return 9;
  return 10;
}
typedef struct item_def {
  const char *kind; /* weapon|ammo|powerup|horn|sphere */
  const qa_q1mp_mission_weapon_definition *weapon;
  const char *item; double amount; const char *powerup; double seconds;
  char model[96]; const char *sound; char name[64]; qa_bounds bounds;
} item_def;
static qa_bounds B(double a,double b,double c,double d,double e,double f){ return (qa_bounds){{a,b,c},{d,e,f}}; }
static bool definition(qa_q1mp_actor *entity, item_def *out) {
  memset(out, 0, sizeof *out);
  const char *name = qa_q1mp_streq(entity->classname, "item_random_powerup") ? qa_q1mp_text(entity, "rogue:random-type") : entity->classname;
  for (size_t i = 0; i < QA_Q1MP_MISSION_WEAPONS_COUNT; i++) {
    const qa_q1mp_mission_weapon_definition *w = &qa_q1mp_mission_weapons[i];
    if (w->pickup && qa_q1mp_streq(w->pickup, name)) {
      out->kind = "weapon"; out->weapon = w; (void)snprintf(out->model, sizeof out->model, "%s", w->world_model);
      out->sound = "weapons/pkup.wav";
      (void)snprintf(out->name, sizeof out->name, "%s", qa_q1mp_streq(w->id,"hipnotic:laser") ? "$qc_laser_cannon" : qa_q1mp_streq(w->id,"hipnotic:mjolnir") ? "$qc_mjolnir" : "$qc_prox_gun");
      out->bounds = B(-16,-16,0,16,16,56); return true;
    }
  }
  qa_bounds artifact = B(-16,-16,-24,16,16,32), floor_b = B(-16,-16,0,16,16,32);
  struct { const char *cn; const char *kind; const char *pw; double sec; const char *model; const char *snd; const char *nm; qa_bounds b; } table[] = {
    {"item_artifact_wetsuit","powerup","hipnotic:wetsuit",30,"progs/wetsuit.mdl","misc/weton.wav","$qc_wetsuit",artifact},
    {"item_artifact_empathy_shields","powerup","hipnotic:empathy",30,"progs/empathy.mdl","hipitems/empathy.wav","$qc_empathy_shields",floor_b},
    {"item_hornofconjuring","horn",NULL,0,"progs/horn.mdl","hipitems/horn.wav","$qc_horn_of_conjuring",floor_b},
    {"item_powerup_shield","powerup","rogue:shield",30,"progs/shield.mdl","shield/pickup.wav","$qc_power_shield",artifact},
    {"item_powerup_belt","powerup","rogue:antigrav",45,"progs/beltup.mdl","belt/pickup.wav","$qc_anti_grav_belt",artifact},
    {"item_sphere","sphere",NULL,0,"progs/sphere.mdl","sphere/sphere.wav","$qc_vengeance_sphere",B(-8,-8,-8,8,8,8)},
  };
  for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) if (qa_q1mp_streq(name, table[i].cn)) {
    out->kind = table[i].kind; out->powerup = table[i].pw; out->seconds = table[i].sec;
    (void)snprintf(out->model, sizeof out->model, "%s", table[i].model); out->sound = table[i].snd;
    (void)snprintf(out->name, sizeof out->name, "%s", table[i].nm); out->bounds = table[i].b; return true;
  }
  bool big = (entity->spawnflags & 1) != 0;
  if (qa_q1mp_streq(name,"item_lava_spikes")||qa_q1mp_streq(name,"item_multi_rockets")||qa_q1mp_streq(name,"item_plasma")) {
    bool lava = qa_q1mp_streq(name,"item_lava_spikes"), rockets = qa_q1mp_streq(name,"item_multi_rockets");
    out->kind = "ammo"; out->item = lava ? "rogue:ammo/lava-nails" : rockets ? "rogue:ammo/multi-rockets" : "rogue:ammo/plasma";
    out->amount = (lava ? 25 : rockets ? 5 : 6) * (big ? 2 : 1);
    (void)snprintf(out->model, sizeof out->model, "maps/b_%s%d.bsp", lava ? "lnail" : rockets ? "mrock" : "plas", big ? 1 : 0);
    out->sound = "weapons/lock4.wav";
    (void)snprintf(out->name, sizeof out->name, "%s", lava ? "$qc_lava_nails" : rockets ? "$qc_multi_rockets" : "plasma");
    out->bounds = B(0,0,0,32,32,56); return true;
  }
  if (qa_q1mp_streq(entity->classname, "item_random_powerup")) {
    const char *pw = qa_q1mp_streq(name,"item_artifact_invulnerability") ? "invulnerability" : qa_q1mp_streq(name,"item_artifact_invisibility") ? "invisibility" : "quad";
    /* shield/belt random types were returned by the table above. */
    out->kind = "powerup"; out->powerup = pw; out->seconds = 30;
    (void)snprintf(out->model, sizeof out->model, "progs/%s.mdl", qa_q1mp_streq(pw,"invulnerability")?"invulner":qa_q1mp_streq(pw,"invisibility")?"invisibl":"quaddama");
    static char snd[64]; (void)snprintf(snd, sizeof snd, "items/%s.wav", qa_q1mp_streq(pw,"invulnerability")?"protect":qa_q1mp_streq(pw,"invisibility")?"inv1":"damage"); out->sound = snd;
    (void)snprintf(out->name, sizeof out->name, "%s", qa_q1mp_streq(pw,"quad")?"$qc_quad_damage":qa_q1mp_streq(pw,"invisibility")?"$qc_ring_of_shadows":"$qc_pentagram_of_protection");
    out->bounds = artifact; return true;
  }
  return false;
}
static void random_type(qa_q1mp_game *game, qa_q1mp_actor *item) {
  double v = qa_q1mp_random(game);
  qa_q1b_fields_set(item, "rogue:random-type", v < 0.2 ? "item_powerup_shield" : v < 0.4 ? "item_powerup_belt" : v < 0.6 ? "item_artifact_invulnerability" : v < 0.8 ? "item_artifact_invisibility" : "item_artifact_super_damage");
}
/* pickupRules access through the q1b mirror (weaponLeave is not on
 * qa_q1b_pickup_rules; the wrapper-registered rules are consulted via
 * game->register hook storage in items_ctx when provided). */
typedef struct items_ctx { qa_q1mp_game *game; qa_q1mp_mission_item_services services; qa_q1mp_pickup_rules *rules; } items_ctx;
static const char *take_weapon(items_ctx *c, qa_q1mp_player_state *player, const qa_q1mp_mission_weapon_definition *weapon) {
  qa_q1mp_game *game = c->game; char buf[96]; const char *item = qa_q1mp_weapon_item(weapon->id, buf, sizeof buf);
  bool leave = c->rules && c->rules->weapon_leave ? c->rules->weapon_leave(c->rules->ctx, game) : false;
  bool owned = qa_q1mp_count(game, player->actor.id, item) > 0;
  if (leave && owned) return "refused";
  (void)qa_q1mp_give(game, player->actor, item, 1);
  (void)qa_q1mp_give(game, player->actor, weapon->ammo ? weapon->ammo : "q1:ammo/cells", weapon->pickup_ammo);
  bool autos = c->rules && c->rules->auto_switch ? c->rules->auto_switch(c->rules->ctx, game, player, owned) : true;
  if (autos && (game->base->options.deathmatch == 0 || weapon->rank < qa_q1mp_hipnotic_weapon_rank(player->weapon)))
    (void)game->base->select_weapon(game->base, player->actor, weapon->id);
  return leave ? "leave" : "taken";
}
static void pickup_touch(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *entity, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; items_ctx *c = vctx; qa_q1mp_game *game = c->game; (void)runtime;
  item_def item; qa_q1mp_player_state *player = game->base->player(game->base, other);
  if (!definition(entity, &item) || !player || !qa_q1mp_streq(entity->solid, "trigger") || (!qa_q1mp_streq(item.kind,"horn") && game->base->health(game->base, other) <= 0)) return;
  const char *result = "taken";
  if (qa_q1mp_streq(item.kind, "weapon")) result = take_weapon(c, player, item.weapon);
  else if (qa_q1mp_streq(item.kind, "ammo")) {
    const char *best = game->base->choose_best(game->base, player->actor);
    if (qa_q1mp_give(game, player->actor, item.item, item.amount) == 0) return;
    if (c->services.enable_combos) c->services.enable_combos(c->services.ctx, player);
    if (qa_q1mp_streq(player->weapon, best) && (qa_q1mp_streq(game->base->options.edition,"classic") || !qa_q1mp_streq(player->auto_switch,"never")))
      (void)game->base->select_weapon(game->base, player->actor, game->base->choose_best(game->base, player->actor));
  } else if (qa_q1mp_streq(item.kind, "powerup")) { if (c->services.powerup) c->services.powerup(c->services.ctx, player, item.powerup, item.seconds); }
  else if (qa_q1mp_streq(item.kind, "sphere")) { if (!c->services.sphere || !c->services.sphere(c->services.ctx, entity, player)) return; }
  if (qa_q1mp_streq(result, "refused")) return;
  if (qa_q1mp_streq(item.kind, "horn")) qa_q1mp_mission_message(game, QA_Q1MP_SOME(other), "$qc_got_horn");
  else qa_q1mp_mission_pickup_message(game, other, item.name);
  game->base->sound_owned(game->base, player->actor, item.sound, QA_Q1MP_CH((qa_q1mp_streq(item.kind,"weapon")||qa_q1mp_streq(item.kind,"ammo")) ? "item" : "voice"), qa_q1mp_streq(item.kind,"horn") ? 0 : 1, 1);
  qa_q1mp_effect_a(game, "pickup", game->base->body(game->base, entity).origin, QA_Q1MP_SOME(other), 0);
  if (qa_q1mp_streq(result, "leave")) return;
  entity->solid = "none"; entity->model[0] = '\0'; game->base->link(game->base, entity);
  double dm = game->base->options.deathmatch;
  double respawn = qa_q1mp_streq(item.kind,"sphere") ? 180 : (qa_q1mp_streq(item.kind,"ammo") && qa_q1mp_streq(game->base->options.edition,"rerelease") && (dm==3||dm==5)) ? 15 :
    (qa_q1mp_streq(item.kind,"weapon")||qa_q1mp_streq(item.kind,"ammo")||(qa_q1mp_streq(entity->classname,"item_random_powerup")&&qa_q1mp_streq(item.kind,"powerup")&&!qa_q1mp_streq(item.powerup,"rogue:shield")&&!qa_q1mp_streq(item.powerup,"rogue:antigrav"))) ? 30 : 60;
  bool returns = (qa_q1mp_streq(item.kind,"weapon")||qa_q1mp_streq(item.kind,"ammo")) ? (qa_q1mp_streq(game->base->options.edition,"classic") ? dm == 1 : (dm != 0 && dm != 2)) : dm != 0;
  if (returns) game->base->schedule(game->base, entity, respawn, game->base->named.action(game->base->named.ctx, entity, qa_q1mp_streq(entity->classname,"item_random_powerup") ? "missionpack:random-regen" : "SUB_regen"));
  else game->base->cancel(game->base, entity);
  if (qa_q1mp_streq(item.kind, "horn")) { if (c->services.horn) c->services.horn(c->services.ctx, entity, player); }
  else game->base->use_targets(game->base, entity, QA_Q1MP_SOME(other));
}
static void random_regen(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *entity) {
  items_ctx *c = vctx; (void)runtime; random_type(c->game, entity); item_def item;
  if (!definition(entity, &item)) qa_range_error("Missing Rogue random powerup definition");
  qa_q1mp_set_model(entity, item.model); (void)snprintf(entity->original_model, sizeof entity->original_model, "%s", item.model);
  entity->solid = "trigger"; qa_q1mp_sound(c->game, entity, "items/itembk2.wav"); c->game->base->link(c->game->base, entity);
}
static void item_spawn(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *entity) {
  items_ctx *c = vctx; qa_q1mp_game *game = c->game; (void)runtime; const char *classname = entity->classname;
  if (game->base->options.deathmatch == 0 && (qa_q1mp_streq(classname,"item_sphere")||qa_q1mp_streq(classname,"item_random_powerup"))) { game->base->remove(game->base, entity); return; }
  if (qa_q1mp_streq(classname, "item_random_powerup")) random_type(game, entity);
  item_def item; if (!definition(entity, &item)) qa_range_error("Unknown mission-pack item %s", classname);
  qa_q1mp_set_model(entity, item.model); (void)snprintf(entity->original_model, sizeof entity->original_model, "%s", item.model);
  entity->solid = "none"; entity->movement = "none";
  if (qa_q1mp_streq(item.kind, "sphere")) entity->angular_velocity = QA_Q1MP_V3(40,40,40);
  entity->touch = game->base->named.touch(game->base->named.ctx, entity, "missionpack:item-touch");
  game->base->set_bounds(game->base, entity, item.bounds);
  game->base->schedule(game->base, entity, 0.2, game->base->named.action(game->base->named.ctx, entity, "PlaceItem"));
}
void qa_q1mp_register_mission_pack_items(qa_q1mp_game *game, const qa_q1mp_mission_item_services *services) {
  items_ctx *c = malloc(sizeof *c); c->game = game; c->services = *services; c->rules = NULL;
  qa_q1b_handlers h = {0}; h.ctx = c; h.touch = pickup_touch;
  game->base->named.register_named(game->base->named.ctx, "missionpack:item-touch", &h);
  qa_q1b_handlers h2 = {0}; h2.ctx = c; h2.action = random_regen;
  game->base->named.register_named(game->base->named.ctx, "missionpack:random-regen", &h2);
  static const char *const hip[] = {"weapon_laser_gun","weapon_mjolnir","weapon_proximity_gun","item_artifact_wetsuit","item_artifact_empathy_shields","item_hornofconjuring"};
  static const char *const rog[] = {"item_lava_spikes","item_multi_rockets","item_plasma","item_powerup_shield","item_powerup_belt","item_sphere","item_random_powerup"};
  bool is_hip = qa_q1mp_streq(services->pack, "hipnotic");
  const char *const *list = is_hip ? hip : rog; int count = is_hip ? 6 : 7;
  for (int i = 0; i < count; i++) game->base->register_spawn(game->base, list[i], item_spawn, c);
}
