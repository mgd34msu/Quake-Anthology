/* Port of src/content/q1/missionpacks/arsenal.ts. Mission weapon
 * definitions are static records (TS registerHipnoticWeapons is a free
 * function, so the game borrows storage that outlives any arsenal); each
 * record is its own ctx, carrying the weapon id the TS closure captures.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/arsenal.h"

#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/backpacks.h"
#include "qa/content/q1/missionpacks/hipnotic_weapons.h"
#include "qa/content/q1/missionpacks/pickup_rules.h"
#include "qa/content/q1/missionpacks/rogue_weapons.h"
#include "qa/content/q1/missionpacks/selection.h"

static bool fire(qa_q1_services *game, qa_q1_player_state *player, const char *weapon) {
  if (qa_q1mp_streq(weapon, "hipnotic:laser")) return qa_q1mp_fire_hipnotic_laser(game, player);
  if (qa_q1mp_streq(weapon, "hipnotic:mjolnir")) return qa_q1mp_fire_hipnotic_mjolnir(game, player);
  if (qa_q1mp_streq(weapon, "hipnotic:proximity")) return qa_q1mp_fire_hipnotic_proximity(game, player);
  if (qa_q1mp_streq(weapon, "rogue:lava-nailgun") || qa_q1mp_streq(weapon, "rogue:lava-supernailgun")) return qa_q1mp_fire_rogue_lava(game, player);
  if (qa_q1mp_streq(weapon, "rogue:multi-grenade")) return qa_q1mp_fire_rogue_multi_grenade(game, player);
  if (qa_q1mp_streq(weapon, "rogue:multi-rocket")) return qa_q1mp_fire_rogue_multi_rocket(game, player);
  return qa_q1mp_fire_rogue_plasma(game, player); /* "rogue:plasma": the TS switch is exhaustive */
}
static void animate(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double seconds) {
  (void)ctx;
  if (player->continuous_firing || player->weapon_animation_at < 0) return;
  const double step = floor((seconds - player->weapon_animation_at) / 0.1);
  const double frame = step >= 6 ? 0 : qa_q1mp_streq(player->weapon, "hipnotic:mjolnir") ? qa_js_min(4, step + 1) : step + 1;
  if (frame != player->weapon_frame) {
    player->weapon_frame = frame;
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "weapon"; event.has_player = true; event.player = player->actor.id; event.weapon = player->weapon;
    event.view_model = qa_q1_services_weapon_model(game, player->weapon, NULL); event.frame = frame; event.punch = 0;
    qa_q1_emit(game, event);
  }
  if (step >= 6) player->weapon_animation_at = -1;
}
static bool best_available(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player) {
  const qa_q1_weapon_definition *definition = ctx;
  return !qa_q1mp_streq(definition->id, "rogue:lava-supernailgun") ||
         qa_shared_inventory_table_count(runtime->host.inventory, player->actor.id, "rogue:ammo/lava-nails") >= 2;
}
static bool definition_fire(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player) {
  const qa_q1_weapon_definition *definition = ctx;
  return fire(runtime, player, definition->id);
}
#define MISSION_DEFINITION(index, weapon, ammo_item, view, weapon_rank) \
  {.ctx = &MISSION_DEFINITIONS[index], .id = (weapon), .ammo = (ammo_item), .model = (view), .rank = (weapon_rank), \
   .best_available = best_available, .fire = definition_fire, .animate = animate}
static qa_q1_weapon_definition MISSION_DEFINITIONS[QA_Q1MP_MISSION_WEAPONS_COUNT] = {
  MISSION_DEFINITION(0, "hipnotic:laser", "q1:ammo/cells", "progs/v_laserg.mdl", 3),
  MISSION_DEFINITION(1, "hipnotic:mjolnir", NULL, "progs/v_hammer.mdl", 9),
  MISSION_DEFINITION(2, "hipnotic:proximity", "q1:ammo/rockets", "progs/v_prox.mdl", 5),
  MISSION_DEFINITION(3, "rogue:lava-nailgun", "rogue:ammo/lava-nails", "progs/v_lava.mdl", 4),
  MISSION_DEFINITION(4, "rogue:lava-supernailgun", "rogue:ammo/lava-nails", "progs/v_lava2.mdl", 2),
  MISSION_DEFINITION(5, "rogue:multi-grenade", "rogue:ammo/multi-rockets", "progs/v_multi.mdl", 6),
  MISSION_DEFINITION(6, "rogue:multi-rocket", "rogue:ammo/multi-rockets", "progs/v_multi2.mdl", 1),
  MISSION_DEFINITION(7, "rogue:plasma", "rogue:ammo/plasma", "progs/v_plasma.mdl", 0),
};
static void register_weapon_definitions(qa_q1_services *game, qa_q1mp_pack pack) {
  char prefix[32];
  (void)snprintf(prefix, sizeof prefix, "%s:", pack);
  for (size_t i = 0; i < QA_Q1MP_MISSION_WEAPONS_COUNT; i++)
    if (qa_q1mp_starts(qa_q1mp_mission_weapons[i].id, prefix)) qa_q1_register_weapon(game, &MISSION_DEFINITIONS[i]);
  static const qa_q1_weapon HIPNOTIC_ORDER[] = {"lightning", "hipnotic:laser", "supernailgun", "supershotgun", "nailgun", "shotgun", "hipnotic:mjolnir", "axe"};
  static const qa_q1_weapon ROGUE_ORDER[] = {"lightning", "rogue:lava-supernailgun", "supernailgun", "rogue:lava-nailgun", "nailgun", "supershotgun", "shotgun", "axe"};
  const bool hipnotic = qa_q1mp_streq(pack, "hipnotic");
  qa_q1_register_weapon_order(game, hipnotic ? "q1:hipnotic" : "q1:rogue", hipnotic ? HIPNOTIC_ORDER : ROGUE_ORDER, 8);
}
void qa_q1mp_register_hipnotic_weapons(qa_q1_services *game) {
  qa_q1mp_register_hipnotic_weapon_callbacks(game);
  register_weapon_definitions(game, "hipnotic");
}

bool qa_q1mp_arsenal_horn_charmer(const qa_q1mp_mission_pack_arsenal *arsenal, qa_actor_id *out) {
  if (!arsenal->has_charmer) return false;
  *out = arsenal->charmer;
  return true;
}
bool qa_q1mp_arsenal_impulse(qa_q1mp_mission_pack_arsenal *arsenal, qa_actor_id actor, double impulse) {
  qa_q1_player_state *player = qa_q1_player(arsenal->game, actor);
  if (player == NULL) return false;
  if (qa_q1mp_streq(arsenal->pack, "rogue") && (impulse == 20 || impulse == 21)) {
    if (impulse == 20) (void)qa_q1mp_toss_rogue_backpack(arsenal->game, player);
    else (void)qa_q1mp_toss_rogue_weapon(arsenal->game, player);
    return true;
  }
  qa_q1mp_players_enable_combos(&arsenal->players, player);
  return qa_q1mp_mission_weapon_impulse(arsenal->game, player, arsenal->pack, impulse);
}
static void item_powerup(void *ctx, qa_q1_player_state *player, qa_q1_powerup powerup, double seconds) {
  qa_q1mp_mission_pack_arsenal *arsenal = ctx;
  qa_q1mp_players_powerup(&arsenal->players, player, powerup, seconds);
}
static bool item_sphere(void *ctx, qa_q1_actor *item, qa_q1_player_state *player) {
  qa_q1mp_mission_pack_arsenal *arsenal = ctx;
  return qa_q1mp_players_sphere(&arsenal->players, item, player);
}
static void item_enable_combos(void *ctx, qa_q1_player_state *player) {
  qa_q1mp_mission_pack_arsenal *arsenal = ctx;
  qa_q1mp_players_enable_combos(&arsenal->players, player);
}
static void item_horn(void *ctx, qa_q1_actor *item, qa_q1_player_state *player) {
  qa_q1mp_mission_pack_arsenal *arsenal = ctx;
  const bool had = arsenal->has_charmer;
  const qa_actor_id previous = arsenal->charmer;
  arsenal->has_charmer = true; arsenal->charmer = player->actor.id;
  qa_q1_use_targets(arsenal->game, item, &player->actor.id);
  arsenal->has_charmer = had; arsenal->charmer = previous; /* TS finally */
}
void qa_q1mp_mission_pack_arsenal_init(qa_q1mp_mission_pack_arsenal *arsenal, qa_q1_services *game, qa_q1mp_pack pack) {
  memset(arsenal, 0, sizeof *arsenal);
  arsenal->game = game; arsenal->pack = pack;
  qa_q1mp_mission_pack_players_init(&arsenal->players, game, pack);
  qa_q1mp_register_mission_pack_pickup_rules(game, pack, &arsenal->players);
  if (qa_q1mp_streq(pack, "hipnotic")) qa_q1mp_register_hipnotic_weapons(game);
  else {
    qa_q1mp_register_rogue_weapon_callbacks(game);
    qa_q1mp_register_rogue_toss_callbacks(game, &arsenal->players);
    qa_q1mp_rogue_grapple_init(&arsenal->grapple_storage, game);
    arsenal->grapple = &arsenal->grapple_storage;
    register_weapon_definitions(game, pack);
  }
  arsenal->items.pack = pack; arsenal->items.ctx = arsenal;
  arsenal->items.powerup = item_powerup; arsenal->items.sphere = item_sphere;
  arsenal->items.enable_combos = item_enable_combos; arsenal->items.horn = item_horn;
  qa_q1mp_register_mission_pack_items(game, &arsenal->items);
}
void qa_q1mp_mission_pack_arsenal_destroy(qa_q1mp_mission_pack_arsenal *arsenal) {
  if (arsenal->grapple != NULL) qa_q1mp_rogue_grapple_destroy(arsenal->grapple);
  qa_q1mp_mission_pack_players_destroy(&arsenal->players);
}
qa_q1mp_mission_pack_arsenal *qa_q1mp_register_mission_pack_arsenal(qa_q1_services *game, qa_q1mp_pack pack, qa_q1mp_mission_pack_arsenal *out) {
  qa_q1mp_mission_pack_arsenal_init(out, game, pack);
  return out;
}
