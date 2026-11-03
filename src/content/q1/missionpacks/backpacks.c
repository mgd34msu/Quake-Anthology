/* Port of src/content/q1/missionpacks/backpacks.ts. The tossed-weapon
 * touch receives the borrowed MissionPackPlayers as ctx. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/backpacks.h"

#include "missionpacks_internal.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/content/q1/missionpacks/messages.h"

qa_q1_weapon qa_q1mp_backpack_base_weapon(qa_q1_weapon weapon) {
  if (qa_q1mp_streq(weapon, "rogue:lava-nailgun")) return "nailgun";
  if (qa_q1mp_streq(weapon, "rogue:lava-supernailgun")) return "supernailgun";
  if (qa_q1mp_streq(weapon, "rogue:multi-grenade")) return "grenadelauncher";
  if (qa_q1mp_streq(weapon, "rogue:multi-rocket")) return "rocketlauncher";
  if (qa_q1mp_streq(weapon, "rogue:plasma")) return "lightning";
  return weapon;
}
qa_q1_actor *qa_q1mp_drop_mission_pack_backpack(qa_q1_services *game, qa_owned_actor actor, qa_q1mp_pack pack) {
  qa_q1_player_state *player = qa_q1_player(game, actor.id);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor.id);
  if (player == NULL || !body.has_value) return NULL;
  qa_shared_inventory_table *inventory = game->host.inventory;
  const double shells = qa_shared_inventory_table_count(inventory, actor.id, "q1:ammo/shells");
  const double nails = qa_shared_inventory_table_count(inventory, actor.id, "q1:ammo/nails");
  const double rockets = qa_shared_inventory_table_count(inventory, actor.id, "q1:ammo/rockets");
  const double cells = qa_shared_inventory_table_count(inventory, actor.id, "q1:ammo/cells");
  qa_q1_backpack_contents contents;
  memset(&contents, 0, sizeof contents);
  if (qa_q1mp_streq(pack, "hipnotic")) {
    if (shells + nails + rockets + cells == 0) return NULL;
    contents.weapon = player->weapon; contents.shells = shells; contents.nails = nails; contents.rockets = rockets;
    contents.cells = qa_q1mp_streq(game->options.edition, "rerelease") &&
                     (qa_q1mp_streq(player->weapon, "hipnotic:laser") || qa_q1mp_streq(player->weapon, "hipnotic:mjolnir"))
                         ? qa_js_max(15, cells) : cells;
    contents.selection = "rank"; contents.has_selection = true;
    contents.avoid_underwater_lightning = true; contents.has_avoid_underwater_lightning = true;
    return qa_q1_drop_backpack(game, body.value.origin, &contents, NULL);
  }
  const double lava = qa_shared_inventory_table_count(inventory, actor.id, "rogue:ammo/lava-nails");
  const double multi = qa_shared_inventory_table_count(inventory, actor.id, "rogue:ammo/multi-rockets");
  const double plasma = qa_shared_inventory_table_count(inventory, actor.id, "rogue:ammo/plasma");
  /* Both source editions omit plasma from this early empty-backpack check. */
  if (shells + nails + rockets + cells + lava + multi == 0) return NULL;
  const qa_q1_backpack_extra extra[3] = {{"rogue:ammo/lava-nails", lava}, {"rogue:ammo/multi-rockets", multi}, {"rogue:ammo/plasma", plasma}};
  contents.weapon = qa_q1mp_backpack_base_weapon(player->weapon);
  contents.shells = shells; contents.nails = nails; contents.rockets = rockets; contents.cells = cells;
  contents.owner_pickup_delay = 1; contents.has_owner_pickup_delay = true;
  contents.extra = extra; contents.extra_count = 3; contents.has_extra = true;
  return qa_q1_drop_backpack(game, body.value.origin, &contents, NULL);
}

static const struct { qa_item_id item; double amount; const char *selected[2]; const char *owners[2]; } TOSS_AMMO[] = {
  {"q1:ammo/shells", 20, {"shotgun", "supershotgun"}, {"shotgun", "supershotgun"}},
  {"q1:ammo/nails", 20, {"nailgun", "supernailgun"}, {"nailgun", "supernailgun"}},
  {"rogue:ammo/lava-nails", 20, {"rogue:lava-nailgun", "rogue:lava-supernailgun"}, {"nailgun", "supernailgun"}},
  {"q1:ammo/rockets", 10, {"grenadelauncher", "rocketlauncher"}, {"grenadelauncher", "rocketlauncher"}},
  {"rogue:ammo/multi-rockets", 10, {"rogue:multi-grenade", "rogue:multi-rocket"}, {"grenadelauncher", "rocketlauncher"}},
  {"q1:ammo/cells", 20, {"lightning", NULL}, {"lightning", NULL}},
  {"rogue:ammo/plasma", 10, {"rogue:plasma", NULL}, {"lightning", NULL}},
};
#define TOSS_AMMO_COUNT (sizeof TOSS_AMMO / sizeof *TOSS_AMMO)
/* amounts.get(item) ?? 0 over the TS Map filled in rule order. */
static double taken_amount(const bool *taken, const double *amounts, qa_item_id item) {
  for (size_t i = 0; i < TOSS_AMMO_COUNT; i++) if (taken[i] && qa_q1mp_streq(TOSS_AMMO[i].item, item)) return amounts[i];
  return 0;
}
qa_q1_actor *qa_q1mp_toss_rogue_backpack(qa_q1_services *game, qa_q1_player_state *player) {
  const qa_item_id ammo = qa_q1_services_weapon_ammo(game, player->weapon);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  qa_shared_inventory_table *inventory = game->host.inventory;
  if (qa_q1mp_teamplay(game) < 1 || ammo == NULL || qa_shared_inventory_table_count(inventory, player->actor.id, ammo) <= 0 ||
      !body.has_value) return NULL;
  bool taken[TOSS_AMMO_COUNT] = {false};
  double amounts[TOSS_AMMO_COUNT] = {0};
  for (size_t i = 0; i < TOSS_AMMO_COUNT; i++) {
    bool selected = false, unowned = true;
    for (size_t w = 0; w < 2; w++) if (TOSS_AMMO[i].selected[w] != NULL && qa_q1mp_streq(TOSS_AMMO[i].selected[w], player->weapon)) selected = true;
    /* take() may run twice per rule; the second consume sees the first. */
    if (selected) {
      const double count = qa_js_min(TOSS_AMMO[i].amount, qa_shared_inventory_table_count(inventory, player->actor.id, TOSS_AMMO[i].item));
      (void)qa_shared_inventory_table_consume(inventory, player->actor, TOSS_AMMO[i].item, count);
      amounts[i] = count; taken[i] = true;
    }
    for (size_t w = 0; w < 2; w++)
      if (TOSS_AMMO[i].owners[w] != NULL &&
          qa_shared_inventory_table_count(inventory, player->actor.id, qa_q1_weapon_item(TOSS_AMMO[i].owners[w])) != 0) unowned = false;
    if (unowned) {
      const double count = qa_js_min(TOSS_AMMO[i].amount, qa_shared_inventory_table_count(inventory, player->actor.id, TOSS_AMMO[i].item));
      (void)qa_shared_inventory_table_consume(inventory, player->actor, TOSS_AMMO[i].item, count);
      amounts[i] = count; taken[i] = true;
    }
  }
  double sum = 0;
  for (size_t i = 0; i < TOSS_AMMO_COUNT; i++) if (taken[i]) sum += amounts[i];
  if (sum == 0) { qa_q1mp_mission_message(game, &player->actor.id, "$qc_no_ammo_available"); return NULL; }
  const qa_vec3 forward = qa_q1_make_vectors(game, player->view_angles).forward;
  qa_q1_backpack_contents contents;
  memset(&contents, 0, sizeof contents);
  contents.weapon = NULL;
  contents.shells = taken_amount(taken, amounts, "q1:ammo/shells");
  contents.nails = taken_amount(taken, amounts, "q1:ammo/nails");
  contents.rockets = taken_amount(taken, amounts, "q1:ammo/rockets");
  contents.cells = taken_amount(taken, amounts, "q1:ammo/cells");
  contents.owner_pickup_delay = 1; contents.has_owner_pickup_delay = true;
  const qa_q1_backpack_extra extra[3] = {
    {"rogue:ammo/lava-nails", taken_amount(taken, amounts, "rogue:ammo/lava-nails")},
    {"rogue:ammo/multi-rockets", taken_amount(taken, amounts, "rogue:ammo/multi-rockets")},
    {"rogue:ammo/plasma", taken_amount(taken, amounts, "rogue:ammo/plasma")}};
  contents.extra = extra; contents.extra_count = 3; contents.has_extra = true;
  qa_q1_backpack_launch launch;
  launch.origin = qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, 16));
  launch.velocity = qa_q1_vscale(qa_q1_aim(game, player->actor, forward), 500);
  launch.movement = "bounce";
  qa_q1_actor *backpack = qa_q1_drop_backpack(game, body.value.origin, &contents, &launch);
  if (backpack != NULL) { backpack->has_owner = true; backpack->owner = player->actor.id; }
  return backpack;
}

static const struct { qa_q1_weapon weapon; qa_q1_weapon powered; const char *classname; const char *model; const char *name; } TOSS_WEAPONS[] = {
  {"supershotgun", NULL, "weapon_supershotgun", "progs/g_shot.mdl", "$qc_double_shotgun"},
  {"nailgun", "rogue:lava-nailgun", "weapon_nailgun", "progs/g_nail.mdl", "$qc_nailgun"},
  {"supernailgun", "rogue:lava-supernailgun", "weapon_supernailgun", "progs/g_nail2.mdl", "$qc_super_nailgun"},
  {"grenadelauncher", "rogue:multi-grenade", "weapon_grenadelauncher", "progs/g_rock.mdl", "$qc_grenade_launcher"},
  {"rocketlauncher", "rogue:multi-rocket", "weapon_rocketlauncher", "progs/g_rock2.mdl", "$qc_rocket_launcher"},
  {"lightning", "rogue:plasma", "weapon_lightning", "progs/g_light.mdl", "$qc_thunderbolt"},
};
#define TOSS_WEAPON_COUNT (sizeof TOSS_WEAPONS / sizeof *TOSS_WEAPONS)
qa_q1_actor *qa_q1mp_toss_rogue_weapon(qa_q1_services *game, qa_q1_player_state *player) {
  size_t found = TOSS_WEAPON_COUNT;
  for (size_t i = 0; i < TOSS_WEAPON_COUNT && found == TOSS_WEAPON_COUNT; i++)
    if (qa_q1mp_streq(TOSS_WEAPONS[i].weapon, player->weapon) ||
        (TOSS_WEAPONS[i].powered != NULL && qa_q1mp_streq(TOSS_WEAPONS[i].powered, player->weapon))) found = i;
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (game->options.deathmatch != 1 || qa_q1mp_teamplay(game) < 1 || found == TOSS_WEAPON_COUNT || !body.has_value) return NULL;
  const qa_q1_weapon weapon = TOSS_WEAPONS[found].weapon, powered = TOSS_WEAPONS[found].powered;
  qa_q1_actor *item = qa_q1_create(game, TOSS_WEAPONS[found].classname, NULL, NULL);
  item->has_owner = true; item->owner = player->actor.id;
  qa_q1mp_set_string(&item->model, TOSS_WEAPONS[found].model);
  item->movement = "bounce"; item->solid = "trigger"; item->movement_flags = qa_js_bit_or(item->movement_flags, 256);
  const qa_vec3 forward = qa_q1_make_vectors(game, player->view_angles).forward;
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true; patch.origin = qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, 16));
  patch.has_velocity = true; patch.velocity = qa_q1_vscale(qa_q1_aim(game, player->actor, forward), 500);
  patch.has_bounds = true; patch.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
  qa_q1_set_body(game, item, &patch);
  (void)qa_shared_inventory_table_consume(game->host.inventory, player->actor, qa_q1_weapon_item(weapon), 1);
  if (powered != NULL) (void)qa_shared_inventory_table_consume(game->host.inventory, player->actor, qa_q1_weapon_item(powered), 1);
  item->touch = qa_q1_callback_registry_touch(game->named, item, "rogue:tossed-weapon-touch");
  qa_q1_schedule(game, item, 120, qa_q1_callback_registry_action(game->named, item, "SUB_Remove"));
  qa_q1_link(game, item);
  (void)qa_q1_select_weapon(game, player->actor, qa_q1_choose_best(game, player->actor));
  return item;
}
static double pickup_rank(qa_q1_services *game, qa_q1_weapon weapon) {
  const qa_q1_pickup_rules *rules = game->pickup_rules;
  return rules != NULL && rules->weapon_rank != NULL ? rules->weapon_rank(rules->ctx, weapon) : 12;
}
static void tossed_weapon_touch(void *ctx, qa_q1_services *runtime, qa_q1_actor *item, qa_actor_id other,
                                const qa_vec3 *normal, const qa_touch_surface *surface) {
  double left;
  double right;
  (void)normal; (void)surface; /* TS (runtime, item, other) */
  qa_q1mp_mission_pack_players *players = ctx;
  qa_q1_player_state *player = qa_q1_player(runtime, other);
  size_t found = TOSS_WEAPON_COUNT;
  for (size_t i = 0; i < TOSS_WEAPON_COUNT && found == TOSS_WEAPON_COUNT; i++)
    if (qa_q1mp_streq(TOSS_WEAPONS[i].classname, item->classname)) found = i;
  if (player == NULL || found == TOSS_WEAPON_COUNT ||
      (item->has_owner && qa_same_actor(item->owner, other) && item->next_think - runtime->time > 119)) return;
  qa_q1mp_mission_pickup_message(runtime, other, TOSS_WEAPONS[found].name);
  qa_q1_sound(runtime, player->actor.id, "weapons/pkup.wav", "item", 1, 1);
  qa_q1_effect(runtime, "pickup", qa_q1_get_body(runtime, item).origin, &other, 1);
  (void)qa_shared_inventory_table_give(runtime->host.inventory, player->actor, qa_q1_weapon_item(TOSS_WEAPONS[found].weapon), 1);
  qa_q1_remove(runtime, item);
  if (runtime->options.deathmatch == 0 || (left = pickup_rank(runtime, TOSS_WEAPONS[found].weapon), right = pickup_rank(runtime, player->weapon), left < right))
    (void)qa_q1_select_weapon(runtime, player->actor, TOSS_WEAPONS[found].weapon);
  qa_q1mp_players_enable_combos(players, player);
}
void qa_q1mp_register_rogue_toss_callbacks(qa_q1_services *game, qa_q1mp_mission_pack_players *players) {
  qa_q1_callback_registry_register(game->named, "rogue:tossed-weapon-touch",
                                   (qa_q1_callback_handlers){.ctx = players, .touch = tossed_weapon_touch});
}
