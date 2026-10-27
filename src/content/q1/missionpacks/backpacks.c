/* Port of src/content/q1/missionpacks/backpacks.ts. dropBackpack via
 * landed qa/content/q1/base/projectiles.h. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/backpacks.h"
#include "missionpacks_internal.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/missionpacks/messages.h"
#include "qa/content/q1/missionpacks/player.h"
#include <math.h>
#include <stdlib.h>

const char *qa_q1mp_backpack_base_weapon(const char *weapon) {
  if (qa_q1mp_streq(weapon,"rogue:lava-nailgun")) return "nailgun";
  if (qa_q1mp_streq(weapon,"rogue:lava-supernailgun")) return "supernailgun";
  if (qa_q1mp_streq(weapon,"rogue:multi-grenade")) return "grenadelauncher";
  if (qa_q1mp_streq(weapon,"rogue:multi-rocket")) return "rocketlauncher";
  if (qa_q1mp_streq(weapon,"rogue:plasma")) return "lightning";
  return weapon;
}
qa_q1mp_actor *qa_q1mp_drop_mission_pack_backpack(qa_q1mp_game *game, qa_owned_actor actor, qa_q1mp_pack pack) {
  qa_q1mp_player_state *player = game->base->player(game->base, actor.id); qa_q1b_body body;
  if (!player || !qa_q1mp_body_read(game, actor.id, &body)) return NULL;
  double shells = qa_q1mp_count(game, actor.id, "q1:ammo/shells"), nails = qa_q1mp_count(game, actor.id, "q1:ammo/nails");
  double rockets = qa_q1mp_count(game, actor.id, "q1:ammo/rockets"), cells = qa_q1mp_count(game, actor.id, "q1:ammo/cells");
  if (qa_q1mp_streq(pack, "hipnotic")) {
    if (shells + nails + rockets + cells == 0) return NULL;
    qa_q1_backpack_contents contents = {0};
    contents.weapon = player->weapon; contents.shells = shells; contents.nails = nails; contents.rockets = rockets;
    contents.cells = (qa_q1mp_streq(game->base->options.edition,"rerelease") && (qa_q1mp_streq(player->weapon,"hipnotic:laser")||qa_q1mp_streq(player->weapon,"hipnotic:mjolnir"))) ? fmax(15, cells) : cells;
    contents.selection = "rank"; contents.has_selection = true; contents.avoid_underwater_lightning = true; contents.has_avoid_underwater_lightning = true;
    return qa_q1_drop_backpack(game->base, body.origin, &contents, NULL);
  }
  double lava = qa_q1mp_count(game, actor.id, "rogue:ammo/lava-nails"), multi = qa_q1mp_count(game, actor.id, "rogue:ammo/multi-rockets"), plasma = qa_q1mp_count(game, actor.id, "rogue:ammo/plasma");
  if (shells + nails + rockets + cells + lava + multi == 0) return NULL;
  qa_q1_backpack_extra extra[] = {{"rogue:ammo/lava-nails", lava}, {"rogue:ammo/multi-rockets", multi}, {"rogue:ammo/plasma", plasma}};
  qa_q1_backpack_contents contents = {0};
  contents.weapon = qa_q1mp_backpack_base_weapon(player->weapon);
  contents.shells = shells; contents.nails = nails; contents.rockets = rockets; contents.cells = cells;
  contents.owner_pickup_delay = 1; contents.has_owner_pickup_delay = true;
  contents.extra = extra; contents.extra_count = 3; contents.has_extra = true;
  return qa_q1_drop_backpack(game->base, body.origin, &contents, NULL);
}
typedef struct toss_ammo { const char *item; double amount; const char *sel[2]; int seln; const char *own[2]; int ownn; } toss_ammo;
static const toss_ammo TOSS_AMMO[] = {
  {"q1:ammo/shells",20,{"shotgun","supershotgun"},2,{"shotgun","supershotgun"},2},
  {"q1:ammo/nails",20,{"nailgun","supernailgun"},2,{"nailgun","supernailgun"},2},
  {"rogue:ammo/lava-nails",20,{"rogue:lava-nailgun","rogue:lava-supernailgun"},2,{"nailgun","supernailgun"},2},
  {"q1:ammo/rockets",10,{"grenadelauncher","rocketlauncher"},2,{"grenadelauncher","rocketlauncher"},2},
  {"rogue:ammo/multi-rockets",10,{"rogue:multi-grenade","rogue:multi-rocket"},2,{"grenadelauncher","rocketlauncher"},2},
  {"q1:ammo/cells",20,{"lightning",NULL},1,{"lightning",NULL},1},
  {"rogue:ammo/plasma",10,{"rogue:plasma",NULL},1,{"lightning",NULL},1},
};
static bool in_list(const char *const *list, int n, const char *w) { for (int i=0;i<n;i++) if (qa_q1mp_streq(list[i],w)) return true;
return false; }
qa_q1mp_actor *qa_q1mp_toss_rogue_backpack(qa_q1mp_game *game, qa_q1mp_player_state *player) {
  qa_item_id ammo = game->base->weapon_ammo(game->base, player->weapon); qa_q1b_body body;
  bool has_body = qa_q1mp_body_read(game, player->actor.id, &body);
  if (qa_q1mp_teamplay(game) < 1 || ammo == NULL || qa_q1mp_count(game, player->actor.id, ammo) <= 0 || !has_body) return NULL;
  double amounts[7] = {0}; bool taken[7] = {0};
  for (size_t i = 0; i < 7; i++) {
    /* TS take() may run twice; second call overwrites with post-consume min. */
    if (in_list(TOSS_AMMO[i].sel, TOSS_AMMO[i].seln, player->weapon)) {
      double c = fmin(TOSS_AMMO[i].amount, qa_q1mp_count(game, player->actor.id, TOSS_AMMO[i].item));
      (void)qa_q1mp_consume(game, player->actor, TOSS_AMMO[i].item, c); amounts[i] = c; taken[i] = true;
    }
    bool all_zero = true; char buf[96];
    for (int j = 0; j < TOSS_AMMO[i].ownn; j++) if (qa_q1mp_count(game, player->actor.id, qa_q1mp_weapon_item(TOSS_AMMO[i].own[j], buf, sizeof buf)) != 0) all_zero = false;
    if (all_zero) {
      double c = fmin(TOSS_AMMO[i].amount, qa_q1mp_count(game, player->actor.id, TOSS_AMMO[i].item));
      (void)qa_q1mp_consume(game, player->actor, TOSS_AMMO[i].item, c); amounts[i] = c; taken[i] = true;
    }
  }
  (void)taken;
  double sum = 0; for (size_t i=0;i<7;i++) sum += amounts[i];
  if (sum == 0) { qa_q1mp_mission_message(game, QA_Q1MP_SOME(player->actor.id), "$qc_no_ammo_available"); return NULL; }
  qa_vec3 forward = qa_q1mp_vectors(game, player->view_angles).forward;
  qa_q1_backpack_extra extra[] = {{"rogue:ammo/lava-nails", amounts[2]}, {"rogue:ammo/multi-rockets", amounts[4]}, {"rogue:ammo/plasma", amounts[6]}};
  qa_q1_backpack_contents contents = {0};
  contents.weapon = NULL; contents.shells = amounts[0]; contents.nails = amounts[1]; contents.rockets = amounts[3]; contents.cells = amounts[5];
  contents.owner_pickup_delay = 1; contents.has_owner_pickup_delay = true; contents.extra = extra; contents.extra_count = 3; contents.has_extra = true;
  qa_q1_backpack_launch launch = {qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,16)), qa_q1mp_vscale(qa_q1mp_aim(game, player->actor, forward), 500), "bounce"};
  qa_q1mp_actor *backpack = qa_q1_drop_backpack(game->base, body.origin, &contents, &launch);
  if (backpack) backpack->owner = QA_Q1MP_SOME(player->actor.id);
  return backpack;
}
typedef struct toss_weapon { const char *weapon; const char *powered; const char *classname; const char *model; const char *name; } toss_weapon;
static const toss_weapon TOSS_WEAPONS[] = {
  {"supershotgun",NULL,"weapon_supershotgun","progs/g_shot.mdl","$qc_double_shotgun"},
  {"nailgun","rogue:lava-nailgun","weapon_nailgun","progs/g_nail.mdl","$qc_nailgun"},
  {"supernailgun","rogue:lava-supernailgun","weapon_supernailgun","progs/g_nail2.mdl","$qc_super_nailgun"},
  {"grenadelauncher","rogue:multi-grenade","weapon_grenadelauncher","progs/g_rock.mdl","$qc_grenade_launcher"},
  {"rocketlauncher","rogue:multi-rocket","weapon_rocketlauncher","progs/g_rock2.mdl","$qc_rocket_launcher"},
  {"lightning","rogue:plasma","weapon_lightning","progs/g_light.mdl","$qc_thunderbolt"},
};
qa_q1mp_actor *qa_q1mp_toss_rogue_weapon(qa_q1mp_game *game, qa_q1mp_player_state *player) {
  const toss_weapon *def = NULL;
  for (size_t i = 0; i < 6; i++) if (qa_q1mp_streq(TOSS_WEAPONS[i].weapon, player->weapon) || qa_q1mp_streq(TOSS_WEAPONS[i].powered, player->weapon)) def = &TOSS_WEAPONS[i];
  qa_q1b_body body; bool has_body = qa_q1mp_body_read(game, player->actor.id, &body);
  if (game->base->options.deathmatch != 1 || qa_q1mp_teamplay(game) < 1 || !def || !has_body) return NULL;
  qa_q1mp_actor *item = game->base->create(game->base, def->classname);
  item->owner = QA_Q1MP_SOME(player->actor.id); qa_q1mp_set_model(item, def->model); item->movement = "bounce"; item->solid = "trigger";
  qa_vec3 forward = qa_q1mp_vectors(game, player->view_angles).forward;
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,16));
  p.has_velocity = true; p.velocity = qa_q1mp_vscale(qa_q1mp_aim(game, player->actor, forward), 500);
  p.has_bounds = true; p.bounds = (qa_bounds){{-16,-16,0},{16,16,56}}; qa_q1mp_patch(game, item, &p);
  char buf[96];
  (void)qa_q1mp_consume(game, player->actor, qa_q1mp_weapon_item(def->weapon, buf, sizeof buf), 1);
  if (def->powered) (void)qa_q1mp_consume(game, player->actor, qa_q1mp_weapon_item(def->powered, buf, sizeof buf), 1);
  item->touch = game->base->named.touch(game->base->named.ctx, item, "rogue:tossed-weapon-touch");
  game->base->schedule(game->base, item, 120, game->base->named.action(game->base->named.ctx, item, "SUB_Remove"));
  game->base->link(game->base, item);
  (void)game->base->select_weapon(game->base, player->actor, game->base->choose_best(game->base, player->actor));
  return item;
}
typedef struct toss_ctx { qa_q1mp_game *game; qa_q1mp_mission_pack_players *players; } toss_ctx;
static void tossed_touch(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *item, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; toss_ctx *c = vctx; qa_q1mp_game *game = c->game; (void)runtime;
  qa_q1mp_player_state *player = game->base->player(game->base, other); const toss_weapon *def = NULL;
  for (size_t i = 0; i < 6; i++) if (qa_q1mp_streq(TOSS_WEAPONS[i].classname, item->classname)) def = &TOSS_WEAPONS[i];
  if (!player || !def || (item->owner.has && qa_same_actor(item->owner.id, other) && item->next_think - game->base->time > 119)) return;
  qa_q1mp_mission_pickup_message(game, other, def->name);
  game->base->sound_owned(game->base, player->actor, "weapons/pkup.wav", QA_Q1MP_CH("item"), 1, 1);
  qa_q1mp_effect_a(game, "pickup", game->base->body(game->base, item).origin, QA_Q1MP_SOME(other), 0);
  char buf[96]; (void)qa_q1mp_give(game, player->actor, qa_q1mp_weapon_item(def->weapon, buf, sizeof buf), 1);
  game->base->remove(game->base, item);
  /* TS pickupRules weaponRank via q1b rules when present. */
  double def_rank = 12, cur_rank = 12;
  if (game->base->pickup_rules && game->base->pickup_rules->has_weapon_rank) {
    def_rank = game->base->pickup_rules->weapon_rank(game->base->pickup_rules->ctx, def->weapon);
    cur_rank = game->base->pickup_rules->weapon_rank(game->base->pickup_rules->ctx, player->weapon);
  }
  if (game->base->options.deathmatch == 0 || def_rank < cur_rank) (void)game->base->select_weapon(game->base, player->actor, def->weapon);
  qa_q1mp_players_enable_combos(c->players, player);
}
void qa_q1mp_register_rogue_toss_callbacks(qa_q1mp_game *game, qa_q1mp_mission_pack_players *players) {
  toss_ctx *c = malloc(sizeof *c); c->game = game; c->players = players;
  qa_q1b_handlers h = {0}; h.ctx = c; h.touch = tossed_touch;
  game->base->named.register_named(game->base->named.ctx, "rogue:tossed-weapon-touch", &h);
}
